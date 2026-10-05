/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "EnvironmentMap.h"
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <algorithm>
#include <cmath>
#include <vector>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/GLUU.h>

#ifndef GL_TEXTURE_CUBE_MAP_SEAMLESS
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#endif

namespace {

// Look direction and up vector of each face, in the OpenGL cube map
// convention (a face's first texture row is its top in the cube's frame).
const float FaceDirections[EnvironmentMap::FaceCount][2][3] = {
    {{ 1, 0, 0}, {0, -1, 0}},
    {{-1, 0, 0}, {0, -1, 0}},
    {{ 0, 1, 0}, {0, 0, 1}},
    {{ 0, -1, 0}, {0, 0, -1}},
    {{ 0, 0, 1}, {0, -1, 0}},
    {{ 0, 0, -1}, {0, -1, 0}}
};

quint32 hash(quint32 x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Value noise in [-1, 1] from integer coordinates.
float noise(int x, int y, quint32 seed) {
    return float(hash(quint32(x) * 73856093U ^ quint32(y) * 19349663U ^ seed) & 0xffff)
            / 32767.5f - 1.0f;
}

unsigned char byte(float value) {
    return static_cast<unsigned char>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// One warehouse face as RGBA rows, first row at the top of the face in the
// cube's frame. u runs right, v runs down, both 0..1.
std::vector<unsigned char> warehouseFace(int face, int size) {
    std::vector<unsigned char> pixels(size_t(size) * size * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (x + 0.5f) / size;
            const float v = (y + 0.5f) / size;
            float r, g, b;
            if (face == 2) {
                // Ceiling: dark steel with two rows of lamps.
                const float base = 0.20f + 0.02f * noise(x / 4, y / 4, 11);
                r = g = b = base;
                const bool beam = std::fmod(u * 6.0f, 1.0f) < 0.06f;
                if (beam) r = g = b = 0.14f;
                for (float lampV : {0.33f, 0.67f}) {
                    for (float lampU : {0.2f, 0.5f, 0.8f}) {
                        if (std::abs(u - lampU) < 0.09f && std::abs(v - lampV) < 0.025f) {
                            r = 1.0f; g = 0.98f; b = 0.92f;
                        }
                    }
                }
            } else if (face == 3) {
                // Floor: concrete slabs with joints.
                const float base = 0.36f + 0.035f * noise(x / 2, y / 2, 23)
                        + 0.02f * noise(x / 16, y / 16, 29);
                r = g = b = base;
                if (std::fmod(u * 4.0f, 1.0f) < 0.008f || std::fmod(v * 4.0f, 1.0f) < 0.008f)
                    r = g = b = base * 0.7f;
            } else {
                // Walls: grey brick in running bond, high windows above.
                const float rows = 26.0f;
                const float row = v * rows;
                const int rowIndex = int(row);
                const float shift = (rowIndex & 1) ? 0.5f : 0.0f;
                const float column = u * rows * 0.5f + shift;
                const int columnIndex = int(std::floor(column));
                const bool mortar = std::fmod(row, 1.0f) < 0.12f
                        || std::fmod(column, 1.0f) < 0.06f;
                const float brick = 0.44f + 0.07f * noise(columnIndex, rowIndex, quint32(face) * 97)
                        + 0.02f * noise(x, y, 31);
                r = g = b = mortar ? 0.60f + 0.02f * noise(x, y, 37) : brick;
                r *= 1.02f;
                b *= 0.97f;
                if (v > 0.12f && v < 0.26f) {
                    const float pane = std::fmod(u * 5.0f, 1.0f);
                    if (pane > 0.15f && pane < 0.85f) {
                        const bool mullion = std::fmod(u * 20.0f, 1.0f) < 0.05f
                                || std::abs(v - 0.19f) < 0.004f;
                        r = mullion ? 0.25f : 0.80f;
                        g = mullion ? 0.25f : 0.86f;
                        b = mullion ? 0.25f : 0.92f;
                    }
                }
                if (v > 0.88f)
                    r = g = b = std::min(r, std::min(g, b)) * 0.8f;
            }
            unsigned char *pixel = &pixels[(size_t(y) * size + x) * 4];
            pixel[0] = byte(r);
            pixel[1] = byte(g);
            pixel[2] = byte(b);
            pixel[3] = 255;
        }
    }
    return pixels;
}

const char *PreviewVertex = R"(#version 330 core
out vec2 vUv;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vUv = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Unfolded cross: +Y above, then -X, +Z, +X, -Z, then -Y below.
const char *PreviewFragment = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform samplerCube environmentMap;
void main() {
    vec2 cross = vec2(vUv.x * 4.0, (1.0 - vUv.y) * 3.0);
    ivec2 cell = ivec2(floor(cross));
    vec2 f = fract(cross) * 2.0 - 1.0;
    float s = f.x, t = f.y;
    vec3 direction;
    if (cell.y == 1) {
        if (cell.x == 0) direction = vec3(-1.0, -t, s);
        else if (cell.x == 1) direction = vec3(s, -t, 1.0);
        else if (cell.x == 2) direction = vec3(1.0, -t, -s);
        else direction = vec3(-s, -t, -1.0);
    } else if (cell.x == 1) {
        direction = cell.y == 0 ? vec3(s, 1.0, t) : vec3(s, -1.0, -t);
    } else {
        discard;
    }
    fragColor = vec4(textureLod(environmentMap, direction, 0.0).rgb, 1.0);
}
)";

}

EnvironmentMap::EnvironmentMap() = default;

EnvironmentMap::~EnvironmentMap() {
    if (context && context == QOpenGLContext::currentContext())
        release();
}

bool EnvironmentMap::ensure(int faceSize) {
    QOpenGLContext *current = QOpenGLContext::currentContext();
    if (current == nullptr || faceSize < 1)
        return false;
    if (cube != 0 && size == faceSize && context == current)
        return true;
    if (context == current)
        release();
    context = current;
    size = faceSize;
    QOpenGLExtraFunctions *f = current->extraFunctions();
    f->glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    f->glGenTextures(1, &cube);
    f->glActiveTexture(GL_TEXTURE0 + TextureUnit);
    f->glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
    for (int face = 0; face < FaceCount; ++face)
        f->glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA8, size, size, 0,
                        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    f->glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    f->glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    f->glActiveTexture(GL_TEXTURE0);

    f->glGenRenderbuffers(1, &depth);
    f->glBindRenderbuffer(GL_RENDERBUFFER, depth);
    f->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    f->glBindRenderbuffer(GL_RENDERBUFFER, 0);
    f->glGenFramebuffers(1, &framebuffer);
    nextFace = 0;
    facesReady = 0;
    std::fill(std::begin(faceUpdated), std::end(faceUpdated), false);
    return true;
}

void EnvironmentMap::faceView(int face, const float *eye, float *view) {
    face = std::clamp(face, 0, FaceCount - 1);
    float position[3] = {eye[0], eye[1], eye[2]};
    float target[3];
    float up[3] = {FaceDirections[face][1][0], FaceDirections[face][1][1],
                   FaceDirections[face][1][2]};
    for (int i = 0; i < 3; ++i)
        target[i] = eye[i] + FaceDirections[face][0][i];
    Mat4::lookAt(view, position, target, up);
}

void EnvironmentMap::faceProjection(float nearPlane, float farPlane, float *projection) {
    Mat4::perspective(projection, float(M_PI / 2.0), 1.0f, nearPlane, farPlane);
}

QVector<int> EnvironmentMap::nextFaces(int count) {
    QVector<int> faces;
    count = std::clamp(count, 0, FaceCount);
    for (int i = 0; i < count; ++i) {
        faces.append(nextFace);
        nextFace = (nextFace + 1) % FaceCount;
    }
    return faces;
}

void EnvironmentMap::beginFace(int face, const float *clearColor) {
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    f->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cube, 0);
    f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    f->glViewport(0, 0, size, size);
    f->glClearColor(clearColor[0], clearColor[1], clearColor[2], 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (face >= 0 && face < FaceCount && !faceUpdated[face]) {
        faceUpdated[face] = true;
        facesReady++;
    }
}

void EnvironmentMap::endFaces(unsigned int restoreFramebuffer) {
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    f->glBindFramebuffer(GL_FRAMEBUFFER, restoreFramebuffer);
    f->glActiveTexture(GL_TEXTURE0 + TextureUnit);
    f->glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
    f->glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    f->glActiveTexture(GL_TEXTURE0);
}

bool EnvironmentMap::fillWarehouse(int faceSize) {
    if (!ensure(faceSize))
        return false;
    QOpenGLExtraFunctions *f = context->extraFunctions();
    f->glActiveTexture(GL_TEXTURE0 + TextureUnit);
    f->glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (int face = 0; face < FaceCount; ++face) {
        const std::vector<unsigned char> pixels = warehouseFace(face, size);
        f->glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, size, size,
                           GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (!faceUpdated[face]) {
            faceUpdated[face] = true;
            facesReady++;
        }
    }
    f->glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    f->glActiveTexture(GL_TEXTURE0);
    return true;
}

void EnvironmentMap::bind() {
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    f->glActiveTexture(GL_TEXTURE0 + TextureUnit);
    f->glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
    f->glActiveTexture(GL_TEXTURE0);
}

void EnvironmentMap::drawPreview(int x, int y, int cellSize) {
    if (cube == 0 || cellSize < 1)
        return;
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    if (preview == nullptr) {
        preview = new QOpenGLShaderProgram();
        if (!preview->addShaderFromSourceCode(QOpenGLShader::Vertex, PreviewVertex)
                || !preview->addShaderFromSourceCode(QOpenGLShader::Fragment, PreviewFragment)
                || !preview->link()) {
            qWarning() << "Environment map preview shader failed" << preview->log();
            delete preview;
            preview = nullptr;
            return;
        }
        f->glGenVertexArrays(1, &previewArray);
    }
    GLint viewport[4];
    f->glGetIntegerv(GL_VIEWPORT, viewport);
    const GLboolean depthTest = f->glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blend = f->glIsEnabled(GL_BLEND);
    f->glDisable(GL_DEPTH_TEST);
    f->glDisable(GL_BLEND);
    f->glViewport(x, y, cellSize * 4, cellSize * 3);
    preview->bind();
    preview->setUniformValue("environmentMap", TextureUnit);
    bind();
    f->glBindVertexArray(previewArray);
    f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    f->glBindVertexArray(0);
    preview->release();
    f->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (depthTest)
        f->glEnable(GL_DEPTH_TEST);
    if (blend)
        f->glEnable(GL_BLEND);
}

void EnvironmentMap::release() {
    QOpenGLContext *current = QOpenGLContext::currentContext();
    if (current != nullptr && context == current) {
        QOpenGLExtraFunctions *f = current->extraFunctions();
        if (cube != 0)
            f->glDeleteTextures(1, &cube);
        if (framebuffer != 0)
            f->glDeleteFramebuffers(1, &framebuffer);
        if (depth != 0)
            f->glDeleteRenderbuffers(1, &depth);
        if (previewArray != 0)
            f->glDeleteVertexArrays(1, &previewArray);
    }
    delete preview;
    preview = nullptr;
    cube = framebuffer = depth = previewArray = 0;
    size = 0;
    facesReady = 0;
    std::fill(std::begin(faceUpdated), std::end(faceUpdated), false);
    context.clear();
}
