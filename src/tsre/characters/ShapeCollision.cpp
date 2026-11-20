/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#include "ShapeCollision.h"

#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/ShapeMetadata.h>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <array>

namespace Characters {

namespace {

struct Entry {
    std::shared_ptr<const CollisionMesh> mesh;
    ShapeCollisionInfo info;
};

QMutex &cacheLock() {
    static QMutex lock;
    return lock;
}

QHash<QString, Entry> &cache() {
    static QHash<QString, Entry> entries;
    return entries;
}

void addBox(std::vector<float> &t, const std::array<float, 6> &b) {
    const float x0 = std::min(b[0], b[3]), x1 = std::max(b[0], b[3]);
    const float y0 = std::min(b[1], b[4]), y1 = std::max(b[1], b[4]);
    const float z0 = std::min(b[2], b[5]), z1 = std::max(b[2], b[5]);
    const float p[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1},
                           {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}};
    const int faces[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    for (const auto &f : faces)
        for (int k : {f[0], f[1], f[2], f[0], f[2], f[3]})
            t.insert(t.end(), p[k], p[k] + 3);
}

Entry build(const QString &path, const QString &texPath, Collision mode) {
    Entry out;
    ShapeMetadata metadata;
    const bool hasMetadata = ShapeMetadata::read(path, metadata, nullptr);
    if (mode == Collision::Default && hasMetadata)
        mode = collisionNamed(metadata.collision);
    out.info.taken = mode;
    if (mode == Collision::None)
        return out;
    std::vector<ComplexShape::TrianglePart> parts;
    ComplexShape::readTriangleParts(path, texPath, parts);
    out.info.parts = int(parts.size());
    for (const ComplexShape::TrianglePart &part : parts) {
        out.info.vegetationParts += part.vegetation ? 1 : 0;
        out.info.alphaTestedParts += part.surface == ComplexShape::TrianglePart::Surface::AlphaTest ? 1 : 0;
    }
    std::vector<float> triangles;
    if (mode == Collision::Box) {
        std::vector<std::array<float, 6>> boxes;
        if (hasMetadata && int(metadata.boxes.size()) > metadata.boundingBoxes)
            boxes.assign(metadata.boxes.begin() + metadata.boundingBoxes, metadata.boxes.end());
        else if (hasMetadata && metadata.boundingBoxes > 0)
            boxes.push_back(metadata.boxes.front());
        // An S file's .sd is in its file's space: drawn mirrored in x.
        if (QFileInfo(path).suffix().compare(QLatin1String("s"), Qt::CaseInsensitive) == 0)
            for (std::array<float, 6> &b : boxes) {
                const float x0 = -b[3], x1 = -b[0];
                b[0] = x0;
                b[3] = x1;
            }
        if (boxes.empty()) {
            // Its triangles' bounds.
            bool any = false;
            std::array<float, 6> b{};
            for (const ComplexShape::TrianglePart &part : parts)
                for (size_t v = 0; v + 2 < part.triangles.size(); v += 3)
                    for (int i = 0; i < 3; ++i) {
                        const float c = part.triangles[v + size_t(i)];
                        b[size_t(i)] = any ? std::min(b[size_t(i)], c) : c;
                        b[size_t(i) + 3] = any ? std::max(b[size_t(i) + 3], c) : c;
                        any = i == 2 || any;
                    }
            if (any)
                boxes.push_back(b);
        }
        for (const std::array<float, 6> &b : boxes)
            addBox(triangles, b);
    } else {
        for (const ComplexShape::TrianglePart &part : parts)
            if (mode == Collision::Solid || !part.vegetation)
                triangles.insert(triangles.end(), part.triangles.begin(), part.triangles.end());
    }
    if (!triangles.empty())
        out.mesh = std::make_shared<CollisionMesh>(std::move(triangles));
    return out;
}

Entry entryOf(const QString &path, const QString &texPath, Collision mode) {
    const QString key = path + QLatin1Char('|') + texPath + QLatin1Char('|') + QString::number(int(mode));
    {
        QMutexLocker locker(&cacheLock());
        const auto found = cache().constFind(key);
        if (found != cache().constEnd())
            return found.value();
    }
    // Read outside the lock: two threads may read one file at once, the
    // first kept.
    Entry entry = build(path, texPath, mode);
    QMutexLocker locker(&cacheLock());
    const auto found = cache().constFind(key);
    if (found != cache().constEnd())
        return found.value();
    cache().insert(key, entry);
    return entry;
}

}

Collision collisionNamed(const QString &name) {
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("solid"))
        return Collision::Solid;
    if (n == QLatin1String("none"))
        return Collision::None;
    if (n == QLatin1String("box"))
        return Collision::Box;
    return Collision::Default;
}

Collision collisionOfObject(unsigned char value) {
    return value <= 3 ? Collision(value) : Collision::Default;
}

std::shared_ptr<const CollisionMesh> shapeCollision(const QString &path, const QString &texPath, Collision mode) {
    return entryOf(path, texPath, mode).mesh;
}

ShapeCollisionInfo shapeCollisionInfo(const QString &path, const QString &texPath, Collision mode) {
    return entryOf(path, texPath, mode).info;
}

void forgetShapeCollisions() {
    QMutexLocker locker(&cacheLock());
    cache().clear();
}

}
