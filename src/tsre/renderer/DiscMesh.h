/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef DISCMESH_H
#define DISCMESH_H

#include <tsre/renderer/MeshHandle.h>
#include <tsre/renderer/RenderItem.h>
#include <vector>

// Flat discs of radius 1 in the xy plane, facing -z, as triangles: signal
// lights (task 26), the sun and the moon (task editor 05). PBR layout
// (position, normal, texture coordinates, alpha, tangent, second texture
// coordinates, colour) for emissive packets, positions only (V) for plain
// ones.
namespace DiscMesh {
constexpr int Segments = 24;
constexpr int Vertices = Segments * 3;
// The shared disc of the layout (PBR or V), made once; invalid while no
// renderer can take meshes yet.
MeshHandle shared(RenderItem::VertexAttr layout);
// Appends a vertex at (x, y) in the disc's plane.
void appendVertex(std::vector<float> &out, RenderItem::VertexAttr layout, float x, float y);
}

#endif
