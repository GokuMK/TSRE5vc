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
#include <tsre/world/QuadTree.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/Game.h>
#include <QFileInfo>
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
    : borders(std::make_unique<OglObj>()), quadLines(std::make_unique<OglObj>()),
      missing(std::make_unique<OglObj>()), highlight(std::make_unique<OglObj>()),
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
    overlays.clear();
    tileFiles.clear();
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
    std::vector<float> vertices;
    appendTileSquare(vertices, tile, view, ProceduralHeight);
    Procedural square;
    square.tileX = tileX;
    square.tileZ = tileZ;
    square.square = std::make_unique<ProceduralSquare>();
    square.square->init(vertices.data(), int(vertices.size()), RenderItem::VT, GL_TRIANGLES);
    return square;
}

void TerrainMapLayer::appendTileSquare(std::vector<float> &out, Terrain *tile,
                                       const MapView &view, float y) {
    const float size = float(tile->getGridLayout().terrainWorldSize);
    const float x0 = (tile->mojex - view.tileX) * 2048.0f - 1024.0f;
    const float z0 = (tile->mojez - view.tileZ) * 2048.0f + 1024.0f - size;
    const float corners[16] = {x0, z0, 0, 0,  x0 + size, z0, 1, 0,
                               x0 + size, z0 + size, 1, 1,  x0, z0 + size, 0, 1};
    appendPatch(out, corners, y);
}

void TerrainMapLayer::pushQuadHighlight(RenderQueue &queue, const MapView &view,
                                        const MapPalette &palette, int x, int y, int level) {
    TerrainInfo quad;
    quad.cx = x;
    quad.cy = y;
    quad.level = level;
    std::vector<float> outline;
    appendOutline(outline, quad, view, HighlightHeight, HighlightPixels * view.metresPerPixel);
    setColour(*highlight, palette.selection);
    highlight->init(outline.data(), int(outline.size()), RenderItem::V, GL_TRIANGLES);
    highlight->pushRenderItem(queue);
}

void TerrainMapLayer::appendFill(std::vector<float> &out, const TerrainInfo &info,
                                  const MapView &view, float y) {
    const float size = std::max(info.level, 1) * 2048.0f;
    const float x0 = (info.cx - view.tileX) * 2048.0f - 1024.0f;
    const float z0 = (-info.cy - view.tileZ) * 2048.0f + 1024.0f - size;
    const float corners[4][2] = {{x0, z0}, {x0 + size, z0}, {x0 + size, z0 + size},
                                 {x0, z0 + size}};
    // Wound as the patches are.
    for (int corner : {0, 3, 2, 0, 2, 1})
        out.insert(out.end(), {corners[corner][0], y, corners[corner][1]});
}

bool TerrainMapLayer::tileFileExists(const TerrainInfo &info) {
    auto found = tileFiles.constFind(info.name);
    if (found != tileFiles.constEnd())
        return found.value();
    static const char *const directories[2] = {"TILES", "LO_TILES"};
    const QString path = ContentPath::normalize(Game::root + "/ROUTES/" + Game::route + "/"
            + directories[info.low ? 1 : 0] + "/" + info.name + ".t");
    const bool exists = QFileInfo::exists(path);
    tileFiles.insert(info.name, exists);
    return exists;
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
    // The tiles being edited (the current tree) are the main layer: the
    // detailed tiles with the distant terrain as a background, or the
    // distant tiles alone, as the 3D view draws them.
    const bool distantMode = terrain->distantIsCurrent();
    const bool patches = distantMode || drawsDetailedPatches(view);
    const bool gpu = drawsProcedural(view);
    procedural.clear();
    overlayPending = false;
    int tiles[4];
    view.visibleTiles(tiles[0], tiles[1], tiles[2], tiles[3], 1);
    const float borderWidth = BorderPixels * view.metresPerPixel;

    QHash<int, std::vector<float>> distantPatches, detailedPatches, overlayTiles;
    std::vector<float> outlines;
    QSet<unsigned int> seenDistant, seenDetailed;
    int distantTiles = 0, detailedTiles = 0;
    TerrainInfo info;
    for (int x = tiles[0]; x <= tiles[1]; ++x)
        for (int z = tiles[2]; z <= tiles[3]; ++z) {
            // Editing detailed tiles: the distant terrain as a background,
            // without borders.
            const unsigned int distantId = distantMode ? 0 : terrain->terrainTileId(x, z, true);
            if (distantId != 0 && !seenDistant.contains(distantId)) {
                seenDistant.insert(distantId);
                Terrain *tile = terrain->getDistantDescriptor(x, z);
                if (tile != nullptr && tile->descriptorLoaded) {
                    appendTile(tile, view, DistantHeight, distantPatches);
                    ++distantTiles;
                }
            }
            const unsigned int detailedId = terrain->terrainTileId(x, z, distantMode);
            if (detailedId == 0 || seenDetailed.contains(detailedId))
                continue;
            seenDetailed.insert(detailedId);
            // Wider views: detailed tiles as borders only, nothing loaded.
            if (!patches)
                continue;
            Terrain *tile = distantMode ? terrain->getDistantDescriptor(x, z)
                                        : terrain->getTerrainDescriptor(x, z);
            if (tile != nullptr && tile->descriptorLoaded) {
                appendTile(tile, view, DetailedHeight, detailedPatches);
                ++detailedTiles;
                if (tile->loaded && tile->showBlob) {
                    const int overlay = tile->mapOverlayTexture();
                    if (overlay >= 0)
                        appendTileSquare(overlayTiles[overlay], tile, view, OverlayHeight);
                    else
                        overlayPending = true;
                }
                // Close: procedural tiles complete (through the current
                // tree, as edits use them), shaded over their bake.
                if (gpu && tile->usesProceduralMaterial()) {
                    Terrain *complete = terrain->getTerrainByXY(x, z, true);
                    if (complete != nullptr && complete->loaded)
                        procedural.push_back(proceduralSquare(complete, view, x, z));
                }
            }
        }

    // The quadtree of the tiles being edited: thin lines around its nodes,
    // tile borders, and a tint where a populated tile has no file.
    std::vector<float> nodeLines, missingTiles;
    int nodes = 0, missingCount = 0;
    if (QuadTree *tree = distantMode ? terrain->getQuadTreeDistant()
                                     : terrain->getQuadTreeDetailed()) {
        const float quadWidth = QuadPixels * view.metresPerPixel;
        QuadTree::Visitor visitor;
        visitor.node = [&](int x, int y, int size) {
            TerrainInfo node;
            node.cx = x;
            node.cy = y;
            node.level = size;
            appendOutline(nodeLines, node, view, QuadHeight, quadWidth);
            ++nodes;
        };
        visitor.tile = [&](const TerrainInfo &tile) {
            if (tileFileExists(tile)) {
                appendOutline(outlines, tile, view, BorderHeight, borderWidth);
            } else {
                appendFill(missingTiles, tile, view, MissingHeight);
                ++missingCount;
            }
        };
        // Tree coordinates count tiles northwards: z negated.
        tree->visit(tiles[0], tiles[1], -tiles[3], -tiles[2], visitor);
    }
    setColour(*quadLines, palette.quadBorder);
    quadLines->init(nodeLines.data(), int(nodeLines.size()), RenderItem::V, GL_TRIANGLES);
    missing->setMaterial(float(palette.missingTile.redF()), float(palette.missingTile.greenF()),
                         float(palette.missingTile.blueF()), MissingAlpha);
    missing->init(missingTiles.data(), int(missingTiles.size()), RenderItem::V, GL_TRIANGLES);

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
    groups(overlays, overlayTiles);
    setColour(*borders, palette.terrainBorder);
    borders->init(outlines.data(), int(outlines.size()), RenderItem::V, GL_TRIANGLES);

    valid = true;
    builtTileX = view.tileX;
    builtTileZ = view.tileZ;
    builtMetresPerPixel = view.metresPerPixel;
    builtDetailed = drawsDetailedPatches(view);
    builtProcedural = gpu;
    builtDistantMode = distantMode;
    std::copy(tiles, tiles + 4, builtTiles);
    builtPalette = palette.name;
    if (trace)
        qInfo().noquote() << "map-trace terrain" << (distantMode ? "distant mode" : "detailed mode")
                          << (patches ? "patches" : "borders") << "m/px"
                          << view.metresPerPixel << "distant tiles" << seenDistant.size()
                          << "loaded" << distantTiles << "detailed tiles" << seenDetailed.size()
                          << "loaded" << detailedTiles << "procedural" << procedural.size() << "quad nodes" << nodes << "missing"
                          << missingCount
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
    const bool rebuild = !valid || overlayPending || view.tileX != builtTileX
            || view.tileZ != builtTileZ
            || scale > RebuildScale || scale < 1.0f / RebuildScale
            || drawsDetailedPatches(view) != builtDetailed
            || drawsProcedural(view) != builtProcedural || palette.name != builtPalette
            || terrain->distantIsCurrent() != builtDistantMode
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
    push(detailed);
    for (Procedural &tile : procedural)
        tile.square->push(queue, terrain->getTerrainByXY(tile.tileX, tile.tileZ, false));
    push(overlays);
    missing->pushRenderItem(queue);
    quadLines->pushRenderItem(queue);
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
