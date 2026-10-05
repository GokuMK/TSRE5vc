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
#include <memory>

#ifndef GL_COPY_WRITE_BUFFER
#define GL_COPY_WRITE_BUFFER 0x8F37
#endif

namespace {

struct Entry {
    // Odd while live; a released slot moves to the next even number, so
    // stale handles never match a reused slot.
    quint32 generation = 0;
    // Data waiting for upload; uploaded data is dropped from the CPU.
    std::unique_ptr<MeshData> pending;
    GLuint vertexBuffer = 0;
    GLuint indexBuffer = 0;
    RenderItem::VertexAttr layout = RenderItem::NO_ATTR;
    quint64 stamp = 0;
};

struct Store {
    QMutex mutex;
    std::vector<Entry> entries;
    std::vector<quint32> freeSlots;
    std::vector<GLuint> deadBuffers;
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
            s.entries[handle.index].pending = std::make_unique<MeshData>(std::move(data));
            return;
        }
    }
    handle = create(std::move(data));
}

void Meshes::release(MeshHandle &handle) {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    if (live(s, handle)) {
        Entry &entry = s.entries[handle.index];
        entry.generation += 1;
        entry.pending.reset();
        if (entry.vertexBuffer != 0)
            s.deadBuffers.push_back(entry.vertexBuffer);
        if (entry.indexBuffer != 0)
            s.deadBuffers.push_back(entry.indexBuffer);
        entry.vertexBuffer = 0;
        entry.indexBuffer = 0;
        entry.layout = RenderItem::NO_ATTR;
        s.freeSlots.push_back(handle.index);
        s.releases++;
    }
    handle = MeshHandle();
}

bool Meshes::prepare(MeshHandle handle, QOpenGLFunctions *f, Buffers &buffers) {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    if (!live(s, handle))
        return false;
    Entry &entry = s.entries[handle.index];
    if (entry.pending) {
        const MeshData &data = *entry.pending;
        const bool hadIndices = entry.indexBuffer != 0;
        if (entry.vertexBuffer == 0)
            f->glGenBuffers(1, &entry.vertexBuffer);
        f->glBindBuffer(GL_ARRAY_BUFFER, entry.vertexBuffer);
        f->glBufferData(GL_ARRAY_BUFFER, data.vertices.size() * sizeof(float),
                        data.vertices.data(), GL_STATIC_DRAW);
        f->glBindBuffer(GL_ARRAY_BUFFER, 0);
        // The copy target leaves the bound vertex array's index binding alone.
        if (!data.indices.isEmpty()) {
            if (entry.indexBuffer == 0)
                f->glGenBuffers(1, &entry.indexBuffer);
            f->glBindBuffer(GL_COPY_WRITE_BUFFER, entry.indexBuffer);
            f->glBufferData(GL_COPY_WRITE_BUFFER, data.indices.size(),
                            data.indices.constData(), GL_STATIC_DRAW);
            f->glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        } else if (entry.indexBuffer != 0) {
            s.deadBuffers.push_back(entry.indexBuffer);
            entry.indexBuffer = 0;
        }
        if (entry.stamp == 0 || entry.layout != data.layout
                || hadIndices != (entry.indexBuffer != 0))
            entry.stamp = s.nextStamp++;
        entry.layout = data.layout;
        entry.pending.reset();
    }
    if (entry.vertexBuffer == 0)
        return false;
    buffers.vertexBuffer = entry.vertexBuffer;
    buffers.indexBuffer = entry.indexBuffer;
    buffers.layout = entry.layout;
    buffers.stamp = entry.stamp;
    return true;
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

void Meshes::setupAttributes(QOpenGLFunctions *f, RenderItem::VertexAttr layout) {
    // Locations: 0 position, 1 texture coordinates, 2 normal, 3 alpha.
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
    case RenderItem::VNTA:
        attribute(0, 3, 0);
        attribute(2, 3, 3);
        attribute(1, 2, 6);
        attribute(3, 1, 8);
        break;
    default:
        break;
    }
}
