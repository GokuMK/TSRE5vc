#include <tsre/tests/MeshStoreTestSuite.h>

#include <QDebug>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <cstring>

#include <tsre/renderer/Mesh.h>

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

    context.doneCurrent();
    qInfo() << "[tests:mesh-store] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
