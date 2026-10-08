/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "TerrainMapLayer.h"
#include "MapPalette.h"
#include "MapView.h"
#include "TrackMapLayer.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QOpenGLFunctions>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainGridLayout.h>
#include <tsre/world/TerrainInfo.h>
#include <tsre/world/TerrainLib.h>

namespace {

// Geometry is rebuilt when the scale changed by more than this factor, so
// borders keep about their width in pixels.
constexpr float RebuildScale = 1.25f;

void setColour(OglObj &object, const QColor &colour) {
    object.setMaterial(float(colour.redF()), float(colour.greenF()), float(colour.blueF()));
}

}

// A procedural tile as one square, submitted as the tile's direct GPU
// material packet (Terrain::configureMapProceduralPacket).
class TerrainMapLayer::ProceduralSquare : public OglObj {
public:
    void push(RenderQueue &queue, Terrain *tile) {
        if (!loaded || tile == nullptr)
            return;
        static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        const unsigned int layers[RenderItem::Water::LAYER_COUNT] = {0, 0};
        RenderItem *packet = framePacket(false, 0, white, false, layers);
        if (tile->configureMapProceduralPacket(*packet))
            queue.submit(packet, 0, RenderQueue::SUBMIT_ORDERED);
    }
};

TerrainMapLayer::TerrainMapLayer()
    : distantBorders(std::make_unique<OglObj>()), borders(std::make_unique<OglObj>()),
      fade(std::make_unique<OglObj>()) {}

TerrainMapLayer::~TerrainMapLayer() = default;

bool TerrainMapLayer::drawsDetailedPatches(const MapView &view) {
    return std::max(view.width, view.height) * view.metresPerPixel <= DetailedExtentMetres;
}

bool TerrainMapLayer::drawsProcedural(const MapView &view) {
    return std::max(view.width, view.height) * view.metresPerPixel < ProceduralExtentMetres;
}

void TerrainMapLayer::appendPatch(std::vector<float> &out, const float *corners, float y) {
    // Corners in sample order (0, 0), (R, 0), (R, R), (0, R): reversed, as
    // TrackMapLayer's ribbons are wound.
    for (int corner : {0, 3, 2, 0, 2, 1}) {
        const float *c = corners + corner * 4;
        out.insert(out.end(), {c[0], y, c[1], c[2], c[3], 1.0f});
    }
}

void TerrainMapLayer::invalidate() {
    valid = false;
    distant.clear();
    detailed.clear();
    procedural.clear();
}

void TerrainMapLayer::appendTile(Terrain *tile, const MapView &view, float y,
                                 QHash<int, std::vector<float>> &byTexture) {
    float corners[16];
    for (int patch = 0; patch < tile->getGridLayout().patchRecordCount(); ++patch) {
        const int id = tile->mapPatchTexture(patch);
        if (id >= 0 && tile->mapPatchCorners(patch, view.tileX, view.tileZ, corners))
            appendPatch(byTexture[id], corners, y);
    }
}

TerrainMapLayer::Procedural TerrainMapLayer::proceduralSquare(Terrain *tile, const MapView &view,
                                                              int tileX, int tileZ) {
    // The tile in sample order, its texture coordinates spanning it.
    const TerrainGridLayout &grid = tile->getGridLayout();
    const float size = float(grid.terrainWorldSize);
    const float x0 = (tile->mojex - view.tileX) * 2048.0f - 1024.0f;
    const float z0 = (tile->mojez - view.tileZ) * 2048.0f + 1024.0f - size;
    const float corners[16] = {x0, z0, 0, 0,  x0 + size, z0, 1, 0,
                               x0 + size, z0 + size, 1, 1,  x0, z0 + size, 0, 1};
    std::vector<float> vertices;
    appendPatch(vertices, corners, ProceduralHeight);
    Procedural square;
    square.tileX = tileX;
    square.tileZ = tileZ;
    square.square = std::make_unique<ProceduralSquare>();
    square.square->init(vertices.data(), int(vertices.size()), RenderItem::VT, GL_TRIANGLES);
    return square;
}

void TerrainMapLayer::appendOutline(std::vector<float> &out, const TerrainInfo &info,
                                    const MapView &view, float y, float width) {
    // As Terrain places a tile: its corner (cx, -cy) and size in world
    // tiles (level).
    const float size = std::max(info.level, 1) * 2048.0f;
    const float x0 = (info.cx - view.tileX) * 2048.0f - 1024.0f;
    const float z0 = (-info.cy - view.tileZ) * 2048.0f + 1024.0f - size;
    const float ring[5][2] = {{x0, z0}, {x0 + size, z0}, {x0 + size, z0 + size},
                              {x0, z0 + size}, {x0, z0}};
    float segments[24];
    for (int i = 0; i < 4; ++i) {
        const float segment[6] = {ring[i][0], y, ring[i][1], ring[i + 1][0], y, ring[i + 1][1]};
        std::copy(segment, segment + 6, segments + i * 6);
    }
    TrackMapLayer::appendRibbons(out, segments, 4, width);
}

void TerrainMapLayer::build(const MapView &view, const MapPalette &palette, TerrainLib *terrain) {
    static const bool trace = qEnvironmentVariableIsSet("TSRE_MAP_TRACE");
    QElapsedTimer timer;
    timer.start();
    const bool patches = drawsDetailedPatches(view);
    const bool gpu = drawsProcedural(view);
    procedural.clear();
    int tiles[4];
    view.visibleTiles(tiles[0], tiles[1], tiles[2], tiles[3], 1);
    const float borderWidth = BorderPixels * view.metresPerPixel;

    QHash<int, std::vector<float>> distantPatches, detailedPatches;
    std::vector<float> distantOutlines, outlines;
    QSet<unsigned int> seenDistant, seenDetailed;
    int distantTiles = 0, detailedTiles = 0;
    TerrainInfo info;
    for (int x = tiles[0]; x <= tiles[1]; ++x)
        for (int z = tiles[2]; z <= tiles[3]; ++z) {
            // Distant terrain always, below the detailed tiles.
            const unsigned int distantId = terrain->terrainTileId(x, z, true, &info);
            if (distantId != 0 && !seenDistant.contains(distantId)) {
                seenDistant.insert(distantId);
                appendOutline(distantOutlines, info, view, DistantBorderHeight, borderWidth);
                Terrain *tile = terrain->getDistantDescriptor(x, z);
                if (tile != nullptr && tile->descriptorLoaded) {
                    appendTile(tile, view, DistantHeight, distantPatches);
                    ++distantTiles;
                }
            }
            const unsigned int detailedId = terrain->terrainTileId(x, z, false, &info);
            if (detailedId == 0 || seenDetailed.contains(detailedId))
                continue;
            seenDetailed.insert(detailedId);
            appendOutline(outlines, info, view, BorderHeight, borderWidth);
            // Wider views: borders only, nothing loaded.
            if (!patches)
                continue;
            Terrain *tile = terrain->getTerrainDescriptor(x, z);
            if (tile != nullptr && tile->descriptorLoaded) {
                appendTile(tile, view, DetailedHeight, detailedPatches);
                ++detailedTiles;
                // Close: procedural tiles complete, shaded over their bake.
                if (gpu && tile->usesProceduralMaterial()) {
                    Terrain *complete = terrain->getTerrainByXY(x, z, true);
                    if (complete != nullptr && complete->loaded)
                        procedural.push_back(proceduralSquare(complete, view, x, z));
                }
            }
        }

    auto groups = [](std::vector<Group> &out, QHash<int, std::vector<float>> &byTexture) {
        out.clear();
        for (auto it = byTexture.begin(); it != byTexture.end(); ++it) {
            Group group;
            group.texture = it.key();
            group.object = std::make_unique<OglObj>();
            group.object->setMaterialTextureId(it.key());
            group.object->init(it.value().data(), int(it.value().size()), RenderItem::VT,
                               GL_TRIANGLES);
            out.push_back(std::move(group));
        }
    };
    groups(distant, distantPatches);
    groups(detailed, detailedPatches);
    setColour(*distantBorders, palette.distantBorder);
    distantBorders->init(distantOutlines.data(), int(distantOutlines.size()), RenderItem::V,
                         GL_TRIANGLES);
    setColour(*borders, palette.terrainBorder);
    borders->init(outlines.data(), int(outlines.size()), RenderItem::V, GL_TRIANGLES);

    valid = true;
    builtTileX = view.tileX;
    builtTileZ = view.tileZ;
    builtMetresPerPixel = view.metresPerPixel;
    builtDetailed = patches;
    builtProcedural = gpu;
    std::copy(tiles, tiles + 4, builtTiles);
    builtPalette = palette.name;
    if (trace)
        qInfo().noquote() << "map-trace terrain" << (patches ? "patches" : "borders") << "m/px"
                          << view.metresPerPixel << "distant tiles" << seenDistant.size()
                          << "loaded" << distantTiles << "detailed tiles" << seenDetailed.size()
                          << "loaded" << detailedTiles << "procedural" << procedural.size()
                          << "textures" << distant.size() + detailed.size() << "ms"
                          << timer.nsecsElapsed() / 1e6;
}

void TerrainMapLayer::pushRenderItems(RenderQueue &queue, const MapView &view,
                                      const MapPalette &palette, TerrainLib *terrain,
                                      bool faded) {
    if (terrain == nullptr)
        return;
    const float scale = view.metresPerPixel / std::max(builtMetresPerPixel, 1e-6f);
    int tiles[4];
    view.visibleTiles(tiles[0], tiles[1], tiles[2], tiles[3], 0);
    const bool rebuild = !valid || view.tileX != builtTileX || view.tileZ != builtTileZ
            || scale > RebuildScale || scale < 1.0f / RebuildScale
            || drawsDetailedPatches(view) != builtDetailed
            || drawsProcedural(view) != builtProcedural || palette.name != builtPalette
            || tiles[0] < builtTiles[0] || tiles[1] > builtTiles[1] || tiles[2] < builtTiles[2]
            || tiles[3] > builtTiles[3];
    if (rebuild)
        build(view, palette, terrain);
    // Textures still loading are left out rather than drawn as missing.
    auto push = [&queue](std::vector<Group> &groups) {
        for (Group &group : groups) {
            auto found = TexLib::mtex.find(group.texture);
            if (found != TexLib::mtex.end() && found->second != nullptr && found->second->loaded)
                group.object->pushRenderItem(queue);
        }
    };
    push(distant);
    distantBorders->pushRenderItem(queue);
    push(detailed);
    for (Procedural &tile : procedural)
        tile.square->push(queue, terrain->getTerrainByXY(tile.tileX, tile.tileZ, false));
    borders->pushRenderItem(queue);
    if (faded && palette.terrainFade > 0.0f) {
        // The ground in view, wound as the patches are.
        const float screen[4][2] = {{0, 0}, {float(view.width), 0},
                                    {float(view.width), float(view.height)},
                                    {0, float(view.height)}};
        float ground[4][2];
        for (int i = 0; i < 4; ++i)
            view.groundAt(screen[i][0], screen[i][1], ground[i][0], ground[i][1]);
        std::vector<float> square;
        for (int corner : {0, 3, 2, 0, 2, 1})
            square.insert(square.end(), {ground[corner][0], FadeHeight, ground[corner][1]});
        fade->setMaterial(float(palette.background.redF()), float(palette.background.greenF()),
                          float(palette.background.blueF()), palette.terrainFade);
        fade->init(square.data(), int(square.size()), RenderItem::V, GL_TRIANGLES);
        fade->pushRenderItem(queue);
    }
}
