/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "RenderSurface.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QVBoxLayout>

namespace {

// A QOpenGLWidget filling its host; mouse input passes through to the host,
// which keeps the keyboard focus.
class GlRenderSurface : public QOpenGLWidget, public RenderSurface {
public:
    GlRenderSurface(QWidget *host, RenderSurfaceClient *client)
        : QOpenGLWidget(host), client(client) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }
    Backend backend() const override { return OpenGL; }
    QWidget *widget() override { return this; }
    void requestUpdate() override { QOpenGLWidget::update(); }
    void makeCurrent() override { QOpenGLWidget::makeCurrent(); }
    void doneCurrent() override { QOpenGLWidget::doneCurrent(); }
    bool isValid() const override { return QOpenGLWidget::isValid(); }
    QString graphicsInfo() override {
        if (!QOpenGLWidget::isValid())
            return QString();
        QOpenGLWidget::makeCurrent();
        const QString info = QString::fromLatin1(reinterpret_cast<const char *>(
                context()->functions()->glGetString(GL_RENDERER)));
        QOpenGLWidget::doneCurrent();
        return info;
    }
    QImage grabFramebuffer() override { return QOpenGLWidget::grabFramebuffer(); }
    unsigned int defaultFramebufferObject() const override {
        return QOpenGLWidget::defaultFramebufferObject();
    }
    QPaintDevice *overlayPaintDevice() override { return this; }
    void detachClient() override {
        if (context() != nullptr)
            QObject::disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, nullptr);
        client = nullptr;
    }

protected:
    void initializeGL() override {
        // The client releases its resources while the context still exists.
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
            if (client != nullptr)
                client->surfaceRelease();
        });
        if (client != nullptr)
            client->surfaceInitialize();
    }
    void resizeGL(int width, int height) override {
        if (client != nullptr)
            client->surfaceResize(width, height);
    }
    void paintGL() override {
        if (client != nullptr)
            client->surfacePaint();
    }

private:
    RenderSurfaceClient *client;
};

}

RenderSurface *RenderSurface::create(QWidget *host, RenderSurfaceClient *client) {
    GlRenderSurface *surface = new GlRenderSurface(host, client);
    QVBoxLayout *layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(surface);
    return surface;
}
