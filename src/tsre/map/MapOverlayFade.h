/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MAPOVERLAYFADE_H
#define MAPOVERLAYFADE_H

#include <memory>
#include <vector>

class MapView;
class OglObj;
class RenderQueue;
struct MapPalette;

// Map > Faded Overlay: the background colour at the palette's overlayFade
// alpha over the ground in view. It lies above terrain, OSM data and the
// terrain aids and under the route's own data (TrackMapLayer: roads at
// 100), so lines and markers stay readable over everything else.
class MapOverlayFade {
public:
    static constexpr float Height = 90.0f;

    MapOverlayFade();
    ~MapOverlayFade();
    void pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette);
    // The ground under the view's corners as two triangles at a height,
    // wound for the map's face culling. Public for tests.
    static void appendView(std::vector<float> &out, const MapView &view, float y);

private:
    std::unique_ptr<OglObj> square;
};

#endif
