/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/objects/TransferObj.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/math3d/GLMatrix.h>
#include <math.h>
#include <tsre/fileFunctions/ParserX.h>
#include <QDebug>
#include <cstdlib>
#include <tsre/texture/TexLib.h>
#include <tsre/math3d/Vector2f.h>

#include <tsre/world/TerrainLib.h>
#include <tsre/world/TerrainSeason.h>
#include <QDir>
#include <QOpenGLShaderProgram>
#include <tsre/Game.h>
#include <tsre/fileFunctions/TS.h>
#include <tsre/world/Ref.h>
#include <tsre/renderer/Renderer.h>

TransferObj::TransferObj() {
    this->width = 10;
    this->height = 10;
    this->texture = "shadwrct.ace";
}

TransferObj::TransferObj(const TransferObj& o) : WorldObj(o) {
    texture = o.texture;
    width = o.width;
    height = o.height;
    respectTerrainHoles = o.respectTerrainHoles;

    tex = o.tex;
    init = false;
    if(o.texturePath != NULL){
        texturePath = new QString();
        *texturePath = *o.texturePath;
    }
}

WorldObj* TransferObj::clone(){
    return new TransferObj(*this);
}

TransferObj::~TransferObj() {
    delete texturePath;
}

void TransferObj::load(int x, int y) {
    this->x = x;
    this->y = y;
    this->position[2] = -this->position[2];
    this->loaded = true;
    this->size = -1;
    this->tex = -1;
    this->init = false;
    this->skipLevel = 3;
    terrainMesh.invalidate();
    this->modified = false;
}

bool TransferObj::allowNew(){
    return true;
}

void TransferObj::set(QString sh, QString val){
    if (sh == ("filename")) {
        texture = val;
        return;
    }
    if (sh == ("ref_filename")) {
        texture = val;
        //TexLib::addTex(resPath+"/"+texture);
        return;
    }
    WorldObj::set(sh, val);
    return;
}

void TransferObj::set(QString sh, long long int val){
    if (sh == ("ref_value")) {
        if(val != 0){
        long long int val1 = (val & 0xFFFFFFFF00000000) >> 32;
        long long int val2 = (val & 0x00000000FFFFFFFF);
        width = (float)val1/1000;
        height = (float)val2/1000;
        //TexLib::addTex(resPath+"/"+texture);
        }
        return;
    }    
    WorldObj::set(sh, val);
    return;
}

void TransferObj::set(QString sh, float val){
    if (sh == ("width")) {
        width = val;
        return;
    }
    if (sh == ("height")) {
        height = val;
        return;
    }
    WorldObj::set(sh, val);
    return;
}

void TransferObj::set(TS::TokenId sh, FileBuffer* data) {
    if (sh == TS::FileName) {
        data->skipLabel();
        int slen = data->getShort()*2;
        texture = *data->getString(data->off, data->off + slen);
        data->off += slen;
        return;
    }
    if (sh == TS::Width) {
        data->skipLabel();
        width = data->getFloat();
        return;
    }
    if (sh == TS::Height) {
        data->skipLabel();
        height = data->getFloat();
        return;
    }
    WorldObj::set(sh, data);
    return;
}

void TransferObj::set(QString sh, FileBuffer* data) {
    if (sh == ("filename")) {
        texture = ParserX::GetString(data);
        return;
    }
    if (sh == ("width")) {
        width = ParserX::GetNumber(data);
        return;
    }
    if (sh == ("height")) {
        height = ParserX::GetNumber(data);
        return;
    }
    WorldObj::set(sh, data);
    return;
}

Ref::RefItem* TransferObj::getRefInfo(){
    Ref::RefItem* r = new Ref::RefItem();
    r->type = this->type;
    r->filename.push_back(this->texture);
    
    long long int val1 = this->width*1000;
    long long int val2 = this->height*1000;
    r->value = val1 << 32 | val2;
    return r;
}

void TransferObj::deleteVBO(){
    //this->shape.deleteVBO();
    this->init = false;
    terrainMesh.invalidate();
    this->box.deleteVBO();
}

void TransferObj::translate(float px, float py, float pz){
    this->position[0]+=px;
    //this->position[1]+=py;
    this->position[2]+=pz;
    setModified();
    deleteVBO();
}

void TransferObj::rotate(float x, float y, float z){
    if(matrix3x3 != NULL) matrix3x3 = NULL;
    if(x!=0) Quat::rotateX(this->qDirection, this->qDirection, x);
    if(y!=0) Quat::rotateY(this->qDirection, this->qDirection, y);
    if(z!=0) Quat::rotateZ(this->qDirection, this->qDirection, z);
    setModified();
    deleteVBO();
}

void TransferObj::resize(float x, float y, float z){
    if(x == 0 && y == 0) return;
    this->width += x;
    this->height += y;
    setModified();
    deleteVBO();
}

void TransferObj::render(GLUU* gluu, float lod, float posx, float posz, float* pos, float* target, float fov, quint32 selectionId, int renderMode) {
    if (!loaded) 
        return;
    if(renderMode == gluu->RENDER_SHADOWMAP) 
        return;
    //if (jestPQ < 2) return;
    //GLUU* gluu = GLUU::get();
    //if((this.position===undefined)||this.qDirection===undefined) return;

    /*if (size > 0) {
        if ((lod > size)) {
            float v1[2];
            v1[0] = pos[0] - (target[0]);
            v1[1] = pos[2] - (target[2]);
            float v2[2];
            v2[0] = posx;
            v2[1] = posz;
            float iloczyn = v1[0] * v2[0] + v1[1] * v2[1];
            float d1 = sqrt(v1[0] * v1[0] + v1[1] * v1[1]);
            float d2 = sqrt(v2[0] * v2[0] + v2[1] * v2[1]);
            float zz = iloczyn / (d1 * d2);
            if (zz > 0) return;

            float ccos = cos(fov) + zz;
            float xxx = sqrt(2 * d2 * d2 * (1 - ccos));
            //if((ccos > 0) && (xxx > 200+50)) return;
            if ((ccos > 0) && (xxx > size + 150) && (skipLevel == 1)) return;
        }
    } else {
        if (ShapeLib::shape[shape]->loaded)
            size = ShapeLib::shape[shape]->size;
    }
*/
    Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, position[0], 0, position[2]);
    //float scale = sqrt(qDirection[0] * qDirection[0] + qDirection[1] * qDirection[1] + qDirection[2] * qDirection[2]);
    //float angle = ((acos(qDirection[3])*360) / M_PI);
    //Mat4::rotate(gluu->mvMatrix, gluu->mvMatrix, gluu->degToRad(-angle), -qDirection[0] * scale, -qDirection[1] * scale, qDirection[2] * scale);
    //Mat4::rotate(gluu->mvMatrix, gluu->mvMatrix, gluu->degToRad(180), 0, -1, 0);

    //if(selected){
    //    selected = !selected;
    //    selectionId = 155;
    //}
    //gluu.setMatrixUniforms();

    //
    //var z = this.position[0]*mmm[9] + this.position[1]*mmm[7] + this.position[2]*mmm[9];

    gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->mvMatrix));
       
    drawShape(false, selectionId);
    if(selected){
        drawBox();
    }
};

void TransferObj::pushRenderItems(float lod, float posx, float posz, float* playerW, float* target, float fov, quint32 selectionId){
    if (!loaded)
        return;
    if (Game::currentRenderer == NULL)
        return;

    Mat4::translate(Game::currentRenderer->mvMatrix, Game::currentRenderer->mvMatrix, position[0], 0, position[2]);
    drawShape(true, selectionId);
}

void TransferObj::drawShape(bool pushToQueue, quint32 selectionId) {
    if (!init && !Game::ignoreLoadLimits && Game::objectLoadingTokens < 1) return;
    QVector<float> vertices, holeVertices;
    if (terrainMesh.update(Game::terrainLib, x, y, position, width, height,
                           qDirection, -GLUU::get()->alphaTest, vertices, holeVertices, respectTerrainHoles)) {
        if (!init && !Game::ignoreLoadLimits) Game::objectLoadingTokens-=2;
        box.deleteVBO();
        if (!vertices.isEmpty() || !holeVertices.isEmpty()) {
            if (!texturePath) texturePath=new QString;
            *texturePath=TerrainSeason::resolve(resPath, Game::season, texture);
            if (texturePath->isEmpty())
                *texturePath=QDir(resPath).filePath(texture);
        }
        if (vertices.isEmpty()) shape.deleteVBO();
        else {
            shape.setMaterial(texturePath);
            shape.init(vertices.data(),int(vertices.size()),RenderItem::VNTA,GL_TRIANGLES);
        }
        if (holeVertices.isEmpty()) {
            if (holeShape) holeShape->deleteVBO();
        } else {
            if (!holeShape) holeShape=std::make_unique<OglObj>();
            holeShape->setMaterial(texturePath);
            holeShape->init(holeVertices.data(),int(holeVertices.size()),RenderItem::VNTA,GL_TRIANGLES);
        }
        init=true;
    }
    shape.terrainDecal = true;
    if (pushToQueue) shape.pushRenderItem(selectionId);
    else shape.render(selectionId);
    if (holeShape && holeShape->loaded) {
        // Hole covers must write depth so geometry below the carpet is occluded.
        holeShape->terrainDecal=false;
        if (pushToQueue) holeShape->pushRenderItem(selectionId);
        else holeShape->render(selectionId);
    }
}

bool TransferObj::getBoxPoints(QVector<float> &points) {
    if (!loaded || terrainMesh.outline().isEmpty()) return false;
    points += terrainMesh.outline();
    return true;
}

int TransferObj::getTexId(){
    if (!shape.loaded && holeShape && holeShape->loaded) return holeShape->getTexId();
    return shape.getTexId();
}

int TransferObj::getDefaultDetailLevel(){
    return -1;
}

void TransferObj::save(QTextStream* out){
    if (!loaded) return;
    if(WorldObj::positiveQuaternionSerialization())
        Quat::makePositive(this->qDirection);
    
*(out) << "	Transfer (\n";
*(out) << "		UiD ( "<<this->UiD<<" )\n";
*(out) << "		Width ( "<<this->width<<" )\n";
*(out) << "		Height ( "<<this->height<<" )\n";
*(out) << "		FileName ( "<<ParserX::AddComIfReq(this->texture)<<" )\n";
*(out) << "		StaticFlags ( "<<ParserX::MakeFlagsString(this->staticFlags)<<" )\n";
*(out) << "		Position ( "<<this->position[0]<<" "<<this->position[1]<<" "<<-this->position[2]<<" )\n";
*(out) << "		QDirection ( "<<this->qDirection[0]<<" "<<this->qDirection[1]<<" "<<this->qDirection[2]<<" "<<this->qDirection[3]<<" )\n";
*(out) << "		VDbId ( "<<this->vDbId<<" )\n";
if(this->staticDetailLevel > -1)
*(out) << "		StaticDetailLevel ( "<<this->staticDetailLevel<<" )\n";
*(out) << "	)\n";
}
