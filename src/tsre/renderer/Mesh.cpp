/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "Mesh.h"
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLFunctions>
#include <rhi/qrhi.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <memory>

#ifndef GL_COPY_WRITE_BUFFER
#define GL_COPY_WRITE_BUFFER 0x8F37
#endif
#ifndef GL_INT_2_10_10_10_REV
#define GL_INT_2_10_10_10_REV 0x8D9F
#endif

namespace {

struct Range {
    int offset = 0;
    QByteArray data;
};

struct Entry {
    // Odd while live; a released slot moves to the next even number, so
    // stale handles never match a reused slot.
    quint32 generation = 0;
    // Data waiting for upload; uploaded data is dropped from the CPU.
    std::unique_ptr<MeshData> pending;
    // Rewrites of uploaded data, applied before the next draw.
    std::vector<Range> ranges;
    GLuint vertexBuffer = 0;
    GLuint indexBuffer = 0;
    MeshHandle sharedIndices;
    MeshData::Format format = MeshData::FloatLayout;
    RenderItem::VertexAttr layout = RenderItem::NO_ATTR;
    int bytes = 0;
    quint64 stamp = 0;
    // QRhi renderer: the buffers, and the bytes of Buffer-format data, kept
    // because one storage can serve as an index buffer and a uniform buffer.
    QRhiBuffer *rhiVertex = nullptr;
    QRhiBuffer *rhiIndex = nullptr;
    QRhiBuffer *rhiUniform = nullptr;
    QByteArray retained;
};

struct Store {
    QMutex mutex;
    std::vector<Entry> entries;
    std::vector<quint32> freeSlots;
    std::vector<GLuint> deadBuffers;
    std::vector<QRhiBuffer *> deadRhiBuffers;
    quint64 nextStamp = 1;
    quint64 releases = 0;
};

Store &store() {
    static Store instance;
    return instance;
}

bool live(const Store &s, MeshHandle handle) {
    return handle.valid() && handle.index < s.entries.size()
            && s.entries[handle.index].generation == handle.generation;
}

// The vertex data of pending data, as bytes.
char *pendingBytes(MeshData &data, int &size) {
    if (data.format == MeshData::FloatLayout) {
        size = int(data.vertices.size() * sizeof(float));
        return reinterpret_cast<char *>(data.vertices.data());
    }
    size = data.bytes.size();
    return data.bytes.data();
}

bool prepareLocked(Store &s, MeshHandle handle, QOpenGLFunctions *f,
                   Meshes::Buffers &buffers, int depth) {
    if (!live(s, handle) || depth > 1)
        return false;
    Entry &entry = s.entries[handle.index];
    if (entry.pending) {
        MeshData &data = *entry.pending;
        const bool hadIndices = entry.indexBuffer != 0;
        const GLenum usage = data.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
        int size = 0;
        const char *vertexBytes = pendingBytes(data, size);
        if (entry.vertexBuffer == 0)
            f->glGenBuffers(1, &entry.vertexBuffer);
        // The copy target leaves the bound vertex array's bindings alone.
        f->glBindBuffer(GL_COPY_WRITE_BUFFER, entry.vertexBuffer);
        f->glBufferData(GL_COPY_WRITE_BUFFER, size, vertexBytes, usage);
        if (!data.indices.isEmpty()) {
            if (entry.indexBuffer == 0)
                f->glGenBuffers(1, &entry.indexBuffer);
            f->glBindBuffer(GL_COPY_WRITE_BUFFER, entry.indexBuffer);
            f->glBufferData(GL_COPY_WRITE_BUFFER, data.indices.size(),
                            data.indices.constData(), GL_STATIC_DRAW);
        } else if (entry.indexBuffer != 0) {
            s.deadBuffers.push_back(entry.indexBuffer);
            entry.indexBuffer = 0;
        }
        f->glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        if (entry.stamp == 0 || entry.layout != data.layout || entry.format != data.format
                || hadIndices != (entry.indexBuffer != 0)
                || !(entry.sharedIndices == data.sharedIndices))
            entry.stamp = s.nextStamp++;
        entry.format = data.format;
        entry.layout = data.layout;
        entry.sharedIndices = data.sharedIndices;
        entry.bytes = size;
        entry.pending.reset();
    }
    if (!entry.ranges.empty() && entry.vertexBuffer != 0) {
        f->glBindBuffer(GL_COPY_WRITE_BUFFER, entry.vertexBuffer);
        for (const Range &range : entry.ranges)
            f->glBufferSubData(GL_COPY_WRITE_BUFFER, range.offset, range.data.size(),
                               range.data.constData());
        f->glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        entry.ranges.clear();
    }
    if (entry.vertexBuffer == 0)
        return false;
    buffers.vertexBuffer = entry.vertexBuffer;
    buffers.indexBuffer = entry.indexBuffer;
    buffers.format = entry.format;
    buffers.layout = entry.layout;
    buffers.stamp = entry.stamp;
    if (entry.sharedIndices.valid()) {
        // Copy before the call: preparing another entry may grow the table.
        const MeshHandle shared = entry.sharedIndices;
        const quint64 stamp = entry.stamp;
        Meshes::Buffers indices;
        if (prepareLocked(s, shared, f, indices, depth + 1)) {
            buffers.indexBuffer = indices.vertexBuffer;
            buffers.stamp = stamp ^ (indices.stamp * 0x9E3779B97F4A7C15ull);
        } else {
            buffers.indexBuffer = 0;
        }
    }
    return true;
}

}

MeshHandle Meshes::create(MeshData data) {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    quint32 index;
    if (!s.freeSlots.empty()) {
        index = s.freeSlots.back();
        s.freeSlots.pop_back();
    } else {
        index = static_cast<quint32>(s.entries.size());
        s.entries.emplace_back();
    }
    Entry &entry = s.entries[index];
    entry.generation += 1;
    entry.pending = std::make_unique<MeshData>(std::move(data));
    return MeshHandle{index, entry.generation};
}

void Meshes::update(MeshHandle &handle, MeshData data) {
    Store &s = store();
    {
        QMutexLocker lock(&s.mutex);
        if (live(s, handle)) {
            Entry &entry = s.entries[handle.index];
            entry.pending = std::make_unique<MeshData>(std::move(data));
            entry.ranges.clear();
            return;
        }
    }
    handle = create(std::move(data));
}

bool Meshes::updateRange(MeshHandle handle, int byteOffset, const void *data, int bytes) {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    if (!live(s, handle) || byteOffset < 0 || bytes < 0)
        return false;
    Entry &entry = s.entries[handle.index];
    if (entry.pending) {
        int size = 0;
        char *target = pendingBytes(*entry.pending, size);
        if (byteOffset + bytes > size)
            return false;
        std::memcpy(target + byteOffset, data, size_t(bytes));
        return true;
    }
    if (byteOffset + bytes > entry.bytes)
        return false;
    // A newer write of the same range replaces the queued one.
    for (Range &range : entry.ranges)
        if (range.offset == byteOffset && range.data.size() == bytes) {
            std::memcpy(range.data.data(), data, size_t(bytes));
            return true;
        }
    entry.ranges.push_back(Range{byteOffset,
                                 QByteArray(static_cast<const char *>(data), bytes)});
    return true;
}

void Meshes::release(MeshHandle &handle) {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    if (live(s, handle)) {
        Entry &entry = s.entries[handle.index];
        entry.generation += 1;
        entry.pending.reset();
        entry.ranges.clear();
        if (entry.vertexBuffer != 0)
            s.deadBuffers.push_back(entry.vertexBuffer);
        if (entry.indexBuffer != 0)
            s.deadBuffers.push_back(entry.indexBuffer);
        entry.vertexBuffer = 0;
        entry.indexBuffer = 0;
        for (QRhiBuffer *buffer : {entry.rhiVertex, entry.rhiIndex, entry.rhiUniform})
            if (buffer != nullptr)
                s.deadRhiBuffers.push_back(buffer);
        entry.rhiVertex = entry.rhiIndex = entry.rhiUniform = nullptr;
        entry.retained.clear();
        entry.sharedIndices = MeshHandle();
        entry.format = MeshData::FloatLayout;
        entry.layout = RenderItem::NO_ATTR;
        entry.bytes = 0;
        s.freeSlots.push_back(handle.index);
        s.releases++;
    }
    handle = MeshHandle();
}

bool Meshes::prepare(MeshHandle handle, QOpenGLFunctions *f, Buffers &buffers) {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    return prepareLocked(s, handle, f, buffers, 0);
}

bool Meshes::alive(MeshHandle handle) {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    return live(s, handle);
}

void Meshes::collectGarbage(QOpenGLFunctions *f) {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    if (s.deadBuffers.empty())
        return;
    f->glDeleteBuffers(static_cast<GLsizei>(s.deadBuffers.size()), s.deadBuffers.data());
    s.deadBuffers.clear();
}

quint64 Meshes::releaseCount() {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    return s.releases;
}

void Meshes::setupAttributes(QOpenGLFunctions *f, const Buffers &buffers) {
    // Locations: 0 position (or terrain height), 1 texture coordinates,
    // 2 normal, 3 alpha, 4 tangent, 5 second texture coordinates, 6 colour.
    if (buffers.format == MeshData::TerrainHeightNormal) {
        const GLsizei stride = 8;
        f->glEnableVertexAttribArray(0);
        f->glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, stride, nullptr);
        f->glEnableVertexAttribArray(2);
        f->glVertexAttribPointer(2, 4, GL_INT_2_10_10_10_REV, GL_TRUE, stride,
                                 reinterpret_cast<void *>(4));
        return;
    }
    if (buffers.format != MeshData::FloatLayout)
        return;
    const RenderItem::VertexAttr layout = buffers.layout;
    const GLsizei stride = static_cast<GLsizei>(layout) * sizeof(GLfloat);
    auto attribute = [&](GLuint location, GLint size, int offset) {
        f->glEnableVertexAttribArray(location);
        f->glVertexAttribPointer(location, size, GL_FLOAT, GL_FALSE, stride,
                                 reinterpret_cast<void *>(offset * sizeof(GLfloat)));
    };
    switch (layout) {
    case RenderItem::V:
        attribute(0, 3, 0);
        break;
    case RenderItem::VT:
        attribute(0, 3, 0);
        attribute(1, 2, 3);
        attribute(3, 1, 5);
        break;
    case RenderItem::VNT:
        attribute(0, 3, 0);
        attribute(2, 3, 3);
        attribute(1, 2, 6);
        break;
    case RenderItem::VNTA:
        attribute(0, 3, 0);
        attribute(2, 3, 3);
        attribute(1, 2, 6);
        attribute(3, 1, 8);
        break;
    case RenderItem::PBR:
        attribute(0, 3, 0);
        attribute(2, 3, 3);
        attribute(1, 2, 6);
        attribute(3, 1, 8);
        attribute(4, 4, 9);
        attribute(5, 2, 13);
        attribute(6, 4, 15);
        break;
    default:
        break;
    }
}

namespace {

// QRhi has no packed 2_10_10_10 vertex format: paged terrain vertices (a
// float height and a packed normal and gap flag) carry the normal as four
// unsigned bytes instead, round(v * 127) + 128, in the same 8 bytes; zero
// stays exactly zero, so the gap flag keeps its sign. Converts the packed
// words inside a byte range of such data in place.
void convertTerrainNormals(char *data, int offset, int size) {
    for (int p = (offset + 3) / 4 * 4; p + 4 <= offset + size; p += 4) {
        if (p % 8 != 4)
            continue;
        quint32 packed;
        std::memcpy(&packed, data + p - offset, 4);
        auto signedField = [packed](int shift, int bits) {
            const int raw = int((packed >> shift) & ((1u << bits) - 1));
            const int value = raw >= (1 << (bits - 1)) ? raw - (1 << bits) : raw;
            const float max = float((1 << (bits - 1)) - 1);
            return std::max(float(value) / max, -1.0f);
        };
        const float values[4] = {signedField(0, 10), signedField(10, 10), signedField(20, 10),
                                 signedField(30, 2)};
        unsigned char bytes[4];
        for (int c = 0; c < 4; ++c)
            bytes[c] = static_cast<unsigned char>(std::lround(values[c] * 127.0f) + 128);
        std::memcpy(data + p - offset, bytes, 4);
    }
}

// A static buffer of the size, reusing one that fits exactly.
QRhiBuffer *ensureBuffer(Store &s, QRhiBuffer *current, QRhi *rhi, QRhiBuffer::UsageFlags usage,
                         int size) {
    if (current != nullptr && current->size() == quint32(size))
        return current;
    if (current != nullptr)
        s.deadRhiBuffers.push_back(current);
    QRhiBuffer *buffer = rhi->newBuffer(QRhiBuffer::Static, usage, quint32(std::max(size, 4)));
    if (!buffer->create()) {
        delete buffer;
        return nullptr;
    }
    return buffer;
}

bool prepareRhiLocked(Store &s, MeshHandle handle, QRhi *rhi, QRhiResourceUpdateBatch *batch,
                      Meshes::RhiBuffers &buffers, int depth) {
    if (!live(s, handle) || depth > 1)
        return false;
    Entry &entry = s.entries[handle.index];
    if (entry.pending) {
        MeshData &data = *entry.pending;
        int size = 0;
        const char *vertexBytes = pendingBytes(data, size);
        if (data.format == MeshData::Buffer) {
            entry.retained = QByteArray(vertexBytes, size);
            // Buffers of other uses are made again from the new bytes.
            for (QRhiBuffer *buffer : {entry.rhiVertex, entry.rhiUniform})
                if (buffer != nullptr)
                    s.deadRhiBuffers.push_back(buffer);
            entry.rhiVertex = entry.rhiUniform = nullptr;
            entry.rhiVertex = ensureBuffer(s, nullptr, rhi, QRhiBuffer::IndexBuffer, size);
        } else {
            entry.rhiVertex = ensureBuffer(s, entry.rhiVertex, rhi, QRhiBuffer::VertexBuffer, size);
        }
        QByteArray converted;
        if (data.format == MeshData::TerrainHeightNormal) {
            converted = QByteArray(vertexBytes, size);
            convertTerrainNormals(converted.data(), 0, size);
            vertexBytes = converted.constData();
        }
        if (entry.rhiVertex != nullptr && size > 0)
            batch->uploadStaticBuffer(entry.rhiVertex, 0, quint32(size), vertexBytes);
        if (!data.indices.isEmpty()) {
            entry.rhiIndex = ensureBuffer(s, entry.rhiIndex, rhi, QRhiBuffer::IndexBuffer,
                                          int(data.indices.size()));
            if (entry.rhiIndex != nullptr)
                batch->uploadStaticBuffer(entry.rhiIndex, 0, quint32(data.indices.size()),
                                          data.indices.constData());
        } else if (entry.rhiIndex != nullptr) {
            s.deadRhiBuffers.push_back(entry.rhiIndex);
            entry.rhiIndex = nullptr;
        }
        entry.format = data.format;
        entry.layout = data.layout;
        entry.sharedIndices = data.sharedIndices;
        entry.bytes = size;
        entry.pending.reset();
    }
    if (!entry.ranges.empty() && entry.rhiVertex != nullptr) {
        for (const Range &range : entry.ranges) {
            QByteArray bytes = range.data;
            if (entry.format == MeshData::TerrainHeightNormal)
                convertTerrainNormals(bytes.data(), range.offset, int(bytes.size()));
            batch->uploadStaticBuffer(entry.rhiVertex, quint32(range.offset),
                                      quint32(bytes.size()), bytes.constData());
            if (!entry.retained.isEmpty()) {
                std::memcpy(entry.retained.data() + range.offset, range.data.constData(),
                            size_t(range.data.size()));
                if (entry.rhiUniform != nullptr)
                    batch->uploadStaticBuffer(entry.rhiUniform, quint32(range.offset),
                                              quint32(range.data.size()), range.data.constData());
            }
        }
        entry.ranges.clear();
    }
    if (entry.rhiVertex == nullptr)
        return false;
    buffers.vertexBuffer = entry.rhiVertex;
    buffers.indexBuffer = entry.rhiIndex;
    buffers.format = entry.format;
    buffers.layout = entry.layout;
    if (entry.sharedIndices.valid()) {
        const MeshHandle shared = entry.sharedIndices;
        Meshes::RhiBuffers indices;
        buffers.indexBuffer = prepareRhiLocked(s, shared, rhi, batch, indices, depth + 1)
                ? indices.vertexBuffer : nullptr;
    }
    return true;
}

}

bool Meshes::prepareRhi(MeshHandle handle, QRhi *rhi, QRhiResourceUpdateBatch *batch,
                        RhiBuffers &buffers) {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    return prepareRhiLocked(s, handle, rhi, batch, buffers, 0);
}

QRhiBuffer *Meshes::uniformBufferRhi(MeshHandle handle, QRhi *rhi, QRhiResourceUpdateBatch *batch) {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    RhiBuffers buffers;
    if (!prepareRhiLocked(s, handle, rhi, batch, buffers, 0))
        return nullptr;
    Entry &entry = s.entries[handle.index];
    if (entry.format != MeshData::Buffer)
        return nullptr;
    if (entry.rhiUniform == nullptr) {
        entry.rhiUniform = ensureBuffer(s, nullptr, rhi, QRhiBuffer::UniformBuffer,
                                        int(entry.retained.size()));
        if (entry.rhiUniform != nullptr && !entry.retained.isEmpty())
            batch->uploadStaticBuffer(entry.rhiUniform, 0, quint32(entry.retained.size()),
                                      entry.retained.constData());
    }
    return entry.rhiUniform;
}

void Meshes::collectGarbageRhi() {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    for (QRhiBuffer *buffer : s.deadRhiBuffers)
        buffer->deleteLater();
    s.deadRhiBuffers.clear();
}

void Meshes::releaseAllRhi() {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    for (Entry &entry : s.entries) {
        for (QRhiBuffer *buffer : {entry.rhiVertex, entry.rhiIndex, entry.rhiUniform})
            delete buffer;
        entry.rhiVertex = entry.rhiIndex = entry.rhiUniform = nullptr;
    }
    for (QRhiBuffer *buffer : s.deadRhiBuffers)
        delete buffer;
    s.deadRhiBuffers.clear();
}
