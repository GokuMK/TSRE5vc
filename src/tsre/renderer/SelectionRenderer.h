/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SELECTIONRENDERER_H
#define SELECTIONRENDERER_H

#include <QtGlobal>

class SelectionRenderer {
public:
    SelectionRenderer() = default;
    ~SelectionRenderer() = default;

    bool begin(int width, int height);
    void end();
    quint32 readPixel(int x, int y) const;
    void release();

    bool isActive() const;
    int width() const;
    int height() const;

private:
    Q_DISABLE_COPY(SelectionRenderer)

    bool resize(int width, int height);

    unsigned int framebuffer = 0;
    unsigned int colorTexture = 0;
    unsigned int depthRenderbuffer = 0;
    int targetWidth = 0;
    int targetHeight = 0;
    bool active = false;

    int previousDrawFramebuffer = 0;
    int previousReadFramebuffer = 0;
    int previousViewport[4] = {0, 0, 0, 0};
    unsigned char previousColorMask[4] = {1, 1, 1, 1};
    unsigned char previousDepthMask = 1;
    bool previousBlend = false;
    bool previousDither = false;
    bool previousMultisample = false;
    bool previousDepthTest = false;
    bool previousScissorTest = false;
    bool previousFramebufferSrgb = false;
};

#endif // SELECTIONRENDERER_H
