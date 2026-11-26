/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "LookPointer.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace LookPointer {

bool position(QPoint *at) {
#ifdef Q_OS_WIN
    POINT cursor;
    if (!GetCursorPos(&cursor))
        return false;
    *at = QPoint(cursor.x, cursor.y);
    return true;
#else
    // X11, to be written and tested on Linux: the same exact read with
    // XQueryPointer on the root window, in the X screen's pixels:
    //   if (QGuiApplication::platformName() != "xcb") return false;
    //   auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    //   Display *display = x11 ? x11->display() : nullptr;
    //   Window root, child; int rootX, rootY, winX, winY; unsigned mask;
    //   if (!display || !XQueryPointer(display, DefaultRootWindow(display), &root, &child,
    //                                  &rootX, &rootY, &winX, &winY, &mask)) return false;
    //   *at = QPoint(rootX, rootY); return true;
    // It needs <X11/Xlib.h> and libX11 linked for the Linux build. Wayland
    // has no read or warp of the pointer (QCursor::setPos does nothing
    // there): a look on Wayland needs the relative-pointer and
    // pointer-constraints protocols, a task of its own. Until then the
    // view reads Qt's event positions (RouteEditorGLWidget::mouseMoveEvent).
    Q_UNUSED(at);
    return false;
#endif
}

}
