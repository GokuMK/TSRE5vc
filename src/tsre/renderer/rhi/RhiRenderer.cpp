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
#include "RhiProgram.h"
#include "RhiRenderSurface.h"
#include "RhiShaderSource.h"
#include "RhiTextures.h"
#include <QDebug>
#include <QFloat16>
#include <QElapsedTimer>
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

static QHash<QString, int> debugCounts;
static const bool traceDraws = qEnvironmentVariableIsSet("TSRE_RHI_TRACE");
static void debugCount(const QString &key) { if (traceDraws) debugCounts[key]++; }

// The present pass's fragment shader (RhiImage.cpp).
extern const char *RhiPresentFragment;

namespace {

constexpr int InstanceFloats = 20;
constexpr int InstanceStride = InstanceFloats * sizeof(float);
constexpr quint32 ArenaChunk = 4u << 20;

const float Identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

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



}

bool RhiRenderer::PipelineKey::operator==(const PipelineKey &o) const {
    return program == o.program && pass == o.pass && topology == o.topology && layout == o.layout
            && format == o.format && blend == o.blend && depthWrite == o.depthWrite
            && decal == o.decal && cullBack == o.cullBack && frontCw == o.frontCw
            && wireframe == o.wireframe && lineWidth == o.lineWidth && colorCount == o.colorCount;
}

size_t RhiRenderer::PipelineKeyHash::operator()(const PipelineKey &k) const {
    size_t h = std::hash<const void *>()(k.program) ^ (std::hash<const void *>()(k.pass) << 1);
    const quint32 bits = k.topology | (k.layout << 4) | (k.format << 10) | (k.blend << 12)
            | (k.depthWrite << 13) | (k.decal << 14) | (k.cullBack << 15) | (k.frontCw << 16)
            | (k.wireframe << 17) | (quint32(k.lineWidth) << 18) | (quint32(k.colorCount) << 26);
    return h ^ (std::hash<quint32>()(bits) * 0x9E3779B97F4A7C15ull);
}

bool RhiRenderer::BindingKey::operator==(const BindingKey &o) const {
    return program == o.program && uniforms == o.uniforms && textures == o.textures
            && samplers == o.samplers;
}

size_t RhiRenderer::BindingKeyHash::operator()(const BindingKey &k) const {
    size_t h = std::hash<const void *>()(k.program) ^ (std::hash<const void *>()(k.uniforms) << 1);
    for (QRhiTexture *texture : k.textures)
        h = h * 31 + std::hash<const void *>()(texture);
    for (QRhiSampler *sampler : k.samplers)
        h = h * 17 + std::hash<const void *>()(sampler);
    return h;
}

RhiRenderer::RhiRenderer(RhiContext *context)
    : context(context), rhi(context->rhi()), gluu(GLUU::get()) {
    uniformStride = quint32(rhi->ubufAligned(1));
    baseInstance = rhi->isFeatureSupported(QRhi::BaseInstance);
    targets[TARGET_VIEW].attachments = &view;
    targets[TargetSelection].attachments = &selection;
    // Programs: the main program and its variants, selection, shadows.
    struct Definition { RhiProgram::Kind kind; const char *vertex; const char *fragment; QStringList defines; };
    const Definition definitions[] = {
        {RhiProgram::MAIN, "StandardFog", mainFragment, {}},
        {RhiProgram::TERRAIN, "StandardFog", mainFragment, {"TSRE_TERRAIN"}},
        {RhiProgram::UNLIT, "StandardFog", mainFragment, {"TSRE_UNLIT"}},
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
    dummyBlack = rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
    dummyBlack->create();
    shadowSampler = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                    QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    shadowSampler->setTextureCompareOp(QRhiSampler::LessOrEqual);
    shadowSampler->create();
    // White placeholders; the depth stand-in is cleared to the far plane.
    QRhiResourceUpdateBatch *batch = RhiTextures::updates();
    if (batch != nullptr) {
        QImage white(1, 1, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        batch->uploadTexture(dummy2D, white);
        QImage black(1, 1, QImage::Format_RGBA8888);
        black.fill(Qt::black);
        batch->uploadTexture(dummyBlack, black);
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
    if (RhiRenderSurface *s = surface())
        s->setFrameEnd(nullptr);
    // A read in flight writes into latestDepth when it completes.
    if (latestDepth && latestDepth->pending)
        rhi->finish();
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
    delete overlayPipeline;
    overlayPipeline = nullptr;
    delete overlayBindings;
    overlayBindings = nullptr;
    delete overlayTexture;
    overlayTexture = nullptr;
    delete presentPipeline;
    presentPipeline = nullptr;
    delete presentBindings;
    presentBindings = nullptr;
    delete presentSampler;
    presentSampler = nullptr;
    releaseAmbientOcclusion();
    releaseBloom();
    releaseGlowClear();
    delete presentUniforms;
    delete presentLinear;
    delete dummyBlack;
    presentUniforms = nullptr;
    presentLinear = nullptr;
    dummyBlack = nullptr;
    releaseEnvironment();
    releaseReflection();
    delete sceneCopy;
    sceneCopy = nullptr;
    releaseDepthProbe();
    releaseLights();
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
    const QMatrix4x4 correction = targetCorrection();
    Mat4::multiply(corrected, const_cast<float *>(correction.constData()), gluu->pMatrix);
    program->setMat4("uPMatrix", corrected);
    program->setMat4("uFMatrix", gluu->fMatrix);
    if (program->kind == RhiProgram::SHADOW) {
        // The shadow program draws with the light's matrix.
        const QMatrix4x4 shadowCorrection = openGlRowCorrection();
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
    program->setVec("diffuseColor", gluu->diffuseColor[0], gluu->diffuseColor[1], gluu->diffuseColor[2],
                    gluu->diffuseColor[3]);
    program->setVec("ambientColor", gluu->ambientColor[0], gluu->ambientColor[1], gluu->ambientColor[2],
                    gluu->ambientColor[3]);
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
    writeLightUniforms(program);
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
    if (program->kind == RhiProgram::PBR && item->pbr.enabled) {
        const RenderItem::Pbr &p = item->pbr;
        program->setVec("pbrBaseColor", p.baseColor[0], p.baseColor[1], p.baseColor[2], p.baseColor[3]);
        program->setVec("pbrMetallicRoughness", p.metallic, p.roughness);
        program->setVec("pbrEmissive", p.emissive[0], p.emissive[1], p.emissive[2]);
        program->setFloat("pbrNormalScale", p.normalScale);
        program->setFloat("pbrOcclusionStrength", p.occlusionStrength);
        program->setFloat("pbrAlphaCutoff", p.alphaCutoff);
        program->setInt("pbrBlend", p.blend ? 1 : 0);
        program->setInt("pbrUnlit", p.unlit ? 1 : 0);
        program->setVec("pbrClearcoat", p.clearcoat, p.clearcoatRoughness, p.clearcoatNormalScale);
        program->setVec("pbrSpecular", p.specularColor[0], p.specularColor[1], p.specularColor[2],
                        p.specular);
        program->setFloat("pbrIor", p.ior);
        program->setVec("pbrTransmission", p.transmission, p.thickness, p.attenuationDistance,
                        sceneCopyLevels);
        program->setVec("pbrAttenuationColor", p.attenuationColor[0], p.attenuationColor[1],
                        p.attenuationColor[2]);
        int texCoords = 0;
        int transformed = 0;
        float transforms[RenderItem::Pbr::MAP_COUNT * 6];
        for (int map = 0; map < RenderItem::Pbr::MAP_COUNT; ++map) {
            if (p.texCoords[map] != 0)
                texCoords |= 1 << map;
            std::copy(p.uvTransforms[map].rows, p.uvTransforms[map].rows + 6, transforms + map * 6);
            if (!p.uvTransforms[map].identity())
                transformed |= 1 << map;
        }
        program->setInt("pbrTexCoords", texCoords);
        program->setInt("pbrUvTransforms", transformed);
        if (transformed != 0)
            program->setVec3Array("pbrUvTransform", transforms, RenderItem::Pbr::MAP_COUNT * 2);
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
    return wrapSampler(mipmaps, clamp ? 1 : 0, clamp ? 1 : 0, nearest);
}

QRhiSampler *RhiRenderer::wrapSampler(bool mipmaps, int addressU, int addressV, bool nearest) {
    const int key = (mipmaps ? 1 : 0) | (nearest ? 2 : 0) | (addressU << 2) | (addressV << 4);
    QRhiSampler *found = samplers.value(key, nullptr);
    if (found != nullptr)
        return found;
    auto mode = [](int address) {
        return address == 1 ? QRhiSampler::ClampToEdge
                            : address == 2 ? QRhiSampler::Mirror : QRhiSampler::Repeat;
    };
    const QRhiSampler::Filter filter = nearest ? QRhiSampler::Nearest : QRhiSampler::Linear;
    QRhiSampler *created = rhi->newSampler(filter, filter,
                                           mipmaps ? QRhiSampler::Linear : QRhiSampler::None,
                                           mode(addressU), mode(addressV));
    created->create();
    samplers.insert(key, created);
    return created;
}

QRhiTexture *RhiRenderer::libraryTexture(int textureId, bool &mipmapped) {
    mipmapped = false;
    const auto found = TexLib::mtex.find(textureId);
    if (found == TexLib::mtex.end() || found->second == nullptr)
        return nullptr;
    Texture *texture = found->second;
    if (!texture->glLoaded && texture->loaded)
        texture->GLTextures();
    if (!texture->glLoaded || texture->tex == nullptr)
        return nullptr;
    const unsigned int handle = texture->tex[0];
    if (TexLib::disabledTextures.value(int(handle), 0) == 1)
        return nullptr;
    mipmapped = RhiTextures::sampledWithMipmaps(handle);
    return RhiTextures::texture(handle);
}

float RhiRenderer::copyFrameForTransmission() {
    // Secondary views and the selection program use no copy: transmissive
    // surfaces there see the environment.
    if (secondaryView || baseProgram != PROGRAM_MAIN || currentTarget != TARGET_VIEW
            || !view.valid())
        return 0.0f;
    RhiRenderSurface *s = surface();
    if (s == nullptr || s->frame().commandBuffer == nullptr)
        return 0.0f;
    flushTarget();
    if (sceneCopy == nullptr || sceneCopy->pixelSize() != view.size
            || sceneCopy->format() != view.color->format()) {
        delete sceneCopy;
        sceneCopy = rhi->newTexture(view.color->format(), view.size, 1,
                                    QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips);
        if (!sceneCopy->create()) {
            delete sceneCopy;
            sceneCopy = nullptr;
            return 0.0f;
        }
    }
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->copyTexture(sceneCopy, view.color);
    batch->generateMips(sceneCopy);
    s->frame().commandBuffer->resourceUpdate(batch);
    return 1.0f + std::floor(std::log2(float(std::max(view.size.width(), view.size.height()))));
}

void RhiRenderer::beginFrameIfNeeded() {
    RhiRenderSurface *s = surface();
    const quint64 serial = s != nullptr ? s->frameSerial() : 0;
    if (serial == frameSerial)
        return;
    frameSerial = serial;
    if (traceDraws)
        qInfo().noquote() << "rhi-trace frame uniform bytes" << uniformArena.data.size()
                          << "instance bytes" << instanceArena.data.size();
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
                                    const QSize &size, QRhiTexture::Flags colorFlags,
                                    QRhiTexture::Format secondFormat,
                                    QRhiTexture::Format thirdFormat) {
    releaseAttachments(attachments);
    attachments.size = size;
    attachments.ownsColor = attachments.ownsDepth = true;
    attachments.colorLayer = attachments.colorLevel = 0;
    if (colorFormat != QRhiTexture::UnknownFormat) {
        attachments.color = rhi->newTexture(colorFormat, size, 1, QRhiTexture::RenderTarget | colorFlags);
        if (!attachments.color->create()) {
            releaseAttachments(attachments);
            return false;
        }
    }
    if (secondFormat != QRhiTexture::UnknownFormat) {
        attachments.color2 = rhi->newTexture(secondFormat, size, 1, QRhiTexture::RenderTarget);
        if (!attachments.color2->create()) {
            releaseAttachments(attachments);
            return false;
        }
    }
    if (thirdFormat != QRhiTexture::UnknownFormat) {
        attachments.color3 = rhi->newTexture(thirdFormat, size, 1, QRhiTexture::RenderTarget);
        if (!attachments.color3->create()) {
            releaseAttachments(attachments);
            return false;
        }
    }
    attachments.depth = rhi->newTexture(QRhiTexture::D32F, size, 1, QRhiTexture::RenderTarget);
    if (!attachments.depth->create()) {
        releaseAttachments(attachments);
        return false;
    }
    return buildTargets(attachments);
}

bool RhiRenderer::buildTargets(Attachments &attachments) {
    for (int clears = 0; clears < 4; ++clears) {
        QRhiTextureRenderTargetDescription description;
        if (attachments.color != nullptr) {
            QRhiColorAttachment color(attachments.color);
            color.setLayer(attachments.colorLayer);
            color.setLevel(attachments.colorLevel);
            if (attachments.color3 != nullptr)
                description.setColorAttachments({color, QRhiColorAttachment(attachments.color2),
                                                 QRhiColorAttachment(attachments.color3)});
            else if (attachments.color2 != nullptr)
                description.setColorAttachments({color, QRhiColorAttachment(attachments.color2)});
            else
                description.setColorAttachments({color});
        }
        if (attachments.depth != nullptr)
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
    if (attachments.ownsColor) {
        delete attachments.color;
        delete attachments.color2;
        delete attachments.color3;
    }
    attachments.color2 = attachments.color3 = nullptr;
    if (attachments.ownsDepth)
        delete attachments.depth;
    attachments.color = attachments.depth = nullptr;
    attachments.size = QSize();
}

bool RhiRenderer::ensureViewTarget(const QSize &size) {
    // HDR draws into floats; ambient occlusion needs the ambient light
    // share, bloom the glow (and, as its outputs are numbered, the ambient
    // share before it).
    const QRhiTexture::Format format = viewFormat();
    const bool glow = bloomEnabled();
    const bool ambient = ambientOcclusionQuality() > 0 || glow;
    if (view.valid() && view.size == size && view.color->format() == format
            && (view.color2 != nullptr) == ambient && (view.color3 != nullptr) == glow)
        return true;
    delete presentBindings;
    presentBindings = nullptr;
    releaseAmbientOcclusion();
    releaseBloom();
    releaseGlowClear();
    return createAttachments(view, format, size, QRhiTexture::UsedAsTransferSource,
                             ambient ? QRhiTexture::RGBA8 : QRhiTexture::UnknownFormat,
                             glow ? QRhiTexture::RGBA16F : QRhiTexture::UnknownFormat);
}

void RhiRenderer::releaseGlowClear() {
    delete glowClear;
    delete glowClearPass;
    glowClear = nullptr;
    glowClearPass = nullptr;
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
        shadowMaps[map].openGlRows = true;
        targets[TARGET_SHADOW_NEAR + map].attachments = &shadowMaps[map];
    }
}

bool RhiRenderer::openGlRowTarget() const {
    const Attachments *attachments = targets[currentTarget].attachments;
    return attachments != nullptr && attachments->openGlRows;
}

QMatrix4x4 RhiRenderer::targetCorrection() const {
    return openGlRowTarget() ? openGlRowCorrection() : rhi->clipSpaceCorrMatrix();
}

QMatrix4x4 RhiRenderer::openGlRowCorrection() const {
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

QByteArray RhiRenderer::readNow(QRhiTexture *texture, QSize *size) {
    RhiRenderSurface *s = surface();
    if (texture == nullptr || s == nullptr || s->frame().commandBuffer == nullptr)
        return QByteArray();
    flushTarget();
    QRhiReadbackResult result;
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->readBackTexture(QRhiReadbackDescription(texture), &result);
    s->frame().commandBuffer->resourceUpdate(batch);
    // Within a frame this submits the work so far and completes readbacks.
    rhi->finish();
    if (size != nullptr)
        *size = result.pixelSize;
    return result.data;
}

namespace {
// A probed depth in the view's depth range. The scene band draws into
// [0, 0.98] of it; OpenGL clears depth before it instead, so farther bands
// read as the far plane.
float bandDepth(float depth, float rangeNear, float rangeFar) {
    if (depth >= rangeFar || rangeFar <= rangeNear)
        return 1.0f;
    return std::clamp((depth - rangeNear) / (rangeFar - rangeNear), 0.0f, 1.0f);
}
}

float RhiRenderer::readDepth(int x, int y) {
    if (!recordDepthProbe(x, y))
        return 1.0f;
    const QByteArray data = readNow(depthProbe.result);
    if (data.size() < 4)
        return 1.0f;
    float depth = 1.0f;
    std::memcpy(&depth, data.constData(), sizeof(depth));
    return bandDepth(depth, depthRange[0], depthRange[1]);
}

float RhiRenderer::readDepthLatest(int x, int y) {
    if (!latestDepth)
        latestDepth = std::make_unique<LatestDepth>();
    LatestDepth *latest = latestDepth.get();
    if (!latest->valid) {
        // Nothing read yet: wait for this one.
        latest->depth = readDepth(x, y);
        latest->valid = true;
        return latest->depth;
    }
    if (!latest->pending && recordDepthProbe(x, y)) {
        latest->pending = true;
        const float rangeNear = depthRange[0], rangeFar = depthRange[1];
        latest->result.completed = [latest, rangeNear, rangeFar] {
            float depth = 1.0f;
            if (latest->result.data.size() >= 4)
                std::memcpy(&depth, latest->result.data.constData(), sizeof(depth));
            latest->depth = bandDepth(depth, rangeNear, rangeFar);
            latest->pending = false;
        };
        QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
        batch->readBackTexture(QRhiReadbackDescription(depthProbe.result), &latest->result);
        surface()->frame().commandBuffer->resourceUpdate(batch);
    }
    return latest->depth;
}

bool RhiRenderer::recordDepthProbe(int x, int y) {
    beginFrameIfNeeded();
    if (currentTarget != TARGET_VIEW || !targetReady() || x < 0 || y < 0
            || x >= view.size.width() || y >= view.size.height())
        return false;
    RhiRenderSurface *s = surface();
    QRhiCommandBuffer *cb = s->frame().commandBuffer;
    flushTarget();
    DepthProbe &probe = depthProbe;
    if (probe.pipeline == nullptr) {
        static const char *vertex = R"(#version 440
void main() {
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";
        static const char *fragment = R"(#version 440
layout(location = 0) out vec4 result;
layout(binding = 0) uniform sampler2D depthTexture;
layout(std140, binding = 1) uniform Probe { ivec4 position; };
void main() {
    result = vec4(texelFetch(depthTexture, position.xy, 0).r);
}
)";
        probe.result = rhi->newTexture(QRhiTexture::R32F, QSize(1, 1), 1,
                                       QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
        probe.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
        probe.sampler = rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        if (!probe.result->create() || !probe.uniforms->create() || !probe.sampler->create()) {
            releaseDepthProbe();
            return false;
        }
        probe.target = rhi->newTextureRenderTarget({QRhiColorAttachment(probe.result)});
        probe.pass = probe.target->newCompatibleRenderPassDescriptor();
        probe.target->setRenderPassDescriptor(probe.pass);
        probe.bindings = rhi->newShaderResourceBindings();
        probe.bindings->setBindings({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage,
                                                      view.depth, probe.sampler),
            QRhiShaderResourceBinding::uniformBuffer(1, QRhiShaderResourceBinding::FragmentStage,
                                                     probe.uniforms)});
        probe.boundDepth = view.depth;
        probe.pipeline = rhi->newGraphicsPipeline();
        probe.pipeline->setShaderStages({{QRhiShaderStage::Vertex, bakeInline(vertex, QShader::VertexStage, rhi)},
                                         {QRhiShaderStage::Fragment, bakeInline(fragment, QShader::FragmentStage, rhi)}});
        probe.pipeline->setShaderResourceBindings(probe.bindings);
        probe.pipeline->setRenderPassDescriptor(probe.pass);
        if (!probe.target->create() || !probe.bindings->create() || !probe.pipeline->create()) {
            releaseDepthProbe();
            return false;
        }
    }
    if (probe.boundDepth != view.depth) {
        // The view was recreated (resized).
        probe.bindings->setBindings({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage,
                                                      view.depth, probe.sampler),
            QRhiShaderResourceBinding::uniformBuffer(1, QRhiShaderResourceBinding::FragmentStage,
                                                     probe.uniforms)});
        probe.bindings->create();
        probe.boundDepth = view.depth;
    }
    // y counts from the bottom, as glReadPixels does.
    const qint32 position[4] = {x, rhi->isYUpInFramebuffer() ? y : view.size.height() - 1 - y, 0, 0};
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->updateDynamicBuffer(probe.uniforms, 0, 16, position);
    cb->beginPass(probe.target, Qt::black, {1.0f, 0}, batch);
    cb->setGraphicsPipeline(probe.pipeline);
    cb->setViewport(QRhiViewport(0, 0, 1, 1));
    cb->setShaderResources(probe.bindings);
    cb->draw(3);
    cb->endPass();
    return true;
}

void RhiRenderer::readColor(int x, int y, int width, int height, unsigned char *rgba) {
    std::fill(rgba, rgba + qsizetype(width) * height * 4, 0);
    beginFrameIfNeeded();
    if (currentTarget != TARGET_VIEW || !targetReady())
        return;
    QSize size;
    QByteArray data = readNow(view.color, &size);
    // A float view (HDR) as 8-bit colour, clipped.
    if (view.color->format() == QRhiTexture::RGBA16F
            && data.size() >= qsizetype(size.width()) * size.height() * 8) {
        QByteArray bytes(qsizetype(size.width()) * size.height() * 4, '\0');
        const qfloat16 *halves = reinterpret_cast<const qfloat16 *>(data.constData());
        for (qsizetype i = 0; i < bytes.size(); ++i)
            bytes[i] = char(std::clamp(int(float(halves[i]) * 255.0f + 0.5f), 0, 255));
        data = bytes;
    }
    if (data.size() < qsizetype(size.width()) * size.height() * 4)
        return;
    // Rows from the bottom, as glReadPixels returns them.
    for (int row = 0; row < height; ++row) {
        const int sourceY = y + row;
        if (sourceY < 0 || sourceY >= size.height())
            continue;
        const int stored = rhi->isYUpInFramebuffer() ? sourceY : size.height() - 1 - sourceY;
        for (int column = 0; column < width; ++column) {
            const int sourceX = x + column;
            if (sourceX < 0 || sourceX >= size.width())
                continue;
            std::memcpy(rgba + (qsizetype(row) * width + column) * 4,
                        data.constData() + (qsizetype(stored) * size.width() + sourceX) * 4, 4);
        }
    }
}

void RhiRenderer::releaseDepthProbe() {
    DepthProbe &probe = depthProbe;
    delete probe.pipeline;
    delete probe.bindings;
    delete probe.target;
    delete probe.pass;
    delete probe.sampler;
    delete probe.uniforms;
    delete probe.result;
    probe = DepthProbe();
}

void RhiRenderer::beginViewBand(const LayeredView &view, ViewBand band) {
    if (!view.projection)
        return;
    prepareLights();
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
    if (band == BAND_SCENE && !secondaryView && currentTarget == TARGET_VIEW) {
        // Ambient occlusion reads this band's depth back.
        float scene[16];
        view.projection(0.2f, view.sceneFar, scene);
        sceneProjection[0] = scene[0];
        sceneProjection[1] = scene[5];
        sceneProjection[2] = 0.2f;
        sceneProjection[3] = view.sceneFar;
        sceneDepthRange[0] = 0.0f;
        sceneDepthRange[1] = 0.98f;
        sceneProjectionValid = true;
    }
    if (band == BAND_DISTANT) {
        if (view.mirrorPlane != nullptr) {
            // Below the plane only the water bed would show; clip it just
            // under the surface so banks meet the water without a gap.
            const float *plane = view.mirrorPlane;
            const float clip[4] = {plane[0], plane[1], plane[2], plane[3] + 0.05f};
            std::copy(clip, clip + 4, gluu->clipPlane);
        }
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

void RhiRenderer::endView(const LayeredView &view) {
    setViewLimits(nullptr);
    setCullView(nullptr);
    if (view.mirrorPlane != nullptr) {
        const float keep[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        std::copy(keep, keep + 4, gluu->clipPlane);
        applyFrameUniforms();
    }
    frontCw = false;
    depthRange[0] = 0.0f;
    depthRange[1] = 1.0f;
}

quint32 RhiRenderer::appendUniforms(const RhiProgram *program) {
    std::vector<char> &data = uniformArena.data;
    const quint32 size = quint32(program->block.size());
    // A draw with the values of the one before shares its block, so the two
    // keep one resource binding.
    if (!data.empty() && uniformArena.lastSize == size
            && std::memcmp(data.data() + uniformArena.lastOffset, program->block.data(), size) == 0)
        return uniformArena.lastOffset;
    const quint32 offset = quint32((data.size() + uniformStride - 1) / uniformStride * uniformStride);
    data.resize(offset + size);
    std::memcpy(data.data() + offset, program->block.data(), size);
    uniformArena.lastOffset = offset;
    uniformArena.lastSize = size;
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
    QRhiGraphicsPipeline::TargetBlend blend;
    if (key.blend) {
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    }
    // One blend state per colour attachment, alike.
    if (key.colorCount > 0 && (key.blend || key.colorCount > 1))
    {
        QVarLengthArray<QRhiGraphicsPipeline::TargetBlend, 3> blends(key.colorCount, blend);
        ps->setTargetBlends(blends.cbegin(), blends.cend());
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
    // PBR maps on the units the OpenGL renderer uses, with the glTF wrap
    // modes; the frame copy on unit 1 while the transmission pass draws.
    static const int PbrMapUnits[RenderItem::Pbr::MAP_COUNT] = {0, 11, 12, 13, 14, 4, 5, 6, 7,
                                                               15, 16, 17};
    auto wrapMode = [](unsigned short mode) {
        return mode == 0x812F ? 1 : mode == 0x8370 ? 2 : 0;  // clamp, mirror, repeat
    };
    const bool pbr = program->kind == RhiProgram::PBR && item->pbr.enabled && selectionId == 0;
    QRhiTexture *pbrMaps[RenderItem::Pbr::MAP_COUNT] = {};
    QRhiSampler *pbrSamplers[RenderItem::Pbr::MAP_COUNT] = {};
    if (pbr) {
        const RenderItem::Pbr &p = item->pbr;
        int present = 0;
        for (int map = 0; map < RenderItem::Pbr::MAP_COUNT; ++map) {
            const unsigned short *modes = p.wrap[map];
            const bool wrapped = modes[0] != 0 || modes[1] != 0;
            bool mipmaps = false;
            QRhiTexture *texture = nullptr;
            if (map == RenderItem::Pbr::MAP_BASE_COLOR) {
                texture = base;
                mipmaps = mipmapped;
            } else if (p.textures[map] >= 0) {
                texture = libraryTexture(p.textures[map], mipmaps);
                if (texture != nullptr)
                    present |= 1 << (map - 1);
            }
            if (texture == nullptr)
                continue;
            pbrMaps[map] = texture;
            // A wrapped map samples its mipmaps, as the OpenGL sampler
            // objects do.
            pbrSamplers[map] = wrapped ? wrapSampler(true, wrapMode(modes[0]), wrapMode(modes[1]))
                                       : sampler(mipmaps, false);
        }
        program->setInt("pbrTextures", present);
    }
    // What each texture unit holds for this program, as the OpenGL renderer
    // binds them: the packet texture on 0, the detail texture on 1, the
    // water layers on 4 and 5 and the wave map on 15.
    int waterLayers = 0;
    for (const RhiProgram::Sampler &slot : program->samplers) {
        QRhiTexture *texture = nullptr;
        QRhiSampler *slotSampler = sampler(true, false);
        if (slot.binding == RhiShaderSource::TerrainPatchDataBinding) {
            texture = item->terrain.paged
                    ? Meshes::dataTextureRhi(item->terrain.paramsBuffer, rhi, frameBatch) : nullptr;
            if (texture == nullptr)
                texture = dummy2D;
            bindingKey.textures.push_back(texture);
            bindingKey.samplers.push_back(sampler(false, true, true));
            continue;
        }
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
        case 2:
            texture = dummyCube;
            if (slot.binding == EnvironmentMap::TextureUnit && environment.sampled != nullptr) {
                texture = environment.sampled;
                slotSampler = environment.sampler;
            }
            break;
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
            if (slot.binding >= 22 && slot.binding <= 24) {
                QRhiTexture *grid[3] = {lightData, lightCells, lightIndices};
                if (grid[slot.binding - 22] != nullptr && !lightGrid.empty()) {
                    texture = grid[slot.binding - 22];
                    slotSampler = sampler(false, true, true);
                }
                break;
            }
            if (pbr && slot.binding == 1) {
                if (sceneCopyLevels > 0.0f && sceneCopy != nullptr) {
                    texture = sceneCopy;
                    slotSampler = sampler(true, true);
                }
                break;
            }
            if (pbr) {
                int map = -1;
                for (int m = 0; m < RenderItem::Pbr::MAP_COUNT; ++m)
                    if (PbrMapUnits[m] == slot.binding && pbrMaps[m] != nullptr)
                        map = m;
                if (map >= 0) {
                    texture = pbrMaps[map];
                    slotSampler = pbrSamplers[map];
                    break;
                }
                if (slot.binding != 0)
                    break;
            }
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
            } else if (program->kind == RhiProgram::WATER
                       && slot.binding == PlanarReflection::TextureUnit && reflectionSampled != nullptr) {
                texture = reflectionSampled;
                slotSampler = sampler(true, true);
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
    key.blend = blending && baseProgram != PROGRAM_SELECTION && target().attachments != nullptr
            && target().attachments->color != nullptr;
    key.depthWrite = !(item->pbr.enabled && item->pbr.blend);
    key.decal = item->material.decal && selectionId == 0;
    key.cullBack = !item->material.doubleSided;
    // Targets flipped against the backend's convention turn the winding
    // around.
    key.frontCw = frontCw != (openGlRowTarget() && !rhi->isYUpInFramebuffer());
    key.wireframe = item->material.wireframe;
    key.lineWidth = quint8(std::clamp(item->material.lineWidth > 0 ? item->material.lineWidth
                                                                  : Game::oglDefaultLineWidth, 1, 255));
    key.pass = target().attachments != nullptr ? target().attachments->pipelinePass() : nullptr;
    key.colorCount = target().attachments == nullptr || target().attachments->color == nullptr ? 0
            : target().attachments->color3 != nullptr ? 3
            : target().attachments->color2 != nullptr ? 2 : 1;
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
    // Views drawn without bands (Shape Viewer) gather the lights here.
    prepareLights();
    if (!targetReady())
        return;
    for (int pass = first; pass <= last; ++pass) {
        PassQueue &queue = passes[pass];
        if (queue.ordered.empty() && queue.grouped.empty())
            continue;
        // Occlusion of the opaque scene, before blended and later passes.
        if (pass > PASS_ALPHA_TEST)
            applyAmbientOcclusion();
        if (pass == PASS_TRANSMISSION)
            sceneCopyLevels = copyFrameForTransmission();
        recordInstances(queue.ordered, pass, false);
        if (pass == PASS_BLENDED || pass == PASS_TRANSMISSION)
            sortBackToFront(queue.grouped);
        else
            sortByTexture(queue.grouped);
        recordInstances(queue.grouped, pass, true);
        sceneCopyLevels = 0.0f;
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
    // Resolve pipelines and resource sets now that the buffers are known;
    // a draw with the keys of the one before takes its results.
    for (size_t i = 0; i < state.draws.size(); ++i) {
        BindingKey &key = state.bindings[i];
        key.uniforms = uniforms;
        DrawCommand &draw = state.draws[i];
        if (i > 0 && key == state.bindings[i - 1])
            draw.bindings = state.draws[i - 1].bindings;
        else
            draw.bindings = bindings(key);
        if (draw.bindings == nullptr)
            draw.pipeline = nullptr;
        else if (i > 0 && state.draws[i - 1].bindings != nullptr && state.keys[i] == state.keys[i - 1])
            draw.pipeline = state.draws[i - 1].pipeline;
        else
            draw.pipeline = pipeline(state.keys[i]);
        draw.instanceBuffer = instances;
    }
    // Clears happen at the start of the pass; otherwise the contents stay.
    int clears = (state.clearColor ? 1 : 0) | (state.clearDepth ? 2 : 0);
    if ((clears & 1) && state.attachments->color3 != nullptr) {
        // The glow (bloom) starts black, not in the background colour: the
        // clear gets a pass of its own, then the glow is cleared again.
        if (glowClear == nullptr) {
            glowClear = rhi->newTextureRenderTarget({QRhiColorAttachment(state.attachments->color3)});
            glowClearPass = glowClear->newCompatibleRenderPassDescriptor();
            glowClear->setRenderPassDescriptor(glowClearPass);
            glowClear->create();
        }
        cb->beginPass(state.attachments->targets[clears], state.color, {1.0f, 0}, frameBatch);
        frameBatch = nullptr;
        cb->endPass();
        cb->beginPass(glowClear, Qt::black, {1.0f, 0});
        cb->endPass();
        clears = 0;
    }
    cb->beginPass(state.attachments->targets[clears], state.color, {1.0f, 0}, frameBatch);
    frameBatch = nullptr;
    // State a draw shares with the one before is not set again: on OpenGL
    // every setShaderResources applies all uniforms and textures, and every
    // setVertexInput all attributes. The instance and index offsets go to
    // the draw (firstInstance, firstIndex) where they can, so the draws of
    // one mesh keep one vertex input.
    QRhiGraphicsPipeline *boundPipeline = nullptr;
    QRhiShaderResourceBindings *boundBindings = nullptr;
    quint32 boundUniformOffset = 0;
    QRhiViewport boundViewport;
    bool viewportBound = false;
    struct VertexState {
        QRhiBuffer *vertex = nullptr;
        quint32 instanceOffset = 0;
        QRhiBuffer *index = nullptr;
        quint32 indexOffset = 0;
        QRhiCommandBuffer::IndexFormat format = QRhiCommandBuffer::IndexUInt16;
        bool operator==(const VertexState &o) const {
            return vertex == o.vertex && instanceOffset == o.instanceOffset && index == o.index
                    && indexOffset == o.indexOffset && format == o.format;
        }
    } boundVertex;
    bool vertexBound = false;
    for (const DrawCommand &draw : state.draws) {
        if (draw.pipeline == nullptr || draw.bindings == nullptr)
            continue;
        if (draw.pipeline != boundPipeline) {
            cb->setGraphicsPipeline(draw.pipeline);
            boundPipeline = draw.pipeline;
            // A new pipeline takes its resources and inputs again.
            boundBindings = nullptr;
            viewportBound = vertexBound = false;
        }
        if (!viewportBound || draw.viewport != boundViewport) {
            cb->setViewport(draw.viewport);
            boundViewport = draw.viewport;
            viewportBound = true;
        }
        if (draw.bindings != boundBindings || draw.uniformOffset != boundUniformOffset) {
            const QRhiCommandBuffer::DynamicOffset offset(RhiShaderSource::UniformBlockBinding,
                                                          draw.uniformOffset);
            cb->setShaderResources(draw.bindings, 1, &offset);
            debugCount("call setShaderResources");
            boundBindings = draw.bindings;
            boundUniformOffset = draw.uniformOffset;
        }
        VertexState vertex;
        vertex.vertex = draw.vertexBuffer;
        quint32 firstInstance = 0;
        if (baseInstance)
            firstInstance = draw.instanceOffset / InstanceStride;
        else
            vertex.instanceOffset = draw.instanceOffset;
        quint32 firstIndex = 0;
        if (draw.indexBuffer != nullptr) {
            vertex.index = draw.indexBuffer;
            vertex.format = draw.indexFormat;
            const quint32 indexSize = draw.indexFormat == QRhiCommandBuffer::IndexUInt32 ? 4 : 2;
            if (draw.indexOffset % indexSize == 0)
                firstIndex = draw.indexOffset / indexSize;
            else
                vertex.indexOffset = draw.indexOffset;
        }
        if (!vertexBound || !(vertex == boundVertex)) {
            const QRhiCommandBuffer::VertexInput inputs[2] = {
                {draw.vertexBuffer, 0}, {draw.instanceBuffer, vertex.instanceOffset}};
            if (draw.indexBuffer != nullptr)
                cb->setVertexInput(0, 2, inputs, vertex.index, vertex.indexOffset, vertex.format);
            else
                cb->setVertexInput(0, 2, inputs);
            debugCount("call setVertexInput");
            boundVertex = vertex;
            vertexBound = true;
        }
        debugCount("call draw");
        if (draw.indexBuffer != nullptr)
            cb->drawIndexed(draw.count, draw.instances, firstIndex, draw.baseVertex, firstInstance);
        else
            cb->draw(draw.count, draw.instances, draw.first, firstInstance);
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
    // Bloom from the view's glow, before the frame's pass begins.
    QRhiTexture *bloomTexture = renderBloom(frame.commandBuffer);
    QRhiTexture *bloomSource = bloomTexture != nullptr ? bloomTexture : dummyBlack;
    if (presentUniforms == nullptr) {
        presentUniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
        presentUniforms->create();
        presentLinear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        presentLinear->create();
    }
    if (presentBindings != nullptr && presentBloomTexture != bloomSource) {
        delete presentBindings;
        presentBindings = nullptr;
    }
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
        const auto fragment = QRhiShaderResourceBinding::FragmentStage;
        presentBindings = rhi->newShaderResourceBindings();
        presentBindings->setBindings({
            QRhiShaderResourceBinding::sampledTexture(0, fragment, view.color, presentSampler),
            QRhiShaderResourceBinding::sampledTexture(1, fragment, bloomSource, presentLinear),
            QRhiShaderResourceBinding::uniformBuffer(2, fragment, presentUniforms)});
        presentBindings->create();
        presentBloomTexture = bloomSource;
        delete presentPipeline;
        presentPipeline = nullptr;
    }
    if (presentPipeline == nullptr) {
        static QShader vertex, fragment;
        if (!vertex.isValid()) {
            vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
            fragment = bakeInline(RhiPresentFragment, QShader::FragmentStage, rhi);
        }
        presentPipeline = rhi->newGraphicsPipeline();
        presentPipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex},
                                          {QRhiShaderStage::Fragment, fragment}});
        presentPipeline->setShaderResourceBindings(presentBindings);
        presentPipeline->setRenderPassDescriptor(frame.passDescriptor);
        presentPipeline->create();
        presentPassKey = frame.passDescriptor;
    }
    // The overlay, painted top row first, over the view.
    QRhiResourceUpdateBatch *overlayUpload = nullptr;
    const QImage *overlay = s->overlay();
    if (overlay != nullptr && !overlay->isNull()) {
        if (overlayTexture == nullptr || overlayTexture->pixelSize() != overlay->size()) {
            delete overlayBindings;
            overlayBindings = nullptr;
            delete overlayTexture;
            overlayTexture = rhi->newTexture(QRhiTexture::RGBA8, overlay->size());
            if (!overlayTexture->create()) {
                delete overlayTexture;
                overlayTexture = nullptr;
            }
        }
        if (overlayTexture != nullptr && overlayBindings == nullptr) {
            overlayBindings = rhi->newShaderResourceBindings();
            overlayBindings->setBindings({QRhiShaderResourceBinding::sampledTexture(
                                              0, QRhiShaderResourceBinding::FragmentStage,
                                              overlayTexture, presentSampler)});
            overlayBindings->create();
            delete overlayPipeline;
            overlayPipeline = nullptr;
        }
        if (overlayPipeline != nullptr && overlayPassKey != frame.passDescriptor) {
            delete overlayPipeline;
            overlayPipeline = nullptr;
        }
        if (overlayTexture != nullptr && overlayPipeline == nullptr) {
            static const char *fragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D overlay;
void main() {
    fragColor = texture(overlay, uv);
}
)";
            static QShader vertex, overlayFragment;
            if (!overlayFragment.isValid()) {
                vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
                overlayFragment = bakeInline(fragment, QShader::FragmentStage, rhi);
            }
            overlayPipeline = rhi->newGraphicsPipeline();
            overlayPipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex},
                                              {QRhiShaderStage::Fragment, overlayFragment}});
            QRhiGraphicsPipeline::TargetBlend blend;
            blend.enable = true;
            blend.srcColor = QRhiGraphicsPipeline::One;
            blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            blend.srcAlpha = QRhiGraphicsPipeline::One;
            blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            overlayPipeline->setTargetBlends({blend});
            overlayPipeline->setShaderResourceBindings(overlayBindings);
            overlayPipeline->setRenderPassDescriptor(frame.passDescriptor);
            overlayPipeline->create();
            overlayPassKey = frame.passDescriptor;
        }
        if (overlayTexture != nullptr) {
            // Uploads keep row order; where the framebuffer's y points up the
            // view's first row is its bottom.
            const QImage rows = rhi->isYUpInFramebuffer()
                    ? overlay->flipped(Qt::Vertical) : *overlay;
            overlayUpload = rhi->nextResourceUpdateBatch();
            overlayUpload->uploadTexture(overlayTexture, rows);
        }
    }
    // Tone curve, exposure and bloom strength (bloom levels add up, so
    // their sum is averaged).
    const float image[4] = {float(toneMapping()), std::exp2(Game::exposure),
                            bloomTexture != nullptr
                                    ? Game::bloomStrength / float(std::max<size_t>(1, bloom.levels.size()))
                                    : 0.0f,
                            0.0f};
    QRhiResourceUpdateBatch *updates = overlayUpload != nullptr ? overlayUpload
                                                                : rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(presentUniforms, 0, sizeof(image), image);
    QRhiCommandBuffer *cb = frame.commandBuffer;
    cb->beginPass(frame.target, Qt::black, {1.0f, 0}, updates);
    cb->setGraphicsPipeline(presentPipeline);
    cb->setViewport(QRhiViewport(0, 0, float(frame.pixelSize.width()), float(frame.pixelSize.height())));
    cb->setShaderResources(presentBindings);
    cb->draw(3);
    if (overlayUpload != nullptr && overlayPipeline != nullptr) {
        cb->setGraphicsPipeline(overlayPipeline);
        cb->setShaderResources(overlayBindings);
        cb->draw(3);
    }
    cb->endPass();
}

void RhiRenderer::renderFrame() {
    if (traceDraws && !debugCounts.isEmpty()) {
        for (auto it = debugCounts.cbegin(); it != debugCounts.cend(); ++it)
            qInfo().noquote() << "rhi-trace" << it.key() << it.value();
        debugCounts.clear();
        qInfo().noquote() << "rhi-trace pipelines" << pipelines.size() << "resource sets"
                          << resourceSets.size();
    }
    drawPasses(PASS_SKY, PASS_UI, true);
    applyAmbientOcclusion();
    flushTarget();
    // The view goes to the frame when the surface ends it, after the
    // client painted its overlay.
    if (RhiRenderSurface *s = surface())
        s->setFrameEnd([this] { present(); });
    clearQueues();
    Renderer::renderFrame();
}

void RhiRenderer::resetFrame() {
    beginFrameIfNeeded();
    QueueRenderer::resetFrame();
    lightsPrepared = false;
    ambientOcclusionApplied = false;
    sceneProjectionValid = false;
}

void RhiRenderer::writeLightUniforms(RhiProgram *program) {
    program->setVec("localLightGrid", lightGrid.origin[0], lightGrid.origin[1], lightGrid.origin[2],
                    lightGrid.horizontalCell);
    const bool textures = lightData != nullptr && lightCells != nullptr && lightIndices != nullptr;
    program->setVec("localLightLayers", lightGrid.verticalCell,
                    textures ? float(lightGrid.lightCount()) : 0.0f);
}

void RhiRenderer::prepareLights() {
    if (lightsPrepared)
        return;
    lightsPrepared = true;
    // Programs take the grid of this frame whatever happens below.
    struct Write {
        RhiRenderer *renderer;
        ~Write() {
            for (auto &program : renderer->programs)
                if (program->valid())
                    renderer->writeLightUniforms(program.get());
        }
    } write{this};
    QElapsedTimer timer;
    timer.start();
    frameLights.clear();
    if (Game::localLightsEnabled)
        gatherLights(frameLights, Game::localLightsExposure * gluu->localLightAdaptation,
                     Game::localLightsEmissiveGain * gluu->localLightAdaptation);
    // Static scenes keep their grid: the lights and the camera's grid
    // window decide it.
    quint64 hash = 1469598103934665603ull;
    auto mix = [&hash](const void *data, size_t bytes) {
        const unsigned char *p = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < bytes; ++i)
            hash = (hash ^ p[i]) * 1099511628211ull;
    };
    for (const LightGrid::Light &light : frameLights) {
        // Field by field: the struct's padding is not initialised.
        mix(light.position, sizeof(light.position));
        mix(light.direction, sizeof(light.direction));
        mix(light.color, sizeof(light.color));
        const float shape[4] = {light.range, light.radius, light.cosInner, light.cosOuter};
        mix(shape, sizeof(shape));
        mix(&light.spot, sizeof(light.spot));
    }
    const float window[3] = {std::floor(viewPosition[0] / LightGrid::MaxExtent),
                             std::floor(viewPosition[1] / LightGrid::MaxHeight),
                             std::floor(viewPosition[2] / LightGrid::MaxExtent)};
    mix(window, sizeof(window));
    if (hash == lightsHash && lightData != nullptr && !frameLights.empty())
        return;
    lightsHash = hash;
    const qint64 gathered = timer.nsecsElapsed();
    lightGrid.build(frameLights, viewPosition);
    if (traceDraws)
        qInfo() << "rhi-trace lights" << frameLights.size() << "binned" << lightGrid.lightCount()
                << "ms" << timer.nsecsElapsed() / 1e6 << "gather ms" << gathered / 1e6
                << "indices" << lightGrid.indexTexels.size() << "eye" << viewPosition[0]
                << viewPosition[1] << viewPosition[2]
                << "first" << (frameLights.empty() ? QString() : QString("%1 %2 %3 r%4")
                       .arg(frameLights[0].position[0]).arg(frameLights[0].position[1])
                       .arg(frameLights[0].position[2]).arg(frameLights[0].range));
    if (lightGrid.empty())
        return;
    QRhiResourceUpdateBatch *batch = RhiTextures::updates();
    if (batch == nullptr)
        return;
    // Textures grow to the frame's needs and are written from the top.
    auto ensure = [this](QRhiTexture *&texture, QRhiTexture::Format format, int width, int rows) {
        if (texture != nullptr && texture->pixelSize().height() >= rows)
            return texture != nullptr;
        int capacity = 16;
        while (capacity < rows)
            capacity *= 2;
        delete texture;
        texture = rhi->newTexture(format, QSize(width, capacity));
        if (!texture->create()) {
            delete texture;
            texture = nullptr;
        }
        return texture != nullptr;
    };
    auto upload = [batch](QRhiTexture *texture, const std::vector<float> &texels, int width,
                          int components) {
        const int rows = int(texels.size() / size_t(width * components));
        QRhiTextureSubresourceUploadDescription description(
                    texels.data(), quint32(texels.size() * sizeof(float)));
        description.setSourceSize(QSize(width, rows));
        batch->uploadTexture(texture, QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, description)));
    };
    const int lights = lightGrid.lightCount();
    const int indexRows = int(lightGrid.indexTexels.size() / LightGrid::IndexWidth);
    if (!ensure(lightData, QRhiTexture::RGBA32F, 4, lights)
            || !ensure(lightCells, QRhiTexture::RGBA32F, LightGrid::CellsWidth, LightGrid::CellsY)
            || !ensure(lightIndices, QRhiTexture::R32F, LightGrid::IndexWidth, indexRows)) {
        lightGrid = LightGrid();
        return;
    }
    upload(lightData, lightGrid.lightTexels, 4, 4);
    upload(lightCells, lightGrid.cellTexels, LightGrid::CellsWidth, 4);
    upload(lightIndices, lightGrid.indexTexels, LightGrid::IndexWidth, 1);
}

void RhiRenderer::releaseLights() {
    delete lightData;
    delete lightCells;
    delete lightIndices;
    lightData = lightCells = lightIndices = nullptr;
    lightGrid = LightGrid();
    lightsHash = 0;
}
