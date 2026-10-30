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
#include <QDateTime>
#include <QDebug>
#include <QFloat16>
#include <QElapsedTimer>
#include <QSet>
#include <algorithm>
#include <cstring>
#include <rhi/qshaderbaker.h>
#include <rhi/qshaderdescription.h>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include "RhiAnisotropy.h"
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/FrameLog.h>
#include <tsre/renderer/RenderStats.h>
#include <tsre/renderer/WaterWaves.h>
#include <tsre/shape/NightTextures.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>

static QHash<QString, int> debugCounts;
static const bool traceDraws = qEnvironmentVariableIsSet("TSRE_RHI_TRACE");
// The frame copy and its depth water sees the bed through (as GLUU assigns
// them: units of PBR maps, unused by the water program).
static const int WaterSceneColorUnit = 11;
static const int WaterSceneDepthUnit = 12;
static void debugCount(const QString &key) { if (traceDraws) debugCounts[key]++; }
// Literals: no string made unless tracing (three a draw in flushTarget).
static void debugCount(const char *key) { if (traceDraws) debugCounts[QString::fromLatin1(key)]++; }

// The present pass's fragment shader (RhiImage.cpp).
extern const char *RhiPresentFragment;

namespace {
// The share of vegetation's shadow maps vegetation itself takes (task 37):
// half; TSRE_VEGETATION_SELF_SHADOW (0 to 1) tries another.
float vegetationSelfShadow() {
    static const float share = [] {
        bool ok = false;
        const float value = qEnvironmentVariable("TSRE_VEGETATION_SELF_SHADOW").toFloat(&ok);
        return ok ? std::clamp(value, 0.0f, 1.0f) : 0.5f;
    }();
    return share;
}

// The model matrix, the (0, 0, 0, 1) of inputs a mesh lacks, and the motion
// (index into last frame's matrices, task 38).
constexpr int InstanceFloats = 20;
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
    uploadBudgetNs = qint64(std::max(0, Settings::integer("core.rendering.objectLoading.uploadBudgetMs"))) * 1000000;
    anisotropy = float(std::max(1, Settings::variant("core.rendering.anisotropicFiltering", SettingType::Enum).toInt()));
    motionExposure = std::max(0, Settings::variant("core.rendering.motionBlur", SettingType::Enum).toInt());
    motionQuality = std::clamp(Settings::variant("core.rendering.motionBlurQuality", SettingType::Enum).toInt(), 1, 2);
    antiAliasing = Settings::variant("core.rendering.antiAliasing", SettingType::Enum).toInt() == 1 ? 1 : 0;
    if (context != nullptr)
        TexLib::setGpuMemory(context->deviceMemory(), context->discreteGpu());
    // Texture loaders prepare for the formats it takes (Texture::prepareForRhi),
    // with the mipmap settings.
    Texture::readMipmapSettings();
    RhiTextures::supportsBlocks();
    uniformStride = quint32(rhi->ubufAligned(1));
    baseInstance = rhi->isFeatureSupported(QRhi::BaseInstance);
    baseVertex = rhi->isFeatureSupported(QRhi::BaseVertex);
    // Instanced draws take their matrices from the instances
    // (recordInstances).
    instanceRowsCopied = false;
    targets[TARGET_VIEW].attachments = &view;
    targets[TargetSelection].attachments = &selection;
    // Programs: the main program and its variants, selection, shadows.
    struct Definition { RhiProgram::Kind kind; const char *vertex; const char *fragment; QStringList defines; };
    // With motion blur or TAA on (tasks 38, 39), records with the motion index, and
    // moving instances' programs writing the velocity (MovingMain, ...,
    // after the kinds); the static world's motion is the blur pass's, from
    // depth, its programs as without motion blur.
    instanceFloats = motionTracked() ? InstanceFloats + 4 : InstanceFloats;
    static_assert(MovingMain == RhiProgram::KIND_COUNT, "moving programs follow the kinds");
    const QStringList velocity{"TSRE_VELOCITY"};
    std::vector<Definition> definitions = {
        {RhiProgram::MAIN, "StandardFog", mainFragment, {}},
        {RhiProgram::TERRAIN, "StandardFog", mainFragment, {"TSRE_TERRAIN"}},
        {RhiProgram::UNLIT, "StandardFog", mainFragment, {"TSRE_UNLIT"}},
        {RhiProgram::PBR, "StandardFog", "StandardFog", {"TSRE_PBR"}},
        {RhiProgram::WATER, "StandardFog", "StandardFog", {"TSRE_WATER"}},
        {RhiProgram::SELECTION, "StandardFog", "Selection", {"TSRE_TERRAIN"}},
        {RhiProgram::SHADOW, "Shadows", "Shadows", {}}};
    if (motionTracked()) {
        definitions.push_back({RhiProgram::MAIN, "StandardFog", mainFragment, velocity});
        definitions.push_back({RhiProgram::UNLIT, "StandardFog", mainFragment, velocity + QStringList{"TSRE_UNLIT"}});
        definitions.push_back({RhiProgram::PBR, "StandardFog", "StandardFog", velocity + QStringList{"TSRE_PBR"}});
    }
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
    // No fog: nothing added, everything let through (Haze.glsl).
    if (rhi->isFeatureSupported(QRhi::ThreeDimensionalTextures)) {
        dummy3D = rhi->newTexture(QRhiTexture::RGBA8, 1, 1, 1, 1, QRhiTexture::ThreeDimensional);
        dummy3D->create();
    }
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
        if (dummy3D != nullptr) {
            // Black, alpha 1: no fog's light, all let through.
            batch->uploadTexture(dummy3D, QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(black)));
        }
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
    uiDrawer.reset();
    pendingUi = nullptr;
    for (auto &entry : pipelines)
        delete entry.second;
    pipelines.clear();
    for (auto &entry : resourceSets)
        delete entry.second;
    resourceSets.clear();
    for (QRhiShaderResourceBindings *set : retiredLayouts)
        delete set;
    retiredLayouts.clear();
    for (QRhiSampler *&sampler : samplers) {
        delete sampler;
        sampler = nullptr;
    }
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
    releaseGlowSplats();
    releaseGlowClear();
    releaseLightShafts();
    releaseFog();
    releaseRain();
    releaseMotionBlur();
    releaseTemporal();
    delete motionTable;
    motionTable = nullptr;
    releaseTerrainShade();
    releaseCloudResources();
    releaseSkyResources();
    releaseEnclosureOpenness();
    releaseLuminance();
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
    delete waterWaveArray;
    waterWaveArray = nullptr;
    releaseDepthProbe();
    releaseWaterDepth();
    releaseLights();
    releaseAttachments(view);
    releaseAttachments(selection);
    for (Attachments &map : shadowMaps)
        releaseAttachments(map);
    for (Attachments &map : vegetationMaps)
        releaseAttachments(map);
    releaseShadowCopy();
    for (Attachments &cache : shadowCaches)
        releaseAttachments(cache);
    delete uniformArena.buffer;
    uniformArena.buffer = nullptr;
    delete instanceArena.buffer;
    instanceArena.buffer = nullptr;
    delete dummy2D;
    delete dummyArray;
    delete dummyCube;
    delete dummyDepth;
    delete dummy3D;
    delete shadowSampler;
    dummy2D = dummyArray = dummyCube = dummyDepth = dummy3D = nullptr;
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

RhiProgram *RhiRenderer::programFor(const RenderItem *item, int pass, bool moving) {
    if (baseProgram == PROGRAM_SELECTION)
        return variant(RhiProgram::SELECTION);
    if (baseProgram == PROGRAM_SHADOW)
        return variant(RhiProgram::SHADOW);
    if (usesTerrainProgram(item))
        return variant(RhiProgram::TERRAIN);
    // Moving instances write the velocity (task 38).
    if (isUnlitPass(pass))
        return variant(moving ? MovingUnlit : RhiProgram::UNLIT);
    if (item->pbr.enabled)
        return variant(moving ? MovingPbr : RhiProgram::PBR);
    if (item->water.enabled)
        return variant(RhiProgram::WATER);
    return variant(moving ? MovingMain : RhiProgram::MAIN);
}

// The values GLUU::setMatrixUniforms gives a program, from the GLUU fields.
float RhiRenderer::cloudSkyLight() const {
    const CloudResources &c = cloudResources;
    if (!c.skyValid || !cloudsWanted())
        return 1.0f;
    // Under the clouds' cover the sky's light through them (the sun's light
    // through them is the sun's share, through their shadow). Into a broken
    // field their sunlit sides scatter the sun's light down besides: the
    // light through them again, the sun's on the ground against the sky's,
    // more where they are many among gaps, none under a closed deck (the sun
    // would count twice there).
    auto linear = [](const float *colour) {
        return 0.2126f * std::pow(std::max(colour[0], 0.0f), 2.2f) + 0.7152f * std::pow(std::max(colour[1], 0.0f), 2.2f)
                + 0.0722f * std::pow(std::max(colour[2], 0.0f), 2.2f);
    };
    const float *l = Game::sunLightDirection;
    const float length = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    const float rise = length > 0.0f ? std::max(l[1] / length, 0.0f) : 0.0f;
    const float skyLight = linear(gluu->ambientColor);
    const float sunOverSky = skyLight > 1e-5f ? linear(gluu->diffuseColor) * rise / skyLight : 0.0f;
    const float cover = c.skyCover, through = c.skyThrough;
    return (1.0f - cover) + cover * through + cover * (1.0f - cover) * through * sunOverSky;
}

void RhiRenderer::writeFrameUniforms(RhiProgram *program) {
    if (program == nullptr)
        return;
    float corrected[16];
    const QMatrix4x4 correction = targetCorrection();
    Mat4::multiply(corrected, const_cast<float *>(correction.constData()), gluu->pMatrix);
    program->setMat4("uPMatrix", corrected);
    // Last frame's (task 38): this projection with last frame's view (the
    // main view's motion); elsewhere this one, no motion.
    if (viewMotion.valid) {
        float previous[16];
        Mat4::multiply(previous, gluu->pMatrix, viewMotion.world);
        Mat4::multiply(previous, const_cast<float *>(correction.constData()), previous);
        program->setMat4("uPrevPMatrix", previous);
        float reference[16];
        Mat4::multiply(reference, gluu->pMatrix, viewMotion.reference);
        Mat4::multiply(reference, const_cast<float *>(correction.constData()), reference);
        program->setMat4("uRefPMatrix", reference);
    } else {
        program->setMat4("uPrevPMatrix", corrected);
        program->setMat4("uRefPMatrix", corrected);
    }
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
    // Sharper or softer textures (core.rendering.textureLodBias); with TAA
    // half a level sharper in the main view (task 39: its blend over the
    // jitter's phases softened them; TSRE_TAA_LODBIAS for tuning).
    static const float temporalLodBias = qEnvironmentVariableIsSet("TSRE_TAA_LODBIAS")
            ? qEnvironmentVariable("TSRE_TAA_LODBIAS").toFloat() : -0.5f;
    const bool temporalView = antiAliasing == 1 && !secondaryView && currentTarget == TARGET_VIEW;
    program->setFloat("textureLodBias", Texture::lodBias + (temporalView ? temporalLodBias : 0.0f));
    // With auto exposure (task 29) the light of the sun and the sky in the
    // scene's scale (display colours: linear light by sceneLight); unlit
    // shading and emissive colour are designed for the screen: undone by the
    // exposure.
    const float scene = sceneScaleDisplay();
    program->setVec("skyColor", gluu->fogColor[0] * scene, gluu->fogColor[1] * scene, gluu->fogColor[2] * scene,
                    gluu->fogColor[3]);
    // The air's haze with a physical sky (Haze.glsl), in place of the fog.
    float hazeValues[4];
    hazeParameters(hazeValues);
    program->setVec("hazeParams", hazeValues[0], hazeValues[1], hazeValues[2], hazeValues[3]);
    if (hazeValues[2] > 0.0f) {
        program->setVec("hazeSun", haze.sun[0], haze.sun[1]);
        float ring[8][4];
        for (int i = 0; i < 8; ++i) {
            for (int k = 0; k < 3; ++k)
                ring[i][k] = haze.ring[i][k] * scene;
            ring[i][3] = 1.0f;
        }
        program->set("hazeRing", ring, int(sizeof(ring)));
    }
    // The sky's light from above for the classic shade (StandardFog.fs
    // ambientLight); w 0: the environment cube's.
    if (hazeValues[2] > 0.0f)
        program->setVec("skyAbove", haze.above[0], haze.above[1], haze.above[2], 1.0f);
    else
        program->setVec("skyAbove", 0.0f, 0.0f, 0.0f, 0.0f);
    program->setVec("diffuseColor", gluu->diffuseColor[0] * scene, gluu->diffuseColor[1] * scene,
                    gluu->diffuseColor[2] * scene, gluu->diffuseColor[3]);
    // The sky's light through the clouds overhead (display colour).
    const float sky = scene * std::pow(cloudSkyLight(), 1.0f / 2.2f);
    program->setVec("ambientColor", gluu->ambientColor[0] * sky, gluu->ambientColor[1] * sky,
                    gluu->ambientColor[2] * sky, gluu->ambientColor[3]);
    const float exposure = autoExposure();
    program->setFloat("unlitScale", std::pow(1.0f / exposure, 1.0f / 2.2f));
    program->setFloat("emissiveScale", 1.0f / exposure);
    // The highlights' light (linear), in the scene's scale as the sun's.
    const float sceneLight = std::pow(scene, 2.2f);
    program->setVec("specularColor", gluu->specularColor[0] * sceneLight, gluu->specularColor[1] * sceneLight,
                    gluu->specularColor[2] * sceneLight, gluu->specularColor[3]);
    program->setVec("lightDirection", Game::sunLightDirection[0], Game::sunLightDirection[1],
                    Game::sunLightDirection[2]);
    writeCloudShadowUniforms(program);
    writeFogUniforms(program);
    float shadePlace[4], shadeParams[4];
    terrainShadePlace(shadePlace, shadeParams);
    program->setVec("terrainShadePlace", shadePlace[0], shadePlace[1], shadePlace[2], shadePlace[3]);
    program->setVec("terrainShadeParams", shadeParams[0], shadeParams[1], shadeParams[2], shadeParams[3]);
    // The sky enclosure (task 27): off leaves lamps and ambient as before.
    const bool enclosed = enclosure.enabled && enclosureDrawn && enclosureMap.valid();
    program->setMat4("skyEnclosureMatrix", enclosure.viewProjection);
    program->setVec("skyEnclosureParams", enclosed ? 1.0f : 0.0f, enclosure.metresPerDepth, enclosure.filter,
                    enclosure.ambientFloor);
    const bool farEnclosed = enclosed && enclosure.farEnabled && enclosureFarDrawn && enclosureFarMap.valid();
    program->setMat4("skyEnclosureFarMatrix", enclosure.farViewProjection);
    program->setVec("skyEnclosureFar", farEnclosed ? 1.0f : 0.0f, enclosure.farFilter);
    program->setVec("skyEnclosureOpen", enclosed && enclosureOpen[0].valid ? 1.0f : 0.0f,
                    farEnclosed && enclosureOpen[1].valid ? 1.0f : 0.0f);
    // With auto exposure lamps keep one scale (gathered at it), enclosed or
    // not: the exposure finds the tunnel dark.
    program->setFloat("localLightDay", enclosed && !exposureActive() ? gluu->localLightAdaptation : 1.0f);
    // Vegetation's shadow maps (task 37), and the share vegetation takes of
    // them: its own crown and its neighbours' as a light shade.
    program->setInt("vegetationShadowsOn", vegetationMaps[0].valid() && Game::shadowsEnabled > 0 ? 1 : 0);
    program->setFloat("vegetationSelfShadow", vegetationSelfShadow());
    // MSTS night textures (task 35): the scale of their light, set by the
    // view with the lamps' rule.
    const NightTextures::Frame &night = NightTextures::frame();
    program->setFloat("nightScale", night.scale);
    program->setFloat("nightEnclosedScale", night.enclosedScale);
    program->setFloat("isAlpha", gluu->alpha);
    program->setFloat("alphaTest", gluu->alphaTest);
    program->setFloat("textureEnabled", 1.0f);
    program->setFloat("enableNormals", 1.0f);
    program->setFloat("secondTexEnabled", 0.0f);
    program->setVec("terrainTextureRemap", 0.0f, 0.0f, 0.0f);
    program->setInt("shadowsEnabled", Game::shadowsEnabled);
    program->setFloat("colorBrightness", gluu->currentBrightness);
    program->setFloat("fogDensity", gluu->fogDensity);
    // Pixels per unit at distance 1 in the main view: emissive surfaces
    // fade their glow into the glow splats as they shrink (task 24).
    program->setFloat("glowFocal", sceneProjection[1] * float(std::max(1, view.size.height())) * 0.5f);
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
    const WaterWaves::Wind wind = WaterWaves::settingsWind();
    const float windDirection = float(wind.direction * M_PI / 180.0);
    program->setVec("waterWind", std::sin(windDirection), -std::cos(windDirection),
                    float(wind.speed), 0.0f);
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

namespace {
// The uniforms every draw sets (writeItemUniforms, recordDraw), found once
// per program (RhiProgram::listedMembers) instead of by name for every draw.
const char *const ItemUniformNames[] = {
    "enableNormals", "colorBrightness", "materialTransparency", "materialVegetation", "blendedTexels",
    "selectionId", "terrainTextureRemap", "secondTexEnabled", "uMSMatrix", "terrainPaged",
    "textureEnabled", "shapeColor", "nightTextureEnabled", "terrainMaterialEnabled", "materialSpecular"};
enum ItemUniform {
    UniformEnableNormals, UniformColorBrightness, UniformMaterialTransparency, UniformMaterialVegetation,
    UniformBlendedTexels, UniformSelectionId, UniformTerrainTextureRemap, UniformSecondTexEnabled,
    UniformMsMatrix, UniformTerrainPaged, UniformTextureEnabled, UniformShapeColor,
    UniformNightTextureEnabled, UniformTerrainMaterialEnabled, UniformMaterialSpecular, ItemUniformCount};
static_assert(sizeof(ItemUniformNames) / sizeof(ItemUniformNames[0]) == ItemUniformCount);

const RhiProgram::Member *const *itemUniforms(RhiProgram *program) {
    return program->listedMembers(ItemUniformNames, ItemUniformCount);
}

// A PBR material's (writeItemUniforms), set for every PBR draw: by name they
// cost a lookup each, 21 a draw.
const char *const PbrUniformNames[] = {
    "pbrBaseColor", "pbrMetallicRoughness", "pbrEmissive", "pbrGlowRadius", "pbrGlowScale", "pbrGlowEnclosed",
    "pbrNormalScale", "pbrOcclusionStrength", "pbrAlphaCutoff", "pbrBlend", "pbrUnlit", "pbrClearcoat",
    "pbrSpecular", "pbrIor", "pbrTransmission", "pbrAttenuationColor", "pbrDiffuseTransmission",
    "pbrTexCoords", "pbrUvTransforms", "pbrUvTransform", "pbrTextures"};
enum PbrUniform {
    PbrBaseColor, PbrMetallicRoughness, PbrEmissive, PbrGlowRadius, PbrGlowScale, PbrGlowEnclosed,
    PbrNormalScale, PbrOcclusionStrength, PbrAlphaCutoff, PbrBlend, PbrUnlit, PbrClearcoat,
    PbrSpecular, PbrIor, PbrTransmission, PbrAttenuationColor, PbrDiffuseTransmission,
    PbrTexCoords, PbrUvTransforms, PbrUvTransformArray, PbrTextures, PbrUniformCount};
static_assert(sizeof(PbrUniformNames) / sizeof(PbrUniformNames[0]) == PbrUniformCount);

const RhiProgram::Member *const *pbrUniforms(RhiProgram *program) {
    return program->listedMembers(PbrUniformNames, PbrUniformCount);
}
void setPbrFloat(RhiProgram *program, PbrUniform uniform, float value) {
    program->setAt(pbrUniforms(program)[uniform], &value, 4);
}
void setPbrInt(RhiProgram *program, PbrUniform uniform, qint32 value) {
    program->setAt(pbrUniforms(program)[uniform], &value, 4);
}
void setPbrVec(RhiProgram *program, PbrUniform uniform, float x, float y, float z = 0.0f, float w = 0.0f) {
    const float v[4] = {x, y, z, w};
    program->setAt(pbrUniforms(program)[uniform], v, 16);
}
void setFloatAt(RhiProgram *program, ItemUniform uniform, float value) {
    program->setAt(itemUniforms(program)[uniform], &value, 4);
}
void setIntAt(RhiProgram *program, ItemUniform uniform, qint32 value) {
    program->setAt(itemUniforms(program)[uniform], &value, 4);
}
void setVecAt(RhiProgram *program, ItemUniform uniform, float x, float y, float z = 0.0f, float w = 0.0f) {
    const float v[4] = {x, y, z, w};
    program->setAt(itemUniforms(program)[uniform], v, 16);
}

// Texture slots whose texture depends on the draw's item (recordDraw): its
// texture and its detail texture or the scene copy (0, 1), terrain
// materials, PBR maps and water layers (4-7, 11-17, 21, 33), its night
// texture and its terrain data. Every other slot depends on the frame only
// and is taken from the program's first draw in a batch: a slot filled
// from the item must be listed here.
bool itemTextureSlot(int binding) {
    switch (binding) {
    case 0: case 1: case 4: case 5: case 6: case 7: case 11: case 12: case 13: case 14:
    case 15: case 16: case 17: case 21: case 33:
        return true;
    default:
        return binding == RhiShaderSource::NightTextureBinding
                || binding == RhiShaderSource::TerrainPatchDataBinding;
    }
}
}

RhiRenderer::SlotCache *RhiRenderer::slotCacheFor(const RhiProgram *program) {
    // Draws in a row mostly share their program.
    if (lastSlotCache < slotCaches.size() && slotCaches[lastSlotCache].program == program)
        return &slotCaches[lastSlotCache];
    for (size_t i = 0; i < slotCaches.size(); ++i)
        if (slotCaches[i].program == program) {
            lastSlotCache = i;
            return &slotCaches[i];
        }
    lastSlotCache = slotCaches.size();
    slotCaches.emplace_back();
    slotCaches.back().program = program;
    return &slotCaches.back();
}

// The values applyItemState and its neighbours set in the OpenGL renderer.
void RhiRenderer::writeItemUniforms(RhiProgram *program, RenderItem *item, quint32 selectionId) {
    setFloatAt(program, UniformEnableNormals, item->material.lit ? 1.0f : 0.0f);
    setFloatAt(program, UniformColorBrightness, item->material.brightness);
    setFloatAt(program, UniformMaterialTransparency, 1.0f - item->material.opacity);
    // Vegetation is never under a roof: no sky enclosure lookups (task 37).
    setIntAt(program, UniformMaterialVegetation, item->material.vegetation ? 1 : 0);
    // MSTS specular light materials (task shapes 13).
    setFloatAt(program, UniformMaterialSpecular, item->material.specularPower);
    setIntAt(program, UniformBlendedTexels, texelsOf(item));
    program->setAt(itemUniforms(program)[UniformSelectionId], &selectionId, 4);
    const QVector3D remap = item->terrain.textureRemap;
    setVecAt(program, UniformTerrainTextureRemap, remap.x(), remap.y(), remap.z());
    const float detailScale = item->material.textured && !selectionId ? item->material.detailScale : 0.0f;
    setFloatAt(program, UniformSecondTexEnabled, detailScale);
    program->setAt(itemUniforms(program)[UniformMsMatrix], item->msMatrix != nullptr ? item->msMatrix : Identity, 64);
    setIntAt(program, UniformTerrainPaged, item->terrain.paged ? 1 : 0);
    if (item->terrain.paged) {
        program->setInt("terrainVerticesPerPatch", item->terrain.verticesPerPatch);
        program->setInt("terrainPatchSide", item->terrain.patchSide);
        program->setFloat("terrainSampleSpacing", item->terrain.sampleSpacing);
        program->setInt("terrainApplyGaps", item->terrain.applyGaps ? 1 : 0);
        program->setInt("terrainMapPass", item->terrain.mapPass ? 1 : 0);
        // The patch slot comes from the vertex index, which on Direct3D
        // leaves out the draw's base vertex (TerrainPatch.glsl).
        const bool vertexIdWithoutBase = rhi->backend() == QRhi::D3D11
                || rhi->backend() == QRhi::D3D12;
        program->setInt("terrainVertexBase", vertexIdWithoutBase ? item->mesh.baseVertex : 0);
    }
    if (program->kind == RhiProgram::PBR && item->pbr.enabled) {
        const RenderItem::Pbr &p = item->pbr;
        setPbrVec(program, PbrBaseColor, p.baseColor[0], p.baseColor[1], p.baseColor[2], p.baseColor[3]);
        setPbrVec(program, PbrMetallicRoughness, p.metallic, p.roughness);
        setPbrVec(program, PbrEmissive, p.emissive[0], p.emissive[1], p.emissive[2]);
        // The size of the emitters standing in for an emissive surface: its
        // glow fades into theirs as they shrink below 3 pixels (task 24).
        float glowRadius = 0.0f;
        int emitters = 0;
        for (const RenderItem::Light &light : item->lights)
            if (light.emissive) {
                glowRadius += light.radius;
                ++emitters;
            }
        setPbrFloat(program, PbrGlowRadius, emitters > 0 ? glowRadius / emitters : 0.0f);
        // Glow follows the lamps' light (task 21): it fades by day and goes
        // with local lights turned off; the lenses' colour stays. Signal
        // lights keep more of it by day (task 26); the sun all of it.
        float glowScale = 1.0f;
        if (p.glow != RenderItem::Pbr::GLOW_FULL) {
            const float daylight = p.glow == RenderItem::Pbr::GLOW_SIGNAL ? signalScale()
                                                                          : gluu->localLightAdaptation;
            glowScale = Game::localLightsEnabled ? daylight : 0.0f;
        }
        setPbrFloat(program, PbrGlowScale, glowScale);
        // Enclosed (task 27), glow keeps its night strength.
        setPbrFloat(program, PbrGlowEnclosed, p.glow == RenderItem::Pbr::GLOW_FULL || exposureActive() ? glowScale
                                             : (Game::localLightsEnabled ? 1.0f : 0.0f));
        setPbrFloat(program, PbrNormalScale, p.normalScale);
        setPbrFloat(program, PbrOcclusionStrength, p.occlusionStrength);
        setPbrFloat(program, PbrAlphaCutoff, p.alphaCutoff);
        setPbrInt(program, PbrBlend, p.blend ? 1 : 0);
        setPbrInt(program, PbrUnlit, p.unlit ? 1 : 0);
        setPbrVec(program, PbrClearcoat, p.clearcoat, p.clearcoatRoughness, p.clearcoatNormalScale);
        setPbrVec(program, PbrSpecular, p.specularColor[0], p.specularColor[1], p.specularColor[2],
                        p.specular);
        setPbrFloat(program, PbrIor, p.ior);
        setPbrVec(program, PbrTransmission, p.transmission, p.thickness, p.attenuationDistance,
                        sceneCopyLevels);
        setPbrVec(program, PbrAttenuationColor, p.attenuationColor[0], p.attenuationColor[1],
                        p.attenuationColor[2]);
        setPbrVec(program, PbrDiffuseTransmission, p.diffuseTransmissionColor[0], p.diffuseTransmissionColor[1],
                        p.diffuseTransmissionColor[2], p.diffuseTransmission);
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
        setPbrInt(program, PbrTexCoords, texCoords);
        setPbrInt(program, PbrUvTransforms, transformed);
        if (transformed != 0)
            program->setVec3ArrayAt(pbrUniforms(program)[PbrUvTransformArray], transforms, RenderItem::Pbr::MAP_COUNT * 2);
    }
}

QRhiTexture *RhiRenderer::packetTexture(const RenderItem *item, bool &mipmapped) {
    mipmapped = false;
    unsigned int handle = 0;
    if (item->material.textureId < 0) {
        handle = item->material.textureObject;
        // Released for memory: it loads again under the same handle.
        if (RhiTextures::evicted(handle))
            TexLib::reloadEvicted(handle);
    } else {
        const auto found = TexLib::mtex.find(item->material.textureId);
        if (found == TexLib::mtex.end() || found->second == nullptr)
            return nullptr;
        Texture *texture = found->second;
        TexLib::reloadReleased(texture);
        if (!texture->glLoaded && texture->loaded && !mayUpload()) {
            ++uploadsDeferred;
            return nullptr;
        }
        if (!texture->glLoaded && texture->loaded) {
            QElapsedTimer made;
            made.start();
            // Shape textures (legacy shapes): mipmapped by the setting.
            texture->GLTextures(Texture::shapeMipmaps.load(std::memory_order_relaxed));
            ++creation.textures;
            creation.textureNs += made.nsecsElapsed();
        }
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
    const int n = WaterWaves::Size;
    if (waterWaveArray == nullptr) {
        waterWaveArray = rhi->newTextureArray(QRhiTexture::RGBA16F, WaterWaves::Cascades,
                                              QSize(n, n), 1, QRhiTexture::MipMapped);
        if (!waterWaveArray->create()) {
            delete waterWaveArray;
            waterWaveArray = nullptr;
            return dummyArray;
        }
        waterWaveSerial = 0;
    }
    if (WaterWaves::shared().update(GLUU::animationSeconds(), WaterWaves::settingsWind(),
                                    Game::animationFrozen, waterWaveSerial, waterWaveTexels)) {
        QVector<QRhiTextureUploadEntry> entries;
        for (int level = 0; level < WaterWaves::Levels; ++level) {
            const int side = n >> level;
            const quint32 bytes = quint32(side) * side * 4 * sizeof(qfloat16);
            for (int c = 0; c < WaterWaves::Cascades; ++c) {
                const qfloat16 *texels = waterWaveTexels.data() + WaterWaves::levelOffset(level)
                        + size_t(c) * side * side * 4;
                QRhiTextureSubresourceUploadDescription description(texels, bytes);
                description.setSourceSize(QSize(side, side));
                entries.append(QRhiTextureUploadEntry(c, level, description));
            }
        }
        QRhiTextureUploadDescription upload;
        upload.setEntries(entries.cbegin(), entries.cend());
        RhiTextures::updates()->uploadTexture(waterWaveArray, upload);
    }
    return waterWaveArray;
}

QRhiSampler *RhiRenderer::sampler(bool mipmaps, bool clamp, bool nearest) {
    return wrapSampler(mipmaps, clamp ? 1 : 0, clamp ? 1 : 0, nearest);
}

QRhiSampler *RhiRenderer::wrapSampler(bool mipmaps, int addressU, int addressV, bool nearest) {
    const int key = (mipmaps ? 1 : 0) | (nearest ? 2 : 0) | (addressU << 2) | (addressV << 4);
    if (samplers[key] != nullptr)
        return samplers[key];
    auto mode = [](int address) {
        return address == 1 ? QRhiSampler::ClampToEdge
                            : address == 2 ? QRhiSampler::Mirror : QRhiSampler::Repeat;
    };
    const QRhiSampler::Filter filter = nearest ? QRhiSampler::Nearest : QRhiSampler::Linear;
    QRhiSampler *created = rhi->newSampler(filter, filter,
                                           mipmaps ? QRhiSampler::Linear : QRhiSampler::None,
                                           mode(addressU), mode(addressV));
    created->create();
    // Textures' mipmaps blur surfaces seen at a slant: anisotropic filtering
    // (core.rendering.anisotropicFiltering), which QRhi has no setting for.
    if (mipmaps && !nearest) {
        const float level = RhiAnisotropy::apply(rhi, created, anisotropy);
        if (!anisotropyLogged) {
            anisotropyLogged = true;
            qInfo().noquote() << "QRhi: anisotropic filtering"
                              << (level > 1.0f ? QStringLiteral("%1x").arg(double(level))
                                               : anisotropy > 1.0f ? QStringLiteral("not available")
                                                                   : QStringLiteral("off"));
        }
    }
    samplers[key] = created;
    return created;
}

QRhiTexture *RhiRenderer::libraryTexture(int textureId, bool &mipmapped) {
    mipmapped = false;
    const auto found = TexLib::mtex.find(textureId);
    if (found == TexLib::mtex.end() || found->second == nullptr)
        return nullptr;
    Texture *texture = found->second;
    TexLib::reloadReleased(texture);
    if (!texture->glLoaded && texture->loaded && !mayUpload()) {
        ++uploadsDeferred;
        return nullptr;
    }
    if (!texture->glLoaded && texture->loaded) {
        QElapsedTimer made;
        made.start();
        texture->GLTextures();
        ++creation.textures;
        creation.textureNs += made.nsecsElapsed();
    }
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

float RhiRenderer::copyFrameForWater() {
    // Secondary views and the selection program draw water without the
    // bed, as does the setting.
    if (!Settings::boolean("core.rendering.water.depth"))
        return 0.0f;
    const float levels = copyFrameForTransmission();
    if (levels <= 0.0f || !copyDepthForWater())
        return 0.0f;
    return levels;
}

bool RhiRenderer::copyDepthForWater() {
    // The depth texture is the view's depth attachment, so water reads a
    // copy: a pass of its own after the frame so far (flushed by the colour
    // copy) writes each texel's depth to a float texture.
    RhiRenderSurface *s = surface();
    QRhiCommandBuffer *cb = s->frame().commandBuffer;
    WaterDepth &copy = waterDepth;
    if (copy.copy != nullptr && copy.copy->pixelSize() != view.size)
        releaseWaterDepth();
    if (copy.copy == nullptr) {
        static const char *fragment = R"(#version 440
layout(location = 0) out vec4 result;
layout(binding = 0) uniform sampler2D depthTexture;
void main() {
    result = vec4(texelFetch(depthTexture, ivec2(gl_FragCoord.xy), 0).r);
}
)";
        copy.copy = rhi->newTexture(QRhiTexture::R32F, view.size, 1, QRhiTexture::RenderTarget);
        copy.sampler = rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                       QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        if (!copy.copy->create() || !copy.sampler->create()) {
            releaseWaterDepth();
            return false;
        }
        copy.target = rhi->newTextureRenderTarget({QRhiColorAttachment(copy.copy)});
        copy.pass = copy.target->newCompatibleRenderPassDescriptor();
        copy.target->setRenderPassDescriptor(copy.pass);
        copy.bindings = rhi->newShaderResourceBindings();
        copy.bindings->setBindings({QRhiShaderResourceBinding::sampledTexture(
                0, QRhiShaderResourceBinding::FragmentStage, view.depth, copy.sampler)});
        copy.boundDepth = view.depth;
        copy.pipeline = rhi->newGraphicsPipeline();
        copy.pipeline->setShaderStages(
                {{QRhiShaderStage::Vertex, bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi)},
                 {QRhiShaderStage::Fragment, bakeInline(fragment, QShader::FragmentStage, rhi)}});
        copy.pipeline->setShaderResourceBindings(copy.bindings);
        copy.pipeline->setRenderPassDescriptor(copy.pass);
        if (!copy.target->create() || !copy.bindings->create() || !copy.pipeline->create()) {
            releaseWaterDepth();
            return false;
        }
    }
    if (copy.boundDepth != view.depth) {
        // The view was recreated.
        copy.bindings->setBindings({QRhiShaderResourceBinding::sampledTexture(
                0, QRhiShaderResourceBinding::FragmentStage, view.depth, copy.sampler)});
        copy.bindings->create();
        copy.boundDepth = view.depth;
    }
    cb->beginPass(copy.target, Qt::black, {1.0f, 0});
    cb->setGraphicsPipeline(copy.pipeline);
    cb->setViewport(QRhiViewport(0, 0, view.size.width(), view.size.height()));
    cb->setShaderResources(copy.bindings);
    cb->draw(3);
    cb->endPass();
    return true;
}

void RhiRenderer::releaseWaterDepth() {
    WaterDepth &copy = waterDepth;
    delete copy.pipeline;
    delete copy.bindings;
    delete copy.target;
    delete copy.pass;
    delete copy.sampler;
    delete copy.copy;
    copy = WaterDepth();
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
    ++uniformArena.frame;
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
    // Library textures over their memory budget go (task textures 04), then
    // every released texture with the resource sets made with it: no draw
    // of this frame holds one yet.
    TexLib::residencyFrame();
    forgetRetiredTextures();
}

bool RhiRenderer::textureComing(const RenderItem *item) const {
    if (item->material.textureId < 0)
        return RhiTextures::evicted(item->material.textureObject);
    const auto found = TexLib::mtex.find(item->material.textureId);
    if (found == TexLib::mtex.end() || found->second == nullptr)
        return false;
    const Texture *texture = found->second;
    return !texture->missing && !texture->error;
}

void RhiRenderer::forgetRetiredTextures() {
    const std::vector<QRhiTexture *> retired = RhiTextures::takeRetired();
    if (retired.empty())
        return;
    const QSet<QRhiTexture *> gone(retired.cbegin(), retired.cend());
    for (auto it = resourceSets.begin(); it != resourceSets.end();) {
        const BindingKey &key = it->first;
        const bool uses = std::any_of(key.textures.cbegin(), key.textures.cend(),
                                      [&gone](QRhiTexture *texture) { return gone.contains(texture); });
        if (!uses) {
            ++it;
            continue;
        }
        // A program's layout set stays alive: its pipelines are made with it
        // (only its layout counts there, never its textures).
        const bool layout = std::any_of(programs.cbegin(), programs.cend(),
                                        [&it](const std::unique_ptr<RhiProgram> &program) {
                                            return program && program->layout == it->second;
                                        });
        if (layout)
            retiredLayouts.push_back(it->second);
        else if (it->second != nullptr)
            it->second->deleteLater();
        it = resourceSets.erase(it);
    }
    for (QRhiTexture *texture : retired)
        texture->deleteLater();
}

bool RhiRenderer::createAttachments(Attachments &attachments, QRhiTexture::Format colorFormat,
                                    const QSize &size, QRhiTexture::Flags colorFlags,
                                    QRhiTexture::Format secondFormat,
                                    QRhiTexture::Format thirdFormat,
                                    QRhiTexture::Format fourthFormat) {
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
        // With the velocity, motion blur copies the glow (task 38).
        attachments.color3 = rhi->newTexture(thirdFormat, size, 1,
                                             fourthFormat != QRhiTexture::UnknownFormat
                                                     ? QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource
                                                     : QRhiTexture::Flags(QRhiTexture::RenderTarget));
        if (!attachments.color3->create()) {
            releaseAttachments(attachments);
            return false;
        }
    }
    if (fourthFormat != QRhiTexture::UnknownFormat) {
        attachments.color4 = rhi->newTexture(fourthFormat, size, 1, QRhiTexture::RenderTarget);
        if (!attachments.color4->create()) {
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
            if (attachments.color4 != nullptr)
                description.setColorAttachments({color, QRhiColorAttachment(attachments.color2),
                                                 QRhiColorAttachment(attachments.color3),
                                                 QRhiColorAttachment(attachments.color4)});
            else if (attachments.color3 != nullptr)
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
    delete attachments.skyTarget;
    delete attachments.skyPass;
    attachments.skyTarget = nullptr;
    attachments.skyPass = nullptr;
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
        delete attachments.color4;
    }
    attachments.color2 = attachments.color3 = attachments.color4 = nullptr;
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
    // The velocity (task 38) after the glow and the ambient share: with it
    // both exist, so the outputs keep their numbers.
    const bool velocity = tracksMotion();
    const bool glow = bloomEnabled() || velocity;
    const bool ambient = ambientOcclusionQuality() > 0 || glow;
    if (view.valid() && view.size == size && view.color->format() == format
            && (view.color2 != nullptr) == ambient && (view.color3 != nullptr) == glow
            && (view.color4 != nullptr) == velocity)
        return true;
    delete presentBindings;
    presentBindings = nullptr;
    releaseAmbientOcclusion();
    releaseBloom();
    releaseGlowClear();
    // Everything else drawing into or reading the view's textures: the new
    // ones can take the old ones' addresses, so a pointer comparison would
    // keep targets and resource sets of deleted textures (changing tone
    // mapping, which keeps the size, crashed; user, 2026-10-12).
    releaseLightShafts();
    releaseFog();
    releaseRain();
    // The clouds' maps (weather, cover, shadow, the sky's light) read none of
    // them and stay: released with them, a resized window's frame lost its
    // cloud shadows (user, 2026-10-14).
    releaseMainCloudView();
    // Auto exposure keeps its last measurement: a resized view is the same
    // scene (dropped, it eased towards no correction until the next one).
    const bool measured = luminance.measured;
    const float measuredLog2 = luminance.measuredLog2;
    releaseLuminance();
    luminance.measured = measured;
    luminance.measuredLog2 = measuredLog2;
    waterDepth.boundDepth = nullptr;
    depthProbe.boundDepth = nullptr;
    releaseMotionBlur();
    return createAttachments(view, format, size, QRhiTexture::UsedAsTransferSource,
                             ambient ? QRhiTexture::RGBA8 : QRhiTexture::UnknownFormat,
                             glow ? QRhiTexture::RGBA16F : QRhiTexture::UnknownFormat,
                             velocity ? velocityFormat() : QRhiTexture::UnknownFormat);
}

void RhiRenderer::releaseGlowClear() {
    delete glowClear;
    delete glowClearPass;
    glowClear = nullptr;
    glowClearPass = nullptr;
    delete velocityClear;
    delete velocityClearPass;
    velocityClear = nullptr;
    velocityClearPass = nullptr;
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
    selectionFlush = true;
    flushTarget();
    selectionFlush = false;
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
        debugCount("call finish (selection)");
        rhi->finish();
        selectionIds = result.data;
        if (traceDraws) {
            int nonZero = 0;
            for (qsizetype i = 0; i + 4 <= selectionIds.size(); i += 4)
                nonZero += selectionIds.at(i) || selectionIds.at(i + 1) || selectionIds.at(i + 2)
                        || selectionIds.at(i + 3);
            qInfo().noquote() << "rhi-trace selection read bytes" << selectionIds.size() << "of"
                              << qsizetype(selection.size.width()) * selection.size.height() * 4
                              << "non-zero ids" << nonZero;
        }
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

void RhiRenderer::createShadowMaps(int nearSize, int middleSize, int farSize) {
    beginFrameIfNeeded();
    const int sizes[3] = {nearSize, middleSize, farSize};
    for (int map = 0; map < 3; ++map) {
        const QSize size(std::max(1, sizes[map]), std::max(1, sizes[map]));
        if (!shadowMaps[map].valid() || shadowMaps[map].size != size)
            createAttachments(shadowMaps[map], QRhiTexture::UnknownFormat, size, {});
        shadowMaps[map].openGlRows = true;
        targets[TARGET_SHADOW_NEAR + map].attachments = &shadowMaps[map];
        // Vegetation's (task 37), as the map.
        if (!vegetationMaps[map].valid() || vegetationMaps[map].size != size)
            createAttachments(vegetationMaps[map], QRhiTexture::UnknownFormat, size, {});
        vegetationMaps[map].openGlRows = true;
        targets[TARGET_VEGETATION_NEAR + map].attachments = &vegetationMaps[map];
    }
}

bool RhiRenderer::beginShadowCache(int map) {
    beginFrameIfNeeded();
    if (map < 0 || map > 5)
        return false;
    const Attachments &source = map < 3 ? shadowMaps[map] : vegetationMaps[map - 3];
    if (!source.valid())
        return false;
    flushTarget();
    Attachments &cache = shadowCaches[map];
    const QSize size = source.size;
    if ((!cache.valid() || cache.size != size)
            && !createAttachments(cache, QRhiTexture::UnknownFormat, size, {}))
        return false;
    // Rows as in the map, which takes it texel for texel.
    cache.openGlRows = true;
    targets[TargetShadowCache + map].attachments = &cache;
    currentTarget = TargetShadowCache + map;
    TargetState &state = target();
    state.clearColor = false;
    state.clearDepth = true;
    setViewport(0, 0, size.width(), size.height());
    return true;
}

bool RhiRenderer::copyShadowCache(int map) {
    if (map < 0 || map > 5 || !shadowCaches[map].valid())
        return false;
    // Over the map's clear: a failed copy leaves it empty, never stale.
    target().copyCache = map;
    return true;
}

bool RhiRenderer::drawShadowCopy(QRhiCommandBuffer *cb, int map, const Attachments &into) {
    const Attachments &cache = shadowCaches[map];
    if (!cache.valid() || cache.depth == nullptr || cache.size != into.size)
        return false;
    ShadowCopy &copy = shadowCopy;
    if (copy.sampler == nullptr) {
        copy.sampler = rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                       QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        if (!copy.sampler->create()) {
            releaseShadowCopy();
            return false;
        }
    }
    if (copy.bindings[map] == nullptr || copy.bound[map] != cache.depth) {
        if (copy.bindings[map] == nullptr)
            copy.bindings[map] = rhi->newShaderResourceBindings();
        copy.bindings[map]->setBindings({QRhiShaderResourceBinding::sampledTexture(
                0, QRhiShaderResourceBinding::FragmentStage, cache.depth, copy.sampler)});
        if (!copy.bindings[map]->create())
            return false;
        copy.bound[map] = cache.depth;
    }
    if (copy.pipeline == nullptr) {
        // Every texel's depth as the cache has it; the maps' passes are
        // alike, so one pipeline serves the three.
        static const char *fragment = R"(#version 440
layout(binding = 0) uniform sampler2D cache;
void main() {
    gl_FragDepth = texelFetch(cache, ivec2(gl_FragCoord.xy), 0).r;
}
)";
        copy.pipeline = rhi->newGraphicsPipeline();
        copy.pipeline->setShaderStages(
                {{QRhiShaderStage::Vertex, bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi)},
                 {QRhiShaderStage::Fragment, bakeInline(fragment, QShader::FragmentStage, rhi)}});
        copy.pipeline->setDepthTest(true);
        copy.pipeline->setDepthWrite(true);
        copy.pipeline->setDepthOp(QRhiGraphicsPipeline::Always);
        copy.pipeline->setShaderResourceBindings(copy.bindings[map]);
        copy.pipeline->setRenderPassDescriptor(into.pipelinePass());
        if (!copy.pipeline->create()) {
            delete copy.pipeline;
            copy.pipeline = nullptr;
            return false;
        }
    }
    cb->setGraphicsPipeline(copy.pipeline);
    cb->setViewport(QRhiViewport(0, 0, float(into.size.width()), float(into.size.height())));
    cb->setShaderResources(copy.bindings[map]);
    cb->draw(3);
    return true;
}

void RhiRenderer::releaseShadowCopy() {
    ShadowCopy &copy = shadowCopy;
    delete copy.pipeline;
    for (QRhiShaderResourceBindings *bindings : copy.bindings)
        delete bindings;
    delete copy.sampler;
    copy = ShadowCopy();
}

bool RhiRenderer::enclosureTarget(int which) const {
    return which == TargetEnclosure || which == TargetEnclosureFar || which == TargetEnclosureBottom
            || which == TargetEnclosureFarBottom;
}

bool RhiRenderer::beginSkyEnclosure(int size, int map) {
    beginFrameIfNeeded();
    flushTarget();
    if (map < 0 || map > 3)
        return false;
    Attachments *maps[4] = {&enclosureMap, &enclosureFarMap, &enclosureBottomMap, &enclosureFarBottomMap};
    const int which[4] = {TargetEnclosure, TargetEnclosureFar, TargetEnclosureBottom, TargetEnclosureFarBottom};
    Attachments &attachments = *maps[map];
    const QSize pixels(std::max(1, size), std::max(1, size));
    if ((!attachments.valid() || attachments.size != pixels)
            && !createAttachments(attachments, QRhiTexture::UnknownFormat, pixels, {}))
        return false;
    // Rows in OpenGL's order, as the shadow maps: the lit shaders look it up
    // the same way.
    attachments.openGlRows = true;
    targets[which[map]].attachments = &attachments;
    currentTarget = which[map];
    TargetState &state = target();
    state.clearColor = false;
    state.clearDepth = true;
    setViewport(0, 0, pixels.width(), pixels.height());
    if (map == 0)
        enclosureDrawn = true;
    else if (map == 1)
        enclosureFarDrawn = true;
    // A map drawn anew: its openness waits for its view from below.
    if (map < 2)
        enclosureOpen[map].valid = false;
    return true;
}

void RhiRenderer::renderEnclosureCasters(float range, int statsSlot, const float *viewProjection, float minRadius) {
    beginFrameIfNeeded();
    if (!enclosureTarget(currentTarget) || !targetReady())
        return;
    // Both views, from above and below, take alpha-tested and blended
    // surfaces (thin roofs; track beds, tunnel tubes).
    planShadowCasters(range, viewProjection, true, minRadius);
    recordCasters(statsSlot);
}

namespace {

// How open the sky is around a point of an enclosure map, from 8 rays out
// to the radius over the maps: under cover a ray goes on; at the first
// column without cover it finds an opening if that column's surface lies
// low in the space under the cover here (the cutting at a tunnel's portal,
// the ground beside a viaduct or a bridge), and is blocked if it rises
// nearly to the cover (the rock beside a tunnel). An opening adds the more
// light the nearer it is. A column has cover where its highest surface (the
// map from above) lies more than 3 m over its floor: its lowest surface (the
// view from below, its rows the other way round), or for points on and over
// the ground no deeper than just under the ground (the terrain's heights).
// Both are kept: the openness from the floor on the ground for points over
// it, from the lowest for points under it (a tunnel, under a hill or a house
// on it).
const char *OpennessFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Openness {
    vec4 params; // radius (map coordinates), metres per depth unit, 1 with the ground
};
layout(binding = 1) uniform sampler2D topMap;
layout(binding = 2) uniform sampler2D bottomMap;
// The terrain's heights under the map, metres over its middle height; below
// -10000 where none is loaded.
layout(binding = 3) uniform sampler2D groundMap;

const int Steps = 12;
// An opening's surface lies within 5 m of the floor, or in the lower 60 %
// of the space between the floor and the cover over it. (The floor is the
// lowest surface: by a river, the river bed under a bridge lies well below
// the ground its arches stand on; measured from it alone, the fields beside
// the bridge rose like walls, user 2026-10-12.)
const float OpeningHeight = 5.0;
const float OpeningShare = 0.6;
// A hollow deeper than this under its cover is measured from this deep: the
// lowest surface the view from below finds can lie far under the floor one
// stands on (ground under a hollow embankment or station shape, objects sunk
// under the terrain). From 14 m under a platform roof at Prudnik the tracks
// beside it rose like walls: no opening found, the platform deep and black
// with the sun kept out, the rest of the roof open (the user, 2026-10-28).
// Light comes in under cover from the sides; deeper floors do not move them.
// (The floor on the ground keeps buried objects out now; this stays for the
// lowest floor's openness.)
const float DeepestHollow = 10.0;
// The floor on the ground lies no deeper than this under it (track beds and
// ballast sunk a little into the terrain).
const float GroundMargin = 1.0;
// params.x, the rays' reach, is this many metres (RouteEditorGLWidget's
// OpenRadius).
const float RadiusMetres = 30.0;

bool opening(float surfaceOverFloor, float coverOverFloor) {
    return surfaceOverFloor < max(OpeningHeight, OpeningShare * coverOverFloor);
}

float topAt(vec2 p) { return textureLod(topMap, p, 0.0).r; }
float bottomAt(vec2 p) { return textureLod(bottomMap, vec2(p.x, 1.0 - p.y), 0.0).r; }

// The ground's height under a column as the view from below has it (both
// views start the same distance from the map's middle height: a top's is 1
// less its depth from above), or -1 without the ground's heights.
float groundAt(vec2 p) {
    if (params.z <= 0.0)
        return -1.0;
    float height = textureLod(groundMap, p, 0.0).r;
    return height < -10000.0 ? -1.0 : 0.5 + height / params.y;
}

// A column's floor: its lowest surface (the view from below); on the ground
// (ground true) no deeper than GroundMargin under it: for what stands on
// and over the ground, what lies under the terrain is buried. An embankment
// shape sunk under a Prudnik platform was the floor of the open platform
// beside a roof: a hollow there, dark (no opening found from that deep),
// and the roof's edge over it darkened the sunlit platform under its eave
// in a block of the embankment's outline (the user, 2026-10-28).
float floorAt(vec2 p, bool ground) {
    float lowest = bottomAt(p);
    float g = ground ? groundAt(p) : -1.0;
    return g < 0.0 ? lowest : max(lowest, g - GroundMargin / params.y);
}

// A column without cover: open sky (1), unless it is solid ground rising
// nearly to the cover of a hollow beside it (the rock beside a tunnel, where
// points moved off the walls land): 0. (All of them 0 darkened the ground
// along a viaduct's footprint: filtered against the hollow's value under
// the deck, user 2026-10-12.)
float uncovered(vec2 p, float top, float step, bool ground) {
    for (int d = 0; d < 8; ++d) {
        float angle = float(d) * 0.785398;
        for (int s = 1; s <= 2; ++s) {
            vec2 q = p + vec2(cos(angle), sin(angle)) * step * float(s);
            float floorThere = floorAt(q, ground);
            float coverThere = (1.0 - topAt(q) - floorThere) * params.y;
            if (coverThere > 3.0 && !opening((1.0 - top - floorThere) * params.y, coverThere))
                return 0.0;
        }
    }
    return 1.0;
}

// How open the sky is over this column, from its floor (floorAt, its depth
// in the view from below: floorHere).
float openness(float topHere, bool ground, out float floorHere) {
    float floorDepth = floorAt(uv, ground);
    floorHere = floorDepth;
    // Points in a hollow (cover over a floor) look for openings; in columns
    // without cover the floor is the top itself.
    float coverHere = (1.0 - topHere - floorDepth) * params.y;
    if (coverHere > DeepestHollow) {
        floorDepth = 1.0 - topHere - DeepestHollow / params.y;
        coverHere = DeepestHollow;
    }
    float hollow = smoothstep(2.0, 4.0, coverHere);
    // Steps of about 1.5 m (the rays' are 2.5 m).
    float open = hollow < 1.0 ? uncovered(uv, topHere, params.x / float(Steps) * 0.6, ground) : 1.0;
    if (hollow <= 0.0)
        return open;
    float light = 0.0;
    for (int d = 0; d < 8; ++d) {
        float angle = float(d) * 0.785398 + 0.2;
        vec2 direction = vec2(cos(angle), sin(angle)) * (params.x / float(Steps));
        for (int s = 1; s <= Steps; ++s) {
            vec2 q = uv + direction * float(s);
            float top = topAt(q);
            // The views from above and below start the same distance from
            // the map's middle height: the top there over the floor here.
            // A column whose top lies low is open above it, whatever lies
            // under it: tracks over an underpass are a hollow too (their
            // highest surface over the underpass floor), and a ray walked on
            // over them as if still under the roof (2026-10-28).
            if (opening((1.0 - top - floorDepth) * params.y, coverHere)) {
                // Falling off as an opening's view does with distance.
                float near = 1.0 - float(s - 1) / float(Steps);
                light += near * near;
                break;
            }
            // High and over a hollow: the cover goes on.
            if ((1.0 - top - floorAt(q, ground)) * params.y > 3.0)
                continue;
            // High and solid: a wall.
            break;
        }
    }
    // Two open sides near by (a viaduct's arches) light it fully.
    return mix(open, clamp(light * 0.5, 0.0, 1.0), hollow);
}

void main() {
    float topHere = topAt(uv);
    // How far in under cover (g, read by the lit shaders): the distance to
    // the nearest column whose top lies more than 2 m under this column's
    // (the cover's edge), out to 4.5 m (1), from 16 directions in 0.5 m
    // steps. From the tops alone, for every column: the view from below plays
    // no part, so what lies under the ground (an embankment shape sunk under
    // the terrain at Prudnik) does not count; the lit shaders take it only
    // where cover lies over the point itself. The shade's colour and light
    // change from it, gradually from a roof's edge in (the user,
    // 2026-10-28). (A share of the points around stayed near 1 at an edge: a
    // hard, stepped change there under the eave of a roof.)
    // Ground at the foot of a higher cover (open ground beside a roof or a
    // wall: a top more than 2 m higher within 2.5 m, a map texel and more)
    // is not under it: 0. With no lower edge around it, it read 1, and the
    // texture's filter carried that into the roof's edge beside it, in the
    // map's blocks (a sunlit strip under an eave at Prudnik).
    float around;
    {
        float step = params.x / RadiusMetres * 0.5;
        float nearest = 4.5;
        bool foot = false;
        for (int d = 0; d < 16 && !foot; ++d) {
            vec2 direction = vec2(cos(float(d) * 0.392699), sin(float(d) * 0.392699)) * step;
            for (int s = 1; s <= 9; ++s) {
                float metres = float(s) * 0.5;
                float rise = (topHere - topAt(uv + direction * float(s))) * params.y;
                if (metres <= 2.5 && rise > 2.0) {
                    foot = true;
                    break;
                }
                if (metres - 0.25 < nearest && rise < -2.0)
                    nearest = metres - 0.25;
                if (metres > 2.5 && metres - 0.25 >= nearest)
                    break;
            }
        }
        around = foot ? 0.0 : nearest / 4.5;
    }
    // The openness from the floor on the ground (r), for points over it, and
    // from the lowest floor (a), for points under it: a tunnel under a hill
    // or a house on it, an underpass under a platform. b: the floor on the
    // ground's height over the map's middle height (m), for the lit shaders
    // to tell which (ShadowSampling.glsl, pointOpenness): smooth as the
    // ground, also across a roof's edge.
    float floorOnGround, unused;
    float onGround = openness(topHere, true, floorOnGround);
    float lowest = params.z > 0.0 ? openness(topHere, false, unused) : onGround;
    result = vec4(onGround, around, (floorOnGround - 0.5) * params.y, lowest);
}
)";


}

void RhiRenderer::finishSkyEnclosure(bool far, float radius, float metresPerDepth, const float *ground,
                                      int groundSize) {
    beginFrameIfNeeded();
    // The view from below.
    flushTarget();
    RhiRenderSurface *s = surface();
    if (s == nullptr || s->frame().commandBuffer == nullptr)
        return;
    Attachments &top = far ? enclosureFarMap : enclosureMap;
    Attachments &bottom = far ? enclosureFarBottomMap : enclosureBottomMap;
    if (!top.valid() || !bottom.valid())
        return;
    EnclosureOpenness &o = enclosureOpen[far ? 1 : 0];
    constexpr int Size = 256;
    if (o.texture == nullptr) {
        // Half floats: b holds a height in metres.
        o.texture = rhi->newTexture(QRhiTexture::RGBA16F, QSize(Size, Size), 1, QRhiTexture::RenderTarget);
        o.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
        if (!o.texture->create() || !o.uniforms->create()) {
            releaseEnclosureOpenness();
            return;
        }
        o.target = rhi->newTextureRenderTarget({QRhiColorAttachment(o.texture)});
        o.pass = o.target->newCompatibleRenderPassDescriptor();
        o.target->setRenderPassDescriptor(o.pass);
        if (!o.target->create()) {
            releaseEnclosureOpenness();
            return;
        }
    }
    // The ground's heights: half floats (a decimetre or two near the map's
    // middle height); a texel bound without them.
    const bool hasGround = ground != nullptr && groundSize > 0;
    const QSize groundPixels = hasGround ? QSize(groundSize, groundSize) : QSize(1, 1);
    if (o.ground == nullptr || (hasGround && o.ground->pixelSize() != groundPixels)) {
        // The bindings go with it (a new texture may take the old one's place).
        delete o.bindings;
        o.bindings = nullptr;
        delete o.ground;
        const auto format = rhi->isTextureFormatSupported(QRhiTexture::R16F) ? QRhiTexture::R16F
                                                                               : QRhiTexture::R32F;
        o.ground = rhi->newTexture(format, groundPixels);
        if (!o.ground->create()) {
            releaseEnclosureOpenness();
            return;
        }
    }
    if (o.bindings == nullptr || o.boundTop != top.depth || o.boundBottom != bottom.depth
            || o.boundGround != o.ground) {
        delete o.bindings;
        const auto fragment = QRhiShaderResourceBinding::FragmentStage;
        o.bindings = rhi->newShaderResourceBindings();
        o.bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, o.uniforms),
                                 QRhiShaderResourceBinding::sampledTexture(1, fragment, top.depth,
                                                                           sampler(false, true, true)),
                                 QRhiShaderResourceBinding::sampledTexture(2, fragment, bottom.depth,
                                                                           sampler(false, true, true)),
                                 QRhiShaderResourceBinding::sampledTexture(3, fragment, o.ground,
                                                                           sampler(false, true))});
        o.bindings->create();
        o.boundTop = top.depth;
        o.boundBottom = bottom.depth;
        o.boundGround = o.ground;
    }
    if (opennessPipeline == nullptr) {
        static QShader vertex, fragment;
        if (!vertex.isValid()) {
            vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
            fragment = bakeInline(OpennessFragment, QShader::FragmentStage, rhi);
        }
        opennessPipeline = rhi->newGraphicsPipeline();
        opennessPipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
        opennessPipeline->setShaderResourceBindings(o.bindings);
        opennessPipeline->setRenderPassDescriptor(o.pass);
        if (!opennessPipeline->create()) {
            delete opennessPipeline;
            opennessPipeline = nullptr;
            return;
        }
    }
    const float params[4] = {radius, metresPerDepth, hasGround ? 1.0f : 0.0f, 0.0f};
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    if (hasGround) {
        const size_t count = size_t(groundSize) * groundSize;
        QByteArray bytes;
        if (o.ground->format() == QRhiTexture::R16F) {
            bytes.resize(int(count * sizeof(qfloat16)));
            qfloat16 *out = reinterpret_cast<qfloat16 *>(bytes.data());
            for (size_t i = 0; i < count; ++i)
                out[i] = qfloat16(std::clamp(ground[i], EnclosureNoGround, 60000.0f));
        } else {
            bytes = QByteArray(reinterpret_cast<const char *>(ground), int(count * sizeof(float)));
        }
        batch->uploadTexture(o.ground, QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(bytes)));
    }
    batch->updateDynamicBuffer(o.uniforms, 0, sizeof(params), params);
    QRhiCommandBuffer *cb = s->frame().commandBuffer;
    cb->beginPass(o.target, Qt::black, {1.0f, 0}, batch);
    cb->setGraphicsPipeline(opennessPipeline);
    cb->setViewport(QRhiViewport(0, 0, float(Size), float(Size)));
    cb->setShaderResources(o.bindings);
    cb->draw(3);
    cb->endPass();
    o.valid = true;
}

void RhiRenderer::releaseEnclosureOpenness() {
    for (EnclosureOpenness &o : enclosureOpen) {
        delete o.bindings;
        delete o.target;
        delete o.pass;
        delete o.uniforms;
        delete o.texture;
        delete o.ground;
        o = EnclosureOpenness();
    }
    delete opennessPipeline;
    opennessPipeline = nullptr;
}

void RhiRenderer::setSkyEnclosure(const SkyEnclosure &next) {
    enclosure = next;
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
    if (clearColor != nullptr) {
        // The sky's colour, in the scene's scale with auto exposure.
        const float scene = sceneScaleDisplay();
        nextClearColor = QColor::fromRgbF(std::min(clearColor[0] * scene, 1.0f), std::min(clearColor[1] * scene, 1.0f),
                                          std::min(clearColor[2] * scene, 1.0f), 1.0f);
    }
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
    debugCount("call finish (readNow)");
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

void RhiRenderer::setSceneProjection(const float *projection, float zNear, float zFar) {
    beginFrameIfNeeded();
    if (secondaryView || currentTarget != TARGET_VIEW)
        return;
    sceneProjection[0] = projection[0];
    sceneProjection[1] = projection[5];
    sceneProjection[2] = zNear;
    sceneProjection[3] = zFar;
    sceneDepthRange[0] = depthRange[0];
    sceneDepthRange[1] = depthRange[1];
    sceneProjectionValid = true;
}

void RhiRenderer::beginViewBand(const LayeredView &view, ViewBand band) {
    if (!view.projection)
        return;
    prepareLights();
    gatherGlowSplats();
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
        setSkyInverse(projection, viewMatrix);
        Mat4::multiply(gluu->pMatrix, projection, viewMatrix);
        applyFrameUniforms();
        setFogLod(0.0f);
        setCullView(nullptr);
        depthRange[0] = 0.99f;
        depthRange[1] = 1.0f;
        return;
    }
    if (band == BAND_SCENE) {
        // The clouds of every view read it.
        float scene[16];
        view.projection(0.2f, view.sceneFar, scene);
        bandScale[currentTarget][0] = scene[0];
        bandScale[currentTarget][1] = scene[5];
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
    if (band == BAND_DISTANT && !secondaryView && currentTarget == TARGET_VIEW)
        distantFar = view.distantFar;
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
    // TAA (task 39): the main view drawn a fraction of a pixel off, a
    // clip-space shift (the sky band smooth, mirrors and other targets not).
    if (antiAliasing == 1 && viewMotionTaken && !secondaryView && currentTarget == TARGET_VIEW
            && view.mirrorPlane == nullptr && !plainImage && this->view.valid()) {
        // The editor's overlays are drawn after the resolve, without it.
        if (band == BAND_SCENE) {
            Mat4::multiply(overlayProjection, projection, viewMatrix);
            overlayProjectionValid = true;
        }
        float shift[16];
        Mat4::identity(shift);
        shift[12] = 2.0f * temporalJitter[0] / float(std::max(1, this->view.size.width()));
        shift[13] = 2.0f * temporalJitter[1] / float(std::max(1, this->view.size.height()));
        Mat4::multiply(projection, shift, projection);
    }
    Mat4::multiply(gluu->pMatrix, projection, viewMatrix);
    applyFrameUniforms();
    setCullView(gluu->pMatrix);
    if (band == BAND_SCENE)
        setViewLimits(view.limits);
}

void RhiRenderer::endView(const LayeredView &view) {
    if (view.mirrorPlane == nullptr)
        prepareGlowSplats();
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

quint32 RhiRenderer::appendUniforms(RhiProgram *program) {
    std::vector<char> &data = uniformArena.data;
    const quint32 size = quint32(program->block.size());
    // Unchanged since the program's last block this frame: that one (the
    // arena only grows within a frame), without comparing 1.4 KB a draw.
    if (!program->blockChanged && program->appendedFrame == uniformArena.frame) {
        uniformArena.lastOffset = program->appendedOffset;
        uniformArena.lastSize = size;
        return program->appendedOffset;
    }
    // A draw with the values of the one before shares its block, so the two
    // keep one resource binding.
    quint32 offset = uniformArena.lastOffset;
    if (data.empty() || uniformArena.lastSize != size
            || std::memcmp(data.data() + uniformArena.lastOffset, program->block.data(), size) != 0) {
        offset = quint32((data.size() + uniformStride - 1) / uniformStride * uniformStride);
        data.resize(offset + size);
        std::memcpy(data.data() + offset, program->block.data(), size);
        uniformArena.lastOffset = offset;
        uniformArena.lastSize = size;
    }
    program->blockChanged = false;
    program->appendedFrame = uniformArena.frame;
    program->appendedOffset = offset;
    return offset;
}

quint32 RhiRenderer::appendInstances(const float *const *matrices, int count, const quint32 *motions) {
    std::vector<char> &data = instanceArena.data;
    const quint32 offset = quint32(data.size());
    data.resize(offset + quint32(count) * instanceStride());
    float *out = reinterpret_cast<float *>(data.data() + offset);
    static const float defaults[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const quint32 capacity = quint32(MotionTableRows * RhiShaderSource::MotionMatricesPerRow);
    const bool motion = instanceFloats > InstanceFloats;
    for (int i = 0; i < count; ++i) {
        float *record = out + i * instanceFloats;
        std::memcpy(record, matrices[i], 16 * sizeof(float));
        std::memcpy(record + 16, defaults, sizeof(defaults));
        if (motion) {
            const quint32 index = motions != nullptr && motions[i] <= capacity ? motions[i] : 0;
            record[20] = float(index);
            record[21] = record[22] = record[23] = 0.0f;
        }
    }
    return offset;
}

QRhiBuffer *RhiRenderer::uploadArena(Arena &arena, QRhiBuffer::UsageFlags usage,
                                     QRhiResourceUpdateBatch *batch) {
    const quint32 size = quint32(arena.data.size());
    // QRhi's Direct3D 11 backend copies all of a Dynamic buffer after each
    // update, and the arenas are updated once per target, about ten times a
    // frame: there the instances go to a Static buffer (updated by range)
    // and the uniforms (Dynamic only) to a buffer sized to the data.
    const bool copiesWhole = rhi->backend() == QRhi::D3D11;
    const bool staticBuffer = copiesWhole && !usage.testFlag(QRhiBuffer::UniformBuffer);
    if (arena.buffer == nullptr || arena.buffer->size() < size) {
        // Grows between frames; within a frame the buffer stays.
        quint32 capacity = copiesWhole ? std::max(65536u, size + size / 2)
                                       : std::max(ArenaChunk, size * 2);
        if (arena.buffer != nullptr)
            arena.buffer->deleteLater();
        arena.buffer = rhi->newBuffer(staticBuffer ? QRhiBuffer::Static : QRhiBuffer::Dynamic, usage,
                                      capacity);
        arena.buffer->create();
        arena.uploaded = 0;
    }
    if (size > arena.uploaded) {
        if (staticBuffer)
            batch->uploadStaticBuffer(arena.buffer, arena.uploaded, size - arena.uploaded,
                                      arena.data.data() + arena.uploaded);
        else
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
    QElapsedTimer made;
    made.start();
    struct Count {
        Creation &c; QElapsedTimer &t;
        ~Count() { ++c.pipelines; c.pipelineNs += t.nsecsElapsed(); }
    } count{creation, made};
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
                             {quint32(instanceStride()), QRhiVertexInputBinding::PerInstance}});
    QVarLengthArray<QRhiVertexInputAttribute, 16> attributes;
    for (const QShaderDescription::InOutVariable &input : program->inputs) {
        const int location = input.location;
        int size = 0, offset = 0;
        if (location >= 8 && location <= 11) {
            attributes.append({1, location, QRhiVertexInputAttribute::Float4,
                               quint32((location - 8) * 16)});
        } else if (location == 12) {
            attributes.append({1, 12, QRhiVertexInputAttribute::Float4, 80});
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
        QVarLengthArray<QRhiGraphicsPipeline::TargetBlend, 4> blends(key.colorCount, blend);
        // The velocity (task 38): the scene's shaders' draws that write
        // the depth, the pixel's owner; translucent ones and decals leave
        // what lies behind them its motion. Unblended: MSTS draws blend all.
        if (key.colorCount > 3) {
            QRhiGraphicsPipeline::TargetBlend velocity;
            velocity.enable = false;
            velocity.colorWrite = program->writesVelocity && key.depthWrite && !key.decal
                    ? QRhiGraphicsPipeline::ColorMask(QRhiGraphicsPipeline::R | QRhiGraphicsPipeline::G
                                                      | QRhiGraphicsPipeline::B | QRhiGraphicsPipeline::A)
                    : QRhiGraphicsPipeline::ColorMask();
            blends[3] = velocity;
        }
        ps->setTargetBlends(blends.cbegin(), blends.cend());
    }
    if (key.wireframe && rhi->isFeatureSupported(QRhi::NonFillPolygonMode))
        ps->setPolygonMode(QRhiGraphicsPipeline::Line);
    if (key.lineWidth > 1 && rhi->isFeatureSupported(QRhi::WideLines))
        ps->setLineWidth(key.lineWidth);
    ps->setShaderResourceBindings(program->layout);
    ps->setRenderPassDescriptor(key.pass);
    if (!ps->create()) {
        qWarning() << "QRhi renderer: pipeline creation failed, program kind" << program->kind
                   << "(" << program->samplers.size() << "samplers)";
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
    QElapsedTimer made;
    made.start();
    struct Count {
        Creation &c; QElapsedTimer &t;
        ~Count() { ++c.bindings; c.bindingNs += t.nsecsElapsed(); }
    } count{creation, made};
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
                             quint32 selectionId, int pass, int category, const quint32 *motions) {
    if (count <= 0 || !hasMesh(item)) {
        debugCount("skip-nomesh");
        return;
    }
    bool moving = false;
    for (int i = 0; motions != nullptr && motionTracked() && i < count && !moving; ++i)
        moving = motions[i] > 0;
    RhiProgram *program = programFor(item, pass, moving);
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
    // World items' first uploads go within the frame's budget.
    uploadBudgeted = category == RenderStats::CategoryWorld
            && item->material.surface != RenderItem::SURFACE_TERRAIN;
    Meshes::RhiBuffers buffers;
    QElapsedTimer prepared;
    prepared.start();
    const bool mayMake = mayUpload();
    bool uploaded = false;
    const bool meshReady = Meshes::prepareRhi(item->mesh.handle, rhi, frameBatch, buffers, mayMake, &uploaded);
    // Uploads only (the budget, the stats): looking up a mesh already there
    // is the draw's own cost.
    if (uploaded)
        creation.meshNs += prepared.nsecsElapsed();
    if (!meshReady
            || buffers.format == MeshData::Buffer) {
        if (!meshReady && !mayMake)
            ++uploadsDeferred;
        if (!meshReady)
            ++waitingDraws;
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
        // Drawn once its texture is there: not magenta while it loads or
        // waits for the upload budget (only a failed one is).
        if (base == nullptr && textureComing(item)) {
            ++waitingDraws;
            debugCount("skip-texture");
            return;
        }
        if (base != nullptr) {
            setFloatAt(program, UniformTextureEnabled, 1.0f);
        } else {
            setFloatAt(program, UniformTextureEnabled, 0.0f);
            setVecAt(program, UniformShapeColor, 1.0f, 0.0f, 1.0f, 1.0f);
        }
    } else if (item->material.textured) {
        setFloatAt(program, UniformTextureEnabled, 0.0f);
        setVecAt(program, UniformShapeColor, 1.0f, 0.0f, 1.0f, 1.0f);
    } else {
        setFloatAt(program, UniformTextureEnabled, 0.0f);
        setVecAt(program, UniformShapeColor, item->material.color[0], item->material.color[1],
                 item->material.color[2], item->material.color[3]);
    }
    const unsigned int baseHandle = item->material.textureId < 0 ? item->material.textureObject : 0;
    // The night texture (task 35), with the day texture drawn.
    QRhiTexture *night = nullptr;
    bool nightMipmapped = false;
    if (base != nullptr && item->material.nightTextureId >= 0 && NightTextures::frame().wanted)
        night = libraryTexture(item->material.nightTextureId, nightMipmapped);
    setFloatAt(program, UniformNightTextureEnabled, night != nullptr ? 1.0f : 0.0f);
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
    setIntAt(program, UniformTerrainMaterialEnabled, materials ? 1 : 0);
    if (materials) {
        // The base texture slot only selects the shader's textured path.
        setFloatAt(program, UniformTextureEnabled, 1.0f);
        const QVector3D mapRemap = terrain.materialMapRemap;
        program->setVec("terrainMaterialMapRemap", mapRemap.x(), mapRemap.y(), mapRemap.z());
        program->setInt("terrainMaterialMapSide", terrain.materialMapSide);
        program->setFloat("terrainMaterialNoiseScale", terrain.materialNoiseScale);
    }
    // PBR maps on the units the OpenGL renderer uses, with the glTF wrap
    // modes; the frame copy on unit 1 while the transmission pass draws.
    static const int PbrMapUnits[RenderItem::Pbr::MAP_COUNT] = {0, 11, 12, 13, 14, 4, 5, 6, 7,
                                                               15, 16, 17, 21, 33};
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
        // MSTS PBR materials (task shapes 13): the clearcoat mask in the
        // metallic-roughness map's alpha, and glass.
        if (p.clearcoatInMetallicRoughnessAlpha && (present & 1) != 0)
            present |= 1 << 13;
        if (p.glass)
            present |= 1 << 14;
        setPbrInt(program, PbrTextures, present);
    }
    // What each texture unit holds for this program, as the OpenGL renderer
    // binds them: the packet texture on 0, the detail texture on 1, the
    // water layers on 4 and 5 and the wave map on 15.
    int waterLayers = 0;
    // The slots that depend only on the frame are found for a program's
    // first draw in a batch and taken from it for the others; the item's
    // own slots are found for every draw.
    SlotCache *slotCache = slotBatch ? slotCacheFor(program) : nullptr;
    const bool slotsCached = slotCache != nullptr && slotCache->filled;
    // A slot's texture and sampler.
    const auto resolveSlot = [&](const RhiProgram::Sampler &slot, QRhiTexture *&keyTexture,
                                 QRhiSampler *&keySampler) {
        QRhiTexture *texture = nullptr;
        QRhiSampler *slotSampler = sampler(true, false);
        if (slot.binding == RhiShaderSource::MotionMatricesBinding) {
            keyTexture = motionTable != nullptr ? motionTable : dummy2D;
            keySampler = sampler(false, true, true);
            return;
        }
        if (slot.binding == RhiShaderSource::TerrainPatchDataBinding) {
            texture = item->terrain.paged
                    ? Meshes::dataTextureRhi(item->terrain.paramsBuffer, rhi, frameBatch) : nullptr;
            if (texture == nullptr)
                texture = dummy2D;
            keyTexture = texture;
            keySampler = sampler(false, true, true);
            return;
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
            keyTexture = texture;
            keySampler = slotSampler;
            return;
        }
        switch (slot.type) {
        case 1:
            texture = dummyArray;
            if (program->kind == RhiProgram::WATER && slot.binding == 15) {
                texture = waterWaves();
                slotSampler = sampler(true, false);
            }
            break;
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
            // Vegetation's (task 37).
            const int vegetation = slot.binding - RhiShaderSource::VegetationShadowBinding;
            if (vegetation >= 0 && vegetation < 3 && vegetationMaps[vegetation].valid() && Game::shadowsEnabled > 0)
                texture = vegetationMaps[vegetation].depth;
            break;
        }
        case 4:
            // The fog volume (task 33): the main view's, once filled.
            texture = dummy3D;
            slotSampler = sampler(false, true);
            if (slot.binding == RhiShaderSource::FogVolumeBinding && !secondaryView
                    && currentTarget == TARGET_VIEW && fogWanted() && createFog(fogQuality(), view.size)
                    && fogResources.filled) {
                texture = fogResources.integrated;
                slotSampler = fogResources.linear;
            }
            break;
        default:
            texture = dummy2D;
            // The PBR programs have no night texture; their occlusion map
            // (pbrOcclusionMap) has the unit. Taken for the night texture,
            // every PBR material sampled white there: no occlusion at all.
            if (slot.binding == RhiShaderSource::NightTextureBinding && program->kind != RhiProgram::PBR) {
                if (night != nullptr) {
                    texture = night;
                    slotSampler = sampler(nightMipmapped, false);
                }
                break;
            }
            if (slot.binding == RhiShaderSource::SkyEnclosureBinding) {
                if (enclosureMap.valid() && enclosureDrawn) {
                    texture = enclosureMap.depth;
                    slotSampler = sampler(false, true, true);
                }
                break;
            }
            if (slot.binding == RhiShaderSource::SkyEnclosureFarBinding) {
                if (enclosureFarMap.valid() && enclosureFarDrawn) {
                    texture = enclosureFarMap.depth;
                    slotSampler = sampler(false, true, true);
                }
                break;
            }
            if (slot.binding == RhiShaderSource::SkyEnclosureOpenBinding
                    || slot.binding == RhiShaderSource::SkyEnclosureFarOpenBinding) {
                const EnclosureOpenness &o
                        = enclosureOpen[slot.binding == RhiShaderSource::SkyEnclosureFarOpenBinding ? 1 : 0];
                if (o.valid && o.texture != nullptr) {
                    texture = o.texture;
                    slotSampler = sampler(false, true);
                }
                break;
            }
            if (slot.binding == RhiShaderSource::TerrainShadeBinding) {
                if (terrainShadeReady()) {
                    texture = shadeResources.texture;
                    slotSampler = sampler(false, true);
                }
                break;
            }
            if (slot.binding == RhiShaderSource::FogFloorBinding) {
                if (fogResources.filled && fogResources.floor != nullptr && fogWanted()) {
                    texture = fogResources.floor;
                    slotSampler = sampler(false, true);
                }
                break;
            }
            if (slot.binding == RhiShaderSource::CloudShadowBinding) {
                if (cloudResources.shadow != nullptr && cloudResources.shadowValid) {
                    texture = cloudResources.shadow;
                    slotSampler = sampler(false, true);
                }
                break;
            }
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
            } else if (program->kind == RhiProgram::WATER && slot.binding == WaterSceneColorUnit
                       && waterSceneLevels > 0.0f && sceneCopy != nullptr) {
                texture = sceneCopy;
                slotSampler = sampler(true, true);
            } else if (program->kind == RhiProgram::WATER && slot.binding == WaterSceneDepthUnit
                       && waterSceneLevels > 0.0f && waterDepth.copy != nullptr) {
                texture = waterDepth.copy;
                slotSampler = sampler(false, true, true);
            }
            break;
        }
        keyTexture = texture;
        keySampler = slotSampler;
    };
    const qsizetype slotCount = qsizetype(program->samplers.size());
    if (slotsCached) {
        // The frame's slots as the batch's first draw of the program found
        // them, the item's own again (a few of the 22 a draw).
        bindingKey.textures = slotCache->textures;
        bindingKey.samplers = slotCache->samplers;
        for (const int slotIndex : slotCache->itemSlots)
            resolveSlot(program->samplers[size_t(slotIndex)], bindingKey.textures[slotIndex],
                        bindingKey.samplers[slotIndex]);
    } else {
        bindingKey.textures.resize(slotCount);
        bindingKey.samplers.resize(slotCount);
        for (qsizetype slotIndex = 0; slotIndex < slotCount; ++slotIndex)
            resolveSlot(program->samplers[size_t(slotIndex)], bindingKey.textures[slotIndex],
                        bindingKey.samplers[slotIndex]);
        if (slotCache != nullptr) {
            slotCache->textures = bindingKey.textures;
            slotCache->samplers = bindingKey.samplers;
            slotCache->itemSlots.clear();
            for (qsizetype slotIndex = 0; slotIndex < slotCount; ++slotIndex)
                if (itemTextureSlot(program->samplers[size_t(slotIndex)].binding))
                    slotCache->itemSlots.push_back(int(slotIndex));
            slotCache->filled = true;
        }
    }
    if (program->kind == RhiProgram::WATER) {
        program->setInt("waterLayers", waterLayers);
        // The bed under the water: the frame copy, its size and mipmap
        // levels, and the scene band (its slice of the depth range and its
        // near and far planes).
        const bool bed = waterSceneLevels > 0.0f;
        program->setVec("waterScene", bed ? 1.0f / view.size.width() : 0.0f,
                        bed ? 1.0f / view.size.height() : 0.0f, waterSceneLevels, bed ? 1.0f : 0.0f);
        program->setVec("waterDepthRange", sceneDepthRange[0], sceneDepthRange[1],
                        sceneProjection[2], sceneProjection[3]);
    }

    DrawCommand draw;
    draw.uniformOffset = appendUniforms(program);
    draw.instanceOffset = appendInstances(matrices, count, motions);
    draw.instances = quint32(count);
    draw.vertexBuffer = buffers.vertexBuffer;
    draw.vertexOffset = buffers.vertexOffset;
    if (item->mesh.indexed) {
        if (buffers.indexBuffer == nullptr)
            return;
        draw.indexBuffer = buffers.indexBuffer;
        draw.indexOffset = buffers.indexOffset + item->mesh.indexOffset;
        draw.indexFormat = item->mesh.indexType == RenderItem::INDEX_U32
                ? QRhiCommandBuffer::IndexUInt32 : QRhiCommandBuffer::IndexUInt16;
        draw.baseVertex = item->mesh.baseVertex;
    } else {
        draw.first = item->mesh.first;
    }
    // A mesh in a shared buffer at a whole number of vertices is drawn from
    // the buffer's start, its place in the draw (base vertex, first vertex):
    // the draws of one buffer keep one vertex input (6,300 inputs for 12,000
    // draws at Prudnik's freight station). Terrain reads the vertex index
    // (TerrainPatch.glsl) and keeps its own.
    const quint32 stride = quint32(std::max(1, int(buffers.layout))) * quint32(sizeof(float));
    if (buffers.format == MeshData::FloatLayout && program->kind != RhiProgram::TERRAIN
            && draw.vertexOffset != 0 && draw.vertexOffset % stride == 0
            && (!item->mesh.indexed || baseVertex)) {
        const quint32 vertices = draw.vertexOffset / stride;
        if (item->mesh.indexed)
            draw.baseVertex += qint32(vertices);
        else
            draw.first += vertices;
        draw.vertexOffset = 0;
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
    // Blended PBR (glass) writes no depth in the blended pass, sorted back to
    // front. Drawn in submission order in the opaque pass (track profiles'
    // layers, OglObj) it writes it as legacy parts there do, its holes
    // discarded (alphaCutoff): else the ambient occlusion took the depth
    // behind it, the terrain under a ballast shoulder, and darkened the
    // shoulder by it (black at dusk; the user, 2026-10-27).
    const bool glassWithoutDepth = item->pbr.enabled && item->pbr.blend && pass != PASS_OPAQUE;
    key.depthWrite = !glassWithoutDepth && texelsOf(item) != TEXELS_TRANSLUCENT;
    key.decal = item->material.decal && selectionId == 0;
    // The sky enclosure maps look down on everything: single-sided roofs and
    // tunnel tubes, made to be seen from below, count too. (The sun's shadow
    // maps keep culling: unculled they cost 5-10 ms at the CMK station; the
    // lit shaders take the sun out under such covers from the enclosure.)
    key.cullBack = !item->material.doubleSided && !enclosureTarget(currentTarget);
    // Targets flipped against the backend's convention turn the winding
    // around.
    key.frontCw = frontCw != (openGlRowTarget() && !rhi->isYUpInFramebuffer());
    key.wireframe = item->material.wireframe;
    key.lineWidth = quint8(std::clamp(item->material.lineWidth > 0 ? item->material.lineWidth
                                                                  : Game::oglDefaultLineWidth, 1, 255));
    key.pass = target().attachments != nullptr ? target().attachments->pipelinePass() : nullptr;
    key.colorCount = target().attachments == nullptr || target().attachments->color == nullptr ? 0
            : target().attachments->color4 != nullptr ? 4
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
    // A run of draws with one key keeps one copy of it (2,000 keys for
    // 12,000 draws at Prudnik's freight station, 670 bytes each).
    if (state.bindings.empty() || !(state.bindings.back() == bindingKey))
        state.bindings.push_back(std::move(bindingKey));
    draw.binding = quint32(state.bindings.size() - 1);
    state.draws.push_back(draw);
    RenderStats::countDraw(static_cast<RenderStats::Category>(category), 0, item->mesh.count * count);
    RenderStats::countPassDraw(pass);
}

void RhiRenderer::recordInstances(const std::vector<DrawInstance> &instances, const std::vector<quint32> *order,
                                  int pass) {
    SlotBatch batch(*this);
    std::vector<const float *> matrices;
    std::vector<quint32> motions;
    if (order == nullptr) {
        for (const DrawInstance &instance : instances) {
            if (!hasMesh(instance.packet) || !visible(instance, cullFrustum))
                continue;
            const float *matrix = instanceMatrix(instance.matrix);
            recordDraw(instance.packet, &matrix, 1, instance.selectionId, pass, instance.category, &instance.motion);
        }
        return;
    }
    planGroups(instances, *order);
    for (const GroupPlan &plan : groupPlans) {
        const DrawInstance &first = *plannedInstances[plan.begin];
        RenderItem *item = first.packet;
        if (RenderStats::inFrame())
            RenderStats::current().groupedPackets++;
        if (plan.base >= 0) {
            matrices.clear();
            motions.clear();
            const bool motion = instanceFloats > InstanceFloats;
            for (size_t k = plan.begin; k < plan.end; ++k) {
                matrices.push_back(instanceMatrix(plannedInstances[k]->matrix));
                if (motion)
                    motions.push_back(plannedInstances[k]->motion);
            }
            recordDraw(item, matrices.data(), int(matrices.size()), first.selectionId,
                       pass, first.category, motions.empty() ? nullptr : motions.data());
            continue;
        }
        for (size_t k = plan.begin; k < plan.end; ++k) {
            const DrawInstance &instance = *plannedInstances[k];
            const float *matrix = instanceMatrix(instance.matrix);
            recordDraw(item, &matrix, 1, instance.selectionId, pass, instance.category, &instance.motion);
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
        // The falling drops over the scene, before the UI; then its motion
        // blurred (task 38), before any UI, also one drawn on its own (the
        // compass).
        if (pass == PASS_UI) {
            // TAA (task 39) before them: the drops have no history.
            applyTemporal();
            if (first < PASS_UI)
                applyRain();
            applyMotionBlur();
        }
        // Blended parts in two, as Open Rails draws them: their opaque
        // texels after the alpha-tested ones, depth written; the rest back
        // to front in the blended pass without, so a window never hides the
        // cab behind it.
        // With TAA the editor's overlays wait for the resolve (kept, drawn
        // without the jitter: thin lines would be averaged fainter).
        if (pass == PASS_OVERLAY && overlaysAfterTemporal())
            continue;
        const bool twoParts = blending && baseProgram != PROGRAM_SELECTION && blendedInTwo();
        const bool opaqueTexels = opaqueTexelsIn(pass, twoParts);
        if (queue.ordered.empty() && queue.grouped.empty() && !opaqueTexels)
            continue;
        // Occlusion of the opaque scene, before blended and later passes.
        if (pass > PASS_ALPHA_TEST) {
            applyAmbientOcclusion();
            applyClouds();
            applySkyFog();
        }
        if (pass == PASS_TRANSMISSION)
            sceneCopyLevels = copyFrameForTransmission();
        // Water sees the bed: the frame and its depth so far.
        if (pass == PASS_WATER)
            waterSceneLevels = copyFrameForWater();
        recordInstances(queue.ordered, nullptr, pass);
        if (pass == PASS_BLENDED && opaqueTexels) {
            blendedTexels = TEXELS_OPAQUE;
            recordInstances(twoPartBlended, &twoPartOrder, pass);
        }
        if (pass == PASS_BLENDED && twoParts)
            blendedTexels = TEXELS_TRANSLUCENT;
        recordInstances(queue.grouped, &groupedOrder(pass), pass);
        if (pass == PASS_ALPHA_TEST && opaqueTexels) {
            blendedTexels = TEXELS_OPAQUE;
            recordInstances(twoPartBlended, &twoPartOrder, pass);
        }
        blendedTexels = TEXELS_ALL;
        sceneCopyLevels = 0.0f;
        waterSceneLevels = 0.0f;
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

void RhiRenderer::renderShadowCasters(float range, int statsSlot, const float *viewProjection,
                                      ShadowCasters casters, ShadowSet set) {
    beginFrameIfNeeded();
    if (!targetReady())
        return;
    // Casters in range and inside the light view, instanced by packet.
    planShadowCasters(range, viewProjection, false, 0.0f, casters, set);
    waitingDraws += quint64(plannedShapesWaiting);
    recordCasters(statsSlot);
}

void RhiRenderer::recordCasters(int statsSlot) {
    SlotBatch batch(*this);
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
    const bool splatsPending = glowSplats.pending && currentTarget == TARGET_VIEW;
    if (state.draws.empty() && !state.clearColor && !state.clearDepth && !splatsPending
            && state.copyCache < 0)
        return;
    if (!targetReady())
        return;
    QRhiCommandBuffer *cb = s->frame().commandBuffer;
    if (QRhiResourceUpdateBatch *textures = RhiTextures::takeUpdates())
        cb->resourceUpdate(textures);
    if (frameBatch == nullptr)
        frameBatch = rhi->nextResourceUpdateBatch();
    // The terrain's shade, before any pass reads it.
    prepareTerrainShade(frameBatch);
    QRhiBuffer *uniforms = uploadArena(uniformArena, QRhiBuffer::UniformBuffer, frameBatch);
    QRhiBuffer *instances = uploadArena(instanceArena, QRhiBuffer::VertexBuffer, frameBatch);
    // Resolve resource sets and pipelines now that the buffers are known: a
    // set for each run of draws with one key; a draw with the pipeline key of
    // the one before takes its pipeline.
    resolvedSets.resize(state.bindings.size());
    for (size_t k = 0; k < state.bindings.size(); ++k) {
        state.bindings[k].uniforms = uniforms;
        resolvedSets[k] = bindings(state.bindings[k]);
    }
    for (size_t i = 0; i < state.draws.size(); ++i) {
        DrawCommand &draw = state.draws[i];
        draw.bindings = resolvedSets[draw.binding];
        if (draw.bindings == nullptr)
            draw.pipeline = nullptr;
        else if (i > 0 && state.draws[i - 1].bindings != nullptr && state.keys[i] == state.keys[i - 1])
            draw.pipeline = state.draws[i - 1].pipeline;
        else
            draw.pipeline = pipeline(state.keys[i]);
        draw.instanceBuffer = instances;
    }
    const bool splats = splatsPending && state.attachments->color3 != nullptr
            && uploadGlowSplats(frameBatch, state.attachments->pipelinePass());
    // Clears happen at the start of the pass; otherwise the contents stay.
    int clears = (state.clearColor ? 1 : 0) | (state.clearDepth ? 2 : 0);
    // A physical sky takes the colour clear's place, before the geometry.
    const bool skyFirst = (clears & 1) && skyFor(currentTarget) && state.attachments->color != nullptr;
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
        // The velocity at no motion (task 38).
        if (state.attachments->color4 != nullptr) {
            if (velocityClear == nullptr) {
                velocityClear = rhi->newTextureRenderTarget({QRhiColorAttachment(state.attachments->color4)});
                velocityClearPass = velocityClear->newCompatibleRenderPassDescriptor();
                velocityClear->setRenderPassDescriptor(velocityClearPass);
                velocityClear->create();
            }
            // x far beyond any motion: drawn by no moving instance, the
            // static world's (from depth in the blur pass).
            cb->beginPass(velocityClear, QColor::fromRgbF(64.0f, 0.0f, 0.0f, 0.0f), {1.0f, 0});
            cb->endPass();
        }
        clears = 0;
    }
    // The clouds' maps (cover, shadow) before the main view's geometry,
    // which samples the shadow.
    if (state.clearColor && currentTarget == TARGET_VIEW && !secondaryView && !selectionFlush) {
        // Last frame's matrices of moving instances, which the scene's
        // draws read (task 38).
        if (frameBatch == nullptr)
            frameBatch = rhi->nextResourceUpdateBatch();
        uploadMotionTable(frameBatch);
        prepareClouds(cb, frameBatch);
        // The fog volume after the cloud shadow map, which it reads.
        prepareFog(cb, frameBatch);
    }
    if (skyFirst) {
        drawSky(cb, *state.attachments, frameBatch);
        clears &= ~1;
    }
    cb->beginPass(state.attachments->targets[clears], state.color, {1.0f, 0}, frameBatch);
    frameBatch = nullptr;
    // A shadow map starting as its cached static casters (task 37), with the
    // moving ones drawn on top.
    if (state.copyCache >= 0 && !drawShadowCopy(cb, state.copyCache, *state.attachments))
        qWarning() << "QRhi: could not copy the cached shadow map" << state.copyCache;
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
        quint32 vertexOffset = 0;
        quint32 instanceOffset = 0;
        QRhiBuffer *index = nullptr;
        quint32 indexOffset = 0;
        QRhiCommandBuffer::IndexFormat format = QRhiCommandBuffer::IndexUInt16;
        bool operator==(const VertexState &o) const {
            return vertex == o.vertex && vertexOffset == o.vertexOffset
                    && instanceOffset == o.instanceOffset && index == o.index
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
        vertex.vertexOffset = draw.vertexOffset;
        quint32 firstInstance = 0;
        if (baseInstance)
            firstInstance = draw.instanceOffset / instanceStride();
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
                {draw.vertexBuffer, draw.vertexOffset}, {draw.instanceBuffer, vertex.instanceOffset}};
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
    // After the scene, against its depth.
    if (splats)
        drawGlowSplats(cb);
    cb->endPass();
    state.draws.clear();
    state.keys.clear();
    state.bindings.clear();
    state.clearColor = state.clearDepth = false;
    state.copyCache = -1;
}

void RhiRenderer::present() {
    RhiRenderSurface *s = surface();
    if (s == nullptr || s->frame().commandBuffer == nullptr || !view.valid())
        return;
    const RhiRenderSurface::Frame &frame = s->frame();
    const double gpuSeconds = frame.commandBuffer->lastCompletedGpuTime();
    if (gpuSeconds > 0.0)
        gpuMs = float(gpuSeconds * 1000.0);
    // Bloom from the view's glow, before the frame's pass begins.
    QRhiTexture *bloomTexture = plainImage ? nullptr : renderBloom(frame.commandBuffer);
    // The view's brightness for auto exposure (task 29), then the light
    // shafts over it: like bloom, light the eye sees, not the scene the
    // exposure meters (measured with them, the exposure took back what
    // they added and gave back what they darkened; user, 2026-10-14).
    if (exposureActive())
        measureLuminance(frame.commandBuffer);
    if (!plainImage)
        applyLightShafts();
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
    QRect overlayDrawn;
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
            overlayPipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);
            overlayPipeline->setShaderResourceBindings(overlayBindings);
            overlayPipeline->setRenderPassDescriptor(frame.passDescriptor);
            overlayPipeline->create();
            overlayPassKey = frame.passDescriptor;
        }
        // The texture keeps the overlay between frames: only the part painted
        // or cleared since the last upload goes up again.
        const QRect changed = s->overlayChanged() & overlay->rect();
        if (overlayTexture != nullptr && !changed.isEmpty()) {
            // Uploads keep row order; where the framebuffer's y points up the
            // view's first row is its bottom. A copy of the part, not a source
            // rectangle: QRhi's Vulkan backend sizes the staging buffer for
            // the whole image either way.
            QImage part = overlay->copy(changed);
            QPoint to = changed.topLeft();
            if (rhi->isYUpInFramebuffer()) {
                part = part.flipped(Qt::Vertical);
                to.setY(overlay->height() - changed.y() - changed.height());
            }
            QRhiTextureSubresourceUploadDescription rows(part);
            rows.setDestinationTopLeft(to);
            overlayUpload = rhi->nextResourceUpdateBatch();
            overlayUpload->uploadTexture(overlayTexture,
                                         QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, rows)));
        }
        overlayDrawn = s->overlayPainted() & overlay->rect();
    }
    // Tone curve, exposure, bloom strength (bloom levels add up, so their
    // sum is averaged) and warmth; none for a plain image.
    const float image[4] = {plainImage ? 0.0f : float(toneMapping()),
                            plainImage ? 1.0f : std::exp2(Game::exposure) * autoExposure(),
                            bloomTexture != nullptr
                                    ? Game::bloomStrength / float(std::max<size_t>(1, bloom.levels.size()))
                                    : 0.0f,
                            plainImage ? 0.0f : std::clamp(warmth, -1.0f, 1.0f)};
    QRhiResourceUpdateBatch *updates = overlayUpload != nullptr ? overlayUpload
                                                                : rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(presentUniforms, 0, sizeof(image), image);
    // The 2D user interface's new geometry, textures and uniforms.
    if (pendingUi != nullptr) {
        if (!uiDrawer)
            uiDrawer = std::make_unique<RhiUiDrawer>(rhi);
        uiDrawer->prepare(*pendingUi, updates, frame.passDescriptor, frame.pixelSize,
                          [this](int textureId, bool &mipmapped) { return libraryTexture(textureId, mipmapped); });
        pendingUi = nullptr;
    }
    QRhiCommandBuffer *cb = frame.commandBuffer;
    cb->beginPass(frame.target, Qt::black, {1.0f, 0}, updates);
    cb->setGraphicsPipeline(presentPipeline);
    cb->setViewport(QRhiViewport(0, 0, float(frame.pixelSize.width()), float(frame.pixelSize.height())));
    cb->setShaderResources(presentBindings);
    cb->draw(3);
    if (uiDrawer)
        uiDrawer->record(cb);
    if (!overlayDrawn.isEmpty() && overlayTexture != nullptr && overlayPipeline != nullptr) {
        cb->setGraphicsPipeline(overlayPipeline);
        cb->setShaderResources(overlayBindings);
        // Scissor origin is the bottom left.
        cb->setScissor(QRhiScissor(overlayDrawn.x(),
                                   frame.pixelSize.height() - overlayDrawn.y() - overlayDrawn.height(),
                                   overlayDrawn.width(), overlayDrawn.height()));
        cb->draw(3);
    }
    cb->endPass();
}

void RhiRenderer::drawUi(UiDrawList &list) {
    pendingUi = &list;
}

void RhiRenderer::renderFrame() {
    if (traceDraws && !debugCounts.isEmpty()) {
        for (auto it = debugCounts.cbegin(); it != debugCounts.cend(); ++it)
            qInfo().noquote() << "rhi-trace" << it.key() << it.value();
        debugCounts.clear();
        qInfo().noquote() << "rhi-trace pipelines" << pipelines.size() << "resource sets"
                          << resourceSets.size();
        // The driver memory QRhi allocated (Vulkan: its allocator's blocks).
        const QRhiStats stats = rhi->statistics();
        qInfo().noquote() << "rhi-trace memory at" << QDateTime::currentMSecsSinceEpoch()
                          << "blocks" << stats.blockCount << "allocations"
                          << stats.allocCount << "used MB" << stats.usedBytes / 1048576
                          << "unused MB" << stats.unusedBytes / 1048576 << "total MB"
                          << stats.totalUsageBytes / 1048576 << "meshes"
                          << Meshes::rhiTraceSummary() << "textures"
                          << RhiTextures::memorySummary();
    }
    drawPasses(PASS_SKY, PASS_UI, true);
    applyAmbientOcclusion();
    applyClouds();
    applySkyFog();
    applyTemporal();
    applyRain();
    applyMotionBlur();
    flushTarget();
    // The view goes to the frame when the surface ends it, after the
    // client painted its overlay.
    if (RhiRenderSurface *s = surface())
        s->setFrameEnd([this] { present(); });
    clearQueues();
    if (RenderStats::inFrame()) {
        RenderStats::FrameStats &frame = RenderStats::current();
        frame.texturesCreated = quint64(creation.textures);
        frame.textureCreateMs = creation.textureNs / 1e6;
        frame.meshCreateMs = creation.meshNs / 1e6;
        frame.bindingCreateMs = creation.bindingNs / 1e6;
        frame.pipelineCreateMs = creation.pipelineNs / 1e6;
        frame.uploadsDeferred = quint64(uploadsDeferred);
        frame.exposureActive = exposureActive();
        frame.exposureMeasured = luminance.measured;
        frame.exposureMeasuredLog2 = luminance.measuredLog2;
        frame.exposureReferenceLog2 = exposureControl.referenceLog2;
        frame.exposureCorrection = luminance.correction;
        frame.exposureBias = exposureControl.biasStops;
    }
    if (FrameLog::active()) {
        FrameLog::add(FrameLog::TexturesMade, creation.textures);
        FrameLog::add(FrameLog::TextureMs, creation.textureNs / 1e6);
        FrameLog::add(FrameLog::MeshMs, creation.meshNs / 1e6);
        FrameLog::add(FrameLog::UploadsDeferred, uploadsDeferred);
    }
    Renderer::renderFrame();
}

void RhiRenderer::logCreation() {
    const Creation &c = creation;
    const qint64 total = c.pipelineNs + c.bindingNs + c.textureNs + c.meshNs;
    if (total > 100000000)
        qInfo().noquote() << QString("QRhi frame made resources for %1 ms: %2 pipelines %3 ms, %4 resource sets %5 ms, "
                                     "%6 textures %7 ms, meshes %8 ms")
                             .arg(total / 1e6, 0, 'f', 1).arg(c.pipelines).arg(c.pipelineNs / 1e6, 0, 'f', 1)
                             .arg(c.bindings).arg(c.bindingNs / 1e6, 0, 'f', 1).arg(c.textures)
                             .arg(c.textureNs / 1e6, 0, 'f', 1).arg(c.meshNs / 1e6, 0, 'f', 1);
    creation = Creation();
    uploadsDeferred = 0;
}

void RhiRenderer::resetFrame() {
    // A frame without a UI draws none.
    pendingUi = nullptr;
    beginFrameIfNeeded();
    logCreation();
    QueueRenderer::resetFrame();
    lightsPrepared = false;
    glowSplats.gathered = glowSplats.pending = false;
    ambientOcclusionApplied = false;
    sceneProjectionValid = false;
    skyResources.viewRendered = false;
    skyResources.usesThisFrame = 0;
    cloudsApplied = false;
    cloudsPrepared = false;
    reflectionCloudsApplied = false;
    shaftsApplied = false;
    fogResources.ready = false;
    fogResources.skyApplied = false;
    rainApplied = false;
    motionBlurApplied = false;
    temporalApplied = false;
    overlayProjectionValid = false;
    viewMotionTaken = false;
    std::fill(std::begin(skyInverseValid), std::end(skyInverseValid), false);
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
    // The signals' grid for the fog (signal and train lamps' glow
    // emitters), from the same queue as the lamps: gathered in the fog's
    // own preparation it came from a queue the views drawn before had
    // already taken, empty but in frames without them (the user,
    // 2026-10-17: train lamps strobing in fog, their light in it mostly
    // gone, dragged with the camera).
    // (Whether the view's sky takes fog is not known yet: the fog's own.)
    if ((fogFogged() || fog.rain > 0.05f) && !fogResources.unsupported)
        if (QRhiResourceUpdateBatch *uploads = RhiTextures::updates())
            prepareSignalGrid(uploads);
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
    {
        // With the sky enclosure the lamps keep their strength here and the
        // shaders dim them where the sky is open (task 27).
        const float day = enclosure.enabled && enclosureDrawn && enclosureMap.valid() && !exposureActive()
                ? 1.0f : gluu->localLightAdaptation;
        // With auto exposure the lamps reach as far as they show: their
        // ranges follow the exposure, in half stops so that adapting does
        // not rebuild the grid each frame (task 29).
        const float shown = exposureActive() ? std::exp2(std::round(std::log2(autoExposure()) * 2.0f) * 0.5f) : 1.0f;
        gatherLights(frameLights, Game::localLightsExposure * day, Game::localLightsEmissiveGain * day, false, 0.0f,
                     shown);
    }
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
    if (!uploadLightGrid(lightGrid, lightData, lightCells, lightIndices, batch))
        lightGrid = LightGrid();
}

bool RhiRenderer::uploadLightGrid(const LightGrid &grid, QRhiTexture *&data, QRhiTexture *&cells,
                                  QRhiTexture *&indices, QRhiResourceUpdateBatch *batch) {
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
    const int lights = grid.lightCount();
    const int indexRows = int(grid.indexTexels.size() / LightGrid::IndexWidth);
    if (!ensure(data, QRhiTexture::RGBA32F, 4, lights)
            || !ensure(cells, QRhiTexture::RGBA32F, LightGrid::CellsWidth, LightGrid::CellsY)
            || !ensure(indices, QRhiTexture::R32F, LightGrid::IndexWidth, indexRows))
        return false;
    upload(data, grid.lightTexels, 4, 4);
    upload(cells, grid.cellTexels, LightGrid::CellsWidth, 4);
    upload(indices, grid.indexTexels, LightGrid::IndexWidth, 1);
    return true;
}

void RhiRenderer::releaseLights() {
    delete lightData;
    delete lightCells;
    delete lightIndices;
    lightData = lightCells = lightIndices = nullptr;
    lightGrid = LightGrid();
    lightsHash = 0;
    delete signalData;
    delete signalCells;
    delete signalIndices;
    signalData = signalCells = signalIndices = nullptr;
    signalGrid = LightGrid();
    signalHash = 0;
}
