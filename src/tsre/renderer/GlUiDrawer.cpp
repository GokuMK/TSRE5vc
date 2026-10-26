/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/renderer/GlUiDrawer.h>
#include <tsre/renderer/UiDrawList.h>
#include <tsre/renderer/UiProjection.h>

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>

namespace {

const char *VertexShader = R"(#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 colour;
layout(location = 2) in vec2 uv;
uniform mat4 transform;
uniform vec2 translation;
out vec4 vColour;
out vec2 vUv;
void main() {
    vColour = colour;
    vUv = uv;
    gl_Position = transform * vec4(position + translation, 0.0, 1.0);
}
)";

const char *FragmentShader = R"(#version 330 core
in vec4 vColour;
in vec2 vUv;
uniform sampler2D image;
// 1 for straight alpha textures (TexLib's).
uniform float premultiply;
out vec4 fragColor;
void main() {
    vec4 texel = texture(image, vUv);
    texel.rgb *= mix(1.0, texel.a, premultiply);
    fragColor = vColour * texel;
}
)";

}

GlUiDrawer::~GlUiDrawer() {
    release();
}

void GlUiDrawer::release() {
    QOpenGLContext *current = QOpenGLContext::currentContext();
    if (current != nullptr && current == context) {
        QOpenGLExtraFunctions *f = current->extraFunctions();
        for (auto &entry : geometries) {
            f->glDeleteVertexArrays(1, &entry.second.array);
            f->glDeleteBuffers(1, &entry.second.vertices);
            f->glDeleteBuffers(1, &entry.second.indices);
        }
        for (auto &entry : textures)
            f->glDeleteTextures(1, &entry.second);
        if (white != 0)
            f->glDeleteTextures(1, &white);
    }
    geometries.clear();
    textures.clear();
    white = 0;
    delete program;
    program = nullptr;
    context.clear();
}

bool GlUiDrawer::ensureProgram() {
    if (program != nullptr)
        return program->isLinked();
    context = QOpenGLContext::currentContext();
    program = new QOpenGLShaderProgram();
    program->addShaderFromSourceCode(QOpenGLShader::Vertex, VertexShader);
    program->addShaderFromSourceCode(QOpenGLShader::Fragment, FragmentShader);
    if (!program->link()) {
        qWarning().noquote() << "[ui] the OpenGL UI program:" << program->log();
        return false;
    }
    transformUniform = program->uniformLocation("transform");
    translationUniform = program->uniformLocation("translation");
    premultiplyUniform = program->uniformLocation("premultiply");
    program->bind();
    program->setUniformValue("image", 0);
    program->release();
    // Untextured draws sample white.
    QOpenGLExtraFunctions *f = context->extraFunctions();
    const quint8 pixel[4] = {255, 255, 255, 255};
    f->glGenTextures(1, &white);
    f->glBindTexture(GL_TEXTURE_2D, white);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return true;
}

const GlUiDrawer::Geometry *GlUiDrawer::geometryFor(UiDrawList &list, quint64 id) {
    auto kept = geometries.find(id);
    if (kept != geometries.end())
        return &kept->second;
    auto source = list.geometries.find(id);
    if (source == list.geometries.end())
        return nullptr;
    QOpenGLExtraFunctions *f = context->extraFunctions();
    Geometry geometry;
    geometry.count = int(source->second.indices.size());
    f->glGenVertexArrays(1, &geometry.array);
    f->glBindVertexArray(geometry.array);
    f->glGenBuffers(1, &geometry.vertices);
    f->glBindBuffer(GL_ARRAY_BUFFER, geometry.vertices);
    f->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(source->second.vertices.size() * sizeof(UiDrawList::Vertex)),
                    source->second.vertices.data(), GL_STATIC_DRAW);
    f->glGenBuffers(1, &geometry.indices);
    f->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry.indices);
    f->glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(source->second.indices.size() * sizeof(quint32)),
                    source->second.indices.data(), GL_STATIC_DRAW);
    const GLsizei stride = sizeof(UiDrawList::Vertex);
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(0));
    f->glEnableVertexAttribArray(1);
    f->glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void *>(8));
    f->glEnableVertexAttribArray(2);
    f->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(12));
    f->glBindVertexArray(0);
    return &(geometries[id] = geometry);
}

unsigned int GlUiDrawer::textureFor(UiDrawList &list, quint64 id) {
    if (id == 0)
        return white;
    auto kept = textures.find(id);
    if (kept != textures.end())
        return kept->second;
    auto source = list.textures.find(id);
    if (source == list.textures.end())
        return white;
    const QImage &image = source->second;
    QOpenGLExtraFunctions *f = context->extraFunctions();
    unsigned int texture = 0;
    f->glGenTextures(1, &texture);
    f->glBindTexture(GL_TEXTURE_2D, texture);
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    // The first row at v = 0, as RmlUi's texture coordinates count from the
    // top.
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    image.constBits());
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    textures[id] = texture;
    return texture;
}

void GlUiDrawer::freeReleased(UiDrawList &list) {
    QOpenGLExtraFunctions *f = context->extraFunctions();
    for (quint64 id : list.releasedGeometries) {
        auto kept = geometries.find(id);
        if (kept == geometries.end())
            continue;
        f->glDeleteVertexArrays(1, &kept->second.array);
        f->glDeleteBuffers(1, &kept->second.vertices);
        f->glDeleteBuffers(1, &kept->second.indices);
        geometries.erase(kept);
    }
    for (quint64 id : list.releasedTextures) {
        auto kept = textures.find(id);
        if (kept == textures.end())
            continue;
        f->glDeleteTextures(1, &kept->second);
        textures.erase(kept);
    }
    list.releasedGeometries.clear();
    list.releasedTextures.clear();
}

void GlUiDrawer::draw(UiDrawList &list, const LibraryTexture &library) {
    QOpenGLContext *current = QOpenGLContext::currentContext();
    if (current == nullptr || list.width < 1 || list.height < 1)
        return;
    if (context != nullptr && context != current)
        release();
    if (!ensureProgram())
        return;
    freeReleased(list);
    if (list.draws.empty())
        return;

    QOpenGLExtraFunctions *f = current->extraFunctions();
    GLint viewport[4];
    f->glGetIntegerv(GL_VIEWPORT, viewport);
    GLint blendFunction[4];
    f->glGetIntegerv(GL_BLEND_SRC_RGB, &blendFunction[0]);
    f->glGetIntegerv(GL_BLEND_DST_RGB, &blendFunction[1]);
    f->glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendFunction[2]);
    f->glGetIntegerv(GL_BLEND_DST_ALPHA, &blendFunction[3]);
    const GLboolean depthTest = f->glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blend = f->glIsEnabled(GL_BLEND);
    const GLboolean cull = f->glIsEnabled(GL_CULL_FACE);
    const GLboolean scissor = f->glIsEnabled(GL_SCISSOR_TEST);

    f->glViewport(0, 0, list.width, list.height);
    f->glDisable(GL_DEPTH_TEST);
    f->glDisable(GL_CULL_FACE);
    f->glEnable(GL_BLEND);
    f->glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    f->glActiveTexture(GL_TEXTURE0);
    program->bind();

    float projection[16];
    UiProjection::pixels(projection, list.width, list.height);
    for (const UiDrawList::Draw &draw : list.draws) {
        const Geometry *geometry = geometryFor(list, draw.geometry);
        if (geometry == nullptr || geometry->count == 0)
            continue;
        // A TexLib texture: drawn once loaded, its alpha straight.
        unsigned int texture = 0;
        const auto fromLibrary = list.libraryTextures.find(draw.texture);
        if (fromLibrary != list.libraryTextures.end()) {
            if (!library || !library(fromLibrary->second, texture))
                continue;
        } else {
            texture = textureFor(list, draw.texture);
        }
        if (draw.scissor) {
            f->glEnable(GL_SCISSOR_TEST);
            // From the bottom left.
            f->glScissor(draw.scissorRect[0], list.height - draw.scissorRect[1] - draw.scissorRect[3],
                         draw.scissorRect[2], draw.scissorRect[3]);
        } else {
            f->glDisable(GL_SCISSOR_TEST);
        }
        float transform[16];
        UiProjection::multiply(transform, projection, draw.transformed ? draw.transform : nullptr);
        program->setUniformValue(transformUniform, *reinterpret_cast<const float(*)[4][4]>(transform));
        program->setUniformValue(translationUniform, draw.translation[0], draw.translation[1]);
        program->setUniformValue(premultiplyUniform, fromLibrary != list.libraryTextures.end() ? 1.0f : 0.0f);
        f->glBindTexture(GL_TEXTURE_2D, texture);
        f->glBindVertexArray(geometry->array);
        f->glDrawElements(GL_TRIANGLES, geometry->count, GL_UNSIGNED_INT, nullptr);
    }
    f->glBindVertexArray(0);
    f->glBindTexture(GL_TEXTURE_2D, 0);
    program->release();

    f->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    f->glBlendFuncSeparate(GLenum(blendFunction[0]), GLenum(blendFunction[1]), GLenum(blendFunction[2]),
                           GLenum(blendFunction[3]));
    if (depthTest)
        f->glEnable(GL_DEPTH_TEST);
    if (!blend)
        f->glDisable(GL_BLEND);
    if (cull)
        f->glEnable(GL_CULL_FACE);
    if (scissor)
        f->glEnable(GL_SCISSOR_TEST);
    else
        f->glDisable(GL_SCISSOR_TEST);
}
