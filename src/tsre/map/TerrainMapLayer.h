/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TERRAINMAPLAYER_H
#define TERRAINMAPLAYER_H

#include <QHash>
#include <QString>
#include <memory>
#include <vector>

class MapView;
class OglObj;
class RenderQueue;
class Terrain;
class TerrainInfo;
class TerrainLib;
struct MapPalette;

// Terrain in map mode (task editor 04, "7. Terrain"): flat textured squares
// under every other layer, by how much ground the view shows. The tiles
// being edited (TerrainLib's current tree) are the main layer, with
// borders, as the 3D view draws them:
// - editing detailed tiles, the distant terrain (LO tiles) is a background
//   without borders, and views wider than DetailedExtentMetres show the
//   detailed tiles as borders only, loading nothing for them;
// - editing distant tiles, they are the main layer at every zoom, without
//   a background or detailed tiles.
// The quadtree of the tiles being edited shows as thin lines around every
// node's quadrants; tiles with their file have the tile border, populated tiles
// without one a light red tint.
// - Narrower views show each detailed patch as a square with its texture
//   and placement from the tile file (procedural tiles: their baked
//   fallback, which their patches reference). Only tile files are read
//   (Terrain::descriptorLoaded), never heights.
// - Views narrower than ProceduralExtentMetres (the track objects' bound)
//   complete the procedural tiles in view, as the 3D view loads them, and
//   draw each as one square with the 3D view's direct GPU material shading
//   over its bake.
class TerrainMapLayer {
public:
    static constexpr float DetailedExtentMetres = 16384.0f;
    static constexpr float ProceduralExtentMetres = 3.0f * 2048.0f;
    // Under the roads (TrackMapLayer: 100).
    static constexpr float DistantHeight = 10.0f;
    static constexpr float DetailedHeight = 20.0f;
    static constexpr float ProceduralHeight = 25.0f;
    // A tile's map texture shown by the geo tools, over its terrain.
    static constexpr float OverlayHeight = 35.0f;
    // OSM data lies between the textures and the terrain aids below
    // (50 to 79), so the quadtree and tile tools stay usable over it.
    // The quadtree of the tiles being edited: every node's quadrants as thin
    // lines, a populated tile whose file is missing tinted.
    static constexpr float MissingHeight = 81.0f;
    static constexpr float QuadHeight = 83.0f;
    static constexpr float BorderHeight = 85.0f;
    static constexpr float QuadPixels = 1.0f;
    static constexpr float MissingAlpha = 0.35f;
    static constexpr float HighlightHeight = 87.0f;
    static constexpr float HighlightPixels = 3.0f;
    static constexpr float BorderPixels = 1.0f;

    TerrainMapLayer();
    ~TerrainMapLayer();
    void pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                         TerrainLib *terrain);
    // Tiles or textures changed: everything is read again.
    void invalidate();
    // Builds the geometry again on the next draw (an edit changed patches).
    void rebuild() { valid = false; }
    // An outline around a quad of the tree (the quadtree tool's quad under
    // the pointer): corner x, y and size level in world tiles, tree
    // coordinates.
    void pushQuadHighlight(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                           int x, int y, int level);

    // Whether a view shows the detailed tiles' patches, and procedural
    // tiles with their GPU material shading.
    static bool drawsDetailedPatches(const MapView &view);
    static bool drawsProcedural(const MapView &view);
    // A patch's two triangles from Terrain::mapPatchCorners, as VT vertices
    // (x, y, z, u, v, alpha), wound for the map's face culling. Public for
    // tests.
    static void appendPatch(std::vector<float> &out, const float *corners, float y);

private:
    struct Group {
        int texture = -1;
        std::unique_ptr<OglObj> object;
    };
    class ProceduralSquare;
    struct Procedural {
        int tileX = 0;
        int tileZ = 0;
        std::unique_ptr<ProceduralSquare> square;
    };
    void build(const MapView &view, const MapPalette &palette, TerrainLib *terrain);
    void appendTile(Terrain *tile, const MapView &view, float y,
                    QHash<int, std::vector<float>> &byTexture);
    static Procedural proceduralSquare(Terrain *tile, const MapView &view, int tileX, int tileZ);
    // The tile as one square in sample order, its texture coordinates
    // spanning it (procedural shading, map overlays).
    static void appendTileSquare(std::vector<float> &out, Terrain *tile, const MapView &view,
                                 float y);
    static void appendOutline(std::vector<float> &out, const TerrainInfo &info,
                              const MapView &view, float y, float width);
    static void appendFill(std::vector<float> &out, const TerrainInfo &info, const MapView &view,
                           float y);
    // Whether a populated tile's file exists (cached by name).
    bool tileFileExists(const TerrainInfo &info);

    bool valid = false;
    int builtTileX = 0;
    int builtTileZ = 0;
    float builtMetresPerPixel = 0.0f;
    bool builtDetailed = false;
    bool builtProcedural = false;
    bool builtDistantMode = false;
    // A shown map overlay whose texture was still loading: build again.
    bool overlayPending = false;
    int builtTiles[4] = {0, 0, 0, 0};
    QString builtPalette;
    std::vector<Group> distant;
    std::vector<Group> detailed;
    std::vector<Procedural> procedural;
    std::vector<Group> overlays;
    std::unique_ptr<OglObj> borders;
    std::unique_ptr<OglObj> quadLines;
    std::unique_ptr<OglObj> missing;
    std::unique_ptr<OglObj> highlight;
    QHash<QString, bool> tileFiles;
};

#endif
