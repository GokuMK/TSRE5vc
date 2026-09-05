#ifndef TERRAINHEIGHTAREA_H
#define TERRAINHEIGHTAREA_H

#include <tsre/world/TerrainAdjacentEdge.h>
#include <tsre/world/Terrain.h>
#include <QSet>
#include <algorithm>
#include <climits>
#include <vector>

class TerrainLibQt;

// Synchronous CPU terrain editing. Coordinates are TSRE global metres, not
// MSTS file Z: globalX = worldTileX * 2048 + localX (likewise for Z).
// See docs/features/terrain-height-area.md for ownership and commit semantics.
namespace TerrainHeightArea {
struct Slice {
    Terrain *terrain;
    TerrainPhysicalBounds bounds;
    int x0, z0, x1, z1; // inclusive native sample rectangle, excluding N+1 border
    int dirtyX0 = INT_MAX, dirtyZ0 = INT_MAX, dirtyX1 = -1, dirtyZ1 = -1;
    bool captured = false, touched = false;
    std::vector<unsigned char> patches;
    void capture();
    // Keep per-sample bookkeeping inline for the direct brush hot loop.
    void touch(int x, int z) {
        if (!captured) capture();
        touched = true;
        const auto &g = terrain->getGridLayout();
        const int col = std::min(x / g.patchResolution, g.patchesPerSide - 1);
        const int row = std::min(z / g.patchResolution, g.patchesPerSide - 1);
        patches[row * g.patchesPerSide + col] = 1;
    }
    void changed(int x, int z) {
        dirtyX0 = std::min(dirtyX0, x); dirtyZ0 = std::min(dirtyZ0, z);
        dirtyX1 = std::max(dirtyX1, x); dirtyZ1 = std::max(dirtyZ1, z);
    }
};

struct Area {
    // Treat geometry and slices as read-only after getArea(). Edit heights only.
    qint64 originX = 0, originZ = 0;
    int spacing = 0, width = 0, height = 0;
    std::vector<float> heights; // heights[z * width + x]; NaN means unavailable
    std::vector<Slice> slices;
    // Optional explicit footprint: commit touched native samples even if their
    // height is unchanged (legacy brush ErrorBias/modified-state semantics).
    // Empty by default: only changed finite heights are committed.
    std::vector<unsigned char> touched;
    bool supported = false;
    bool committed = false;
    Area() = default;
    Area(const Area &) = delete;
    Area &operator=(const Area &) = delete;
    Area(Area &&) = default;
    Area &operator=(Area &&) = default;
};

// Inclusive rectangle, rounded outwards to the finest participating grid.
// spacingFilter=0 includes mixed grids; positive values select only that native
// spacing. Only already loaded/editable terrain participates. Unsupported grids
// return supported=false without modifying terrain or rejecting its loading.
Area getArea(TerrainLibQt &library, qint64 minX, qint64 minZ,
             qint64 maxX, qint64 maxZ, int spacingFilter = 0);

// Advanced overload: gather a prepared aligned selection, avoiding rediscovery
// in tools which already need native slices (the brush is an example).
Area getArea(qint64 originX, qint64 originZ, int width, int height, int spacing,
             std::vector<Slice> slices);

// Single-use commit; caller owns the enclosing Undo::StateBegin/StateEnd.
// Does not save route files or perform route-object updates. Calls the library's
// updateTerrainHeightmap hook, retaining implementation-specific client behavior.
QSet<Terrain*> setArea(TerrainLibQt &library, Area &area);

// Shared native-slice bookkeeping for direct tools without a float rectangle.
QSet<Terrain*> commitSlices(TerrainLibQt &library, std::vector<Slice> &slices);
}
#endif
