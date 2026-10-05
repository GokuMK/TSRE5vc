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
    void setMaterial(QString* path);
    void setMaterialTextureId(int id);
    void resetTexture();
    int getTexId();
    void setDistanceRange(float min, float max);
    void setLineWidth(int val);
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
    Vector4f *color = NULL;
    RenderItem::VertexAttr vAttribures = RenderItem::NO_ATTR;
    // Gather packets reused across frames. One object can be submitted
    // several times per frame with different materials, so each frame takes
    // a matching or unused packet from this pool.
    QVector<RenderItem*> packets;
    quint64 packetFrame = 0;
    int packetsUsed = 0;
    RenderItem *framePacket(bool textured, unsigned int texAddr,
                            const float *color, bool decal);
    void retirePackets();
};

#endif	/* OGLOBJ_H */

