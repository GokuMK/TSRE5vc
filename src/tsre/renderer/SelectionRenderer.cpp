/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "SelectionRenderer.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>

namespace {

void restoreCapability(QOpenGLFunctions *functions, unsigned int capability,
                       bool enabled){
    if(enabled)
        functions->glEnable(capability);
    else
        functions->glDisable(capability);
}

}

bool SelectionRenderer::begin(int width, int height){
    if(active){
        qWarning() << "Selection renderer begin called while already active";
        return false;
    }
    if(width <= 0 || height <= 0)
        return false;
    if(!resize(width, height))
        return false;

    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == nullptr)
        return false;
    QOpenGLFunctions *functions = context->functions();
    QOpenGLExtraFunctions *extra = context->extraFunctions();

    functions->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,
                             &previousDrawFramebuffer);
    functions->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,
                             &previousReadFramebuffer);
    functions->glGetIntegerv(GL_VIEWPORT, previousViewport);
    functions->glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
    functions->glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    previousBlend = functions->glIsEnabled(GL_BLEND);
    previousDither = functions->glIsEnabled(GL_DITHER);
    previousMultisample = functions->glIsEnabled(GL_MULTISAMPLE);
    previousDepthTest = functions->glIsEnabled(GL_DEPTH_TEST);
    previousScissorTest = functions->glIsEnabled(GL_SCISSOR_TEST);
#ifdef GL_FRAMEBUFFER_SRGB
    previousFramebufferSrgb = functions->glIsEnabled(GL_FRAMEBUFFER_SRGB);
#endif

    functions->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    functions->glViewport(0, 0, targetWidth, targetHeight);
    functions->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    functions->glDepthMask(GL_TRUE);
    functions->glEnable(GL_DEPTH_TEST);
    functions->glDisable(GL_BLEND);
    functions->glDisable(GL_DITHER);
    functions->glDisable(GL_MULTISAMPLE);
    functions->glDisable(GL_SCISSOR_TEST);
#ifdef GL_FRAMEBUFFER_SRGB
    functions->glDisable(GL_FRAMEBUFFER_SRGB);
#endif

    const unsigned int noSelection = 0;
    extra->glClearBufferuiv(GL_COLOR, 0, &noSelection);
    functions->glClear(GL_DEPTH_BUFFER_BIT);
    active = true;
    return true;
}

void SelectionRenderer::end(){
    if(!active)
        return;

    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == nullptr){
        active = false;
        return;
    }
    QOpenGLFunctions *functions = context->functions();

    functions->glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                                 previousDrawFramebuffer);
    functions->glBindFramebuffer(GL_READ_FRAMEBUFFER,
                                 previousReadFramebuffer);
    functions->glViewport(previousViewport[0], previousViewport[1],
                          previousViewport[2], previousViewport[3]);
    functions->glColorMask(previousColorMask[0], previousColorMask[1],
                           previousColorMask[2], previousColorMask[3]);
    functions->glDepthMask(previousDepthMask);
    restoreCapability(functions, GL_BLEND, previousBlend);
    restoreCapability(functions, GL_DITHER, previousDither);
    restoreCapability(functions, GL_MULTISAMPLE, previousMultisample);
    restoreCapability(functions, GL_DEPTH_TEST, previousDepthTest);
    restoreCapability(functions, GL_SCISSOR_TEST, previousScissorTest);
#ifdef GL_FRAMEBUFFER_SRGB
    restoreCapability(functions, GL_FRAMEBUFFER_SRGB, previousFramebufferSrgb);
#endif
    active = false;
}

quint32 SelectionRenderer::readPixel(int x, int y) const{
    if(!active || x < 0 || y < 0 || x >= targetWidth || y >= targetHeight)
        return 0;

    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == nullptr)
        return 0;

    quint32 selectionId = 0;
    context->functions()->glReadPixels(x, y, 1, 1, GL_RED_INTEGER,
                                      GL_UNSIGNED_INT, &selectionId);
    return selectionId;
}

void SelectionRenderer::release(){
    if(active)
        end();

    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == nullptr){
        framebuffer = 0;
        colorTexture = 0;
        depthRenderbuffer = 0;
        targetWidth = 0;
        targetHeight = 0;
        return;
    }
    QOpenGLFunctions *functions = context->functions();
    if(depthRenderbuffer != 0)
        functions->glDeleteRenderbuffers(1, &depthRenderbuffer);
    if(colorTexture != 0)
        functions->glDeleteTextures(1, &colorTexture);
    if(framebuffer != 0)
        functions->glDeleteFramebuffers(1, &framebuffer);
    framebuffer = 0;
    colorTexture = 0;
    depthRenderbuffer = 0;
    targetWidth = 0;
    targetHeight = 0;
}

bool SelectionRenderer::isActive() const{
    return active;
}

int SelectionRenderer::width() const{
    return targetWidth;
}

int SelectionRenderer::height() const{
    return targetHeight;
}

bool SelectionRenderer::resize(int width, int height){
    if(framebuffer != 0 && targetWidth == width && targetHeight == height)
        return true;

    QOpenGLContext *context = QOpenGLContext::currentContext();
    if(context == nullptr)
        return false;
    QOpenGLFunctions *functions = context->functions();
    QOpenGLExtraFunctions *extra = context->extraFunctions();

    int previousDrawFramebufferBinding = 0;
    int previousReadFramebufferBinding = 0;
    int previousTextureBinding = 0;
    int previousRenderbufferBinding = 0;
    functions->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,
                             &previousDrawFramebufferBinding);
    functions->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,
                             &previousReadFramebufferBinding);
    functions->glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    functions->glGetIntegerv(GL_RENDERBUFFER_BINDING,
                             &previousRenderbufferBinding);

    if(framebuffer == 0)
        functions->glGenFramebuffers(1, &framebuffer);
    if(colorTexture == 0)
        functions->glGenTextures(1, &colorTexture);
    if(depthRenderbuffer == 0)
        functions->glGenRenderbuffers(1, &depthRenderbuffer);

    functions->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    functions->glBindTexture(GL_TEXTURE_2D, colorTexture);
    functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    functions->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    functions->glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width, height, 0,
                            GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    functions->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_TEXTURE_2D, colorTexture, 0);

    functions->glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
    functions->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                                     width, height);
    functions->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                         GL_RENDERBUFFER, depthRenderbuffer);

    const unsigned int colorAttachment = GL_COLOR_ATTACHMENT0;
    extra->glDrawBuffers(1, &colorAttachment);
    extra->glReadBuffer(GL_COLOR_ATTACHMENT0);

    const unsigned int status = functions->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    bool storageVerified = false;
    if(status == GL_FRAMEBUFFER_COMPLETE){
        const unsigned int probeValue = 0x5aa55aa5u;
        unsigned int readValue = 0;
        extra->glClearBufferuiv(GL_COLOR, 0, &probeValue);
        functions->glReadPixels(0, 0, 1, 1, GL_RED_INTEGER,
                                GL_UNSIGNED_INT, &readValue);
        storageVerified = readValue == probeValue;
    }
    functions->glBindRenderbuffer(GL_RENDERBUFFER,
                                  previousRenderbufferBinding);
    functions->glBindTexture(GL_TEXTURE_2D, previousTextureBinding);
    functions->glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                                 previousDrawFramebufferBinding);
    functions->glBindFramebuffer(GL_READ_FRAMEBUFFER,
                                 previousReadFramebufferBinding);

    if(status != GL_FRAMEBUFFER_COMPLETE){
        qWarning() << "Selection framebuffer is incomplete:" << Qt::hex << status;
        release();
        return false;
    }
    if(!storageVerified){
        qWarning() << "Selection framebuffer integer storage/readback test failed";
        release();
        return false;
    }

    targetWidth = width;
    targetHeight = height;
    return true;
}
