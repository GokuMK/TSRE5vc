/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RENDERCONTEXT_H
#define RENDERCONTEXT_H

#include <QOpenGLContext>
#include <tsre/Game.h>

namespace RenderContext {
// Whether producers can make renderer resources (meshes, textures) now: the
// OpenGL renderer needs a current context; the QRhi renderer takes them at
// any time and uploads them with its next frame.
inline bool ready() {
    return Game::renderBackend == QLatin1String("qrhi") || QOpenGLContext::currentContext() != nullptr;
}
// Whether raw OpenGL calls reach the renderer's context: the OpenGL
// renderer with a current context. QRhi on OpenGL makes its own context
// current, which such calls must not touch.
inline bool openGl() {
    return Game::renderBackend != QLatin1String("qrhi") && QOpenGLContext::currentContext() != nullptr;
}
inline bool rhi() {
    return Game::renderBackend == QLatin1String("qrhi");
}
}

#endif
