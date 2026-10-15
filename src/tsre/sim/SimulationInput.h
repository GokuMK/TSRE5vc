#pragma once
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrainRoute.h>

class Activity;
class TDB;

namespace Sim {

// A car or engine of a train: its length and how it is drawn.
struct Vehicle {
    float length = 0.0f;
    bool flip = false;
    // Eng's wagonTypeId: 1 carriage, 2 freight, 3 tender, 4 to 6 engines.
    int type = 0;
    // Its Eng in the editor's EngLib, which draws it (the core does not
    // read it); -1 for one not found.
    int eng = -1;
};

// A station stop on a train's route.
struct StopPlan {
    int part = 0;
    // Route distance where the train's front stands: centred on the
    // platform, as Open Rails stops trains.
    double front = 0.0;
    // Booked times, seconds after midnight; -1 for a stop without them.
    int arrival = -1;
    int departure = -1;
    // The least time standing: Open Rails' boarding time, seconds.
    int boarding = 20;
    // The efficiency on the run to this stop.
    float efficiency = 1.0f;
    QString platform;
};

// A waiting point on a train's route, with its value (WaitingPoint).
struct WaitPlan {
    int part = 0;
    double front = 0.0;
    int value = 0;
};

// What a train does in the simulation, made from the activity.
struct TrainPlan {
    int id = 0;
    bool player = false;
    // The editor's train: the player's, or an AI service and its start time
    // and its place in the activity's traffic (Activity::aiTrainService).
    QString service;
    int serviceTime = -1;
    int traffic = -1;
    // The consist file, for drawing the vehicles.
    QString consist;
    std::vector<Vehicle> vehicles;
    float length = 0.0f;
    // Before efficiency, m/s.
    float maxSpeed = 0.0f;
    // After the last stop, and for a train without stops.
    float efficiency = 1.0f;
    float acceleration = 1.0f;
    float deceleration = 1.0f;
    // Seconds after midnight.
    int startTime = 0;
    TrainRoute route;
    // Route distance of the front at the start, on the first part.
    double startFront = 0.0;
    std::vector<StopPlan> stops;
    std::vector<WaitPlan> waits;
};

// Everything a simulation reads, copied from the editor's data when it
// starts; immutable after.
struct SimulationInput {
    QString activity;
    // Seconds after midnight.
    int startTime = 0;
    float routeSpeedLimit = 44.4444f;
    unsigned int runId = 0;
    std::shared_ptr<const TrackGeometry> geometry;
    std::vector<TrainPlan> trains;
    // Trains not made, and why.
    QStringList warnings;

    // Bytes held: the trains' plans, and the geometry's when asked.
    size_t memory(bool withGeometry = true) const;
};

// Open Rails' AI rates (m/s²): passenger trains 1.0, braking 1.5 above
// 40 m/s and 2.5 above 55 m/s; freight 0.5 both.
void SetRates(TrainPlan &train);

// The input for an activity, made on the main thread from the editor's data
// (the activity's player and AI trains, their services, paths, consists and
// timetables) and `tdb`, which must be Game::trackDB. `geometry` may be one
// copied earlier from the same database.
std::shared_ptr<const SimulationInput> MakeInput(Activity *activity, TDB *tdb, float routeSpeedLimit,
                                                 std::shared_ptr<const TrackGeometry> geometry = nullptr);

}
