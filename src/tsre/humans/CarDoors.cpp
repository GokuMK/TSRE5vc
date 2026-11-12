/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/humans/CarDoors.h>

#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <tsre/maszyna/E3dFile.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/trains/CarAnimation.h>

namespace Humans {

namespace {

QMutex &lock() {
    static QMutex mutex;
    return mutex;
}

// Read files, and those being read.
QHash<QString, std::shared_ptr<const CarDoors>> &files() {
    static QHash<QString, std::shared_ptr<const CarDoors>> read;
    return read;
}

QSet<QString> &reading() {
    static QSet<QString> busy;
    return busy;
}

// The door group a part's name gives (CarAnimation's rule), -1 none.
int doorGroup(const QString &name) {
    if (!name.startsWith("DOOR"))
        return -1;
    if (name.startsWith("DOOR_D") || name.startsWith("DOOR_E") || name.startsWith("DOOR_F"))
        return CarAnimation::DoorLeft;
    if (name.startsWith("DOOR_A") || name.startsWith("DOOR_B") || name.startsWith("DOOR_C"))
        return CarAnimation::DoorRight;
    return -1;
}

// A MaSzyna model's doors: parts named the prefix and a number (MaSzyna's
// 8 at most when its .mmd gives none), odd ones on one side, even on the
// other, with the parts under them; read fresh (the shared copy gives its
// vertices to the renderer).
std::shared_ptr<const CarDoors> readE3dDoors(const QString &path, const QString &prefix, int count) {
    auto out = std::make_shared<CarDoors>();
    out->read = true;
    if (prefix.isEmpty())
        return out;
    QString error;
    const std::shared_ptr<const E3dModel> model = E3dFile::read(path, &error, true);
    if (!model || model->vertices.empty())
        return out;
    const E3dModel &m = *model;
    const auto indexAt = [&m](unsigned i) -> unsigned {
        const char *data = m.indices.constData();
        if (m.indexBytes == 4) {
            quint32 v;
            std::memcpy(&v, data + size_t(i) * 4, 4);
            return v;
        }
        quint16 v;
        std::memcpy(&v, data + size_t(i) * 2, 2);
        return v;
    };
    std::vector<float> sides[2];
    // A part and those under it: their triangles at rest, in the model's
    // space.
    std::function<void(int, std::vector<float> &)> collect = [&](int index, std::vector<float> &to) {
        const E3dModel::Part &part = m.parts[size_t(index)];
        if (part.kind == E3dModel::Kind::Mesh && part.vertexCount > 0) {
            const unsigned n = part.indexCount > 0 ? part.indexCount : part.vertexCount;
            for (unsigned k = 0; k < n; ++k) {
                const unsigned vertex = part.firstVertex + (part.indexCount > 0 ? indexAt(part.firstIndex + k) : k);
                if (size_t(vertex) * E3dModel::VertexFloats + 2 >= m.vertices.size())
                    continue;
                const float *v = m.vertices.data() + size_t(vertex) * E3dModel::VertexFloats;
                for (int r = 0; r < 3; ++r)
                    to.push_back(part.world[r] * v[0] + part.world[4 + r] * v[1] + part.world[8 + r] * v[2]
                                 + part.world[12 + r]);
            }
            // Whole triangles only.
            to.resize(to.size() - to.size() % 9);
        }
        for (int child = part.firstChild; child >= 0; child = m.parts[size_t(child)].nextSibling)
            collect(child, to);
    };
    const int doors = count > 0 ? count : 8;
    for (int i = 1; i <= doors; ++i) {
        const int part = m.byName.value((prefix + QString::number(i)).toLower(), -1);
        if (part < 0 || size_t(part) >= m.parts.size())
            continue;
        const size_t before = sides[(i & 1) != 0 ? 0 : 1].size();
        collect(part, sides[(i & 1) != 0 ? 0 : 1]);
        out->note += QString("%1%2 %3 points; ").arg(prefix).arg(i).arg((sides[(i & 1) != 0 ? 0 : 1].size() - before) / 3);
    }
    for (int side = 0; side < 2; ++side) {
        const std::vector<CarDoors::Door> found =
                CarDoors::split(sides[side], side == 0 ? CarAnimation::DoorLeft : CarAnimation::DoorRight);
        out->doors.insert(out->doors.end(), found.begin(), found.end());
    }
    return out;
}

std::shared_ptr<const CarDoors> readDoors(const QString &path, const QString &texPath) {
    auto out = std::make_shared<CarDoors>();
    out->read = true;
    if (QFileInfo(path).suffix().toLower() != "s")
        return out;
    SFileComplex shape(path, QFileInfo(path).fileName(), texPath);
    if (!shape.loadData())
        return out;
    const int parts = shape.partCount();
    // Each part's door group: its own name's, else its parent's.
    std::vector<int> group(size_t(parts), -1);
    for (int part = 0; part < parts; ++part) {
        int g = doorGroup(shape.partName(part).toUpper());
        for (int up = shape.partParent(part); g < 0 && up >= 0 && up < part; up = shape.partParent(up))
            g = group[size_t(up)];
        group[size_t(part)] = g;
    }
    const unsigned int state = shape.newState();
    std::vector<float> sides[2], points, uvs;
    for (int part = 0; part < parts; ++part) {
        const int g = group[size_t(part)];
        float m[16];
        if (g < 0 || !shape.partTriangles(part, points, uvs) || !shape.partMatrix(state, part, m))
            continue;
        std::vector<float> &to = sides[g == CarAnimation::DoorLeft ? 0 : 1];
        float zMin = 1e9f, zMax = -1e9f;
        for (size_t i = 0; i + 2 < points.size(); i += 3) {
            for (int r = 0; r < 3; ++r)
                to.push_back(m[r] * points[i] + m[4 + r] * points[i + 1] + m[8 + r] * points[i + 2] + m[12 + r]);
            zMin = std::min(zMin, to[to.size() - 1]);
            zMax = std::max(zMax, to[to.size() - 1]);
        }
        out->note += QString("%1 %2 points z %3..%4; ").arg(shape.partName(part)).arg(points.size() / 3)
                     .arg(zMin, 0, 'f', 1).arg(zMax, 0, 'f', 1);
    }
    for (int side = 0; side < 2; ++side) {
        const std::vector<CarDoors::Door> doors =
                CarDoors::split(sides[side], side == 0 ? CarAnimation::DoorLeft : CarAnimation::DoorRight);
        out->doors.insert(out->doors.end(), doors.begin(), doors.end());
    }
    return out;
}

}

std::vector<CarDoors::Door> CarDoors::split(const std::vector<float> &points, int group, float gap) {
    std::vector<Door> doors;
    // Each triangle's span along the car (a leaf's corners lie apart, so
    // spans, not points, are joined), in order of where they begin.
    const size_t triangles = points.size() / 9;
    if (triangles == 0)
        return doors;
    struct Span {
        float from, to;
        size_t triangle;
    };
    std::vector<Span> spans;
    spans.reserve(triangles);
    for (size_t t = 0; t < triangles; ++t) {
        const float *p = &points[t * 9];
        spans.push_back({std::min({p[2], p[5], p[8]}), std::max({p[2], p[5], p[8]}), t});
    }
    std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) { return a.from < b.from; });
    size_t first = 0;
    float reach = spans[0].to;
    for (size_t k = 1; k <= spans.size(); ++k) {
        if (k < spans.size() && spans[k].from <= reach + gap) {
            reach = std::max(reach, spans[k].to);
            continue;
        }
        // Spans first..k-1: one door.
        float zMin = spans[first].from, zMax = reach;
        float bottom = std::numeric_limits<float>::max();
        double x = 0.0;
        for (size_t j = first; j < k; ++j) {
            const float *p = &points[spans[j].triangle * 9];
            for (int c = 0; c < 3; ++c) {
                bottom = std::min(bottom, p[c * 3 + 1]);
                x += double(p[c * 3]);
            }
        }
        const float width = zMax - zMin;
        if (width >= 0.4f && width <= 4.5f) {
            Door door;
            door.group = group;
            door.z = 0.5f * (zMin + zMax);
            door.width = width;
            door.x = float(x / double((k - first) * 3));
            door.bottom = bottom;
            doors.push_back(door);
        }
        if (k < spans.size()) {
            first = k;
            reach = spans[k].to;
        }
    }
    return doors;
}

std::shared_ptr<const CarDoors> CarDoors::readNow(const QString &path, const QString &texPath, const QString &doorPrefix,
                                                  int doorCount) {
    const bool e3d = QFileInfo(path).suffix().compare(QLatin1String("e3d"), Qt::CaseInsensitive) == 0;
    return e3d ? readE3dDoors(path, doorPrefix, doorCount) : readDoors(path, texPath);
}

std::shared_ptr<const CarDoors> carDoors(const QString &path, const QString &texPath, const QString &doorPrefix,
                                         int doorCount) {
    const QString key = path + QLatin1Char('|') + texPath + QLatin1Char('|') + doorPrefix + QLatin1Char('|')
            + QString::number(doorCount);
    {
        QMutexLocker locked(&lock());
        const auto found = files().constFind(key);
        if (found != files().constEnd())
            return found.value();
        if (reading().contains(key))
            return nullptr;
        reading().insert(key);
    }
    QThreadPool::globalInstance()->start([key, path, texPath, doorPrefix, doorCount]() {
        std::shared_ptr<const CarDoors> read = CarDoors::readNow(path, texPath, doorPrefix, doorCount);
        QMutexLocker locked(&lock());
        reading().remove(key);
        files().insert(key, read);
    });
    return nullptr;
}

void forgetCarDoors() {
    QMutexLocker locked(&lock());
    files().clear();
}

}
