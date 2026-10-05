/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PlanarReflection.h"
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <algorithm>
#include <cmath>

PlanarReflection::~PlanarReflection() {
    if (context != nullptr && context == QOpenGLContext::currentContext())
        release();
}

bool PlanarReflection::ensure(int width, int height) {
    QOpenGLContext *current = QOpenGLContext::currentContext();
    if (current == nullptr || width < 1 || height < 1)
        return false;
    if (texture != 0 && context == current && width == targetWidth && height == targetHeight)
        return true;
    if (context == current)
        release();
    texture = depth = framebuffer = 0;
    context = current;
    targetWidth = width;
    targetHeight = height;
    QOpenGLExtraFunctions *f = current->extraFunctions();
    f->glGenTextures(1, &texture);
    f->glActiveTexture(GL_TEXTURE0 + TextureUnit);
    f->glBindTexture(GL_TEXTURE_2D, texture);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                    GL_UNSIGNED_BYTE, nullptr);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glGenerateMipmap(GL_TEXTURE_2D);
    f->glBindTexture(GL_TEXTURE_2D, 0);
    f->glActiveTexture(GL_TEXTURE0);

    f->glGenRenderbuffers(1, &depth);
    f->glBindRenderbuffer(GL_RENDERBUFFER, depth);
    f->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    f->glBindRenderbuffer(GL_RENDERBUFFER, 0);

    GLint previous = 0;
    f->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    f->glGenFramebuffers(1, &framebuffer);
    f->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    const bool complete = f->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    f->glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previous));
    if (!complete) {
        qWarning() << "Water reflection framebuffer is incomplete";
        release();
        return false;
    }
    return true;
}

void PlanarReflection::begin(const float *clearColor) {
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    f->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    f->glViewport(0, 0, targetWidth, targetHeight);
    f->glClearColor(clearColor[0], clearColor[1], clearColor[2], 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void PlanarReflection::end(unsigned int restoreFramebuffer) {
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    f->glBindFramebuffer(GL_FRAMEBUFFER, restoreFramebuffer);
    f->glActiveTexture(GL_TEXTURE0 + TextureUnit);
    f->glBindTexture(GL_TEXTURE_2D, texture);
    f->glGenerateMipmap(GL_TEXTURE_2D);
    f->glActiveTexture(GL_TEXTURE0);
}

void PlanarReflection::bind() const {
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    f->glActiveTexture(GL_TEXTURE0 + TextureUnit);
    f->glBindTexture(GL_TEXTURE_2D, texture);
    f->glActiveTexture(GL_TEXTURE0);
}

void PlanarReflection::unbind() {
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    f->glActiveTexture(GL_TEXTURE0 + TextureUnit);
    f->glBindTexture(GL_TEXTURE_2D, 0);
    f->glActiveTexture(GL_TEXTURE0);
}

int PlanarReflection::levels() const {
    if (texture == 0)
        return 0;
    return 1 + int(std::floor(std::log2(double(std::max(targetWidth, targetHeight)))));
}

bool PlanarReflection::fitPlane(const std::vector<float> &spheres, const float *eye,
                                float *plane) {
    // Weighted least squares for y = a dx + b dz + c around the eye.
    double m[3][3] = {}, r[3] = {}, total = 0.0;
    for (size_t i = 0; i + 3 < spheres.size(); i += 4) {
        const double dx = spheres[i] - eye[0], dz = spheres[i + 2] - eye[2], y = spheres[i + 1];
        const double distance = std::sqrt(dx * dx + dz * dz) / 150.0;
        const double w = 1.0 / (1.0 + distance * distance);
        const double row[3] = {dx, dz, 1.0};
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < 3; ++b)
                m[a][b] += w * row[a] * row[b];
            r[a] += w * row[a] * y;
        }
        total += w;
    }
    if (total <= 0.0)
        return false;
    // Patches in one row cannot fix the slope across it: keep it level there.
    m[0][0] += total * 25.0;
    m[1][1] += total * 25.0;
    auto det3 = [](const double k[3][3]) {
        return k[0][0] * (k[1][1] * k[2][2] - k[1][2] * k[2][1])
                - k[0][1] * (k[1][0] * k[2][2] - k[1][2] * k[2][0])
                + k[0][2] * (k[1][0] * k[2][1] - k[1][1] * k[2][0]);
    };
    const double det = det3(m);
    if (std::abs(det) < 1e-12)
        return false;
    double solution[3];
    for (int column = 0; column < 3; ++column) {
        double k[3][3];
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                k[a][b] = b == column ? r[a] : m[a][b];
        solution[column] = det3(k) / det;
    }
    const double slopeX = std::clamp(solution[0], -0.1, 0.1);
    const double slopeZ = std::clamp(solution[1], -0.1, 0.1);
    // y - a (x - ex) - b (z - ez) - c = 0, normalised.
    const double length = std::sqrt(slopeX * slopeX + 1.0 + slopeZ * slopeZ);
    plane[0] = float(-slopeX / length);
    plane[1] = float(1.0 / length);
    plane[2] = float(-slopeZ / length);
    plane[3] = float((slopeX * eye[0] + slopeZ * eye[2] - solution[2]) / length);
    return true;
}

void PlanarReflection::mirrorMatrix(const float *plane, float *out) {
    // p' = p - 2 (n . p + d) n
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row)
            out[column * 4 + row] = (row == column ? 1.0f : 0.0f) - 2.0f * plane[row] * plane[column];
        out[column * 4 + 3] = 0.0f;
    }
    for (int row = 0; row < 3; ++row)
        out[12 + row] = -2.0f * plane[3] * plane[row];
    out[15] = 1.0f;
}

void PlanarReflection::release() {
    QOpenGLContext *current = QOpenGLContext::currentContext();
    if (current != nullptr && context == current) {
        QOpenGLExtraFunctions *f = current->extraFunctions();
        if (texture != 0)
            f->glDeleteTextures(1, &texture);
        if (depth != 0)
            f->glDeleteRenderbuffers(1, &depth);
        if (framebuffer != 0)
            f->glDeleteFramebuffers(1, &framebuffer);
    }
    texture = depth = framebuffer = 0;
    targetWidth = targetHeight = 0;
}
