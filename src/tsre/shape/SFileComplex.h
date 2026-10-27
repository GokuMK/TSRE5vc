#pragma once
#include <QStringList>
#include <QVector>
#include <memory>
#include <vector>
#include <tsre/shape/MstsMaterials.h>

class QMatrix4x4;
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/ShapeAnimation.h>

class RenderQueue;

class SFileComplex final : public ComplexShape {
  public:
    enum class Retention { Unloaded, Complete, Partial, Compact };
    enum class Health { Valid, Recovered, Broken };
    enum class GpuState { NotInitialized, Ready, Failed };
    enum class Format { Text, Binary };
    struct Statistics {
        int matrices = 0, images = 0, lods = 0, subobjects = 0, parts = 0, vertices = 0,
            animations = 0;
        // Estimates exclude allocator overhead and the shared texture cache.
        qint64 loadDocumentBytes = 0, sourceBlocks = 0, sourceScalars = 0, skippedSourceBlocks = 0;
        qint64 documentBytes = 0, sourceGeometryBytes = 0, runtimeBytes = 0, gpuBytes = 0;
        // Last CPU load stages; read includes file I/O, inflation and parsing.
        double readMs = 0, extractMs = 0, metadataMs = 0, cleanupMs = 0;
    };
    SFileComplex(QString path, QString name, QString textureRoot);
    ~SFileComplex() override;
    SFileComplex(const SFileComplex &) = delete;
    SFileComplex &operator=(const SFileComplex &) = delete;
    const QString &getPathId() const override;
    const QString &getTexPath() const override;
    int getEsdDetailLevel() const override;
    bool isLoaded() const override;
    bool hasLoadFailed() const override;
    float getSize() const override;
    const float *getBound() const override;
    Retention retention() const;
    Health health() const;
    GpuState gpuState() const;
    QStringList diagnostics() const;
    Statistics statistics() const;
    bool storageFormat(Format &format, bool &compressed) const;
    bool setLoadOptions(const ShapeLoadOptions &options) override; // Only before CPU load.
    bool loadData(); // CPU only; retained broken documents remain inspectable.
    bool initGL();
    void releaseGL();
    bool compact(); // Requires successful GL initialization.
    bool reloadComplete();
    bool save(const QString &path, Format format, bool compressed, QString *error = nullptr);
    bool saveMetadata(const QString &path, bool compressed, QString *error = nullptr) const;
    // Complete-mode editing without exposing mutable document storage.
    // Paths: "points/point[0]"; prefix "sd/" selects metadata.
    // Indices select repeated block occurrences.
    QString field(const QString &blockPath, int scalarIndex) const;
    bool setField(const QString &blockPath, int scalarIndex, const QString &value,
                  QString *error = nullptr);

    // Materials (task shapes 13, the Shape Viewer's materials editor): the
    // primitive states. Their label, shader name, textures (image files in
    // tex_idxs order; PBR: base colour, ORM, normal map), MSTS light
    // material (their vertex state's, -5 to -12), alpha test, z-bias, the
    // matrix of their vertex state, the primitives drawn with them (all
    // levels) and PBR factors (the .sd's, keyed by the first texture).
    struct MaterialInfo {
        QString name;
        QString shader;
        QStringList textures;
        int lightMaterial = -5;
        bool alphaTest = false;
        float zBias = 0.0f;
        int matrix = 0;
        QString matrixName;
        int uses = 0;
        MstsMaterials::Factors factors;
    };
    int materialCount() const;
    bool materialInfo(int material, MaterialInfo &out) const;
    // The distance level a state draws, and the primitives of a level
    // drawn with a material (ComplexShape's numbering).
    int drawnLevel(unsigned int stateId) const;
    QVector<int> primitivesWithMaterial(int level, int material) const;
    // Edits, in Complete mode; each extracts the shape again (the view
    // draws it at once). setMaterial: all but the matrix and uses; a
    // vertex state of its own when its light material changes and others
    // share it. addMaterial: a copy, its index (-1 failed). removeMaterial:
    // an unused one. assignMaterial: these primitives of a level drawn with
    // the material, each at its own matrix (states for other matrices found
    // or made); with allLevels the other levels' primitives of the same
    // matrix and material too.
    bool setMaterial(int material, const MaterialInfo &info, QString *error = nullptr);
    int addMaterial(int copyOf, QString *error = nullptr);
    bool removeMaterial(int material, QString *error = nullptr);
    bool assignMaterial(int level, const QVector<int> &primitives, int material, bool allLevels,
                        QString *error = nullptr);
    // The .sd changed (PBR factors), and saving it beside a shape file (made
    // when the shape had none).
    bool metadataEdited() const;
    bool saveMetadataFor(const QString &shapePath, QString *error = nullptr);
    // Undo: the shape's documents (S and .sd) as they are, and back.
    struct Snapshot;
    std::shared_ptr<const Snapshot> snapshot() const;
    static qint64 snapshotBytes(const Snapshot &snapshot);
    bool restore(const Snapshot &snapshot, QString *error = nullptr);
    void load() override;
    ComplexShape *detachedCopy() const override;
    void loadDetached() override;
    void adopt(ComplexShape &copy) override;
    void reload() override;
    unsigned int newState() override;
    void releaseState(unsigned int) override;
    int partCount() const override;
    QString partName(int) const override;
    int partParent(int) const override;
    bool partRest(int, float *) const override;
    float partLastFrame(int) const override;
    bool partAtFrame(int, float, float *) const override;
    float animationFrames() const override;
    float animationRate() const override;
    void setPartFrame(unsigned int, int, float) override;
    void setPartTransform(unsigned int, int, const float *) override;
    void clearPose(unsigned int) override;
    bool partMatrix(unsigned int, int, float *) const override;
    void setAnimated(unsigned int, bool) override;
    void setEnabledSubObjs(unsigned int, unsigned int) override;
    void setCurrentDistanceLevel(unsigned int, int) override;
    int distanceLevelCount() const override;
    float distanceLevelDistance(int) const override;
    void enableSubObjByName(unsigned int, const QString &, bool) override;
    void enableSubObjByNameQueue(unsigned int, const QString &, bool) override;
    void updateSim(float, unsigned int = 0) override;
    void pushRenderItem(RenderQueue &queue) override;
    void pushRenderItem(RenderQueue &queue, quint32, unsigned int) override;
    int drawnPrimitiveCount(unsigned int) const override;
    int drawnPrimitiveMaterial(unsigned int, int) const override;
    void pushPrimitiveHighlight(RenderQueue &, unsigned int, const QVector<int> &, const float *) override;
    void invalidateRenderState(bool = true) override;
    void enablePart(unsigned int, unsigned int = 0) override;
    void disablePart(unsigned int, unsigned int = 0) override;
    bool getBoxPoints(QVector<float> &) override;
    void getFloorBorderLinePoints(float *&) override;
    bool isSnapable() const override;
    void addSnapablePoints(QVector<float> &) override;
    bool matrixByName(const QString &, float *) const override;
    void fillShapeTextureInfo(QHash<int, ShapeTextureInfo *> &, unsigned int = 0) override;
    void fillShapeHierarchyInfo(ShapeHierarchyInfo *, unsigned int = 0) override;
    void fillContentHierarchyInfo(QVector<ContentHierarchyInfo *> &, int) override;

  private:
    struct Data;
    std::unique_ptr<Data> d;
    bool prepare(unsigned int);
    bool extract();
    void loadMetadata(bool readFile = true);
    void updateMatrices(unsigned int);
    // Hierarchy matrices of a LOD as the pose has its parts (null: none),
    // the others looping at `frame` when `looping`, else at rest.
    void fillMatrices(int lod, const ShapeAnimation::Pose *pose, bool looping, float frame,
                      std::vector<QMatrix4x4> &out) const;
    // Static pose of a LOD, shared by non-animated states.
    const std::vector<QMatrix4x4> &staticMatrices(int lod);
    // Identifies the parts a state draws, for sharing static packets.
    QByteArray packetKey(unsigned int) const;
    void clearSharedPackets();
    void syncTextures();
    void releaseTextures();
    // Each PBR material's factors from the .sd's (task shapes 13).
    void resolvePbrFactors();
    // A mesh's PBR parts with tangents (initGL).
    void buildPbrBuffer(size_t lod, size_t mesh);
    bool editable(QString *error) const;
    void afterMaterialEdit();
    void readPbrFactors();
    bool ensureMetadata(QString *error);
    bool setPbrFactors(const QString &key, const MstsMaterials::Factors &factors, QString *error);
};
