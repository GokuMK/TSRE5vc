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

// One drawable packet: a mesh range drawn with one material. Producers own
// persistent packets and submit them to a RenderQueue each frame.
class RenderItem {
public:
    // Floats per vertex. PBR: position, normal, texture coordinates, alpha,
    // tangent (xyz, handedness), second texture coordinates, colour (rgba).
    enum VertexAttr {NO_ATTR = 0, V = 3, VT = 6, VNT = 8, VNTA = 9, PBR = 19};
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

    // The renderer-owned mesh this packet draws from, and the range of it.
    struct Mesh {
        MeshHandle handle;
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
        // TextureAlpha::Class of textureObject's texture, from producers that
        // resolve textures themselves (0: unknown). TexLib textures
        // (textureId) carry their own.
        unsigned char textureAlpha = 0;
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
        // Drawn without back-face culling.
        bool doubleSided = false;
    };

    // Metallic-roughness material (glTF), drawn by the PBR program variant.
    // The base colour texture is the material texture; the other maps are
    // TexLib texture ids resolved when drawing, -1 for none. Colours are
    // linear, as in glTF.
    struct Pbr {
        enum Map {MAP_BASE_COLOR = 0, MAP_METALLIC_ROUGHNESS, MAP_NORMAL, MAP_OCCLUSION,
                  MAP_EMISSIVE, MAP_CLEARCOAT, MAP_CLEARCOAT_ROUGHNESS, MAP_CLEARCOAT_NORMAL,
                  MAP_SPECULAR, MAP_SPECULAR_COLOR, MAP_TRANSMISSION, MAP_THICKNESS,
                  MAP_COUNT};
        bool enabled = false;
        // Shaded with the base colour only (KHR_materials_unlit).
        bool unlit = false;
        float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float metallic = 1.0f;
        float roughness = 1.0f;
        float emissive[3] = {0.0f, 0.0f, 0.0f};
        float normalScale = 1.0f;
        float occlusionStrength = 1.0f;
        // Alpha below it is discarded (MASK); negative keeps every pixel.
        float alphaCutoff = -1.0f;
        // Whether alpha is kept for blending (BLEND); otherwise opaque.
        bool blend = false;
        // KHR_materials_clearcoat: layer strength, its roughness and the
        // scale of its normal map; strength 0 has no layer.
        float clearcoat = 0.0f;
        float clearcoatRoughness = 0.0f;
        float clearcoatNormalScale = 1.0f;
        // KHR_materials_specular and KHR_materials_ior: strength and colour of
        // the dielectric reflection, and the index of refraction it starts from.
        float specular = 1.0f;
        float specularColor[3] = {1.0f, 1.0f, 1.0f};
        float ior = 1.5f;
        // KHR_materials_transmission and KHR_materials_volume: the share of
        // light passing through, the thickness of the volume (0 is thin-walled)
        // and its absorption, as the distance after which light keeps the
        // attenuation colour (0 never absorbs).
        float transmission = 0.0f;
        float thickness = 0.0f;
        float attenuationDistance = 0.0f;
        float attenuationColor[3] = {1.0f, 1.0f, 1.0f};
        int textures[MAP_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
        // Texture coordinate set (0 or 1) of each map.
        unsigned char texCoords[MAP_COUNT] = {};
        // KHR_texture_transform of each map: rows of a 2 x 3 matrix applied
        // to the texture coordinates.
        struct UvTransform {
            float rows[6] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
            bool identity() const {
                return rows[0] == 1.0f && rows[1] == 0.0f && rows[2] == 0.0f
                        && rows[3] == 0.0f && rows[4] == 1.0f && rows[5] == 0.0f;
            }
        };
        UvTransform uvTransforms[MAP_COUNT];
        // Wrap mode of each map along s and t as GL enums; 0 repeats.
        unsigned short wrap[MAP_COUNT][2] = {};
    };

    // Shaded water surface, drawn by the water program variant. Its colour
    // is the route's water layers stacked: the material texture is the top
    // layer, over the middle and bottom layers given here.
    struct Water {
        bool enabled = false;
        enum Layer {LAYER_BOTTOM = 0, LAYER_MIDDLE, LAYER_COUNT};
        // Backend texture objects; 0 for none.
        unsigned int layers[LAYER_COUNT] = {0, 0};
    };

    // Terrain-only parameters; unused by other packets.
    struct Terrain {
        // Paged terrain meshes: vertices are generated from patch parameters.
        bool paged = false;
        // Plain storage (MeshData::Buffer) of the patch records: the
        // TerrainPatchBlock in OpenGL, a data texture in QRhi.
        MeshHandle paramsBuffer;
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

    // A light the packet carries, in its model space (before msMatrix).
    // Intensity is in engine units: a white matte surface at distance d
    // facing the light shows intensity / (d^2 + radius^2) times its colour.
    struct Light {
        enum Type {POINT = 0, SPOT};
        float position[3] = {0.0f, 0.0f, 0.0f};
        // Spot lights: the direction the cone points along.
        float direction[3] = {0.0f, 0.0f, -1.0f};
        float color[3] = {1.0f, 1.0f, 1.0f};
        float intensity = 0.0f;
        // Where the light fades to nothing; 0 derives it from the intensity.
        float range = 0.0f;
        // Size of the emitter, softening surfaces close to it.
        float radius = 0.0f;
        // Spot cone half-angles in radians.
        float innerCone = 0.0f;
        float outerCone = 0.785398f;
        unsigned char type = POINT;
        // Made from an emissive surface (scaled by the emissive gain) rather
        // than a light source (scaled by the exposure).
        bool emissive = false;
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
    Pbr pbr;
    Water water;
    Terrain terrain;
    Bounds bounds;
    // Lights placed with the packet (task 21).
    QVector<Light> lights;
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
    // The surface the packet draws with. MSTS shapes take blending from the
    // shader name and use it for many opaque parts: a blended packet whose
    // texture has no partly transparent texels draws as opaque, and one whose
    // texture alpha is binary as alpha-tested (core.rendering.blendedParts).
    unsigned char drawSurface() const;
    // Sets the bounds from a sphere in mesh space, moved into the submission
    // space by transform (normally msMatrix; null is identity). A negative
    // radius clears them.
    void setBounds(const float *center, float radius, const float *transform = nullptr);

    // Conversions from the GL constants some loaders still use.
    static Primitive primitiveFromGl(unsigned int mode);
    static IndexType indexTypeFromGl(unsigned int type);
};

#endif /* RENDERITEM_H */
