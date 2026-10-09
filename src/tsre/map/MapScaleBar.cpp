/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "MapScaleBar.h"
#include "MapPalette.h"
#include "MapView.h"
#include <QOpenGLFunctions>
#include <cmath>
#include <vector>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderItem.h>

namespace {

// A screen rectangle (pixels, y down) as two ground triangles, wound as the map's
// ribbons (front faces have a negative cross in x, z); with texture coordinates
// when uv is given (u0, v0, u1, v1).
void appendRect(std::vector<float> &out, const MapView &view, float height, float x0, float y0, float x1,
                float y1, const float *uv = nullptr) {
    float g[4][2];
    view.groundAt(x0, y0, g[0][0], g[0][1]);
    view.groundAt(x1, y0, g[1][0], g[1][1]);
    view.groundAt(x1, y1, g[2][0], g[2][1]);
    view.groundAt(x0, y1, g[3][0], g[3][1]);
    const float cross = (g[1][0] - g[0][0]) * (g[2][1] - g[0][1]) - (g[1][1] - g[0][1]) * (g[2][0] - g[0][0]);
    static const int orders[2][6] = {{0, 2, 1, 0, 3, 2}, {0, 1, 2, 0, 2, 3}};
    for (int c : orders[cross > 0 ? 0 : 1]) {
        out.insert(out.end(), {g[c][0], height, g[c][1]});
        if (uv != nullptr)
            out.insert(out.end(), {uv[c == 1 || c == 2 ? 2 : 0], uv[c >= 2 ? 3 : 1], 0.0f});
    }
}

}

MapScaleBar::MapScaleBar()
    : halo(std::make_unique<OglObj>()), bar(std::make_unique<OglObj>()), label(std::make_unique<OglObj>()) {}

MapScaleBar::~MapScaleBar() = default;

double MapScaleBar::roundLength(double metres) {
    if (!(metres > 0.0))
        return 0.0;
    const double power = std::pow(10.0, std::floor(std::log10(metres)));
    for (double step : {5.0, 2.0, 1.0})
        if (step * power <= metres * (1.0 + 1e-9))
            return step * power;
    return power;
}

QString MapScaleBar::text(double metres) {
    if (metres >= 1000.0)
        //% "%1 km"
        return qtTrId("route.editor.map.scale.kilometres").arg(metres / 1000.0);
    //% "%1 m"
    return qtTrId("route.editor.map.scale.metres").arg(metres);
}

void MapScaleBar::pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                                  float pixelRatio) {
    const float r = pixelRatio;
    const double metres = roundLength(double(MaxPixels * r) * view.metresPerPixel);
    if (metres <= 0.0)
        return;
    // Whole pixels, so the bar and its text stay sharp.
    const float length = std::round(float(metres / view.metresPerPixel));
    const float left = std::round(MarginPixels * r), bottom = std::round(float(view.height) - MarginPixels * r);
    const float line = std::max(1.0f, std::round(LinePixels * r)), tick = std::round(TickPixels * r);
    const float h = HaloPixels * r;
    // The bar and its end ticks: |____|
    const float rects[3][4] = {{left, bottom - line, left + length, bottom},
                               {left, bottom - tick, left + line, bottom},
                               {left + length - line, bottom - tick, left + length, bottom}};
    std::vector<float> haloTriangles, barTriangles;
    for (const auto &q : rects) {
        appendRect(haloTriangles, view, HaloHeight, q[0] - h, q[1] - h, q[2] + h, q[3] + h);
        appendRect(barTriangles, view, BarHeight, q[0], q[1], q[2], q[3]);
    }
    halo->setMaterial(float(palette.labelHalo.redF()), float(palette.labelHalo.greenF()),
                      float(palette.labelHalo.blueF()));
    halo->init(haloTriangles.data(), int(haloTriangles.size()), RenderItem::V, GL_TRIANGLES);
    bar->setMaterial(float(palette.label.redF()), float(palette.label.greenF()), float(palette.label.blueF()));
    bar->init(barTriangles.data(), int(barTriangles.size()), RenderItem::V, GL_TRIANGLES);
    halo->pushRenderItem(queue);
    bar->pushRenderItem(queue);

    // The length above the bar, centred on it.
    atlas.setStyle(palette.label, palette.labelHalo, pixelRatio);
    const MapLabelAtlas::Entry *entry = atlas.get(text(metres), false);
    if (entry == nullptr) {
        atlas.clear();
        entry = atlas.get(text(metres), false);
    }
    if (entry == nullptr)
        return;
    const float w = float(entry->rect.width()), th = float(entry->rect.height());
    const float x0 = std::round(left + (length - w) / 2.0f), y1 = std::round(bottom - tick - GapPixels * r);
    const float page = float(MapLabelAtlas::PageSize);
    const float uv[4] = {entry->rect.left() / page, entry->rect.top() / page,
                         (entry->rect.left() + entry->rect.width()) / page,
                         (entry->rect.top() + entry->rect.height()) / page};
    std::vector<float> quad;
    appendRect(quad, view, TextHeight, std::max(x0, left), y1 - th, std::max(x0, left) + w, y1, uv);
    atlas.upload();
    label->setMaterialTextureId(atlas.textureId(entry->page));
    label->init(quad.data(), int(quad.size()), RenderItem::VT, GL_TRIANGLES);
    label->pushRenderItem(queue);
}
