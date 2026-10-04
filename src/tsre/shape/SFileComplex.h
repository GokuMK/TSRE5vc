#pragma once
#include <QStringList>
#include <memory>
#include <vector>

class QMatrix4x4;
#include <tsre/shape/ComplexShape.h>

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
    void load() override;
    void reload() override;
    unsigned int newState() override;
    void setAnimated(unsigned int, bool) override;
    void setEnabledSubObjs(unsigned int, unsigned int) override;
    void setCurrentDistanceLevel(unsigned int, int) override;
    void enableSubObjByName(unsigned int, const QString &, bool) override;
    void enableSubObjByNameQueue(unsigned int, const QString &, bool) override;
    void updateSim(float, unsigned int = 0) override;
    void pushRenderItem(RenderQueue &queue) override;
    void pushRenderItem(RenderQueue &queue, quint32, unsigned int) override;
    void invalidateRenderState(bool = true) override;
    void enablePart(unsigned int, unsigned int = 0) override;
    void disablePart(unsigned int, unsigned int = 0) override;
    bool getBoxPoints(QVector<float> &) override;
    void getFloorBorderLinePoints(float *&) override;
    bool isSnapable() const override;
    void addSnapablePoints(QVector<float> &) override;
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
    // Hierarchy matrices of a LOD at an animation frame; animated false
    // gives the static pose.
    void fillMatrices(int lod, bool animated, float frame, std::vector<QMatrix4x4> &out) const;
    // Static pose of a LOD, shared by non-animated states.
    const std::vector<QMatrix4x4> &staticMatrices(int lod);
    // Identifies the parts a state draws, for sharing static packets.
    QByteArray packetKey(unsigned int) const;
    void clearSharedPackets();
    void syncTextures();
    void releaseTextures();
};
