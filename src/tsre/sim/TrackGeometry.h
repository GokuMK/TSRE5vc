#pragma once
#include <cstddef>
#include <unordered_map>
#include <vector>
#include <tsre/tdb/TSection.h>

class TDB;

// The activity simulation (task simulation 01): its own C++ core, run on a
// thread of its own over an immutable copy of what it needs. Nothing in it
// reads editor objects or Game's globals.
namespace Sim {

// A place on the track and the track's direction there, as
// TDB::getDrawPositionOnTrNode gives it: position in its tile (x, y, z),
// the direction (yaw) and slope (pitch), the tile, and the superelevation
// (roll, its sElev, looking along the node; the other way it is negated).
struct TrackPose {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    int tileX = 0;
    int tileZ = 0;
    float roll = 0.0f;
    // Where it is on the track: its vector node, the distance along it, and
    // whether the train's way runs the node's way there (-1: not known).
    int node = -1;
    float distance = 0.0f;
    bool forward = true;
};

// The vector nodes of a track database as the simulation needs them: each
// node's placed sections with their places, and the section shapes they
// use, copied. Immutable once made.
class TrackGeometry {
public:
    explicit TrackGeometry(TDB *tdb);
    // The pose at `distance` along vector node `node`, the same as
    // TDB::getDrawPositionOnTrNode at the time of the copy; false off the
    // node or for a node or section the copy did not have.
    bool pose(int node, float distance, TrackPose &out) const;
    // The node's length; -1 for a node not copied.
    float length(int node) const;
    int nodeCount() const { return copiedNodes; }
    // Bytes held.
    size_t memory() const;

private:
    struct Placed {
        int shape = -1;
        float end = 0.0f;
        float length = 0.0f;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        // The next section's az, 0 after the node's last.
        float nextAz = 0.0f;
        int tileX = 0, tileZ = 0;
    };
    struct Node {
        int first = 0;
        int count = 0;
        float length = -1.0f;
        bool complete = false;
    };
    std::vector<Node> nodes;
    std::vector<Placed> placed;
    std::vector<TSection> shapes;
    int copiedNodes = 0;
};

}
