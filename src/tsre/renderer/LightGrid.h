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
// camera (task 21): 16 m cells reaching 4 km from it, moved in Step's. Each
// cell lists the lights whose range reaches it, the brightest first. The
// cells are columns of every height (CellsY 1: the world is one level, task
// 45); more layers would split them by height, each layer 1 MB more. The
// result is laid out as the shaders read it from float textures.
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
        // A signal light's emitter (task 26).
        bool signal = false;
        // A glowing surface's emitter (EmissiveEmitters), or on something
        // moving (a train): no cached shadow map (task 46), a moving lamp's
        // drawn each frame (task 46 A).
        bool emissive = false;
        bool moving = false;
        // The car it is on (Eng, RenderQueue::setLampStatic), null for none:
        // a moving lamp's map is known by it and leaves it out.
        const void *owner = nullptr;
        // Its static shadow map (task 46): the layer of the lamps' map array
        // (-1 none) and the matrix its depth was drawn with (OpenGL clip
        // space, column-major, submission space).
        int shadowLayer = -1;
        // Its map's set: 0 the fine maps, 1 the coarse ones.
        int shadowSet = 0;
        float shadowMatrix[16] = {};
    };

    static constexpr int CellsX = 512;
    static constexpr int CellsY = 1;
    static constexpr int CellsZ = 512;
    // Every cell's width: the same at any distance (task 45: one far light
    // made the cells around the camera 4 to 8 times wider when they spanned
    // the lights).
    static constexpr float CellSize = 16.0f;
    // Edge to edge (8 km: 4 km from the camera), and the heights around it.
    static constexpr float MaxExtent = CellsX * CellSize;
    static constexpr float MaxHeight = 2048.0f;
    // The grid follows the camera in steps of this (static lights keep their
    // cells in between).
    static constexpr float Step = 256.0f;
    static constexpr int MaxLightsPerCell = 64;
    // Texture widths of the cell and index data; the cells x + z * CellsX +
    // y * CellsX * CellsZ, CellsWidth a row.
    static constexpr int CellsWidth = 4096;
    static constexpr int CellRows = CellsX * CellsZ * CellsY / CellsWidth;
    static constexpr int IndexWidth = 4096;
    static constexpr int MaxLights = 65536;
    static constexpr int MaxIndices = IndexWidth * 1024;

    // Where a light of this brightness (colour times intensity, largest
    // channel) fades under 1/512 of white, limited to maxRange.
    static float rangeFor(float brightness, float radius, float maxRange = 250.0f);
    // The reach of every light unless its object says otherwise: strong
    // lamps are already faint there, weak ones fade sooner anyway. (With
    // 1/512 alone, 49 strong lamps underground reached 175 m each, up to 58
    // in a cell; at 30 m, 32, about 20 ms less at 1080p, nothing visible;
    // task renderer 27.)
    static constexpr float DefaultReach = 30.0f;
    // The longest reach an object may ask for.
    static constexpr float MaxReach = 250.0f;

    // Bins the lights around eye. Lights whose range does not reach the
    // grid are left out.
    void build(const std::vector<Light> &lights, const float *eye);

    // Texels a light (its data's width).
    static constexpr int LightTexels = 8;
    int lightCount() const { return int(lightTexels.size() / (LightTexels * 4)); }
    bool empty() const { return lightCount() == 0; }
    // Grid origin (minimum corner) and cell sizes (x and z share one); the
    // layers span the lights' heights.
    float origin[3] = {0.0f, 0.0f, 0.0f};
    float horizontalCell = CellSize;
    float verticalCell = CellSize;

    // Eight RGBA texels per light, one light per row: position and range;
    // colour and squared radius; spot direction and spot flag; cosine of
    // the outer angle, 1 / (cos inner - cos outer), the shadow layer plus 1
    // (0 none) and its set; then the shadow matrix's four columns (task 46).
    std::vector<float> lightTexels;
    // One R32F texel per cell: its first index and count packed into the
    // float's bits (packCell).
    std::vector<float> cellTexels;
    // Light indices, IndexWidth per row.
    std::vector<float> indexTexels;
    // Index of a cell, -1 outside the grid.
    int cellAt(const float *position) const;
    // A cell's first index (under 2^23) and count (under 128) in one float's
    // bits, as the shaders read them (floatBitsToUint): the count in bits 0
    // to 6, the index in 7 to 29, bit 30 set so the float is an ordinary
    // positive number (no denormal or NaN a texture fetch might change).
    // 0 for an empty cell.
    static float packCell(int first, int count);
    static void unpackCell(float cell, int &first, int &count);
    // The cells' lamp lists, for traces (task 45): cells holding any, the
    // longest list, the mean of those holding any, the list at a position.
    struct Stats {
        int cells = 0;
        int longest = 0;
        double mean = 0.0;
        int at = 0;
    };
    Stats stats(const float *position) const;
    // What an upload of the grid sends, in bytes.
    long long uploadBytes() const;
};

#endif
