/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef WEATHERMAPLAYER_H
#define WEATHERMAPLAYER_H

#include <QColor>
#include <functional>
#include <memory>

class MapView;
class OglObj;
class RenderQueue;
class Texture;
class Weather;
struct MapPalette;

// The weather in map mode (task environment 01), as a weather app shows it:
// the low, mid and high clouds where they are at the time shown, rain and
// snow in radar colours over them, the wind's direction as arrows, and the
// weather's cells when zoomed in.
//
// The clouds are an image of the ground around the view, made on a worker
// thread and anchored to the ground: while the view pans or a new image is
// made, the last one stays where it belongs.
class WeatherMapLayer {
public:
    // Over the route's data, under the measure line, labels and pointer.
    static constexpr float Height = 760.0f;
    static constexpr float CellHeight = 762.0f;
    static constexpr float ArrowHeight = 765.0f;
    // Texels along a side of the image.
    static constexpr int Size = 512;

    WeatherMapLayer();
    ~WeatherMapLayer();
    // Called on the worker thread when an image is ready (draw again).
    void setReadyCallback(std::function<void()> callback);
    // The weather itself changed (another env file, preset or seed).
    void invalidate();
    void pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                         const Weather &weather, double hours, bool cells);
    // An image is being made.
    bool busy() const;

    // The colours of a layer's clouds and of precipitation (mm/h) on a
    // palette's background; shared with the legend.
    static QColor cloudColour(const MapPalette &palette, int layer);
    static QColor precipitationColour(float rate, bool snow);

    // What the worker makes an image of, and what it shares with the layer
    // (public for the worker's functions).
    struct Shared;
    struct Request;

private:
    std::shared_ptr<Shared> shared;
    Texture *texture = nullptr;
    int textureId = -1;
    std::unique_ptr<OglObj> image;
    std::unique_ptr<OglObj> cellLines;
    std::unique_ptr<OglObj> arrows;
    int revision = 0;
    // The image drawn: its ground (MSTS metres), time and weather revision.
    bool shown = false;
    double shownWest = 0.0, shownNorth = 0.0, shownSize = 0.0, shownHours = 0.0;
    int shownRevision = -1;
    // The image asked for last.
    bool requested = false;
    double requestedWest = 0.0, requestedNorth = 0.0, requestedSize = 0.0, requestedHours = 0.0;
    int requestedRevision = -1;
    void request(const Weather &weather, double hours, double west, double north, double size,
                 const MapPalette &palette);
    void takeResult();
};

#endif
