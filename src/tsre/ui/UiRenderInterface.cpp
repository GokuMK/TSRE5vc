/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/ui/UiRenderInterface.h>

#include <RmlUi/Core/Vertex.h>
#include <tsre/texture/TexLib.h>
#include <QDebug>
#include <atomic>
#include <cstring>

namespace Ui {

namespace {

static_assert(sizeof(Rml::Vertex) == sizeof(UiDrawList::Vertex), "RmlUi's vertex is the draw list's");

// Ids unique in the program, so renderers can keep copies of several lists.
quint64 nextId() {
    static std::atomic<quint64> id{1};
    return id++;
}

}

void RenderInterface::beginFrame(int width, int height) {
    drawList.width = width;
    drawList.height = height;
    drawList.draws.clear();
}

Rml::CompiledGeometryHandle RenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                             Rml::Span<const int> indices) {
    const quint64 id = nextId();
    UiDrawList::Geometry &geometry = drawList.geometries[id];
    geometry.vertices.resize(vertices.size());
    std::memcpy(geometry.vertices.data(), vertices.data(), vertices.size() * sizeof(Rml::Vertex));
    geometry.indices.assign(indices.begin(), indices.end());
    return Rml::CompiledGeometryHandle(id);
}

void RenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                                     Rml::TextureHandle texture) {
    UiDrawList::Draw draw;
    draw.geometry = quint64(geometry);
    draw.texture = quint64(texture);
    draw.translation[0] = translation.x;
    draw.translation[1] = translation.y;
    draw.scissor = scissor;
    std::copy(scissorRect, scissorRect + 4, draw.scissorRect);
    draw.transformed = transformed;
    if (transformed)
        std::copy(transform, transform + 16, draw.transform);
    drawList.draws.push_back(draw);
}

void RenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    drawList.geometries.erase(quint64(geometry));
    drawList.releasedGeometries.push_back(quint64(geometry));
}

Rml::TextureHandle RenderInterface::LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) {
    const QString path = QString::fromStdString(source);
    // Its size now, its pixels from TexLib's thread: the render thread does
    // not wait for them.
    const QSize size = TexLib::imageSize(path);
    const int texture = size.isEmpty() ? -1 : TexLib::addTex(path, false, TexLib::Loading::Background);
    if (texture < 0) {
        qWarning().noquote() << "[ui] image not read:" << path;
        return 0;
    }
    dimensions = Rml::Vector2i(size.width(), size.height());
    const quint64 id = nextId();
    drawList.libraryTextures[id] = texture;
    return Rml::TextureHandle(id);
}

Rml::TextureHandle RenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) {
    // RmlUi gives premultiplied RGBA, rows from the top.
    QImage image(dimensions.x, dimensions.y, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < dimensions.y; ++y)
        std::memcpy(image.scanLine(y), source.data() + size_t(y) * size_t(dimensions.x) * 4,
                    size_t(dimensions.x) * 4);
    const quint64 id = nextId();
    drawList.textures[id] = image;
    return Rml::TextureHandle(id);
}

void RenderInterface::ReleaseTexture(Rml::TextureHandle texture) {
    auto library = drawList.libraryTextures.find(quint64(texture));
    if (library != drawList.libraryTextures.end()) {
        TexLib::delRef(library->second);
        drawList.libraryTextures.erase(library);
    }
    drawList.textures.erase(quint64(texture));
    drawList.releasedTextures.push_back(quint64(texture));
}

void RenderInterface::EnableScissorRegion(bool enable) {
    scissor = enable;
}

void RenderInterface::SetScissorRegion(Rml::Rectanglei region) {
    scissorRect[0] = region.Left();
    scissorRect[1] = region.Top();
    scissorRect[2] = region.Width();
    scissorRect[3] = region.Height();
}

void RenderInterface::SetTransform(const Rml::Matrix4f *matrix) {
    transformed = matrix != nullptr;
    if (transformed)
        std::memcpy(transform, matrix->data(), sizeof(transform));
}

}
