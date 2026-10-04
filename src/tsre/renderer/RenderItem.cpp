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
#include <tsre/renderer/RenderStats.h>
#include <tsre/math3d/Vector3f.h>
#include <tsre/math3d/Vector4f.h>
#include <QOpenGLFunctions>

RenderItem::RenderItem() {
    RenderStats::countRenderItem();
}

RenderItem::RenderItem(const RenderItem& orig) {
    RenderStats::countRenderItem();
    *this = orig;
}

RenderItem::~RenderItem() {
}

void RenderItem::setVertexAttributes(VertexAttr attr){
    mesh.layout = attr;
    material.lit = attr == VNT || attr == VNTA;
}

void RenderItem::disableTextures(Vector4f* color){
    disableTextures(color->x, color->y, color->z, color->c);
}

void RenderItem::disableTextures(Vector3f* color){
    disableTextures(color->x, color->y, color->z, 1.0f);
}

void RenderItem::disableTextures(float x, float y, float z, float a){
    material.textured = false;
    material.color[0] = x;
    material.color[1] = y;
    material.color[2] = z;
    material.color[3] = a;
}

void RenderItem::setSelectionId(quint32 id){
    selectionId = id;
}

void RenderItem::enableTextures(unsigned int textureObject){
    material.textured = true;
    material.textureObject = textureObject;
}

void RenderItem::enableTextureId(int id){
    material.textured = true;
    material.textureId = id;
}

RenderItem::Primitive RenderItem::primitiveFromGl(unsigned int mode){
    switch(mode){
    case GL_TRIANGLE_STRIP: return PRIMITIVE_TRIANGLE_STRIP;
    case GL_TRIANGLE_FAN: return PRIMITIVE_TRIANGLE_FAN;
    case GL_LINES: return PRIMITIVE_LINES;
    case GL_LINE_STRIP: return PRIMITIVE_LINE_STRIP;
    case GL_LINE_LOOP: return PRIMITIVE_LINE_LOOP;
    case GL_POINTS: return PRIMITIVE_POINTS;
    default: return PRIMITIVE_TRIANGLES;
    }
}

RenderItem::IndexType RenderItem::indexTypeFromGl(unsigned int type){
    return type == GL_UNSIGNED_INT ? INDEX_U32 : INDEX_U16;
}
