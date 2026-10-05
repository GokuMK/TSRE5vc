/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MESH_H
#define MESH_H

#include <QtGlobal>
#include <QByteArray>
#include <vector>
#include <tsre/renderer/MeshHandle.h>
#include <tsre/renderer/RenderItem.h>

class QOpenGLFunctions;

// Vertex and index data a producer hands to the renderer, interleaved in one
// of the RenderItem layouts. Without indices the mesh draws vertex ranges.
struct MeshData {
    RenderItem::VertexAttr layout = RenderItem::NO_ATTR;
    std::vector<float> vertices;
    QByteArray indices;
    RenderItem::IndexType indexType = RenderItem::INDEX_U16;
};

namespace Meshes {
    // Takes the data; the renderer uploads it before the mesh is first drawn.
    MeshHandle create(MeshData data);
    // Replaces the mesh's data; an invalid handle creates a new mesh.
    void update(MeshHandle &handle, MeshData data);
    // Frees the mesh and resets the handle. The GL objects go on the GL thread.
    void release(MeshHandle &handle);

    // Renderer side, GL thread with a context of the share group current.
    struct Buffers {
        unsigned int vertexBuffer = 0;
        unsigned int indexBuffer = 0;
        RenderItem::VertexAttr layout = RenderItem::NO_ATTR;
        // Changes whenever the buffers or the layout change, so per-context
        // vertex arrays know to rebuild.
        quint64 stamp = 0;
    };
    // Uploads pending data and returns the mesh's buffers; false for a
    // released or empty mesh.
    bool prepare(MeshHandle handle, QOpenGLFunctions *f, Buffers &buffers);
    // Whether the handle still names a live mesh.
    bool alive(MeshHandle handle);
    // Deletes the buffers of released meshes.
    void collectGarbage(QOpenGLFunctions *f);
    // Changes whenever a mesh is released, so renderers know when to drop
    // vertex arrays of released meshes.
    quint64 releaseCount();
    // Enables the vertex attributes of a layout on the bound vertex array,
    // reading from the bound array buffer.
    void setupAttributes(QOpenGLFunctions *f, RenderItem::VertexAttr layout);
}

#endif // MESH_H
