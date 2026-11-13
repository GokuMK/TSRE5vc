/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef GLTFSHAPE_H
#define GLTFSHAPE_H

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>
#include <algorithm>
#include <memory>
#include <vector>
#include <tsre/renderer/MeshHandle.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/shape/ComplexShape.h>

class RenderQueue;
class SkeletalAnimation;

class ContentHierarchyInfo;
class RenderItem;
class ShapeHierarchyInfo;
class ShapeTextureInfo;
class Texture;

class GltfShape : public ComplexShape {
public:
    GltfShape(QString pathid, QString name, QString texPath);
    ~GltfShape() override;

    const QString& getPathId() const override { return pathid; }
    const QString& getTexPath() const override { return texPath; }

    bool isLoaded() const override { return loaded == 1; }
    bool hasLoadFailed() const override { return loaded == 2; }
    float getSize() const override { return size; }
    const float* getBound() const override { return bound; }
    bool getBoxPoints(QVector<float> &points) override;

    void load() override;
    ComplexShape *detachedCopy() const override;
    void loadDetached() override;
    void adopt(ComplexShape &copy) override;
    void reload() override;

    unsigned int newState() override;
    void setAnimated(unsigned int stateId, bool animated) override;
    void setCurrentDistanceLevel(unsigned int stateId, int level) override;
    int distanceLevelCount() const override;
    float distanceLevelCoverage(int level) const override;
    void setCameraDirection(unsigned int stateId, const float direction[3]) override;
    // Trees (an impostor): the last mesh level before the impostor, a real
    // mesh in both bindings (ReplaceLast's cruciform is never drawn).
    int shadowFloorLevel() const override;
    void setShadowDistanceLevel(unsigned int stateId, int level) override;
    void updateSim(float deltaTime, unsigned int stateId = 0) override;

    void pushRenderItem(RenderQueue &queue) override;
    void pushRenderItem(RenderQueue &queue, quint32 selectionId, unsigned int stateId) override;

    void invalidateRenderState(bool invalidateMatrixCache = true) override;
    void enablePart(unsigned int uid, unsigned int stateId = 0) override;
    void disablePart(unsigned int uid, unsigned int stateId = 0) override;

    void fillShapeTextureInfo(QHash<int, ShapeTextureInfo*> &list, unsigned int stateId = 0) override;
    void fillShapeHierarchyInfo(ShapeHierarchyInfo* info, unsigned int stateId = 0) override;
    void fillContentHierarchyInfo(QVector<ContentHierarchyInfo*> &list, int parent) override;
    const QVector<ShapeLights::Light> &lights() const override { return shapeLights; }
    ShapeLod::Extension distanceLevelExtension() const override { return extension; }
    ShapeLights::Schedule lightSchedule() const override { return schedule; }
    void setLightStrength(unsigned int stateId, int light, float strength) override {
        drawnLights.setStrength(*this, stateId, light, strength);
    }
    void pushLights(RenderQueue &queue, unsigned int stateId) override {
        drawnLights.push(queue, *this, stateId, 0, ShapeLights::Drawn::What::Lights);
    }
    void releaseState(unsigned int stateId) override;

    // Clips (task humans 02): the file's animations, played per state.
    int clipCount() const override;
    QString clipName(int clip) const override;
    float clipDuration(int clip) const override;
    // The file's word on a clip (animation extras TSRE_use, TSRE_weight:
    // SkeletalAnimation::clipUse, clipWeight).
    QString clipUse(int clip) const;
    float clipWeight(int clip) const;
    void setClips(unsigned int stateId, const ClipPlay *plays, int count) override;
    // Samples the state's clips now (pushRenderItem does when they changed);
    // for callers sampling many states at once.
    void updatePose(unsigned int stateId);
    // The skeleton (null without one), a node's matrix in the state's pose
    // (shape space, column major) and a skin's joint rows (RenderItem::Skin),
    // as last sampled; null when there are none.
    const SkeletalAnimation *skeleton() const { return rig.get(); }
    // Adds another loaded shape's clips (a shared clips file), bound by node
    // names, `suffix` added to their names (SkeletalAnimation::addClips).
    // Returns how many; both must have skeletons.
    int addClipsFrom(const GltfShape &other, const QString &suffix);
    const float *poseNodeMatrix(unsigned int stateId, int node) const;
    const float *poseJointRows(unsigned int stateId, int skin) const;
    // The skeleton shared, for work on another thread (an impostor's bake).
    std::shared_ptr<const SkeletalAnimation> sharedSkeleton() const { return rig; }

    // Loaded with ShapeLoadOption::SkinnedTriangles (task humans 03): one
    // level's skinned primitives as drawn, for baking an impostor on a
    // worker. The level: 1 (a fifth of the triangles for humans-gen's
    // humans), else the only one. Vertices in the PBR_SKINNED layout, three
    // a triangle; each primitive's skin, material factors, alpha mode and
    // its base colour and normal images (a file, or the encoded bytes from
    // the glTF file; decoded where they are used). Null without skins or
    // the option; dropSkinnedSource() once used.
    struct SkinnedSource {
        struct Image {
            QString path;
            QByteArray bytes;
            bool valid() const { return !path.isEmpty() || !bytes.isEmpty(); }
        };
        struct Primitive {
            int skin = 0;
            std::vector<float> vertices;
            float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            // MASK's cutoff; negative: opaque (BLEND drawn as opaque).
            float alphaCutoff = -1.0f;
            float normalScale = 1.0f;
            float roughness = 1.0f;
            float metallic = 1.0f;
            Image color, normal;
        };
        int level = 0;
        std::vector<Primitive> primitives;
    };
    std::shared_ptr<const SkinnedSource> skinnedSource() const { return bakeSource; }
    // A file's triangles on the CPU (task humans 04: walk areas), on any
    // thread: the most detailed level's unskinned primitives in the shape's
    // space (nine floats a triangle), without textures or GPU meshes, and
    // its scene's extras.TSRE_walk (empty without). False when it does not
    // load.
    static bool readTriangles(const QString &path, const QString &texPath, std::vector<float> &triangles,
                              QString *walk = nullptr);
    void dropSkinnedSource() { bakeSource.reset(); }
    bool setLoadOptions(const ShapeLoadOptions &options) override;

private:
    // A state's animation (task humans 02): what it plays, its pose, and
    // its copies of the level packets pointing at the pose.
    struct StateAnimation;

    struct State {
        bool animated = false;
        // The level of detail drawn (MSFT_lod), set by ShapeLod.
        int level = 0;
        // The impostor's frame facing the camera, set by ShapeLod.
        int frame = 0;
        // The level cast into the shadow maps, -1 for the view's (ShapeLod).
        int shadowLevel = -1;
        // Only for a shape with a skeleton (task humans 02).
        std::shared_ptr<StateAnimation> animation;
    };

    struct MaterialRuntime {
        int texId = -1;
        int texAddr = -1;
        bool hasTexture = false;
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float alphaAttr = 1.0f;
        // RenderItem::Surface from the glTF alpha mode.
        unsigned char surface = 0;
        bool doubleSided = false;
        // Metallic-roughness parameters and the TexLib ids of the other maps.
        RenderItem::Pbr pbr;
        QString debugName;
    };

    struct MeshPrimitiveGpu {
        // Renderer-owned PBR-layout vertices.
        MeshHandle mesh;
        int vertCount = 0;
        MaterialRuntime material;
        // Sphere around the primitive's vertices, for view culling.
        float boundCenter[3] = {0.0f, 0.0f, 0.0f};
        float boundRadius = -1.0f;
        // Lights standing in for its emission, in mesh space.
        QVector<RenderItem::Light> lights;
        // Skinned (task humans 02): its vertices carry joints and weights,
        // for any skin a node draws its mesh with; per such skin, the
        // farthest any vertex lies from a joint moving it (bind pose).
        bool skinned = false;
        QHash<int, float> skinMargins;
    };

    struct MeshGpu {
        QVector<MeshPrimitiveGpu*> primitives;
        float min[3] = {0.0f, 0.0f, 0.0f};
        float max[3] = {0.0f, 0.0f, 0.0f};
        bool hasBounds = false;
    };

    struct DrawUnit {
        int nodeIndex = -1;
        int meshIndex = -1;
        int primitiveIndex = -1;
        bool enabled = true;
        // MSFT_lod: the level its node belongs to (-1 every level) and the
        // last level of its node's group, drawn for the levels past it.
        int level = -1;
        int lastLevel = 0;
        // The impostor's frame it draws (-1 for meshes).
        int frame = -1;
        // The skin its node draws its mesh with (task humans 02), -1 none.
        int skin = -1;
        bool drawnAt(int shapeLevel) const {
            return level < 0 || level == std::min(shapeLevel, lastLevel);
        }
    };

    QString pathid;
    QString name;
    QString texPath;

    int isinit = 1;
    int loaded = 0; // 0=not loaded, 1=loaded, 2=loading/failed

    float size = 0.0f;
    float bound[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    QVector<State> state;

    QVector<MeshGpu*> meshes;
    QVector<DrawUnit> drawUnits;
    QVector<float*> nodeWorldMatrices; // owned (float[16])
    QVector<QString> nodeNames;
    QVector<int> nodeParents;
    // KHR_lights_punctual lights in shape space, as the shape's lights with
    // their names, drawn per state (task shapes 14).
    QVector<ShapeLights::Light> shapeLights;
    ShapeLights::Drawn drawnLights;
    // How far its last level is drawn on: its scene's
    // extras.TSRE_distanceLevelExtension (task shapes 16).
    ShapeLod::Extension extension = ShapeLod::Extension::Default;
    // When its lights are on: its scene's extras.TSRE_lightSchedule (task
    // renderer 46).
    ShapeLights::Schedule schedule = ShapeLights::Schedule::Default;
    // Levels of detail, internal (MSFT_lod) or external (companion files):
    // how many (0 without them) and the least screen coverage of each
    // (MSFT_screencoverage).
    int lodLevels = 0;
    QVector<float> lodCoverage;
    // The scene root's MSFT_screencoverage, for external levels.
    QVector<float> rootCoverage;
    // Whether external levels are looked for (not in a level file itself).
    bool discoverLevels = true;
    // The impostor's level (-1 without one) and its frames' directions
    // (three values each): only its quads draw at that level.
    int impostorLevel = -1;
    QVector<float> impostorDirections;

    bool requiresUpdate = false;

    // A detached copy loading on a worker (ShapeLoader) registers no
    // textures: it records them, with ids PendingTextureBase + index in the
    // materials, and adopt() registers them on the main thread.
    struct PendingTexture {
        QString path;
        Texture *embedded = nullptr; // owned until registered
    };
    static constexpr int PendingTextureBase = 1 << 30;
    bool deferTextures = false;
    QVector<PendingTexture> pendingTextures;
    int addTexture(const QString &path, Texture *embedded);
    // Packets do not depend on the instance state, so every state submits
    // the set under key 0 and repeated objects draw instanced.
    QHash<unsigned int, QVector<RenderItem *>> renderItems;
    // The draw unit of each packet of a set, and how often the set was
    // built (task humans 02: the states' copies follow it; a rebuilt set's
    // packets may reuse the old ones' addresses).
    QHash<unsigned int, QVector<int>> renderItemUnits;
    QHash<unsigned int, unsigned int> renderItemsBuilt;
    // The file's skeleton and clips (task humans 02), null without
    // animations and skins: such a shape draws as before.
    std::shared_ptr<SkeletalAnimation> rig;
    // ShapeLoadOption::SkinnedTriangles, and what it keeps.
    bool keepSkinned = false;
    std::shared_ptr<const SkinnedSource> bakeSource;
    // readTriangles: no textures; level 0's triangles kept here; the scene's
    // TSRE_walk.
    bool cpuTriangles = false;
    std::vector<float> cpuTriangleData;
    QString walkRole;
    StateAnimation &animationOf(unsigned int stateId);
    // The state's copies of a level's packets, posed.
    QVector<RenderItem *> &statePackets(unsigned int stateId, unsigned int key, const QVector<RenderItem *> &shared);
    void releaseAnimation(State &s);
    QHash<unsigned int, unsigned long long> renderItemsTextureHash;
    // The textures are synced once a frame (Renderer::frameNumber()), not
    // per object: a forest pushes the shape thousands of times.
    quint64 texturesSyncedFrame = ~quint64(0);
    unsigned long long textureStateHash = 0;

    void cleanupGpu();
    void cleanupNodeMatrices();
    void cleanupRenderItems();

    bool parseAndBuild();
    // The file of the shape alone, without its external levels.
    bool parseFile();
    // External levels: "tree_LOD01.gltf" ... beside "tree.gltf", loaded
    // and appended (appendLevel()) as levels 1, 2...
    void loadExternalLevels();
    void appendLevel(GltfShape &level, int index, int lastLevel);
    // The impostor beside the shape ("tree.impostor"): a quad per frame, in
    // the frame's capture plane, after the last mesh level (Append) or
    // instead of it (ReplaceLast).
    void loadImpostor();
    // The packets of a level (and an impostor frame, -1 for meshes), built
    // or rebuilt when missing or their textures changed.
    QVector<RenderItem *> &levelPackets(int level, int frame, bool checkTextures);
    static unsigned int levelKey(int level, int frame) { return unsigned(level) | (unsigned(frame + 1) << 8); }
    void syncTextureAddresses();
    unsigned long long getTextureStateHash() const;
};

#endif /* GLTFSHAPE_H */

