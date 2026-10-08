/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// HDR and bloom of the QRhi renderer (task 24). With a tone curve chosen the
// view draws into floats, so lamps and glowing surfaces can exceed white;
// present() applies the exposure, adds bloom and maps the result through the
// curve. Bloom is built only from the glow the lit shaders write (emitted
// light), so sunlit walls, snow and glints never bloom: a downsample chain
// of 13-tap filters, then tent-filtered upsamples added back level by level
// (Jimenez, "Next generation post processing in Call of Duty", 2014).

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include <QDebug>
#include <algorithm>
#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/GLUU.h>

namespace {

// Smallest bloom level and the most levels: a wide but bounded halo.
constexpr int MinBloomSide = 8;
constexpr int MaxBloomLevels = 6;

// Glow splats: one emitter per instance, spread around its projected
// position by a tent R pixels wide each way, R its projected radius rounded
// to whole pixels (1 to 16). A tent of whole pixels sums to the same over the
// pixel grid wherever it falls, so the glow moves smoothly and never blinks;
// a lens drawn as pixels does, also a near one: seen nearly edge-on it is a
// sliver a pixel or two high. The total is the glow of the emitter seen as
// a sphere of its area and radiance (task 21): pi I (f/d)^2 in pixels,
// faded by fog. It hands over to the surface's own glow while the emitter's
// radius grows from 16 to 32 pixels (PbrShading.glsl).
constexpr int GlowSplatStride = 8 * sizeof(float);
constexpr int GlowSplatUniformBytes = 160;

const char *GlowSplatVertex = R"(#version 440
layout(location = 0) in vec4 emitter; // position, radius
layout(location = 1) in vec4 power;   // colour times intensity
layout(location = 0) out vec2 offset;
layout(location = 1) out vec3 glow;
layout(location = 2) flat out float tentArea;
layout(std140, binding = 0) uniform Splats {
    mat4 projection;
    mat4 fogMatrix;
    vec4 camera;   // position, focal length in pixels
    vec4 viewport; // width, height, fog lod, fog density
};

void main() {
    vec2 corner = vec2((gl_VertexIndex & 1) != 0 ? 1.0 : -1.0, (gl_VertexIndex & 2) != 0 ? 1.0 : -1.0);
    vec3 toEmitter = emitter.xyz - camera.xyz;
    float distance = max(length(toEmitter), 0.001);
    float pixels = emitter.w * camera.w / distance;
    float share = 1.0 - smoothstep(16.0, 32.0, pixels);
    float tent = clamp(floor(pixels + 0.5), 1.0, 16.0);
    tentArea = tent * tent;
    vec4 fogPosition = fogMatrix * vec4(emitter.xyz, 1.0);
    float fog = sqrt(fogPosition.x * fogPosition.x + fogPosition.z * fogPosition.z) / (viewport.z * 1.4);
    fog = abs(min(clamp(fog, 0.0, viewport.w), viewport.z));
    glow = power.rgb * (3.14159265 * camera.w * camera.w / (distance * distance)) * share * (1.0 - fog);
    offset = corner;
    // Depth at the emitter's front, so its own housing does not hide it.
    vec3 front = emitter.xyz - toEmitter / distance * min(emitter.w, distance * 0.5);
    vec4 clip = projection * vec4(front, 1.0);
    if (share <= 0.0 || clip.w <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    gl_Position = vec4(clip.xy + corner * tent * 2.0 / viewport.xy * clip.w, clip.zw);
}
)";

const char *GlowSplatFragment = R"(#version 440
layout(location = 0) in vec2 offset;
layout(location = 1) in vec3 glow;
layout(location = 2) flat in float tentArea;
layout(location = 0) out vec4 colorOut;
layout(location = 1) out vec4 ambientOut;
layout(location = 2) out vec4 glowOut;

void main() {
    float weight = max(1.0 - abs(offset.x), 0.0) * max(1.0 - abs(offset.y), 0.0);
    colorOut = vec4(0.0);
    ambientOut = vec4(0.0);
    glowOut = vec4(glow * weight / tentArea, 0.0);
}
)";

const char *BloomDownFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D source;

void main() {
    vec2 t = 1.0 / vec2(textureSize(source, 0));
    vec3 a = texture(source, uv + t * vec2(-2.0, 2.0)).rgb;
    vec3 b = texture(source, uv + t * vec2(0.0, 2.0)).rgb;
    vec3 c = texture(source, uv + t * vec2(2.0, 2.0)).rgb;
    vec3 d = texture(source, uv + t * vec2(-2.0, 0.0)).rgb;
    vec3 e = texture(source, uv).rgb;
    vec3 f = texture(source, uv + t * vec2(2.0, 0.0)).rgb;
    vec3 g = texture(source, uv + t * vec2(-2.0, -2.0)).rgb;
    vec3 h = texture(source, uv + t * vec2(0.0, -2.0)).rgb;
    vec3 i = texture(source, uv + t * vec2(2.0, -2.0)).rgb;
    vec3 j = texture(source, uv + t * vec2(-1.0, 1.0)).rgb;
    vec3 k = texture(source, uv + t * vec2(1.0, 1.0)).rgb;
    vec3 l = texture(source, uv + t * vec2(-1.0, -1.0)).rgb;
    vec3 m = texture(source, uv + t * vec2(1.0, -1.0)).rgb;
    vec3 result = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625
            + (j + k + l + m) * 0.125;
    fragColor = vec4(result, 1.0);
}
)";

const char *BloomUpFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D source;

void main() {
    vec2 t = 1.0 / vec2(textureSize(source, 0));
    vec3 sum = texture(source, uv + t * vec2(-1.0, -1.0)).rgb
            + 2.0 * texture(source, uv + t * vec2(0.0, -1.0)).rgb
            + texture(source, uv + t * vec2(1.0, -1.0)).rgb
            + 2.0 * texture(source, uv + t * vec2(-1.0, 0.0)).rgb
            + 4.0 * texture(source, uv).rgb
            + 2.0 * texture(source, uv + t * vec2(1.0, 0.0)).rgb
            + texture(source, uv + t * vec2(-1.0, 1.0)).rgb
            + 2.0 * texture(source, uv + t * vec2(0.0, 1.0)).rgb
            + texture(source, uv + t * vec2(1.0, 1.0)).rgb;
    fragColor = vec4(sum / 16.0, 1.0);
}
)";

}

// The final pass: the view to the frame. Off (curve 0, exposure 1, no
// bloom) copies the view as before; otherwise the view's display colour is
// taken to linear light, the bloom added, the exposure applied and the curve
// maps it back to display colour.
const char *RhiPresentFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D view;
layout(binding = 1) uniform sampler2D bloomTexture;
layout(std140, binding = 2) uniform Image {
    // Tone curve, exposure factor, bloom strength.
    vec4 settings;
};

vec3 toLinear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }
vec3 toDisplay(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }

// The display colour as it is up to the knee, then rolled off towards
// white with the hue kept: the usual look, with highlights compressed.
vec3 softShoulder(vec3 display) {
    const float knee = 0.85;
    float peak = max(display.r, max(display.g, display.b));
    if (peak <= knee)
        return display;
    float rolled = knee + (1.0 - knee) * (1.0 - exp(-(peak - knee) / (1.0 - knee)));
    return display * (rolled / peak);
}

// ACES filmic curve (Narkowicz fit), linear in and out.
vec3 aces(vec3 x) {
    x *= 0.6;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// AgX (Sobotka; the minimal fit by Wrensch): linear in, display out.
vec3 agx(vec3 linear) {
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                            0.0784335999999992, 0.878468636469772, 0.0784336,
                            0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                             -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                             -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;
    vec3 c = inset * linear;
    c = clamp(log2(max(c, vec3(1e-10))), minEv, maxEv);
    c = (c - minEv) / (maxEv - minEv);
    vec3 c2 = c * c;
    vec3 c4 = c2 * c2;
    c = 15.5 * c4 * c2 - 40.14 * c4 * c + 31.96 * c4 - 6.868 * c2 * c + 0.4298 * c2
            + 0.1191 * c - 0.00232;
    return outset * c;
}

void main() {
    vec3 colour = texture(view, uv).rgb;
    int curve = int(settings.x + 0.5);
    if (curve == 0 && settings.y == 1.0 && settings.z == 0.0) {
        fragColor = vec4(colour, 1.0);
        return;
    }
    vec3 linear = (toLinear(colour) + settings.z * texture(bloomTexture, uv).rgb) * settings.y;
    vec3 display;
    if (curve == 1)
        display = softShoulder(toDisplay(linear));
    else if (curve == 2)
        display = toDisplay(aces(linear));
    else if (curve == 3)
        display = agx(linear);
    else
        display = toDisplay(linear);
    fragColor = vec4(clamp(display, 0.0, 1.0), 1.0);
}
)";

QList<QByteArray> RhiRenderer::imageShaders() {
    return {RhiPresentFragment, BloomDownFragment, BloomUpFragment};
}

int RhiRenderer::toneMapping() const {
    return std::clamp(Game::toneMapping, 0, 3);
}

QRhiTexture::Format RhiRenderer::viewFormat() const {
    if (toneMapping() > 0 && rhi->isTextureFormatSupported(QRhiTexture::RGBA16F))
        return QRhiTexture::RGBA16F;
    return QRhiTexture::RGBA8;
}

bool RhiRenderer::bloomEnabled() const {
    return Game::bloomStrength > 0.0f && rhi->isTextureFormatSupported(QRhiTexture::RGBA16F);
}

QRhiTexture *RhiRenderer::renderBloom(QRhiCommandBuffer *cb) {
    if (!bloomEnabled() || view.color3 == nullptr || cb == nullptr)
        return nullptr;
    Bloom &b = bloom;
    if (b.viewSize != view.size || b.levels.empty()) {
        releaseBloom();
        b.viewSize = view.size;
        b.linear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                   QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        b.linear->create();
        QSize size(std::max(1, view.size.width() / 2), std::max(1, view.size.height() / 2));
        while (int(b.levels.size()) < MaxBloomLevels
               && std::min(size.width(), size.height()) >= MinBloomSide) {
            QRhiTexture *level = rhi->newTexture(QRhiTexture::RGBA16F, size, 1, QRhiTexture::RenderTarget);
            if (!level->create()) {
                delete level;
                break;
            }
            b.levels.push_back(level);
            QRhiTextureRenderTarget *down = rhi->newTextureRenderTarget({QRhiColorAttachment(level)});
            QRhiTextureRenderTarget *up = rhi->newTextureRenderTarget(
                        {QRhiColorAttachment(level)}, QRhiTextureRenderTarget::PreserveColorContents);
            QRhiRenderPassDescriptor *downPass = down->newCompatibleRenderPassDescriptor();
            QRhiRenderPassDescriptor *upPass = up->newCompatibleRenderPassDescriptor();
            down->setRenderPassDescriptor(downPass);
            up->setRenderPassDescriptor(upPass);
            down->create();
            up->create();
            b.downTargets.push_back(down);
            b.upTargets.push_back(up);
            b.passes.push_back(downPass);
            b.passes.push_back(upPass);
            size = QSize(std::max(1, size.width() / 2), std::max(1, size.height() / 2));
        }
        if (b.levels.empty()) {
            releaseBloom();
            return nullptr;
        }
        const auto fragment = QRhiShaderResourceBinding::FragmentStage;
        for (size_t i = 0; i < b.levels.size(); ++i) {
            QRhiTexture *source = i == 0 ? view.color3 : b.levels[i - 1];
            QRhiShaderResourceBindings *down = rhi->newShaderResourceBindings();
            down->setBindings({QRhiShaderResourceBinding::sampledTexture(0, fragment, source, b.linear)});
            down->create();
            b.downBindings.push_back(down);
            QRhiShaderResourceBindings *up = rhi->newShaderResourceBindings();
            if (i + 1 < b.levels.size())
                up->setBindings({QRhiShaderResourceBinding::sampledTexture(0, fragment, b.levels[i + 1], b.linear)});
            else
                up->setBindings({QRhiShaderResourceBinding::sampledTexture(0, fragment, b.levels[i], b.linear)});
            up->create();
            b.upBindings.push_back(up);
        }
        static QShader vertex, downShader, upShader;
        if (!vertex.isValid()) {
            vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
            downShader = bakeInline(BloomDownFragment, QShader::FragmentStage, rhi);
            upShader = bakeInline(BloomUpFragment, QShader::FragmentStage, rhi);
        }
        b.down = rhi->newGraphicsPipeline();
        b.down->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, downShader}});
        b.down->setShaderResourceBindings(b.downBindings[0]);
        b.down->setRenderPassDescriptor(b.passes[0]);
        b.up = rhi->newGraphicsPipeline();
        b.up->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, upShader}});
        // Each level adds the wider levels below it.
        QRhiGraphicsPipeline::TargetBlend add;
        add.enable = true;
        add.srcColor = QRhiGraphicsPipeline::One;
        add.dstColor = QRhiGraphicsPipeline::One;
        add.srcAlpha = QRhiGraphicsPipeline::Zero;
        add.dstAlpha = QRhiGraphicsPipeline::One;
        b.up->setTargetBlends({add});
        b.up->setShaderResourceBindings(b.upBindings[0]);
        b.up->setRenderPassDescriptor(b.passes[1]);
        if (!b.down->create() || !b.up->create()) {
            releaseBloom();
            return nullptr;
        }
    }
    auto fullScreen = [cb](QRhiTextureRenderTarget *target, QRhiGraphicsPipeline *pipeline,
                           QRhiShaderResourceBindings *bindings, const QSize &size) {
        cb->beginPass(target, Qt::black, {1.0f, 0});
        cb->setGraphicsPipeline(pipeline);
        cb->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
        cb->setShaderResources(bindings);
        cb->draw(3);
        cb->endPass();
    };
    for (size_t i = 0; i < b.levels.size(); ++i)
        fullScreen(b.downTargets[i], b.down, b.downBindings[i], b.levels[i]->pixelSize());
    for (size_t i = b.levels.size() - 1; i-- > 0; )
        fullScreen(b.upTargets[i], b.up, b.upBindings[i], b.levels[i]->pixelSize());
    return b.levels[0];
}

void RhiRenderer::releaseBloom() {
    Bloom &b = bloom;
    delete b.down;
    delete b.up;
    for (QRhiShaderResourceBindings *set : b.downBindings)
        delete set;
    for (QRhiShaderResourceBindings *set : b.upBindings)
        delete set;
    for (QRhiTextureRenderTarget *target : b.downTargets)
        delete target;
    for (QRhiTextureRenderTarget *target : b.upTargets)
        delete target;
    for (QRhiRenderPassDescriptor *pass : b.passes)
        delete pass;
    for (QRhiTexture *level : b.levels)
        delete level;
    delete b.linear;
    b = Bloom();
    // The present pass may hold the first level.
    if (presentBloomTexture != nullptr) {
        delete presentBindings;
        presentBindings = nullptr;
        presentBloomTexture = nullptr;
    }
}

QList<QByteArray> RhiRenderer::glowSplatShaders() {
    return {GlowSplatVertex, GlowSplatFragment};
}

// Once a frame, with the lights: the emissive emitters of the queue at
// their own power, without exposure or gain, as the surfaces' own glow has
// none, but following the lamps' light (task 21) as that glow does: faded
// by day, none with local lights off.
void RhiRenderer::gatherGlowSplats() {
    if (glowSplats.gathered)
        return;
    glowSplats.gathered = true;
    glowSplats.emitters.clear();
    if (bloomEnabled() && Game::localLightsEnabled)
        gatherLights(glowSplats.emitters, 0.0f, gluu->localLightAdaptation, true);
}

// At the end of the main view: its scene band's projection, fog and
// viewport, for the splats drawn with the view's next pass.
void RhiRenderer::prepareGlowSplats() {
    GlowSplats &g = glowSplats;
    if (secondaryView || currentTarget != TARGET_VIEW || g.emitters.empty() || !bloomEnabled()
            || view.color3 == nullptr)
        return;
    const QMatrix4x4 correction = targetCorrection();
    Mat4::multiply(g.projection, const_cast<float *>(correction.constData()), gluu->pMatrix);
    std::copy(gluu->fMatrix, gluu->fMatrix + 16, g.fogMatrix);
    std::copy(viewPosition, viewPosition + 3, g.camera);
    const float w = viewportRect[2] > 0 ? float(viewportRect[2]) : float(view.size.width());
    const float h = viewportRect[3] > 0 ? float(viewportRect[3]) : float(view.size.height());
    g.viewport = QRhiViewport(float(viewportRect[0]), float(viewportRect[1]), w, h,
                              depthRange[0], depthRange[1]);
    g.focal = sceneProjection[1] * h * 0.5f;
    g.lod = fogLodOverride ? fogLod : Game::objectLod;
    g.fogDensity = gluu->fogDensity;
    g.pending = true;
}

bool RhiRenderer::uploadGlowSplats(QRhiResourceUpdateBatch *batch, QRhiRenderPassDescriptor *pass) {
    GlowSplats &g = glowSplats;
    const quint32 count = quint32(g.emitters.size());
    if (batch == nullptr || pass == nullptr || count == 0)
        return false;
    const quint32 bytes = count * GlowSplatStride;
    if (g.instances == nullptr || g.capacity < bytes) {
        delete g.instances;
        g.capacity = std::max<quint32>(4096, bytes + bytes / 2);
        g.instances = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, g.capacity);
        if (!g.instances->create()) {
            delete g.instances;
            g.instances = nullptr;
            g.capacity = 0;
            return false;
        }
    }
    std::vector<float> data(size_t(count) * 8);
    for (quint32 i = 0; i < count; ++i) {
        const LightGrid::Light &e = g.emitters[i];
        float *out = data.data() + i * 8;
        std::copy(e.position, e.position + 3, out);
        out[3] = e.radius;
        std::copy(e.color, e.color + 3, out + 4);
        out[7] = 0.0f;
    }
    batch->updateDynamicBuffer(g.instances, 0, bytes, data.data());
    if (g.uniforms == nullptr) {
        g.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, GlowSplatUniformBytes);
        if (!g.uniforms->create()) {
            delete g.uniforms;
            g.uniforms = nullptr;
            return false;
        }
    }
    float block[GlowSplatUniformBytes / sizeof(float)];
    std::copy(g.projection, g.projection + 16, block);
    std::copy(g.fogMatrix, g.fogMatrix + 16, block + 16);
    std::copy(g.camera, g.camera + 3, block + 32);
    block[35] = g.focal;
    block[36] = g.viewport.viewport()[2];
    block[37] = g.viewport.viewport()[3];
    block[38] = g.lod;
    block[39] = g.fogDensity;
    batch->updateDynamicBuffer(g.uniforms, 0, GlowSplatUniformBytes, block);
    if (g.bindings == nullptr) {
        g.bindings = rhi->newShaderResourceBindings();
        g.bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, g.uniforms)});
        g.bindings->create();
    }
    if (g.pipeline == nullptr || g.pipelinePass != pass) {
        delete g.pipeline;
        static QShader vertex, fragment;
        if (!vertex.isValid()) {
            vertex = bakeInline(GlowSplatVertex, QShader::VertexStage, rhi);
            fragment = bakeInline(GlowSplatFragment, QShader::FragmentStage, rhi);
        }
        g.pipeline = rhi->newGraphicsPipeline();
        g.pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
        QRhiVertexInputLayout layout;
        layout.setBindings({{GlowSplatStride, QRhiVertexInputBinding::PerInstance}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float4, 0},
                              {0, 1, QRhiVertexInputAttribute::Float4, 4 * sizeof(float)}});
        g.pipeline->setVertexInputLayout(layout);
        g.pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        // Behind the scene's surfaces they are hidden; they add to the glow
        // only, leaving the colour and the ambient share.
        g.pipeline->setDepthTest(true);
        g.pipeline->setDepthWrite(false);
        g.pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        QRhiGraphicsPipeline::TargetBlend untouched;
        untouched.colorWrite = {};
        QRhiGraphicsPipeline::TargetBlend add;
        add.enable = true;
        add.srcColor = QRhiGraphicsPipeline::One;
        add.dstColor = QRhiGraphicsPipeline::One;
        add.srcAlpha = QRhiGraphicsPipeline::Zero;
        add.dstAlpha = QRhiGraphicsPipeline::One;
        add.colorWrite = QRhiGraphicsPipeline::R | QRhiGraphicsPipeline::G | QRhiGraphicsPipeline::B;
        g.pipeline->setTargetBlends({untouched, untouched, add});
        g.pipeline->setShaderResourceBindings(g.bindings);
        g.pipeline->setRenderPassDescriptor(pass);
        g.pipelinePass = pass;
        if (!g.pipeline->create()) {
            qWarning() << "QRhi renderer: glow splat pipeline creation failed";
            delete g.pipeline;
            g.pipeline = nullptr;
            return false;
        }
    }
    g.count = count;
    return true;
}

void RhiRenderer::drawGlowSplats(QRhiCommandBuffer *cb) {
    GlowSplats &g = glowSplats;
    g.pending = false;
    if (g.pipeline == nullptr || g.count == 0)
        return;
    cb->setGraphicsPipeline(g.pipeline);
    cb->setViewport(g.viewport);
    cb->setShaderResources(g.bindings);
    const QRhiCommandBuffer::VertexInput input(g.instances, 0);
    cb->setVertexInput(0, 1, &input);
    cb->draw(4, g.count);
}

void RhiRenderer::releaseGlowSplats() {
    GlowSplats &g = glowSplats;
    delete g.pipeline;
    delete g.bindings;
    delete g.uniforms;
    delete g.instances;
    g = GlowSplats();
}
