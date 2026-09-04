/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef TERRAINMESHBACKEND_H
#define TERRAINMESHBACKEND_H

#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QHash>
#include <QVector>
#include <QtGlobal>

class RenderItem;
class Shader;
class Terrain;

enum TerrainMeshDirtyReason : unsigned int {
    TerrainDirtyNone = 0,
    TerrainDirtyHeight = 1u << 0,
    TerrainDirtyNormals = 1u << 1,
    TerrainDirtyGaps = 1u << 2,
    TerrainDirtyUvParams = 1u << 3,
    TerrainDirtyAll = TerrainDirtyHeight | TerrainDirtyNormals
            | TerrainDirtyGaps | TerrainDirtyUvParams
};

class TerrainMeshBackend {
public:
    explicit TerrainMeshBackend(Terrain &terrain);
    virtual ~TerrainMeshBackend();

    virtual bool isPaged() const = 0;
    virtual bool ensureInitialized() = 0;
    virtual void configureRenderItem(RenderItem &item, int patchId,
                                     bool mapPass, bool applyGaps,
                                     int sourceStep = 1,
                                     quint8 edgeMask = 0) = 0;
    virtual void drawPatch(int patchId, bool mapPass, bool applyGaps,
                           int sourceStep = 1,
                           quint8 edgeMask = 0) = 0;
    virtual void endDirectRender() = 0;
    virtual void invalidateAll(unsigned int reasons = TerrainDirtyAll) = 0;
    virtual void invalidatePatch(int patchId, unsigned int reasons) = 0;
    virtual void invalidateSamples(int minX, int minZ, int maxX, int maxZ,
                                   unsigned int reasons) = 0;
    virtual void refreshModified() = 0;

protected:
    Terrain &terrain;
};

class TerrainMeshLegacy final : public TerrainMeshBackend {
public:
    explicit TerrainMeshLegacy(Terrain &terrain);
    bool isPaged() const override;
    bool ensureInitialized() override;
    void configureRenderItem(RenderItem &item, int patchId,
                             bool mapPass, bool applyGaps,
                             int sourceStep = 1,
                             quint8 edgeMask = 0) override;
    void drawPatch(int patchId, bool mapPass, bool applyGaps,
                   int sourceStep = 1,
                   quint8 edgeMask = 0) override;
    void endDirectRender() override;
    void invalidateAll(unsigned int reasons = TerrainDirtyAll) override;
    void invalidatePatch(int patchId, unsigned int reasons) override;
    void invalidateSamples(int minX, int minZ, int maxX, int maxZ,
                           unsigned int reasons) override;
    void refreshModified() override;
};

#pragma pack(push, 1)
struct TerrainVertex8Derived {
    float height;
    quint32 packedNormal;
};
#pragma pack(pop)

struct TerrainPatchGpuParams {
    float uvAndOriginX[4];
    float uvAndOriginZ[4];
};

class TerrainMeshPaged final : public TerrainMeshBackend {
public:
    static constexpr int PatchesPerPage = 256;

    explicit TerrainMeshPaged(Terrain &terrain);
    ~TerrainMeshPaged() override;

    bool isPaged() const override;
    bool ensureInitialized() override;
    void configureRenderItem(RenderItem &item, int patchId,
                             bool mapPass, bool applyGaps,
                             int sourceStep = 1,
                             quint8 edgeMask = 0) override;
    void drawPatch(int patchId, bool mapPass, bool applyGaps,
                   int sourceStep = 1,
                   quint8 edgeMask = 0) override;
    void endDirectRender() override;
    void invalidateAll(unsigned int reasons = TerrainDirtyAll) override;
    void invalidatePatch(int patchId, unsigned int reasons) override;
    void invalidateSamples(int minX, int minZ, int maxX, int maxZ,
                           unsigned int reasons) override;
    void refreshModified() override;

    static QVector<quint16> buildRegularIndices(int patchResolution);
    static QVector<quint16> buildLodIndices(int patchResolution,
                                            int sourceStep,
                                            quint8 edgeMask);
    static quint32 packNormal(float x, float y, float z, bool gap = false);
    static TerrainPatchGpuParams terrainParams(const Terrain &terrain,
                                                int patchId);
    static TerrainPatchGpuParams mapParams(const Terrain &terrain,
                                            int patchId);

private:
    struct IndexTemplate {
        unsigned int byteOffset = 0;
        int indexCount = 0;
    };

    struct Page {
        explicit Page();
        QOpenGLBuffer vertexBuffer;
        QOpenGLBuffer terrainParamsBuffer;
        QOpenGLBuffer mapParamsBuffer;
        QOpenGLVertexArrayObject vertexArray;
        int firstPatch = 0;
        int patchCount = 0;
    };

    QVector<Page*> pages;
    QOpenGLBuffer indexBuffer;
    QHash<int, IndexTemplate> indexTemplates;
    quint64 indexBufferBytes = 0;
    QVector<unsigned int> dirtyReasons;
    bool initialized = false;
    bool needsEdgeFill = true;
    Shader *directShader = nullptr;
    unsigned int directParamsBuffer = 0;
    int directVerticesPerPatch = 0;
    int directPatchSide = 0;
    float directSampleSpacing = 0.0f;
    bool directApplyGaps = false;
    bool directMapPass = false;
    bool directStateValid = false;
    QOpenGLVertexArrayObject *directVertexArray = nullptr;

    Page *pageForPatch(int patchId) const;
    void buildPage(Page &page);
    void updatePatch(int patchId, unsigned int reasons);
    QVector<TerrainVertex8Derived> buildPatchVertices(int patchId) const;
    void calculateNormal(int sampleX, int sampleZ,
                         float &normalX, float &normalY, float &normalZ) const;
    void bindDrawState(const RenderItem &item);
    static int indexTemplateKey(int sourceStep, quint8 edgeMask);
};

static_assert(sizeof(TerrainVertex8Derived) == 8,
              "Stage 2 derived-coordinate terrain vertex must be exactly 8 bytes");
static_assert(sizeof(TerrainPatchGpuParams) == 32,
              "Terrain UBO record must be exactly two vec4 values");

#endif // TERRAINMESHBACKEND_H
