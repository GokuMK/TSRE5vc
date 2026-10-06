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
#include <QPointer>
#include <algorithm>
#include <cmath>
#include <vector>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/renderer/Renderer.h>

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

// Convolves the cube with the GGX lobe of a roughness for one face of one
// level: importance sampling, each sample read from the source mip whose
// texels match its footprint, averaged in linear colour.
const char *PrefilterFragment = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform samplerCube environmentSource;
uniform int face;
uniform float roughness;
uniform float sourceSize;
const int Samples = 32;
const float Pi = 3.14159265;

vec3 faceDirection(int face, vec2 uv) {
    float s = uv.x * 2.0 - 1.0, t = uv.y * 2.0 - 1.0;
    if (face == 0) return vec3(1.0, -t, -s);
    if (face == 1) return vec3(-1.0, -t, s);
    if (face == 2) return vec3(s, 1.0, t);
    if (face == 3) return vec3(s, -1.0, -t);
    if (face == 4) return vec3(s, -t, 1.0);
    return vec3(-s, -t, -1.0);
}

vec2 hammersley(uint i) {
    uint b = i;
    b = (b << 16u) | (b >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
    return vec2(float(i) / float(Samples), float(b) * 2.3283064365386963e-10);
}

void main() {
    vec3 n = normalize(faceDirection(face, vUv));
    if (roughness < 1e-3) {
        fragColor = vec4(textureLod(environmentSource, n, 0.0).rgb, 1.0);
        return;
    }
    float a = roughness * roughness;
    vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tx = normalize(cross(up, n));
    vec3 ty = cross(n, tx);
    float texelSolidAngle = 4.0 * Pi / (6.0 * sourceSize * sourceSize);
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (int i = 0; i < Samples; ++i) {
        vec2 xi = hammersley(uint(i));
        float phi = 2.0 * Pi * xi.x;
        float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        vec3 h = normalize(tx * cos(phi) * sinTheta + ty * sin(phi) * sinTheta + n * cosTheta);
        vec3 l = normalize(2.0 * dot(n, h) * h - n);
        float nDotL = dot(n, l);
        if (nDotL <= 0.0)
            continue;
        float nDotH = max(dot(n, h), 0.0);
        float d = nDotH * nDotH * (a * a - 1.0) + 1.0;
        float pdf = a * a / (Pi * d * d) * 0.25 + 1e-4;
        float sampleSolidAngle = 1.0 / (float(Samples) * pdf);
        float lod = max(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0);
        sum += pow(textureLod(environmentSource, l, lod).rgb, vec3(2.2)) * nDotL;
        weight += nDotL;
    }
    fragColor = vec4(pow(sum / max(weight, 1e-4), vec3(1.0 / 2.2)), 1.0);
}
)";

}

// u runs right, v runs down, both 0..1.
QByteArray EnvironmentMap::warehouseFace(int face, int size) {
    QByteArray pixels(qsizetype(size) * size * 4, '\0');
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
                // A dark painted band along the lower wall and an open loading
                // door with daylight outside: surfaces looking sideways, such
                // as car bodies, reflect something at their own height.
                if (v > 0.62f && v <= 0.88f)
                    r = g = b = 0.22f + 0.015f * noise(x / 2, y / 2, 41);
                const float doorCenter = face == 0 ? 0.5f : face == 1 ? 0.36f
                        : face == 4 ? 0.62f : 0.45f;
                const float doorU = std::abs(u - doorCenter);
                if (doorU < 0.16f && v > 0.34f && v <= 0.88f) {
                    if (doorU > 0.145f || v < 0.355f) {
                        r = g = b = 0.12f;
                    } else if (v < 0.52f) {
                        // Sky, paler towards the horizon.
                        const float t = (v - 0.355f) / (0.52f - 0.355f);
                        r = 0.78f + 0.16f * t; g = 0.86f + 0.10f * t; b = 0.98f;
                    } else {
                        // Yard outside: asphalt and a lighter far edge.
                        const float t = (v - 0.52f) / (0.88f - 0.52f);
                        r = g = b = 0.62f - 0.22f * t;
                    }
                }
                if (v > 0.88f)
                    r = g = b = std::min(r, std::min(g, b)) * 0.8f;
            }
            unsigned char *pixel = reinterpret_cast<unsigned char *>(pixels.data())
                    + (qsizetype(y) * size + x) * 4;
            pixel[0] = byte(r);
            pixel[1] = byte(g);
            pixel[2] = byte(b);
            pixel[3] = 255;
        }
    }
    return pixels;
}

namespace {

// The cubes in OpenGL objects of the context current at create().
class GlEnvironmentStorage : public EnvironmentMap::Storage {
public:
    ~GlEnvironmentStorage() override {
        if (context && context == QOpenGLContext::currentContext())
            release();
    }
    bool ready(int faceSize) const override {
        return cube != 0 && size == faceSize && context == QOpenGLContext::currentContext();
    }
    bool create(int faceSize, int levelCount) override;
    void beginFace(int face, const float *clearColor) override;
    void endFaces(const QVector<int> &prefilterFaces) override;
    void uploadFaces(const QByteArray *faces) override;
    void bind(bool prefilteredCube) override {
        QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
        f->glActiveTexture(GL_TEXTURE0 + EnvironmentMap::TextureUnit);
        f->glBindTexture(GL_TEXTURE_CUBE_MAP, prefilteredCube ? prefiltered : cube);
        f->glActiveTexture(GL_TEXTURE0);
    }
    void unbind() override {
        QOpenGLContext *current = QOpenGLContext::currentContext();
        if (current == nullptr)
            return;
        QOpenGLExtraFunctions *f = current->extraFunctions();
        f->glActiveTexture(GL_TEXTURE0 + EnvironmentMap::TextureUnit);
        f->glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        f->glActiveTexture(GL_TEXTURE0);
    }
    void drawPreview(int x, int y, int cellSize) override;
    void release() override;
    unsigned int texture() const override { return cube; }
    unsigned int prefilteredTexture() const override { return prefiltered; }

private:
    QPointer<QOpenGLContext> context;
    unsigned int cube = 0;
    unsigned int framebuffer = 0;
    unsigned int depth = 0;
    unsigned int previewArray = 0;
    QOpenGLShaderProgram *preview = nullptr;
    // The cube convolved with the GGX lobe of each level's roughness.
    unsigned int prefiltered = 0;
    QOpenGLShaderProgram *prefilterProgram = nullptr;
    int size = 0;
    int levels = 0;
    // The framebuffer bound before the first face, bound again at the end.
    GLint restoreFramebuffer = 0;
    bool drawingFaces = false;
    // Prefilters the given faces at every level; the raw cube must have its
    // mipmaps.
    void prefilter(const QVector<int> &faces);
    bool ensurePrograms();
};

}

const char *EnvironmentMap::fullScreenVertexShader() { return PreviewVertex; }
const char *EnvironmentMap::prefilterFragmentShader() { return PrefilterFragment; }
const char *EnvironmentMap::previewFragmentShader() { return PreviewFragment; }

EnvironmentMap::Storage *EnvironmentMap::createOpenGlStorage() {
    return new GlEnvironmentStorage();
}

bool GlEnvironmentStorage::create(int faceSize, int levelCount) {
    QOpenGLContext *current = QOpenGLContext::currentContext();
    if (current == nullptr || faceSize < 1)
        return false;
    if (context == current)
        release();
    context = current;
    size = faceSize;
    levels = levelCount;
    QOpenGLExtraFunctions *f = current->extraFunctions();
    f->glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    f->glGenTextures(1, &cube);
    f->glActiveTexture(GL_TEXTURE0 + EnvironmentMap::TextureUnit);
    f->glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
    for (int face = 0; face < EnvironmentMap::FaceCount; ++face)
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
    f->glGenTextures(1, &prefiltered);
    f->glActiveTexture(GL_TEXTURE0 + EnvironmentMap::TextureUnit);
    f->glBindTexture(GL_TEXTURE_CUBE_MAP, prefiltered);
    for (int level = 0; level < levels; ++level)
        for (int face = 0; face < EnvironmentMap::FaceCount; ++face)
            f->glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGBA8,
                            std::max(size >> level, 1), std::max(size >> level, 1), 0,
                            GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, std::max(levels - 1, 0));
    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    f->glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    f->glActiveTexture(GL_TEXTURE0);
    return true;
}

void GlEnvironmentStorage::beginFace(int face, const float *clearColor) {
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    if (!drawingFaces) {
        f->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &restoreFramebuffer);
        drawingFaces = true;
    }
    f->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cube, 0);
    f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    f->glViewport(0, 0, size, size);
    f->glClearColor(clearColor[0], clearColor[1], clearColor[2], 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GlEnvironmentStorage::endFaces(const QVector<int> &prefilterFaces) {
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    if (!drawingFaces)
        f->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &restoreFramebuffer);
    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    f->glActiveTexture(GL_TEXTURE0 + EnvironmentMap::TextureUnit);
    f->glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
    f->glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    f->glActiveTexture(GL_TEXTURE0);
    prefilter(prefilterFaces);
    f->glBindFramebuffer(GL_FRAMEBUFFER, GLuint(restoreFramebuffer));
    drawingFaces = false;
}

void GlEnvironmentStorage::uploadFaces(const QByteArray *faces) {
    QOpenGLExtraFunctions *f = context->extraFunctions();
    f->glActiveTexture(GL_TEXTURE0 + EnvironmentMap::TextureUnit);
    f->glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (int face = 0; face < EnvironmentMap::FaceCount; ++face)
        f->glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, size, size,
                           GL_RGBA, GL_UNSIGNED_BYTE, faces[face].constData());
    f->glActiveTexture(GL_TEXTURE0);
}

bool GlEnvironmentStorage::ensurePrograms() {
    if (preview != nullptr && prefilterProgram != nullptr)
        return true;
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    auto build = [](const char *fragment, const char *name) -> QOpenGLShaderProgram * {
        auto *program = new QOpenGLShaderProgram();
        if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, PreviewVertex)
                || !program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment)
                || !program->link()) {
            qWarning() << "Environment map" << name << "shader failed" << program->log();
            delete program;
            return nullptr;
        }
        return program;
    };
    if (preview == nullptr)
        preview = build(PreviewFragment, "preview");
    if (prefilterProgram == nullptr)
        prefilterProgram = build(PrefilterFragment, "prefilter");
    if (previewArray == 0)
        f->glGenVertexArrays(1, &previewArray);
    return preview != nullptr && prefilterProgram != nullptr;
}

void GlEnvironmentStorage::prefilter(const QVector<int> &faces) {
    if (faces.isEmpty() || prefiltered == 0 || !ensurePrograms())
        return;
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    GLint viewport[4];
    f->glGetIntegerv(GL_VIEWPORT, viewport);
    const GLboolean depthTest = f->glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blend = f->glIsEnabled(GL_BLEND);
    const GLboolean cull = f->glIsEnabled(GL_CULL_FACE);
    f->glDisable(GL_DEPTH_TEST);
    f->glDisable(GL_BLEND);
    f->glDisable(GL_CULL_FACE);
    f->glActiveTexture(GL_TEXTURE0 + EnvironmentMap::TextureUnit);
    f->glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
    prefilterProgram->bind();
    prefilterProgram->setUniformValue("environmentSource", EnvironmentMap::TextureUnit);
    prefilterProgram->setUniformValue("sourceSize", float(size));
    f->glBindVertexArray(previewArray);
    for (int level = 0; level < levels; ++level) {
        const int side = std::max(size >> level, 1);
        f->glViewport(0, 0, side, side);
        prefilterProgram->setUniformValue("roughness", levels > 1 ? float(level) / (levels - 1) : 0.0f);
        for (int face : faces) {
            f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, prefiltered, level);
            prefilterProgram->setUniformValue("face", face);
            f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }
    f->glBindVertexArray(0);
    prefilterProgram->release();
    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    f->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (depthTest)
        f->glEnable(GL_DEPTH_TEST);
    if (blend)
        f->glEnable(GL_BLEND);
    if (cull)
        f->glEnable(GL_CULL_FACE);
}

void GlEnvironmentStorage::drawPreview(int x, int y, int cellSize) {
    if (cube == 0 || cellSize < 1)
        return;
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    if (!ensurePrograms())
        return;
    GLint viewport[4];
    f->glGetIntegerv(GL_VIEWPORT, viewport);
    const GLboolean depthTest = f->glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blend = f->glIsEnabled(GL_BLEND);
    f->glDisable(GL_DEPTH_TEST);
    f->glDisable(GL_BLEND);
    f->glViewport(x, y, cellSize * 4, cellSize * 3);
    preview->bind();
    preview->setUniformValue("environmentMap", EnvironmentMap::TextureUnit);
    // The rendered faces, not the prefiltered ones.
    f->glActiveTexture(GL_TEXTURE0 + EnvironmentMap::TextureUnit);
    f->glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
    f->glActiveTexture(GL_TEXTURE0);
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

void GlEnvironmentStorage::release() {
    QOpenGLContext *current = QOpenGLContext::currentContext();
    if (current != nullptr && context == current) {
        QOpenGLExtraFunctions *f = current->extraFunctions();
        if (cube != 0)
            f->glDeleteTextures(1, &cube);
        if (prefiltered != 0)
            f->glDeleteTextures(1, &prefiltered);
        if (framebuffer != 0)
            f->glDeleteFramebuffers(1, &framebuffer);
        if (depth != 0)
            f->glDeleteRenderbuffers(1, &depth);
        if (previewArray != 0)
            f->glDeleteVertexArrays(1, &previewArray);
    }
    delete preview;
    preview = nullptr;
    delete prefilterProgram;
    prefilterProgram = nullptr;
    cube = framebuffer = depth = previewArray = prefiltered = 0;
    size = levels = 0;
    drawingFaces = false;
    context.clear();
}

EnvironmentMap::EnvironmentMap(Renderer *renderer) : renderer(renderer) {}

EnvironmentMap::~EnvironmentMap() = default;

void EnvironmentMap::resetFaces() {
    pendingFaces.clear();
    prefilteredOnce = false;
    nextFace = 0;
    facesReady = 0;
    std::fill(std::begin(faceUpdated), std::end(faceUpdated), false);
}

bool EnvironmentMap::ensure(int faceSize) {
    if (faceSize < 1)
        return false;
    if (!storage)
        storage.reset(renderer != nullptr ? renderer->createEnvironmentStorage()
                                          : createOpenGlStorage());
    if (!storage)
        return false;
    if (storage->ready(faceSize))
        return true;
    resetFaces();
    size = 0;
    if (!storage->create(faceSize, levelsFor(faceSize)))
        return false;
    size = faceSize;
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
    if (!storage || size == 0 || face < 0 || face >= FaceCount)
        return;
    storage->beginFace(face, clearColor);
    if (!faceUpdated[face]) {
        faceUpdated[face] = true;
        facesReady++;
    }
    if (!pendingFaces.contains(face))
        pendingFaces.append(face);
}

void EnvironmentMap::endFaces() {
    if (!storage || size == 0)
        return;
    // Until every face exists the convolution would blur in empty faces;
    // after that, the faces drawn this frame.
    QVector<int> prefilterFaces;
    if (complete()) {
        prefilterFaces = prefilteredOnce ? pendingFaces : QVector<int>({0, 1, 2, 3, 4, 5});
        prefilteredOnce = true;
    }
    storage->endFaces(prefilterFaces);
    pendingFaces.clear();
}

bool EnvironmentMap::fillWarehouse(int faceSize) {
    if (!ensure(faceSize))
        return false;
    QByteArray faces[FaceCount];
    for (int face = 0; face < FaceCount; ++face) {
        faces[face] = warehouseFace(face, size);
        if (!faceUpdated[face]) {
            faceUpdated[face] = true;
            facesReady++;
        }
    }
    storage->uploadFaces(faces);
    storage->endFaces({0, 1, 2, 3, 4, 5});
    prefilteredOnce = true;
    pendingFaces.clear();
    return true;
}

int EnvironmentMap::levelsFor(int faceSize) {
    int count = 0;
    for (int side = faceSize; side >= 4; side >>= 1)
        ++count;
    return count;
}

int EnvironmentMap::levels() const {
    return storage && size > 0 ? levelsFor(size) : 0;
}

void EnvironmentMap::unbind() {
    if (storage)
        storage->unbind();
}

void EnvironmentMap::bind() {
    if (storage && size > 0)
        storage->bind(prefilteredOnce);
}

void EnvironmentMap::drawPreview(int x, int y, int cellSize) {
    if (storage && size > 0 && cellSize > 0)
        storage->drawPreview(x, y, cellSize);
}

void EnvironmentMap::release() {
    if (storage)
        storage->release();
    storage.reset();
    resetFaces();
    size = 0;
}
