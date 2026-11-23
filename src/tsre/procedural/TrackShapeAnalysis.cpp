/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/procedural/TrackShapeAnalysis.h>

#include <QHash>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TrackShape.h>

namespace {

using Point = TrackShapeAnalysis::Point;
using Line = QVector<Point>;

constexpr float Pi = 3.14159265358979323846f;

// How much a path turns, radians.
float straightness(const Line &line) {
    return line.isEmpty() ? 0.0f : std::abs(line.last().heading - line.first().heading);
}

bool straight(const Line &line) {
    return straightness(line) < 1e-4f;
}

// A straight path from the shape's origin along its axis.
bool onAxis(const Line &line) {
    return !line.isEmpty() && straight(line) && std::abs(line[0].x) < 1e-3f
            && std::abs(line[0].z) < 1e-3f && std::abs(line[0].heading) < 1e-4f;
}

struct Box {
    float minX = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = -std::numeric_limits<float>::max();
};

Box boxOf(const Line &line) {
    Box box;
    for(const Point &point : line){
        box.minX = std::min(box.minX, point.x);
        box.maxX = std::max(box.maxX, point.x);
        box.minZ = std::min(box.minZ, point.z);
        box.maxZ = std::max(box.maxZ, point.z);
    }
    return box;
}

// Whether two sampled centre lines come closer than `limit`: b's points in
// a grid of limit-sized cells, each of a's points checked against the nine
// cells around it.
bool closer(const Line &a, const Box &boxA, const Line &b, const Box &boxB, float limit) {
    const float gapX = std::max({0.0f, boxA.minX - boxB.maxX, boxB.minX - boxA.maxX});
    const float gapZ = std::max({0.0f, boxA.minZ - boxB.maxZ, boxB.minZ - boxA.maxZ});
    if(gapX * gapX + gapZ * gapZ >= limit * limit || limit <= 0)
        return false;
    const float limit2 = limit * limit;
    auto cell = [&](float value) { return qint64(std::floor(value / limit)); };
    auto key = [](qint64 x, qint64 z) { return (x << 32) ^ (z & 0xffffffff); };
    QHash<qint64, QVector<int>> grid;
    for(int i = 0; i < b.size(); i++)
        grid[key(cell(b[i].x), cell(b[i].z))].append(i);
    for(const Point &p : a){
        const qint64 cx = cell(p.x);
        const qint64 cz = cell(p.z);
        for(qint64 dx = -1; dx <= 1; dx++)
            for(qint64 dz = -1; dz <= 1; dz++){
                const auto found = grid.constFind(key(cx + dx, cz + dz));
                if(found == grid.constEnd())
                    continue;
                for(int i : found.value()){
                    const float ex = p.x - b[i].x;
                    const float ez = p.z - b[i].z;
                    if(ex * ex + ez * ez < limit2)
                        return true;
                }
            }
    }
    return false;
}

// Where the straight centre lines a and b cross, within [start, end) of
// both, as tieplan.py's sampled test finds it.
bool crossing(const Line &a, const Line &b, float &x, float &z) {
    const Point &a0 = a.first();
    const Point &a1 = a.last();
    const Point &b0 = b.first();
    const Point &b1 = b.last();
    const double rx = a1.x - a0.x, rz = a1.z - a0.z;
    const double sx = b1.x - b0.x, sz = b1.z - b0.z;
    const double den = rx * sz - rz * sx;
    if(std::abs(den) < 1e-12)
        return false;
    const double qx = b0.x - a0.x, qz = b0.z - a0.z;
    const double t = (qx * sz - qz * sx) / den;
    const double u = (qx * rz - qz * rx) / den;
    if(t < 0 || t >= 1 || u < 0 || u >= 1)
        return false;
    x = float(a0.x + t * rx);
    z = float(a0.z + t * rz);
    return true;
}

long rounded4(float value) {
    return std::lround(double(value) * 1e4);
}

TrackShapeAnalysis::Reference referenceOf(const QVector<Line> &paths,
        const QVector<int> &members, float maxSharedAngle) {
    TrackShapeAnalysis::Reference reference;
    // The path order: on the axis, the straightest, nearest the axis's
    // heading, the longest.
    QVector<int> order = members;
    std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
        auto key = [&](int i) {
            const Line &line = paths[i];
            return std::make_tuple(!onAxis(line), rounded4(straightness(line)),
                    rounded4(std::abs(std::sin(line.first().heading))),
                    -line.last().distance);
        };
        return key(left) < key(right);
    });

    QVector<int> straightPaths;
    for(int i : members)
        if(straight(paths[i]))
            straightPaths.append(i);

    // Two straight paths whose centre lines cross: the first pair found.
    int crossA = -1, crossB = -1;
    float crossX = 0, crossZ = 0;
    for(int ai = 0; ai < straightPaths.size() && crossA < 0; ai++)
        for(int bi = ai + 1; bi < straightPaths.size() && crossA < 0; bi++){
            const int a = straightPaths[ai];
            const int b = straightPaths[bi];
            if(std::abs(paths[a].first().heading - paths[b].first().heading) < 1e-4f)
                continue;
            if(crossing(paths[a], paths[b], crossX, crossZ)){
                crossA = a;
                crossB = b;
            }
        }

    if(crossA >= 0){
        const float ha = paths[crossA].first().heading;
        // The undirected difference, -90 to 90 degrees.
        float d = paths[crossB].first().heading - ha + Pi / 2;
        d -= Pi * std::floor(d / Pi);
        d -= Pi / 2;
        const float angle = std::abs(d) * 180.0f / Pi;
        reference.crossingAngle = angle;
        // A thousandth of a degree over: a 60 degree diamond stays shared
        // at 60 (decision 9) whatever its float rounding.
        if(angle > maxSharedAngle + 0.001f){
            reference.type = TrackShapeAnalysis::Reference::Type::None;
            reference.path = order.first();
            reference.line = paths[order.first()];
            return reference;
        }
        const float heading = ha + d / 2;
        const float sinH = std::sin(heading);
        const float cosH = std::cos(heading);
        float s0 = std::numeric_limits<float>::max();
        float s1 = -std::numeric_limits<float>::max();
        for(int i : members)
            for(const Point &point : paths[i]){
                const float s = (point.x - crossX) * sinH + (point.z - crossZ) * cosH;
                s0 = std::min(s0, s);
                s1 = std::max(s1, s);
            }
        s0 -= 1;
        s1 += 1;
        const int steps = int((s1 - s0) / TrackShapeAnalysis::SampleStep);
        reference.type = TrackShapeAnalysis::Reference::Type::Bisector;
        reference.path = crossA;
        reference.otherPath = crossB;
        reference.line.reserve(steps + 1);
        for(int j = 0; j <= steps; j++){
            const float s = s0 + j * TrackShapeAnalysis::SampleStep;
            Point point;
            point.x = crossX + s * sinH;
            point.z = crossZ + s * cosH;
            point.heading = heading;
            point.distance = j * TrackShapeAnalysis::SampleStep;
            reference.line.append(point);
        }
        return reference;
    }

    if(straightPaths.isEmpty()){
        // The mean of the two least curved, from their shared start.
        QVector<int> byStraightness = members;
        std::stable_sort(byStraightness.begin(), byStraightness.end(), [&](int left, int right) {
            return straightness(paths[left]) < straightness(paths[right]);
        });
        const Line &a = paths[byStraightness[0]];
        const Line &b = paths[byStraightness[1]];
        const float length = std::min(a.last().distance, b.last().distance);
        reference.type = TrackShapeAnalysis::Reference::Type::Mean;
        reference.path = byStraightness[0];
        reference.otherPath = byStraightness[1];
        for(int j = 0; j * TrackShapeAnalysis::SampleStep <= length + 1e-6f; j++){
            const float s = j * TrackShapeAnalysis::SampleStep;
            const Point pa = TrackShapeAnalysis::at(a, s);
            const Point pb = TrackShapeAnalysis::at(b, s);
            Point point;
            point.x = (pa.x + pb.x) / 2;
            point.z = (pa.z + pb.z) / 2;
            point.heading = (pa.heading + pb.heading) / 2;
            point.distance = s;
            reference.line.append(point);
        }
        return reference;
    }

    reference.type = TrackShapeAnalysis::Reference::Type::Path;
    reference.path = order.first();
    reference.line = paths[order.first()];
    return reference;
}

}

QVector<TrackShapeAnalysis::Point> TrackShapeAnalysis::samplePath(
        const TrackShape &shape, int path,
        const std::unordered_map<int, TSection*> &sections, float step) {
    Line line;
    if(path < 0 || path >= shape.numpaths || shape.path == nullptr || step <= 0)
        return line;
    const TrackShape::SectionIdx &sectionIdx = shape.path[path];
    Point point;
    point.x = sectionIdx.pos[0];
    point.z = sectionIdx.pos[2];
    point.heading = sectionIdx.rotDeg * Pi / 180.0f;
    line.append(point);
    // Doubles along the way, so long paths keep their millimetres.
    double x = point.x, z = point.z, heading = point.heading, distance = 0;
    for(int k = 0; k < sectionIdx.n && k < TrackShape::MaxSectionIdxCount; k++){
        const auto found = sections.find(int(sectionIdx.sect[k]));
        if(found == sections.end() || found->second == nullptr)
            return Line();
        const TSection &section = *found->second;
        double turn = 0, length = section.size;
        if(section.type == 1){
            // Radians, positive to the right, as the file's degrees.
            turn = section.angle;
            length = std::abs(section.angle) * section.radius;
        }
        const int n = std::max(1, int(std::ceil(length / step)));
        const double segment = length / n;
        const double dTurn = turn / n;
        for(int i = 0; i < n; i++){
            const double middle = heading + dTurn / 2;
            x += segment * std::sin(middle);
            z += segment * std::cos(middle);
            heading += dTurn;
            distance += segment;
            line.append(Point{float(x), float(z), float(heading), float(distance)});
        }
    }
    return line;
}

TrackShapeAnalysis::Point TrackShapeAnalysis::at(const QVector<Point> &line, float distance) {
    if(line.isEmpty())
        return Point();
    if(distance <= 0)
        return line.first();
    for(int i = 1; i < line.size(); i++){
        if(line[i].distance >= distance){
            const Point &a = line[i - 1];
            const Point &b = line[i];
            const float f = (distance - a.distance) / std::max(1e-9f, b.distance - a.distance);
            Point point;
            point.x = a.x + f * (b.x - a.x);
            point.z = a.z + f * (b.z - a.z);
            point.heading = a.heading + f * (b.heading - a.heading);
            point.distance = distance;
            return point;
        }
    }
    return line.last();
}

TrackShapeAnalysis TrackShapeAnalysis::analyse(const TrackShape &shape,
        const std::unordered_map<int, TSection*> &sections,
        float groupDistance, float maxSharedAngle) {
    TrackShapeAnalysis analysis;
    if(shape.tunnelshape)
        analysis.kinds |= Tunnel;
    if(shape.roadshape)
        analysis.kinds |= Road;
    analysis.valid = true;
    for(int i = 0; i < shape.numpaths; i++){
        Line line = samplePath(shape, i, sections);
        if(line.isEmpty()){
            analysis.valid = false;
            analysis.paths.clear();
            return analysis;
        }
        analysis.paths.append(line);
    }
    const int n = analysis.paths.size();
    if(n == 0)
        return analysis;
    if(n == 1){
        analysis.kinds |= Single;
        analysis.independent.append(0);
        return analysis;
    }

    QVector<Box> boxes;
    for(const Line &line : analysis.paths)
        boxes.append(boxOf(line));
    QVector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int i) {
        while(parent[i] != i)
            i = parent[i];
        return i;
    };
    for(int a = 0; a < n; a++)
        for(int b = a + 1; b < n; b++){
            if(find(a) == find(b))
                continue;
            if(closer(analysis.paths[a], boxes[a], analysis.paths[b], boxes[b], groupDistance))
                parent[find(a)] = find(b);
        }

    QVector<int> roots;
    QVector<QVector<int>> members;
    for(int i = 0; i < n; i++){
        const int root = find(i);
        int index = roots.indexOf(root);
        if(index < 0){
            roots.append(root);
            members.append(QVector<int>());
            index = roots.size() - 1;
        }
        members[index].append(i);
    }
    for(const QVector<int> &group : members){
        if(group.size() == 1){
            analysis.independent.append(group.first());
            continue;
        }
        Group complexGroup;
        complexGroup.paths = group;
        complexGroup.reference = referenceOf(analysis.paths, group, maxSharedAngle);
        analysis.groups.append(complexGroup);
    }
    std::sort(analysis.independent.begin(), analysis.independent.end());
    analysis.kinds |= analysis.groups.isEmpty() ? Parallel : Complex;
    return analysis;
}

QString TrackShapeAnalysis::kindNames(int kinds) {
    QStringList names;
    if(kinds & Single)
        names.append("Single");
    if(kinds & Parallel)
        names.append("Parallel");
    if(kinds & Complex)
        names.append("Complex");
    if(kinds & Tunnel)
        names.append("Tunnel");
    if(kinds & Road)
        names.append("Road");
    return names.join(", ");
}

int TrackShapeAnalysis::kindFromName(const QString &name) {
    const QString value = name.trimmed();
    if(value.compare("Single", Qt::CaseInsensitive) == 0)
        return Single;
    if(value.compare("Parallel", Qt::CaseInsensitive) == 0)
        return Parallel;
    if(value.compare("Complex", Qt::CaseInsensitive) == 0)
        return Complex;
    if(value.compare("Tunnel", Qt::CaseInsensitive) == 0)
        return Tunnel;
    if(value.compare("Road", Qt::CaseInsensitive) == 0)
        return Road;
    return 0;
}
