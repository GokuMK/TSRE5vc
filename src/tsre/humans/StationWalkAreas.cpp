/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "StationWalkAreas.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>
#include <QThreadPool>
#include <QDebug>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/Game.h>
#include <tsre/humans/ShapeTriangles.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/world/Route.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/world/Tile.h>
#include <tsre/world/objects/PlatformObj.h>
#include <tsre/world/objects/WorldObj.h>

namespace Humans {

namespace {

// Around each platform's track: the corridors' reach (m), across and past
// its ends; terrain sampled every TerrainStep m; track sampled within.
constexpr double CorridorReach = 16.0;
constexpr double TerrainStep = 2.0;
constexpr double PlatformStep = 2.0;

// A role from its written name (any case); false for none of them.
bool roleNamed(const QString &name, WalkArea::Role &role, bool &ignored) {
    const QString n = name.trimmed().toLower();
    ignored = n == "ignore" || n == "ignored";
    if (n == "floor")
        role = WalkArea::Role::Floor;
    else if (n == "solid")
        role = WalkArea::Role::Solid;
    else if (n == "ground")
        role = WalkArea::Role::Ground;
    else if (!ignored)
        return false;
    return true;
}

// An object type's role; false: not in walk areas.
bool roleOfType(int type, WalkArea::Role &role) {
    switch (type) {
    case WorldObj::sstatic:
    case WorldObj::collideobject:
        role = WalkArea::Role::Floor;
        return true;
    case WorldObj::signal:
    case WorldObj::speedpost:
    case WorldObj::gantry:
    case WorldObj::levelcr:
    case WorldObj::pickup:
    case WorldObj::hazard:
        role = WalkArea::Role::Solid;
        return true;
    default:
        return false;
    }
}

// The editor's tile of a global coordinate and the place in it.
int tileOf(double global, float &local) {
    const int tile = int(std::floor((global + 1024.0) / 2048.0));
    local = float(global - double(tile) * 2048.0);
    return tile;
}

// Whether the route's world tile is there to read (loaded, or no file).
bool tileReady(Route *route, int x, int z) {
    Tile *tile = route->tile.value(x * 10000 + z, nullptr);
    return tile != nullptr && (tile->loaded == 1 || tile->loaded == -2);
}

// Where problems point: a global editor place as the error list takes it
// (MSTS's: z north).
void locate(ErrorMessage *message, double x, double y, double z) {
    float lx = 0.0f, lz = 0.0f;
    const int tx = tileOf(x, lx);
    const int tz = tileOf(-z, lz);
    message->setLocationXYZ(float(tx), float(tz), lx, float(y), lz);
}

}

// A station's input, gathered on the main thread; the objects' triangles
// read and the walk area built on a worker.
struct StationWalkAreas::Job {
    WalkArea::Input input;
    struct Object {
        QString path, texPath;
        float matrix[16];
        WalkArea::Role role = WalkArea::Role::Floor;
        // The object's own role (TSREWalk): over its shape's.
        bool fixed = false;
    };
    std::vector<Object> objects;
    // Platform names, as in the input, and their platforms (indices of
    // the platforms' list); the station's.
    QStringList names;
    std::vector<int> platformOf;
    QString station;
    std::atomic<bool> done{false};
    WalkArea::Built built;
    int triangles = 0;
    int objectsUsed = 0;
    double buildMs = 0.0;
};

// The overlay of a built area: its triangles by kind, its seeds.
struct StationWalkAreas::Overlay {
    std::unique_ptr<OglObj> floor, edge, strip, dropped, seedsFloor, seedsGround, seedsMissed;
};

namespace {

void runJob(StationWalkAreas::Job &job);

}

StationWalkAreas &StationWalkAreas::instance() {
    static StationWalkAreas areas;
    return areas;
}

bool StationWalkAreas::busy() const {
    for (const Entry &e : entries)
        if (e.job && !e.job->done.load(std::memory_order_acquire))
            return true;
    return false;
}

void StationWalkAreas::clear() {
    list.clear();
    entries.clear();
    platforms.clear();
    routeRead = nullptr;
    itemsRead = -1;
}

void StationWalkAreas::readStations() {
    TDB *tdb = Game::trackDB;
    if (tdb == nullptr || Game::currentRoute == nullptr) {
        clear();
        return;
    }
    if (routeRead == Game::currentRoute && itemsRead == tdb->iTRitems)
        return;
    // A build running keeps its job; its result is dropped.
    clear();
    routeRead = Game::currentRoute;
    itemsRead = tdb->iTRitems;
    platforms = StationPlatforms::read(*tdb);
    QHash<QString, int> byName;
    for (int p = 0; p < int(platforms.size()); ++p) {
        const QString name = platforms[size_t(p)].station;
        const QString key = name.isEmpty() ? QString("#%1").arg(platforms[size_t(p)].start) : name.toLower();
        int s = byName.value(key, -1);
        if (s < 0) {
            s = int(list.size());
            byName.insert(key, s);
            Station station;
            station.name = name.isEmpty() ? QString("(%1)").arg(platforms[size_t(p)].name) : name;
            list.push_back(station);
            entries.push_back(Entry());
        }
        entries[size_t(s)].platforms.push_back(p);
    }
    for (size_t s = 0; s < list.size(); ++s) {
        double x = 0.0, z = 0.0;
        int n = 0;
        for (int p : entries[s].platforms) {
            const StationPlatforms::Platform &platform = platforms[size_t(p)];
            x += platform.ends[0] + platform.ends[3];
            z += -(platform.ends[2] + platform.ends[5]);
            n += 2;
        }
        list[s].middle[0] = n > 0 ? x / n : 0.0;
        list[s].middle[1] = n > 0 ? z / n : 0.0;
    }
}

bool StationWalkAreas::gather(int s, Job &job) {
    QElapsedTimer timer;
    timer.start();
    TDB *tdb = Game::trackDB;
    Route *route = Game::currentRoute;
    Station &station = list[size_t(s)];
    station.problems.clear();

    // The platforms' tracks, every 2 m (global editor coordinates: x, the
    // rail's top, z south), and the box around them.
    struct Line {
        std::vector<double> points;
        int platform;
    };
    std::vector<Line> lines;
    double box[4] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                     -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
    double railSum = 0.0;
    int railCount = 0;
    for (int p : entries[size_t(s)].platforms) {
        StationPlatforms::Platform &platform = platforms[size_t(p)];
        if (!platform.sampled)
            StationPlatforms::sample(*tdb, platform, PlatformStep);
        Line line;
        line.platform = p;
        for (size_t i = 0; i + 2 < platform.points.size(); i += 3) {
            const double x = platform.points[i], y = platform.points[i + 1] + RailTop, z = -platform.points[i + 2];
            line.points.insert(line.points.end(), {x, y, z});
            box[0] = std::min(box[0], x - CorridorReach);
            box[1] = std::min(box[1], z - CorridorReach);
            box[2] = std::max(box[2], x + CorridorReach);
            box[3] = std::max(box[3], z + CorridorReach);
            railSum += y;
            ++railCount;
        }
        lines.push_back(line);
    }
    if (railCount == 0)
        return false;
    // Its tiles first.
    const int tx0 = int(std::floor((box[0] + 1024.0) / 2048.0)), tx1 = int(std::floor((box[2] + 1024.0) / 2048.0));
    const int tz0 = int(std::floor((box[1] + 1024.0) / 2048.0)), tz1 = int(std::floor((box[3] + 1024.0) / 2048.0));
    for (int tx = tx0; tx <= tx1; ++tx)
        for (int tz = tz0; tz <= tz1; ++tz)
            if (!tileReady(route, tx, tz))
                return false;

    job.station = station.name;
    station.origin[0] = 0.5 * (box[0] + box[2]);
    station.origin[1] = railSum / railCount;
    station.origin[2] = 0.5 * (box[1] + box[3]);
    const double *o = station.origin;
    job.input.limits[0] = float(box[0] - o[0]);
    job.input.limits[1] = float(box[1] - o[2]);
    job.input.limits[2] = float(box[2] - o[0]);
    job.input.limits[3] = float(box[3] - o[2]);

    // Each platform's sides, from its world object: left and right of the
    // way from its begin item to its end item.
    for (const Line &line : lines) {
        const StationPlatforms::Platform &platform = platforms[size_t(line.platform)];
        QVector<WorldObj *> found;
        route->fillWorldObjectsByTrackItemId(found, 0, platform.start);
        PlatformObj *object = nullptr;
        for (WorldObj *w : found)
            if (w != nullptr && (w->typeID == WorldObj::platform))
                object = static_cast<PlatformObj *>(w);
        const QString name = platform.name.isEmpty() ? QString::number(platform.start) : platform.name;
        if (object == nullptr) {
            station.problems << QString("Platform %1 of %2 has no platform object: no people there.")
                                .arg(name, station.name);
            continue;
        }
        const bool left = object->getSideLeft(), right = object->getSideRight();
        if (!left && !right) {
            station.problems << QString("Platform %1 of %2 has neither side set: no people there.")
                                .arg(name, station.name);
            continue;
        }
        // Its sides are as seen going from its first track item (flags 0)
        // to its second (flags 0xFFFF0000), as the simulation's doors take
        // them; the line goes the way of growing distance.
        bool forward = true;
        const auto a = tdb->trackItems.find(platform.start), b = tdb->trackItems.find(platform.end);
        if (a != tdb->trackItems.end() && b != tdb->trackItems.end() && a->second && b->second
                && a->second->platformTrItemData && b->second->platformTrItemData) {
            const bool aSecond = (a->second->platformTrItemData[0] & 0xFFFF0000u) != 0;
            TRitem *first = aSecond ? b->second : a->second, *second = aSecond ? a->second : b->second;
            forward = first->getTrackPosition() <= second->getTrackPosition();
        }
        for (int sideBit = 0; sideBit < 2; ++sideBit) {
            if ((sideBit == 0 && !left) || (sideBit == 1 && !right))
                continue;
            // Left is +1 of WalkArea's side in the editor's frame (17 of
            // EUROPE1's 18 stations; the 18th has its sides set wrong); the
            // line going the other way swaps it.
            float side = sideBit == 0 ? 1.0f : -1.0f;
            if (!forward)
                side = -side;
            WalkArea::Platform walk;
            walk.side = side;
            walk.name = name;
            for (size_t i = 0; i + 2 < line.points.size(); i += 3) {
                walk.line.push_back(float(line.points[i] - o[0]));
                walk.line.push_back(float(line.points[i + 1] - o[1]));
                walk.line.push_back(float(line.points[i + 2] - o[2]));
            }
            job.input.platforms.push_back(walk);
            job.names << name;
            job.platformOf.push_back(line.platform);
        }
    }

    // The tracks within the box: every vector node with a section near it.
    for (int id = 1; id <= tdb->iTRnodes; ++id) {
        const auto found = tdb->trackNodes.find(id);
        if (found == tdb->trackNodes.end() || found->second == nullptr || found->second->typ != 1)
            continue;
        const TRnode *node = found->second;
        bool near = false;
        for (int v = 0; v < node->iTrv && !near; ++v) {
            const TrackVectorSection &section = node->trVectorSection[v];
            const double x = double(section.tileX) * 2048.0 + section.x;
            const double z = -(double(section.tileZ) * 2048.0 + section.z);
            near = x > box[0] - 600.0 && x < box[2] + 600.0 && z > box[1] - 600.0 && z < box[3] + 600.0;
        }
        if (!near)
            continue;
        float *buffer = nullptr;
        int length = 0;
        float lx = 0.0f, lz = 0.0f;
        const int tileX = tileOf(o[0], lx), tileZ = tileOf(o[2], lz);
        tdb->getVectorSectionLine(buffer, length, tileX, tileZ, id);
        for (int i = 0; i + 11 < length; i += 12) {
            const double ax = double(tileX) * 2048.0 + buffer[i], az = double(tileZ) * 2048.0 + buffer[i + 2];
            const double bx = double(tileX) * 2048.0 + buffer[i + 6], bz = double(tileZ) * 2048.0 + buffer[i + 8];
            const bool inside = (ax > box[0] - 5.0 && ax < box[2] + 5.0 && az > box[1] - 5.0 && az < box[3] + 5.0)
                    || (bx > box[0] - 5.0 && bx < box[2] + 5.0 && bz > box[1] - 5.0 && bz < box[3] + 5.0);
            if (!inside)
                continue;
            job.input.tracks.push_back({float(ax - o[0]), float(double(buffer[i + 1]) + RailTop - o[1]), float(az - o[2]),
                                        float(bx - o[0]), float(double(buffer[i + 7]) + RailTop - o[1]), float(bz - o[2])});
        }
        delete[] buffer;
    }

    // The objects in the box and their roles.
    for (int tx = tx0; tx <= tx1; ++tx)
        for (int tz = tz0; tz <= tz1; ++tz) {
            Tile *tile = route->tile.value(tx * 10000 + tz, nullptr);
            if (tile == nullptr || tile->loaded != 1)
                continue;
            for (auto &pair : tile->obiekty) {
                WorldObj *object = pair.second;
                WalkArea::Role role = WalkArea::Role::Floor;
                if (object == nullptr || !object->loaded || object->shape < 0 || !roleOfType(object->typeID, role))
                    continue;
                const auto shape = Game::currentShapeLib->shape.find(object->shape);
                if (shape == Game::currentShapeLib->shape.end() || shape->second == nullptr)
                    continue;
                const double x = double(object->x) * 2048.0 + object->position[0];
                const double z = double(object->y) * 2048.0 + object->position[2];
                const double radius = std::max(double(shape->second->getSize()) * 0.5, 10.0);
                if (x < box[0] - radius || x > box[2] + radius || z < box[1] - radius || z > box[3] + radius)
                    continue;
                Job::Object out;
                out.path = shape->second->getPathId();
                out.texPath = shape->second->getTexPath();
                out.role = role;
                // Its own role (TSREWalk) over its type's and its shape's.
                if (object->tsreWalk == 4)
                    continue;
                if (object->tsreWalk > 0 && object->tsreWalk < 4) {
                    out.role = object->tsreWalk == 1 ? WalkArea::Role::Floor
                            : object->tsreWalk == 2 ? WalkArea::Role::Solid : WalkArea::Role::Ground;
                    out.fixed = true;
                }
                float place[16];
                Mat4::identity(place);
                Mat4::translate(place, place, float(double(object->x) * 2048.0 - o[0]), float(-o[1]),
                                float(double(object->y) * 2048.0 - o[2]));
                Mat4::multiply(out.matrix, place, object->matrix);
                job.objects.push_back(out);
            }
        }

    // The terrain, as ground: heights every 2 m.
    const int columns = int(std::ceil((box[2] - box[0]) / TerrainStep)) + 1;
    const int rows = int(std::ceil((box[3] - box[1]) / TerrainStep)) + 1;
    std::vector<float> heights(size_t(columns) * size_t(rows), std::numeric_limits<float>::quiet_NaN());
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < columns; ++c) {
            const double x = box[0] + c * TerrainStep, z = box[1] + r * TerrainStep;
            float lx = 0.0f, lz = 0.0f;
            const int tx = tileOf(x, lx), tz = tileOf(z, lz);
            float h = 0.0f;
            if (Game::terrainLib != nullptr && Game::terrainLib->tryGetHeight(tx, tz, lx, lz, h))
                heights[size_t(r) * size_t(columns) + size_t(c)] = h;
        }
    std::vector<int> index(heights.size(), -1);
    for (size_t i = 0; i < heights.size(); ++i) {
        if (std::isnan(heights[i]))
            continue;
        index[i] = int(job.input.vertices.size() / 3);
        const int r = int(i) / columns, c = int(i) % columns;
        job.input.vertices.push_back(float(box[0] + c * TerrainStep - o[0]));
        job.input.vertices.push_back(float(double(heights[i]) - o[1]));
        job.input.vertices.push_back(float(box[1] + r * TerrainStep - o[2]));
    }
    for (int r = 0; r + 1 < rows; ++r)
        for (int c = 0; c + 1 < columns; ++c) {
            const int a = index[size_t(r) * size_t(columns) + size_t(c)], b = index[size_t(r) * size_t(columns) + size_t(c + 1)];
            const int d = index[size_t(r + 1) * size_t(columns) + size_t(c)], e = index[size_t(r + 1) * size_t(columns) + size_t(c + 1)];
            if (a < 0 || b < 0 || d < 0 || e < 0)
                continue;
            for (int v : {a, d, b, b, d, e})
                job.input.triangles.push_back(v);
            job.input.roles.push_back(WalkArea::Role::Ground);
            job.input.roles.push_back(WalkArea::Role::Ground);
        }
    station.gatherMs = double(timer.nsecsElapsed()) / 1e6;
    return true;
}

namespace {

void runJob(StationWalkAreas::Job &job) {
    QElapsedTimer timer;
    timer.start();
    WalkArea::Input &in = job.input;
    const float *limits = in.limits;
    for (const StationWalkAreas::Job::Object &object : job.objects) {
        const std::shared_ptr<const ShapeTriangles> shape = shapeTriangles(object.path, object.texPath);
        WalkArea::Role role = object.role;
        bool ignored = false;
        WalkArea::Role named = role;
        if (!object.fixed && !shape->walk.isEmpty() && roleNamed(shape->walk, named, ignored))
            role = named;
        if (ignored || shape->triangles.empty())
            continue;
        ++job.objectsUsed;
        const float *m = object.matrix;
        const std::vector<float> &t = shape->triangles;
        for (size_t i = 0; i + 8 < t.size(); i += 9) {
            float v[9];
            for (int k = 0; k < 3; ++k) {
                const float *p = &t[i + size_t(k) * 3];
                for (int r = 0; r < 3; ++r)
                    v[k * 3 + r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
            }
            // Wholly outside the area: left out.
            const float minX = std::min({v[0], v[3], v[6]}), maxX = std::max({v[0], v[3], v[6]});
            const float minZ = std::min({v[2], v[5], v[8]}), maxZ = std::max({v[2], v[5], v[8]});
            if (maxX < limits[0] || minX > limits[2] || maxZ < limits[1] || minZ > limits[3])
                continue;
            const int base = int(in.vertices.size() / 3);
            in.vertices.insert(in.vertices.end(), v, v + 9);
            in.triangles.insert(in.triangles.end(), {base, base + 1, base + 2});
            in.roles.push_back(role);
        }
    }
    job.triangles = int(in.roles.size());
    job.built = WalkArea::build(in);
    job.buildMs = double(timer.nsecsElapsed()) / 1e6;

    // TSRE_WALK_DUMP (a folder, read once): each build's input and result as
    // an OBJ file, groups by role, for looking at a station's walk area.
    static const QString dump = qEnvironmentVariable("TSRE_WALK_DUMP");
    if (dump.isEmpty())
        return;
    QDir().mkpath(dump);
    QFile file(QDir(dump).filePath(QString(job.station).replace(QRegularExpression("[^A-Za-z0-9_-]"), "_") + ".obj"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    QTextStream out(&file);
    for (size_t i = 0; i + 2 < in.vertices.size(); i += 3)
        out << "v " << in.vertices[i] << ' ' << in.vertices[i + 1] << ' ' << in.vertices[i + 2] << '\n';
    int vertices = int(in.vertices.size() / 3);
    const char *names[] = {"floor", "solid", "ground"};
    for (int role = 0; role < 3; ++role) {
        out << "g " << names[role] << '\n';
        for (size_t t = 0; t < in.roles.size(); ++t)
            if (int(in.roles[t]) == role)
                out << "f " << in.triangles[t * 3] + 1 << ' ' << in.triangles[t * 3 + 1] + 1 << ' ' << in.triangles[t * 3 + 2] + 1 << '\n';
    }
    auto polyline = [&](const std::vector<float> &line, const QString &group) {
        out << "g " << group << '\n';
        for (size_t i = 0; i + 2 < line.size(); i += 3)
            out << "v " << line[i] << ' ' << line[i + 1] << ' ' << line[i + 2] << '\n';
        out << 'l';
        for (size_t i = 0; i + 2 < line.size(); i += 3)
            out << ' ' << ++vertices;
        out << '\n';
    };
    for (const std::vector<float> &track : in.tracks)
        polyline(track, "track");
    for (size_t p = 0; p < in.platforms.size(); ++p)
        polyline(in.platforms[p].line, QString("platform_%1_side_%2").arg(p).arg(in.platforms[p].side));
    for (const WalkArea::Seed &seed : job.built.seeds) {
        out << "g seed_" << int(seed.kind) << '\n' << "v " << seed.position[0] << ' ' << seed.position[1] << ' '
            << seed.position[2] << '\n' << "p " << ++vertices << '\n';
    }
    for (size_t t = 0; t < job.built.areas.size(); ++t) {
        out << "g mesh_" << int(job.built.areas[t]) << '\n';
        for (int k = 0; k < 3; ++k)
            out << "v " << job.built.triangles[t * 9 + size_t(k) * 3] << ' ' << job.built.triangles[t * 9 + size_t(k) * 3 + 1]
                << ' ' << job.built.triangles[t * 9 + size_t(k) * 3 + 2] << '\n';
        out << "f " << vertices + 1 << ' ' << vertices + 2 << ' ' << vertices + 3 << '\n';
        vertices += 3;
    }
}

std::unique_ptr<OglObj> overlayOf(const std::vector<float> &points, int glType, float r, float g, float b, float a) {
    if (points.empty())
        return nullptr;
    auto object = std::make_unique<OglObj>();
    object->setMaterial(r, g, b, a);
    std::vector<float> copy = points;
    object->init(copy.data(), int(copy.size()), RenderItem::V, glType);
    return object;
}

}

void StationWalkAreas::finish(int s) {
    Station &station = list[size_t(s)];
    Entry &entry = entries[size_t(s)];
    std::shared_ptr<Job> job = std::move(entry.job);
    station.objects = job->objectsUsed;
    station.triangles = job->triangles;
    station.buildMs = job->buildMs;
    auto built = std::make_shared<WalkArea::Built>(std::move(job->built));
    station.built = built;
    station.state = built->ok ? Station::State::Built : Station::State::Failed;
    station.sides.clear();
    for (size_t p = 0; p < job->input.platforms.size() && p < job->platformOf.size(); ++p) {
        Station::Side side;
        side.platform = job->platformOf[p];
        side.start = platforms[size_t(side.platform)].start;
        side.end = platforms[size_t(side.platform)].end;
        const bool swapped = p < built->platforms.size() && built->platforms[p].swapped;
        side.side = swapped ? -job->input.platforms[p].side : job->input.platforms[p].side;
        side.line = job->input.platforms[p].line;
        station.sides.push_back(std::move(side));
    }
    if (!built->ok || built->reached == 0)
        station.problems << QString("Station %1 has no walk area (%2).").arg(station.name,
                                                                             built->ok ? "no seed reached floor" : built->error);
    for (size_t p = 0; p < built->platforms.size() && p < size_t(job->names.size()); ++p) {
        const WalkArea::Built::PlatformSeeds &seeds = built->platforms[p];
        if (seeds.swapped)
            station.problems << QString("Platform %1 of %2 has its side set where there is no platform; the other "
                                        "side has one and is used.").arg(job->names[int(p)], station.name);
        if (seeds.floor == 0 && seeds.ground > 0)
            station.problems << QString("Platform %1 of %2 found no platform floor: people stand on a ground strip.")
                                .arg(job->names[int(p)], station.name);
        else if (seeds.floor == 0 && seeds.ground == 0)
            station.problems << QString("Platform %1 of %2 found neither floor nor ground beside its track.")
                                .arg(job->names[int(p)], station.name);
    }
    for (const QString &problem : station.problems) {
        qInfo().noquote() << "Walk area problem:" << problem;
        auto *message = new ErrorMessage(ErrorMessage::Type_Warning, ErrorMessage::Source_World, problem,
                                         "Walk areas (task humans 04)");
        locate(message, station.middle[0], station.origin[1], station.middle[1]);
        ErrorMessagesLib::PushErrorMessage(message);
    }
    for (size_t p = 0; p < built->platforms.size() && p < size_t(job->names.size()); ++p)
        qInfo().noquote() << QString("Walk area seeds: %1 %2: floor %3, ground %4, missed %5 (edges %6, other side %7%8)")
                             .arg(station.name).arg(job->names[int(p)]).arg(built->platforms[p].floor)
                             .arg(built->platforms[p].ground).arg(built->platforms[p].missed)
                             .arg(built->platforms[p].edges).arg(built->platforms[p].otherEdges)
                             .arg(built->platforms[p].swapped ? ", swapped" : "");
    qInfo().noquote() << QString("Walk area %1: %2 platforms, %3 objects, %4 triangles, %5 polygons (%6 reached); "
                                 "gather %7 ms, build %8 ms%9")
                         .arg(station.name).arg(job->names.size()).arg(station.objects).arg(station.triangles)
                         .arg(built->polygons).arg(built->reached).arg(station.gatherMs, 0, 'f', 1)
                         .arg(station.buildMs, 0, 'f', 1).arg(built->ok ? QString() : ": " + built->error);
    // The overlay: a little above, so it shows on the surfaces.
    auto overlay = std::make_shared<Overlay>();
    std::vector<float> floor, edge, strip, dropped;
    for (size_t t = 0; t < built->areas.size(); ++t) {
        const unsigned char area = built->areas[t];
        std::vector<float> &to = (area & WalkArea::DroppedFlag) ? dropped
                : area == WalkArea::AreaEdge ? edge : area == WalkArea::AreaStrip ? strip : floor;
        for (int k = 0; k < 9; ++k)
            to.push_back(built->triangles[t * 9 + size_t(k)] + (k % 3 == 1 ? 0.06f : 0.0f));
    }
    std::vector<float> seeds[3];
    for (const WalkArea::Seed &seed : built->seeds) {
        std::vector<float> &to = seeds[seed.kind];
        to.insert(to.end(), {seed.position[0], seed.position[1], seed.position[2], seed.position[0],
                             seed.position[1] + 1.2f, seed.position[2]});
    }
    overlay->floor = overlayOf(floor, 4 /* GL_TRIANGLES */, 0.1f, 0.85f, 0.2f, 0.45f);
    overlay->edge = overlayOf(edge, 4, 1.0f, 0.85f, 0.1f, 0.55f);
    overlay->strip = overlayOf(strip, 4, 1.0f, 0.5f, 0.1f, 0.5f);
    overlay->dropped = overlayOf(dropped, 4, 0.5f, 0.5f, 0.55f, 0.35f);
    overlay->seedsMissed = overlayOf(seeds[WalkArea::Seed::Missed], 1 /* GL_LINES */, 1.0f, 0.1f, 0.1f, 1.0f);
    overlay->seedsFloor = overlayOf(seeds[WalkArea::Seed::OnFloor], 1, 0.1f, 1.0f, 0.3f, 1.0f);
    overlay->seedsGround = overlayOf(seeds[WalkArea::Seed::OnGround], 1, 1.0f, 0.6f, 0.1f, 1.0f);
    entry.overlay = overlay;
}

void StationWalkAreas::frame(const float *playerT, const float *playerW, float reach) {
    readStations();
    for (size_t s = 0; s < entries.size(); ++s)
        if (entries[s].job && entries[s].job->done.load(std::memory_order_acquire))
            finish(int(s));
    if (busy())
        return;
    const double cx = double(playerT[0]) * 2048.0 + playerW[0], cz = double(playerT[1]) * 2048.0 + playerW[2];
    // The nearest station waiting within reach, its tiles loaded.
    std::vector<std::pair<double, int>> waiting;
    for (size_t s = 0; s < list.size(); ++s) {
        if (list[s].state != Station::State::Waiting)
            continue;
        const double d = std::hypot(list[s].middle[0] - cx, list[s].middle[1] - cz);
        if (d <= reach)
            waiting.push_back({d, int(s)});
    }
    std::sort(waiting.begin(), waiting.end());
    for (const auto &w : waiting) {
        auto job = std::make_shared<Job>();
        if (!gather(w.second, *job))
            continue;
        list[size_t(w.second)].state = Station::State::Building;
        entries[size_t(w.second)].job = job;
        QThreadPool::globalInstance()->start([job]() {
            runJob(*job);
            job->done.store(true, std::memory_order_release);
        });
        return;
    }
}

bool StationWalkAreas::buildNow(const QString &name) {
    readStations();
    for (size_t s = 0; s < list.size(); ++s) {
        if (list[s].name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        auto job = std::make_shared<Job>();
        if (!gather(int(s), *job))
            return false;
        runJob(*job);
        job->done = true;
        entries[s].job = job;
        finish(int(s));
        return true;
    }
    return false;
}

void StationWalkAreas::pushOverlay(RenderQueue &queue, const float *playerT, const float *playerW, float reach) {
    const double cx = double(playerT[0]) * 2048.0 + playerW[0], cz = double(playerT[1]) * 2048.0 + playerW[2];
    for (size_t s = 0; s < list.size(); ++s) {
        const Overlay *overlay = entries[s].overlay.get();
        if (overlay == nullptr || std::hypot(list[s].middle[0] - cx, list[s].middle[1] - cz) > reach)
            continue;
        const double *o = list[s].origin;
        queue.pushTransform();
        Mat4::translate(queue.transform(), queue.transform(), float(o[0] - double(playerT[0]) * 2048.0), float(o[1]),
                        float(o[2] - double(playerT[1]) * 2048.0));
        for (const std::unique_ptr<OglObj> *part : {&overlay->floor, &overlay->edge, &overlay->strip, &overlay->dropped,
                                                    &overlay->seedsFloor, &overlay->seedsGround, &overlay->seedsMissed})
            if (*part)
                (*part)->pushRenderItem(queue);
        queue.popTransform();
    }
}

}
