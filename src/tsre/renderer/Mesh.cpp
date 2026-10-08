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
#include <map>
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

// QRhi renderer: meshes share large buffers (chunks) instead of having a
// buffer each. On Vulkan every buffer is an allocation; a camera jump made
// hundreds of them in one frame, about 0.4 ms each on the Steam Deck. Chunks
// are Static: QRhi keeps a host copy of each for its uploads (one per frame
// in flight), a fixed cost per chunk. Immutable ones drop that copy after
// every upload, so each mesh put into one made a staging buffer as large as
// the chunk, and a jump of hundreds of meshes ran out of memory. Edited
// meshes, paged terrain (updated in place while painted), Buffer-format data
// and meshes larger than a quarter of a chunk keep buffers of their own.
constexpr quint32 ChunkBytes = 8 * 1024 * 1024;
constexpr quint32 LargestSlice = ChunkBytes / 4;
constexpr quint32 SliceAlignment = 256;
// Frames a released slice waits before reuse: the GPU may still read it.
constexpr int SliceRetireFrames = 3;

struct Slice {
    QRhiBuffer *buffer = nullptr; // the chunk; null: not shared
    quint32 offset = 0;
    quint32 size = 0;
};

struct Chunk {
    QRhiBuffer *buffer = nullptr;
    // Free ranges: offset to size.
    std::map<quint32, quint32> free;
    quint32 used = 0;
};

struct Pool {
    QRhiBuffer::UsageFlag usage;
    std::vector<Chunk> chunks;
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
    // because one storage can serve as an index buffer and a data texture.
    QRhiBuffer *rhiVertex = nullptr;
    QRhiBuffer *rhiIndex = nullptr;
    QRhiTexture *rhiData = nullptr;
    // Where rhiVertex and rhiIndex are shared chunks, the parts in use.
    Slice rhiVertexSlice;
    Slice rhiIndexSlice;
    // Uploads of whole data to the QRhi buffers so far.
    int rhiUploads = 0;
    QByteArray retained;
};

struct Store {
    QMutex mutex;
    std::vector<Entry> entries;
    std::vector<quint32> freeSlots;
    std::vector<GLuint> deadBuffers;
    std::vector<QRhiResource *> deadRhiBuffers;
    Pool rhiVertexPool{QRhiBuffer::VertexBuffer, {}};
    Pool rhiIndexPool{QRhiBuffer::IndexBuffer, {}};
    struct DeadSlice {
        Slice slice;
        Pool *pool = nullptr;
        int frames = 0;
    };
    std::vector<DeadSlice> deadSlices;
    quint64 nextStamp = 1;
    quint64 releases = 0;
    // QRhi uploads since the last trace: new meshes, meshes uploaded again
    // into their buffers, range updates, and their bytes.
    int rhiNewUploads = 0;
    int rhiAgainUploads = 0;
    int rhiRangeUploads = 0;
    qint64 rhiUploadBytes = 0;
};

Store &store() {
    static Store instance;
    return instance;
}

// A part of a chunk of the pool: the first free range that fits, or a new
// chunk when none does.
bool allocateSlice(Pool &pool, QRhi *rhi, quint32 size, Slice &slice) {
    const quint32 need = std::max<quint32>(SliceAlignment,
                                           (size + SliceAlignment - 1) / SliceAlignment * SliceAlignment);
    for (Chunk &chunk : pool.chunks)
        for (auto range = chunk.free.begin(); range != chunk.free.end(); ++range)
            if (range->second >= need) {
                slice = Slice{chunk.buffer, range->first, need};
                const quint32 rest = range->second - need, after = range->first + need;
                chunk.free.erase(range);
                if (rest > 0)
                    chunk.free[after] = rest;
                chunk.used += need;
                return true;
            }
    QRhiBuffer *buffer = rhi->newBuffer(QRhiBuffer::Static, pool.usage, ChunkBytes);
    if (!buffer->create()) {
        delete buffer;
        return false;
    }
    Chunk chunk;
    chunk.buffer = buffer;
    chunk.free[need] = ChunkBytes - need;
    chunk.used = need;
    pool.chunks.push_back(std::move(chunk));
    slice = Slice{buffer, 0, need};
    return true;
}

// Returns a slice to its chunk, merged with free neighbours; an empty chunk
// is released while the pool has another.
void freeSlice(Pool &pool, const Slice &slice) {
    for (auto chunk = pool.chunks.begin(); chunk != pool.chunks.end(); ++chunk) {
        if (chunk->buffer != slice.buffer)
            continue;
        quint32 offset = slice.offset, size = slice.size;
        auto next = chunk->free.lower_bound(offset);
        if (next != chunk->free.end() && offset + size == next->first) {
            size += next->second;
            next = chunk->free.erase(next);
        }
        if (next != chunk->free.begin()) {
            auto previous = std::prev(next);
            if (previous->first + previous->second == offset) {
                offset = previous->first;
                size += previous->second;
                chunk->free.erase(previous);
            }
        }
        chunk->free[offset] = size;
        chunk->used -= slice.size;
        if (chunk->used == 0 && pool.chunks.size() > 1) {
            chunk->buffer->deleteLater();
            pool.chunks.erase(chunk);
        }
        return;
    }
}

// Lets go of a mesh's vertex or index storage: a shared slice after a few
// frames, a buffer of its own with the other dead resources.
void dropStorage(Store &s, QRhiBuffer *&buffer, Slice &slice, Pool &pool) {
    if (slice.buffer != nullptr)
        s.deadSlices.push_back({slice, &pool, SliceRetireFrames});
    else if (buffer != nullptr)
        s.deadRhiBuffers.push_back(buffer);
    buffer = nullptr;
    slice = Slice();
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
        dropStorage(s, entry.rhiVertex, entry.rhiVertexSlice, s.rhiVertexPool);
        dropStorage(s, entry.rhiIndex, entry.rhiIndexSlice, s.rhiIndexPool);
        if (entry.rhiData != nullptr)
            s.deadRhiBuffers.push_back(entry.rhiData);
        entry.rhiData = nullptr;
        entry.rhiUploads = 0;
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

// Buffer-format data as a texture: RGBA32F texels in one row.
constexpr int DataTexelBytes = 16;

// Uploads the texels of an entry's data texture covering a byte range of
// its retained data.
void uploadDataTexels(const Entry &entry, QRhiResourceUpdateBatch *batch, int offset, int size) {
    const int width = entry.rhiData->pixelSize().width();
    const int first = std::max(0, offset / DataTexelBytes);
    const int end = std::min(width, (offset + size + DataTexelBytes - 1) / DataTexelBytes);
    if (end <= first)
        return;
    QRhiTextureSubresourceUploadDescription description(
            entry.retained.constData() + first * DataTexelBytes, quint32((end - first) * DataTexelBytes));
    description.setDestinationTopLeft(QPoint(first, 0));
    description.setSourceSize(QSize(end - first, 1));
    batch->uploadTexture(entry.rhiData, QRhiTextureUploadDescription({0, 0, description}));
}

// A GPU buffer of the size, reusing one that fits exactly. QRhi's Vulkan
// backend keeps a host copy of a Static buffer, as large as the buffer, for
// later uploads (one per frame in flight), and frees it after the upload only
// for Immutable ones, which then allocate a new copy for every later upload
// (terrain editing: one per patch a brush step, slow on the Steam Deck's
// driver). Meshes that are edited (dynamic, or uploaded a third time: legacy
// terrain tiles are built twice while loading, and on every brush step when
// painted) are Static, the rest Immutable.
QRhiBuffer *ensureBuffer(Store &s, QRhiBuffer *current, QRhi *rhi, QRhiBuffer::UsageFlags usage,
                         int size, bool edited) {
    const QRhiBuffer::Type type = edited ? QRhiBuffer::Static : QRhiBuffer::Immutable;
    if (current != nullptr && current->size() == quint32(size) && current->type() == type)
        return current;
    if (current != nullptr)
        s.deadRhiBuffers.push_back(current);
    QRhiBuffer *buffer = rhi->newBuffer(type, usage, quint32(std::max(size, 4)));
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
        const bool edited = data.dynamic || entry.rhiUploads >= 2;
        ++entry.rhiUploads;
        const bool shareable = !edited && data.format != MeshData::Buffer
                && data.format != MeshData::TerrainHeightNormal;
        if (data.format == MeshData::Buffer) {
            entry.retained = QByteArray(vertexBytes, size);
            // Buffers of other uses are made again from the new bytes.
            dropStorage(s, entry.rhiVertex, entry.rhiVertexSlice, s.rhiVertexPool);
            if (entry.rhiData != nullptr)
                s.deadRhiBuffers.push_back(entry.rhiData);
            entry.rhiData = nullptr;
            entry.rhiVertex = ensureBuffer(s, nullptr, rhi, QRhiBuffer::IndexBuffer, size, edited);
        } else if (shareable && size > 0 && quint32(size) <= LargestSlice) {
            dropStorage(s, entry.rhiVertex, entry.rhiVertexSlice, s.rhiVertexPool);
            if (allocateSlice(s.rhiVertexPool, rhi, quint32(size), entry.rhiVertexSlice))
                entry.rhiVertex = entry.rhiVertexSlice.buffer;
        } else {
            if (entry.rhiVertexSlice.buffer != nullptr)
                dropStorage(s, entry.rhiVertex, entry.rhiVertexSlice, s.rhiVertexPool);
            entry.rhiVertex = ensureBuffer(s, entry.rhiVertex, rhi, QRhiBuffer::VertexBuffer, size,
                                           edited);
        }
        ++(entry.bytes > 0 ? s.rhiAgainUploads : s.rhiNewUploads);
        s.rhiUploadBytes += size + data.indices.size();
        QByteArray converted;
        if (data.format == MeshData::TerrainHeightNormal) {
            converted = QByteArray(vertexBytes, size);
            convertTerrainNormals(converted.data(), 0, size);
            vertexBytes = converted.constData();
        }
        if (entry.rhiVertex != nullptr && size > 0)
            batch->uploadStaticBuffer(entry.rhiVertex, entry.rhiVertexSlice.offset, quint32(size),
                                      vertexBytes);
        const quint32 indexBytes = quint32(data.indices.size());
        if (indexBytes > 0 && shareable && indexBytes <= LargestSlice) {
            dropStorage(s, entry.rhiIndex, entry.rhiIndexSlice, s.rhiIndexPool);
            if (allocateSlice(s.rhiIndexPool, rhi, indexBytes, entry.rhiIndexSlice))
                entry.rhiIndex = entry.rhiIndexSlice.buffer;
        } else if (indexBytes > 0) {
            if (entry.rhiIndexSlice.buffer != nullptr)
                dropStorage(s, entry.rhiIndex, entry.rhiIndexSlice, s.rhiIndexPool);
            entry.rhiIndex = ensureBuffer(s, entry.rhiIndex, rhi, QRhiBuffer::IndexBuffer,
                                          int(indexBytes), edited);
        } else {
            dropStorage(s, entry.rhiIndex, entry.rhiIndexSlice, s.rhiIndexPool);
        }
        if (entry.rhiIndex != nullptr && indexBytes > 0)
            batch->uploadStaticBuffer(entry.rhiIndex, entry.rhiIndexSlice.offset, indexBytes,
                                      data.indices.constData());
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
            batch->uploadStaticBuffer(entry.rhiVertex, entry.rhiVertexSlice.offset + quint32(range.offset),
                                      quint32(bytes.size()), bytes.constData());
            ++s.rhiRangeUploads;
            s.rhiUploadBytes += bytes.size();
            if (!entry.retained.isEmpty()) {
                std::memcpy(entry.retained.data() + range.offset, range.data.constData(),
                            size_t(range.data.size()));
                if (entry.rhiData != nullptr)
                    uploadDataTexels(entry, batch, range.offset, int(range.data.size()));
            }
        }
        entry.ranges.clear();
    }
    if (entry.rhiVertex == nullptr)
        return false;
    buffers.vertexBuffer = entry.rhiVertex;
    buffers.indexBuffer = entry.rhiIndex;
    buffers.vertexOffset = entry.rhiVertexSlice.offset;
    buffers.indexOffset = entry.rhiIndexSlice.offset;
    buffers.format = entry.format;
    buffers.layout = entry.layout;
    if (entry.sharedIndices.valid()) {
        const MeshHandle shared = entry.sharedIndices;
        Meshes::RhiBuffers indices;
        buffers.indexBuffer = prepareRhiLocked(s, shared, rhi, batch, indices, depth + 1)
                ? indices.vertexBuffer : nullptr;
        buffers.indexOffset = indices.vertexOffset;
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

QRhiTexture *Meshes::dataTextureRhi(MeshHandle handle, QRhi *rhi, QRhiResourceUpdateBatch *batch) {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    RhiBuffers buffers;
    if (!prepareRhiLocked(s, handle, rhi, batch, buffers, 0))
        return nullptr;
    Entry &entry = s.entries[handle.index];
    if (entry.format != MeshData::Buffer || entry.retained.size() < DataTexelBytes)
        return nullptr;
    if (entry.rhiData == nullptr) {
        const int texels = int(entry.retained.size() / DataTexelBytes);
        QRhiTexture *texture = rhi->newTexture(QRhiTexture::RGBA32F, QSize(texels, 1));
        if (!texture->create()) {
            delete texture;
            return nullptr;
        }
        entry.rhiData = texture;
        uploadDataTexels(entry, batch, 0, texels * DataTexelBytes);
    }
    return entry.rhiData;
}

void Meshes::collectGarbageRhi() {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    for (QRhiResource *resource : s.deadRhiBuffers)
        resource->deleteLater();
    s.deadRhiBuffers.clear();
    // Released slices go back to their chunks once the GPU is past them.
    for (auto dead = s.deadSlices.begin(); dead != s.deadSlices.end();) {
        if (--dead->frames > 0) {
            ++dead;
            continue;
        }
        freeSlice(*dead->pool, dead->slice);
        dead = s.deadSlices.erase(dead);
    }
}

QString Meshes::rhiTraceSummary() {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    qint64 immutable = 0, other = 0;
    for (const Pool *pool : {&s.rhiVertexPool, &s.rhiIndexPool})
        for (const Chunk &chunk : pool->chunks)
            other += chunk.buffer->size();
    for (const Entry &entry : s.entries) {
        if (entry.rhiVertex != nullptr && entry.rhiVertexSlice.buffer == nullptr)
            (entry.rhiVertex->type() == QRhiBuffer::Immutable ? immutable : other)
                    += entry.rhiVertex->size();
        if (entry.rhiIndex != nullptr && entry.rhiIndexSlice.buffer == nullptr)
            (entry.rhiIndex->type() == QRhiBuffer::Immutable ? immutable : other)
                    += entry.rhiIndex->size();
        if (entry.rhiData != nullptr)
            immutable += qint64(entry.rhiData->pixelSize().width()) * DataTexelBytes;
    }
    const QString summary = QString("immutable MB %1 static MB %2 uploads new %3 again %4 ranges %5 KB %6")
            .arg(immutable / 1048576).arg(other / 1048576).arg(s.rhiNewUploads)
            .arg(s.rhiAgainUploads).arg(s.rhiRangeUploads).arg(s.rhiUploadBytes / 1024);
    s.rhiNewUploads = s.rhiAgainUploads = s.rhiRangeUploads = 0;
    s.rhiUploadBytes = 0;
    return summary;
}

void Meshes::releaseAllRhi() {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    for (Entry &entry : s.entries) {
        if (entry.rhiVertexSlice.buffer == nullptr)
            delete entry.rhiVertex;
        if (entry.rhiIndexSlice.buffer == nullptr)
            delete entry.rhiIndex;
        delete entry.rhiData;
        entry.rhiVertex = entry.rhiIndex = nullptr;
        entry.rhiData = nullptr;
        entry.rhiVertexSlice = entry.rhiIndexSlice = Slice();
    }
    for (Pool *pool : {&s.rhiVertexPool, &s.rhiIndexPool}) {
        for (Chunk &chunk : pool->chunks)
            delete chunk.buffer;
        pool->chunks.clear();
    }
    s.deadSlices.clear();
    for (QRhiResource *resource : s.deadRhiBuffers)
        delete resource;
    s.deadRhiBuffers.clear();
}
