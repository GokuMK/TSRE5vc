/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/ShapeSeats.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/shape/SFileDocument.h>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <cmath>

namespace ShapeSeats {

namespace {

// A line's numbers: all `count` of them, finite; false otherwise.
bool numbers(const SFileDetail::Node &node, int count, float *out) {
    if (node.scalarCount() < count)
        return false;
    for (int i = 0; i < count; ++i) {
        const double v = node.number(i, std::nan(""));
        if (!std::isfinite(v))
            return false;
        out[i] = float(v);
    }
    return true;
}

bool numbers(const QJsonValue &value, int count, float *out) {
    const QJsonArray a = value.toArray();
    if (a.size() < count)
        return false;
    for (int i = 0; i < count; ++i) {
        if (!a[i].isDouble())
            return false;
        out[i] = float(a[i].toDouble());
    }
    return true;
}

void finish(Seats &seats, QStringList *problems) {
    if (problems == nullptr)
        return;
    if (!seats.seats.isEmpty() && seats.aisles.isEmpty())
        //% "seats without an Aisle: people cannot reach them, none used"
        *problems << qtTrId("shapeseats.noaisle");
}

}

Seats read(const SFileDetail::Node &shape, QStringList *problems) {
    Seats out;
    for (const SFileDetail::Node block : shape.children("esd_tsreseats")) {
        for (const SFileDetail::Node line : block.children()) {
            const QString name = line.name().toLower();
            float v[4];
            if (name == QLatin1String("seat")) {
                if (!numbers(line, 4, v)) {
                    if (problems != nullptr)
                        //% "a Seat without its four numbers (x y z heading) left out"
                        *problems << qtTrId("shapeseats.seat");
                    continue;
                }
                Seat seat;
                std::copy(v, v + 3, seat.position);
                seat.heading = v[3];
                seat.number = line.label().toInt();
                out.seats.push_back(seat);
            } else if (name == QLatin1String("aisle")) {
                if (!numbers(line, 4, v)) {
                    if (problems != nullptr)
                        //% "an Aisle without its four numbers (x y z0 z1) left out"
                        *problems << qtTrId("shapeseats.aisle");
                    continue;
                }
                out.aisles.push_back({v[0], v[1], std::min(v[2], v[3]), std::max(v[2], v[3])});
            } else if (name == QLatin1String("passage")) {
                if (!numbers(line, 4, v)) {
                    if (problems != nullptr)
                        //% "a Passage without its four numbers (x y z width) left out"
                        *problems << qtTrId("shapeseats.passage");
                    continue;
                }
                Passage passage;
                std::copy(v, v + 3, passage.position);
                passage.width = v[3];
                out.passages.push_back(passage);
            } else if (problems != nullptr) {
                //% "%1 is not one of its lines (Seat, Aisle, Passage)"
                *problems << qtTrId("shapeseats.unknown").arg(line.name());
            }
        }
    }
    finish(out, problems);
    return out;
}

Seats fromExtras(const QJsonObject &extras, QStringList *problems) {
    Seats out;
    const QJsonObject block = extras.value(QStringLiteral("TSRE_seats")).toObject();
    float v[4];
    for (const QJsonValue &value : block.value(QStringLiteral("seats")).toArray()) {
        if (!numbers(value, 4, v)) {
            if (problems != nullptr)
                *problems << qtTrId("shapeseats.seat");
            continue;
        }
        Seat seat;
        std::copy(v, v + 3, seat.position);
        seat.heading = v[3];
        seat.number = out.seats.size() + 1;
        out.seats.push_back(seat);
    }
    for (const QJsonValue &value : block.value(QStringLiteral("aisles")).toArray()) {
        if (!numbers(value, 4, v)) {
            if (problems != nullptr)
                *problems << qtTrId("shapeseats.aisle");
            continue;
        }
        out.aisles.push_back({v[0], v[1], std::min(v[2], v[3]), std::max(v[2], v[3])});
    }
    for (const QJsonValue &value : block.value(QStringLiteral("passages")).toArray()) {
        if (!numbers(value, 4, v)) {
            if (problems != nullptr)
                *problems << qtTrId("shapeseats.passage");
            continue;
        }
        Passage passage;
        std::copy(v, v + 3, passage.position);
        passage.width = v[3];
        out.passages.push_back(passage);
    }
    finish(out, problems);
    return out;
}

namespace {

// A glTF file's JSON (a .glb's first chunk, or a .gltf's text).
QJsonObject gltfJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QByteArray json;
    if (QFileInfo(path).suffix().compare(QLatin1String("glb"), Qt::CaseInsensitive) == 0) {
        const QByteArray header = file.read(20);
        if (header.size() < 20 || !header.startsWith("glTF"))
            return {};
        const quint32 length = qFromLittleEndian<quint32>(header.constData() + 12);
        if (header.mid(16, 4) != "JSON" || length > 64u * 1024u * 1024u)
            return {};
        json = file.read(qint64(length));
    } else {
        json = file.readAll();
    }
    return QJsonDocument::fromJson(json).object();
}

}

bool readFor(const QString &modelPath, Seats &out, QStringList *problems) {
    out = Seats();
    const QString suffix = QFileInfo(modelPath).suffix().toLower();
    if (suffix == QLatin1String("glb") || suffix == QLatin1String("gltf")) {
        const QJsonObject json = gltfJson(modelPath);
        const QJsonArray scenes = json.value(QStringLiteral("scenes")).toArray();
        const int scene = json.value(QStringLiteral("scene")).toInt(0);
        if (scene < 0 || scene >= scenes.size())
            return false;
        out = fromExtras(scenes[scene].toObject().value(QStringLiteral("extras")).toObject(), problems);
        return !out.empty();
    }
    const QString sd = ContentPath::withExtension(modelPath, "sd");
    if (!QFileInfo::exists(sd))
        return false;
    SFileDetail::Document document;
    if (!document.read(sd)) {
        if (problems != nullptr)
            *problems << document.diagnostics;
        return false;
    }
    out = read(document.root, problems);
    return !out.empty();
}

Seats drawn(const Seats &seats, const QString &modelPath) {
    if (QFileInfo(modelPath).suffix().compare(QLatin1String("s"), Qt::CaseInsensitive) != 0)
        return seats;
    Seats out = seats;
    for (Seat &seat : out.seats) {
        seat.position[0] = -seat.position[0];
        seat.heading = -seat.heading;
    }
    for (Aisle &aisle : out.aisles)
        aisle.x = -aisle.x;
    for (Passage &passage : out.passages)
        passage.position[0] = -passage.position[0];
    return out;
}

}
