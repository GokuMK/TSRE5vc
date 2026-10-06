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
class QRhi;
class QRhiBuffer;
class QRhiTexture;
class QRhiResourceUpdateBatch;

// Data a producer hands to the renderer. Without indices the mesh draws
// vertex ranges.
struct MeshData {
    // How the renderer reads the vertices. FloatLayout interleaves floats in
    // `layout` (V, VT, VNT or VNTA). TerrainHeightNormal is the paged
    // terrain vertex: a float height and a packed 2_10_10_10 normal, 8 bytes.
    // Buffer is plain storage without vertex attributes, for uniform blocks
    // or an index buffer other meshes share.
    enum Format : unsigned char { FloatLayout = 0, TerrainHeightNormal, Buffer };
    Format format = FloatLayout;
    RenderItem::VertexAttr layout = RenderItem::NO_ATTR;
    // FloatLayout vertices; the other formats use `bytes`.
    std::vector<float> vertices;
    QByteArray bytes;
    QByteArray indices;
    RenderItem::IndexType indexType = RenderItem::INDEX_U16;
    // Draw with this mesh's storage as the index buffer instead of `indices`.
    MeshHandle sharedIndices;
    // Ranges are rewritten often (terrain editing).
    bool dynamic = false;
};

namespace Meshes {
    // Takes the data; the renderer uploads it before the mesh is first drawn.
    MeshHandle create(MeshData data);
    // Replaces the mesh's data; an invalid handle creates a new mesh.
    void update(MeshHandle &handle, MeshData data);
    // Rewrites bytes of the mesh's vertex data (or plain storage) in place,
    // uploaded before the next draw. False for a dead mesh or a range outside
    // the data.
    bool updateRange(MeshHandle handle, int byteOffset, const void *data, int bytes);
    // Frees the mesh and resets the handle. The GL objects go on the GL thread.
    void release(MeshHandle &handle);

    // Renderer side, GL thread with a context of the share group current.
    struct Buffers {
        unsigned int vertexBuffer = 0;
        unsigned int indexBuffer = 0;
        MeshData::Format format = MeshData::FloatLayout;
        RenderItem::VertexAttr layout = RenderItem::NO_ATTR;
        // Changes whenever the buffers, the format or the layout change, so
        // per-context vertex arrays know to rebuild.
        quint64 stamp = 0;
    };
    // Uploads pending data and ranges and returns the mesh's buffers; false
    // for a released mesh.
    bool prepare(MeshHandle handle, QOpenGLFunctions *f, Buffers &buffers);
    // Whether the handle still names a live mesh.
    bool alive(MeshHandle handle);
    // Deletes the buffers of released meshes.
    void collectGarbage(QOpenGLFunctions *f);
    // Changes whenever a mesh is released, so renderers know when to drop
    // vertex arrays of released meshes.
    quint64 releaseCount();
    // Enables the vertex attributes of a mesh on the bound vertex array,
    // reading from the bound array buffer.
    void setupAttributes(QOpenGLFunctions *f, const Buffers &buffers);

    // QRhi renderer side, GUI thread. Meshes live in RhiContext's QRhi.
    struct RhiBuffers {
        QRhiBuffer *vertexBuffer = nullptr;
        QRhiBuffer *indexBuffer = nullptr;
        MeshData::Format format = MeshData::FloatLayout;
        RenderItem::VertexAttr layout = RenderItem::NO_ATTR;
    };
    // Records uploads of pending data and ranges into the batch and returns
    // the mesh's buffers; false for a released mesh.
    bool prepareRhi(MeshHandle handle, QRhi *rhi, QRhiResourceUpdateBatch *batch,
                    RhiBuffers &buffers);
    // A Buffer-format mesh (terrain patch parameters) as a one-row RGBA32F
    // texture, one texel per 16 bytes, for shaders to fetch from.
    QRhiTexture *dataTextureRhi(MeshHandle handle, QRhi *rhi, QRhiResourceUpdateBatch *batch);
    // Releases the QRhi buffers of released meshes.
    void collectGarbageRhi();
    // Releases every QRhi buffer, before the QRhi goes away.
    void releaseAllRhi();
    // The live QRhi buffers' bytes by kind and the uploads since the last
    // call, for TSRE_RHI_TRACE.
    QString rhiTraceSummary();
}

#endif // MESH_H
