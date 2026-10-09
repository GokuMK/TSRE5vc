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
#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/math3d/Vector4f.h>
#include <algorithm>

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
    Meshes::release(mesh);
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

void OglObj::setMaterial(float r, float g, float b, float a) {
    setMaterial(r, g, b);
    color->c = a;
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
    for (int &layer : layerTexIds) {
        if (layer >= 0)
            TexLib::delRef(layer);
        layer = -1;
    }
}

void OglObj::setWater(bool enabled, QString *bottomPath, QString *middlePath) {
    water = enabled;
    QString *paths[RenderItem::Water::LAYER_COUNT] = {bottomPath, middlePath};
    for (int i = 0; i < RenderItem::Water::LAYER_COUNT; ++i) {
        if (paths[i] == layerRes[i])
            continue;
        if (layerTexIds[i] >= 0)
            TexLib::delRef(layerTexIds[i]);
        layerTexIds[i] = -1;
        layerRes[i] = paths[i];
    }
}

// The uploaded texture of a lower water layer; 0 for none or not loaded yet.
unsigned int OglObj::layerTexture(int layer) {
    if (!water || layerRes[layer] == NULL)
        return 0;
    int &id = layerTexIds[layer];
    if (id == -1)
        id = TexLib::addTex(*layerRes[layer]);
    if (id < 0 || !TexLib::mtex[id]->loaded)
        return 0;
    if (!TexLib::mtex[id]->glLoaded)
        TexLib::mtex[id]->GLTextures();
    return TexLib::mtex[id]->glLoaded ? TexLib::mtex[id]->tex[0] : 0;
}

void OglObj::deleteVBO(){
    Meshes::release(mesh);
    loaded = false;
}

void OglObj::init(float* punkty, int ptr, enum RenderItem::VertexAttr v, int type) {
    shapeType = type;
    vAttribures = v;
    MeshData data;
    data.layout = v;
    data.vertices.assign(punkty, punkty + ptr);
    Meshes::update(mesh, std::move(data));
    length = ptr / v;
    loaded = true;
    boundRadius = -1.0f;
    if (length > 0 && v >= 3) {
        float low[3] = {punkty[0], punkty[1], punkty[2]};
        float high[3] = {punkty[0], punkty[1], punkty[2]};
        for (int i = 1; i < length; ++i)
            for (int c = 0; c < 3; ++c) {
                low[c] = std::min(low[c], punkty[i * v + c]);
                high[c] = std::max(high[c], punkty[i * v + c]);
            }
        float radius = 0.0f;
        for (int c = 0; c < 3; ++c)
            boundCenter[c] = 0.5f * (low[c] + high[c]);
        for (int i = 0; i < length; ++i) {
            float distance = 0.0f;
            for (int c = 0; c < 3; ++c) {
                const float d = punkty[i * v + c] - boundCenter[c];
                distance += d * d;
            }
            radius = std::max(radius, distance);
        }
        boundRadius = std::sqrt(radius);
    }
}

void OglObj::setLineWidth(int val){
    lineWidth = val;
}

void OglObj::setOpacity(float value){
    opacity = value;
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

    unsigned int layers[RenderItem::Water::LAYER_COUNT] = {0, 0};
    if (textured)
        for (int i = 0; i < RenderItem::Water::LAYER_COUNT; ++i)
            layers[i] = layerTexture(i);
    RenderItem *packet = framePacket(textured, texAddr, materialColor,
                                     terrainDecal && selectionId == 0, layers);
    queue.submit(packet, selectionId, RenderQueue::SUBMIT_ORDERED);
}

RenderItem *OglObj::framePacket(bool textured, unsigned int texAddr,
                                const float *materialColor, bool decal,
                                const unsigned int *layers){
    const quint64 frame = Renderer::frameNumber();
    if(packetFrame != frame){
        packetFrame = frame;
        packetsUsed = 0;
    }
    const auto matches = [&](const RenderItem *packet){
        const RenderItem::Material &m = packet->material;
        return m.textured == textured
                && (!textured || m.textureObject == texAddr)
                && m.color[0] == materialColor[0] && m.color[1] == materialColor[1]
                && m.color[2] == materialColor[2] && m.color[3] == materialColor[3]
                && m.decal == decal
                && packet->water.enabled == water
                && std::equal(layers, layers + RenderItem::Water::LAYER_COUNT,
                              packet->water.layers)
                && packet->mesh.count == static_cast<unsigned int>(length)
                && packet->mesh.primitive == RenderItem::primitiveFromGl(shapeType)
                && m.lineWidth == lineWidth
                && m.opacity == opacity;
    };
    for(int i = 0; i < packetsUsed; ++i){
        if(matches(packets[i]))
            return packets[i];
    }
    if(packetsUsed == packets.size())
        packets.push_back(new RenderItem());
    RenderItem *packet = packets[packetsUsed++];
    packet->setVertexAttributes(vAttribures);
    packet->material.decal = decal;
    if(textured)
        packet->enableTextures(texAddr);
    else
        packet->disableTextures(materialColor[0], materialColor[1],
                                materialColor[2], materialColor[3]);
    std::copy(materialColor, materialColor + 4, packet->material.color);
    packet->material.lineWidth = lineWidth;
    packet->material.opacity = opacity;
    packet->water.enabled = water;
    std::copy(layers, layers + RenderItem::Water::LAYER_COUNT, packet->water.layers);
    packet->mesh.handle = mesh;
    packet->msMatrix = NULL;
    packet->mesh.primitive = RenderItem::primitiveFromGl(shapeType);
    packet->mesh.first = 0;
    packet->mesh.count = length;
    packet->setBounds(boundCenter, boundRadius);
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
