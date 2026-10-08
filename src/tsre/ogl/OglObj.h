/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef OGLOBJ_H
#define	OGLOBJ_H

#include <QString>
#include <tsre/ogl/GLUU.h>
#include <tsre/renderer/MeshHandle.h>
#include <tsre/renderer/RenderItem.h>

class RenderQueue;

class OglObj {
public:
    enum MaterialType {NONE, TEXTURE, COLOR};
    bool loaded;
    // Ground-only decal: biased depth test, no depth writes. Not used for picking.
    bool terrainDecal = false;
    
    OglObj();
    OglObj(const OglObj& orig);
    virtual ~OglObj();
    void init(float* punkty, int ptr, enum RenderItem::VertexAttr v, int type);
    virtual void pushRenderItem(RenderQueue &queue);
    virtual void pushRenderItem(RenderQueue &queue, quint32 selectionId, float lod = 0);
    void deleteVBO();
    void setMaterial(float r, float g, float b);
    // A colour drawn with transparency below an alpha of 1.
    void setMaterial(float r, float g, float b, float a);
    void setMaterial(QString* path);
    void setMaterialTextureId(int id);
    void resetTexture();
    int getTexId();
    void setDistanceRange(float min, float max);
    void setLineWidth(int val);
    // Draws as a shaded water surface (water program variant) over the
    // bottom and middle water layer textures, or as a plain surface when off.
    void setWater(bool enabled, QString *bottomPath = NULL, QString *middlePath = NULL);
    bool getSimpleBorder(float* border);
    void setBound(float *b);
private:
    // Renderer-owned vertices; init() needs no GL context.
    MeshHandle mesh;
    int length; 
    int shapeType;
    // Sphere around the vertices given to init().
    float boundCenter[3] = {0.0f, 0.0f, 0.0f};
    float boundRadius = -1.0f;
    int texId;
    int materialType;
    int lineWidth = 0;
    float bound[6];
    float minDistance = -1;
    float maxDistance = 999999;
    QString *res;
    bool water = false;
    // Lower water layers (RenderItem::Water::Layer order).
    QString *layerRes[RenderItem::Water::LAYER_COUNT] = {NULL, NULL};
    int layerTexIds[RenderItem::Water::LAYER_COUNT] = {-1, -1};
    Vector4f *color = NULL;
    RenderItem::VertexAttr vAttribures = RenderItem::NO_ATTR;
    // Gather packets reused across frames. One object can be submitted
    // several times per frame with different materials, so each frame takes
    // a matching or unused packet from this pool.
    QVector<RenderItem*> packets;
    quint64 packetFrame = 0;
    int packetsUsed = 0;
    unsigned int layerTexture(int layer);
    void retirePackets();
protected:
    // A packet of this frame for the material: one already used this frame
    // when it matches, or a free one. Subclasses configure and submit it.
    RenderItem *framePacket(bool textured, unsigned int texAddr,
                            const float *color, bool decal, const unsigned int *layers);
};

#endif	/* OGLOBJ_H */

