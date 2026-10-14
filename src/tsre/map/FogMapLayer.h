/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef FOGMAPLAYER_H
#define FOGMAPLAYER_H

#include <tsre/environment/WeatherFog.h>

#include <QColor>

#include <memory>

class FogFloor;
class MapView;
class OglObj;
class RenderQueue;
class Texture;
struct MapPalette;

// Where the fog lies (task renderer 33), over the weather in map mode: the
// floor map around the camera, misty blue-grey (pale on a dark palette)
// where the ground lies under the fog's top, the more opaque the deeper.
class FogMapLayer {
public:
    // Over the weather's clouds, under its cells and arrows.
    static constexpr float Height = 761.0f;

    FogMapLayer();
    ~FogMapLayer();
    // The floor map or the fog changed: a new image at the next draw.
    void invalidate() { dirty = true; }
    void pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                         const FogFloor &floor, const WeatherFog::State &fog);

private:
    Texture *texture = nullptr;
    int textureId = -1;
    std::unique_ptr<OglObj> image;
    bool dirty = true;
    bool any = false;
    QColor colour;
};

#endif
