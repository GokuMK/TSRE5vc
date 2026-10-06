/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef LIGHTGRID_H
#define LIGHTGRID_H

#include <vector>

// The local lights of a frame binned into a world-space grid around the
// camera (task 21). Each cell lists the lights whose range reaches it, the
// brightest first. The result is laid out as the shaders read it from float
// textures.
class LightGrid {
public:
    // A light in submission space.
    struct Light {
        float position[3] = {0.0f, 0.0f, 0.0f};
        float direction[3] = {0.0f, 0.0f, -1.0f};
        // Colour times intensity, in engine units (see RenderItem::Light).
        float color[3] = {0.0f, 0.0f, 0.0f};
        float range = 0.0f;
        float radius = 0.0f;
        // Spot lights: cosines of the cone half-angles.
        float cosInner = 1.0f;
        float cosOuter = 1.0f;
        bool spot = false;
    };

    static constexpr int CellsX = 64;
    static constexpr int CellsY = 16;
    static constexpr int CellsZ = 64;
    static constexpr float CellSize = 16.0f;
    static constexpr int MaxLightsPerCell = 64;
    // Texture widths of the cell and index data.
    static constexpr int CellsWidth = CellsX * CellsZ;
    static constexpr int IndexWidth = 4096;
    static constexpr int MaxLights = 65536;
    static constexpr int MaxIndices = IndexWidth * 1024;

    // Where a light of this brightness (colour times intensity, largest
    // channel) fades under 1/512 of white, limited to maxRange.
    static float rangeFor(float brightness, float radius, float maxRange = 250.0f);

    // Bins the lights around eye. Lights whose range does not reach the
    // grid are left out.
    void build(const std::vector<Light> &lights, const float *eye);

    int lightCount() const { return int(lightTexels.size() / 16); }
    bool empty() const { return lightCount() == 0; }
    // Grid origin (minimum corner) and cell sizes (x and z share one).
    float origin[3] = {0.0f, 0.0f, 0.0f};
    float horizontalCell = CellSize;
    float verticalCell = CellSize;

    // Four RGBA texels per light, one light per row: position and range;
    // colour and squared radius; spot direction and spot flag; cosine of
    // the outer angle and 1 / (cos inner - cos outer).
    std::vector<float> lightTexels;
    // One RGBA texel per cell (x + z * CellsX across, y down): first index
    // and count.
    std::vector<float> cellTexels;
    // Light indices, IndexWidth per row.
    std::vector<float> indexTexels;
    // Index of a cell, -1 outside the grid.
    int cellAt(const float *position) const;
};

#endif
