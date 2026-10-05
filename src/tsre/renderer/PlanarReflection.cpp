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

void PlanarReflection::mirrorMatrix(float height, float *out) {
    std::fill(out, out + 16, 0.0f);
    out[0] = 1.0f;
    out[5] = -1.0f;
    out[10] = 1.0f;
    out[13] = 2.0f * height;
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
