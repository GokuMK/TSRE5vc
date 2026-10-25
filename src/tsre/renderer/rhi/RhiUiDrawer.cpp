/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/renderer/rhi/RhiUiDrawer.h>
#include <tsre/renderer/UiDrawList.h>
#include <tsre/renderer/UiProjection.h>
#include <tsre/renderer/rhi/RhiProgram.h>

#include <QMatrix4x4>
#include <rhi/qrhi.h>

namespace {

const char *VertexShader = R"(#version 440
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 colour;
layout(location = 2) in vec2 uv;
layout(std140, binding = 0) uniform Draw {
    mat4 transform;
    vec2 translation;
};
layout(location = 0) out vec4 vColour;
layout(location = 1) out vec2 vUv;
void main() {
    vColour = colour;
    vUv = uv;
    gl_Position = transform * vec4(position + translation, 0.0, 1.0);
}
)";

const char *FragmentShader = R"(#version 440
layout(location = 0) in vec4 vColour;
layout(location = 1) in vec2 vUv;
layout(binding = 1) uniform sampler2D image;
layout(location = 0) out vec4 fragColor;
void main() {
    fragColor = vColour * texture(image, vUv);
}
)";

// mat4 and vec2, std140.
const quint32 UniformBytes = 80;

}

RhiUiDrawer::RhiUiDrawer(QRhi *rhi) : rhi(rhi) {
}

// Called with the frames done (the renderer's resources going).
RhiUiDrawer::~RhiUiDrawer() {
    for (auto &entry : bindings)
        delete entry.second;
    for (auto &entry : geometries) {
        delete entry.second.vertices;
        delete entry.second.indices;
    }
    for (auto &entry : textures)
        delete entry.second;
    delete pipeline;
    delete layout;
    delete uniforms;
    delete sampler;
    delete white;
}

bool RhiUiDrawer::ensurePipeline(QRhiRenderPassDescriptor *pass, QRhiResourceUpdateBatch *batch) {
    if (sampler == nullptr) {
        sampler = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                  QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        sampler->create();
        // Untextured draws sample white.
        white = rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
        white->create();
        QImage pixel(1, 1, QImage::Format_RGBA8888_Premultiplied);
        pixel.fill(Qt::white);
        batch->uploadTexture(white, pixel);
    }
    if (pipeline != nullptr && pipelinePass == pass)
        return true;
    if (pipeline != nullptr)
        pipeline->deleteLater();
    pipeline = nullptr;
    static QShader vertex, fragment;
    if (!fragment.isValid()) {
        vertex = bakeInline(VertexShader, QShader::VertexStage, rhi);
        fragment = bakeInline(FragmentShader, QShader::FragmentStage, rhi);
    }
    if (!vertex.isValid() || !fragment.isValid())
        return false;
    // The pipeline's layout: the kinds of bindings every draw's set has.
    if (layout == nullptr) {
        layout = rhi->newShaderResourceBindings();
        layout->setBindings({QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                                 0, QRhiShaderResourceBinding::VertexStage, uniforms, UniformBytes),
                             QRhiShaderResourceBinding::sampledTexture(
                                 1, QRhiShaderResourceBinding::FragmentStage, white, sampler)});
        layout->create();
    }
    pipeline = rhi->newGraphicsPipeline();
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
    QRhiVertexInputLayout input;
    input.setBindings({QRhiVertexInputBinding(sizeof(UiDrawList::Vertex))});
    input.setAttributes({QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
                         QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::UNormByte4, 8),
                         QRhiVertexInputAttribute(0, 2, QRhiVertexInputAttribute::Float2, 12)});
    pipeline->setVertexInputLayout(input);
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    pipeline->setTargetBlends({blend});
    pipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);
    pipeline->setShaderResourceBindings(layout);
    pipeline->setRenderPassDescriptor(pass);
    if (!pipeline->create()) {
        delete pipeline;
        pipeline = nullptr;
        return false;
    }
    pipelinePass = pass;
    return true;
}

void RhiUiDrawer::ensureUniforms(quint32 draws) {
    if (uniformStride == 0)
        uniformStride = quint32(rhi->ubufAligned(int(UniformBytes)));
    if (uniforms != nullptr && draws <= uniformCapacity)
        return;
    // The resource sets and the pipeline's layout name the buffer: made
    // again with it.
    for (auto &entry : bindings)
        entry.second->deleteLater();
    bindings.clear();
    if (layout != nullptr)
        layout->deleteLater();
    layout = nullptr;
    if (pipeline != nullptr)
        pipeline->deleteLater();
    pipeline = nullptr;
    if (uniforms != nullptr)
        uniforms->deleteLater();
    uniformCapacity = std::max<quint32>(64, draws + draws / 2);
    uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, uniformStride * uniformCapacity);
    uniforms->create();
}

const RhiUiDrawer::Geometry *RhiUiDrawer::geometryFor(UiDrawList &list, quint64 id,
                                                      QRhiResourceUpdateBatch *batch) {
    auto kept = geometries.find(id);
    if (kept != geometries.end())
        return &kept->second;
    auto source = list.geometries.find(id);
    if (source == list.geometries.end() || source->second.indices.empty())
        return nullptr;
    const auto &vertices = source->second.vertices;
    const auto &indices = source->second.indices;
    Geometry geometry;
    const quint32 vertexBytes = quint32(vertices.size() * sizeof(UiDrawList::Vertex));
    const quint32 indexBytes = quint32(indices.size() * sizeof(quint32));
    geometry.vertices = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vertexBytes);
    geometry.indices = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, indexBytes);
    geometry.vertices->create();
    geometry.indices->create();
    batch->uploadStaticBuffer(geometry.vertices, vertices.data());
    batch->uploadStaticBuffer(geometry.indices, indices.data());
    geometry.count = quint32(indices.size());
    return &(geometries[id] = geometry);
}

QRhiShaderResourceBindings *RhiUiDrawer::bindingsFor(UiDrawList &list, quint64 texture,
                                                     QRhiResourceUpdateBatch *batch) {
    auto kept = bindings.find(texture);
    if (kept != bindings.end())
        return kept->second;
    QRhiTexture *sampled = white;
    if (texture != 0) {
        auto made = textures.find(texture);
        if (made != textures.end()) {
            sampled = made->second;
        } else {
            auto source = list.textures.find(texture);
            if (source != list.textures.end() && !source->second.isNull()) {
                sampled = rhi->newTexture(QRhiTexture::RGBA8, source->second.size());
                sampled->create();
                // The first row at v = 0, as RmlUi's texture coordinates
                // count from the top.
                batch->uploadTexture(sampled, source->second);
                textures[texture] = sampled;
            } else {
                return bindingsFor(list, 0, batch);
            }
        }
    }
    QRhiShaderResourceBindings *set = rhi->newShaderResourceBindings();
    set->setBindings({QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                          0, QRhiShaderResourceBinding::VertexStage, uniforms, UniformBytes),
                      QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                                sampled, sampler)});
    set->create();
    bindings[texture] = set;
    return set;
}

void RhiUiDrawer::freeReleased(UiDrawList &list) {
    for (quint64 id : list.releasedGeometries) {
        auto kept = geometries.find(id);
        if (kept == geometries.end())
            continue;
        kept->second.vertices->deleteLater();
        kept->second.indices->deleteLater();
        geometries.erase(kept);
    }
    for (quint64 id : list.releasedTextures) {
        auto set = bindings.find(id);
        if (set != bindings.end()) {
            set->second->deleteLater();
            bindings.erase(set);
        }
        auto kept = textures.find(id);
        if (kept == textures.end())
            continue;
        kept->second->deleteLater();
        textures.erase(kept);
    }
    list.releasedGeometries.clear();
    list.releasedTextures.clear();
}

void RhiUiDrawer::prepare(UiDrawList &list, QRhiResourceUpdateBatch *batch, QRhiRenderPassDescriptor *pass,
                          const QSize &frameSize) {
    commands.clear();
    size = frameSize;
    if (list.width < 1 || list.height < 1)
        return;
    freeReleased(list);
    if (list.draws.empty())
        return;
    ensureUniforms(quint32(list.draws.size()));
    if (!ensurePipeline(pass, batch))
        return;

    // Pixels to QRhi's clip space (its y and depth conventions).
    float pixels[16];
    UiProjection::pixels(pixels, list.width, list.height);
    float projection[16];
    UiProjection::multiply(projection, rhi->clipSpaceCorrMatrix().constData(), pixels);

    std::vector<char> block(size_t(uniformStride) * list.draws.size(), 0);
    for (const UiDrawList::Draw &draw : list.draws) {
        const Geometry *geometry = geometryFor(list, draw.geometry, batch);
        if (geometry == nullptr)
            continue;
        Command command;
        command.geometry = geometry;
        command.bindings = bindingsFor(list, draw.texture, batch);
        command.uniformOffset = quint32(commands.size()) * uniformStride;
        command.scissor = draw.scissor;
        std::copy(draw.scissorRect, draw.scissorRect + 4, command.scissorRect);
        float *values = reinterpret_cast<float *>(block.data() + command.uniformOffset);
        UiProjection::multiply(values, projection, draw.transformed ? draw.transform : nullptr);
        values[16] = draw.translation[0];
        values[17] = draw.translation[1];
        commands.push_back(command);
    }
    if (!commands.empty())
        batch->updateDynamicBuffer(uniforms, 0, quint32(commands.size()) * uniformStride, block.data());
}

void RhiUiDrawer::record(QRhiCommandBuffer *cb) {
    if (commands.empty() || pipeline == nullptr)
        return;
    cb->setGraphicsPipeline(pipeline);
    cb->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
    for (const Command &command : commands) {
        // From the bottom left; the whole frame when not cut.
        if (command.scissor)
            cb->setScissor(QRhiScissor(command.scissorRect[0],
                                       size.height() - command.scissorRect[1] - command.scissorRect[3],
                                       command.scissorRect[2], command.scissorRect[3]));
        else
            cb->setScissor(QRhiScissor(0, 0, size.width(), size.height()));
        const QRhiCommandBuffer::DynamicOffset offset(0, command.uniformOffset);
        cb->setShaderResources(command.bindings, 1, &offset);
        const QRhiCommandBuffer::VertexInput input(command.geometry->vertices, 0);
        cb->setVertexInput(0, 1, &input, command.geometry->indices, 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(command.geometry->count);
    }
    commands.clear();
}
