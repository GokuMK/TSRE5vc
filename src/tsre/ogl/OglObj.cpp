/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "OglObj.h"
#include "ScopedTerrainDecal.h"
#include <tsre/texture/TexLib.h>
#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/math3d/Vector4f.h>

OglObj::OglObj() {
    loaded = false;
    texId = -1;
    materialType = NONE;
    for(int i = 0; i < 6; i++)
        bound[i] = 0;   
}

OglObj::OglObj(const OglObj& orig) {
}

OglObj::~OglObj() {
    retirePackets();
}

void OglObj::setMaterial(float r, float g, float b) {
    materialType = COLOR;
    if(color == NULL) {
        color = new Vector4f(r, g, b, 1.0);
    } else {
        color->x = r;
        color->y = g;
        color->z = b;
        color->c = 1.0;
    }
}

void OglObj::setMaterial(QString* path) {
    materialType = TEXTURE;
    res = path;
}

void OglObj::setMaterialTextureId(int id) {
    materialType = TEXTURE;
    texId = id;
}

void OglObj::resetTexture() {
    if (texId >= 0)
        TexLib::delRef(texId);
    texId = -1;
}

void OglObj::deleteVBO(){
    if(loaded){
        VBO.destroy();
        VAO.destroy();
    }
    loaded = false;
}

float* OglObj::mapBuffer(){
    QOpenGLVertexArrayObject::Binder vaoBinder(&VAO);
    VBO.bind();
    return (float*)VBO.map(QOpenGLBuffer::ReadWrite);
    VBO.release();
}

void OglObj::unmapBuffer(){
    QOpenGLVertexArrayObject::Binder vaoBinder(&VAO);
    VBO.bind();
    VBO.unmap();
    VBO.release();
}

void OglObj::init(float* punkty, int ptr, enum RenderItem::VertexAttr v, int type) {
    //if(loaded){
    //    VBO.destroy();
    //    VAO.destroy();
    //}
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    shapeType = type;
    vAttribures = v;
    if(!loaded){
        if(!VAO.isCreated()){
            VAO.create();
            VBO.create();
        }
    }
    QOpenGLVertexArrayObject::Binder vaoBinder(&VAO);
    //VBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    VBO.bind();
    VBO.allocate(punkty, ptr * sizeof (GLfloat));
    
    if (v == RenderItem::V) {
        f->glEnableVertexAttribArray(0);
        f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof (GLfloat), 0);
    } else if (v == RenderItem::VT) {
        f->glEnableVertexAttribArray(0);
        f->glEnableVertexAttribArray(1);
        f->glEnableVertexAttribArray(3);
        f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof (GLfloat), 0);
        f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof (GLfloat), reinterpret_cast<void *> (3 * sizeof (GLfloat)));
        f->glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 6 * sizeof (GLfloat), reinterpret_cast<void *> (5 * sizeof (GLfloat)));
    } else if (v == RenderItem::VNTA) {
        f->glEnableVertexAttribArray(0);
        f->glEnableVertexAttribArray(1);
        f->glEnableVertexAttribArray(2);
        f->glEnableVertexAttribArray(3);
        f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof (GLfloat), 0);
        f->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof (GLfloat), reinterpret_cast<void *> (3 * sizeof (GLfloat)));
        f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof (GLfloat), reinterpret_cast<void *> (6 * sizeof (GLfloat)));
        f->glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof (GLfloat), reinterpret_cast<void *> (8 * sizeof (GLfloat)));
    }
    
    VBO.release();
    length = ptr / v;
    loaded = true;
}

void OglObj::setLineWidth(int val){
    lineWidth = val;
}

void OglObj::pushRenderItem(RenderQueue &queue) {
    pushRenderItem(queue, 0);
}

void OglObj::setDistanceRange(float min, float max){
    minDistance = min;
    maxDistance = max;
}

void OglObj::pushRenderItem(RenderQueue &queue, quint32 selectionId, float lod){
    if(!loaded)
        return;
    if(lod > maxDistance || lod < minDistance)
        return;
    if(vAttribures == RenderItem::NO_ATTR)
        return;
    if (texId == -2)
        return;
    if(materialType == NONE)
        return;

    // Selection draws untextured; otherwise resolve the material now so a
    // texture that finished loading is used without rebuilding anything.
    static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const float missing[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    bool textured = false;
    unsigned int texAddr = 0;
    const float *materialColor = white;
    float colorValues[4];
    if(selectionId == 0 && materialType == TEXTURE){
        if (texId == -1) {
            texId = TexLib::addTex(*res);
        }
        materialColor = missing;
        if (TexLib::mtex[texId]->loaded) {
            if (!TexLib::mtex[texId]->glLoaded)
                TexLib::mtex[texId]->GLTextures();
            if(TexLib::mtex[texId]->glLoaded){
                textured = true;
                texAddr = TexLib::mtex[texId]->tex[0];
                materialColor = white;
            }
        }
    } else if(selectionId == 0 && materialType == COLOR){
        colorValues[0] = color->x;
        colorValues[1] = color->y;
        colorValues[2] = color->z;
        colorValues[3] = color->c;
        materialColor = colorValues;
    }

    RenderItem *packet = framePacket(textured, texAddr, materialColor,
                                     terrainDecal && selectionId == 0);
    queue.submit(packet, selectionId, RenderQueue::SUBMIT_ORDERED);
}

RenderItem *OglObj::framePacket(bool textured, unsigned int texAddr,
                                const float *materialColor, bool decal){
    const quint64 frame = Renderer::frameNumber();
    if(packetFrame != frame){
        packetFrame = frame;
        packetsUsed = 0;
    }
    const auto matches = [&](const RenderItem *packet){
        return packet->texturesEnabled == (textured ? 1 : 0)
                && (!textured || packet->texAddr == texAddr)
                && packet->colorX == materialColor[0] && packet->colorY == materialColor[1]
                && packet->colorZ == materialColor[2] && packet->colorA == materialColor[3]
                && packet->terrainDecal == decal
                && packet->vertCount == static_cast<unsigned int>(length)
                && packet->itemType == static_cast<unsigned int>(shapeType)
                && packet->lineWidth == lineWidth;
    };
    for(int i = 0; i < packetsUsed; ++i){
        if(matches(packets[i]))
            return packets[i];
    }
    if(packetsUsed == packets.size())
        packets.push_back(new RenderItem());
    RenderItem *packet = packets[packetsUsed++];
    packet->setVertexAttributes(vAttribures);
    packet->terrainDecal = decal;
    if(textured)
        packet->enableTextures(texAddr);
    else
        packet->disableTextures(materialColor[0], materialColor[1],
                                materialColor[2], materialColor[3]);
    packet->colorX = materialColor[0];
    packet->colorY = materialColor[1];
    packet->colorZ = materialColor[2];
    packet->colorA = materialColor[3];
    packet->lineWidth = lineWidth;
    packet->VBO = &VBO;
    packet->VAO = &VAO;
    packet->msMatrix = NULL;
    packet->itemType = shapeType;
    packet->vertOffset = 0;
    packet->vertCount = length;
    return packet;
}

void OglObj::retirePackets(){
    for(RenderItem *packet : packets)
        Renderer::retirePacket(packet);
    packets.clear();
    packetsUsed = 0;
}

int OglObj::getTexId(){
    return this->texId;
}

bool OglObj::getSimpleBorder(float* border){
    border[0] = bound[0];
    border[1] = bound[1];
    border[2] = bound[2];
    border[3] = bound[3];
    border[4] = bound[4];
    border[5] = bound[5];
    return true;
}

void OglObj::setBound(float *b){
    bound[0] = b[0];
    bound[1] = b[1];
    bound[2] = b[2];
    bound[3] = b[3];
    bound[4] = b[4];
    bound[5] = b[5];
}
