/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef EMISSIVEEMITTERS_H
#define EMISSIVEEMITTERS_H

#include <QImage>
#include <QVector>
#include <tsre/renderer/RenderItem.h>

// Lights standing in for an emissive surface (task 21). Its triangles are
// binned on a grid of cells about a quarter of the surface's size (at least
// MinCell); each cell with emitted light becomes one emitter at the centroid
// of its emission, carrying its power: a surface of radiance M and area A
// gives intensity M A / (4 pi), as a sphere of that area and radiance.
namespace EmissiveEmitters {

constexpr float MinCell = 0.5f;
constexpr int MaxEmitters = 64;

struct Surface {
    // Triangle list: three vertices per triangle, `stride` floats each,
    // position first, texture coordinates at uvOffset.
    const float *vertices = nullptr;
    int vertexCount = 0;
    int stride = 3;
    int uvOffset = -1;
    // Emitted radiance in linear colour (emissive factor times strength).
    float emissive[3] = {0.0f, 0.0f, 0.0f};
    // Emissive map (sRGB) sampled at each triangle's centre; null for none.
    const QImage *map = nullptr;
    // Texture coordinate transform as rows of a 2 x 3 matrix.
    float uvTransform[6] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
};

// Emitters in the surface's space, at most maxEmitters (coarser cells when
// there would be more). None when nothing is emitted.
QVector<RenderItem::Light> extract(const Surface &surface, int maxEmitters = MaxEmitters);

}

#endif
