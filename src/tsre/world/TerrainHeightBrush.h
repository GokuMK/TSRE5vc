#ifndef TERRAINHEIGHTBRUSH_H
#define TERRAINHEIGHTBRUSH_H

#include <QSet>
#include <cstddef>
class Terrain;
class TerrainLibQt;
class Brush;

// DirectSlices is the editor path. HeightBuffer is a working example of the
// reusable TerrainHeightArea get/edit/set API, retained for comparison tests.
namespace TerrainHeightBrush {
enum class Method { DirectSlices, HeightBuffer };
struct Timings {
    // For HeightBuffer, scatterMs includes setArea's commit/refresh as well;
    // commitMs then measures disposal. DirectSlices reports commit separately.
    double prepareMs = 0, gatherMs = 0, editMs = 0, scatterMs = 0, commitMs = 0;
    std::size_t bufferBytes = 0;
    bool fallback = false;
};
QSet<Terrain*> paint(TerrainLibQt &library, Brush &brush, int worldX, int worldZ,
                     float *position, Method method, bool allowMixedResolution = false,
                     Timings *timings = nullptr);
}

#endif
