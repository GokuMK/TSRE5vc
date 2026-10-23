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
#include <QVariantMap>
#include <QVector>

class RenderQueue;

class ContentHierarchyInfo;
class ShapeHierarchyInfo;
class ShapeTextureInfo;

using ShapeLoadOptions = QVariantMap;

namespace ShapeLoadOption {
inline constexpr const char FirstLodOnly[] = "firstLodOnly";
inline constexpr const char Compact[] = "compact";
// Every surface drawn and shadowed from both sides: a 3D cab's shell, its
// walls and ceiling facing in, still keeps the sun out.
inline constexpr const char DoubleSided[] = "doubleSided";
}

class ComplexShape {
public:
    virtual ~ComplexShape() = default;

    virtual const QString& getPathId() const = 0;
    virtual const QString& getTexPath() const;
    virtual QString getShapePreviewPath() const;
    virtual int getEsdDetailLevel() const;

    virtual bool isLoaded() const = 0;
    virtual bool hasLoadFailed() const { return false; }
    virtual float getSize() const = 0;
    virtual const float* getBound() const = 0;
    virtual bool getBoxPoints(QVector<float> &points) = 0;

    virtual void load() = 0;
    virtual void reload() = 0;
    // Load options are applied before load(). Backends consume the keys they
    // support and ignore the rest. False means loading has already started.
    virtual bool setLoadOptions(const ShapeLoadOptions &options);

    // Loading on a worker thread (ShapeLoader). detachedCopy() gives a new,
    // unloaded shape of the same file and options, or nullptr when the shape
    // loads only on the main thread. loadDetached() runs on a worker for such
    // a copy: it does what load() does except registering textures, which
    // the main thread owns. adopt() runs on the main thread and takes a
    // loaded copy's data, unless this shape has been loaded meanwhile.
    virtual ComplexShape *detachedCopy() const { return nullptr; }
    virtual void loadDetached() {}
    virtual void adopt(ComplexShape &copy) { (void)copy; }

    virtual unsigned int newState() = 0;
    virtual void setAnimated(unsigned int stateId, bool animated) = 0;
    virtual void setEnabledSubObjs(unsigned int stateId, unsigned int enabledSubObjs);
    virtual void setCurrentDistanceLevel(unsigned int stateId, int level);
    // Its distance levels (see ShapeLod): how many (0 without any), the
    // distance (metres, MSTS dlevel_selection) each is drawn to, or instead
    // the least screen coverage (glTF MSFT_screencoverage, above 0) each is
    // drawn at; the most detailed first.
    virtual int distanceLevelCount() const;
    virtual float distanceLevelDistance(int level) const;
    virtual float distanceLevelCoverage(int level) const;
    // The direction from the object to the camera in the shape's space (unit),
    // set each frame (ShapeLod): impostors pick their view by it.
    virtual void setCameraDirection(unsigned int stateId, const float direction[3]) {
        (void)stateId;
        (void)direction;
    }
    // Shadows of a coarser level (task renderer 37): the coarsest level fit
    // to stand in for the view's in the shadow maps, -1 for none (the shadows
    // are the view's level). Not an impostor, which faces the camera, nor a
    // cruciform fallback.
    virtual int shadowFloorLevel() const { return -1; }
    // The level cast into the shadow maps, set each frame (ShapeLod); -1
    // casts the view's.
    virtual void setShadowDistanceLevel(unsigned int stateId, int level) {
        (void)stateId;
        (void)level;
    }
    virtual void enableSubObjByName(unsigned int stateId, const QString& name, bool val);
    virtual void enableSubObjByNameQueue(unsigned int stateId, const QString& name, bool val);
    virtual void updateSim(float deltaTime, unsigned int stateId = 0) = 0;

    virtual void pushRenderItem(RenderQueue &queue) = 0;
    virtual void pushRenderItem(RenderQueue &queue, quint32 selectionId, unsigned int stateId) = 0;

    virtual void invalidateRenderState(bool invalidateMatrixCache = true) = 0;

    virtual void enablePart(unsigned int uid, unsigned int stateId = 0);
    virtual void disablePart(unsigned int uid, unsigned int stateId = 0);

    virtual void fillShapeTextureInfo(QHash<int, ShapeTextureInfo*> &list, unsigned int stateId = 0);
    virtual void fillShapeHierarchyInfo(ShapeHierarchyInfo* info, unsigned int stateId = 0);
    virtual void fillContentHierarchyInfo(QVector<ContentHierarchyInfo*> &list, int parent);

    virtual void getFloorBorderLinePoints(float *&points);
    virtual bool isSnapable() const;
    virtual void addSnapablePoints(QVector<float> &out);
    // The 4x4 transform of the matrix with this name (any case), with its
    // parents, in the static pose of the most detailed level: the space the
    // shape's vertices are drawn in. False when the shape is not loaded or
    // has no such matrix.
    virtual bool matrixByName(const QString &name, float *out) const;
};

inline bool ComplexShape::matrixByName(const QString &name, float *out) const {
    (void)name;
    (void)out;
    return false;
}

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

inline bool ComplexShape::setLoadOptions(const ShapeLoadOptions& options) {
    (void)options;
    return true;
}

inline void ComplexShape::setEnabledSubObjs(unsigned int stateId, unsigned int enabledSubObjs) {
    (void)stateId;
    (void)enabledSubObjs;
}

inline void ComplexShape::setCurrentDistanceLevel(unsigned int stateId, int level) {
    (void)stateId;
    (void)level;
}

inline int ComplexShape::distanceLevelCount() const {
    return 0;
}

inline float ComplexShape::distanceLevelDistance(int level) const {
    (void)level;
    return 0.0f;
}

inline float ComplexShape::distanceLevelCoverage(int level) const {
    (void)level;
    return 0.0f;
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
