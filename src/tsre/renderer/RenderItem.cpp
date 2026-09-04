/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/renderer/RenderItem.h>
#include <tsre/math3d/Vector3f.h>
#include <tsre/math3d/Vector4f.h>

RenderItem::RenderItem() {
    colorX = 1.0f;
    colorY = 1.0f;
    colorZ = 1.0f;
    colorA = 1.0f;
}

RenderItem::RenderItem(const RenderItem& orig) {
    *this = orig;
}

RenderItem::~RenderItem() {
    //delete mvMatrix;
}

void RenderItem::setVertexAttributes(VertexAttr attr){
    vertexAttr = attr;
    if(vertexAttr == V){
        normalsEnabled = 0;
    } else if(vertexAttr == VT){
        normalsEnabled = 0;
    } else if(vertexAttr == VNT){
        normalsEnabled = 1;
    } else if(vertexAttr == VNTA){
        normalsEnabled = 1;
    }  
}

void RenderItem::disableTextures(Vector4f* color){
    texturesEnabled = 0;
    colorX = color->x;
    colorY = color->y;
    colorZ = color->z;
    colorA = color->c;
}

void RenderItem::disableTextures(Vector3f* color){
    texturesEnabled = 0;
    colorX = color->x;
    colorY = color->y;
    colorZ = color->z;
    colorA = 1.0f;
}

void RenderItem::disableTextures(float x, float y, float z, float a){
    texturesEnabled = 0;
    colorX = x;
    colorY = y;
    colorZ = z;
    colorA = a;
}

void RenderItem::setSelectionId(quint32 id){
    selectionId = id;
}

void RenderItem::enableTextures(unsigned int addr){
    texturesEnabled = 1;
    texAddr = addr;
}
