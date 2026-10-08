#include <tsre/tests/MeshStoreTestSuite.h>

#include <QDebug>
#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <cstring>
#include <functional>

#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/rhi/RhiContext.h>
#include <rhi/qrhi.h>

namespace {

MeshData vertexData(RenderItem::VertexAttr layout, int vertexCount, float base) {
    MeshData data;
    data.layout = layout;
    data.vertices.resize(static_cast<size_t>(vertexCount) * layout);
    for (size_t i = 0; i < data.vertices.size(); ++i)
        data.vertices[i] = base + static_cast<float>(i);
    return data;
}

// Reads a buffer back through a mapping on the copy target.
std::vector<float> readBack(QOpenGLExtraFunctions *e, unsigned int buffer, size_t count) {
    std::vector<float> values(count);
    e->glBindBuffer(GL_COPY_READ_BUFFER, buffer);
    const void *mapped = e->glMapBufferRange(GL_COPY_READ_BUFFER, 0,
                                             count * sizeof(float), GL_MAP_READ_BIT);
    if (mapped != nullptr) {
        std::memcpy(values.data(), mapped, count * sizeof(float));
        e->glUnmapBuffer(GL_COPY_READ_BUFFER);
    }
    e->glBindBuffer(GL_COPY_READ_BUFFER, 0);
    return values;
}

}

int TsreTests::runMeshStoreSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    auto check = [&](bool condition, const char *name) {
        if (condition) {
            ++passed;
            if (verbose)
                qInfo() << "[tests:mesh-store] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:mesh-store] FAIL" << name;
        }
    };

    // Handles, without a GL context: producers create meshes on any thread.
    MeshHandle first = Meshes::create(vertexData(RenderItem::V, 3, 0.0f));
    check(first.valid() && Meshes::alive(first), "create returns a live handle");
    const MeshHandle stale = first;
    const quint64 releasesBefore = Meshes::releaseCount();
    Meshes::release(first);
    check(!first.valid() && !Meshes::alive(stale)
          && Meshes::releaseCount() == releasesBefore + 1,
          "release resets the handle and counts the release");
    MeshHandle reused = Meshes::create(vertexData(RenderItem::V, 3, 0.0f));
    check(reused.index == stale.index && reused.generation != stale.generation
          && Meshes::alive(reused) && !Meshes::alive(stale),
          "a reused slot never matches a stale handle");
    MeshHandle fromStale = stale;
    Meshes::update(fromStale, vertexData(RenderItem::V, 3, 0.0f));
    check(fromStale.valid() && !(fromStale == stale) && Meshes::alive(fromStale),
          "update of a stale handle creates a new mesh");
    Meshes::release(fromStale);
    Meshes::release(reused);

    QOpenGLContext context;
    if (!context.create()) {
        qWarning() << "[tests:mesh-store] no OpenGL context";
        return 2;
    }
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!context.makeCurrent(&surface)) {
        qWarning() << "[tests:mesh-store] cannot make the context current";
        return 2;
    }
    QOpenGLFunctions *f = context.functions();
    QOpenGLExtraFunctions *e = context.extraFunctions();

    MeshHandle mesh = Meshes::create(vertexData(RenderItem::VT, 4, 10.0f));
    Meshes::Buffers buffers;
    check(Meshes::prepare(mesh, f, buffers) && buffers.vertexBuffer != 0
          && buffers.indexBuffer == 0 && buffers.layout == RenderItem::VT
          && f->glIsBuffer(buffers.vertexBuffer),
          "prepare uploads a vertex buffer");
    const std::vector<float> uploaded = readBack(e, buffers.vertexBuffer, 24);
    check(uploaded.size() == 24 && uploaded.front() == 10.0f && uploaded.back() == 33.0f,
          "the buffer holds the vertices");

    const Meshes::Buffers firstUpload = buffers;
    Meshes::update(mesh, vertexData(RenderItem::VT, 4, 50.0f));
    check(Meshes::prepare(mesh, f, buffers)
          && buffers.vertexBuffer == firstUpload.vertexBuffer
          && buffers.stamp == firstUpload.stamp
          && readBack(e, buffers.vertexBuffer, 1).front() == 50.0f,
          "an update with the same layout refills the buffer and keeps vertex arrays valid");

    Meshes::update(mesh, vertexData(RenderItem::VNTA, 2, 0.0f));
    check(Meshes::prepare(mesh, f, buffers) && buffers.layout == RenderItem::VNTA
          && buffers.stamp != firstUpload.stamp,
          "a layout change invalidates vertex arrays");

    const Meshes::Buffers beforeIndices = buffers;
    MeshData indexed = vertexData(RenderItem::VNTA, 2, 0.0f);
    const quint16 indices[] = {0, 1, 1};
    indexed.indices = QByteArray(reinterpret_cast<const char *>(indices), sizeof(indices));
    Meshes::update(mesh, std::move(indexed));
    check(Meshes::prepare(mesh, f, buffers) && buffers.indexBuffer != 0
          && f->glIsBuffer(buffers.indexBuffer) && buffers.stamp != beforeIndices.stamp,
          "indices add an index buffer and invalidate vertex arrays");

    GLint boundArray = -1;
    f->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundArray);
    check(boundArray == 0, "uploads leave the vertex array binding alone");

    const unsigned int vertexBuffer = buffers.vertexBuffer;
    const unsigned int indexBuffer = buffers.indexBuffer;
    const MeshHandle released = mesh;
    Meshes::release(mesh);
    check(!Meshes::prepare(released, f, buffers), "a released mesh has nothing to draw");
    Meshes::collectGarbage(f);
    check(!f->glIsBuffer(vertexBuffer) && !f->glIsBuffer(indexBuffer),
          "garbage collection deletes released buffers");

    MeshHandle empty = Meshes::create(MeshData());
    check(Meshes::prepare(empty, f, buffers) && buffers.layout == RenderItem::NO_ATTR,
          "an empty mesh uploads without error");
    Meshes::release(empty);
    Meshes::collectGarbage(f);
    check(f->glGetError() == GL_NO_ERROR, "no GL errors");

    // Range updates: before the upload they patch the pending data, after it
    // they are uploaded before the next draw.
    MeshHandle ranged = Meshes::create(vertexData(RenderItem::V, 4, 0.0f));
    const float pendingPatch[3] = {100.0f, 101.0f, 102.0f};
    check(Meshes::updateRange(ranged, 3 * sizeof(float), pendingPatch, sizeof(pendingPatch))
          && !Meshes::updateRange(ranged, 11 * sizeof(float), pendingPatch, sizeof(pendingPatch)),
          "range updates of pending data stay inside it");
    check(Meshes::prepare(ranged, f, buffers)
          && readBack(e, buffers.vertexBuffer, 12)[3] == 100.0f,
          "a pending range update is part of the first upload");
    const float uploadedPatch[2] = {200.0f, 201.0f};
    check(Meshes::updateRange(ranged, 0, uploadedPatch, sizeof(uploadedPatch))
          && Meshes::updateRange(ranged, 0, uploadedPatch, sizeof(uploadedPatch))
          && !Meshes::updateRange(ranged, 46, uploadedPatch, sizeof(uploadedPatch)),
          "range updates of uploaded data are checked against its size");
    const quint64 rangedStamp = buffers.stamp;
    const std::vector<float> patched = Meshes::prepare(ranged, f, buffers)
            ? readBack(e, buffers.vertexBuffer, 12) : std::vector<float>();
    check(patched.size() == 12 && patched[0] == 200.0f && patched[1] == 201.0f
          && patched[2] == 2.0f && patched[3] == 100.0f && buffers.stamp == rangedStamp,
          "uploaded range updates rewrite only their bytes and keep vertex arrays valid");

    // Plain storage shared as another mesh's index buffer.
    MeshData indexData;
    indexData.format = MeshData::Buffer;
    const quint16 sharedIndices[] = {0, 1, 2, 2, 1, 3};
    indexData.bytes = QByteArray(reinterpret_cast<const char *>(sharedIndices), sizeof(sharedIndices));
    MeshHandle indexStorage = Meshes::create(std::move(indexData));
    MeshData packed;
    packed.format = MeshData::TerrainHeightNormal;
    packed.layout = RenderItem::VNT;
    packed.bytes = QByteArray(4 * 8, 0);
    packed.sharedIndices = indexStorage;
    MeshHandle terrainPage = Meshes::create(std::move(packed));
    Meshes::Buffers storage;
    check(Meshes::prepare(indexStorage, f, storage) && storage.format == MeshData::Buffer
          && storage.indexBuffer == 0,
          "plain storage uploads without attributes or indices");
    check(Meshes::prepare(terrainPage, f, buffers)
          && buffers.format == MeshData::TerrainHeightNormal
          && buffers.indexBuffer == storage.vertexBuffer,
          "a mesh draws with shared index storage");
    const quint64 sharedStamp = buffers.stamp;
    MeshData replacement;
    replacement.format = MeshData::Buffer;
    replacement.bytes = QByteArray(sizeof(sharedIndices), 0);
    Meshes::release(indexStorage);
    indexStorage = Meshes::create(std::move(replacement));
    check(Meshes::prepare(terrainPage, f, buffers) && buffers.indexBuffer == 0,
          "a released shared index buffer is no longer bound");
    check(buffers.stamp != sharedStamp,
          "losing shared indices invalidates vertex arrays");

    GLuint array = 0;
    e->glGenVertexArrays(1, &array);
    e->glBindVertexArray(array);
    f->glBindBuffer(GL_ARRAY_BUFFER, buffers.vertexBuffer);
    Meshes::setupAttributes(f, buffers);
    GLint enabled[4] = {};
    GLint normalType = 0;
    for (GLuint location = 0; location < 4; ++location)
        e->glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled[location]);
    e->glGetVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_TYPE, &normalType);
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    e->glBindVertexArray(0);
    e->glDeleteVertexArrays(1, &array);
    check(enabled[0] && !enabled[1] && enabled[2] && !enabled[3]
          && normalType == GL_INT_2_10_10_10_REV,
          "the terrain format reads a height and a packed normal");

    Meshes::release(ranged);
    Meshes::release(terrainPage);
    Meshes::release(indexStorage);
    Meshes::collectGarbage(f);
    check(f->glGetError() == GL_NO_ERROR, "no GL errors after range and shared buffers");

    context.doneCurrent();
    qInfo() << "[tests:mesh-store] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}

int TsreTests::runMeshUploadRhiBenchmark(bool verbose) {
    Q_UNUSED(verbose);
    RhiContext *context = RhiContext::instance();
    QRhi *rhi = context != nullptr ? context->rhi() : nullptr;
    if (rhi == nullptr) {
        qWarning() << "[tests:mesh-upload-rhi-benchmark] no QRhi";
        return 1;
    }
    // Times frames that upload a mesh's changes and submit them.
    auto frames = [rhi](int count, const std::function<void(int)> &change, MeshHandle mesh) {
        QElapsedTimer timer;
        timer.start();
        for (int frame = 0; frame < count; ++frame) {
            change(frame);
            QRhiCommandBuffer *cb = nullptr;
            if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
                return -1.0;
            QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
            Meshes::RhiBuffers buffers;
            Meshes::prepareRhi(mesh, rhi, batch, buffers);
            cb->resourceUpdate(batch);
            rhi->endOffscreenFrame();
            Meshes::collectGarbageRhi();
        }
        return timer.nsecsElapsed() / 1e6 / count;
    };
    // A paged terrain page: 256 patches of 17 x 17 vertices of 8 bytes; a
    // brush rewrites 50 patches a frame.
    constexpr int PatchBytes = 17 * 17 * 8;
    constexpr int Patches = 256;
    MeshData page;
    page.format = MeshData::TerrainHeightNormal;
    page.dynamic = true;
    page.bytes = QByteArray(PatchBytes * Patches, char(0));
    MeshHandle paged = Meshes::create(std::move(page));
    const QByteArray patch(PatchBytes, char(1));
    const double pagedMs = frames(200, [&](int frame) {
        for (int i = 0; i < 50; ++i)
            Meshes::updateRange(paged, ((frame * 50 + i) * 7 % Patches) * PatchBytes,
                                patch.constData(), PatchBytes);
    }, paged);
    // A legacy terrain tile: 256 patches of 16 x 16 x 6 vertices of 5
    // floats, replaced whole on every brush step.
    MeshHandle tile;
    auto tileData = [](float value) {
        MeshData data;
        data.layout = RenderItem::VT;
        data.vertices.assign(size_t(256) * 16 * 16 * 6 * 5, value);
        return data;
    };
    Meshes::update(tile, tileData(0.0f));
    const double legacyMs = frames(50, [&](int frame) {
        Meshes::update(tile, tileData(float(frame)));
    }, tile);
    qInfo().noquote() << "[tests:mesh-upload-rhi-benchmark]" << rhi->backendName()
                      << "paged page, 50 patches a frame:" << pagedMs << "ms a frame;"
                      << "legacy tile replaced:" << legacyMs << "ms a frame";
    const QRhiStats stats = rhi->statistics();
    qInfo().noquote() << "[tests:mesh-upload-rhi-benchmark] memory used MB"
                      << stats.usedBytes / 1048576 << "allocations" << stats.allocCount;
    Meshes::release(paged);
    Meshes::release(tile);
    Meshes::collectGarbageRhi();
    return pagedMs < 0.0 || legacyMs < 0.0 ? 1 : 0;
}
