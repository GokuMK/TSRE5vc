/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#ifndef CHARACTERS_ROUTECOLLISION_H
#define CHARACTERS_ROUTECOLLISION_H

#include <tsre/characters/CollisionWorld.h>
#include <tsre/characters/ShapeCollision.h>
#include <QHash>
#include <QString>
#include <atomic>
#include <memory>
#include <vector>

class Eng;

namespace Characters {

// The route about a walking character (task characters 02), on the main
// thread, in the frame of one editor tile (its tile x and z; metres from
// its middle, as the camera's place in its tile):
// - its objects' collision meshes (ShapeCollision: by type as the walk
//   areas take them, static and collide objects, signals, speed posts,
//   gantries, level crossings, pickups, hazards; each object's
//   TSRECollision over its shape's) placed within Near metres; read ahead
//   on workers within Ahead, those within Needed read here when not yet;
//   placed again when it has moved Again metres, a read has finished, or
//   its tile changed (the frame is then the new tile's); an object by how
//   far its shape reaches from its origin (its bounds' farthest corner), so
//   a large shape whose origin lies in a tile further away (up to
//   LargestReach) is placed too;
// - the trains' cars within 40 m as last drawn (a moving train's within
//   the last second: a car not drawn, behind the camera, is left out),
//   each its shape's collision mesh placed by its body's matrix, every
//   update;
// - the terrain's height;
// - the reach: how far it is over the ground from the nearest track's
//   centre line (the TDB's vector sections, every 2 m), the route's
//   TsreCharacterReach or 100 m; the tracks read again past 100 m.
class RouteCollision {
public:
    static constexpr float Near = 48.0f;
    static constexpr float Ahead = 96.0f;
    static constexpr float Needed = 16.0f;
    static constexpr float Again = 4.0f;
    // The farthest a shape is looked for beyond Ahead: objects in the tiles
    // within Ahead + LargestReach, whose shapes reach within Ahead (the
    // walk areas likewise beyond their box).
    static constexpr float LargestReach = 1024.0f;
    static constexpr float DefaultReach = 100.0f;

    RouteCollision();
    ~RouteCollision();
    // How far a shape reaches from its origin (m), from its bounds (max x,
    // min x, max y, min y, max z, min z): to their farthest corner, so a
    // shape with its origin at one end counts whole (CMK's Krakow Glowny:
    // its platforms one shape, the origin past their south end); 10 m at
    // least (a shape not loaded yet has no bounds).
    static float reachOf(const float *bound);
    // About a place: the editor's tile (x, z) and the place in it.
    void update(const int *tile, const float *position);
    const CollisionWorld &world() const { return collisions; }
    CollisionWorld &world() { return collisions; }
    // The frame's tile.
    const int *frameTile() const { return base; }
    float reach() const;
    // Over the ground from the nearest track (m), in the frame; a large
    // number without one near.
    float trackDistance(float x, float z) const;
    bool allowed(float x, float z) const { return trackDistance(x, z) <= reach(); }
    // Everything let go (a new route, the run stopped).
    void clear();
    // The cars placed (task 03: the floor under a character names its car).
    struct PlacedCar {
        int train = -1;
        int car = -1;
        // Its index in the world.
        int index = -1;
        const Eng *eng = nullptr;
    };
    const std::vector<PlacedCar> &cars() const { return carPlaced; }
    // The car a placed mesh is, null for none.
    const PlacedCar *carOf(int placedIndex) const;
    struct Stats {
        int objects = 0;
        int placed = 0;
        int pending = 0;
        int trackSegments = 0;
        int cars = 0;
        // Tiles and objects about it not loaded yet at the last gather
        // (gathered again a second on while any).
        int unready = 0;
        double gatherMs = 0.0;
    };
    const Stats &stats() const { return figures; }

private:
    struct Job {
        QString path, texPath;
        Collision mode = Collision::Default;
        std::shared_ptr<const CollisionMesh> mesh;
        std::atomic<bool> done{false};
    };
    CollisionWorld collisions;
    int base[2] = {0, 0};
    bool based = false;
    float gatheredAt[3] = {0.0f, 0.0f, 0.0f};
    qint64 gatheredWhen = 0;
    float tracksAt[2] = {0.0f, 0.0f};
    bool tracksRead = false;
    // Each track segment in the frame: x0, z0, x1, z1.
    std::vector<float> tracks;
    QHash<QString, std::shared_ptr<Job>> jobs;
    QHash<QString, bool> read;
    Stats figures;
    void gather(const float *position);
    void placeCars(const float *position);
    // A shape's mesh: read (the cache's), read here within Needed, else
    // asked of a worker (null until it is read).
    std::shared_ptr<const CollisionMesh> meshFor(const QString &path, const QString &texPath, Collision mode, float away);
    std::vector<PlacedCar> carPlaced;
    void readTracks(const float *position);
    void rebase(const int *tile);
};

}

#endif
