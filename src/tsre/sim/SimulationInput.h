#pragma once
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrackNetwork.h>
#include <tsre/sim/TrainRoute.h>
#include <tsre/trains/VehicleDefinition.h>

class Activity;
class TDB;
class Path;

namespace Sim {

// A car the activity's wagon lists cannot name (an AI train's).
constexpr unsigned int NoCar = 0xFFFFFFFFu;

// A car or engine of a train: its file as read and how it is drawn.
struct Vehicle {
    Vehicle() = default;
    // A vehicle `length` metres long of that kind (as Eng's wagonTypeId), as
    // tests make them: with a definition of its own.
    Vehicle(float length, bool flip, int type);
    // Its file as read (task driving 01): shared with the editor's cars,
    // read by the simulation thread, never changed; never null.
    std::shared_ptr<const VehicleDefinition> definition = VehicleDefinition::empty();
    bool flip = false;
    // Its Eng in the editor's EngLib, which draws it (the core does not
    // read it); -1 for one not found.
    int eng = -1;
    // As the activity's wagon lists name it (Open Rails' car id): its
    // consist's number × 65536 + its UiD in the consist, the player's train
    // being consist 0 and a loose consist its object's id; NoCar for an AI
    // train's.
    unsigned int id = NoCar;

    // From its definition: its length with its coupler (as the editor lines
    // cars up), its kind (1 carriage, 2 freight, 3 tender, 4 to 6 engines),
    // whether it carries passengers (a carriage, or a power car with a
    // passenger capacity: an EMU's).
    float length() const { return definition->length(); }
    int type() const { return definition->wagonTypeId; }
    bool passengerCapacity() const { return definition->passengerCapacity; }
    // Its share of its train's performance (task 03), summed again when a
    // train's cars change (task 07): its mass (kg; 0 not known), an
    // engine's force (N), power (W) and top speed (m/s; 0 none), its Davis
    // coefficients (the third as any vehicle but the leading one, and as the
    // leading one; 0 without a known mass).
    double mass() const { return definition->massKg; }
    double force() const { return type() >= 4 ? definition->forceN : 0.0; }
    double power() const { return type() >= 4 ? definition->powerW : 0.0; }
    float maxSpeed() const { return type() >= 4 ? float(definition->maxSpeedMps) : 0.0f; }
    double davis(int k) const { return definition->massKg > 0.0 ? definition->resistance[k] : 0.0; }
    double davisLeading() const { return definition->massKg > 0.0 ? definition->resistanceLeading : 0.0; }
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
    // The platform's track item the stop refers to (an activity's
    // PlatformStartID; a timetable's: the item its path leaves it at; task
    // shapes 12: the side its doors open on), -1 for none. Open Rails'
    // timetable trains open no doors; ours do.
    int platformItem = -1;
    // Its row in the train's timetable.
    int entry = -1;
    // The platform's exit signal held until shortly before it departs, as
    // Open Rails' activities hold it; an Open Rails timetable's stop only
    // with `$hold` (task 08).
    bool holdsExit = true;
    // Its exit signal (task 08): the first stop signal beyond the platform
    // on the platform's track node, within ExitSignalReach or with a
    // junction in its block (Open Rails' activities); at any distance (its
    // timetable mode); or wherever it is (`$forcehold`, `$forcewait`).
    enum class Exit : unsigned char { Activity, Timetable, First };
    Exit exit = Exit::Activity;
    // It leaves only once its exit signal, the next ahead, shows more than
    // STOP (Open Rails' timetable trains; not with `$nowaitsignal`).
    bool waitsForSignal = false;
    // How far short of its exit signal its front stops, m (`$closeupsignal`:
    // Open Rails' 7 m); negative: the station clearing.
    double signalClearing = -1.0;
    // The platform's end just short of its exit signal
    // (`$extendplatformtosignal`): the stop centred on that.
    bool extendToSignal = false;
    // Task 08, phase 8 (Open Rails' timetable stops): it may leave once it
    // has stood its boarding time, before its booked departure (`*`, a
    // request stop `x` without a time); a request stop (`x`) it stops at
    // only when someone gets on or off (`needed`, drawn as Open Rails
    // draws it from `$req /pu /pd`), else it passes, giving it up within
    // RequestSight of it as Open Rails' AI does; `$endstop` read (Open
    // Rails' AI and autopilot trains run on past it: only a player driving
    // is told the train terminates, kept for the player's driving); its
    // front where it stands kept there even past the exit signal
    // (`$keepclear /rear /force`).
    bool departsEarly = false;
    bool request = false;
    bool needed = true;
    bool endStop = false;
    bool forcePosition = false;
    // What its place was made from (MakeInput): at the platform's end
    // where its route goes no further (`$terminal`), the platform's end
    // short of a signal inside it (`$restrictplatformtosignal`), so much of
    // the platform kept clear ahead of it or behind it (`$keepclear /front
    // /rear`, m, -1 none).
    bool terminal = false;
    bool restrictToSignal = false;
    double keepClearFront = -1.0;
    double keepClearRear = -1.0;
};

// Where a train's route joins another's again after running apart (task
// 08, `$follow`): the track section (of the network) entered at route
// distance `at` on its route part `part`, and where the other train leaves
// it, on that train's part `otherPart`.
struct FollowPlan {
    int train = -1;
    int section = -1;
    int part = 0;
    double at = 0.0;
    int otherPart = 0;
    double otherLeave = 0.0;
    // Task 08, phase 8 (Open Rails' TTTrain.CheckWaitCondition): it waits
    // only while the other train is less late than `maxDelay` plus its own
    // delay (s, -1 always; `/maxdelay`), only while it is itself `ownDelay`
    // later than the other (s, -1 always; `/owndelay`), and also while the
    // other has not started or runs an earlier part of its route
    // (`/notstarted`; without it the wait is given up then).
    int maxDelay = -1;
    int ownDelay = -1;
    bool notStarted = false;
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
    // The autopilot's cut there (task 07, part B): the cars kept, counted
    // from the front in the direction of travel (or from the rear); -1
    // none.
    int keep = -1;
    bool keepFront = true;
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
    // An Open Rails timetable's train: named by its column alone (names are
    // unique there; an activity's AI services repeat, their times with them).
    bool timetableTrain = false;
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
    // Factors on those (an Open Rails timetable's `$acc`, `$dec`).
    float accelerationFactor = 1.0f;
    float decelerationFactor = 1.0f;
    // Open Rails' AI efficiency in driving (Physics): the service's, the
    // player's train 0.9.
    float drivingEfficiency = 1.0f;
    // No freight wagon in it (Open Rails): passenger speed limits apply,
    // else freight ones.
    bool passenger = false;
    Driving driving = Driving::Simple;
    Performance performance;
    // Seconds after midnight (more for the next day).
    int startTime = 0;
    // Open Rails timetables (task 08): put on its track at this time,
    // standing until its start holding only the track it stands on
    // (`$create`), -1 at its start; or when the run of the train it is
    // formed of ends (`$triggers`), -1 none. It stays standing at its
    // path's end (`#dispose $static`).
    int createTime = -1;
    int formedBy = -1;
    bool staysAtEnd = false;
    // Task 08, phase 8: formed of that train's vehicles where its run
    // ended, turned where it starts the other way (`$forms`, not
    // `$triggers`' own consist); put on its track ahead of that train,
    // 2.5 m beyond its front where its route runs on that train's way,
    // after that train is (`$create /ahead`); started only once that train
    // activates it (`$activated`; `activation` the activating train's
    // `$activate`: when it starts, arrives at or leaves its stop
    // `activationStop`, or its run ends), -1 none.
    bool formedOfVehicles = false;
    int createAhead = -1;
    enum class Activation : unsigned char { Start, Arrival, Departure, End };
    int activatedBy = -1;
    Activation activation = Activation::Start;
    int activationStop = -1;
    // Its top speed while on time (`$cruise`, m/s, -1 none), up to `$max`
    // (its maxSpeed) once it is later than `cruiseMaxDelay` (s, -1 never;
    // `$maxdelay`).
    float cruiseSpeed = -1.0f;
    int cruiseMaxDelay = -1;
    // Where it waits for other trains to pass (`$follow`), in its route's
    // order.
    std::vector<FollowPlan> follows;
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
    // The autopilot's work (task 07, part B): on each part, the route
    // distance from which its moves are the work's (they may pass a stop
    // signal into an occupied block, as with permission), -1 none; and to
    // which (where it comes back to its path), -1 the part's end.
    std::vector<double> workFrom;
    std::vector<double> workTo;
};

// A consist of the activity standing on the track (a loose consist): the
// track it covers, from its first vehicle's end to its last's; a standing
// train of the core (task 07).
struct StaticConsist {
    QString name;
    // Its place among the activity's objects.
    int object = -1;
    float length = 0.0f;
    std::vector<RouteInterval> track;
    // Its vehicles in the consist's order (their ids, Vehicle::id).
    std::vector<Vehicle> vehicles;
};

// A place on a vector node of the track.
struct TrackPlace {
    int node = -1;
    float distance = 0.0f;
};

// An activity event (task simulation 06), as the core checks it: Open
// Rails' rules (Activity.cs).
struct EventPlan {
    enum Kind {
        Time, Location, AllStops, AssembleTrain, AssembleTrainAtLocation, DropOffWagonsAtLocation,
        PickUpWagons, PickUpPassengers, ReachSpeed, Other
    };
    // The activity's id for it, its place in the activity's list, its name.
    int id = 0;
    int index = -1;
    QString name;
    Kind kind = Other;
    // Its activation level as the file has it (looked at while above 0).
    int activation = 0;
    bool reversible = false;
    // It shows a message; ORTSContinue: the seconds its message stays while
    // the game runs (no other event checked meanwhile), below 0 none.
    bool message = false;
    int autoContinue = -1;
    // Time: seconds after the activity's start.
    int time = 0;
    // Location: the places on the track within 2.5 m of its point, the
    // radius (m), and whether the train must stand there.
    std::vector<TrackPlace> places;
    float radius = 0.0f;
    bool onStop = false;
    // Without places: how far its point is from the nearest track (m; -1
    // more than 50 m). When the path of the train it looks at does not pass
    // its places: how far the point is from that path's track (m; -1 more
    // than 30 m), as on the other track of a double line.
    float offTrack = -1.0f;
    float offPath = -1.0f;
    // Location: the train looked at, an AI train of the input, until it
    // starts the player's train (Open Rails); -1 the player's. A triggering
    // train named but not in the activity (the player's train is looked at,
    // as Open Rails does).
    int trigger = -1;
    QString triggerMissing;
    // ReachSpeed: m/s.
    float speed = 0.0f;
    // Wagon events: the listed cars (Vehicle::id), and the siding: a node
    // and the stretch of it between its two items. Without a siding the
    // place always matches (Open Rails).
    std::vector<unsigned int> cars;
    bool siding = false;
    int sidingNode = -1;
    float sidingFrom = 0.0f;
    float sidingTo = 0.0f;
    // Outcomes, as Open Rails acts on them: the events (by place in the
    // input's list) activated, restored to their level in the file, their
    // level decreased and increased; the activity ending; an AI train of
    // the input waiting at a waiting point of value `restartWait` waiting
    // `restartDelay` seconds from now instead.
    std::vector<int> activate;
    std::vector<int> restore;
    std::vector<int> decrease;
    std::vector<int> increase;
    bool success = false;
    bool failure = false;
    int restartTrain = -1;
    int restartWait = 0;
    int restartDelay = 0;
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
    // The activity's events, in its order (task 06), and where the player's
    // train stops at its stop-here events: nowhere (decided 2026-10-21: in
    // the local content they mostly catch a player's mistakes), along the
    // platforms of its station stops only (the stop moved there), or
    // everywhere.
    std::vector<EventPlan> events;
    // The activity's restricted speed zones: their two ends on the track.
    struct Zone {
        int node[2] = {-1, -1};
        float distance[2] = {0.0f, 0.0f};
    };
    std::vector<Zone> zones;
    // The player's work orders (task 07, part B): each wagon event's as the
    // autopilot plans it; not planned: why, and whether impractical (the
    // user, 2026-10-21: skipped, the run's log warning of it).
    struct WorkOrder {
        int event = -1;
        bool planned = false;
        bool impractical = false;
        // Active at some point of the event chain as the planner follows it.
        bool active = true;
        QString why;
    };
    std::vector<WorkOrder> workOrders;
    enum class EventStops { None, Platforms, All };
    EventStops eventStops = EventStops::None;
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
// The same for a train's vehicles (task 07: again when they change): no
// freight wagon makes a passenger train; its acceleration and deceleration.
struct Rates {
    bool passenger = false;
    float acceleration = 1.0f;
    float deceleration = 1.0f;
};
Rates RatesOf(const std::vector<Vehicle> &vehicles, float topSpeed);
// A train's performance from its vehicles (task 03): their masses, the
// engines' force and power, their resistance, the first vehicle's air drag
// as the leading one's; and whether it is driven with it (every vehicle's
// mass known, force or power).
Performance PerformanceOf(const std::vector<Vehicle> &vehicles);
Driving DrivingOf(const std::vector<Vehicle> &vehicles);

// The input for an activity, made on the main thread from the editor's data
// (the activity's player and AI trains, their services, paths, consists and
// timetables, loose consists) and `tdb`, which must be Game::trackDB.
// `geometry` and `network` may be ones made earlier from the same database.
// `work`: the player's work orders planned and run by the autopilot (task
// 07, part B: ApplyWork); without, the caller may plan them before sharing
// the input (PlanWorkOrders).
std::shared_ptr<SimulationInput> MakeInput(Activity *activity, TDB *tdb, float routeSpeedLimit,
                                                 std::shared_ptr<const TrackGeometry> geometry = nullptr,
                                                 std::shared_ptr<const TrackNetwork> network = nullptr,
                                                 float restrictedSpeed = -1.0f, bool work = true);

// An Open Rails timetable set's trains into the input (task simulation 08):
// each running train with its consist (the parts, turned ones turned), its
// path (the rear at its start), its top speed (the #speed row's lower one),
// its start time, its stops at its stations' platforms with their booked
// times; static trains standing trains; the set's chosen train the
// player's. MakeInput uses it for a set.
void TimetableTrains(SimulationInput &input, Activity *set, TDB *tdb);

// An Open Rails timetable train's stations along its path, as
// TimetableTrains finds its stops (the Timetable window lists them, the
// user's review of task 08): the stations of the platforms it passes, and
// of `keys` (the timetable's stations, in lower case) one with a platform
// behind its start or beyond its end in the track section there, or one it
// reverses inside; in the order it meets them, each once. The name as the
// platform has it.
struct PathStation {
    QString name;
    QString key;
};
QVector<PathStation> TimetablePathStations(TDB *tdb, Path *path, const QStringList &keys);

// A route part's way and speed limits (the zones it passes at the input's
// restricted speed), as MakeInput makes them.
void FinishPart(const SimulationInput &input, const TrainPlan &plan, const RouteTrack &part, Projection &way,
                std::vector<SpeedLimit> &limits);

// A route part's speed limits: the speed posts facing it that apply to its
// kind of train (a limit from its post to the next limit or resume post),
// and zones (route distances from, to) at `zoneSpeed`; the lowest where
// they overlap.
std::vector<SpeedLimit> SpeedLimits(const Projection &way, const TrackNetwork &network, bool passenger,
                                    const std::vector<std::pair<double, double>> &zones, float zoneSpeed,
                                    double partLength);

}
