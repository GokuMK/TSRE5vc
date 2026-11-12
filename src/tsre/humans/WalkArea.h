/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef HUMANS_WALKAREA_H
#define HUMANS_WALKAREA_H

#include <QString>
#include <memory>
#include <vector>

class dtNavMesh;

namespace Humans {

// Where people may walk at a station (task humans 04; the roadmap's "Where
// people may walk"): a navigation mesh (Recast, Detour) built from the
// station's triangles, each with its role, beside its platforms. Plain maths
// on any thread, in the station's frame: metres, y up, near the origin.
//
// - Floor may be stood on within Recast's rules: 40 degrees of slope at
//   most, 2 m of headroom, 0.25 m to step up, a person's radius 0.3 m. A
//   bench's top is too high a step, and under it there is no headroom.
//   Solid takes space and is never stood on. Ground is stood on only in a
//   platform's ground strip. Where floor and ground meet within a step, the
//   floor wins.
// - Track: within 1.45 m of a track's centre line, from 1 m below its rail
//   to 1.5 m above, nothing is walkable (a footbridge higher up is).
// - Seeds every 2 m along each platform look out from the track's band
//   (up to 3 m) for the platform's edge, the first floor within the height
//   window (0.1 m below the rail top to 2 m above: platforms stand 0.3 to
//   1.5 m above the rail), and stand 1 m in from it on the lowest walkable
//   floor there (a bench's seat lies above it). Only what they reach is
//   kept, a flood fill over the mesh's polygons: roofs, bench tops and stray
//   flat tops go.
// - A seed finding no floor in its window goes on down to the ground, at
//   most 2 m below the rail's top. Where it lands on ground (a halt without
//   a platform shape), that stretch of the platform walks on the ground: a
//   strip from 1.7 m off the track's centre, 2.5 m wide. Ground stays closed
//   everywhere else.
// - A platform whose seeds find clearly more platform edges on the other
//   side (over twice as many, a quarter of them at least: content with its
//   side set wrong) takes the other side.
// - An edge zone along each platform, 0.8 m wide from its edge as the seeds
//   found it (a ground strip's from the strip's start), is walkable at a
//   high cost: people wait behind it, bodies and all (its area reaches a
//   person's radius further in).
class WalkArea {
public:
    enum class Role : unsigned char { Floor, Solid, Ground };
    // A polygon's area (Recast's area ids; the floor's the highest).
    enum Area : unsigned char { AreaNone = 0, AreaGround = 1, AreaStrip = 2, AreaEdge = 3, AreaFloor = 63 };
    // A polygon's flags: walked on (reached from the seeds).
    enum Flag : unsigned short { FlagWalk = 1 };

    struct Platform {
        // Its track's centre line along the platform: x, y (the rail's top),
        // z a point, two points at least.
        std::vector<float> line;
        // Which side the platform is on: +1 towards (-dz, 0, dx) from the
        // line's way (dx, 0, dz), -1 the other.
        float side = 1.0f;
        QString name;
    };
    struct Input {
        // Triangles: vertices (x, y, z), three indices each, a role each.
        std::vector<float> vertices;
        std::vector<int> triangles;
        std::vector<Role> roles;
        std::vector<Platform> platforms;
        // Every track's centre line in the area (rail top heights).
        std::vector<std::vector<float>> tracks;
        // The area across (x min, z min, x max, z max); all 0: as far as the
        // triangles go.
        float limits[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    };
    struct Settings {
        float cellSize = 0.15f;
        float cellHeight = 0.1f;
        float personHeight = 2.0f;
        float personRadius = 0.3f;
        float step = 0.25f;
        float maxSlope = 40.0f;
        float trackHalfWidth = 1.45f;
        float trackBelow = 1.0f;
        float trackAbove = 1.5f;
        float seedSpacing = 2.0f;
        float seedInset = 1.0f;
        float seedSearch = 3.0f;
        float windowBelow = 0.1f;
        float windowAbove = 2.0f;
        float groundDepth = 2.0f;
        float stripFrom = 1.7f;
        float stripWidth = 2.5f;
        float edgeWidth = 0.8f;
    };

    struct Seed {
        enum Kind : unsigned char { Missed, OnFloor, OnGround };
        float position[3] = {0.0f, 0.0f, 0.0f};
        Kind kind = Missed;
        int platform = 0;
    };
    struct Built {
        bool ok = false;
        QString error;
        std::shared_ptr<dtNavMesh> mesh;
        int polygons = 0;
        int reached = 0;
        std::vector<Seed> seeds;
        // Per platform: its seeds on floor, on ground, missed.
        struct PlatformSeeds {
            int floor = 0;
            int ground = 0;
            int missed = 0;
            // Its side had no floor, the other side had: taken.
            bool swapped = false;
            // Edges found on its given side and on the other.
            int edges = 0;
            int otherEdges = 0;
        };
        std::vector<PlatformSeeds> platforms;
        // The mesh as triangles for an overlay (nine floats each) and each
        // one's area, DroppedFlag added where no seed reached it.
        std::vector<float> triangles;
        std::vector<unsigned char> areas;
        float bounds[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        int cells[2] = {0, 0};
        double milliseconds = 0.0;
    };
    static constexpr unsigned char DroppedFlag = 0x80;
    static Built build(const Input &input, const Settings &settings);
    static Built build(const Input &input);
};

}

#endif
