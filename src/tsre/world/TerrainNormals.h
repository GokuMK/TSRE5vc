#ifndef TERRAINNORMALS_H
#define TERRAINNORMALS_H
#include <cstdint>

// Pure CPU heightfield normals. No Qt/OpenGL context, allocation or tile lookup.
namespace TerrainNormals {
struct Normal { float x, y, z; };
// Cache once per grid; false uses actual coordinate differences, not rejection.
bool uniformCoordinates(int samples, float spacing);
Normal calculate(float *const *heights, int samples, float spacing, int x, int z, bool uniformGrid);
std::uint32_t packNormal(float x, float y, float z, bool gap);
}
#endif
