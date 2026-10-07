/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "TrackItemMapLayer.h"
#include "MapPalette.h"
#include "MapView.h"
#include "TrackMapLayer.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QOpenGLFunctions>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/world/Route.h>
#include <tsre/world/Tile.h>
#include <tsre/world/objects/WorldObj.h>

namespace {

// Geometry is rebuilt when the scale changed by more than this factor, so
// circles keep about their size in pixels.
constexpr float RebuildScale = 1.25f;
constexpr int CircleSegments = 12;

void appendCircle(std::vector<float> &out, float x, float y, float z, float radius) {
    constexpr float step = 6.28318531f / CircleSegments;
    // Wound like TrackMapLayer's ribbons and octagons, which the map's face
    // culling keeps.
    for (int i = 0; i < CircleSegments; ++i) {
        const float a = i * step;
        const float b = (i + 1) * step;
        const float triangle[9] = {x, y, z,
                                   x + radius * std::cos(b), y, z + radius * std::sin(b),
                                   x + radius * std::cos(a), y, z + radius * std::sin(a)};
        out.insert(out.end(), triangle, triangle + 9);
    }
}

}

TrackItemMapLayer::TrackItemMapLayer() : borders(std::make_unique<OglObj>()) {
    for (auto &fill : fills)
        fill = std::make_unique<OglObj>();
}

TrackItemMapLayer::~TrackItemMapLayer() = default;

int TrackItemMapLayer::kindOfItemType(const QString &type) {
    static const QHash<QString, int> kinds = {
        {"signalitem", Signal}, {"speedpostitem", SpeedPost}, {"platformitem", Platform},
        {"sidingitem", Siding}, {"carspawneritem", CarSpawner}, {"levelcritem", LevelCrossing},
        {"hazzarditem", Hazard}, {"pickupitem", Pickup}, {"soundregionitem", SoundRegion}};
    return kinds.value(type.toLower(), -1);
}

int TrackItemMapLayer::kindOfObjectType(int typeId) {
    switch (typeId) {
    case WorldObj::signal: return Signal;
    case WorldObj::speedpost: return SpeedPost;
    case WorldObj::platform: return Platform;
    case WorldObj::siding: return Siding;
    case WorldObj::carspawner: return CarSpawner;
    case WorldObj::levelcr: return LevelCrossing;
    case WorldObj::hazard: return Hazard;
    case WorldObj::pickup: return Pickup;
    case WorldObj::soundregion: return SoundRegion;
    default: return -1;
    }
}

QColor TrackItemMapLayer::colour(const MapPalette &palette, int kind) {
    switch (kind) {
    case Signal: return palette.signal;
    case SpeedPost: return palette.speedPost;
    case Platform: return palette.platform;
    case Siding: return palette.siding;
    case CarSpawner: return palette.carSpawner;
    case LevelCrossing: return palette.levelCrossing;
    case Hazard: return palette.hazard;
    case Pickup: return palette.pickup;
    case SoundRegion: return palette.soundRegion;
    default: return palette.itemBorder;
    }
}

bool TrackItemMapLayer::drawsWorldObjects(const MapView &view) {
    return std::max(view.width, view.height) * view.metresPerPixel < WorldObjectExtentMetres;
}

void TrackItemMapLayer::invalidate() {
    trackIndex = Index();
    roadIndex = Index();
    valid = false;
}

void TrackItemMapLayer::buildIndex(Index &index, TDB *database) {
    index.built = true;
    index.items.clear();
    if (database == nullptr || !database->loaded)
        return;
    // The node of each item, from the nodes' references.
    QHash<int, int> nodeOfItem;
    for (int id = 1; id <= database->iTRnodes; ++id) {
        auto found = database->trackNodes.find(id);
        if (found == database->trackNodes.end() || found->second == nullptr)
            continue;
        const TRnode *node = found->second;
        if (node->typ != 1 || node->trItemRef == nullptr)
            continue;
        for (int i = 0; i < node->iTri; ++i)
            nodeOfItem.insert(node->trItemRef[i], id);
    }
    float out[8];
    for (const auto &entry : database->trackItems) {
        TRitem *item = entry.second;
        if (item == nullptr)
            continue;
        const int kind = kindOfItemType(item->type);
        const auto node = nodeOfItem.constFind(int(item->trItemId));
        if (kind < 0 || node == nodeOfItem.constEnd())
            continue;
        if (!database->getDrawPositionOnTrNode(out, node.value(), item->getTrackPosition()))
            continue;
        Position position;
        position.tileX = int(out[5]);
        position.tileZ = int(out[6]);
        position.x = out[0];
        position.z = out[2];
        position.kind = kind;
        index.items.insert(int(item->trItemId), position);
    }
}

void TrackItemMapLayer::collectDatabaseMarkers(std::vector<Position> &markers, const int *tiles) {
    for (const Index *index : {&trackIndex, &roadIndex})
        for (const Position &position : index->items)
            // Database tiles are not negated; the view's are.
            if (position.tileX >= tiles[0] && position.tileX <= tiles[1]
                    && -position.tileZ >= tiles[2] && -position.tileZ <= tiles[3])
                markers.push_back(position);
}

int TrackItemMapLayer::objectCount(Route *route, const int *tiles) {
    if (route == nullptr)
        return 0;
    int count = 0;
    for (int x = tiles[0]; x <= tiles[1]; ++x)
        for (int z = tiles[2]; z <= tiles[3]; ++z) {
            const Tile *tile = route->requestTile(x, z, false);
            if (tile != nullptr && tile->loaded == 1)
                count += int(tile->obiekty.size());
        }
    return count;
}

void TrackItemMapLayer::collectObjectMarkers(std::vector<Position> &markers,
                                             std::vector<float> *lines, const MapView &view,
                                             Route *route, const int *tiles) {
    if (route == nullptr)
        return;
    QVector<int> ids;
    for (int x = tiles[0]; x <= tiles[1]; ++x)
        for (int z = tiles[2]; z <= tiles[3]; ++z) {
            Tile *tile = route->requestTile(x, z, false);
            if (tile == nullptr || tile->loaded != 1)
                continue;
            for (const auto &entry : tile->obiekty) {
                WorldObj *object = entry.second;
                if (object == nullptr || !object->loaded || !object->isTrackItem())
                    continue;
                const int kind = kindOfObjectType(object->typeID);
                if (kind < 0)
                    continue;
                object->getMapLine(lines[kind], view.tileX, view.tileZ);
                for (int database = 0; database < 2; ++database) {
                    const Index &index = database == 0 ? trackIndex : roadIndex;
                    ids.clear();
                    object->getTrackItemIds(ids, database);
                    for (int id : ids) {
                        auto found = index.items.constFind(id);
                        if (found == index.items.constEnd())
                            continue;
                        Position position = found.value();
                        position.kind = kind;
                        markers.push_back(position);
                    }
                }
            }
        }
}

void TrackItemMapLayer::build(const MapView &view, const MapPalette &palette, Route *route,
                              const int *tiles, bool worldObjects) {
    static const bool trace = qEnvironmentVariableIsSet("TSRE_MAP_TRACE");
    QElapsedTimer timer;
    timer.start();
    std::vector<Position> markers;
    std::vector<float> lines[KindCount];
    if (worldObjects)
        collectObjectMarkers(markers, lines, view, route, tiles);
    else
        collectDatabaseMarkers(markers, tiles);

    const float radius = 0.5f * MarkerPixels * view.metresPerPixel;
    const float border = radius + BorderPixels * view.metresPerPixel;
    std::vector<float> borderVertices;
    std::vector<float> fillVertices[KindCount];
    const float lineWidth = LinePixels * view.metresPerPixel;
    const float lineBorder = lineWidth + 2.0f * BorderPixels * view.metresPerPixel;
    for (int kind = 0; kind < KindCount; ++kind) {
        std::vector<float> &segments = lines[kind];
        const int count = int(segments.size() / 6);
        for (size_t i = 1; i < segments.size(); i += 3)
            segments[i] = BorderHeight;
        TrackMapLayer::appendRibbons(borderVertices, segments.data(), count, lineBorder);
        for (size_t i = 1; i < segments.size(); i += 3)
            segments[i] = FillHeight;
        TrackMapLayer::appendRibbons(fillVertices[kind], segments.data(), count, lineWidth);
    }
    for (const Position &marker : markers) {
        if (marker.kind < 0 || marker.kind >= KindCount)
            continue;
        const float x = float(marker.tileX - view.tileX) * 2048.0f + marker.x;
        const float z = float(-marker.tileZ - view.tileZ) * 2048.0f - marker.z;
        appendCircle(borderVertices, x, BorderHeight, z, border);
        appendCircle(fillVertices[marker.kind], x, FillHeight, z, radius);
    }
    auto upload = [](OglObj &object, std::vector<float> &vertices, const QColor &colour) {
        object.setMaterial(float(colour.redF()), float(colour.greenF()), float(colour.blueF()));
        object.init(vertices.data(), int(vertices.size()), RenderItem::V, GL_TRIANGLES);
    };
    upload(*borders, borderVertices, palette.itemBorder);
    for (int kind = 0; kind < KindCount; ++kind)
        upload(*fills[kind], fillVertices[kind], colour(palette, kind));

    valid = true;
    builtTileX = view.tileX;
    builtTileZ = view.tileZ;
    builtMetresPerPixel = view.metresPerPixel;
    builtWorldObjects = worldObjects;
    std::copy(tiles, tiles + 4, builtTiles);
    builtPalette = palette.name;
    if (trace)
        qInfo().noquote() << "map-trace items" << (worldObjects ? "objects" : "database")
                          << "m/px" << view.metresPerPixel << "markers" << markers.size()
                          << "ms" << timer.nsecsElapsed() / 1e6;
}

void TrackItemMapLayer::pushRenderItems(RenderQueue &queue, const MapView &view,
                                        const MapPalette &palette, Route *route, TDB *track,
                                        TDB *road) {
    if (!trackIndex.built)
        buildIndex(trackIndex, track);
    if (!roadIndex.built)
        buildIndex(roadIndex, road);
    const bool worldObjects = drawsWorldObjects(view);
    // Zoomed in, the tiles in view, whose world files the map loads; zoomed
    // out, the database items of the tiles in view and one more around.
    int tiles[4];
    view.visibleTiles(tiles[0], tiles[1], tiles[2], tiles[3], 0);
    const float scale = view.metresPerPixel / std::max(builtMetresPerPixel, 1e-6f);
    bool rebuild = !valid || view.tileX != builtTileX || view.tileZ != builtTileZ
            || scale > RebuildScale || scale < 1.0f / RebuildScale
            || worldObjects != builtWorldObjects || palette.name != builtPalette;
    int objects = 0;
    if (worldObjects) {
        rebuild = rebuild || !std::equal(tiles, tiles + 4, builtTiles);
        // Objects added or removed in the tiles, or tiles loaded.
        objects = objectCount(route, tiles);
        rebuild = rebuild || objects != builtObjects;
    } else {
        rebuild = rebuild || tiles[0] < builtTiles[0] || tiles[1] > builtTiles[1]
                || tiles[2] < builtTiles[2] || tiles[3] > builtTiles[3];
        for (int i = 0; i < 4; ++i)
            tiles[i] += i % 2 == 0 ? -1 : 1;
    }
    if (rebuild) {
        build(view, palette, route, tiles, worldObjects);
        builtObjects = objects;
    }
    borders->pushRenderItem(queue);
    for (auto &fill : fills)
        fill->pushRenderItem(queue);
}
