/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ShapeTriangles.h"

#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include <tsre/shape/GltfShape.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/shape/ShapeMetadata.h>

namespace Humans {

namespace {

QMutex &cacheLock() {
    static QMutex lock;
    return lock;
}

QHash<QString, std::shared_ptr<const ShapeTriangles>> &cache() {
    static QHash<QString, std::shared_ptr<const ShapeTriangles>> files;
    return files;
}

std::shared_ptr<const ShapeTriangles> readFile(const QString &path, const QString &texPath) {
    auto out = std::make_shared<ShapeTriangles>();
    const QString extension = QFileInfo(path).suffix().toLower();
    ShapeMetadata metadata;
    ShapeMetadata::read(path, metadata);
    if (extension == "s") {
        SFileComplex shape(path, QFileInfo(path).fileName(), texPath);
        if (shape.loadData()) {
            const unsigned int state = shape.newState();
            std::vector<float> points, uvs;
            for (int part = 0; part < shape.partCount(); ++part) {
                float m[16];
                if (!shape.partTriangles(part, points, uvs) || !shape.partMatrix(state, part, m))
                    continue;
                for (size_t i = 0; i + 2 < points.size(); i += 3)
                    for (int r = 0; r < 3; ++r)
                        out->triangles.push_back(m[r] * points[i] + m[4 + r] * points[i + 1]
                                                 + m[8 + r] * points[i + 2] + m[12 + r]);
            }
            out->read = true;
        }
        out->walk = metadata.walk;
    } else if (extension == "gltf" || extension == "glb") {
        QString walk;
        out->read = GltfShape::readTriangles(path, texPath, out->triangles, &walk);
        out->walk = !walk.isEmpty() ? walk.trimmed() : metadata.walk;
    }
    return out;
}

}

std::shared_ptr<const ShapeTriangles> shapeTriangles(const QString &path, const QString &texPath) {
    const QString key = path + QLatin1Char('|') + texPath;
    {
        QMutexLocker locked(&cacheLock());
        const auto found = cache().constFind(key);
        if (found != cache().constEnd())
            return found.value();
    }
    // Read outside the lock; a file read twice at once is kept once.
    std::shared_ptr<const ShapeTriangles> read = readFile(path, texPath);
    QMutexLocker locked(&cacheLock());
    const auto found = cache().constFind(key);
    if (found != cache().constEnd())
        return found.value();
    cache().insert(key, read);
    return read;
}

void forgetShapeTriangles() {
    QMutexLocker locked(&cacheLock());
    cache().clear();
}

}
