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
#include <tsre/shape/ShapeAxes.h>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

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

// An aisle from its line's numbers: x y z0 z1, and the floor at z1 where
// a fifth is given (stairs, a ramp); its ends in order along the car.
Aisle aisleOf(const float *v, bool sloped, int number) {
    Aisle aisle;
    aisle.x = v[0];
    aisle.number = number;
    const float y0 = v[1], y1 = sloped ? v[4] : v[1];
    if (v[2] <= v[3]) {
        aisle.z0 = v[2];
        aisle.z1 = v[3];
        aisle.y = y0;
        aisle.rise = y1 - y0;
    } else {
        aisle.z0 = v[3];
        aisle.z1 = v[2];
        aisle.y = y1;
        aisle.rise = y0 - y1;
    }
    return aisle;
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
            float v[5];
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
                out.aisles.push_back(aisleOf(v, numbers(line, 5, v), line.label().toInt()));
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
            } else if (name == QLatin1String("endpassage")) {
                if (!numbers(line, 4, v) || !(v[3] > 0.0f)) {
                    if (problems != nullptr)
                        //% "an EndPassage without its four numbers (x y z width) left out"
                        *problems << qtTrId("shapeseats.endpassage");
                    continue;
                }
                EndPassage passage;
                std::copy(v, v + 3, passage.position);
                passage.width = v[3];
                out.endPassages.push_back(passage);
            } else if (name == QLatin1String("door")) {
                if (!numbers(line, 4, v) || !(v[3] > 0.0f)) {
                    if (problems != nullptr)
                        //% "a Door without its four numbers (x y z width) left out"
                        *problems << qtTrId("shapeseats.door");
                    continue;
                }
                Door door;
                std::copy(v, v + 3, door.position);
                door.width = v[3];
                door.number = line.label().toInt();
                out.doors.push_back(door);
            } else if (problems != nullptr) {
                //% "%1 is not one of its lines (Seat, Aisle, Passage, EndPassage, Door)"
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
    float v[5];
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
        out.aisles.push_back(aisleOf(v, numbers(value, 5, v), int(out.aisles.size()) + 1));
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
    for (const QJsonValue &value : block.value(QStringLiteral("endPassages")).toArray()) {
        if (!numbers(value, 4, v) || !(v[3] > 0.0f)) {
            if (problems != nullptr)
                *problems << qtTrId("shapeseats.endpassage");
            continue;
        }
        EndPassage passage;
        std::copy(v, v + 3, passage.position);
        passage.width = v[3];
        out.endPassages.push_back(passage);
    }
    for (const QJsonValue &value : block.value(QStringLiteral("doors")).toArray()) {
        if (!numbers(value, 4, v) || !(v[3] > 0.0f)) {
            if (problems != nullptr)
                *problems << qtTrId("shapeseats.door");
            continue;
        }
        Door door;
        std::copy(v, v + 3, door.position);
        door.width = v[3];
        door.number = out.doors.size() + 1;
        out.doors.push_back(door);
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
        return !out.none();
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
    // In the model's own axes (task shapes 20).
    const QString axesName = document.root.child("esd_tsreaxes").scalar(0);
    const ShapeAxes::Axes axes = ShapeAxes::fromName(axesName);
    if (axes == ShapeAxes::Axes::Unknown && problems != nullptr && !out.none())
        *problems << ShapeAxes::unknown(axesName);
    if (ShapeAxes::mirrors(axes, ShapeAxes::ofModel(modelPath)))
        mirror(out);
    return !out.none();
}

void mirror(Seats &seats) {
    for (Seat &seat : seats.seats) {
        ShapeAxes::mirrorPoint(seat.position);
        seat.heading = ShapeAxes::mirrorHeading(seat.heading);
    }
    for (Aisle &aisle : seats.aisles)
        aisle.x = -aisle.x;
    for (Passage &passage : seats.passages)
        ShapeAxes::mirrorPoint(passage.position);
    for (EndPassage &passage : seats.endPassages)
        ShapeAxes::mirrorPoint(passage.position);
    for (Door &door : seats.doors)
        ShapeAxes::mirrorPoint(door.position);
}

Seats drawn(const Seats &seats, const QString &modelPath) {
    if (ShapeAxes::ofModel(modelPath) != ShapeAxes::Axes::Msts)
        return seats;
    Seats out = seats;
    mirror(out);
    return out;
}

// Aisles as routes (task humans 12) ------------------------------------------

namespace {

// A seat's cushion top above its floor (m): the finder's cushions are 0.33
// to 0.58 m above it.
constexpr float CushionHeight = 0.45f;

}

bool Aisle::sloped() const {
    return std::fabs(rise) > LevelStep;
}

std::vector<Join> joins(const QVector<Aisle> &aisles) {
    std::vector<Join> out;
    // A sloped aisle's end at z onto aisle o: where o holds z (JoinGap to
    // spare) with its floor there at most OneStep from the end's; onto
    // stairs only at their own end. Where on o: `at`.
    const auto endOnto = [](const Aisle &s, float z, const Aisle &o, float *at) {
        if (z < o.z0 - JoinGap || z > o.z1 + JoinGap)
            return false;
        float zo = std::clamp(z, o.z0, o.z1);
        if (o.sloped()) {
            if (std::fabs(zo - o.z0) <= JoinGap)
                zo = o.z0;
            else if (std::fabs(zo - o.z1) <= JoinGap)
                zo = o.z1;
            else
                return false;
        }
        if (std::fabs(o.yAt(zo) - s.yAt(z)) > OneStep + StepTolerance)
            return false;
        *at = zo;
        return true;
    };
    for (int i = 0; i < aisles.size(); ++i)
        for (int j = i + 1; j < aisles.size(); ++j) {
            const Aisle &p = aisles[i], &q = aisles[j];
            if (!p.sloped() && !q.sloped()) {
                // Two level aisles: overlapping or nearly meeting, a step
                // at most between their floors.
                if (std::fabs(p.y - q.y) > OneStep + StepTolerance)
                    continue;
                const float overlap = std::min(p.z1, q.z1) - std::max(p.z0, q.z0);
                if (overlap < -JoinGap)
                    continue;
                Join join;
                join.a = i;
                join.b = j;
                if (overlap >= 0.0f) {
                    join.za = join.zb = 0.5f * (std::max(p.z0, q.z0) + std::min(p.z1, q.z1));
                } else if (p.z1 <= q.z0) {
                    join.za = p.z1;
                    join.zb = q.z0;
                } else {
                    join.za = p.z0;
                    join.zb = q.z1;
                }
                out.push_back(join);
                continue;
            }
            // Stairs or a ramp: at their ends (both sloped: each end tried
            // onto the other, a join found twice kept once).
            const size_t first = out.size();
            for (int side = 0; side < 2; ++side) {
                const int a = side == 0 ? i : j, b = side == 0 ? j : i;
                const Aisle &s = aisles[a], &o = aisles[b];
                if (!s.sloped())
                    continue;
                for (const float z : {s.z0, s.z1}) {
                    float at = 0.0f;
                    if (!endOnto(s, z, o, &at))
                        continue;
                    bool known = false;
                    for (size_t k = first; k < out.size(); ++k) {
                        const Join &n = out[k];
                        known = known || (n.a == b && n.b == a && n.za == at && n.zb == z)
                                || (n.a == a && n.b == b && n.za == z && n.zb == at);
                    }
                    if (!known)
                        out.push_back({a, b, z, at});
                }
            }
        }
    return out;
}

int aisleFor(const QVector<Aisle> &aisles, float x, float floor, float z, float reach) {
    int best = -1;
    float bestY = 0.0f, bestX = 0.0f;
    for (int k = 0; k < aisles.size(); ++k) {
        const Aisle &a = aisles[k];
        if (a.sloped() || z < a.z0 - reach || z > a.z1 + reach)
            continue;
        const float dy = std::isnan(floor) ? 0.0f : std::fabs(a.y - floor);
        const float dx = std::fabs(a.x - x);
        if (best < 0 || dy < bestY - LevelStep || (std::fabs(dy - bestY) <= LevelStep && dx < bestX)) {
            best = k;
            bestY = dy;
            bestX = dx;
        }
    }
    if (best >= 0)
        return best;
    // None holds it: the nearest along the car (then by height).
    float bestGap = 0.0f;
    for (int k = 0; k < aisles.size(); ++k) {
        const Aisle &a = aisles[k];
        if (a.sloped())
            continue;
        const float gap = z < a.z0 ? a.z0 - z : (z > a.z1 ? z - a.z1 : 0.0f);
        const float dy = std::isnan(floor) ? 0.0f : std::fabs(a.y - floor);
        if (best < 0 || gap < bestGap - 0.01f || (std::fabs(gap - bestGap) <= 0.01f && dy < bestY)) {
            best = k;
            bestGap = gap;
            bestY = dy;
        }
    }
    return best;
}

int aisleOfSeat(const QVector<Aisle> &aisles, const Seat &seat) {
    return aisleFor(aisles, seat.position[0], seat.position[1] - CushionHeight, seat.position[2]);
}

void connect(Seats &seats) {
    seats.joins = joins(seats.aisles);
    seats.seatAisles.assign(size_t(seats.seats.size()), -1);
    for (int k = 0; k < seats.seats.size(); ++k)
        seats.seatAisles[size_t(k)] = aisleOfSeat(seats.aisles, seats.seats[k]);
}

bool route(const QVector<Aisle> &aisles, const std::vector<Join> &joins, int from, float fromZ, int to, float toZ,
           Way *way) {
    if (from < 0 || to < 0 || from >= aisles.size() || to >= aisles.size())
        return false;
    // The places: the start, the end and each join's two ends; along an
    // aisle between any two of its places, across a join between its ends.
    struct Node {
        int aisle;
        float z;
    };
    std::vector<Node> nodes{{from, fromZ}, {to, toZ}};
    for (const Join &j : joins) {
        nodes.push_back({j.a, j.za});
        nodes.push_back({j.b, j.zb});
    }
    const size_t n = nodes.size();
    const auto across = [&](size_t i) -> int {
        // A join's other end (its two ends are next to each other).
        return i >= 2 ? int(i % 2 == 0 ? i + 1 : i - 1) : -1;
    };
    const auto cost = [&](size_t i, size_t k) -> float {
        const Node &p = nodes[i], &q = nodes[k];
        if (p.aisle == q.aisle)
            return std::fabs(p.z - q.z);
        if (int(k) == across(i)) {
            const Aisle &a = aisles[p.aisle], &b = aisles[q.aisle];
            return std::hypot(b.x - a.x, q.z - p.z);
        }
        return -1.0f;
    };
    std::vector<float> dist(n, std::numeric_limits<float>::infinity());
    std::vector<int> previous(n, -1);
    std::vector<bool> done(n, false);
    dist[0] = 0.0f;
    for (;;) {
        size_t u = n;
        for (size_t i = 0; i < n; ++i)
            if (!done[i] && std::isfinite(dist[i]) && (u == n || dist[i] < dist[u]))
                u = i;
        if (u == n || u == 1)
            break;
        done[u] = true;
        for (size_t k = 0; k < n; ++k) {
            if (done[k] || k == u)
                continue;
            const float c = cost(u, k);
            if (c >= 0.0f && dist[u] + c < dist[k]) {
                dist[k] = dist[u] + c;
                previous[k] = int(u);
            }
        }
    }
    if (!std::isfinite(dist[1]))
        return false;
    if (way == nullptr)
        return true;
    std::vector<int> path;
    for (int i = 1; i >= 0; i = previous[size_t(i)])
        path.push_back(i);
    std::reverse(path.begin(), path.end());
    way->points.clear();
    way->length = dist[1];
    for (const int i : path) {
        const Node &node = nodes[size_t(i)];
        const Aisle &a = aisles[node.aisle];
        const float p[3] = {a.x, a.yAt(node.z), node.z};
        const size_t size = way->points.size();
        if (size >= 3 && way->points[size - 3] == p[0] && way->points[size - 2] == p[1] && way->points[size - 1] == p[2])
            continue;
        way->points.insert(way->points.end(), p, p + 3);
    }
    if (way->points.size() == 3)
        way->points.insert(way->points.end(), way->points.begin(), way->points.begin() + 3);
    return true;
}

namespace {

float legLength(const float *a, const float *b) {
    return std::hypot(b[0] - a[0], b[2] - a[2]);
}

}

Way wayStart(const Way &way, float length) {
    Way out;
    if (way.points.size() < 3)
        return out;
    out.points.assign(way.points.begin(), way.points.begin() + 3);
    float left = std::max(0.0f, length);
    for (size_t i = 3; i + 2 < way.points.size(); i += 3) {
        const float *a = &way.points[i - 3], *b = &way.points[i];
        const float d = legLength(a, b);
        if (d <= left) {
            out.points.insert(out.points.end(), b, b + 3);
            out.length += d;
            left -= d;
            continue;
        }
        const float t = d > 0.0f ? left / d : 0.0f;
        const float p[3] = {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
        out.points.insert(out.points.end(), p, p + 3);
        out.length += left;
        break;
    }
    return out;
}

Way wayShort(const Way &way, float length) {
    return wayStart(way, way.length - length);
}

std::vector<std::pair<float, int>> seatsByWay(const Seats &seats, int fromAisle, float fromZ,
                                              const std::vector<int> *taken) {
    std::vector<std::pair<float, int>> out;
    if (fromAisle < 0 || fromAisle >= seats.aisles.size() || seats.seatAisles.size() != size_t(seats.seats.size()))
        return out;
    for (int s = 0; s < seats.seats.size(); ++s) {
        if (taken != nullptr && (size_t(s) >= taken->size() || (*taken)[size_t(s)] >= 0))
            continue;
        const int k = seats.seatAisles[size_t(s)];
        const Seat &seat = seats.seats[s];
        Way way;
        if (k < 0 || !route(seats.aisles, seats.joins, fromAisle, fromZ, k, seat.position[2], &way))
            continue;
        out.push_back({way.length + std::fabs(seat.position[0] - seats.aisles[k].x), s});
    }
    std::sort(out.begin(), out.end());
    return out;
}

}
