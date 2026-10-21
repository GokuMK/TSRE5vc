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

#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>
#include <algorithm>
#include <tsre/renderer/MeshHandle.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/shape/ComplexShape.h>

class RenderQueue;

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
    void updateSim(float deltaTime, unsigned int stateId = 0) override;

    void pushRenderItem(RenderQueue &queue) override;
    void pushRenderItem(RenderQueue &queue, quint32 selectionId, unsigned int stateId) override;

    void invalidateRenderState(bool invalidateMatrixCache = true) override;
    void enablePart(unsigned int uid, unsigned int stateId = 0) override;
    void disablePart(unsigned int uid, unsigned int stateId = 0) override;

    void fillShapeTextureInfo(QHash<int, ShapeTextureInfo*> &list, unsigned int stateId = 0) override;
    void fillShapeHierarchyInfo(ShapeHierarchyInfo* info, unsigned int stateId = 0) override;
    void fillContentHierarchyInfo(QVector<ContentHierarchyInfo*> &list, int parent) override;

private:
    struct State {
        bool animated = false;
        // The level of detail drawn (MSFT_lod), set by ShapeLod.
        int level = 0;
        // The impostor's frame facing the camera, set by ShapeLod.
        int frame = 0;
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
    // KHR_lights_punctual lights in shape space.
    QVector<RenderItem::Light> punctualLights;
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
    void syncTextureAddresses();
    unsigned long long getTextureStateHash() const;
};

#endif /* GLTFSHAPE_H */

