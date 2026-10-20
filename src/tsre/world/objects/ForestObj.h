/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef FORESTOBJ_H
#define	FORESTOBJ_H

#include <tsre/world/objects/WorldObj.h>
#include <QString>
#include <QVector>

class RenderQueue;
class Vector4f;

class OglObj;
//class Ref::RefItem;

class ForestObj : public WorldObj{
public:
    /*struct Shape{
        int iloscv;
        QOpenGLBuffer VBO;
        QOpenGLVertexArrayObject VAO;
    };*/
    struct ForestList{
        QString name;
        QString texture;
        float scaleRangeX = 0;
        float scaleRangeZ = 0;
        float treeSizeX = 0;
        float treeSizeZ = 0;
    };
    static float ForestClearDistance;
    static QVector<ForestList> forestList;
    QString treeTexture = "";
    float scaleRangeX = 0;
    float scaleRangeZ = 0;
    float areaX = 0;
    float areaZ = 0;
    float treeSizeX = 0;
    float treeSizeZ = 0;
    float population = 0;
    ForestObj();
    ForestObj(const ForestObj& o);
    WorldObj* clone();
    bool allowNew();
    void load(int x, int y);
    void set(TS::TokenId sh, FileBuffer* data);
    void set(QString sh, long long int val);
    void set(QString sh, float val);
    void set(QString sh, QString val);
    void set(QString sh, FileBuffer* data);
    Ref::RefItem* getRefInfo();
    void save(QTextStream* out);
    void deleteVBO();
    void translate(float px, float py, float pz);
    void rotate(float x, float y, float z);
    void resize(float x, float y, float z);
    int getDefaultDetailLevel();
    void pushRenderItems(RenderQueue &queue, float lod, float posx, float posz, float* playerW, float* target, float fov, quint32 selectionId);
    static void LoadForestList();
    static int GetListIdByTexture(QString texture);
    // The forests.dat definition (forestList) whose texture or shape this
    // forest draws, -1 for none.
    int definitionId() const;
    // Takes a definition's texture or shape, tree size and scale range.
    void setDefinition(int id);
    // Whether the trees are shapes (.s, .gltf, .glb) rather than a texture.
    bool shapeTrees() const;
    virtual ~ForestObj();
private:
    // A shape tree: where it stands (from the forest's position), its
    // transform (turned and scaled at random) and its shape state.
    struct Tree {
        float position[3];
        float matrix[16];
        unsigned int state = 0;
    };
    void pushRenderShape(RenderQueue &queue, quint32 selectionId);
    void pushRenderTrees(RenderQueue &queue, quint32 selectionId, float posx, float posz, const float *playerW);
    // The trees' places from the forest's seed (tile coordinates, y unset),
    // without those cleared off the track and roads.
    QVector<Vector4f> treePoints();
    bool getBoxPoints(QVector<float>& points);
    int tex;
    bool init;
    OglObj shape;
    QString * texturePath = NULL;
    // Shape trees: the shape (ShapeLib id) of treeShapeName, the trees and
    // the shape states they reuse.
    int treeShape = -1;
    QString treeShapeName;
    QVector<Tree> trees;
    QVector<unsigned int> treeStates;
};

#endif	/* FORESTOBJ_H */

