/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RULEROBJ_H
#define	RULEROBJ_H

#include <tsre/world/objects/WorldObj.h>
#include <QString>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/procedural/ComplexLine.h>
#include <array>


class RenderQueue;
class OglObj;

class RulerObj : public WorldObj {
public:
    static bool TwoPointRuler;
    static bool DrawPoints;
    
    RulerObj();
    RulerObj(const RulerObj& o);
    WorldObj* clone();
    virtual ~RulerObj();
    bool allowNew();
    void reload();
    void reloadProceduralProfile() override;
    void setTemplate(QString name);
    void setNodeShape(QString name);
    QString getNodeShape() const;
    void load(int x, int y);
    void set(TS::TokenId sh, FileBuffer* data) override;
    void set(QString sh, QString val) override;
    void set(QString sh, FileBuffer* data) override;
    void setPosition(int x, int z, float* p);
    void updateSim(float deltaTime) override;
    void appendPoint(int tileX, int tileZ, const float* position);
    bool updateLastPoint(int tileX, int tileZ, const float* position);
    bool duplicateLastPoint();
    bool removeLastPoint();
    int pointCount() const;
    float lastSegmentLength() const;
    bool select(int value);
    void save(QTextStream* out);
    bool hasLinePoints();
    void getLinePoints(float *&punkty);
    void getPosition(float len, float* pos);
    float getLength();
    float getGeoLength();
    float getElevation();
    void createRoadPaths();
    void removeRoadPaths();
    void pushRenderItems(RenderQueue &queue, float lod, float posx, float posz, float* playerW, float* target, float fov, quint32 selectionId);

private:
    struct Point {
        bool selected = false;
        int shapeType = 0;
        float position[3];
        QVector<OglObj*> procShape;
        bool procShapeOwned = false;
        float quat[4];
        float matrix[16];
    };
    struct ProceduralInstance {
        OglObj *object = nullptr;
        QVector<std::array<float, 16>> transforms;
    };
    
    QVector<Point> points;
    QVector<ProceduralInstance> proceduralInstances;
    QVector<std::array<float, 16>> nodeTransforms;
    ComplexShape *nodeShapePointer = NULL;
    unsigned int nodeShapeState = 0;
    OglObj* point3d = NULL;
    OglObj* line3d = NULL;
    OglObj* point3dSelected = NULL;
    int selectionValue = 0;
    float length = 0;
    float geoLength = 0;

    void refreshLength();
    void invalidatePathGeometry();
    void pointFromTilePosition(Point &point, int tileX, int tileZ,
            const float *position) const;
    void ensureProceduralShape();
    void clearProceduralShape();
    void ensureNodeShape();
    void resetNodeShape();
    void ensureNodeTransforms();
    bool shapeEnabled = false;
    bool proceduralShapeInit = false;
    bool nodeTransformsInit = false;

};

#endif	/* RULEROBJ_H */

