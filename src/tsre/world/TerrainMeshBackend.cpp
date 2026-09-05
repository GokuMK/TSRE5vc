/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/world/TerrainMeshBackend.h>
#include <tsre/world/TerrainBrushProfiler.h>
#include <tsre/world/TerrainNormals.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <QOpenGLContext>
#include <QDebug>
#include <QElapsedTimer>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>

#include <tsre/Game.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/ogl/Shader.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/world/TerrainLod.h>

TerrainMeshBackend::TerrainMeshBackend(Terrain &terrainValue)
    : terrain(terrainValue) {
}

TerrainMeshBackend::~TerrainMeshBackend() = default;

TerrainMeshLegacy::TerrainMeshLegacy(Terrain &terrainValue)
    : TerrainMeshBackend(terrainValue) {
}

bool TerrainMeshLegacy::isPaged() const {
    return false;
}

bool TerrainMeshLegacy::ensureInitialized() {
    if (terrain.isOgl)
        return true;
    if (!terrain.loaded || terrain.tfile == nullptr || terrain.terrainData == nullptr)
        return false;
    QElapsedTimer timer;
    timer.start();
    if (terrain.VBO == nullptr)
        terrain.VBO = new QOpenGLBuffer();
    if (terrain.VAO == nullptr)
        terrain.VAO = new QOpenGLVertexArrayObject();
    Game::terrainLib->fillRaw(&terrain, static_cast<int>(terrain.mojex),
                              static_cast<int>(terrain.mojez));
    terrain.initializePatchBounds();
    terrain.vertexInit();
    terrain.normalInit();
    terrain.oglInit();
    terrain.isOgl = true;
    const quint64 blobBytes = static_cast<quint64>(terrain.gridLayout.storedCellCount)
            * 6u * 9u * sizeof(float);
    qInfo() << "Legacy terrain mesh" << terrain.name
            << "GPU bytes" << terrain.gridLayout.terrainVboBytes + blobBytes
            << "(terrain" << terrain.gridLayout.terrainVboBytes
            << "map" << blobBytes << ")"
            << "build microseconds" << timer.nsecsElapsed() / 1000;
    return true;
}

void TerrainMeshLegacy::configureRenderItem(RenderItem &item, int patchId,
                                             bool, bool, int, quint8) {
    const int patches = terrain.gridLayout.patchesPerSide;
    const int row = patchId / patches;
    const int column = patchId % patches;
    item.VBO = terrain.VBO;
    item.VAO = terrain.VAO;
    item.vertOffset = (column * patches + row)
            * terrain.gridLayout.pagedIndicesPerPatch();
    item.vertCount = terrain.gridLayout.pagedIndicesPerPatch();
}

void TerrainMeshLegacy::drawPatch(int patchId, bool, bool, int, quint8) {
    const int patches = terrain.gridLayout.patchesPerSide;
    const int row = patchId / patches;
    const int column = patchId % patches;
    QOpenGLFunctions *functions = QOpenGLContext::currentContext()->functions();
    functions->glDrawArrays(GL_TRIANGLES,
                            (column * patches + row)
                            * terrain.gridLayout.pagedIndicesPerPatch(),
                            terrain.gridLayout.pagedIndicesPerPatch());
}

void TerrainMeshLegacy::endDirectRender() {
}

void TerrainMeshLegacy::invalidateAll(unsigned int) {
    terrain.isOgl = false;
}

void TerrainMeshLegacy::invalidatePatch(int, unsigned int) {
    invalidateAll();
}

void TerrainMeshLegacy::invalidateSamples(int, int, int, int, unsigned int) {
    invalidateAll();
}

void TerrainMeshLegacy::refreshModified() {
    TerrainBrushProfiler::add(TerrainBrushProfiler::LegacyRefresh);
}

TerrainMeshPaged::Page::Page()
    : vertexBuffer(QOpenGLBuffer::VertexBuffer),
      terrainParamsBuffer(QOpenGLBuffer::VertexBuffer),
      mapParamsBuffer(QOpenGLBuffer::VertexBuffer) {
}

TerrainMeshPaged::TerrainMeshPaged(Terrain &terrainValue)
    : TerrainMeshBackend(terrainValue),
      indexBuffer(QOpenGLBuffer::IndexBuffer) {
    dirtyReasons.fill(TerrainDirtyAll, terrain.gridLayout.patchRecordCount());
    uniformNormalGrid = TerrainNormals::uniformCoordinates(terrain.gridLayout.sampleCount,
                                                            terrain.gridLayout.sampleSpacing);
}

TerrainMeshPaged::~TerrainMeshPaged() {
    qDeleteAll(pages);
    pages.clear();
}

bool TerrainMeshPaged::isPaged() const {
    return true;
}

QVector<quint16> TerrainMeshPaged::buildRegularIndices(int resolution) {
    return buildLodIndices(resolution, 1, 0);
}

QVector<quint16> TerrainMeshPaged::buildLodIndices(
        int resolution, int sourceStep, quint8 edgeMask) {
    QVector<quint16> indices;
    if (resolution < 1 || resolution > 128 || sourceStep < 1
            || resolution % sourceStep != 0 || (sourceStep & (sourceStep - 1))
            || (edgeMask & 0xf0u))
        return indices;
    const int cells = resolution / sourceStep;
    if (edgeMask != 0 && (cells < 2 || (cells & 1)))
        return indices;
    indices.reserve(cells * cells * 6);
    const int side = resolution + 1;
    auto vertex = [&](int levelX, int levelZ) {
        if ((edgeMask & TerrainLod::LocalX0)
                && levelX == 0 && (levelZ & 1))
            --levelZ;
        if ((edgeMask & TerrainLod::LocalXMax)
                && levelX == cells && (levelZ & 1))
            --levelZ;
        if ((edgeMask & TerrainLod::LocalZ0)
                && levelZ == 0 && (levelX & 1))
            --levelX;
        if ((edgeMask & TerrainLod::LocalZMax)
                && levelZ == cells && (levelX & 1))
            --levelX;
        return static_cast<quint16>((levelZ * sourceStep) * side
                                    + levelX * sourceStep);
    };
    auto appendTriangle = [&](quint16 first, quint16 second, quint16 third) {
        if (first == second || first == third || second == third)
            return;
        indices << first << second << third;
    };
    for (int z = 0; z < cells; ++z) {
        for (int x = 0; x < cells; ++x) {
            const quint16 p00 = vertex(x, z);
            const quint16 p10 = vertex(x + 1, z);
            const quint16 p01 = vertex(x, z + 1);
            const quint16 p11 = vertex(x + 1, z + 1);
            if (((x + z) & 1) == 0) {
                appendTriangle(p00, p01, p11);
                appendTriangle(p00, p11, p10);
            } else {
                appendTriangle(p01, p11, p10);
                appendTriangle(p00, p01, p10);
            }
        }
    }
    return indices;
}

int TerrainMeshPaged::indexTemplateKey(int sourceStep, quint8 edgeMask) {
    return (sourceStep << 4) | (edgeMask & 0x0f);
}

quint32 TerrainMeshPaged::packNormal(float x, float y, float z, bool gap) {
    return TerrainNormals::packNormal(x, y, z, gap);
}

TerrainPatchGpuParams TerrainMeshPaged::terrainParams(const Terrain &terrain,
                                                       int patchId) {
    TerrainPatchGpuParams params{};
    if (!terrain.gridLayout.isPatchIndexValid(patchId) || terrain.tfile == nullptr)
        return params;
    const int column = terrain.gridLayout.patchColumn(patchId);
    const int row = terrain.gridLayout.patchRow(patchId);
    params.uvAndOriginX[0] = terrain.tfile->patchValue(
                patchId, TFile::PatchField::TextureW);
    params.uvAndOriginX[1] = terrain.tfile->patchValue(
                patchId, TFile::PatchField::TextureB);
    params.uvAndOriginX[2] = terrain.tfile->patchValue(
                patchId, TFile::PatchField::TextureX);
    params.uvAndOriginX[3] = column * terrain.gridLayout.patchWorldSize;
    params.uvAndOriginZ[0] = terrain.tfile->patchValue(
                patchId, TFile::PatchField::TextureC);
    params.uvAndOriginZ[1] = terrain.tfile->patchValue(
                patchId, TFile::PatchField::TextureH);
    params.uvAndOriginZ[2] = terrain.tfile->patchValue(
                patchId, TFile::PatchField::TextureY);
    params.uvAndOriginZ[3] = row * terrain.gridLayout.patchWorldSize;
    return params;
}

TerrainPatchGpuParams TerrainMeshPaged::mapParams(const Terrain &terrain,
                                                   int patchId) {
    TerrainPatchGpuParams params{};
    if (!terrain.gridLayout.isPatchIndexValid(patchId))
        return params;
    const int column = terrain.gridLayout.patchColumn(patchId);
    const int row = terrain.gridLayout.patchRow(patchId);
    const float step = 1.0f / terrain.gridLayout.sampleCount;
    params.uvAndOriginX[0] = step;
    params.uvAndOriginX[2] = column * terrain.gridLayout.patchResolution * step;
    params.uvAndOriginX[3] = column * terrain.gridLayout.patchWorldSize;
    params.uvAndOriginZ[1] = step;
    params.uvAndOriginZ[2] = row * terrain.gridLayout.patchResolution * step;
    params.uvAndOriginZ[3] = row * terrain.gridLayout.patchWorldSize;
    return params;
}

void TerrainMeshPaged::calculateNormal(int sampleX, int sampleZ,
                                       float &normalX, float &normalY,
                                       float &normalZ) const {
    const auto normal = TerrainNormals::calculate(terrain.terrainData, terrain.gridLayout.sampleCount,
                         terrain.gridLayout.sampleSpacing, sampleX, sampleZ, uniformNormalGrid);
    normalX = normal.x; normalY = normal.y; normalZ = normal.z;
}

QVector<TerrainVertex8Derived> TerrainMeshPaged::buildPatchVertices(int patchId) const {
    QVector<TerrainVertex8Derived> vertices;
    if (!terrain.gridLayout.isPatchIndexValid(patchId))
        return vertices;
    const int resolution = terrain.gridLayout.patchResolution;
    const int firstX = terrain.gridLayout.patchColumn(patchId) * resolution;
    const int firstZ = terrain.gridLayout.patchRow(patchId) * resolution;
    if (TerrainBrushProfiler::active()) {
        // Diagnostic-only split: time two whole-patch passes, never a timer per
        // vertex. Reuse the output array; no temporary normal array is needed.
        // The ordinary interleaved builder below stays unchanged.
        {
            TerrainBrushProfiler::Scope timing(TerrainBrushProfiler::Vertex);
            vertices.resize((resolution + 1) * (resolution + 1));
            int output = 0;
            for (int localZ = 0; localZ <= resolution; ++localZ)
                for (int localX = 0; localX <= resolution; ++localX)
                    vertices[output++].height = terrain.terrainData[firstZ + localZ][firstX + localX];
        }
        {
            TerrainBrushProfiler::Scope timing(TerrainBrushProfiler::Normals);
            int output = 0;
            for (int localZ = 0; localZ <= resolution; ++localZ) {
                for (int localX = 0; localX <= resolution; ++localX) {
                    const int sampleX = firstX + localX;
                    const int sampleZ = firstZ + localZ;
                    float nx = 0.0f, ny = 1.0f, nz = 0.0f;
                    calculateNormal(sampleX, sampleZ, nx, ny, nz);
                    const bool gap = terrain.jestF && (terrain.fData[sampleZ][sampleX] & 0x04);
                    vertices[output++].packedNormal = TerrainNormals::packNormal(nx, ny, nz, gap);
                }
            }
        }
        return vertices;
    }
    vertices.resize((resolution + 1) * (resolution + 1));
    int output = 0;
    for (int localZ = 0; localZ <= resolution; ++localZ) {
        for (int localX = 0; localX <= resolution; ++localX) {
            const int sampleX = firstX + localX;
            const int sampleZ = firstZ + localZ;
            float nx = 0.0f;
            float ny = 1.0f;
            float nz = 0.0f;
            calculateNormal(sampleX, sampleZ, nx, ny, nz);
            TerrainVertex8Derived &vertex = vertices[output++];
            vertex.height = terrain.terrainData[sampleZ][sampleX];
            const bool gap = terrain.jestF
                    && (terrain.fData[sampleZ][sampleX] & 0x04);
            vertex.packedNormal = TerrainNormals::packNormal(nx, ny, nz, gap);
        }
    }
    return vertices;
}

bool TerrainMeshPaged::ensureInitialized() {
    if (initialized) {
        refreshModified();
        return true;
    }
    if (!terrain.loaded || terrain.tfile == nullptr || terrain.terrainData == nullptr
            || QOpenGLContext::currentContext() == nullptr)
        return false;

    QElapsedTimer timer;
    timer.start();
    Game::terrainLib->fillRaw(&terrain, static_cast<int>(terrain.mojex),
                              static_cast<int>(terrain.mojez));
    terrain.initializePatchBounds();
    needsEdgeFill = false;
    QVector<quint16> allIndices;
    QVector<int> sourceSteps = TerrainLod::availableSourceSteps(
                terrain.gridLayout);
    if (sourceSteps.isEmpty())
        sourceSteps.append(1);
    indexTemplates.clear();
    for (int level = 0; level < sourceSteps.size(); ++level) {
        const int sourceStep = sourceSteps[level];
        // A neighbour can be coarser natively, including beyond this tile's
        // coarsest selectable LOD. Such borders still need transition masks.
        const int cells = terrain.gridLayout.patchResolution / sourceStep;
        const int lastMask = cells >= 2 && cells % 2 == 0 ? 15 : 0;
        for (int mask = 0; mask <= lastMask; ++mask) {
            const QVector<quint16> indices = buildLodIndices(
                        terrain.gridLayout.patchResolution,
                        sourceStep, static_cast<quint8>(mask));
            if (indices.isEmpty())
                return false;
            IndexTemplate entry;
            entry.byteOffset = static_cast<unsigned int>(
                        allIndices.size() * int(sizeof(quint16)));
            entry.indexCount = indices.size();
            indexTemplates.insert(indexTemplateKey(sourceStep,
                                                    static_cast<quint8>(mask)),
                                  entry);
            allIndices += indices;
        }
    }
    indexBufferBytes = static_cast<quint64>(allIndices.size())
            * sizeof(quint16);

    indexBuffer.create();
    indexBuffer.bind();
    indexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    indexBuffer.allocate(allIndices.constData(),
                         allIndices.size() * int(sizeof(quint16)));
    indexBuffer.release();

    for (int pageIndex = 0; pageIndex < terrain.gridLayout.pagedPageCount();
         ++pageIndex) {
        Page *page = new Page();
        page->firstPatch = pageIndex * PatchesPerPage;
        page->patchCount = std::min(PatchesPerPage,
                terrain.gridLayout.patchRecordCount() - page->firstPatch);
        buildPage(*page);
        pages.append(page);
    }
    initialized = true;
    terrain.isOgl = true;
    dirtyReasons.fill(TerrainDirtyNone, terrain.gridLayout.patchRecordCount());
    const quint64 vertexBytes = static_cast<quint64>(
                terrain.gridLayout.pagedPatchVertexBytes)
            * terrain.gridLayout.patchRecordCount();
    const quint64 parameterBytes = static_cast<quint64>(pages.size())
            * PatchesPerPage * sizeof(TerrainPatchGpuParams) * 2u;
    qInfo() << "Paged terrain mesh" << terrain.name
            << "patches" << terrain.gridLayout.patchRecordCount()
            << "pages" << pages.size()
            << "GPU bytes" << vertexBytes
                               + indexBufferBytes
                               + parameterBytes
            << "(vertices" << vertexBytes
            << "indices" << indexBufferBytes
            << "terrain+map params" << parameterBytes << ")"
            << "build microseconds" << timer.nsecsElapsed() / 1000;
    return true;
}

void TerrainMeshPaged::buildPage(Page &page) {
    page.vertexArray.create();
    page.vertexBuffer.create();
    page.terrainParamsBuffer.create();
    page.mapParamsBuffer.create();

    QOpenGLVertexArrayObject::Binder binder(&page.vertexArray);
    page.vertexBuffer.bind();
    page.vertexBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    page.vertexBuffer.allocate(page.patchCount
            * static_cast<int>(terrain.gridLayout.pagedPatchVertexBytes));
    indexBuffer.bind();

    QOpenGLFunctions *functions = QOpenGLContext::currentContext()->functions();
    functions->glEnableVertexAttribArray(0);
    functions->glEnableVertexAttribArray(2);
    functions->glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE,
                                     sizeof(TerrainVertex8Derived), nullptr);
    functions->glVertexAttribPointer(2, 4, GL_INT_2_10_10_10_REV, GL_TRUE,
                                     sizeof(TerrainVertex8Derived),
                                     reinterpret_cast<void*>(offsetof(TerrainVertex8Derived, packedNormal)));
    page.vertexBuffer.release();

    QVector<TerrainPatchGpuParams> terrainRecords(PatchesPerPage);
    QVector<TerrainPatchGpuParams> mapRecords(PatchesPerPage);
    for (int slot = 0; slot < page.patchCount; ++slot) {
        const int patchId = page.firstPatch + slot;
        const QVector<TerrainVertex8Derived> vertices = buildPatchVertices(patchId);
        page.vertexBuffer.bind();
        page.vertexBuffer.write(slot
                * static_cast<int>(terrain.gridLayout.pagedPatchVertexBytes),
                vertices.constData(), vertices.size() * int(sizeof(TerrainVertex8Derived)));
        page.vertexBuffer.release();
        terrainRecords[slot] = terrainParams(terrain, patchId);
        mapRecords[slot] = mapParams(terrain, patchId);
    }

    page.terrainParamsBuffer.bind();
    page.terrainParamsBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    page.terrainParamsBuffer.allocate(terrainRecords.constData(),
                                      terrainRecords.size()
                                      * int(sizeof(TerrainPatchGpuParams)));
    page.terrainParamsBuffer.release();
    page.mapParamsBuffer.bind();
    page.mapParamsBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    page.mapParamsBuffer.allocate(mapRecords.constData(),
                                  mapRecords.size()
                                  * int(sizeof(TerrainPatchGpuParams)));
    page.mapParamsBuffer.release();
}

TerrainMeshPaged::Page *TerrainMeshPaged::pageForPatch(int patchId) const {
    const int pageIndex = patchId / PatchesPerPage;
    return pageIndex >= 0 && pageIndex < pages.size() ? pages[pageIndex] : nullptr;
}

void TerrainMeshPaged::configureRenderItem(RenderItem &item, int patchId,
                                            bool mapPass, bool applyGaps,
                                            int sourceStep, quint8 edgeMask) {
    Page *page = pageForPatch(patchId);
    if (page == nullptr)
        return;
    const int slot = patchId - page->firstPatch;
    item.VBO = &page->vertexBuffer;
    item.VAO = &page->vertexArray;
    item.vertOffset = 0;
    auto indexTemplate = indexTemplates.constFind(
                indexTemplateKey(sourceStep, edgeMask));
    if (indexTemplate == indexTemplates.constEnd())
        indexTemplate = indexTemplates.constFind(indexTemplateKey(sourceStep, 0));
    if (indexTemplate == indexTemplates.constEnd())
        indexTemplate = indexTemplates.constFind(indexTemplateKey(1, 0));
    if (indexTemplate == indexTemplates.constEnd())
        return;
    item.vertCount = indexTemplate->indexCount;
    item.indexed = true;
    item.indexType = GL_UNSIGNED_SHORT;
    item.indexOffset = indexTemplate->byteOffset;
    item.baseVertex = slot * terrain.gridLayout.pagedVerticesPerPatch();
    item.terrainPaged = true;
    item.terrainParamsBuffer = mapPass
            ? &page->mapParamsBuffer : &page->terrainParamsBuffer;
    item.terrainVerticesPerPatch = terrain.gridLayout.pagedVerticesPerPatch();
    item.terrainPatchSide = terrain.gridLayout.patchResolution + 1;
    item.terrainSampleSpacing = terrain.gridLayout.sampleSpacing;
    item.terrainApplyGaps = applyGaps;
    item.terrainMapPass = mapPass;
}

void TerrainMeshPaged::bindDrawState(const RenderItem &item) {
    GLUU *gluu = GLUU::get();
    if (gluu == nullptr || gluu->currentShader == nullptr)
        return;
    Shader *shader = gluu->currentShader;
    const unsigned int paramsBuffer = item.terrainParamsBuffer == nullptr ? 0
            : item.terrainParamsBuffer->bufferId();
    const bool shaderChanged = !directStateValid || directShader != shader;
    if (shaderChanged)
        shader->setUniformValue(shader->terrainPaged, 1);
    if (shaderChanged || directVerticesPerPatch != item.terrainVerticesPerPatch)
        shader->setUniformValue(shader->terrainVerticesPerPatch,
                                item.terrainVerticesPerPatch);
    if (shaderChanged || directPatchSide != item.terrainPatchSide)
        shader->setUniformValue(shader->terrainPatchSide,
                                item.terrainPatchSide);
    if (shaderChanged || directSampleSpacing != item.terrainSampleSpacing)
        shader->setUniformValue(shader->terrainSampleSpacing,
                                item.terrainSampleSpacing);
    if (shaderChanged || directApplyGaps != item.terrainApplyGaps)
        shader->setUniformValue(shader->terrainApplyGaps,
                                item.terrainApplyGaps ? 1 : 0);
    if (shaderChanged || directMapPass != item.terrainMapPass)
        shader->setUniformValue(shader->terrainMapPass,
                                item.terrainMapPass ? 1 : 0);
    if (shaderChanged || directParamsBuffer != paramsBuffer)
        QOpenGLContext::currentContext()->extraFunctions()->glBindBufferBase(
                    GL_UNIFORM_BUFFER, 0, paramsBuffer);
    directShader = shader;
    directParamsBuffer = paramsBuffer;
    directVerticesPerPatch = item.terrainVerticesPerPatch;
    directPatchSide = item.terrainPatchSide;
    directSampleSpacing = item.terrainSampleSpacing;
    directApplyGaps = item.terrainApplyGaps;
    directMapPass = item.terrainMapPass;
    directStateValid = true;
}

void TerrainMeshPaged::drawPatch(int patchId, bool mapPass, bool applyGaps,
                                 int sourceStep, quint8 edgeMask) {
    RenderItem item;
    configureRenderItem(item, patchId, mapPass, applyGaps,
                        sourceStep, edgeMask);
    if (item.VAO == nullptr)
        return;
    bindDrawState(item);
    if (directVertexArray != item.VAO) {
        if (directVertexArray != nullptr)
            directVertexArray->release();
        item.VAO->bind();
        directVertexArray = item.VAO;
    }
    QOpenGLContext::currentContext()->extraFunctions()->glDrawElementsBaseVertex(
                GL_TRIANGLES, item.vertCount, item.indexType,
                reinterpret_cast<void*>(static_cast<quintptr>(item.indexOffset)),
                item.baseVertex);
}

void TerrainMeshPaged::endDirectRender() {
    if (directVertexArray != nullptr) {
        directVertexArray->release();
        directVertexArray = nullptr;
    }
    if (directStateValid && directShader != nullptr)
        directShader->setUniformValue(directShader->terrainPaged, 0);
    if (directStateValid && QOpenGLContext::currentContext() != nullptr)
        QOpenGLContext::currentContext()->extraFunctions()->glBindBufferBase(
                    GL_UNIFORM_BUFFER, 0, 0);
    directShader = nullptr;
    directParamsBuffer = 0;
    directStateValid = false;
}

void TerrainMeshPaged::invalidateAll(unsigned int reasons) {
    for (int patchId = 0; patchId < dirtyReasons.size(); ++patchId)
        dirtyReasons[patchId] |= reasons;
    if (reasons & (TerrainDirtyHeight | TerrainDirtyNormals))
        needsEdgeFill = true;
}

void TerrainMeshPaged::invalidatePatch(int patchId, unsigned int reasons) {
    if (patchId >= 0 && patchId < dirtyReasons.size())
        dirtyReasons[patchId] |= reasons;
}

void TerrainMeshPaged::invalidateSamples(int minX, int minZ, int maxX, int maxZ,
                                         unsigned int reasons) {
    if (dirtyReasons.isEmpty())
        return;
    if ((reasons & (TerrainDirtyHeight | TerrainDirtyNormals))
            && (maxX >= terrain.gridLayout.sampleCount
                || maxZ >= terrain.gridLayout.sampleCount))
        needsEdgeFill = true;
    if (reasons & (TerrainDirtyHeight | TerrainDirtyNormals)) {
        --minX;
        --minZ;
        ++maxX;
        ++maxZ;
        reasons |= TerrainDirtyNormals;
    }
    minX = std::max(0, minX);
    minZ = std::max(0, minZ);
    maxX = std::min(terrain.gridLayout.sampleCount, maxX);
    maxZ = std::min(terrain.gridLayout.sampleCount, maxZ);
    const int resolution = terrain.gridLayout.patchResolution;
    for (int patchId = 0; patchId < dirtyReasons.size(); ++patchId) {
        const int patchX = terrain.gridLayout.patchColumn(patchId) * resolution;
        const int patchZ = terrain.gridLayout.patchRow(patchId) * resolution;
        if (patchX <= maxX && patchX + resolution >= minX
                && patchZ <= maxZ && patchZ + resolution >= minZ)
            dirtyReasons[patchId] |= reasons;
    }
}

void TerrainMeshPaged::updatePatch(int patchId, unsigned int reasons) {
    Page *page = pageForPatch(patchId);
    if (page == nullptr)
        return;
    const int slot = patchId - page->firstPatch;
    if (reasons & (TerrainDirtyHeight | TerrainDirtyNormals | TerrainDirtyGaps)) {
        QVector<TerrainVertex8Derived> vertices;
        {
            TerrainBrushProfiler::Scope timing(TerrainBrushProfiler::Build);
            vertices = buildPatchVertices(patchId);
        }
        TerrainBrushProfiler::add(TerrainBrushProfiler::Patches);
        TerrainBrushProfiler::add(TerrainBrushProfiler::Vertices, vertices.size());
        TerrainBrushProfiler::add(TerrainBrushProfiler::UploadCalls);
        TerrainBrushProfiler::add(TerrainBrushProfiler::UploadBytes,
                                 vertices.size() * sizeof(TerrainVertex8Derived));
        TerrainBrushProfiler::Scope timing(TerrainBrushProfiler::Upload);
        page->vertexBuffer.bind();
        page->vertexBuffer.write(slot
                * static_cast<int>(terrain.gridLayout.pagedPatchVertexBytes),
                vertices.constData(), vertices.size() * int(sizeof(TerrainVertex8Derived)));
        page->vertexBuffer.release();
    }
    if (reasons & TerrainDirtyUvParams) {
        const TerrainPatchGpuParams params = terrainParams(terrain, patchId);
        TerrainBrushProfiler::add(TerrainBrushProfiler::UploadCalls);
        TerrainBrushProfiler::add(TerrainBrushProfiler::UploadBytes, sizeof(params));
        TerrainBrushProfiler::Scope timing(TerrainBrushProfiler::Upload);
        page->terrainParamsBuffer.bind();
        page->terrainParamsBuffer.write(slot * int(sizeof(TerrainPatchGpuParams)),
                                        &params, sizeof(params));
        page->terrainParamsBuffer.release();
    }
}

void TerrainMeshPaged::refreshModified() {
    if (!initialized || QOpenGLContext::currentContext() == nullptr) {
        TerrainBrushProfiler::add(TerrainBrushProfiler::Deferred);
        return;
    }
    const bool profilingWork = TerrainBrushProfiler::enabled()
            && std::any_of(dirtyReasons.cbegin(), dirtyReasons.cend(),
                           [](unsigned int reason) { return reason != TerrainDirtyNone; });
    TerrainBrushProfiler::Event profile("mesh", profilingWork
        ? QString("tile=%1 N=%2 P=%3").arg(terrain.name)
          .arg(terrain.gridLayout.sampleCount).arg(terrain.gridLayout.patchesPerSide)
        : QString(), profilingWork);
    TerrainBrushProfiler::Scope meshTiming(TerrainBrushProfiler::Mesh);
    if (needsEdgeFill) {
        TerrainBrushProfiler::Scope edgeTiming(TerrainBrushProfiler::Edges);
        Game::terrainLib->fillRaw(&terrain, static_cast<int>(terrain.mojex),
                                  static_cast<int>(terrain.mojez));
        for (int patchId = 0; patchId < dirtyReasons.size(); ++patchId) {
            if (dirtyReasons[patchId] & TerrainDirtyHeight)
                terrain.markPatchBoundsDirty(patchId);
        }
        {
            TerrainBrushProfiler::Scope boundsTiming(TerrainBrushProfiler::Bounds);
            terrain.refreshPatchBounds(true);
        }
        needsEdgeFill = false;
    }
    QElapsedTimer timer;
    timer.start();
    int updatedPatches = 0;
    quint64 uploadedBytes = 0;
    for (int patchId = 0; patchId < dirtyReasons.size(); ++patchId) {
        if (dirtyReasons[patchId] == TerrainDirtyNone)
            continue;
        if (dirtyReasons[patchId]
                & (TerrainDirtyHeight | TerrainDirtyNormals | TerrainDirtyGaps))
            uploadedBytes += terrain.gridLayout.pagedPatchVertexBytes;
        if (dirtyReasons[patchId] & TerrainDirtyUvParams)
            uploadedBytes += sizeof(TerrainPatchGpuParams);
        updatePatch(patchId, dirtyReasons[patchId]);
        dirtyReasons[patchId] = TerrainDirtyNone;
        ++updatedPatches;
    }
    if (updatedPatches > 0 && !TerrainBrushProfiler::active())
        qDebug() << "Paged terrain refresh" << terrain.name
                 << "patches" << updatedPatches
                 << "bytes" << uploadedBytes
                 << "microseconds" << timer.nsecsElapsed() / 1000;
}
