/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "MapOverlayFade.h"
#include "MapPalette.h"
#include "MapView.h"
#include <QOpenGLFunctions>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderItem.h>

MapOverlayFade::MapOverlayFade() : square(std::make_unique<OglObj>()) {}

MapOverlayFade::~MapOverlayFade() = default;

void MapOverlayFade::appendView(std::vector<float> &out, const MapView &view, float y) {
    const float screen[4][2] = {{0, 0}, {float(view.width), 0},
                                {float(view.width), float(view.height)},
                                {0, float(view.height)}};
    float ground[4][2];
    for (int i = 0; i < 4; ++i)
        view.groundAt(screen[i][0], screen[i][1], ground[i][0], ground[i][1]);
    for (int corner : {0, 3, 2, 0, 2, 1})
        out.insert(out.end(), {ground[corner][0], y, ground[corner][1]});
}

void MapOverlayFade::pushRenderItems(RenderQueue &queue, const MapView &view,
                                     const MapPalette &palette) {
    if (palette.overlayFade <= 0.0f)
        return;
    std::vector<float> vertices;
    appendView(vertices, view, Height);
    square->setMaterial(float(palette.background.redF()), float(palette.background.greenF()),
                        float(palette.background.blueF()), palette.overlayFade);
    square->init(vertices.data(), int(vertices.size()), RenderItem::V, GL_TRIANGLES);
    square->pushRenderItem(queue);
}
