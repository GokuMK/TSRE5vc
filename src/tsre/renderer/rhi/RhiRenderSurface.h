/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RHIRENDERSURFACE_H
#define RHIRENDERSURFACE_H

#include <QSize>
#include <memory>
#include <tsre/renderer/RenderSurface.h>

class QRhiCommandBuffer;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiRenderTarget;
class QRhiSwapChain;
class QRhiTexture;
class QRhiTextureRenderTarget;
class RhiContext;
class RhiWindow;

// A QWindow with a QRhi swapchain, embedded in the host widget. Input
// reaching the window goes to the host.
class RhiRenderSurface : public RenderSurface {
public:
    RhiRenderSurface(QWidget *host, RenderSurfaceClient *client, RhiContext *context);
    ~RhiRenderSurface() override;

    Backend backend() const override { return Rhi; }
    Renderer *createRenderer() override;
    QWidget *widget() override { return container; }
    void requestUpdate() override;
    void makeCurrent() override;
    void doneCurrent() override {}
    bool isValid() const override;
    QString graphicsInfo() override;
    QImage grabFramebuffer() override;
    unsigned int defaultFramebufferObject() const override { return 0; }
    QPaintDevice *overlayPaintDevice() override { return nullptr; }
    void detachClient() override { client = nullptr; }

    // The frame being painted: its command buffer and final target. The
    // target has colour and depth; offscreen frames (grabs) read it back.
    struct Frame {
        QRhiCommandBuffer *commandBuffer = nullptr;
        QRhiRenderTarget *target = nullptr;
        QRhiRenderPassDescriptor *passDescriptor = nullptr;
        QSize pixelSize;
        bool offscreen = false;
    };
    const Frame &frame() const { return current; }
    // Counts frames this surface started (on screen and grabs), so a
    // renderer knows when per-frame storage can be reused.
    quint64 frameSerial() const { return serial; }
    RhiContext *context() const { return rhiContext; }
    QWidget *host() const { return hostWidget; }

    // From the window.
    void exposed();
    void render();
    void releaseSwapChain();

private:
    void initialize();
    bool ensureSwapChain();
    QWidget *hostWidget;
    RenderSurfaceClient *client;
    RhiContext *rhiContext;
    RhiWindow *window = nullptr;
    QWidget *container = nullptr;
    std::unique_ptr<QRhiSwapChain> swapChain;
    std::unique_ptr<QRhiRenderBuffer> depthStencil;
    std::unique_ptr<QRhiRenderPassDescriptor> passDescriptor;
    // Offscreen frame for grabs.
    std::unique_ptr<QRhiTexture> grabColor;
    std::unique_ptr<QRhiRenderBuffer> grabDepth;
    std::unique_ptr<QRhiTextureRenderTarget> grabTarget;
    std::unique_ptr<QRhiRenderPassDescriptor> grabPassDescriptor;
    bool initialized = false;
    bool swapChainReady = false;
    quint64 serial = 0;
    Frame current;
};

#endif
