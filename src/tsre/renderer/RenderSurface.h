/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RENDERSURFACE_H
#define RENDERSURFACE_H

#include <QImage>
#include <QString>

class QPaintDevice;
class QWidget;

// The view a render surface draws for: it sets up, resizes and paints when
// the surface asks.
class RenderSurfaceClient {
public:
    virtual ~RenderSurfaceClient() = default;
    // The surface's graphics context (OpenGL) or QRhi is ready.
    virtual void surfaceInitialize() = 0;
    virtual void surfaceResize(int width, int height) = 0;
    virtual void surfacePaint() = 0;
    // The surface's graphics resources are about to go away.
    virtual void surfaceRelease() = 0;
};

// Where an editor view draws: a QOpenGLWidget child for the OpenGL renderer
// or a QRhi window for the QRhi renderer, filling the host widget. Input
// goes to the host widget.
class RenderSurface {
public:
    enum Backend {OpenGL, Rhi};
    virtual ~RenderSurface() = default;
    // A surface for the backend chosen in the settings, filling host.
    static RenderSurface *create(QWidget *host, RenderSurfaceClient *client);
    virtual Backend backend() const = 0;
    virtual QWidget *widget() = 0;
    // Schedules a repaint.
    virtual void requestUpdate() = 0;
    // Makes the graphics context current outside painting (OpenGL).
    virtual void makeCurrent() = 0;
    virtual void doneCurrent() = 0;
    // The graphics API and device, for reports.
    virtual QString graphicsInfo() = 0;
    // Whether the surface has working graphics resources.
    virtual bool isValid() const = 0;
    // Paints a frame and returns it.
    virtual QImage grabFramebuffer() = 0;
    // The framebuffer a paint draws into (OpenGL); 0 elsewhere.
    virtual unsigned int defaultFramebufferObject() const = 0;
    // Where 2D overlays can be painted with QPainter after the frame; null
    // when the surface has none.
    virtual QPaintDevice *overlayPaintDevice() = 0;
    // Stops calling the client (its host is being destroyed).
    virtual void detachClient() = 0;
};

#endif
