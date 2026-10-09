/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MAPSCALEBAR_H
#define MAPSCALEBAR_H

#include "MapLabelLayer.h"
#include <QString>
#include <memory>

class MapView;
class OglObj;
class RenderQueue;
struct MapPalette;

// The map's scale bar (Map > Scale Bar): in the bottom left corner, a bar of a
// round length (1, 2 or 5 times a power of ten) at most MaxPixels long, with its
// length above it. In route (game) metres, as the map is drawn: on routes whose
// projection scales distances (the legacy MSTS one) a geo length differs, and by
// direction; Measure Distance shows both for a line.
//
// Drawn on the ground like the labels, each frame from screen rectangles, above
// everything but the pointer.
class MapScaleBar {
public:
    static constexpr float HaloHeight = 850.0f;
    static constexpr float BarHeight = 851.0f;
    static constexpr float TextHeight = 852.0f;
    // Logical pixels (times the pixel ratio).
    static constexpr float MaxPixels = 120.0f;
    static constexpr float MarginPixels = 12.0f;
    static constexpr float LinePixels = 2.0f;
    static constexpr float TickPixels = 8.0f;
    static constexpr float HaloPixels = 1.5f;
    static constexpr float GapPixels = 2.0f;

    MapScaleBar();
    ~MapScaleBar();
    // The longest round length up to metres: 1, 2 or 5 times a power of ten.
    static double roundLength(double metres);
    // "500 m", "2 km".
    static QString text(double metres);
    void pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette, float pixelRatio);

private:
    MapLabelAtlas atlas;
    std::unique_ptr<OglObj> halo;
    std::unique_ptr<OglObj> bar;
    std::unique_ptr<OglObj> label;
};

#endif
