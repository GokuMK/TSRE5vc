/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "RhiRenderer.h"
#include "RhiContext.h"
#include "RhiRenderSurface.h"
#include "RhiShaderSource.h"
#include "RhiTextures.h"
#include <QDebug>
#include <algorithm>
#include <cstring>
#include <rhi/qshaderbaker.h>
#include <rhi/qshaderdescription.h>
#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderStats.h>
#include <tsre/renderer/WaterNormalMap.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>

// A program variant: its baked shaders and what their reflection says about
// the uniform block, the samplers and the vertex inputs.
struct RhiProgram {
    enum Kind {MAIN, TERRAIN, UNLIT, PBR, WATER, SELECTION, SHADOW, KIND_COUNT};
    Kind kind = MAIN;
    const RhiContext::Program *source = nullptr;
    struct Member {
        int offset = 0;
        int size = 0;
        int arrayStride = 0;
    };
    QHash<QByteArray, Member> members;
    int blockSize = 0;
    std::vector<char> block;
    // 0 2D, 1 2D array, 2 cube, 3 2D shadow (depth compare).
    struct Sampler {
        int binding = 0;
        int type = 0;
    };
    std::vector<Sampler> samplers;
    bool terrainPatches = false;
    QVector<QShaderDescription::InOutVariable> inputs;
    // A resource set the pipelines are created against.
    QRhiShaderResourceBindings *layout = nullptr;

    bool valid() const { return source != nullptr && source->valid(); }
    void set(const char *name, const void *data, int bytes) {
        auto found = members.constFind(QByteArray::fromRawData(name, int(std::strlen(name))));
        if (found == members.constEnd())
            return;
        std::memcpy(block.data() + found->offset, data, size_t(std::min(bytes, found->size)));
    }
    void setFloat(const char *name, float value) { set(name, &value, 4); }
    void setInt(const char *name, int value) { set(name, &value, 4); }
    void setUint(const char *name, quint32 value) { set(name, &value, 4); }
    void setVec(const char *name, float x, float y, float z = 0.0f, float w = 0.0f) {
        const float v[4] = {x, y, z, w};
        set(name, v, 16);
    }
    void setMat4(const char *name, const float *matrix) { set(name, matrix, 64); }
    // Array of vec3 (std140: one vec4 per element).
    void setVec3Array(const char *name, const float *values, int count) {
        auto found = members.constFind(QByteArray::fromRawData(name, int(std::strlen(name))));
        if (found == members.constEnd() || found->arrayStride == 0)
            return;
        for (int i = 0; i < count && (i + 1) * found->arrayStride <= found->size; ++i)
            std::memcpy(block.data() + found->offset + i * found->arrayStride, values + i * 3, 12);
    }
};

namespace {

constexpr int InstanceFloats = 20;
constexpr int InstanceStride = InstanceFloats * sizeof(float);
constexpr quint32 ArenaChunk = 4u << 20;

const float Identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void reflect(RhiProgram &program) {
    const QShader *stages[2] = {&program.source->vertex, &program.source->fragment};
    QHash<int, RhiProgram::Sampler> samplers;
    for (const QShader *stage : stages) {
        const QShaderDescription description = stage->description();
        for (const QShaderDescription::UniformBlock &block : description.uniformBlocks()) {
            if (block.binding == RhiShaderSource::TerrainPatchBlockBinding) {
                program.terrainPatches = true;
                continue;
            }
            if (block.binding != RhiShaderSource::UniformBlockBinding)
                continue;
            program.blockSize = std::max(program.blockSize, block.size);
            for (const QShaderDescription::BlockVariable &member : block.members) {
                RhiProgram::Member entry;
                entry.offset = member.offset;
                entry.size = member.size;
                entry.arrayStride = member.arrayStride;
                program.members.insert(member.name, entry);
            }
        }
        for (const QShaderDescription::InOutVariable &sampler : description.combinedImageSamplers()) {
            RhiProgram::Sampler entry;
            entry.binding = sampler.binding;
            switch (sampler.type) {
            case QShaderDescription::Sampler2DArray: entry.type = 1; break;
            case QShaderDescription::SamplerCube: entry.type = 2; break;
            case QShaderDescription::Sampler2D:
                entry.type = 0;
                break;
            default:
                entry.type = 0;
                break;
            }
            // A comparison sampler shows up as Sampler2D with the shadow flag
            // in its name only through the declared names; the shadow maps
            // use bindings 2, 3 and 9.
            if (sampler.name == "shadow0" || sampler.name == "shadow1" || sampler.name == "shadow2")
                entry.type = 3;
            samplers.insert(entry.binding, entry);
        }
    }
    QList<int> bindings = samplers.keys();
    std::sort(bindings.begin(), bindings.end());
    for (int binding : bindings)
        program.samplers.push_back(samplers.value(binding));
    program.inputs = program.source->vertex.description().inputVariables();
    program.block.assign(size_t(std::max(program.blockSize, 16)), 0);
}

// Float components and offset (in floats) of an attribute in a float layout.
bool layoutAttribute(RenderItem::VertexAttr layout, int location, int &size, int &offset) {
    struct Entry { int location, size, offset; };
    static const std::vector<Entry> v = {{0, 3, 0}};
    static const std::vector<Entry> vt = {{0, 3, 0}, {1, 2, 3}, {3, 1, 5}};
    static const std::vector<Entry> vnt = {{0, 3, 0}, {2, 3, 3}, {1, 2, 6}};
    static const std::vector<Entry> vnta = {{0, 3, 0}, {2, 3, 3}, {1, 2, 6}, {3, 1, 8}};
    static const std::vector<Entry> pbr = {{0, 3, 0}, {2, 3, 3}, {1, 2, 6}, {3, 1, 8},
                                           {4, 4, 9}, {5, 2, 13}, {6, 4, 15}};
    const std::vector<Entry> *entries = nullptr;
    switch (layout) {
    case RenderItem::V: entries = &v; break;
    case RenderItem::VT: entries = &vt; break;
    case RenderItem::VNT: entries = &vnt; break;
    case RenderItem::VNTA: entries = &vnta; break;
    case RenderItem::PBR: entries = &pbr; break;
    default: return false;
    }
    for (const Entry &entry : *entries)
        if (entry.location == location) {
            size = entry.size;
            offset = entry.offset;
            return true;
        }
    return false;
}

QRhiVertexInputAttribute::Format floatFormat(int components) {
    switch (components) {
    case 1: return QRhiVertexInputAttribute::Float;
    case 2: return QRhiVertexInputAttribute::Float2;
    case 3: return QRhiVertexInputAttribute::Float3;
    default: return QRhiVertexInputAttribute::Float4;
    }
}

int inputComponents(QShaderDescription::VariableType type) {
    switch (type) {
    case QShaderDescription::Float: return 1;
    case QShaderDescription::Vec2: return 2;
    case QShaderDescription::Vec3: return 3;
    default: return 4;
    }
}

QRhiGraphicsPipeline::Topology topology(RenderItem::Primitive primitive) {
    switch (primitive) {
    case RenderItem::PRIMITIVE_TRIANGLE_STRIP: return QRhiGraphicsPipeline::TriangleStrip;
    case RenderItem::PRIMITIVE_TRIANGLE_FAN: return QRhiGraphicsPipeline::TriangleFan;
    case RenderItem::PRIMITIVE_LINES: return QRhiGraphicsPipeline::Lines;
    case RenderItem::PRIMITIVE_LINE_STRIP:
    case RenderItem::PRIMITIVE_LINE_LOOP: return QRhiGraphicsPipeline::LineStrip;
    case RenderItem::PRIMITIVE_POINTS: return QRhiGraphicsPipeline::Points;
    case RenderItem::PRIMITIVE_TRIANGLES: break;
    }
    return QRhiGraphicsPipeline::Triangles;
}

bool usesTerrainProgram(const RenderItem *item) {
    return item->material.surface == RenderItem::SURFACE_TERRAIN || item->terrain.paged
            || item->terrain.materialMap != 0 || !item->terrain.textureRemap.isNull();
}

bool isUnlitPass(int pass) {
    return pass == Renderer::PASS_OVERLAY || pass == Renderer::PASS_UI;
}

const char *PresentVertex = R"(#version 440
layout(location = 0) out vec2 uv;
void main() {
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    uv = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";
const char *PresentFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D view;
void main() {
    fragColor = vec4(texture(view, uv).rgb, 1.0);
}
)";

QShader bakeInline(const char *source, QShader::Stage stage, QRhi *rhi) {
    QShaderBaker baker;
    switch (rhi->backend()) {
    case QRhi::Vulkan: baker.setGeneratedShaders({{QShader::SpirvShader, QShaderVersion(100)}}); break;
    case QRhi::OpenGLES2: baker.setGeneratedShaders({{QShader::GlslShader, QShaderVersion(330)}}); break;
    case QRhi::Metal: baker.setGeneratedShaders({{QShader::MslShader, QShaderVersion(12)}}); break;
    default: baker.setGeneratedShaders({{QShader::HlslShader, QShaderVersion(50)}}); break;
    }
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setSourceString(source, stage);
    QShader shader = baker.bake();
    if (!shader.isValid())
        qWarning() << "QRhi present shader:" << baker.errorMessage();
    return shader;
}

}

bool RhiRenderer::PipelineKey::operator==(const PipelineKey &o) const {
    return program == o.program && pass == o.pass && topology == o.topology && layout == o.layout
            && format == o.format && blend == o.blend && depthWrite == o.depthWrite
            && decal == o.decal && cullBack == o.cullBack && frontCw == o.frontCw
            && wireframe == o.wireframe && lineWidth == o.lineWidth;
}

size_t RhiRenderer::PipelineKeyHash::operator()(const PipelineKey &k) const {
    size_t h = std::hash<const void *>()(k.program) ^ (std::hash<const void *>()(k.pass) << 1);
    const quint32 bits = k.topology | (k.layout << 4) | (k.format << 10) | (k.blend << 12)
            | (k.depthWrite << 13) | (k.decal << 14) | (k.cullBack << 15) | (k.frontCw << 16)
            | (k.wireframe << 17) | (quint32(k.lineWidth) << 18);
    return h ^ (std::hash<quint32>()(bits) * 0x9E3779B97F4A7C15ull);
}

bool RhiRenderer::BindingKey::operator==(const BindingKey &o) const {
    return program == o.program && uniforms == o.uniforms && terrainPatches == o.terrainPatches
            && textures == o.textures && samplers == o.samplers;
}

size_t RhiRenderer::BindingKeyHash::operator()(const BindingKey &k) const {
    size_t h = std::hash<const void *>()(k.program) ^ (std::hash<const void *>()(k.uniforms) << 1)
            ^ (std::hash<const void *>()(k.terrainPatches) << 2);
    for (QRhiTexture *texture : k.textures)
        h = h * 31 + std::hash<const void *>()(texture);
    for (QRhiSampler *sampler : k.samplers)
        h = h * 17 + std::hash<const void *>()(sampler);
    return h;
}

RhiRenderer::RhiRenderer(RhiContext *context)
    : context(context), rhi(context->rhi()), gluu(GLUU::get()) {
    uniformStride = quint32(rhi->ubufAligned(1));
    targets[TARGET_VIEW].attachments = &view;
    targets[TargetSelection].attachments = &selection;
    // Programs: the main program and its variants, selection, shadows.
    struct Definition { RhiProgram::Kind kind; const char *vertex; const char *fragment; QStringList defines; };
    const Definition definitions[] = {
        {RhiProgram::MAIN, "StandardFog", "StandardFog", {}},
        {RhiProgram::TERRAIN, "StandardFog", "StandardFog", {"TSRE_TERRAIN"}},
        {RhiProgram::UNLIT, "StandardFog", "StandardFog", {"TSRE_UNLIT"}},
        {RhiProgram::PBR, "StandardFog", "StandardFog", {"TSRE_PBR"}},
        {RhiProgram::WATER, "StandardFog", "StandardFog", {"TSRE_WATER"}},
        {RhiProgram::SELECTION, "StandardFog", "Selection", {"TSRE_TERRAIN"}},
        {RhiProgram::SHADOW, "Shadows", "Shadows", {}}};
    for (const Definition &definition : definitions) {
        auto program = std::make_unique<RhiProgram>();
        program->kind = definition.kind;
        program->source = &context->program(definition.vertex, definition.fragment, definition.defines);
        if (program->valid())
            reflect(*program);
        programs.push_back(std::move(program));
    }

    dummy2D = rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
    dummy2D->create();
    dummyArray = rhi->newTextureArray(QRhiTexture::RGBA8, 1, QSize(1, 1));
    dummyArray->create();
    dummyCube = rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1, QRhiTexture::CubeMap);
    dummyCube->create();
    dummyDepth = rhi->newTexture(QRhiTexture::D32F, QSize(1, 1), 1, QRhiTexture::RenderTarget);
    dummyDepth->create();
    shadowSampler = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                    QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    shadowSampler->setTextureCompareOp(QRhiSampler::LessOrEqual);
    shadowSampler->create();
    // std140 TerrainPatchBlock: 256 patches of two vec4.
    dummyTerrainPatches = rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::UniformBuffer, 256 * 32);
    dummyTerrainPatches->create();
    // White placeholders; the depth stand-in is cleared to the far plane.
    QRhiResourceUpdateBatch *batch = RhiTextures::updates();
    if (batch != nullptr) {
        QImage white(1, 1, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        batch->uploadTexture(dummy2D, white);
        QRhiTextureUploadDescription arrayUpload(QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(white)));
        batch->uploadTexture(dummyArray, arrayUpload);
        QVarLengthArray<QRhiTextureUploadEntry, 6> faces;
        for (int face = 0; face < 6; ++face)
            faces.append(QRhiTextureUploadEntry(face, 0, QRhiTextureSubresourceUploadDescription(white)));
        QRhiTextureUploadDescription cubeUpload;
        cubeUpload.setEntries(faces.cbegin(), faces.cend());
        batch->uploadTexture(dummyCube, cubeUpload);
    }
}

RhiRenderer::~RhiRenderer() {
    clearQueues();
    releaseResources();
}

void RhiRenderer::releaseResources() {
    for (auto &entry : pipelines)
        delete entry.second;
    pipelines.clear();
    for (auto &entry : resourceSets)
        delete entry.second;
    resourceSets.clear();
    for (QRhiSampler *sampler : std::as_const(samplers))
        delete sampler;
    samplers.clear();
    for (auto &program : programs)
        program->layout = nullptr;
    delete presentPipeline;
    presentPipeline = nullptr;
    delete presentBindings;
    presentBindings = nullptr;
    delete presentSampler;
    presentSampler = nullptr;
    releaseAttachments(view);
    releaseAttachments(selection);
    for (Attachments &map : shadowMaps)
        releaseAttachments(map);
    delete uniformArena.buffer;
    uniformArena.buffer = nullptr;
    delete instanceArena.buffer;
    instanceArena.buffer = nullptr;
    delete dummy2D;
    delete dummyArray;
    delete dummyCube;
    delete dummyDepth;
    delete shadowSampler;
    delete dummyTerrainPatches;
    dummyTerrainPatches = nullptr;
    dummy2D = dummyArray = dummyCube = dummyDepth = nullptr;
    shadowSampler = nullptr;
}

RhiRenderSurface *RhiRenderer::surface() const {
    return static_cast<RhiRenderSurface *>(viewSurface);
}

bool RhiRenderer::programsReady() const {
    for (const auto &program : programs)
        if (!program->valid())
            return false;
    return true;
}

RhiProgram *RhiRenderer::variant(int index) {
    if (index < 0 || index >= int(programs.size()) || !programs[size_t(index)]->valid())
        return nullptr;
    return programs[size_t(index)].get();
}

void RhiRenderer::useProgram(Program program) {
    baseProgram = program;
    currentVariant = variant(program == PROGRAM_SELECTION ? RhiProgram::SELECTION
                             : program == PROGRAM_SHADOW ? RhiProgram::SHADOW : RhiProgram::MAIN);
    fogLodOverride = false;
}

RhiProgram *RhiRenderer::programFor(const RenderItem *item, int pass) {
    if (baseProgram == PROGRAM_SELECTION)
        return variant(RhiProgram::SELECTION);
    if (baseProgram == PROGRAM_SHADOW)
        return variant(RhiProgram::SHADOW);
    if (usesTerrainProgram(item))
        return variant(RhiProgram::TERRAIN);
    if (isUnlitPass(pass))
        return variant(RhiProgram::UNLIT);
    if (item->pbr.enabled)
        return variant(RhiProgram::PBR);
    if (item->water.enabled)
        return variant(RhiProgram::WATER);
    return variant(RhiProgram::MAIN);
}

// The values GLUU::setMatrixUniforms gives a program, from the GLUU fields.
void RhiRenderer::writeFrameUniforms(RhiProgram *program) {
    if (program == nullptr)
        return;
    float corrected[16];
    const QMatrix4x4 correction = rhi->clipSpaceCorrMatrix();
    Mat4::multiply(corrected, const_cast<float *>(correction.constData()), gluu->pMatrix);
    program->setMat4("uPMatrix", corrected);
    program->setMat4("uFMatrix", gluu->fMatrix);
    if (program->kind == RhiProgram::SHADOW) {
        // The shadow program draws with the light's matrix.
        const QMatrix4x4 shadowCorrection = shadowClipCorrection();
        Mat4::multiply(corrected, const_cast<float *>(shadowCorrection.constData()), gluu->pShadowMatrix);
        program->setMat4("uShadowPMatrix", corrected);
    } else {
        program->setMat4("uShadowPMatrix", gluu->pShadowMatrix);
    }
    program->setMat4("uShadow2PMatrix", gluu->pShadowMatrix2);
    program->setMat4("uShadow0PMatrix", gluu->pShadowMatrix0);
    program->setMat4("uMVMatrix", gluu->mvMatrix);
    program->setMat4("uMSMatrix", gluu->objStrMatrix);
    program->setFloat("lod", fogLodOverride ? fogLod : Game::objectLod);
    program->setVec("skyColor", gluu->fogColor[0], gluu->fogColor[1], gluu->fogColor[2], gluu->fogColor[3]);
    program->setVec("diffuseColor", 0.7f, 0.7f, 0.7f, 0.7f);
    program->setVec("ambientColor", 0.3f, 0.3f, 0.3f, 0.3f);
    program->setVec("specularColor", 1.0f, 1.0f, 1.0f, 1.0f);
    program->setVec("lightDirection", Game::sunLightDirection[0], Game::sunLightDirection[1],
                    Game::sunLightDirection[2]);
    program->setFloat("isAlpha", gluu->alpha);
    program->setFloat("alphaTest", gluu->alphaTest);
    program->setFloat("textureEnabled", 1.0f);
    program->setFloat("enableNormals", 1.0f);
    program->setFloat("secondTexEnabled", 0.0f);
    program->setVec("terrainTextureRemap", 0.0f, 0.0f, 0.0f);
    program->setInt("shadowsEnabled", Game::shadowsEnabled);
    program->setFloat("colorBrightness", gluu->currentBrightness);
    program->setFloat("fogDensity", gluu->fogDensity);
    program->setUint("selectionId", 0);
    program->setFloat("shadow1Res", gluu->shadow1Res);
    program->setFloat("shadow2Res", gluu->shadow2Res);
    program->setFloat("shadow2Bias", gluu->shadow2Bias);
    program->setVec("shadowMapScale", gluu->shadowMapScale[0], gluu->shadowMapScale[1],
                    gluu->shadowMapScale[2], gluu->shadowMapScale[3]);
    program->setVec("shadowNormalOffset", gluu->shadowNormalOffset[0], gluu->shadowNormalOffset[1],
                    gluu->shadowNormalOffset[2]);
    program->setVec("shadowDepthBias", gluu->shadowDepthBias[0], gluu->shadowDepthBias[1]);
    program->setVec("shadowLightDirection", gluu->shadowLightDirection[0],
                    gluu->shadowLightDirection[1], gluu->shadowLightDirection[2]);
    program->setVec("cameraPosition", gluu->cameraPosition[0], gluu->cameraPosition[1],
                    gluu->cameraPosition[2]);
    program->setFloat("environmentMapLevels", float(gluu->environmentMapLevels));
    program->setFloat("waterTime", GLUU::animationSeconds());
    program->setVec("waterReflectionView", gluu->waterReflectionView[0], gluu->waterReflectionView[1],
                    gluu->waterReflectionView[2], gluu->waterReflectionView[3]);
    program->setVec("waterReflectionPlane", gluu->waterReflectionPlane[0], gluu->waterReflectionPlane[1],
                    gluu->waterReflectionPlane[2], gluu->waterReflectionPlane[3]);
    program->setVec("clipPlane", gluu->clipPlane[0], gluu->clipPlane[1], gluu->clipPlane[2],
                    gluu->clipPlane[3]);
    program->setInt("terrainPaged", 0);
    program->setInt("terrainMaterialEnabled", 0);
}

void RhiRenderer::applyFrameUniforms() {
    fogLodOverride = false;
    for (auto &program : programs)
        if (program->valid())
            writeFrameUniforms(program.get());
}

void RhiRenderer::setFogLod(float lod) {
    fogLod = lod;
    fogLodOverride = true;
    for (auto &program : programs)
        if (program->valid())
            program->setFloat("lod", lod);
}

// The values applyItemState and its neighbours set in the OpenGL renderer.
void RhiRenderer::writeItemUniforms(RhiProgram *program, RenderItem *item, quint32 selectionId) {
    program->setFloat("enableNormals", item->material.lit ? 1.0f : 0.0f);
    program->setFloat("colorBrightness", item->material.brightness);
    program->setUint("selectionId", selectionId);
    const QVector3D remap = item->terrain.textureRemap;
    program->setVec("terrainTextureRemap", remap.x(), remap.y(), remap.z());
    const float detailScale = item->material.textured && !selectionId ? item->material.detailScale : 0.0f;
    program->setFloat("secondTexEnabled", detailScale);
    program->setMat4("uMSMatrix", item->msMatrix != nullptr ? item->msMatrix : Identity);
    program->setInt("terrainPaged", item->terrain.paged ? 1 : 0);
    if (item->terrain.paged) {
        program->setInt("terrainVerticesPerPatch", item->terrain.verticesPerPatch);
        program->setInt("terrainPatchSide", item->terrain.patchSide);
        program->setFloat("terrainSampleSpacing", item->terrain.sampleSpacing);
        program->setInt("terrainApplyGaps", item->terrain.applyGaps ? 1 : 0);
        program->setInt("terrainMapPass", item->terrain.mapPass ? 1 : 0);
    }
}

QRhiTexture *RhiRenderer::packetTexture(const RenderItem *item, bool &mipmapped) {
    mipmapped = false;
    unsigned int handle = 0;
    if (item->material.textureId < 0) {
        handle = item->material.textureObject;
    } else {
        const auto found = TexLib::mtex.find(item->material.textureId);
        if (found == TexLib::mtex.end() || found->second == nullptr)
            return nullptr;
        Texture *texture = found->second;
        if (!texture->glLoaded && texture->loaded)
            texture->GLTextures();
        if (!texture->glLoaded || texture->tex == nullptr)
            return nullptr;
        handle = texture->tex[0];
        if (TexLib::disabledTextures.value(int(handle), 0) == 1)
            return nullptr;
    }
    mipmapped = RhiTextures::sampledWithMipmaps(handle);
    return RhiTextures::texture(handle);
}

QRhiTexture *RhiRenderer::waterWaves() {
    if (waterWaveHandle == 0) {
        const QByteArray texels(reinterpret_cast<const char *>(
                                    WaterNormalMap::generate(WaterNormalMap::Size).data()),
                                WaterNormalMap::Size * WaterNormalMap::Size * 4);
        waterWaveHandle = RhiTextures::create(WaterNormalMap::Size, WaterNormalMap::Size, {texels});
        RhiTextures::setSampling(waterWaveHandle, true, false);
    }
    QRhiTexture *texture = RhiTextures::texture(waterWaveHandle);
    return texture != nullptr ? texture : dummy2D;
}

QRhiSampler *RhiRenderer::sampler(bool mipmaps, bool clamp, bool nearest) {
    const int key = (mipmaps ? 1 : 0) | (clamp ? 2 : 0) | (nearest ? 4 : 0);
    QRhiSampler *found = samplers.value(key, nullptr);
    if (found != nullptr)
        return found;
    const QRhiSampler::AddressMode address = clamp ? QRhiSampler::ClampToEdge : QRhiSampler::Repeat;
    const QRhiSampler::Filter filter = nearest ? QRhiSampler::Nearest : QRhiSampler::Linear;
    QRhiSampler *created = rhi->newSampler(filter, filter,
                                           mipmaps ? QRhiSampler::Linear : QRhiSampler::None,
                                           address, address);
    created->create();
    samplers.insert(key, created);
    return created;
}

void RhiRenderer::beginFrameIfNeeded() {
    RhiRenderSurface *s = surface();
    const quint64 serial = s != nullptr ? s->frameSerial() : 0;
    if (serial == frameSerial)
        return;
    frameSerial = serial;
    uniformArena.data.clear();
    uniformArena.uploaded = 0;
    instanceArena.data.clear();
    instanceArena.uploaded = 0;
    for (TargetState &state : targets) {
        state.draws.clear();
        state.keys.clear();
        state.bindings.clear();
        state.clearColor = state.clearDepth = false;
    }
    targets[TARGET_VIEW].attachments = &view;
    targets[TargetSelection].attachments = &selection;
    currentTarget = TARGET_VIEW;
    Meshes::collectGarbageRhi();
}

bool RhiRenderer::createAttachments(Attachments &attachments, QRhiTexture::Format colorFormat,
                                    const QSize &size, QRhiTexture::Flags colorFlags) {
    releaseAttachments(attachments);
    attachments.size = size;
    if (colorFormat != QRhiTexture::UnknownFormat) {
        attachments.color = rhi->newTexture(colorFormat, size, 1, QRhiTexture::RenderTarget | colorFlags);
        if (!attachments.color->create()) {
            releaseAttachments(attachments);
            return false;
        }
    }
    attachments.depth = rhi->newTexture(QRhiTexture::D32F, size, 1, QRhiTexture::RenderTarget);
    if (!attachments.depth->create()) {
        releaseAttachments(attachments);
        return false;
    }
    for (int clears = 0; clears < 4; ++clears) {
        QRhiTextureRenderTargetDescription description;
        if (attachments.color != nullptr)
            description.setColorAttachments({QRhiColorAttachment(attachments.color)});
        description.setDepthTexture(attachments.depth);
        QRhiTextureRenderTarget::Flags flags;
        if (!(clears & 1))
            flags |= QRhiTextureRenderTarget::PreserveColorContents;
        if (!(clears & 2))
            flags |= QRhiTextureRenderTarget::PreserveDepthStencilContents;
        QRhiTextureRenderTarget *target = rhi->newTextureRenderTarget(description, flags);
        attachments.passes[clears] = target->newCompatibleRenderPassDescriptor();
        target->setRenderPassDescriptor(attachments.passes[clears]);
        attachments.targets[clears] = target;
        if (!target->create()) {
            releaseAttachments(attachments);
            return false;
        }
    }
    return true;
}

void RhiRenderer::releaseAttachments(Attachments &attachments) {
    QRhiRenderPassDescriptor *pass = attachments.pipelinePass();
    if (pass != nullptr) {
        for (auto it = pipelines.begin(); it != pipelines.end(); ) {
            if (it->first.pass == pass) {
                delete it->second;
                it = pipelines.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (int clears = 0; clears < 4; ++clears) {
        delete attachments.targets[clears];
        delete attachments.passes[clears];
        attachments.targets[clears] = nullptr;
        attachments.passes[clears] = nullptr;
    }
    delete attachments.color;
    delete attachments.depth;
    attachments.color = attachments.depth = nullptr;
    attachments.size = QSize();
}

bool RhiRenderer::ensureViewTarget(const QSize &size) {
    if (view.valid() && view.size == size)
        return true;
    delete presentBindings;
    presentBindings = nullptr;
    return createAttachments(view, QRhiTexture::RGBA8, size, QRhiTexture::UsedAsTransferSource);
}

bool RhiRenderer::targetReady() {
    RhiRenderSurface *s = surface();
    if (s == nullptr || s->frame().commandBuffer == nullptr)
        return false;
    if (currentTarget == TARGET_VIEW)
        return ensureViewTarget(s->frame().pixelSize);
    const Attachments *attachments = targets[currentTarget].attachments;
    return attachments != nullptr && attachments->valid();
}

RhiRenderer::TargetState &RhiRenderer::target() {
    return targets[currentTarget];
}

void RhiRenderer::bindTarget(Target target) {
    beginFrameIfNeeded();
    if (currentTarget != target)
        flushTarget();
    currentTarget = target;
}

bool RhiRenderer::beginSelection(int width, int height) {
    beginFrameIfNeeded();
    if (!targetReady() && currentTarget == TARGET_VIEW)
        return false;
    flushTarget();
    const QSize size(std::max(1, width), std::max(1, height));
    if ((!selection.valid() || selection.size != size)
            && !createAttachments(selection, QRhiTexture::R32UI, size,
                                  QRhiTexture::UsedAsTransferSource))
        return false;
    std::copy(viewportRect, viewportRect + 4, selectionViewport);
    setViewport(0, 0, size.width(), size.height());
    currentTarget = TargetSelection;
    TargetState &state = target();
    state.clearColor = state.clearDepth = true;
    state.color = Qt::black;
    selectionRead = false;
    selectionIds.clear();
    return true;
}

quint32 RhiRenderer::readSelection(int x, int y) {
    if (!selection.valid() || x < 0 || y < 0 || x >= selection.size.width()
            || y >= selection.size.height())
        return 0;
    if (!selectionRead) {
        selectionRead = true;
        const int previous = currentTarget;
        currentTarget = TargetSelection;
        flushTarget();
        currentTarget = previous;
        RhiRenderSurface *s = surface();
        if (s == nullptr || s->frame().commandBuffer == nullptr)
            return 0;
        // The ids are needed now: wait for the GPU, which completes the
        // readback within the frame.
        QRhiReadbackResult result;
        QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
        batch->readBackTexture(QRhiReadbackDescription(selection.color), &result);
        s->frame().commandBuffer->resourceUpdate(batch);
        rhi->finish();
        selectionIds = result.data;
    }
    if (selectionIds.size() < qsizetype(selection.size.width()) * selection.size.height() * 4)
        return 0;
    // Rows come top first unless the backend's framebuffer has y up.
    const int row = rhi->isYUpInFramebuffer() ? y : selection.size.height() - 1 - y;
    quint32 id = 0;
    std::memcpy(&id, selectionIds.constData()
                + (qsizetype(row) * selection.size.width() + x) * 4, sizeof(id));
    return id;
}

void RhiRenderer::endSelection() {
    if (currentTarget == TargetSelection)
        flushTarget();
    currentTarget = TARGET_VIEW;
    setViewport(selectionViewport[0], selectionViewport[1], selectionViewport[2],
                selectionViewport[3]);
}

void RhiRenderer::createShadowMaps(int nearSize, int farSize) {
    beginFrameIfNeeded();
    const int sizes[3] = {nearSize, nearSize, farSize};
    for (int map = 0; map < 3; ++map) {
        const QSize size(std::max(1, sizes[map]), std::max(1, sizes[map]));
        if (!shadowMaps[map].valid() || shadowMaps[map].size != size)
            createAttachments(shadowMaps[map], QRhiTexture::UnknownFormat, size, {});
        targets[TARGET_SHADOW_NEAR + map].attachments = &shadowMaps[map];
    }
}

bool RhiRenderer::shadowTarget() const {
    return currentTarget >= TARGET_SHADOW_NEAR && currentTarget <= TARGET_SHADOW_FAR;
}

QMatrix4x4 RhiRenderer::shadowClipCorrection() const {
    QMatrix4x4 correction = rhi->clipSpaceCorrMatrix();
    // Where the framebuffer's y points down, rows would come out flipped
    // against OpenGL's texture coordinates.
    if (!rhi->isYUpInFramebuffer())
        correction.scale(1.0f, -1.0f, 1.0f);
    return correction;
}

bool RhiRenderer::setBlending(bool enabled) {
    const bool previous = blending;
    blending = enabled;
    return previous;
}

void RhiRenderer::clear(bool color, bool depth, const float *clearColor) {
    beginFrameIfNeeded();
    TargetState &state = target();
    if ((color || depth) && !state.draws.empty())
        flushTarget();
    // As glClearColor: the colour of this and later clears.
    if (clearColor != nullptr)
        nextClearColor = QColor::fromRgbF(clearColor[0], clearColor[1], clearColor[2], 1.0f);
    if (color)
        state.color = nextClearColor;
    state.clearColor = state.clearColor || color;
    state.clearDepth = state.clearDepth || depth;
}

void RhiRenderer::setViewport(int x, int y, int width, int height) {
    viewportRect[0] = x;
    viewportRect[1] = y;
    viewportRect[2] = width;
    viewportRect[3] = height;
}

void RhiRenderer::viewport(int *rectangle) const {
    std::copy(viewportRect, viewportRect + 4, rectangle);
}

float RhiRenderer::readDepth(int, int) {
    return 1.0f;
}

void RhiRenderer::readColor(int, int, int width, int height, unsigned char *rgba) {
    std::fill(rgba, rgba + qsizetype(width) * height * 4, 0);
}

void RhiRenderer::beginViewBand(const LayeredView &view, ViewBand band) {
    if (!view.projection)
        return;
    float projection[16];
    float viewMatrix[16];
    std::copy(view.view, view.view + 16, viewMatrix);
    frontCw = view.mirrorPlane != nullptr;
    // Each band gets a slice of the depth range, farther bands behind nearer
    // ones, in place of the depth clears between bands.
    if (band == BAND_SKY) {
        view.projection(0.2f, view.sceneFar, projection);
        Mat4::multiply(gluu->fMatrix, projection, viewMatrix);
        view.projection(100.0f, 10000.0f, projection);
        Mat4::multiply(gluu->pMatrix, projection, viewMatrix);
        applyFrameUniforms();
        setFogLod(0.0f);
        setCullView(nullptr);
        depthRange[0] = 0.99f;
        depthRange[1] = 1.0f;
        return;
    }
    if (band == BAND_DISTANT) {
        view.projection(600.0f, view.distantFar, projection);
        depthRange[0] = 0.98f;
        depthRange[1] = 0.99f;
    } else {
        view.projection(0.2f, view.sceneFar, projection);
        depthRange[0] = 0.0f;
        depthRange[1] = 0.98f;
    }
    Mat4::multiply(gluu->pMatrix, projection, viewMatrix);
    applyFrameUniforms();
    setCullView(gluu->pMatrix);
    if (band == BAND_SCENE)
        setViewLimits(view.limits);
}

void RhiRenderer::endView(const LayeredView &) {
    setViewLimits(nullptr);
    setCullView(nullptr);
    frontCw = false;
    depthRange[0] = 0.0f;
    depthRange[1] = 1.0f;
}

quint32 RhiRenderer::appendUniforms(const RhiProgram *program) {
    std::vector<char> &data = uniformArena.data;
    const quint32 offset = quint32((data.size() + uniformStride - 1) / uniformStride * uniformStride);
    data.resize(offset + quint32(program->block.size()));
    std::memcpy(data.data() + offset, program->block.data(), program->block.size());
    return offset;
}

quint32 RhiRenderer::appendInstances(const float *const *matrices, int count) {
    std::vector<char> &data = instanceArena.data;
    const quint32 offset = quint32(data.size());
    data.resize(offset + quint32(count) * InstanceStride);
    float *out = reinterpret_cast<float *>(data.data() + offset);
    static const float defaults[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < count; ++i) {
        std::memcpy(out + i * InstanceFloats, matrices[i], 16 * sizeof(float));
        std::memcpy(out + i * InstanceFloats + 16, defaults, sizeof(defaults));
    }
    return offset;
}

QRhiBuffer *RhiRenderer::uploadArena(Arena &arena, QRhiBuffer::UsageFlags usage,
                                     QRhiResourceUpdateBatch *batch) {
    const quint32 size = quint32(arena.data.size());
    if (arena.buffer == nullptr || arena.buffer->size() < size) {
        // Grows between frames; within a frame the buffer stays.
        quint32 capacity = std::max(ArenaChunk, size * 2);
        if (arena.buffer != nullptr)
            arena.buffer->deleteLater();
        arena.buffer = rhi->newBuffer(QRhiBuffer::Dynamic, usage, capacity);
        arena.buffer->create();
        arena.uploaded = 0;
    }
    if (size > arena.uploaded) {
        batch->updateDynamicBuffer(arena.buffer, arena.uploaded, size - arena.uploaded,
                                   arena.data.data() + arena.uploaded);
        arena.uploaded = size;
    }
    return arena.buffer;
}

QRhiGraphicsPipeline *RhiRenderer::pipeline(const PipelineKey &key) {
    auto found = pipelines.find(key);
    if (found != pipelines.end())
        return found->second;
    const RhiProgram *program = key.program;
    QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
    ps->setShaderStages({{QRhiShaderStage::Vertex, program->source->vertex},
                         {QRhiShaderStage::Fragment, program->source->fragment}});
    // Binding 0: the mesh; binding 1: per-instance model matrix and the
    // (0, 0, 0, 1) values of inputs the mesh does not have.
    const RenderItem::VertexAttr layout = static_cast<RenderItem::VertexAttr>(key.layout);
    const bool terrainVertex = key.format == MeshData::TerrainHeightNormal;
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{terrainVertex ? 8u : quint32(std::max(1, int(layout)) * sizeof(float))},
                             {quint32(InstanceStride), QRhiVertexInputBinding::PerInstance}});
    QVarLengthArray<QRhiVertexInputAttribute, 16> attributes;
    for (const QShaderDescription::InOutVariable &input : program->inputs) {
        const int location = input.location;
        int size = 0, offset = 0;
        if (location >= 8 && location <= 11) {
            attributes.append({1, location, QRhiVertexInputAttribute::Float4,
                               quint32((location - 8) * 16)});
        } else if (terrainVertex && location == 0) {
            attributes.append({0, 0, QRhiVertexInputAttribute::Float, 0});
        } else if (terrainVertex && location == 2) {
            attributes.append({0, 2, QRhiVertexInputAttribute::UNormByte4, 4});
        } else if (!terrainVertex && layoutAttribute(layout, location, size, offset)) {
            attributes.append({0, location, floatFormat(size), quint32(offset * sizeof(float))});
        } else {
            attributes.append({1, location, floatFormat(inputComponents(input.type)), 64});
        }
    }
    inputLayout.setAttributes(attributes.cbegin(), attributes.cend());
    ps->setVertexInputLayout(inputLayout);
    ps->setTopology(static_cast<QRhiGraphicsPipeline::Topology>(key.topology));
    ps->setCullMode(key.cullBack ? QRhiGraphicsPipeline::Back : QRhiGraphicsPipeline::None);
    ps->setFrontFace(key.frontCw ? QRhiGraphicsPipeline::CW : QRhiGraphicsPipeline::CCW);
    ps->setDepthTest(true);
    ps->setDepthWrite(key.depthWrite && !key.decal);
    ps->setDepthOp(QRhiGraphicsPipeline::Less);
    if (key.decal) {
        ps->setDepthBias(-2);
        ps->setSlopeScaledDepthBias(-2.0f);
    }
    if (key.blend) {
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        ps->setTargetBlends({blend});
    }
    if (key.wireframe && rhi->isFeatureSupported(QRhi::NonFillPolygonMode))
        ps->setPolygonMode(QRhiGraphicsPipeline::Line);
    if (key.lineWidth > 1 && rhi->isFeatureSupported(QRhi::WideLines))
        ps->setLineWidth(key.lineWidth);
    ps->setShaderResourceBindings(program->layout);
    ps->setRenderPassDescriptor(key.pass);
    if (!ps->create()) {
        qWarning() << "QRhi renderer: pipeline creation failed";
        delete ps;
        ps = nullptr;
    }
    pipelines.emplace(key, ps);
    return ps;
}

QRhiShaderResourceBindings *RhiRenderer::bindings(const BindingKey &key) {
    auto found = resourceSets.find(key);
    if (found != resourceSets.end())
        return found->second;
    const RhiProgram *program = key.program;
    QVarLengthArray<QRhiShaderResourceBinding, 24> entries;
    const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
    entries.append(QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                       RhiShaderSource::UniformBlockBinding, stages, key.uniforms,
                       quint32(program->block.size())));
    if (program->terrainPatches && key.terrainPatches != nullptr)
        entries.append(QRhiShaderResourceBinding::uniformBuffer(
                           RhiShaderSource::TerrainPatchBlockBinding,
                           QRhiShaderResourceBinding::VertexStage, key.terrainPatches));
    for (size_t i = 0; i < program->samplers.size(); ++i)
        entries.append(QRhiShaderResourceBinding::sampledTexture(
                           program->samplers[i].binding, stages, key.textures[i], key.samplers[i]));
    QRhiShaderResourceBindings *set = rhi->newShaderResourceBindings();
    set->setBindings(entries.cbegin(), entries.cend());
    if (!set->create()) {
        qWarning() << "QRhi renderer: resource set creation failed";
        delete set;
        set = nullptr;
    }
    resourceSets.emplace(key, set);
    RhiProgram *mutableProgram = const_cast<RhiProgram *>(program);
    if (mutableProgram->layout == nullptr)
        mutableProgram->layout = set;
    return set;
}

static QHash<QString, int> debugCounts;
static const bool traceDraws = qEnvironmentVariableIsSet("TSRE_RHI_TRACE");
static void debugCount(const QString &key) { if (traceDraws) debugCounts[key]++; }

void RhiRenderer::recordDraw(RenderItem *item, const float *const *matrices, int count,
                             quint32 selectionId, int pass, int category) {
    if (count <= 0 || !hasMesh(item)) {
        debugCount("skip-nomesh");
        return;
    }
    RhiProgram *program = programFor(item, pass);
    if (program == nullptr)
        return;
    if (program != currentVariant) {
        // A newly used program receives the frame values again, as in OpenGL.
        currentVariant = program;
        writeFrameUniforms(program);
        if (fogLodOverride)
            program->setFloat("lod", fogLod);
    }
    if (frameBatch == nullptr)
        frameBatch = rhi->nextResourceUpdateBatch();
    Meshes::RhiBuffers buffers;
    if (!Meshes::prepareRhi(item->mesh.handle, rhi, frameBatch, buffers)
            || buffers.format == MeshData::Buffer) {
        debugCount("skip-prepare");
        return;
    }
    if (traceDraws)
        debugCount(QString("draw kind%1 format%2 pass%3 indexed%4").arg(program->kind)
                   .arg(int(buffers.format)).arg(pass).arg(item->mesh.indexed));
    writeItemUniforms(program, item, selectionId);

    // Textures: the packet texture on 0, the detail texture on 1, stand-ins
    // elsewhere.
    BindingKey bindingKey;
    bindingKey.program = program;
    if (program->terrainPatches) {
        bindingKey.terrainPatches = item->terrain.paged
                ? Meshes::uniformBufferRhi(item->terrain.paramsBuffer, rhi, frameBatch) : nullptr;
        if (bindingKey.terrainPatches == nullptr)
            bindingKey.terrainPatches = dummyTerrainPatches;
    }
    bool mipmapped = false;
    QRhiTexture *base = nullptr;
    if (item->material.textured && selectionId == 0) {
        base = packetTexture(item, mipmapped);
        if (base != nullptr) {
            program->setFloat("textureEnabled", 1.0f);
        } else {
            program->setFloat("textureEnabled", 0.0f);
            program->setVec("shapeColor", 1.0f, 0.0f, 1.0f, 1.0f);
        }
    } else if (item->material.textured) {
        program->setFloat("textureEnabled", 0.0f);
        program->setVec("shapeColor", 1.0f, 0.0f, 1.0f, 1.0f);
    } else {
        program->setFloat("textureEnabled", 0.0f);
        program->setVec("shapeColor", item->material.color[0], item->material.color[1],
                        item->material.color[2], item->material.color[3]);
    }
    const unsigned int baseHandle = item->material.textureId < 0 ? item->material.textureObject : 0;
    QRhiTexture *detail = nullptr;
    if (item->material.textured && selectionId == 0 && item->material.detailScale != 0.0f)
        detail = RhiTextures::texture(item->material.detailTextureObject);
    // Direct procedural terrain: the material map, the material and detail
    // arrays and the per-material parameters on 4 to 7.
    const RenderItem::Terrain &terrain = item->terrain;
    const bool materials = program->kind == RhiProgram::TERRAIN && selectionId == 0
            && RhiTextures::texture(terrain.materialMap) != nullptr
            && RhiTextures::texture(terrain.materialTextures) != nullptr
            && RhiTextures::texture(terrain.materialParams) != nullptr;
    program->setInt("terrainMaterialEnabled", materials ? 1 : 0);
    if (materials) {
        // The base texture slot only selects the shader's textured path.
        program->setFloat("textureEnabled", 1.0f);
        const QVector3D mapRemap = terrain.materialMapRemap;
        program->setVec("terrainMaterialMapRemap", mapRemap.x(), mapRemap.y(), mapRemap.z());
        program->setInt("terrainMaterialMapSide", terrain.materialMapSide);
        program->setFloat("terrainMaterialNoiseScale", terrain.materialNoiseScale);
    }
    // What each texture unit holds for this program, as the OpenGL renderer
    // binds them: the packet texture on 0, the detail texture on 1, the
    // water layers on 4 and 5 and the wave map on 15.
    int waterLayers = 0;
    for (const RhiProgram::Sampler &slot : program->samplers) {
        QRhiTexture *texture = nullptr;
        QRhiSampler *slotSampler = sampler(true, false);
        if (materials && slot.binding >= 4 && slot.binding <= 7) {
            const unsigned int handles[4] = {terrain.materialMap, terrain.materialTextures,
                                             terrain.materialDetails, terrain.materialParams};
            const unsigned int handle = handles[slot.binding - 4];
            texture = RhiTextures::texture(handle);
            if (texture == nullptr)
                texture = slot.type == 1 ? dummyArray : dummy2D;
            else
                slotSampler = sampler(RhiTextures::sampledWithMipmaps(handle),
                                      RhiTextures::clampedToEdge(handle),
                                      RhiTextures::sampledNearest(handle));
            bindingKey.textures.push_back(texture);
            bindingKey.samplers.push_back(slotSampler);
            continue;
        }
        switch (slot.type) {
        case 1: texture = dummyArray; break;
        case 2: texture = dummyCube; break;
        case 3: {
            texture = dummyDepth;
            slotSampler = shadowSampler;
            const int map = slot.binding == 9 ? 0 : slot.binding == 2 ? 1 : slot.binding == 3 ? 2 : -1;
            if (map >= 0 && shadowMaps[map].valid() && Game::shadowsEnabled > 0)
                texture = shadowMaps[map].depth;
            break;
        }
        default:
            texture = dummy2D;
            if (slot.binding == 0 && base != nullptr) {
                texture = base;
                slotSampler = sampler(mipmapped, baseHandle != 0
                                      && RhiTextures::clampedToEdge(baseHandle));
            } else if (slot.binding == 1 && detail != nullptr) {
                texture = detail;
            } else if (program->kind == RhiProgram::WATER && (slot.binding == 4 || slot.binding == 5)) {
                const int layer = slot.binding - 4;
                const unsigned int handle = item->water.layers[layer];
                if (QRhiTexture *layerTexture = handle != 0 ? RhiTextures::texture(handle) : nullptr) {
                    texture = layerTexture;
                    slotSampler = sampler(RhiTextures::sampledWithMipmaps(handle), false);
                    waterLayers |= 1 << layer;
                }
            } else if (program->kind == RhiProgram::WATER && slot.binding == 15) {
                texture = waterWaves();
            }
            break;
        }
        bindingKey.textures.push_back(texture);
        bindingKey.samplers.push_back(slotSampler);
    }
    if (program->kind == RhiProgram::WATER)
        program->setInt("waterLayers", waterLayers);

    DrawCommand draw;
    draw.uniformOffset = appendUniforms(program);
    draw.instanceOffset = appendInstances(matrices, count);
    draw.instances = quint32(count);
    draw.vertexBuffer = buffers.vertexBuffer;
    if (item->mesh.indexed) {
        if (buffers.indexBuffer == nullptr)
            return;
        draw.indexBuffer = buffers.indexBuffer;
        draw.indexOffset = item->mesh.indexOffset;
        draw.indexFormat = item->mesh.indexType == RenderItem::INDEX_U32
                ? QRhiCommandBuffer::IndexUInt32 : QRhiCommandBuffer::IndexUInt16;
        draw.baseVertex = item->mesh.baseVertex;
    } else {
        draw.first = item->mesh.first;
    }
    draw.count = item->mesh.count;
    // Pipeline state as PacketRasterState and ScopedTerrainDecal set it.
    PipelineKey key;
    key.program = program;
    key.topology = quint8(topology(item->mesh.primitive));
    key.layout = quint8(buffers.layout);
    key.format = quint8(buffers.format);
    key.blend = blending && baseProgram != PROGRAM_SELECTION && !shadowTarget();
    key.depthWrite = !(item->pbr.enabled && item->pbr.blend);
    key.decal = item->material.decal && selectionId == 0;
    key.cullBack = !item->material.doubleSided;
    // Shadow maps flipped against the backend's convention turn the
    // winding around.
    key.frontCw = frontCw != (shadowTarget() && !rhi->isYUpInFramebuffer());
    key.wireframe = item->material.wireframe;
    key.lineWidth = quint8(std::clamp(item->material.lineWidth > 0 ? item->material.lineWidth
                                                                  : Game::oglDefaultLineWidth, 1, 255));
    key.pass = target().attachments != nullptr ? target().attachments->pipelinePass() : nullptr;
    // The uniform buffer is known when the pass runs; the resource set and
    // pipeline are made then.
    draw.pipeline = nullptr;
    draw.bindings = nullptr;
    const QSize size = target().attachments != nullptr && target().attachments->valid()
            ? target().attachments->size : QSize(1, 1);
    const float w = viewportRect[2] > 0 ? float(viewportRect[2]) : float(size.width());
    const float h = viewportRect[3] > 0 ? float(viewportRect[3]) : float(size.height());
    draw.viewport = QRhiViewport(float(viewportRect[0]), float(viewportRect[1]), w, h,
                                 depthRange[0], depthRange[1]);
    TargetState &state = target();
    state.keys.push_back(key);
    state.bindings.push_back(std::move(bindingKey));
    state.draws.push_back(draw);
    RenderStats::countDraw(static_cast<RenderStats::Category>(category), 0, item->mesh.count * count);
    RenderStats::countPassDraw(pass);
}

void RhiRenderer::recordInstances(const std::vector<DrawInstance> &instances, int pass,
                                  bool grouped) {
    std::vector<const float *> matrices;
    if (!grouped) {
        for (const DrawInstance &instance : instances) {
            if (!hasMesh(instance.packet) || !visible(instance, cullFrustum))
                continue;
            const float *matrix = instanceMatrix(instance.matrix);
            recordDraw(instance.packet, &matrix, 1, instance.selectionId, pass, instance.category);
        }
        return;
    }
    planGroups(instances);
    for (const GroupPlan &plan : groupPlans) {
        RenderItem *item = instances[plan.begin].packet;
        if (!hasMesh(item) || plan.visible == 0)
            continue;
        if (RenderStats::inFrame())
            RenderStats::current().groupedPackets++;
        if (plan.base >= 0) {
            matrices.clear();
            for (size_t k = plan.begin; k < plan.end; ++k)
                if (instanceVisible[k])
                    matrices.push_back(instanceMatrix(instances[k].matrix));
            size_t first = plan.begin;
            while (!instanceVisible[first])
                ++first;
            recordDraw(item, matrices.data(), int(matrices.size()), instances[first].selectionId,
                       pass, instances[first].category);
            continue;
        }
        for (size_t k = plan.begin; k < plan.end; ++k) {
            if (!instanceVisible[k])
                continue;
            const float *matrix = instanceMatrix(instances[k].matrix);
            recordDraw(item, &matrix, 1, instances[k].selectionId, pass, instances[k].category);
        }
    }
}

void RhiRenderer::drawPasses(RenderPass first, RenderPass last, bool consume) {
    beginFrameIfNeeded();
    if (!targetReady())
        return;
    for (int pass = first; pass <= last; ++pass) {
        PassQueue &queue = passes[pass];
        if (queue.ordered.empty() && queue.grouped.empty())
            continue;
        recordInstances(queue.ordered, pass, false);
        if (pass == PASS_BLENDED || pass == PASS_TRANSMISSION)
            sortBackToFront(queue.grouped);
        else
            sortByTexture(queue.grouped);
        recordInstances(queue.grouped, pass, true);
        if (consume)
            consumePass(queue);
    }
}

void RhiRenderer::renderPasses(RenderPass first, RenderPass last) {
    drawPasses(first, last, true);
}

void RhiRenderer::renderPassesRetained(RenderPass first, RenderPass last) {
    drawPasses(first, last, false);
}

void RhiRenderer::renderPassesMeasured(RenderPass first, RenderPass last) {
    drawPasses(first, last, true);
}

void RhiRenderer::renderShadowCasters(float range, int statsSlot, const float *viewProjection) {
    beginFrameIfNeeded();
    if (!targetReady())
        return;
    // Casters in range and inside the light view, instanced by packet.
    planShadowCasters(range, viewProjection);
    std::vector<const float *> matrices;
    for (const GroupPlan &plan : groupPlans) {
        RenderItem *item = shadowCasters[plan.begin]->packet;
        matrices.clear();
        for (size_t k = plan.begin; k < plan.end; ++k)
            matrices.push_back(instanceMatrix(shadowCasters[k]->matrix));
        recordDraw(item, matrices.data(), int(matrices.size()), 0, statsSlot,
                   shadowCasters[plan.begin]->category);
    }
}

void RhiRenderer::flushTarget() {
    RhiRenderSurface *s = surface();
    TargetState &state = target();
    if (s == nullptr || s->frame().commandBuffer == nullptr)
        return;
    if (state.draws.empty() && !state.clearColor && !state.clearDepth)
        return;
    if (!targetReady())
        return;
    QRhiCommandBuffer *cb = s->frame().commandBuffer;
    if (QRhiResourceUpdateBatch *textures = RhiTextures::takeUpdates())
        cb->resourceUpdate(textures);
    if (frameBatch == nullptr)
        frameBatch = rhi->nextResourceUpdateBatch();
    QRhiBuffer *uniforms = uploadArena(uniformArena, QRhiBuffer::UniformBuffer, frameBatch);
    QRhiBuffer *instances = uploadArena(instanceArena, QRhiBuffer::VertexBuffer, frameBatch);
    // Resolve pipelines and resource sets now that the buffers are known.
    for (size_t i = 0; i < state.draws.size(); ++i) {
        BindingKey &key = state.bindings[i];
        key.uniforms = uniforms;
        state.draws[i].bindings = bindings(key);
        state.draws[i].pipeline = state.draws[i].bindings != nullptr ? pipeline(state.keys[i]) : nullptr;
        state.draws[i].instanceBuffer = instances;
    }
    // Clears happen at the start of the pass; otherwise the contents stay.
    const int clears = (state.clearColor ? 1 : 0) | (state.clearDepth ? 2 : 0);
    cb->beginPass(state.attachments->targets[clears], state.color, {1.0f, 0}, frameBatch);
    frameBatch = nullptr;
    for (const DrawCommand &draw : state.draws) {
        if (draw.pipeline == nullptr || draw.bindings == nullptr)
            continue;
        cb->setGraphicsPipeline(draw.pipeline);
        cb->setViewport(draw.viewport);
        const QRhiCommandBuffer::DynamicOffset offset(RhiShaderSource::UniformBlockBinding,
                                                      draw.uniformOffset);
        cb->setShaderResources(draw.bindings, 1, &offset);
        const QRhiCommandBuffer::VertexInput inputs[2] = {
            {draw.vertexBuffer, 0}, {draw.instanceBuffer, draw.instanceOffset}};
        if (draw.indexBuffer != nullptr) {
            cb->setVertexInput(0, 2, inputs, draw.indexBuffer, draw.indexOffset, draw.indexFormat);
            cb->drawIndexed(draw.count, draw.instances, 0, draw.baseVertex, 0);
        } else {
            cb->setVertexInput(0, 2, inputs);
            cb->draw(draw.count, draw.instances, draw.first, 0);
        }
    }
    cb->endPass();
    state.draws.clear();
    state.keys.clear();
    state.bindings.clear();
    state.clearColor = state.clearDepth = false;
}

void RhiRenderer::present() {
    RhiRenderSurface *s = surface();
    if (s == nullptr || s->frame().commandBuffer == nullptr || !view.valid())
        return;
    const RhiRenderSurface::Frame &frame = s->frame();
    if (presentPipeline == nullptr || presentPassKey != frame.passDescriptor
            || !presentPassKey->isCompatible(frame.passDescriptor)) {
        delete presentPipeline;
        presentPipeline = nullptr;
    }
    if (presentSampler == nullptr) {
        presentSampler = rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
                                         QRhiSampler::None, QRhiSampler::ClampToEdge,
                                         QRhiSampler::ClampToEdge);
        presentSampler->create();
    }
    if (presentBindings == nullptr) {
        presentBindings = rhi->newShaderResourceBindings();
        presentBindings->setBindings({QRhiShaderResourceBinding::sampledTexture(
                                          0, QRhiShaderResourceBinding::FragmentStage, view.color,
                                          presentSampler)});
        presentBindings->create();
        delete presentPipeline;
        presentPipeline = nullptr;
    }
    if (presentPipeline == nullptr) {
        static QShader vertex, fragment;
        if (!vertex.isValid()) {
            vertex = bakeInline(PresentVertex, QShader::VertexStage, rhi);
            fragment = bakeInline(PresentFragment, QShader::FragmentStage, rhi);
        }
        presentPipeline = rhi->newGraphicsPipeline();
        presentPipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex},
                                          {QRhiShaderStage::Fragment, fragment}});
        presentPipeline->setShaderResourceBindings(presentBindings);
        presentPipeline->setRenderPassDescriptor(frame.passDescriptor);
        presentPipeline->create();
        presentPassKey = frame.passDescriptor;
    }
    QRhiCommandBuffer *cb = frame.commandBuffer;
    cb->beginPass(frame.target, Qt::black, {1.0f, 0});
    cb->setGraphicsPipeline(presentPipeline);
    cb->setViewport(QRhiViewport(0, 0, float(frame.pixelSize.width()), float(frame.pixelSize.height())));
    cb->setShaderResources(presentBindings);
    cb->draw(3);
    cb->endPass();
}

void RhiRenderer::renderFrame() {
    if (traceDraws && !debugCounts.isEmpty()) {
        for (auto it = debugCounts.cbegin(); it != debugCounts.cend(); ++it)
            qInfo().noquote() << "rhi-trace" << it.key() << it.value();
        debugCounts.clear();
    }
    drawPasses(PASS_SKY, PASS_UI, true);
    flushTarget();
    // A selection pass ends in its ids, not on screen.
    if (currentTarget != TargetSelection)
        present();
    clearQueues();
    Renderer::renderFrame();
}

void RhiRenderer::resetFrame() {
    beginFrameIfNeeded();
    QueueRenderer::resetFrame();
}
