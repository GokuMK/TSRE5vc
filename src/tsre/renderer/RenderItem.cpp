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
#include <tsre/Game.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>
#include <tsre/texture/TextureAlpha.h>
#include <tsre/math3d/Vector3f.h>
#include <tsre/math3d/Vector4f.h>
#include <QOpenGLFunctions>
#include <algorithm>
#include <cmath>

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

unsigned char RenderItem::drawSurface() const{
    if(material.opacity < 1.0f && material.surface != SURFACE_TERRAIN)
        return SURFACE_BLENDED;
    if(material.surface != SURFACE_BLENDED || !material.textured || pbr.enabled
            || Game::blendedParts == 0)
        return material.surface;
    // Until the texture has loaded the part stays blended.
    unsigned char alpha = material.textureAlpha;
    if(material.textureId >= 0){
        const auto found = TexLib::mtex.find(material.textureId);
        alpha = found != TexLib::mtex.end() && found->second != nullptr && found->second->loaded
                ? found->second->alphaClass.load() : TextureAlpha::Unknown;
    }
    if(alpha == TextureAlpha::Opaque)
        return SURFACE_OPAQUE;
    if(alpha == TextureAlpha::Binary && Game::blendedParts >= 2)
        return SURFACE_ALPHA_TEST;
    return SURFACE_BLENDED;
}

void RenderItem::setBounds(const float *center, float radius, const float *transform){
    if(radius < 0.0f){
        bounds = Bounds();
        return;
    }
    if(transform == nullptr){
        std::copy(center, center + 3, bounds.center);
        bounds.radius = radius;
        return;
    }
    float scale = 0.0f;
    for(int column = 0; column < 3; ++column){
        const float *axis = transform + column * 4;
        scale = std::max(scale, axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    }
    for(int i = 0; i < 3; ++i)
        bounds.center[i] = transform[i] * center[0] + transform[4 + i] * center[1]
                + transform[8 + i] * center[2] + transform[12 + i];
    bounds.radius = radius * std::sqrt(scale);
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
