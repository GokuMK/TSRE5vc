/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// The QRhi renderer's environment map and water reflection: offscreen
// targets the scene is drawn into, and the textures the scene samples.

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include "RhiRenderSurface.h"
#include "RhiShaderSource.h"
#include "RhiTextures.h"
#include <QDebug>

namespace {

class RhiEnvironmentStorage : public EnvironmentMap::Storage {
public:
    explicit RhiEnvironmentStorage(RhiRenderer *renderer) : renderer(renderer) {}
    ~RhiEnvironmentStorage() override { renderer->releaseEnvironment(); }
    bool ready(int faceSize) const override { return renderer->environmentReady(faceSize); }
    bool create(int faceSize, int levels) override {
        return renderer->createEnvironment(faceSize, levels);
    }
    void beginFace(int face, const float *clearColor) override {
        renderer->beginEnvironmentFace(face, clearColor);
    }
    void endFaces(const QVector<int> &prefilterFaces) override {
        renderer->endEnvironmentFaces(prefilterFaces);
    }
    void uploadFaces(const QByteArray *faces) override { renderer->uploadEnvironment(faces); }
    void bind(bool prefiltered) override { renderer->bindEnvironment(prefiltered); }
    void unbind() override { renderer->unbindEnvironment(); }
    void drawPreview(int x, int y, int cellSize) override {
        renderer->drawEnvironmentPreview(x, y, cellSize);
    }
    void release() override { renderer->releaseEnvironment(); }

private:
    RhiRenderer *renderer;
};

class RhiReflectionStorage : public PlanarReflection::Storage {
public:
    explicit RhiReflectionStorage(RhiRenderer *renderer) : renderer(renderer) {}
    ~RhiReflectionStorage() override { renderer->releaseReflection(); }
    bool ready(int width, int height) const override {
        return renderer->reflectionReady(width, height);
    }
    bool create(int width, int height) override { return renderer->createReflection(width, height); }
    void begin(const float *clearColor) override { renderer->beginReflection(clearColor); }
    void end() override { renderer->endReflection(); }
    void bind() override { renderer->bindReflection(true); }
    void unbind() override { renderer->bindReflection(false); }
    void release() override { renderer->releaseReflection(); }

private:
    RhiRenderer *renderer;
};

}

EnvironmentMap::Storage *RhiRenderer::createEnvironmentStorage() {
    return new RhiEnvironmentStorage(this);
}

PlanarReflection::Storage *RhiRenderer::createReflectionStorage() {
    return new RhiReflectionStorage(this);
}

void RhiRenderer::beginOffscreen(int which, Attachments *attachments, const float *clearColor) {
    beginFrameIfNeeded();
    flushTarget();
    currentTarget = which;
    TargetState &state = targets[which];
    state.attachments = attachments;
    // As glClearColor: later clears take this colour too.
    nextClearColor = QColor::fromRgbF(clearColor[0], clearColor[1], clearColor[2], 1.0f);
    state.color = nextClearColor;
    state.clearColor = state.clearDepth = true;
    setViewport(0, 0, attachments->size.width(), attachments->size.height());
}

bool RhiRenderer::environmentReady(int faceSize) const {
    return environment.cube != nullptr && environment.size == faceSize;
}

bool RhiRenderer::createEnvironment(int faceSize, int levels) {
    releaseEnvironment();
    if (faceSize < 1 || levels < 1)
        return false;
    const QSize size(faceSize, faceSize);
    Environment &e = environment;
    e.cube = rhi->newTexture(QRhiTexture::RGBA8, size, 1,
                             QRhiTexture::CubeMap | QRhiTexture::MipMapped
                             | QRhiTexture::UsedWithGenerateMips | QRhiTexture::RenderTarget);
    e.prefiltered = rhi->newTexture(QRhiTexture::RGBA8, size, 1,
                                    QRhiTexture::CubeMap | QRhiTexture::MipMapped
                                    | QRhiTexture::RenderTarget);
    e.depth = rhi->newTexture(QRhiTexture::D32F, size, 1, QRhiTexture::RenderTarget);
    if (!e.cube->create() || !e.prefiltered->create() || !e.depth->create()) {
        releaseEnvironment();
        return false;
    }
    e.size = faceSize;
    e.levels = levels;
    // Faces are looked up by direction: they keep OpenGL's rows.
    for (int face = 0; face < EnvironmentMap::FaceCount; ++face) {
        Attachments &target = e.faces[face];
        target.color = e.cube;
        target.depth = e.depth;
        target.colorLayer = face;
        target.colorLevel = 0;
        target.ownsColor = target.ownsDepth = false;
        target.openGlRows = true;
        target.size = size;
        if (!buildTargets(target)) {
            releaseEnvironment();
            return false;
        }
    }
    e.prefilterTargets.resize(size_t(EnvironmentMap::FaceCount * levels));
    for (int face = 0; face < EnvironmentMap::FaceCount; ++face) {
        for (int level = 0; level < levels; ++level) {
            Attachments &target = e.prefilterTargets[size_t(face * levels + level)];
            target.color = e.prefiltered;
            target.depth = nullptr;
            target.colorLayer = face;
            target.colorLevel = level;
            target.ownsColor = target.ownsDepth = false;
            const int side = std::max(faceSize >> level, 1);
            target.size = QSize(side, side);
            if (!buildTargets(target)) {
                releaseEnvironment();
                return false;
            }
        }
    }

    // The prefilter pass: one uniform block per face and level.
    if (!e.program) {
        e.program = std::make_unique<RhiProgram>();
        e.program->kind = RhiProgram::OTHER;
        e.program->source = &context->programFromSource(
                    "environment-prefilter", EnvironmentMap::fullScreenVertexShader(),
                    EnvironmentMap::prefilterFragmentShader());
        if (e.program->valid())
            reflect(*e.program);
    }
    if (!e.program->valid()) {
        releaseEnvironment();
        return false;
    }
    RhiProgram &program = *e.program;
    e.uniformStride = quint32(rhi->ubufAligned(int(program.block.size())));
    const int blocks = EnvironmentMap::FaceCount * levels;
    QByteArray data(qsizetype(e.uniformStride) * blocks, '\0');
    for (int face = 0; face < EnvironmentMap::FaceCount; ++face) {
        for (int level = 0; level < levels; ++level) {
            program.setInt("face", face);
            program.setFloat("roughness", levels > 1 ? float(level) / (levels - 1) : 0.0f);
            program.setFloat("sourceSize", float(faceSize));
            // The face's first row is texture row 0, as in OpenGL: flipped
            // where clip space and the framebuffer disagree about y.
            program.setFloat("rhiFlipY", rhi->isYUpInNDC() != rhi->isYUpInFramebuffer() ? 1.0f : 0.0f);
            std::memcpy(data.data() + qsizetype(face * levels + level) * e.uniformStride,
                        program.block.data(), program.block.size());
        }
    }
    // Dynamic: Direct3D 11 takes uniform buffers of no other type.
    e.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, quint32(data.size()));
    e.sampler = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
                                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    if (!e.uniforms->create() || !e.sampler->create()) {
        releaseEnvironment();
        return false;
    }
    if (QRhiResourceUpdateBatch *batch = RhiTextures::updates())
        batch->updateDynamicBuffer(e.uniforms, 0, quint32(data.size()), data.constData());
    const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
    e.bindings = rhi->newShaderResourceBindings();
    e.bindings->setBindings({
        QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                    RhiShaderSource::UniformBlockBinding, stages, e.uniforms,
                    quint32(program.block.size())),
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage,
                                                  e.cube, e.sampler)});
    e.pipeline = rhi->newGraphicsPipeline();
    e.pipeline->setShaderStages({{QRhiShaderStage::Vertex, program.source->vertex},
                                 {QRhiShaderStage::Fragment, program.source->fragment}});
    e.pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    e.pipeline->setShaderResourceBindings(e.bindings);
    e.pipeline->setRenderPassDescriptor(e.prefilterTargets[0].pipelinePass());
    if (!e.bindings->create() || !e.pipeline->create()) {
        releaseEnvironment();
        return false;
    }
    return true;
}

void RhiRenderer::beginEnvironmentFace(int face, const float *clearColor) {
    if (environment.cube == nullptr || face < 0 || face >= EnvironmentMap::FaceCount)
        return;
    beginOffscreen(TargetEnvironment, &environment.faces[face], clearColor);
}

void RhiRenderer::endEnvironmentFaces(const QVector<int> &prefilterFaces) {
    if (environment.cube == nullptr)
        return;
    if (currentTarget == TargetEnvironment) {
        flushTarget();
        currentTarget = TARGET_VIEW;
    }
    environment.mipmapsPending = true;
    for (int face : prefilterFaces)
        if (!environment.prefilterPending.contains(face))
            environment.prefilterPending.append(face);
    runEnvironmentWork();
}

void RhiRenderer::uploadEnvironment(const QByteArray *faces) {
    QRhiResourceUpdateBatch *batch = RhiTextures::updates();
    if (environment.cube == nullptr || batch == nullptr)
        return;
    QVarLengthArray<QRhiTextureUploadEntry, 6> entries;
    for (int face = 0; face < EnvironmentMap::FaceCount; ++face) {
        if (faces[face].size() != qsizetype(environment.size) * environment.size * 4)
            return;
        QRhiTextureSubresourceUploadDescription description(faces[face]);
        description.setSourceSize(QSize(environment.size, environment.size));
        entries.append(QRhiTextureUploadEntry(face, 0, description));
    }
    QRhiTextureUploadDescription upload;
    upload.setEntries(entries.cbegin(), entries.cend());
    batch->uploadTexture(environment.cube, upload);
}

void RhiRenderer::runEnvironmentWork() {
    Environment &e = environment;
    if (e.cube == nullptr || (!e.mipmapsPending && e.prefilterPending.isEmpty()))
        return;
    RhiRenderSurface *s = surface();
    if (s == nullptr || s->frame().commandBuffer == nullptr)
        return;
    QRhiCommandBuffer *cb = s->frame().commandBuffer;
    // Uploaded faces first, then their mipmaps.
    if (QRhiResourceUpdateBatch *textures = RhiTextures::takeUpdates())
        cb->resourceUpdate(textures);
    if (e.mipmapsPending) {
        QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
        batch->generateMips(e.cube);
        cb->resourceUpdate(batch);
        e.mipmapsPending = false;
    }
    for (int level = 0; level < e.levels; ++level) {
        const int side = std::max(e.size >> level, 1);
        for (int face : std::as_const(e.prefilterPending)) {
            const quint32 index = quint32(face * e.levels + level);
            Attachments &target = e.prefilterTargets[index];
            cb->beginPass(target.targets[1], Qt::black, {1.0f, 0});
            cb->setGraphicsPipeline(e.pipeline);
            cb->setViewport(QRhiViewport(0, 0, float(side), float(side)));
            const QRhiCommandBuffer::DynamicOffset offset(RhiShaderSource::UniformBlockBinding,
                                                          index * e.uniformStride);
            cb->setShaderResources(e.bindings, 1, &offset);
            cb->draw(4);
            cb->endPass();
        }
    }
    e.prefilterPending.clear();
}

void RhiRenderer::bindEnvironment(bool prefiltered) {
    environment.sampled = prefiltered ? environment.prefiltered : environment.cube;
}

void RhiRenderer::drawEnvironmentPreview(int x, int y, int cellSize) {
    Environment &e = environment;
    RhiRenderSurface *s = surface();
    if (e.cube == nullptr || cellSize < 1 || s == nullptr || s->frame().commandBuffer == nullptr
            || !view.valid())
        return;
    if (currentTarget != TARGET_VIEW) {
        flushTarget();
        currentTarget = TARGET_VIEW;
    }
    flushTarget();
    if (!e.previewProgram) {
        e.previewProgram = std::make_unique<RhiProgram>();
        e.previewProgram->kind = RhiProgram::OTHER;
        e.previewProgram->source = &context->programFromSource(
                    "environment-preview", EnvironmentMap::fullScreenVertexShader(),
                    EnvironmentMap::previewFragmentShader());
        if (e.previewProgram->valid())
            reflect(*e.previewProgram);
    }
    RhiProgram &program = *e.previewProgram;
    if (!program.valid())
        return;
    if (e.previewPipeline != nullptr && e.previewPass != view.pipelinePass()) {
        delete e.previewPipeline;
        e.previewPipeline = nullptr;
    }
    if (e.previewUniforms == nullptr) {
        program.setFloat("rhiFlipY", rhi->isYUpInNDC() ? 0.0f : 1.0f);
        e.previewUniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                           quint32(program.block.size()));
        e.previewUniforms->create();
        if (QRhiResourceUpdateBatch *batch = RhiTextures::updates())
            batch->updateDynamicBuffer(e.previewUniforms, 0, quint32(program.block.size()), program.block.data());
    }
    if (e.previewBindings == nullptr) {
        e.previewBindings = rhi->newShaderResourceBindings();
        const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
        e.previewBindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(RhiShaderSource::UniformBlockBinding, stages,
                                                     e.previewUniforms),
            QRhiShaderResourceBinding::sampledTexture(EnvironmentMap::TextureUnit,
                                                      QRhiShaderResourceBinding::FragmentStage,
                                                      e.cube, e.sampler)});
        e.previewBindings->create();
    }
    if (e.previewPipeline == nullptr) {
        e.previewPipeline = rhi->newGraphicsPipeline();
        e.previewPipeline->setShaderStages({{QRhiShaderStage::Vertex, program.source->vertex},
                                            {QRhiShaderStage::Fragment, program.source->fragment}});
        e.previewPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        e.previewPipeline->setShaderResourceBindings(e.previewBindings);
        e.previewPipeline->setRenderPassDescriptor(view.pipelinePass());
        if (!e.previewPipeline->create()) {
            delete e.previewPipeline;
            e.previewPipeline = nullptr;
            return;
        }
        e.previewPass = view.pipelinePass();
    }
    QRhiCommandBuffer *cb = s->frame().commandBuffer;
    QRhiResourceUpdateBatch *uploads = RhiTextures::takeUpdates();
    cb->beginPass(view.targets[0], Qt::black, {1.0f, 0}, uploads);
    cb->setGraphicsPipeline(e.previewPipeline);
    cb->setViewport(QRhiViewport(float(x), float(y), float(cellSize * 4), float(cellSize * 3)));
    cb->setShaderResources(e.previewBindings);
    cb->draw(4);
    cb->endPass();
}

void RhiRenderer::releaseEnvironment() {
    Environment &e = environment;
    delete e.previewPipeline;
    delete e.previewBindings;
    delete e.previewUniforms;
    e.previewPipeline = nullptr;
    e.previewBindings = nullptr;
    e.previewUniforms = nullptr;
    e.previewPass = nullptr;
    if (currentTarget == TargetEnvironment)
        currentTarget = TARGET_VIEW;
    targets[TargetEnvironment].attachments = nullptr;
    targets[TargetEnvironment].draws.clear();
    targets[TargetEnvironment].keys.clear();
    targets[TargetEnvironment].bindings.clear();
    for (Attachments &face : e.faces)
        releaseAttachments(face);
    for (Attachments &target : e.prefilterTargets)
        releaseAttachments(target);
    e.prefilterTargets.clear();
    delete e.pipeline;
    delete e.bindings;
    delete e.uniforms;
    delete e.sampler;
    delete e.cube;
    delete e.prefiltered;
    delete e.depth;
    e.pipeline = nullptr;
    e.bindings = nullptr;
    e.uniforms = nullptr;
    e.sampler = nullptr;
    e.cube = e.prefiltered = e.depth = e.sampled = nullptr;
    e.size = e.levels = 0;
    e.mipmapsPending = false;
    e.prefilterPending.clear();
}

bool RhiRenderer::reflectionReady(int width, int height) const {
    return reflection.valid() && reflection.size == QSize(width, height);
}

bool RhiRenderer::createReflection(int width, int height) {
    releaseReflection();
    return createAttachments(reflection, QRhiTexture::RGBA8, QSize(width, height),
                             QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips);
}

void RhiRenderer::beginReflection(const float *clearColor) {
    if (reflection.valid())
        beginOffscreen(TargetReflection, &reflection, clearColor);
}

void RhiRenderer::endReflection() {
    if (currentTarget != TargetReflection)
        return;
    flushTarget();
    currentTarget = TARGET_VIEW;
    RhiRenderSurface *s = surface();
    if (s == nullptr || s->frame().commandBuffer == nullptr)
        return;
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->generateMips(reflection.color);
    s->frame().commandBuffer->resourceUpdate(batch);
}

void RhiRenderer::bindReflection(bool bound) {
    reflectionSampled = bound && reflection.valid() ? reflection.color : nullptr;
}

void RhiRenderer::releaseReflection() {
    if (currentTarget == TargetReflection)
        currentTarget = TARGET_VIEW;
    targets[TargetReflection].attachments = nullptr;
    targets[TargetReflection].draws.clear();
    targets[TargetReflection].keys.clear();
    targets[TargetReflection].bindings.clear();
    reflectionSampled = nullptr;
    releaseAttachments(reflection);
}
