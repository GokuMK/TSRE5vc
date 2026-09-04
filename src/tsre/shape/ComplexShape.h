/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef COMPLEXSHAPE_H
#define	COMPLEXSHAPE_H

#include <QHash>
#include <QString>
#include <QVector>

class ContentHierarchyInfo;
class ShapeHierarchyInfo;
class ShapeTextureInfo;

class ComplexShape {
public:
    virtual ~ComplexShape() = default;

    virtual const QString& getPathId() const = 0;
    virtual const QString& getTexPath() const;
    virtual QString getShapePreviewPath() const;
    virtual int getEsdDetailLevel() const;

    virtual bool isLoaded() const = 0;
    virtual float getSize() const = 0;
    virtual const float* getBound() const = 0;
    virtual bool getBoxPoints(QVector<float> &points) = 0;

    virtual void load() = 0;
    virtual void reload() = 0;

    virtual unsigned int newState() = 0;
    virtual void setAnimated(unsigned int stateId, bool animated) = 0;
    virtual void setEnabledSubObjs(unsigned int stateId, unsigned int enabledSubObjs);
    virtual void setCurrentDistanceLevel(unsigned int stateId, int level);
    virtual void enableSubObjByName(unsigned int stateId, const QString& name, bool val);
    virtual void enableSubObjByNameQueue(unsigned int stateId, const QString& name, bool val);
    virtual void updateSim(float deltaTime, unsigned int stateId = 0) = 0;

    virtual void render() = 0;
    virtual void render(quint32 selectionId, unsigned int stateId) = 0;
    virtual void pushRenderItem() = 0;
    virtual void pushRenderItem(quint32 selectionId, unsigned int stateId) = 0;

    virtual void invalidateRenderState(bool invalidateMatrixCache = true) = 0;

    virtual void enablePart(unsigned int uid, unsigned int stateId = 0);
    virtual void disablePart(unsigned int uid, unsigned int stateId = 0);

    virtual void fillShapeTextureInfo(QHash<int, ShapeTextureInfo*> &list, unsigned int stateId = 0);
    virtual void fillShapeHierarchyInfo(ShapeHierarchyInfo* info, unsigned int stateId = 0);
    virtual void fillContentHierarchyInfo(QVector<ContentHierarchyInfo*> &list, int parent);

    virtual void getFloorBorderLinePoints(float *&points);
    virtual bool isSnapable() const;
    virtual void addSnapablePoints(QVector<float> &out);
};

inline const QString& ComplexShape::getTexPath() const {
    static const QString empty;
    return empty;
}

inline QString ComplexShape::getShapePreviewPath() const {
    if(getTexPath().isEmpty())
        return getPathId();
    return getPathId() + "|" + getTexPath();
}

inline int ComplexShape::getEsdDetailLevel() const {
    return -1;
}

inline void ComplexShape::setEnabledSubObjs(unsigned int stateId, unsigned int enabledSubObjs) {
    (void)stateId;
    (void)enabledSubObjs;
}

inline void ComplexShape::setCurrentDistanceLevel(unsigned int stateId, int level) {
    (void)stateId;
    (void)level;
}

inline void ComplexShape::enableSubObjByName(unsigned int stateId, const QString& name, bool val) {
    (void)stateId;
    (void)name;
    (void)val;
}

inline void ComplexShape::enableSubObjByNameQueue(unsigned int stateId, const QString& name, bool val) {
    (void)stateId;
    (void)name;
    (void)val;
}

inline void ComplexShape::enablePart(unsigned int uid, unsigned int stateId) {
    (void)uid;
    (void)stateId;
}

inline void ComplexShape::disablePart(unsigned int uid, unsigned int stateId) {
    (void)uid;
    (void)stateId;
}

inline void ComplexShape::fillShapeTextureInfo(QHash<int, ShapeTextureInfo*>& list, unsigned int stateId) {
    (void)list;
    (void)stateId;
}

inline void ComplexShape::fillShapeHierarchyInfo(ShapeHierarchyInfo* info, unsigned int stateId) {
    (void)info;
    (void)stateId;
}

inline void ComplexShape::fillContentHierarchyInfo(QVector<ContentHierarchyInfo*>& list, int parent) {
    (void)list;
    (void)parent;
}

inline void ComplexShape::getFloorBorderLinePoints(float*& points) {
    (void)points;
}

inline bool ComplexShape::isSnapable() const {
    return false;
}

inline void ComplexShape::addSnapablePoints(QVector<float>& out) {
    (void)out;
}

#endif	/* COMPLEXSHAPE_H */
