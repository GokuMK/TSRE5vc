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
#include <QElapsedTimer>
#include <QPlatformSurfaceEvent>
#include <QSet>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <cstring>
#include <rhi/qrhi.h>

class RhiWindow : public QWindow {
public:
    RhiWindow(RhiRenderSurface *surface, RhiContext *context) : surface(surface) {
        setSurfaceType(context->surfaceType());
#if TSRE_RHI_VULKAN
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

namespace {
QSet<RhiRenderSurface *> &liveSurfaces() {
    static QSet<RhiRenderSurface *> surfaces;
    return surfaces;
}
}

RhiRenderSurface::RhiRenderSurface(QWidget *host, RenderSurfaceClient *client, RhiContext *context)
    : hostWidget(host), client(client), rhiContext(context) {
    liveSurfaces().insert(this);
    window = new RhiWindow(this, context);
    container = QWidget::createWindowContainer(window, host);
    container->setFocusPolicy(Qt::NoFocus);
    QVBoxLayout *layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(container);
}

RhiRenderSurface::~RhiRenderSurface() {
    liveSurfaces().remove(this);
    releaseResources();
}

void RhiRenderSurface::releaseResources() {
    releaseSwapChain();
    grabTarget.reset();
    grabPassDescriptor.reset();
    grabDepth.reset();
    grabColor.reset();
}

void RhiRenderSurface::releaseAll() {
    for (RhiRenderSurface *surface : std::as_const(liveSurfaces())) {
        surface->releaseResources();
        surface->client = nullptr;
    }
}

Renderer *RhiRenderSurface::createRenderer() {
    return new RhiRenderer(rhiContext);
}

void RhiRenderSurface::requestUpdate() {
    // Frames are paced by the widget's timer, as with QOpenGLWidget, whose
    // repaints are posted events too. QWindow::requestUpdate() would deliver
    // the frame later, after a platform timer or the compositor's frame
    // callback (Steam Deck on Windows: 40 instead of 53 frames a second).
    if (window == nullptr || updatePosted)
        return;
    updatePosted = true;
    QCoreApplication::postEvent(window, new QEvent(QEvent::UpdateRequest), Qt::LowEventPriority);
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
        // As the OpenGL renderer (swap interval 0 in main.cpp): frames are
        // paced by the widget's timer and core.system.fpsLimit, not by the
        // display's refresh.
        swapChain->setFlags(QRhiSwapChain::NoVSync);
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
    updatePosted = false;
    QRhi *rhi = rhiContext->rhi();
    if (rhi == nullptr || client == nullptr || !ensureSwapChain())
        return;
    // TSRE_RHI_TRACE: where the frames' time goes, averaged over 60 frames.
    static const bool trace = qEnvironmentVariableIsSet("TSRE_RHI_TRACE");
    static QElapsedTimer clock;
    static qint64 lastEnd = 0, sums[4] = {}, frames = 0;
    if (trace && !clock.isValid())
        clock.start();
    const qint64 start = trace ? clock.nsecsElapsed() : 0;
    QRhi::FrameOpResult result = rhi->beginFrame(swapChain.get());
    if (result == QRhi::FrameOpSwapChainOutOfDate) {
        swapChainReady = false;
        if (!ensureSwapChain())
            return;
        result = rhi->beginFrame(swapChain.get());
    }
    if (result != QRhi::FrameOpSuccess)
        return;
    const qint64 begun = trace ? clock.nsecsElapsed() : 0;
    current.commandBuffer = swapChain->currentFrameCommandBuffer();
    current.target = swapChain->currentFrameRenderTarget();
    current.passDescriptor = passDescriptor.get();
    current.pixelSize = swapChain->currentPixelSize();
    current.offscreen = false;
    paintFrame();
    const qint64 painted = trace ? clock.nsecsElapsed() : 0;
    rhi->endFrame(swapChain.get());
    if (trace) {
        const qint64 end = clock.nsecsElapsed();
        if (lastEnd != 0) {
            sums[0] += start - lastEnd;
            sums[1] += begun - start;
            sums[2] += painted - begun;
            sums[3] += end - painted;
            if (++frames == 60) {
                qInfo().noquote() << "rhi-trace frame ms between" << sums[0] / 60e6 << "beginFrame"
                                  << sums[1] / 60e6 << "paint" << sums[2] / 60e6 << "endFrame"
                                  << sums[3] / 60e6;
                frames = 0;
                std::fill(std::begin(sums), std::end(sums), 0);
            }
        }
        lastEnd = end;
    }
}

void RhiRenderSurface::paintFrame() {
    ++serial;
    overlayUsed = false;
    client->surfacePaint();
    if (frameEnd)
        frameEnd();
    if (overlayUsed)
        overlayStaleRect = overlayPaintedRect;
    overlayUsed = false;
    current = Frame();
}

QPaintDevice *RhiRenderSurface::overlayPaintDevice(const QRect &area) {
    if (current.commandBuffer == nullptr)
        return nullptr;
    // Uploading and composing the whole window each frame cost the Steam
    // Deck about a third of its frame rate (the FPS display).
    if (overlayImage.size() != current.pixelSize) {
        overlayImage = QImage(current.pixelSize, QImage::Format_RGBA8888_Premultiplied);
        overlayImage.fill(Qt::transparent);
        overlayStaleRect = QRect();
    }
    const qreal ratio = hostWidget->devicePixelRatioF();
    overlayImage.setDevicePixelRatio(ratio);
    if (!overlayUsed) {
        overlayUsed = true;
        const QRect stale = overlayStaleRect & overlayImage.rect();
        for (int y = stale.top(); y <= stale.bottom(); ++y)
            std::memset(overlayImage.scanLine(y) + stale.left() * 4, 0, size_t(stale.width()) * 4);
        overlayChangedRect = stale;
        overlayPaintedRect = QRect();
    }
    // In pixels, with a pixel more for antialiased edges.
    const QRect painted = area.isNull() ? overlayImage.rect()
            : QRectF(area.x() * ratio, area.y() * ratio, area.width() * ratio, area.height() * ratio)
                      .toAlignedRect().adjusted(-1, -1, 1, 1) & overlayImage.rect();
    overlayPaintedRect |= painted;
    overlayChangedRect |= painted;
    return &overlayImage;
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
    QRhiReadbackResult readback;
    paintFrame();
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
