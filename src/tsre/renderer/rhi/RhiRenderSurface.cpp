/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "RhiRenderSurface.h"
#include "RhiContext.h"
#include "RhiRenderer.h"
#include <QCoreApplication>
#include <QPlatformSurfaceEvent>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <rhi/qrhi.h>

class RhiWindow : public QWindow {
public:
    RhiWindow(RhiRenderSurface *surface, RhiContext *context) : surface(surface) {
        setSurfaceType(context->surfaceType());
#if QT_CONFIG(vulkan)
        if (context->vulkanInstance() != nullptr)
            setVulkanInstance(context->vulkanInstance());
#endif
    }

protected:
    void exposeEvent(QExposeEvent *) override {
        if (isExposed())
            surface->exposed();
    }
    bool event(QEvent *event) override {
        switch (event->type()) {
        case QEvent::UpdateRequest:
            surface->render();
            return true;
        case QEvent::PlatformSurface:
            if (static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType()
                    == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
                surface->releaseSwapChain();
            break;
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ContextMenu:
            // The host widget handles input; the window fills it, so window
            // positions are host positions.
            QCoreApplication::sendEvent(surface->host(), event);
            return true;
        default:
            break;
        }
        return QWindow::event(event);
    }

private:
    RhiRenderSurface *surface;
};

RhiRenderSurface::RhiRenderSurface(QWidget *host, RenderSurfaceClient *client, RhiContext *context)
    : hostWidget(host), client(client), rhiContext(context) {
    window = new RhiWindow(this, context);
    container = QWidget::createWindowContainer(window, host);
    container->setFocusPolicy(Qt::NoFocus);
    QVBoxLayout *layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(container);
}

RhiRenderSurface::~RhiRenderSurface() {
    releaseSwapChain();
    grabTarget.reset();
    grabPassDescriptor.reset();
    grabDepth.reset();
    grabColor.reset();
}

Renderer *RhiRenderSurface::createRenderer() {
    return new RhiRenderer(rhiContext);
}

void RhiRenderSurface::requestUpdate() {
    if (window != nullptr)
        window->requestUpdate();
}

void RhiRenderSurface::makeCurrent() {
    if (rhiContext->rhi() != nullptr)
        rhiContext->rhi()->makeThreadLocalNativeContextCurrent();
}

bool RhiRenderSurface::isValid() const {
    return rhiContext->rhi() != nullptr;
}

QString RhiRenderSurface::graphicsInfo() {
    return rhiContext->info();
}

void RhiRenderSurface::initialize() {
    if (initialized)
        return;
    initialized = true;
    if (client != nullptr)
        client->surfaceInitialize();
}

bool RhiRenderSurface::ensureSwapChain() {
    QRhi *rhi = rhiContext->rhi();
    if (rhi == nullptr || !window->isExposed())
        return false;
    if (!swapChain) {
        swapChain.reset(rhi->newSwapChain());
        swapChain->setWindow(window);
        depthStencil.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, QSize(), 1,
                                                QRhiRenderBuffer::UsedWithSwapChainOnly));
        swapChain->setDepthStencil(depthStencil.get());
        passDescriptor.reset(swapChain->newCompatibleRenderPassDescriptor());
        swapChain->setRenderPassDescriptor(passDescriptor.get());
    }
    if (!swapChainReady || swapChain->currentPixelSize() != swapChain->surfacePixelSize()) {
        swapChainReady = swapChain->createOrResize();
        if (swapChainReady && client != nullptr)
            client->surfaceResize(window->width(), window->height());
    }
    return swapChainReady;
}

void RhiRenderSurface::releaseSwapChain() {
    swapChain.reset();
    passDescriptor.reset();
    depthStencil.reset();
    swapChainReady = false;
}

void RhiRenderSurface::exposed() {
    initialize();
    if (ensureSwapChain())
        render();
}

void RhiRenderSurface::render() {
    QRhi *rhi = rhiContext->rhi();
    if (rhi == nullptr || client == nullptr || !ensureSwapChain())
        return;
    QRhi::FrameOpResult result = rhi->beginFrame(swapChain.get());
    if (result == QRhi::FrameOpSwapChainOutOfDate) {
        swapChainReady = false;
        if (!ensureSwapChain())
            return;
        result = rhi->beginFrame(swapChain.get());
    }
    if (result != QRhi::FrameOpSuccess)
        return;
    current.commandBuffer = swapChain->currentFrameCommandBuffer();
    current.target = swapChain->currentFrameRenderTarget();
    current.passDescriptor = passDescriptor.get();
    current.pixelSize = swapChain->currentPixelSize();
    current.offscreen = false;
    ++serial;
    client->surfacePaint();
    current = Frame();
    rhi->endFrame(swapChain.get());
}

QImage RhiRenderSurface::grabFramebuffer() {
    QRhi *rhi = rhiContext->rhi();
    if (rhi == nullptr)
        return QImage();
    initialize();
    if (client == nullptr)
        return QImage();
    const QSize size = (QSizeF(hostWidget->size()) * hostWidget->devicePixelRatioF()).toSize()
            .expandedTo(QSize(1, 1));
    if (!grabColor || grabColor->pixelSize() != size) {
        grabTarget.reset();
        grabPassDescriptor.reset();
        grabColor.reset(rhi->newTexture(QRhiTexture::RGBA8, size, 1,
                                        QRhiTexture::RenderTarget
                                        | QRhiTexture::UsedAsTransferSource));
        grabDepth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size));
        if (!grabColor->create() || !grabDepth->create())
            return QImage();
        QRhiTextureRenderTargetDescription description{QRhiColorAttachment(grabColor.get())};
        description.setDepthStencilBuffer(grabDepth.get());
        grabTarget.reset(rhi->newTextureRenderTarget(description));
        grabPassDescriptor.reset(grabTarget->newCompatibleRenderPassDescriptor());
        grabTarget->setRenderPassDescriptor(grabPassDescriptor.get());
        if (!grabTarget->create())
            return QImage();
        client->surfaceResize(hostWidget->width(), hostWidget->height());
    }
    QRhiCommandBuffer *commandBuffer = nullptr;
    if (rhi->beginOffscreenFrame(&commandBuffer) != QRhi::FrameOpSuccess)
        return QImage();
    current.commandBuffer = commandBuffer;
    current.target = grabTarget.get();
    current.passDescriptor = grabPassDescriptor.get();
    current.pixelSize = size;
    current.offscreen = true;
    ++serial;
    client->surfacePaint();
    current = Frame();
    QRhiReadbackResult readback;
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->readBackTexture(QRhiReadbackDescription(grabColor.get()), &readback);
    commandBuffer->resourceUpdate(batch);
    rhi->endOffscreenFrame();
    if (readback.data.isEmpty())
        return QImage();
    QImage image(reinterpret_cast<const uchar *>(readback.data.constData()),
                 readback.pixelSize.width(), readback.pixelSize.height(),
                 QImage::Format_RGBA8888);
    image = image.copy();
    if (rhi->isYUpInFramebuffer())
        image.mirror(false, true);
    return image;
}

RenderSurface *createRhiRenderSurface(QWidget *host, RenderSurfaceClient *client) {
    RhiContext *context = RhiContext::instance();
    if (context == nullptr || context->rhi() == nullptr)
        return nullptr;
    return new RhiRenderSurface(host, client, context);
}
