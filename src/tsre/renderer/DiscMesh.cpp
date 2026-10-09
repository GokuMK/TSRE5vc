/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/renderer/DiscMesh.h>
#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/RenderContext.h>
#include <cmath>

namespace DiscMesh {

void appendVertex(std::vector<float> &out, RenderItem::VertexAttr layout, float x, float y) {
    out.insert(out.end(), {x, y, 0.0f});
    if (layout != RenderItem::PBR)
        return;
    out.insert(out.end(), {0.0f, 0.0f, -1.0f, 0.5f + 0.5f * x, 0.5f - 0.5f * y, 1.0f,
                           1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f});
}

MeshHandle shared(RenderItem::VertexAttr layout) {
    static MeshHandle meshes[2];
    const bool pbr = layout == RenderItem::PBR;
    MeshHandle &mesh = meshes[pbr ? 1 : 0];
    if (mesh.valid() || !RenderContext::ready())
        return mesh;
    const RenderItem::VertexAttr made = pbr ? RenderItem::PBR : RenderItem::V;
    std::vector<float> v;
    v.reserve(size_t(Vertices) * made);
    for (int i = 0; i < Segments; ++i) {
        const float a = float(2.0 * M_PI * i / Segments);
        const float b = float(2.0 * M_PI * (i + 1) / Segments);
        appendVertex(v, made, 0.0f, 0.0f);
        appendVertex(v, made, std::cos(a), std::sin(a));
        appendVertex(v, made, std::cos(b), std::sin(b));
    }
    MeshData data;
    data.layout = made;
    data.vertices = std::move(v);
    mesh = Meshes::create(std::move(data));
    return mesh;
}

}
