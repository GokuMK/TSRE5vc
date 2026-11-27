/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#include "RouteCollision.h"

#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/VehicleDefinition.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/world/Route.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/world/Tile.h>
#include <tsre/world/Trk.h>
#include <tsre/world/objects/WorldObj.h>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Characters {

namespace {

// The editor's tile of a global coordinate and the place in it.
int tileOf(double global, float &local) {
    const int tile = int(std::floor((global + 1024.0) / 2048.0));
    local = float(global - double(tile) * 2048.0);
    return tile;
}

// Whether a world object's type collides (as the walk areas take them).
bool collides(int type) {
    switch (type) {
    case WorldObj::sstatic:
    case WorldObj::collideobject:
    case WorldObj::signal:
    case WorldObj::speedpost:
    case WorldObj::gantry:
    case WorldObj::levelcr:
    case WorldObj::pickup:
    case WorldObj::hazard:
        return true;
    default:
        return false;
    }
}

float segmentDistance(float px, float pz, const float *s) {
    const float dx = s[2] - s[0], dz = s[3] - s[1];
    const float length2 = dx * dx + dz * dz;
    float t = length2 > 0.0f ? ((px - s[0]) * dx + (pz - s[1]) * dz) / length2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return std::hypot(px - (s[0] + dx * t), pz - (s[1] + dz * t));
}

}

float RouteCollision::reachOf(const float *bound, bool loaded) {
    if (!loaded)
        return UnknownReach;
    const float x = std::max(std::fabs(bound[0]), std::fabs(bound[1]));
    const float y = std::max(std::fabs(bound[2]), std::fabs(bound[3]));
    const float z = std::max(std::fabs(bound[4]), std::fabs(bound[5]));
    return std::max(std::sqrt(x * x + y * y + z * z), 10.0f);
}

RouteCollision::RouteCollision() = default;

RouteCollision::~RouteCollision() = default;

float RouteCollision::reach() const {
    if (Game::currentRoute != nullptr && Game::currentRoute->trk != nullptr && Game::currentRoute->trk->tsreCharacterReach >= 0.0f)
        return Game::currentRoute->trk->tsreCharacterReach;
    return DefaultReach;
}

void RouteCollision::clear() {
    collisions.clear();
    carPlaced.clear();
    based = false;
    tracksRead = false;
    tracks.clear();
    jobs.clear();
    read.clear();
    figures = Stats();
}

void RouteCollision::rebase(const int *tile) {
    base[0] = tile[0];
    base[1] = tile[1];
    based = true;
    collisions.clear();
    carPlaced.clear();
    tracksRead = false;
    collisions.setTerrain([this](float x, float z, float *height) {
        if (Game::terrainLib == nullptr)
            return false;
        float lx = 0.0f, lz = 0.0f;
        const int tx = tileOf(double(base[0]) * 2048.0 + x, lx), tz = tileOf(double(base[1]) * 2048.0 + z, lz);
        return Game::terrainLib->tryGetHeight(tx, tz, lx, lz, *height);
    });
}

void RouteCollision::update(const int *tile, const float *position) {
    bool again = false;
    if (!based || tile[0] != base[0] || tile[1] != base[1]) {
        rebase(tile);
        again = true;
    }
    // Shapes read on workers since.
    for (auto it = jobs.begin(); it != jobs.end();) {
        if (it.value()->done.load(std::memory_order_acquire)) {
            read.insert(it.key(), true);
            it = jobs.erase(it);
            again = true;
        } else {
            ++it;
        }
    }
    // Gathered again a second on while tiles or objects about it were not
    // loaded (a walk begun as the route loads, a character in a car since).
    if (figures.unready > 0 && WorldObj::clock() - gatheredWhen > 1000)
        again = true;
    if (again || std::hypot(position[0] - gatheredAt[0], position[2] - gatheredAt[2]) > Again
            || std::fabs(position[1] - gatheredAt[1]) > Again)
        gather(position);
    if (!tracksRead || std::hypot(position[0] - tracksAt[0], position[2] - tracksAt[1]) > 100.0f)
        readTracks(position);
    placeCars(position);
}

std::shared_ptr<const CollisionMesh> RouteCollision::meshFor(const QString &path, const QString &texPath, Collision mode,
                                                             float away) {
    const QString key = path + QLatin1Char('|') + texPath + QLatin1Char('|') + QString::number(int(mode));
    if (read.contains(key) || away <= Needed) {
        read.insert(key, true);
        return shapeCollision(path, texPath, mode);
    }
    if (!jobs.contains(key)) {
        auto job = std::make_shared<Job>();
        job->path = path;
        job->texPath = texPath;
        job->mode = mode;
        jobs.insert(key, job);
        QThreadPool::globalInstance()->start([job]() {
            job->mesh = shapeCollision(job->path, job->texPath, job->mode);
            job->done.store(true, std::memory_order_release);
        });
    }
    return nullptr;
}

const RouteCollision::PlacedCar *RouteCollision::carOf(int placedIndex) const {
    if (placedIndex < 0)
        return nullptr;
    for (const PlacedCar &placed : carPlaced)
        if (placed.index == placedIndex)
            return &placed;
    return nullptr;
}

void RouteCollision::placeCars(const float *position) {
    for (const PlacedCar &placed : carPlaced)
        collisions.remove(placed.index);
    carPlaced.clear();
    figures.cars = 0;
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const std::shared_ptr<const Sim::SimulationFrame> frame = simulation != nullptr ? simulation->viewFrame() : nullptr;
    if (!frame)
        return;
    const qint64 now = WorldObj::clock();
    for (int train = 0; train < int(frame->trains.size()); ++train) {
        Consist *consist = simulation->consistOf(train);
        if (consist == nullptr)
            continue;
        // A moving train's cars only as drawn within the last second (one
        // not drawn, behind the camera, would be where it was); a standing
        // one's as last drawn.
        const bool moving = std::fabs(frame->trains[size_t(train)].state.speed) > 0.05f;
        for (int v = 0; v < consist->engItems.size(); ++v) {
            const Eng *eng = consist->engItems[v].engPointer;
            float drawn[16];
            int tile[2] = {0, 0};
            qint64 when = 0;
            if (eng == nullptr || !eng->definition || !eng->lastDrawn(drawn, tile, &when) || (moving && now - when > 1000))
                continue;
            const float ox = float(double(tile[0] - base[0]) * 2048.0) + drawn[12];
            const float oz = float(double(tile[1] - base[1]) * 2048.0) + drawn[14];
            const float away = std::hypot(ox - position[0], oz - position[2]);
            if (away > 40.0f)
                continue;
            const QString path = eng->definition->shapePath;
            const auto mesh = meshFor(path, QFileInfo(path).path(), Collision::Default, away - 15.0f);
            if (!mesh)
                continue;
            float place[16], matrix[16];
            Mat4::identity(place);
            Mat4::translate(place, place, float(double(tile[0] - base[0]) * 2048.0), 0.0f,
                            float(double(tile[1] - base[1]) * 2048.0));
            Mat4::multiply(matrix, place, drawn);
            PlacedCar placed;
            placed.train = train;
            placed.car = v;
            placed.eng = eng;
            placed.index = collisions.add(mesh, matrix, -1);
            carPlaced.push_back(placed);
            ++figures.cars;
        }
    }
}

void RouteCollision::gather(const float *position) {
    QElapsedTimer timer;
    timer.start();
    std::copy(position, position + 3, gatheredAt);
    gatheredWhen = WorldObj::clock();
    collisions.clear();
    carPlaced.clear();
    figures.objects = figures.placed = figures.unready = 0;
    Route *route = Game::currentRoute;
    if (route == nullptr || Game::currentShapeLib == nullptr)
        return;
    const double gx = double(base[0]) * 2048.0 + position[0], gz = double(base[1]) * 2048.0 + position[2];
    float unused = 0.0f;
    // The tiles within Ahead, waited for (counted while they or their
    // objects are not loaded); and those about them, for a large shape whose
    // origin lies there but which reaches this far (Krakow Glowny's
    // platforms, their origin in the next tile: none under the middle of
    // them, the user's report 2026-11-26): only their loaded shapes, not
    // waited for.
    const int tx0 = tileOf(gx - double(Ahead), unused), tx1 = tileOf(gx + double(Ahead), unused);
    const int tz0 = tileOf(gz - double(Ahead), unused), tz1 = tileOf(gz + double(Ahead), unused);
    const double around = double(Ahead + LargestReach);
    const int ax0 = tileOf(gx - around, unused), ax1 = tileOf(gx + around, unused);
    const int az0 = tileOf(gz - around, unused), az1 = tileOf(gz + around, unused);
    for (int tx = ax0; tx <= ax1; ++tx)
        for (int tz = az0; tz <= az1; ++tz) {
            const bool within = tx >= tx0 && tx <= tx1 && tz >= tz0 && tz <= tz1;
            Tile *tile = route->tile.value(tx * 10000 + tz, nullptr);
            // Not made yet or not loaded (the first view of a run, the
            // camera just come): gathered again later. A tile the route
            // lacks is counted too: a gather a second near its edge.
            if (within && (tile == nullptr || tile->loaded != 1))
                ++figures.unready;
            if (tile == nullptr || tile->loaded != 1)
                continue;
            for (auto &pair : tile->obiekty) {
                WorldObj *object = pair.second;
                if (object == nullptr || !collides(object->typeID))
                    continue;
                if (!object->loaded || object->shape < 0) {
                    if (within)
                        ++figures.unready;
                    continue;
                }
                const float ox = float(double(object->x - base[0]) * 2048.0) + object->position[0];
                const float oz = float(double(object->y - base[1]) * 2048.0) + object->position[2];
                const float centre = std::hypot(ox - position[0], oz - position[2]);
                if (!within && centre > Ahead + LargestReach)
                    continue;
                const Collision mode = collisionOfObject(object->tsreCollision);
                if (mode == Collision::None)
                    continue;
                const auto shape = Game::currentShapeLib->shape.find(object->shape);
                if (shape == Game::currentShapeLib->shape.end() || shape->second == nullptr) {
                    if (within)
                        ++figures.unready;
                    continue;
                }
                if (!within && !shape->second->isLoaded())
                    continue;
                const float away = centre - reachOf(shape->second->getBound(), shape->second->isLoaded());
                if (away > Ahead)
                    continue;
                ++figures.objects;
                const QString path = shape->second->getPathId(), texPath = shape->second->getTexPath();
                const QString key = path + QLatin1Char('|') + texPath + QLatin1Char('|') + QString::number(int(mode));
                std::shared_ptr<const CollisionMesh> mesh;
                if (read.contains(key) || away <= Needed) {
                    // Read (the cache's), or needed now: read here.
                    mesh = shapeCollision(path, texPath, mode);
                    read.insert(key, true);
                } else {
                    if (!jobs.contains(key)) {
                        auto job = std::make_shared<Job>();
                        job->path = path;
                        job->texPath = texPath;
                        job->mode = mode;
                        jobs.insert(key, job);
                        QThreadPool::globalInstance()->start([job]() {
                            job->mesh = shapeCollision(job->path, job->texPath, job->mode);
                            job->done.store(true, std::memory_order_release);
                        });
                    }
                    continue;
                }
                if (!mesh || away > Near)
                    continue;
                float place[16], matrix[16];
                Mat4::identity(place);
                Mat4::translate(place, place, float(double(object->x - base[0]) * 2048.0), 0.0f,
                                float(double(object->y - base[1]) * 2048.0));
                Mat4::multiply(matrix, place, object->matrix);
                collisions.add(mesh, matrix, object->typeID);
                ++figures.placed;
            }
        }
    figures.pending = int(jobs.size());
    figures.gatherMs = double(timer.nsecsElapsed()) / 1e6;
}

void RouteCollision::readTracks(const float *position) {
    tracksRead = true;
    tracksAt[0] = position[0];
    tracksAt[1] = position[2];
    tracks.clear();
    TDB *tdb = Game::trackDB;
    if (tdb == nullptr)
        return;
    // The tracks within the reach and a margin of where it is now (read
    // again 100 m on).
    const float range = reach() + 150.0f;
    const double cx = double(base[0]) * 2048.0 + position[0], cz = double(base[1]) * 2048.0 + position[2];
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
            near = std::fabs(x - cx) < range + 600.0 && std::fabs(z - cz) < range + 600.0;
        }
        if (!near)
            continue;
        float *buffer = nullptr;
        int length = 0;
        tdb->getVectorSectionLine(buffer, length, base[0], base[1], id);
        for (int i = 0; i + 11 < length; i += 12) {
            const float s[4] = {buffer[i], buffer[i + 2], buffer[i + 6], buffer[i + 8]};
            if (segmentDistance(position[0], position[2], s) <= range)
                tracks.insert(tracks.end(), s, s + 4);
        }
        delete[] buffer;
    }
    figures.trackSegments = int(tracks.size() / 4);
}

float RouteCollision::trackDistance(float x, float z) const {
    float best = std::numeric_limits<float>::max();
    for (size_t i = 0; i + 3 < tracks.size(); i += 4)
        best = std::min(best, segmentDistance(x, z, &tracks[i]));
    return best;
}

}
