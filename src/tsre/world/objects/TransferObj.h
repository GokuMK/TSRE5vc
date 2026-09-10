/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TRANSFEROBJ_H
#define	TRANSFEROBJ_H

#include <tsre/world/objects/WorldObj.h>
#include <tsre/ogl/OglObj.h>
#include <QString>
#include <memory>
#include "TransferMesh.h"

class TransferObj : public WorldObj {
public:
    /*struct Shape{
        int iloscv;
        QOpenGLBuffer VBO;
        QOpenGLVertexArrayObject VAO;
    };*/
    //Shape shape;
    OglObj shape;
    QString texture;
    float width = 0;
    float height = 0;
    // Legacy transfers cover missing ground. Retain opt-in clipping for a future UI.
    // Runtime-only: no new W-file field is introduced.
    bool respectTerrainHoles = false;
    
    TransferObj();
    TransferObj(const TransferObj& o);
    WorldObj* clone();
    virtual ~TransferObj();
    bool allowNew();
    void load(int x, int y);
    void set(TS::TokenId sh, FileBuffer* val);
    void set(QString sh, QString val);
    void set(QString sh, float val);
    void set(QString sh, long long int val);
    void set(QString sh, FileBuffer* data);
    void save(QTextStream* out);
    void deleteVBO();
    int getTexId();
    virtual Ref::RefItem* getRefInfo();
    void translate(float px, float py, float pz);
    void rotate(float x, float y, float z);
    void resize(float x, float y, float z);
    int getDefaultDetailLevel();
    void pushRenderItems(float lod, float posx, float posz, float* playerW, float* target, float fov, quint32 selectionId);
    void render(GLUU* gluu, float lod, float posx, float posz, float* playerW, float* target, float fov, quint32 selectionId, int renderMode);
private:
    // Created only after a hole is encountered; release its GPU mesh when empty.
    std::unique_ptr<OglObj> holeShape;
    void drawShape(bool pushToQueue = false, quint32 selectionId = 0);
    int tex = -1;
    bool init = false;
    TransferMesh terrainMesh;
    float bound[6];
    QString *texturePath = nullptr;
    bool getBoxPoints(QVector<float> &points);
};

#endif	/* TRANSFEROBJ_H */

