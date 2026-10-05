/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RENDERITEM_H
#define RENDERITEM_H

#include <QVector>
#include <QVector3D>
#include <tsre/renderer/MeshHandle.h>

class Vector3f;
class Vector4f;
class QOpenGLBuffer;
class QOpenGLVertexArrayObject;

// One drawable packet: a mesh range drawn with one material. Producers own
// persistent packets and submit them to a RenderQueue each frame.
class RenderItem {
public:
    enum VertexAttr {NO_ATTR = 0, V = 3, VT = 6, VNT = 8, VNTA = 9};
    // How the renderer routes a packet: opaque and alpha-tested packets are
    // batched by texture, blended packets are drawn back to front.
    enum Surface {SURFACE_OPAQUE = 0, SURFACE_ALPHA_TEST = 1, SURFACE_BLENDED = 2,
                  SURFACE_TERRAIN = 3};
    enum Primitive : unsigned char {
        PRIMITIVE_TRIANGLES = 0,
        PRIMITIVE_TRIANGLE_STRIP,
        PRIMITIVE_TRIANGLE_FAN,
        PRIMITIVE_LINES,
        PRIMITIVE_LINE_STRIP,
        PRIMITIVE_LINE_LOOP,
        PRIMITIVE_POINTS
    };
    enum IndexType : unsigned char {INDEX_U16 = 0, INDEX_U32};

    // Geometry this packet draws from, and the range of it. Meshes are
    // renderer-owned (handle); producers not yet moved to Meshes still pass
    // their own vertex array and buffer.
    struct Mesh {
        MeshHandle handle;
        QOpenGLVertexArrayObject *vao = nullptr;
        QOpenGLBuffer *vbo = nullptr;
        VertexAttr layout = NO_ATTR;
        Primitive primitive = PRIMITIVE_TRIANGLES;
        // First vertex and vertex count, or index count when indexed.
        unsigned int first = 0;
        unsigned int count = 0;
        bool indexed = false;
        IndexType indexType = INDEX_U16;
        // Byte offset of the first index.
        unsigned int indexOffset = 0;
        int baseVertex = 0;
    };

    // How the surface looks. A backend decides how to draw it.
    struct Material {
        unsigned char surface = SURFACE_OPAQUE;
        // Lit by the sun using the mesh normals.
        bool lit = false;
        bool textured = false;
        // TexLib texture resolved when drawing; -1 uses textureObject. A
        // texture that is not uploaded yet draws in the missing-texture colour.
        int textureId = -1;
        // Backend texture object for producers that resolve textures themselves.
        unsigned int textureObject = 0;
        // Colour of untextured surfaces.
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float brightness = 1.0f;
        // Detail texture blended over the base texture; scale 0 disables it.
        unsigned int detailTextureObject = 0;
        float detailScale = 0.0f;
        // Drawn on terrain at equal depth, without writing depth.
        bool decal = false;
        // Pixels; 0 uses the default.
        int lineWidth = 0;
        bool wireframe = false;
    };

    // Terrain-only parameters; unused by other packets.
    struct Terrain {
        // Paged terrain meshes: vertices are generated from patch parameters.
        bool paged = false;
        QOpenGLBuffer *paramsBuffer = nullptr;
        int verticesPerPatch = 0;
        int patchSide = 0;
        float sampleSpacing = 0.0f;
        bool applyGaps = false;
        bool mapPass = false;
        // Maps tile texture coordinates into a shared bake; zero is identity.
        QVector3D textureRemap;
        // Direct procedural terrain, drawn in one pass: the shader picks each
        // pixel's material from the categorical material map and samples its
        // layer of the material and detail texture arrays. materialParams
        // holds per material id: base layer, detail layer (-1 for none) and
        // detail scale.
        unsigned int materialMap = 0;
        unsigned int materialTextures = 0;
        unsigned int materialDetails = 0;
        unsigned int materialParams = 0;
        QVector3D materialMapRemap;
        int materialMapSide = 0;
        float materialNoiseScale = 0.0f;
    };

    // Sphere enclosing the packet in the space of the submission transform
    // (after msMatrix). A negative radius means unknown.
    struct Bounds {
        float center[3] = {0.0f, 0.0f, 0.0f};
        float radius = -1.0f;
        bool valid() const { return radius >= 0.0f; }
    };

    Mesh mesh;
    Material material;
    Terrain terrain;
    Bounds bounds;
    // Model-space transform applied before the submission transform; null
    // means identity.
    float *msMatrix = nullptr;
    quint32 selectionId = 0;
    // A shared item is owned by its producer even when submitted as a frame
    // item; the queue copies it.
    bool shared = false;
    // RenderStats::Category of the producer that queued this item.
    unsigned char statsCategory = 0;

    RenderItem();
    RenderItem(const RenderItem& orig);
    virtual ~RenderItem();
    // Sets the vertex layout and whether the mesh has normals to light with.
    void setVertexAttributes(VertexAttr attr);
    void disableTextures(Vector3f* color);
    void disableTextures(Vector4f* color);
    void disableTextures(float x, float y, float z, float a);
    void setSelectionId(quint32 selectionId);
    void enableTextures(unsigned int textureObject);
    void enableTextureId(int id);
    // Sets the bounds from a sphere in mesh space, moved into the submission
    // space by transform (normally msMatrix; null is identity). A negative
    // radius clears them.
    void setBounds(const float *center, float radius, const float *transform = nullptr);

    // Conversions from the GL constants some loaders still use.
    static Primitive primitiveFromGl(unsigned int mode);
    static IndexType indexTypeFromGl(unsigned int type);
};

#endif /* RENDERITEM_H */
