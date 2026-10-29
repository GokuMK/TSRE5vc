/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Motion blur (task renderer 38): fast motion streaks as a camera's exposure
// shows it, from the view's velocity (each pixel's motion on screen since
// last frame, the whole of it, which TAA will take too) less the camera's
// own movement where it holds still: in a vehicle it rides, the vehicle's
// motion blurs the world and its head's turning does not; not riding, the
// world stays sharp and moving things blur by their own motion.
// After McGuire et al. 2012, "A Reconstruction Filter for Plausible Motion
// Blur", with Jimenez's half of the samples along the pixel's own motion
// (Call of Duty: Advanced Warfare, 2014). Five passes, all full-screen:
// the blur's vector per pixel, its longest in 8 x 8 blocks, in tiles as
// long as the longest blur, in each tile's 3 x 3 tiles, then the gather of
// the colour and the glow (bloom's source) along them.

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include "RhiRenderSurface.h"
#include "RhiShaderSource.h"
#include <algorithm>
#include <cmath>
#include <QMatrix4x4>
#include <tsre/math3d/GLMatrix.h>

namespace {

struct MotionBlurParameters {
    float toPrevious[16];   // this frame's clip space to last frame's (OpenGL's)
    float toReference[16];  // to the reference's (the camera held still)
    float size[4];          // the view's width, height and their inverses
    float params[4];        // exposure / frame time, longest blur (px), tile (px), y up in the framebuffer
    float tiles[4];         // tiles across, down, the near and far planes
    float sides[4];         // texels a block takes, blocks a tile takes
    float slice[4];         // the scene band's slice of the depth range
};

#define MOTION_BLOCK R"(#version 440
layout(location = 0) in vec2 uv;
layout(std140, binding = 0) uniform MotionBlur {
    mat4 toPrevious;
    mat4 toReference;
    vec4 size;
    vec4 params;
    vec4 tiles;
    vec4 sides;
    vec4 slice;
};
// The scene band's depth from the depth buffer's (its slice of the range;
// the distant band and the sky behind it, its far plane).
float sceneDepth(float d) {
    return clamp((d - slice.x) / max(slice.y - slice.x, 1e-6), 0.0, 1.0);
}
// Its distance (m) from the near and far planes.
float linearDepth(float d) {
    float z = sceneDepth(d) * 2.0 - 1.0;
    return 2.0 * tiles.z * tiles.w / max(tiles.w + tiles.z - z * (tiles.w - tiles.z), 1e-6);
}
)"

// The blur's vector per pixel (pixels over the exposure): the velocity less
// the camera's own movement in its reference, found from the pixel's depth
// (where last frame's view and the reference's put its point), limited in
// length.
const char *BlurFragment = MOTION_BLOCK R"(
layout(location = 0) out vec4 result;
layout(binding = 1) uniform sampler2D velocityMap;
layout(binding = 2) uniform sampler2D depthMap;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 v = (texelFetch(velocityMap, p, 0).xy * 2.0 - 1.0) * size.xy;
    float raw = texelFetch(depthMap, p, 0).r;
    float d = sceneDepth(raw);
    vec2 ndc = (vec2(p) + 0.5) * size.zw * 2.0 - 1.0;
    if (params.w < 0.5)
        ndc.y = -ndc.y;
    vec4 c = vec4(ndc, d * 2.0 - 1.0, 1.0);
    vec4 a = toPrevious * c;
    vec4 b = toReference * c;
    vec2 own = vec2(0.0);
    // Beyond the scene band (the distant band, the sky and its clouds, their
    // velocity not this projection's or none): the static world, moved by
    // the reference alone.
    if (raw >= slice.y) {
        v = vec2(0.0);
        a = c;
    }
    if (a.w > 1e-5 && b.w > 1e-5)
        own = (a.xy / a.w - b.xy / b.w) * 0.5 * size.xy;
    if (params.w < 0.5)
        own.y = -own.y;
    vec2 blur = (v + own) * params.x;
    float len = length(blur);
    if (len > params.y) {
        blur *= params.y / len;
        len = params.y;
    }
    // With the pixel's distance and its blur's radius (half its length
    // either way), what the gather's samples read.
    result = vec4(blur, linearDepth(raw), max(len * 0.5, 0.5));
}
)";

// The longest of 8 x 8 texels, and of tiles x tiles blocks.
const char *BlockFragment = MOTION_BLOCK R"(
layout(location = 0) out vec4 result;
layout(binding = 1) uniform sampler2D source;
void main() {
    // Blocks: 8 x 8, a constant the loops unroll by (fetched together: a
    // count from the uniforms ran them one by one, 0.5 ms more on the
    // Steam Deck); tiles: blocks a tile takes.
    int n = SIDE;
    ivec2 limit = textureSize(source, 0) - 1;
    ivec2 base = ivec2(gl_FragCoord.xy) * n;
    vec2 best = vec2(0.0);
    float bestLength = 0.0;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            vec2 b = texelFetch(source, min(base + ivec2(x, y), limit), 0).xy;
            float l = dot(b, b);
            if (l > bestLength) {
                bestLength = l;
                best = b;
            }
        }
    result = vec4(best, 0.0, 0.0);
}
)";

// The longest in each tile's 3 x 3 tiles: what may blur into it.
const char *NeighbourFragment = MOTION_BLOCK R"(
layout(location = 0) out vec4 result;
layout(binding = 1) uniform sampler2D source;
void main() {
    ivec2 limit = textureSize(source, 0) - 1;
    ivec2 base = ivec2(gl_FragCoord.xy);
    vec2 best = vec2(0.0);
    float bestLength = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 b = texelFetch(source, clamp(base + ivec2(x, y), ivec2(0), limit), 0).xy;
            float l = dot(b, b);
            if (l > bestLength) {
                bestLength = l;
                best = b;
            }
        }
    result = vec4(best, 0.0, 0.0);
}
)";

// The gather: samples along the neighbourhood's longest blur and the
// pixel's own, each weighted by what can be seen of it there: a sample in
// front blurs over the pixel as far as its own blur reaches, one behind
// shows through the pixel's blur, two blurred ones mix (McGuire's cone and
// cylinder weights); offsets jittered per pixel.
const char *GatherFragment = MOTION_BLOCK R"(
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 glow;
layout(binding = 1) uniform sampler2D colorMap;
layout(binding = 2) uniform sampler2D glowMap;
layout(binding = 3) uniform sampler2D blurMap;
layout(binding = 5) uniform sampler2D neighbourMap;
layout(binding = 6) uniform sampler2D velocityMap;

// 1 where a lies in front of b (or as far), fading over a few per cent of
// the depth behind it.
float inFront(float za, float zb) {
    return clamp(1.0 - (za - zb) / max(0.5, 0.03 * zb), 0.0, 1.0);
}
float cone(float distance, float radius) {
    return clamp(1.0 - distance / radius, 0.0, 1.0);
}
float cylinder(float distance, float radius) {
    return 1.0 - smoothstep(0.95 * radius, 1.05 * radius, distance);
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 limit = ivec2(size.xy) - 1;
    ivec2 tile = min(p / int(params.z + 0.5), ivec2(tiles.xy) - 1);
    vec2 vn = texelFetch(neighbourMap, tile, 0).xy;
    // TSRE_MOTION_DEBUG: the velocity (pixels a frame), the blur, the
    // neighbourhood's longest, as colours (red x, green y, 32 px full).
    if (sides.z > 0.5) {
        vec2 shown = sides.z < 1.5 ? (texelFetch(velocityMap, p, 0).xy * 2.0 - 1.0) * size.xy
                : sides.z < 2.5 ? texelFetch(blurMap, p, 0).xy : vn;
        color = vec4(abs(shown) / 32.0, shown.x < 0.0 || shown.y < 0.0 ? 0.25 : 0.0, 1.0);
        glow = vec4(0.0);
        return;
    }
    // Still around: the view's own, unwritten.
    if (dot(vn, vn) < 0.25)
        discard;
    vec4 c0 = texelFetch(colorMap, p, 0);
    vec4 g0 = texelFetch(glowMap, p, 0);
    vec4 bc = texelFetch(blurMap, p, 0);
    vec2 vc = bc.xy;
    float zc = bc.z;
    float rc = bc.w;
    vec2 own = length(vc) > 1.0 ? vc : vn;
    // Interleaved gradient noise: the samples' offset along the blur.
    float jitter = fract(52.9829189 * fract(dot(vec2(p), vec2(0.06711056, 0.00583715)))) - 0.5;
    float centre = 1.0 / rc;
    vec4 sumColor = c0 * centre;
    vec4 sumGlow = g0 * centre;
    float total = centre;
    // About one sample per 3 pixels of the neighbourhood's longest blur
    // (the same in its tile), 4 to 12, an even count: half along it, half
    // along the pixel's own.
    int samples = clamp(2 * int(ceil(length(vn) / 6.0)), 4, 12);
    for (int i = 0; i < samples; ++i) {
        float t = mix(-1.0, 1.0, (float(i) + 0.5 + jitter) / float(samples));
        vec2 offset = ((i & 1) == 0 ? vn : own) * 0.5 * t;
        float distance = length(offset);
        ivec2 q = clamp(p + ivec2(round(offset)), ivec2(0), limit);
        vec4 bs = texelFetch(blurMap, q, 0);
        float zs = bs.z;
        float rs = bs.w;
        float weight = inFront(zs, zc) * cone(distance, rs) + inFront(zc, zs) * cone(distance, rc)
                + cylinder(distance, rs) * cylinder(distance, rc) * 2.0;
        sumColor += texelFetch(colorMap, q, 0) * weight;
        sumGlow += texelFetch(glowMap, q, 0) * weight;
        total += weight;
    }
    color = sumColor / total;
    glow = sumGlow / total;
}
)";

#undef MOTION_BLOCK

} // namespace

QList<QByteArray> RhiRenderer::motionBlurShaders() {
    QByteArray blocks(BlockFragment), tiles(BlockFragment);
    blocks.replace("#version 440\n", "#version 440\n#define SIDE 8\n");
    tiles.replace("#version 440\n", "#version 440\n#define SIDE int(sides.y + 0.5)\n");
    return {QByteArray(BlurFragment), blocks, tiles, QByteArray(NeighbourFragment), QByteArray(GatherFragment)};
}

QRhiTexture::Format RhiRenderer::velocityFormat() const {
    return rhi->isTextureFormatSupported(QRhiTexture::RG16, QRhiTexture::RenderTarget) ? QRhiTexture::RG16
                                                                                       : QRhiTexture::RGBA16F;
}

bool RhiRenderer::tracksMotion() const {
    return motionExposure > 0 && viewMotionUsed;
}

void RhiRenderer::setViewMotion(const ViewMotion &motion) {
    viewMotion = motion;
    if (motionExposure <= 0)
        return;
    viewMotionUsed = true;
    // The table of moving instances' last-frame matrices: there before the
    // frame's draws take it.
    if (motionTable == nullptr) {
        motionTable = rhi->newTexture(QRhiTexture::RGBA32F,
                                      QSize(4 * RhiShaderSource::MotionMatricesPerRow, MotionTableRows));
        if (!motionTable->create()) {
            delete motionTable;
            motionTable = nullptr;
        }
    }
}

void RhiRenderer::uploadMotionTable(QRhiResourceUpdateBatch *batch) {
    const std::vector<std::array<float, 16>> &table = motionMatrices();
    if (!tracksMotion() || motionTable == nullptr || table.empty() || batch == nullptr)
        return;
    const int perRow = RhiShaderSource::MotionMatricesPerRow;
    const int count = std::min(int(table.size()), MotionTableRows * perRow);
    const int rows = (count + perRow - 1) / perRow;
    QByteArray bytes(rows * perRow * 4 * 4 * int(sizeof(float)), 0);
    float *out = reinterpret_cast<float *>(bytes.data());
    for (int i = 0; i < count; ++i)
        std::copy(table[size_t(i)].begin(), table[size_t(i)].end(), out + size_t(i) * 16);
    QRhiTextureSubresourceUploadDescription description(bytes);
    description.setSourceSize(QSize(4 * perRow, rows));
    description.setDestinationTopLeft(QPoint(0, 0));
    batch->uploadTexture(motionTable, QRhiTextureUploadEntry(0, 0, description));
}

void RhiRenderer::releaseMotionBlur() {
    MotionBlurResources &m = motionBlur;
    for (QRhiGraphicsPipeline *p : m.pipelines)
        delete p;
    for (QRhiShaderResourceBindings *b : m.bindings)
        delete b;
    for (QRhiTextureRenderTarget *t : m.targets)
        delete t;
    for (QRhiRenderPassDescriptor *d : m.passes)
        delete d;
    delete m.blur;
    delete m.blocks;
    delete m.tiles;
    delete m.neighbours;
    delete m.colorCopy;
    delete m.glowCopy;
    delete m.uniforms;
    m = MotionBlurResources();
}

void RhiRenderer::applyMotionBlur() {
    if (motionBlurApplied || secondaryView || currentTarget != TARGET_VIEW || !view.valid() || plainImage)
        return;
    motionBlurApplied = true;
    if (!tracksMotion() || !viewMotion.valid || view.color4 == nullptr || view.color3 == nullptr)
        return;
    RhiRenderSurface *surfaceNow = surface();
    if (surfaceNow == nullptr || surfaceNow->frame().commandBuffer == nullptr)
        return;
    flushTarget();
    MotionBlurResources &m = motionBlur;
    const QSize size = view.size;
    // The longest blur: 3 % of the view's width; tiles of it, in blocks of 8.
    const float longest = std::max(8.0f, 0.03f * float(size.width()));
    const int blocksPerTile = std::max(1, int(std::ceil(longest / 8.0f)));
    const int tile = 8 * blocksPerTile;
    const QSize blocksSize((size.width() + 7) / 8, (size.height() + 7) / 8);
    const QSize tilesSize((size.width() + tile - 1) / tile, (size.height() + tile - 1) / tile);
    if (m.viewColor != view.color || m.viewGlow != view.color3 || m.size != size) {
        releaseMotionBlur();
        m.viewColor = view.color;
        m.viewGlow = view.color3;
        m.size = size;
        auto texture = [this](QRhiTexture::Format format, const QSize &s, QRhiTexture::Flags flags) {
            QRhiTexture *t = rhi->newTexture(format, s, 1, flags);
            if (!t->create()) {
                delete t;
                return static_cast<QRhiTexture *>(nullptr);
            }
            return t;
        };
        m.blur = texture(QRhiTexture::RGBA16F, size, QRhiTexture::RenderTarget);
        m.blocks = texture(QRhiTexture::RGBA16F, blocksSize, QRhiTexture::RenderTarget);
        m.tiles = texture(QRhiTexture::RGBA16F, tilesSize, QRhiTexture::RenderTarget);
        m.neighbours = texture(QRhiTexture::RGBA16F, tilesSize, QRhiTexture::RenderTarget);
        m.colorCopy = texture(view.color->format(), size, {});
        m.glowCopy = texture(view.color3->format(), size, {});
        m.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(MotionBlurParameters));
        if (m.blur == nullptr || m.blocks == nullptr || m.tiles == nullptr || m.neighbours == nullptr
                || m.colorCopy == nullptr || m.glowCopy == nullptr || !m.uniforms->create()) {
            releaseMotionBlur();
            m.viewColor = view.color;
            m.viewGlow = view.color3;
            m.size = size;
            m.failed = true;
            return;
        }
        // The block shader twice: texels to blocks, blocks to tiles (one
        // uniform update a frame: Dynamic buffers take the frame's last).
        static QShader vertex, shaders[5];
        if (!vertex.isValid()) {
            vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
            const QList<QByteArray> sources = motionBlurShaders();
            for (int i = 0; i < 5; ++i)
                shaders[i] = bakeInline(sources[i].constData(), QShader::FragmentStage, rhi);
        }
        const auto fragment = QRhiShaderResourceBinding::FragmentStage;
        const auto stages = QRhiShaderResourceBinding::VertexStage | fragment;
        QRhiSampler *nearest = sampler(false, true, true);
        // Passes: the blur, blocks, tiles, neighbours, the gather.
        QRhiTexture *outputs[5] = {m.blur, m.blocks, m.tiles, m.neighbours, nullptr};
        const int programs[5] = {0, 1, 2, 3, 4};
        for (int pass = 0; pass < 5; ++pass) {
            QRhiTextureRenderTarget *target;
            if (pass == 4) {
                QRhiTextureRenderTargetDescription description;
                description.setColorAttachments({QRhiColorAttachment(view.color),
                                                 QRhiColorAttachment(view.color3)});
                // Still pixels are left as they are (discarded), not cleared.
                target = rhi->newTextureRenderTarget(description, QRhiTextureRenderTarget::PreserveColorContents);
            } else {
                target = rhi->newTextureRenderTarget({QRhiColorAttachment(outputs[pass])});
            }
            m.passes[pass] = target->newCompatibleRenderPassDescriptor();
            target->setRenderPassDescriptor(m.passes[pass]);
            target->create();
            m.targets[pass] = target;
            QRhiShaderResourceBindings *b = rhi->newShaderResourceBindings();
            QVarLengthArray<QRhiShaderResourceBinding, 6> list;
            list.append(QRhiShaderResourceBinding::uniformBuffer(0, stages, m.uniforms));
            switch (pass) {
            case 0:
                list.append(QRhiShaderResourceBinding::sampledTexture(1, fragment, view.color4, nearest));
                list.append(QRhiShaderResourceBinding::sampledTexture(2, fragment, view.depth, nearest));
                break;
            case 1:
                list.append(QRhiShaderResourceBinding::sampledTexture(1, fragment, m.blur, nearest));
                break;
            case 2:
                list.append(QRhiShaderResourceBinding::sampledTexture(1, fragment, m.blocks, nearest));
                break;
            case 3:
                list.append(QRhiShaderResourceBinding::sampledTexture(1, fragment, m.tiles, nearest));
                break;
            default:
                list.append(QRhiShaderResourceBinding::sampledTexture(1, fragment, m.colorCopy, nearest));
                list.append(QRhiShaderResourceBinding::sampledTexture(2, fragment, m.glowCopy, nearest));
                list.append(QRhiShaderResourceBinding::sampledTexture(3, fragment, m.blur, nearest));
                list.append(QRhiShaderResourceBinding::sampledTexture(5, fragment, m.neighbours, nearest));
                list.append(QRhiShaderResourceBinding::sampledTexture(6, fragment, view.color4, nearest));
                break;
            }
            b->setBindings(list.cbegin(), list.cend());
            b->create();
            m.bindings[pass] = b;
            QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
            ps->setShaderStages({{QRhiShaderStage::Vertex, vertex},
                                 {QRhiShaderStage::Fragment, shaders[programs[pass]]}});
            ps->setVertexInputLayout(QRhiVertexInputLayout());
            if (pass == 4)
                ps->setTargetBlends({QRhiGraphicsPipeline::TargetBlend(), QRhiGraphicsPipeline::TargetBlend()});
            ps->setShaderResourceBindings(b);
            ps->setRenderPassDescriptor(m.passes[pass]);
            if (!ps->create()) {
                delete ps;
                ps = nullptr;
                m.failed = true;
            }
            m.pipelines[pass] = ps;
        }
    }
    if (m.failed)
        return;

    // This frame's clip space to last frame's and to the reference's: the
    // main view's view-projection P, the camera's movements W (Renderer::
    // ViewMotion): P W P^-1.
    MotionBlurParameters p = {};
    float inverse[16];
    {
        // Mat4 has no inverse; QMatrix4x4 reads its array row by row.
        const QMatrix4x4 q = QMatrix4x4(viewMotion.viewProjection).transposed().inverted();
        std::copy(q.constData(), q.constData() + 16, inverse);
    }
    float step[16];
    Mat4::multiply(step, viewMotion.viewProjection, viewMotion.world);
    Mat4::multiply(p.toPrevious, step, inverse);
    Mat4::multiply(step, viewMotion.viewProjection, viewMotion.reference);
    Mat4::multiply(p.toReference, step, inverse);
    p.size[0] = float(size.width());
    p.size[1] = float(size.height());
    p.size[2] = 1.0f / p.size[0];
    p.size[3] = 1.0f / p.size[1];
    // The exposure's share of the frame's time: the same blur at any frame
    // rate.
    const float frame = std::clamp(viewMotion.frameSeconds, 1.0f / 480.0f, 0.5f);
    p.params[0] = (1.0f / float(motionExposure)) / frame;
    p.params[1] = longest;
    p.params[2] = float(tile);
    p.params[3] = rhi->isYUpInFramebuffer() ? 1.0f : 0.0f;
    p.tiles[0] = float(tilesSize.width());
    p.tiles[1] = float(tilesSize.height());
    p.tiles[2] = viewMotion.nearPlane;
    p.tiles[3] = viewMotion.farPlane;
    p.sides[0] = 8.0f;
    p.sides[1] = float(blocksPerTile);
    static const int debug = qEnvironmentVariableIntValue("TSRE_MOTION_DEBUG");
    p.sides[2] = float(debug);
    p.slice[0] = sceneDepthRange[0];
    p.slice[1] = sceneDepthRange[1];

    QRhiCommandBuffer *cb = surfaceNow->frame().commandBuffer;
    auto run = [&](int pass, const QSize &passSize, QRhiResourceUpdateBatch *batch) {
        cb->beginPass(m.targets[pass], Qt::black, {1.0f, 0}, batch);
        cb->setGraphicsPipeline(m.pipelines[pass]);
        cb->setViewport(QRhiViewport(0, 0, float(passSize.width()), float(passSize.height())));
        cb->setShaderResources(m.bindings[pass]);
        cb->draw(3);
        cb->endPass();
    };
    QRhiResourceUpdateBatch *first = rhi->nextResourceUpdateBatch();
    first->updateDynamicBuffer(m.uniforms, 0, sizeof(p), &p);
    run(0, size, first);
    run(1, blocksSize, nullptr);
    run(2, tilesSize, nullptr);
    run(3, tilesSize, nullptr);
    QRhiResourceUpdateBatch *copies = rhi->nextResourceUpdateBatch();
    copies->copyTexture(m.colorCopy, view.color);
    copies->copyTexture(m.glowCopy, view.color3);
    run(4, size, copies);
}
