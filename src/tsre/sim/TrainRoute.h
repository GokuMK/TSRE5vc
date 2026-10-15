#pragma once
#include <QVector>
#include <vector>
#include <tsre/trains/PathTrack.h>

class TDB;

namespace Sim {

// A stretch of one vector node in the direction travelled, from `from` to
// `to` (growing distances run towards the node's pin 1); `start` is the
// route distance at `from`.
struct RouteInterval {
    int node = -1;
    float from = 0.0f;
    float to = 0.0f;
    double start = 0.0;
    double length() const;
    bool forward() const { return to >= from; }
};

// Where a route distance is: the vector node, the distance along it and the
// direction of travel there.
struct RoutePoint {
    int node = -1;
    float distance = 0.0f;
    bool forward = true;
};

// Track travelled one way, from a path's start (with the lead-in behind it)
// or a reversal point to the next reversal point or the path's end.
// Distances along it are route distances, from 0.
class RouteTrack {
public:
    // Adds track at the end; joined to the last interval when it goes on
    // along the same node the same way.
    void append(int node, float from, float to);
    double length() const;
    // The point at a route distance, kept within the track.
    RoutePoint locate(double distance) const;
    const std::vector<RouteInterval> &intervals() const { return parts; }

private:
    std::vector<RouteInterval> parts;
};

// A train's route: its path's track in the path's order (each pass of a
// junction on its own branch), split at the reversal points. The first part
// begins with the lead-in: the track behind the path's start the train
// stands on.
struct TrainRoute {
    std::vector<RouteTrack> parts;
    // Route distance of the path's start on the first part: the lead-in's
    // length.
    double pathStart = 0.0;
    // The lead-in ended at a track end (so the train may stand advanced).
    bool leadInEnded = false;
    // The path distance (along all the path's legs from its start, as
    // Path's distanceDownPath) where each part begins.
    std::vector<double> partPathStart;
    // The path broke off (a leg off the track database): the route ends
    // there.
    bool broken = false;

    // The part and route distance of a path distance; false off the route.
    bool place(double pathDistance, int &part, double &distance) const;
};

// The route of a train standing `leadIn` metres behind the start of a path
// (its resolved nodes and legs). Made on the main thread: the lead-in is
// found on the live track database, the main route at facing junctions,
// ending early at a track end.
TrainRoute BuildRoute(TDB *tdb, const QVector<PathTrack::Node> &nodes,
                      const QVector<PathTrack::Leg> &legs, double leadIn);

}
