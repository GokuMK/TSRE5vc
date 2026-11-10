/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef E3DSHAPE_H
#define E3DSHAPE_H

#include <QHash>
#include <QString>
#include <QVector>
#include <deque>
#include <memory>
#include <vector>
#include <tsre/renderer/MeshHandle.h>
#include <tsre/shape/ComplexShape.h>

class RenderItem;
class RenderQueue;
struct E3dModel;
struct MaszynaMaterial;

// A MaSzyna model as a shape (task maszyna 01): one vertex and index buffer,
// shared with everything using the file; a packet per mesh part and pass, as
// ranges of it. A state holds its pose, what it hides, its level, its
// repaint and its sections' lights; states with the same repaint share their
// packets, which draw every section unlit.
class E3dShape : public ComplexShape {
public:
    E3dShape(QString pathid, QString name, QString texPath);
    ~E3dShape() override;

    const QString& getPathId() const override { return pathid; }
    const QString& getTexPath() const override { return texPath; }

    bool isLoaded() const override { return loaded == 1; }
    bool hasLoadFailed() const override { return loaded == 2; }
    float getSize() const override;
    const float* getBound() const override;
    bool getBoxPoints(QVector<float> &points) override;

    void load() override;
    void reload() override;
    ComplexShape *detachedCopy() const override;
    void loadDetached() override;
    void adopt(ComplexShape &copy) override;

    unsigned int newState() override;
    void releaseState(unsigned int stateId) override;
    void setAnimated(unsigned int stateId, bool animated) override;
    void setCurrentDistanceLevel(unsigned int stateId, int level) override;
    int distanceLevelCount() const override;
    float distanceLevelDistance(int level) const override;
    void enableSubObjByName(unsigned int stateId, const QString& name, bool val) override;
    void enableSubObjByNameQueue(unsigned int stateId, const QString& name, bool val) override;
    void updateSim(float deltaTime, unsigned int stateId = 0) override;
    void setRepaint(unsigned int stateId, const QString &repaint) override;
    void setSectionLight(unsigned int stateId, const QString &section, float level) override;

    int partCount() const override;
    QString partName(int part) const override;
    int partParent(int part) const override;
    bool partRest(int part, float *out) const override;
    void setPartTransform(unsigned int stateId, int part, const float *local) override;
    void clearPose(unsigned int stateId) override;
    bool partMatrix(unsigned int stateId, int part, float *out) const override;
    bool matrixByName(const QString &name, float *out) const override;
    QStringList imageNames() const override;
    bool usedFiles(const QString &repaint, QStringList &found, QStringList &missing) const override;

    void pushRenderItem(RenderQueue &queue) override;
    void pushRenderItem(RenderQueue &queue, quint32 selectionId, unsigned int stateId) override;
    int drawnPrimitiveCount(unsigned int stateId) const override;
    int drawnPrimitiveMaterial(unsigned int stateId, int primitive) const override;
    void pushPrimitiveHighlight(RenderQueue &queue, unsigned int stateId,
                                const QVector<int> &primitives, const float *color) override;

    void invalidateRenderState(bool invalidateMatrixCache = true) override;
    void enablePart(unsigned int uid, unsigned int stateId = 0) override;
    void disablePart(unsigned int uid, unsigned int stateId = 0) override;

    void fillShapeTextureInfo(QHash<int, ShapeTextureInfo*> &list, unsigned int stateId = 0) override;
    void fillShapeHierarchyInfo(ShapeHierarchyInfo* info, unsigned int stateId = 0) override;
    void fillContentHierarchyInfo(QVector<ContentHierarchyInfo*> &list, int parent) override;

private:
    // A material as drawn: what it read, and its TexLib textures once
    // registered (the main thread's).
    struct Material {
        std::shared_ptr<const MaszynaMaterial> source;
        QString name;
        int texture = -1, normal = -1, orm = -1;
        bool registered = false;
    };

    static constexpr int SlotCount = 4;
    struct Repaint {
        QString name;
        Material slot[SlotCount];
        bool made = false;
    };

    // A level's packets for a repaint: every mesh part and pass in it. Made
    // once, shared by the states drawing them; again once the textures it
    // waited for have told their alpha. The packets of parts without a skin
    // are one set per level (NoSkins), shared by its repaints.
    struct PacketSet {
        QVector<RenderItem *> owned;    // those this set made
        QVector<RenderItem *> all;
        std::vector<int> parts;         // each packet's part
        // Each packet's: in a section, and glowing when the section is lit.
        std::vector<unsigned char> glows;
        QVector<RenderItem *> atRest;   // those a state hiding nothing draws
        QVector<int> waiting;           // textures whose alpha is not known
        quint64 checkedFrame = ~quint64(0);
        unsigned generation = 0;
        unsigned commonGeneration = 0;
        bool built = false;
    };
    static constexpr int NoSkins = -2;

    // A state's copies of the shared packets of the parts its pose moves or
    // its light reaches.
    struct Pool {
        QVector<RenderItem *> items;
        unsigned generation = 0;
    };

    struct State {
        bool free = false;
        int level = 0;
        int repaint = -1;
        // Only for a state away from the rest pose: which parts are posed
        // and their local matrices.
        std::vector<unsigned char> posed;
        std::vector<float> locals;      // 16 a part
        std::vector<float> world;       // 16 a part
        std::vector<unsigned char> moved; // the part or a parent is posed
        bool worldStale = false;
        int posedParts = 0;
        // Only for a state changing what is shown: each part's own wish, and
        // with its parents'.
        std::vector<unsigned char> shown, showing;
        // Only for a state lighting a section: the lights above 0 by section
        // name (lower case), as set before or after a load, and by the
        // model's sections once matched.
        QHash<QString, float> lights;
        std::vector<float> sectionLight;
        int litSections = 0;
        bool lightsStale = false;
        QHash<unsigned, Pool> pools;
    };

    QString pathid, name, texPath, vehicleFolder;
    int loaded = 0; // 0 not loaded, 1 loaded, 2 failed
    bool freshRead = false;
    QString failure;

    std::shared_ptr<const E3dModel> model;
    MeshHandle mesh;
    std::vector<float> restWorld;           // 16 a part
    std::vector<unsigned char> restShowing;
    QVector<Material> materials;            // as model->materialNames
    QVector<Repaint> repaints;
    QHash<QString, int> repaintByName;
    std::deque<State> state;        // stable: packets point at their matrices

    QHash<unsigned, PacketSet> sets;
    unsigned generations = 0;

    bool read();
    void finishLoad();
    void registerTextures(Material &material);
    void releaseTextures(Material &material);
    void report(const Material &material);
    void makeRepaint(Repaint &repaint);
    // The materials a repaint names, by slot.
    QStringList repaintMaterials(const QString &repaint) const;
    void retirePackets();
    void retirePool(Pool &pool);
    void resetState(State &s);

    const Material *materialOf(int partMaterial, int repaint) const;
    PacketSet &packets(int level, int repaint);
    void startSet(PacketSet &set);
    void build(PacketSet &set, int level, int repaint, const PacketSet *common);
    void addPart(PacketSet &set, int part, int repaint);
    void addPacket(PacketSet &set, int part, const Material *material, int pass, bool alphaTest);
    bool isCustom(const State &s) const { return s.posedParts > 0 || !s.showing.empty() || !s.lights.isEmpty(); }
    int levelOf(const State &s) const;
    static unsigned keyOf(int level, int repaint) { return unsigned(level) | unsigned(repaint + 2) << 8; }

    void updateWorld(State &s) const;
    void matchLights(State &s) const;
    void setShown(State &s, int part, bool on);
};

#endif // E3DSHAPE_H
