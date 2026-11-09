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

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <tsre/shape/ShapeDisplays.h>
#include <tsre/shape/ShapeEmitters.h>
#include <tsre/shape/ShapeLights.h>
#include <tsre/shape/ShapeLod.h>

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
    // Its files read again (the .sd too), its author messages told again
    // (forgetReports).
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
    // How far its last level is drawn on (task shapes 16): the .sd's
    // ESD_TSREDistanceLevelExtension, a glTF scene's extras.TSRE_distanceLevelExtension.
    virtual ShapeLod::Extension distanceLevelExtension() const { return ShapeLod::Extension::Default; }
    // When its lights are on (task renderer 46): the .sd's
    // ESD_TSRELightSchedule, a glTF scene's extras.TSRE_lightSchedule.
    virtual ShapeLights::Schedule lightSchedule() const { return ShapeLights::Schedule::Default; }
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
    // The level a shape's shadow is drawn with where it stands for itself
    // (the lamps' tiles over a moving train, task renderer 46 A), -1 for
    // none: for now its shadow floor level, else its coarsest (its only one
    // for most rolling stock); a level made for it at load may take its
    // place (task 17), the renderers staying as they are.
    virtual int shadowLevel() const {
        const int floor = shadowFloorLevel();
        return floor >= 0 ? floor : distanceLevelCount() - 1;
    }
    // The level cast into the shadow maps, set each frame (ShapeLod); -1
    // casts the view's.
    virtual void setShadowDistanceLevel(unsigned int stateId, int level) {
        (void)stateId;
        (void)level;
    }
    virtual void enableSubObjByName(unsigned int stateId, const QString& name, bool val);
    virtual void enableSubObjByNameQueue(unsigned int stateId, const QString& name, bool val);
    virtual void updateSim(float deltaTime, unsigned int stateId = 0) = 0;

    // Its parts (task shapes 12): its matrices, as it names them, in the
    // most detailed level's hierarchy (-1: at the root), each one's rest
    // transform relative to its parent (4x4, column major) and the last
    // frame of its keys (-1 without keys); the animation's frame count and
    // rate (frames a second), 0 without one. Empty until loaded.
    virtual int partCount() const { return 0; }
    virtual QString partName(int part) const;
    virtual int partParent(int part) const;
    virtual bool partRest(int part, float *out) const;
    virtual float partLastFrame(int part) const;
    // The part's transform relative to its parent at a frame of its keys
    // (its rest transform without keys).
    virtual bool partAtFrame(int part, float frame, float *out) const;
    virtual float animationFrames() const { return 0.0f; }
    virtual float animationRate() const { return 0.0f; }
    // A state's pose (rolling stock: wheels, bogies, doors): a part at a
    // frame of its keys (ShapeAnimation), or at a transform of its own,
    // relative to its parent (null: back to its keys or rest). The other
    // parts stay at rest, or loop with the state's animation (setAnimated).
    virtual void setPartFrame(unsigned int stateId, int part, float frame);
    virtual void setPartTransform(unsigned int stateId, int part, const float *local);
    virtual void clearPose(unsigned int stateId);
    // A part as the state poses it, with its parents, in the most detailed
    // level: the space the shape's vertices are drawn in. False when the
    // shape is not loaded or has no such part.
    virtual bool partMatrix(unsigned int stateId, int part, float *out) const;
    // A state no longer used (a car gone): newState() may give it again.
    virtual void releaseState(unsigned int stateId);
    // A state's texture overrides (task scripting 02; generic): one of the
    // shape's images (as its `images` name it, without a folder, any case)
    // drawn with a renderer's texture object instead
    // (RenderItem::Material::textureObject); 0 draws its own again. Every
    // part with that image shows it, without its night texture; its
    // material as the shape has it, or `surface`'s preset
    // (ShapeDisplays::applySurface). Kept until changed or the state is
    // released.
    virtual void setTextureOverride(unsigned int stateId, const QString &image, unsigned int textureObject,
                                    ShapeDisplays::Display::Surface surface);
    // Its displays (task scripting 02): its .sd's ESD_TSREDisplay blocks,
    // read with the .sd as it loads; none until loaded. The blocks left
    // out, why (ShapeDisplays::leftOut), for the messages.
    virtual const QVector<ShapeDisplays::Display> &displays() const;
    virtual const QStringList &displayProblems() const;
    // Its lights (task shapes 14): its .sd's ESD_TSRELight blocks (S
    // shapes; drawn with its packets) or its punctual lights (glTF, in the
    // shape's space); none until loaded. The .sd's problems (blocks left
    // out, values taken in their place), for the messages.
    virtual const QVector<ShapeLights::Light> &lights() const;
    virtual const QStringList &lightProblems() const;
    // A light's strength in a state (task shapes 14): 0 off to 1 its full
    // light, from what switches it (an ENG light bound to it); below 0 its
    // own (On). Kept until changed or the state is released.
    virtual void setLightStrength(unsigned int stateId, int light, float strength);
    // Its lights alone, without its parts: a car's, seen from its cab.
    virtual void pushLights(RenderQueue &queue, unsigned int stateId);
    // Its emitters (task shapes 15): its .sd's ESD_TSREEmitter blocks (S
    // shapes); none until loaded. The .sd's problems, for the messages.
    virtual const QVector<ShapeEmitters::Emitter> &emitters() const;
    virtual const QStringList &emitterProblems() const;
    // Its images' file names, as its `images` list has them (without a
    // folder); empty when not known (a display's Image checked).
    virtual QStringList imageNames() const;

    virtual void pushRenderItem(RenderQueue &queue) = 0;
    // A ShapePart selection id (task shapes 13) picks a primitive: each is
    // submitted with its number (SelectionIdCodec::forShapePart).
    virtual void pushRenderItem(RenderQueue &queue, quint32 selectionId, unsigned int stateId) = 0;
    // The primitives of the state's drawn distance level, numbered over its
    // sub-objects and their primitives in order, disabled sub-objects
    // included (as picked; not the animation's parts above): how many, and
    // the material each draws with (MSTS: its primitive state), -1 unknown.
    virtual int drawnPrimitiveCount(unsigned int stateId) const;
    virtual int drawnPrimitiveMaterial(unsigned int stateId, int primitive) const;
    // Submits these primitives of the state again over the shape in a flat
    // colour (rgba; alpha its opacity): picked primitives highlighted.
    virtual void pushPrimitiveHighlight(RenderQueue &queue, unsigned int stateId,
                                        const QVector<int> &primitives, const float *color);

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

    // Author messages about a shape (its .sd's mistakes: ShapeDisplays::
    // report), told once a load: true the first time `topic` asks for the
    // shape at `path`, again after forgetReports (a reload).
    static bool firstReport(const QString &path, const char *topic);
    static void forgetReports(const QString &path);

private:
    static QHash<QString, QSet<QByteArray>> &reported();
};

inline QHash<QString, QSet<QByteArray>> &ComplexShape::reported() {
    static QHash<QString, QSet<QByteArray>> told;
    return told;
}

inline bool ComplexShape::firstReport(const QString &path, const char *topic) {
    QSet<QByteArray> &topics = reported()[path];
    const QByteArray key(topic);
    if (topics.contains(key))
        return false;
    topics.insert(key);
    return true;
}

inline void ComplexShape::forgetReports(const QString &path) {
    reported().remove(path);
}

inline QString ComplexShape::partName(int part) const {
    (void)part;
    return QString();
}

inline int ComplexShape::partParent(int part) const {
    (void)part;
    return -1;
}

inline bool ComplexShape::partRest(int part, float *out) const {
    (void)part;
    (void)out;
    return false;
}

inline bool ComplexShape::partAtFrame(int part, float frame, float *out) const {
    (void)frame;
    return partRest(part, out);
}

inline float ComplexShape::partLastFrame(int part) const {
    (void)part;
    return -1.0f;
}

inline void ComplexShape::setPartFrame(unsigned int stateId, int part, float frame) {
    (void)stateId;
    (void)part;
    (void)frame;
}

inline void ComplexShape::setPartTransform(unsigned int stateId, int part, const float *local) {
    (void)stateId;
    (void)part;
    (void)local;
}

inline void ComplexShape::clearPose(unsigned int stateId) {
    (void)stateId;
}

inline bool ComplexShape::partMatrix(unsigned int stateId, int part, float *out) const {
    (void)stateId;
    (void)part;
    (void)out;
    return false;
}

inline void ComplexShape::setTextureOverride(unsigned int stateId, const QString &image,
                                              unsigned int textureObject, ShapeDisplays::Display::Surface surface) {
    (void)stateId;
    (void)image;
    (void)textureObject;
    (void)surface;
}

inline void ComplexShape::releaseState(unsigned int stateId) {
    (void)stateId;
}

inline const QVector<ShapeDisplays::Display> &ComplexShape::displays() const {
    static const QVector<ShapeDisplays::Display> none;
    return none;
}

inline const QStringList &ComplexShape::displayProblems() const {
    static const QStringList none;
    return none;
}

inline const QVector<ShapeLights::Light> &ComplexShape::lights() const {
    static const QVector<ShapeLights::Light> none;
    return none;
}

inline const QStringList &ComplexShape::lightProblems() const {
    static const QStringList none;
    return none;
}

inline void ComplexShape::setLightStrength(unsigned int stateId, int light, float strength) {
    (void)stateId;
    (void)light;
    (void)strength;
}

inline void ComplexShape::pushLights(RenderQueue &queue, unsigned int stateId) {
    (void)queue;
    (void)stateId;
}

inline const QVector<ShapeEmitters::Emitter> &ComplexShape::emitters() const {
    static const QVector<ShapeEmitters::Emitter> none;
    return none;
}

inline const QStringList &ComplexShape::emitterProblems() const {
    static const QStringList none;
    return none;
}

inline QStringList ComplexShape::imageNames() const {
    return QStringList();
}

inline int ComplexShape::drawnPrimitiveCount(unsigned int stateId) const {
    (void)stateId;
    return 0;
}

inline int ComplexShape::drawnPrimitiveMaterial(unsigned int stateId, int primitive) const {
    (void)stateId;
    (void)primitive;
    return -1;
}

inline void ComplexShape::pushPrimitiveHighlight(RenderQueue &queue, unsigned int stateId,
                                                 const QVector<int> &primitives, const float *color) {
    (void)queue;
    (void)stateId;
    (void)primitives;
    (void)color;
}

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
