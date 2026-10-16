#pragma once
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrackNetwork.h>
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
    // It carries passengers (a carriage, or a power car with a passenger
    // capacity: an EMU's).
    bool passengerCapacity = false;
};

// How a train is driven (task simulation 03): Simple, task 01's constant
// acceleration and braking; Physics, as Open Rails' AI drives with its
// force and mass.
enum class Driving { Simple, Physics };

// What the physics driving reads of a train: its mass (kg), its engines'
// force (N) and power (W), its resistance as Davis coefficients summed
// over its vehicles (N, N/(m/s), N/(m/s)²).
struct Performance {
    double mass = 0.0;
    double force = 0.0;
    double power = 0.0;
    double davisA = 0.0;
    double davisB = 0.0;
    double davisC = 0.0;
};

// A station stop on a train's route.
struct StopPlan {
    int part = 0;
    // Route distance where the train's front stands: centred on the
    // platform, as Open Rails stops trains; and the platform's far and near
    // ends.
    double front = 0.0;
    double platformEnd = 0.0;
    double platformStart = 0.0;
    // Booked times, seconds after midnight; -1 for a stop without them.
    int arrival = -1;
    int departure = -1;
    // The least time standing: Open Rails' boarding time, seconds.
    int boarding = 20;
    // The efficiency on the run to this stop.
    float efficiency = 1.0f;
    QString platform;
    // Its row in the train's timetable.
    int entry = -1;
};

// A speed limit along a route part (route distances), m/s.
struct SpeedLimit {
    double from = 0.0;
    double to = 0.0;
    float speed = 0.0f;
};

// A passing path beside a train's route (Open Rails' path-based rule, task
// 02): on route part `part` the main route leaves it at route distance
// `from` and rejoins it at `to` (its junctions); `track` is the passing
// path between those junctions (route distances from 0).
struct PassingPlan {
    int part = 0;
    double from = 0.0;
    double to = 0.0;
    RouteTrack track;
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
    // Open Rails' AI targets (m/s²): passenger trains 1.0 (an EMU 1.5) and
    // braking 1.0, 1.5 above 40 m/s, 2.5 above 55 m/s (an EMU 2.0); freight
    // 0.5 both ways.
    float acceleration = 1.0f;
    float deceleration = 1.0f;
    // Open Rails' AI efficiency in driving (Physics): the service's, the
    // player's train 0.9.
    float drivingEfficiency = 1.0f;
    // No freight wagon in it (Open Rails): passenger speed limits apply,
    // else freight ones.
    bool passenger = false;
    Driving driving = Driving::Simple;
    Performance performance;
    // Seconds after midnight.
    int startTime = 0;
    TrainRoute route;
    // Route distance of the front at the start, on the first part.
    double startFront = 0.0;
    std::vector<StopPlan> stops;
    std::vector<WaitPlan> waits;
    // Each route part's way through the track network.
    std::vector<Projection> projection;
    // Each route part's speed limits from speed posts facing it (for its
    // kind of train) and the activity's speed zones, in order, apart.
    std::vector<std::vector<SpeedLimit>> limits;
    // Its path's passing paths, in the order passed.
    std::vector<PassingPlan> passing;
};

// A consist of the activity standing on the track for the whole run (a loose
// consist): the track it covers.
struct StaticConsist {
    QString name;
    // Its place among the activity's objects.
    int object = -1;
    float length = 0.0f;
    std::vector<RouteInterval> track;
};

// Everything a simulation reads, copied from the editor's data when it
// starts; immutable after.
struct SimulationInput {
    QString activity;
    // Seconds after midnight.
    int startTime = 0;
    float routeSpeedLimit = 44.4444f;
    // The speed in the activity's restricted speed zones (the route's
    // TempRestrictedSpeed; Open Rails takes 25 km/h without one).
    float restrictedSpeed = 25.0f / 3.6f;
    // Departures from stations rounded up to this many seconds, the train
    // waiting for its rounded departure (a timetable's calculation, task
    // 04); 0 none.
    int departureStep = 0;
    unsigned int runId = 0;
    std::shared_ptr<const TrackGeometry> geometry;
    std::shared_ptr<const TrackNetwork> network;
    std::vector<TrainPlan> trains;
    std::vector<StaticConsist> staticConsists;
    // Trains not made, and why.
    QStringList warnings;

    // Bytes held: the trains' plans, and the geometry's when asked.
    size_t memory(bool withGeometry = true) const;
};

// Open Rails' AI rates (m/s²): passenger trains 1.0, braking 1.5 above
// 40 m/s and 2.5 above 55 m/s of `topSpeed` (its top speed × its service's
// efficiency within the route's limit, Open Rails' TrainMaxSpeedMpS; its
// maxSpeed when negative); freight 0.5 both.
void SetRates(TrainPlan &train, float topSpeed = -1.0f);

// The input for an activity, made on the main thread from the editor's data
// (the activity's player and AI trains, their services, paths, consists and
// timetables, loose consists) and `tdb`, which must be Game::trackDB.
// `geometry` and `network` may be ones made earlier from the same database.
std::shared_ptr<const SimulationInput> MakeInput(Activity *activity, TDB *tdb, float routeSpeedLimit,
                                                 std::shared_ptr<const TrackGeometry> geometry = nullptr,
                                                 std::shared_ptr<const TrackNetwork> network = nullptr,
                                                 float restrictedSpeed = -1.0f);

// A route part's speed limits: the speed posts facing it that apply to its
// kind of train (a limit from its post to the next limit or resume post),
// and zones (route distances from, to) at `zoneSpeed`; the lowest where
// they overlap.
std::vector<SpeedLimit> SpeedLimits(const Projection &way, const TrackNetwork &network, bool passenger,
                                    const std::vector<std::pair<double, double>> &zones, float zoneSpeed,
                                    double partLength);

}
