#include <tsre/world/TerrainNormals.h>
#include <algorithm>
#include <cmath>

namespace {
struct Vec3 {
    float x;
    float y;
    float z;
};

TerrainNormals::Normal normalize(const Vec3 &sum) {
    const float length = std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);
    if (length > 0.0f)
        return {sum.x / length, sum.y / length, sum.z / length};
    return {0.0f, 1.0f, 0.0f};
}
int packedOptimized(float value) {
    value = std::max(-1.0f, std::min(1.0f, value));
    const float scaled = value * 511.0f; // keep the original float multiplication
    // Exact lround semantics on the clamped [-511,511] domain. The addition
    // MUST be double: float + 0.5 can prematurely round just below a half tie.
    return static_cast<int>(double(scaled) + (scaled >= 0.0f ? 0.5 : -0.5));
}
std::uint32_t pack(int x, int y, int z, bool gap) {
    return (std::uint32_t(x) & 0x3ffu) | ((std::uint32_t(y) & 0x3ffu) << 10)
            | ((std::uint32_t(z) & 0x3ffu) << 20) | (gap ? 1u << 30 : 0u);
}
}

std::uint32_t TerrainNormals::packNormal(float x, float y, float z, bool gap) {
    return pack(packedOptimized(x), packedOptimized(y), packedOptimized(z), gap);
}

bool TerrainNormals::uniformCoordinates(int samples, float spacing) {
    int exponent;
    // Powers of two keep grid-coordinate subtraction and triangle area exact.
    // Other accepted layouts use their actual coordinate differences.
    return samples > 0 && samples <= 2048 && spacing > 0 && spacing <= 65536
            && std::frexp(spacing, &exponent) == 0.5f;
}
namespace {
// Actual grid-coordinate differences also handle non-power-of-two spacing.
// Include only triangles that exist at an outer tile edge, in mesh order.
TerrainNormals::Normal boundaryNormal(float *const *h, int n, float s, int x, int z) {
    Vec3 sum{0, 0, 0};
    const float centre = h[z][x];
    const float left = x * s - (x - 1) * s, right = (x + 1) * s - x * s;
    const float north = z * s - (z - 1) * s, south = (z + 1) * s - z * s;
    auto add = [&](float dx, float dz, float width, float depth) {
        sum.x += dx * depth;
        sum.y += depth * width;
        sum.z += dz * width;
    };
    if (x > 0 && z > 0)
        add(h[z][x - 1] - centre, h[z - 1][x] - centre, left, north);
    if (x < n && z > 0) {
        add(h[z - 1][x] - h[z - 1][x + 1], h[z - 1][x] - centre, right, north);
        add(centre - h[z][x + 1], h[z - 1][x + 1] - h[z][x + 1], right, north);
    }
    if (x > 0 && z < n) {
        add(h[z][x - 1] - centre, h[z][x - 1] - h[z + 1][x - 1], left, south);
        add(h[z + 1][x - 1] - h[z + 1][x], centre - h[z + 1][x], left, south);
    }
    if (x < n && z < n)
        add(centre - h[z][x + 1], centre - h[z + 1][x], right, south);
    return normalize(sum);
}
}

TerrainNormals::Normal TerrainNormals::calculate(float *const *heights,
        int samples, float spacing, int x, int z, bool uniformGrid) {
    if (!uniformGrid || x == 0 || z == 0 || x == samples || z == samples)
        return boundaryNormal(heights, samples, spacing, x, z);
    const float centre = heights[z][x];
    const float west = heights[z][x - 1], east = heights[z][x + 1];
    const float north = heights[z - 1][x], northEast = heights[z - 1][x + 1];
    const float south = heights[z + 1][x], southWest = heights[z + 1][x - 1];

    const float wx = (west - centre) * spacing;
    const float nx = (north - northEast) * spacing;
    const float ex = (centre - east) * spacing;
    const float sx = (southWest - south) * spacing;
    const float nz = (north - centre) * spacing;
    const float ez = (northEast - east) * spacing;
    const float wz = (west - southWest) * spacing;
    const float sz = (centre - south) * spacing;

    // Preserve the six mesh triangle contributions and their accumulation order:
    // NW second, NE first/second, SW first/second, SE first. Do not collapse
    // these sums into a central difference or reassociate them with fast-math.
    Vec3 sum{0.0f, 0.0f, 0.0f};
    sum.x += wx; sum.x += nx; sum.x += ex;
    sum.x += wx; sum.x += sx; sum.x += ex;
    sum.z += nz; sum.z += nz; sum.z += ez;
    sum.z += wz; sum.z += sz; sum.z += sz;
    const float area = spacing * spacing;
    for (int triangle = 0; triangle < 6; ++triangle) sum.y += area;
    const float length = std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);
    if (!std::isfinite(length))
        return boundaryNormal(heights, samples, spacing, x, z);
    if (length > 0.0f)
        return {sum.x / length, sum.y / length, sum.z / length};
    return {0.0f, 1.0f, 0.0f};
}
