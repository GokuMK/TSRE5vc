/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef IMAGERYMAPLAYER_H
#define IMAGERYMAPLAYER_H

#include <QHash>
#include <QString>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class MapView;
class OglObj;
class RenderQueue;
class Texture;

// Aerial imagery in map mode (docs/tasks/geo/imagery-map-layer.md): the imagery
// catalogue's default world source (a Web Mercator WMTS, ESA WorldCover by default)
// streamed as the service's own tiles, between the terrain and the OSM data.
//
// Tiles are fetched on a thread, sixteen at a time, through the disk cache Load
// Imagery uses. Their zoom matches the view's scale, capped at the source's native
// resolution. Tiles live in texture pages of 8 x 8 tiles, so a page is one draw;
// each tile is a small mesh bent through the route's projection. A tile not loaded
// yet shows its four children or the nearest loaded ancestor.
class ImageryMapLayer {
public:
    // Above the terrain textures (35), under the OSM data (50).
    static constexpr float Height = 40.0f;
    // Requests in flight at once, on one HTTP/2 connection as a web map viewer's. 36
    // fresh WorldCover tiles took 3.3 s eight at a time and 1.7 s sixteen (curl, 16
    // tiles: 1.9-3.6 s four, 1.2 s eight, 0.75 s sixteen). The service's first render
    // of a tile dominates and varies: one fresh area took 32 s, again 0.4 s.
    static constexpr int ParallelRequests = 16;
    // Texture pages of PageSize pixels hold (PageSize / tile pixels)^2 tiles.
    static constexpr int PageSize = 2048;
    static constexpr int MaxPages = 4;
    // Ancestors searched for a missing tile.
    static constexpr int MaxFallbackLevels = 8;

    ImageryMapLayer();
    ~ImageryMapLayer();
    // Called on the fetch thread when tiles arrive (the view should draw again).
    void setReadyCallback(std::function<void()> callback);
    // The source drawn; false (and why) when the catalogue has no usable world tile
    // source or the route has no geographic reference.
    bool prepare(QString &error);
    // Draws the loaded tiles and asks for the missing ones.
    void pushRenderItems(RenderQueue &queue, const MapView &view);
    // The layer is hidden: tiles not yet requested are not fetched.
    void pause();
    // Tiles are being fetched or wait to be drawn.
    bool busy() const;
    // The source's name and attribution, for display while the layer shows.
    QString attribution() const;

    // A tile address packed: zoom, column and row.
    static uint64_t key(int zoom, int column, int row) {
        return (uint64_t(zoom) << 58) | (uint64_t(column) << 29) | uint64_t(row);
    }
    static int zoomOf(uint64_t key) { return int(key >> 58); }
    static int columnOf(uint64_t key) { return int((key >> 29) & 0x1fffffff); }
    static int rowOf(uint64_t key) { return int(key & 0x1fffffff); }

private:
    class Fetcher;
    struct Source;
    struct Mesh;
    struct Page;
    struct Slot;
    struct Draw;

    void takeDeliveries();
    int allocateSlot();
    void upload(int slot, const unsigned char *rgba);
    const Mesh &meshFor(uint64_t key);
    bool collect(const MapView &view, std::vector<Draw> &draws, std::vector<uint64_t> &missing);
    void build(const MapView &view, const std::vector<Draw> &draws);

    std::unique_ptr<Source> source;
    std::unique_ptr<Fetcher> fetcher;
    std::function<void()> ready;
    std::vector<std::unique_ptr<Page>> pages;
    std::vector<Slot> tileSlots;
    std::vector<int> freeSlots;
    QHash<uint64_t, int> slotOf;
    QHash<uint64_t, Mesh> meshes;
    QHash<uint64_t, qint64> failedAt;
    uint64_t frame = 0;
    std::vector<Draw> drawn;
    std::vector<uint64_t> asked;
    std::vector<std::unique_ptr<OglObj>> objects;  // one per page
    int builtTile[2] = {0, 0};
    int meshTile[2] = {0, 0};
    bool warned = false;
};

#endif
