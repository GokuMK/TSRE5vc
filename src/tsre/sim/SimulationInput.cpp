#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/WorkPlanner.h>

#include <QDir>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_map>
#include <tsre/Game.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityEvent.h>
#include <tsre/trains/ActivityObject.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/OrTimetable.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/ServiceStops.h>

namespace Sim {

Vehicle::Vehicle(float length, bool flip, int type) : flip(flip) {
    auto own = std::make_shared<VehicleDefinition>();
    own->size[2] = length;
    own->wagonTypeId = type;
    definition = own;
}

namespace {

std::atomic<unsigned int> nextRun{1};

const TRitem *platformItem(TDB *tdb, int id) {
    auto it = tdb->trackItems.find(id);
    if (it == tdb->trackItems.end() || it->second == nullptr || it->second->platformTrItemData == nullptr)
        return nullptr;
    return it->second;
}

// Passengers waiting at a platform: the activity's figure, else the
// platform item's.
int passengersWaiting(Activity *activity, TDB *tdb, int platformId) {
    const TRitem *item = platformItem(tdb, platformId);
    const int partner = item != nullptr ? int(item->platformTrItemData[1]) : -1;
    for (const QPair<int, int> &data : activity->platformNumPassengersWaiting)
        if (data.first == platformId || data.first == partner)
            return data.second;
    return item != nullptr ? std::max(0, item->platformNumPassengersWaiting) : 0;
}

bool sameName(const QString &a, const QString &b) {
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

Eng *engOf(int engId) {
    if (engId < 0 || Game::currentEngLib == nullptr)
        return nullptr;
    auto e = Game::currentEngLib->eng.find(engId);
    return e != Game::currentEngLib->eng.end() ? e->second : nullptr;
}

// A consist's vehicle as the core sees it: its file's definition, shared.
Vehicle VehicleOf(const Consist::EngItem &item) {
    Vehicle vehicle;
    vehicle.flip = item.flip;
    vehicle.eng = item.eng;
    if (const Eng *eng = engOf(item.eng))
        vehicle.definition = eng->definition;
    return vehicle;
}

// A place of the activity (tile x, tile z, x, z as its files have them) on
// the nearest track, as the editor finds it.
bool placeOnTrack(TDB *tdb, const float *tile, int &node, float &distance) {
    float posT[2] = {tile[0], -tile[1]};
    float pos[3] = {tile[2], 0.0f, -tile[3]};
    float tpos[3] = {0.0f, 0.0f, 0.0f};
    if (tdb->findNearestPositionOnTDB(posT, pos, nullptr, tpos, false) < 0 || tpos[0] < 1.0f)
        return false;
    node = int(tpos[0]);
    distance = tpos[1];
    return true;
}

// The activity's loose consists on the track, placed as the editor places
// them (Consist::initOnTrack): the first vehicle's middle at the object's
// place, the rest behind it.
std::vector<StaticConsist> StaticConsists(Activity *activity, TDB *tdb) {
    std::vector<StaticConsist> consists;
    for (int i = 0; i < activity->activityObjects.size(); ++i) {
        ActivityObject *object = activity->activityObjects[i];
        if (object == nullptr || object->objectTypeId != ActivityObject::WAGONLIST || object->con == nullptr
                || object->con->engItems.isEmpty())
            continue;
        StaticConsist consist;
        consist.name = object->con->showName;
        consist.object = i;
        float first = 0.0f;
        for (int v = 0; v < object->con->engItems.size(); ++v) {
            const Consist::EngItem &item = object->con->engItems[v];
            Vehicle vehicle = VehicleOf(item);
            vehicle.id = unsigned(object->id) * 65536u + unsigned(item.uid);
            if (v == 0)
                first = vehicle.length();
            consist.length += vehicle.length();
            consist.vehicles.push_back(vehicle);
        }
        int node = -1;
        float distance = 0.0f;
        if (consist.length <= 0.0f || !placeOnTrack(tdb, object->tile, node, distance))
            continue;
        const bool forward = object->direction == 0;
        // To the first vehicle's front, then back along the whole consist.
        const QVector<PathTrack::Leg> ahead = PathTrack::follow(tdb, node, distance, forward, first * 0.5f);
        if (ahead.isEmpty())
            continue;
        const PathTrack::Leg &front = ahead.last();
        for (const PathTrack::Leg &leg : PathTrack::follow(tdb, front.track, front.to, !forward, consist.length)) {
            RouteInterval interval;
            interval.node = leg.track;
            interval.from = leg.from;
            interval.to = leg.to;
            consist.track.push_back(interval);
        }
        consists.push_back(consist);
    }
    return consists;
}

// An AI train of the input by its service and start time (-1 any), as Open
// Rails finds a triggering or restarted train; -1 none.
int aiTrainOf(const SimulationInput &input, const QString &service, int time) {
    for (const TrainPlan &train : input.trains)
        if (!train.player && sameName(train.service, service) && (time < 0 || train.serviceTime == time))
            return train.id;
    return -1;
}

EventPlan::Kind kindOf(const ActivityEvent &event) {
    if (event.category == ActivityEvent::CategoryTime)
        return EventPlan::Time;
    if (event.category == ActivityEvent::CategoryLocation)
        return EventPlan::Location;
    switch (event.eventType) {
    case ActivityEvent::EventTypeAllstops: return EventPlan::AllStops;
    case ActivityEvent::EventTypeAssembleTrain: return EventPlan::AssembleTrain;
    case ActivityEvent::EventTypeAssembleTrainAtLocation: return EventPlan::AssembleTrainAtLocation;
    case ActivityEvent::EventTypeDropoffWagonsAtLocation: return EventPlan::DropOffWagonsAtLocation;
    case ActivityEvent::EventTypePickupWagons: return EventPlan::PickUpWagons;
    case ActivityEvent::EventTypePickupPassengers: return EventPlan::PickUpPassengers;
    case ActivityEvent::EventTypeReachSpeed: return EventPlan::ReachSpeed;
    default: return EventPlan::Other;
    }
}

// The activity's events as the core checks them (task 06): a location
// event's point on the track within 2.5 m of it (Open Rails'
// MaximumCenterlineOffset), sidings between their two items, outcomes by
// the events' places in the list.
std::vector<EventPlan> Events(Activity *activity, TDB *tdb, const SimulationInput &input, QStringList &warnings) {
    std::vector<EventPlan> plans;
    const auto byId = [activity](int id) {
        std::vector<int> found;
        for (int k = 0; k < activity->event.size(); ++k)
            if (activity->event[k] != nullptr && activity->event[k]->id == id)
                found.push_back(k);
        return found;
    };
    for (int i = 0; i < activity->event.size(); ++i) {
        const ActivityEvent *event = activity->event[i];
        if (event == nullptr)
            continue;
        EventPlan plan;
        plan.id = event->id;
        plan.index = i;
        plan.name = event->name.isEmpty() ? QString::number(event->id) : event->name;
        plan.kind = kindOf(*event);
        plan.activation = event->activationLevel != -99999 ? event->activationLevel : 0;
        plan.reversible = event->reversableEvent;
        plan.autoContinue = event->ortsContinue != -99999 ? event->ortsContinue : -1;
        plan.time = event->time != -99999 ? event->time : 0;
        plan.speed = event->speed != -99999 ? event->speed : 0.0f;
        const QString &name = plan.name;
        if (plan.kind == EventPlan::Location && event->location != nullptr) {
            plan.radius = event->location[4];
            plan.onStop = event->triggerOnStop == 1;
            float posT[2] = {event->location[0], -event->location[1]};
            float pos[3] = {event->location[2], 0.0f, -event->location[3]};
            QVector<TDB::IntersectionPoint> points;
            tdb->findNearestPositionsOnTDB(posT, pos, points, 2.5f, false);
            for (const TDB::IntersectionPoint &point : points) {
                TrackPlace place{int(point.idx), point.m};
                bool seen = false;
                for (const TrackPlace &other : plan.places)
                    seen = seen || (other.node == place.node && std::fabs(other.distance - place.distance) < 0.5f);
                if (!seen)
                    plan.places.push_back(place);
            }
            if (plan.places.empty()) {
                QVector<TDB::IntersectionPoint> further;
                tdb->findNearestPositionsOnTDB(posT, pos, further, 50.0f, false);
                for (const TDB::IntersectionPoint &point : further)
                    plan.offTrack = plan.offTrack < 0.0f ? point.distance : std::min(plan.offTrack, point.distance);
            }
            if (!event->triggeringService.isEmpty()) {
                plan.trigger = aiTrainOf(input, event->triggeringService, event->triggeringTime);
                if (plan.trigger < 0) {
                    plan.triggerMissing = event->triggeringService;
                    warnings << QString("event %1: its triggering train %2 is not in the activity").arg(name, event->triggeringService);
                }
            }
            // The path of the train it looks at: does it pass the places;
            // else how near its track comes.
            const TrainPlan *looked = nullptr;
            for (const TrainPlan &train : input.trains)
                if (plan.trigger >= 0 ? train.id == plan.trigger : train.player)
                    looked = &train;
            const auto onPath = [looked](int node, float distance) {
                if (looked != nullptr)
                    for (const RouteTrack &part : looked->route.parts)
                        for (const RouteInterval &interval : part.intervals())
                            if (interval.node == node && distance >= std::min(interval.from, interval.to) - 1e-3f
                                    && distance <= std::max(interval.from, interval.to) + 1e-3f)
                                return true;
                return false;
            };
            bool passed = false;
            for (const TrackPlace &place : plan.places)
                passed = passed || onPath(place.node, place.distance);
            if (!passed && looked != nullptr) {
                QVector<TDB::IntersectionPoint> around;
                tdb->findNearestPositionsOnTDB(posT, pos, around, 30.0f, false);
                for (const TDB::IntersectionPoint &point : around)
                    if (onPath(int(point.idx), point.m))
                        plan.offPath = plan.offPath < 0.0f ? point.distance : std::min(plan.offPath, point.distance);
            }
        }
        for (const ActivityEvent::WagonList &wagon : event->wagonList)
            plan.cars.push_back(wagon.uid);
        if ((plan.kind == EventPlan::AssembleTrainAtLocation || plan.kind == EventPlan::DropOffWagonsAtLocation)
                && event->sidingItem >= 0) {
            // A siding item and the one it links to; without them the place
            // always matches (Open Rails).
            auto found = tdb->trackItems.find(event->sidingItem);
            TRitem *a = found != tdb->trackItems.end() ? found->second : nullptr;
            TRitem *b = nullptr;
            if (a != nullptr && a->type == "sidingitem" && a->platformTrItemData != nullptr) {
                auto linked = tdb->trackItems.find(int(a->platformTrItemData[1]));
                b = linked != tdb->trackItems.end() ? linked->second : nullptr;
            }
            if (a != nullptr && b != nullptr) {
                const int nodeA = tdb->findTrItemNodeId(int(a->trItemId)), nodeB = tdb->findTrItemNodeId(int(b->trItemId));
                plan.siding = true;
                if (nodeA >= 0 && nodeA == nodeB) {
                    plan.sidingNode = nodeA;
                    plan.sidingFrom = std::min(a->getTrackPosition(), b->getTrackPosition());
                    plan.sidingTo = std::max(a->getTrackPosition(), b->getTrackPosition());
                } else {
                    warnings << QString("event %1: its siding's two ends are not on one track node").arg(name);
                }
            }
        }
        for (const ActivityEvent::Outcome *outcome : event->outcomes) {
            if (outcome == nullptr)
                continue;
            switch (outcome->type) {
            case ActivityEvent::Outcome::TypeDisplayMessage:
                plan.message = plan.message || !outcome->value.toString().isEmpty();
                break;
            case ActivityEvent::Outcome::TypeActivitySuccess:
                plan.success = true;
                break;
            case ActivityEvent::Outcome::TypeActivityFail:
                plan.failure = true;
                break;
            case ActivityEvent::Outcome::TypeActivateEvent:
                for (int k : byId(outcome->value.toInt()))
                    plan.activate.push_back(k);
                break;
            case ActivityEvent::Outcome::TypeRestorAactLevel:
                for (int k : byId(outcome->value.toInt()))
                    plan.restore.push_back(k);
                break;
            case ActivityEvent::Outcome::TypeDecActLevel:
                for (int k : byId(outcome->value.toInt()))
                    plan.decrease.push_back(k);
                break;
            case ActivityEvent::Outcome::TypeIncActLevel:
                for (int k : byId(outcome->value.toInt()))
                    plan.increase.push_back(k);
                break;
            case ActivityEvent::Outcome::TypeORTSRestartWaitingTrain:
                if (outcome->restartService.isEmpty())
                    break;
                plan.restartTrain = aiTrainOf(input, outcome->restartService, outcome->restartTime);
                plan.restartWait = outcome->restartWaitingPoint;
                plan.restartDelay = outcome->restartDelay;
                if (plan.restartTrain < 0)
                    warnings << QString("event %1: the train to restart, %2, is not in the activity").arg(name, outcome->restartService);
                break;
            default:
                break;
            }
        }
        plans.push_back(plan);
    }
    // Outcomes name events by their place in the activity's list; the core
    // keeps the plans in that order without the missing ones.
    std::vector<int> place(size_t(activity->event.size()), -1);
    for (size_t k = 0; k < plans.size(); ++k)
        place[size_t(plans[k].index)] = int(k);
    for (EventPlan &plan : plans)
        for (std::vector<int> *list : {&plan.activate, &plan.restore, &plan.decrease, &plan.increase})
            for (int &k : *list)
                k = place[size_t(k)];
    return plans;
}

}

size_t SimulationInput::memory(bool withGeometry) const {
    size_t bytes = sizeof(SimulationInput) + trains.capacity() * sizeof(TrainPlan);
    for (const TrainPlan &train : trains) {
        bytes += train.vehicles.capacity() * sizeof(Vehicle) + train.stops.capacity() * sizeof(StopPlan)
                + train.waits.capacity() * sizeof(WaitPlan)
                + train.route.parts.capacity() * sizeof(RouteTrack) + train.route.partPathStart.capacity() * sizeof(double);
        for (const RouteTrack &part : train.route.parts)
            bytes += part.intervals().capacity() * sizeof(RouteInterval);
    }
    if (withGeometry && geometry != nullptr)
        bytes += geometry->memory();
    return bytes;
}

std::vector<SpeedLimit> SpeedLimits(const Projection &way, const TrackNetwork &network, bool passenger,
                                    const std::vector<std::pair<double, double>> &zones, float zoneSpeed,
                                    double partLength) {
    // Where the posts' limit changes (a negative speed: none).
    std::vector<std::pair<double, float>> changes;
    for (const ProjectedItem &item : way.speedPosts) {
        const SpeedPost &post = network.speedPosts()[size_t(item.index)];
        if (post.kind == SpeedPost::Resume)
            changes.push_back({item.at, -1.0f});
        else if (post.kind == SpeedPost::Limit && (passenger ? post.passenger : post.freight) && post.speed > 0.0f)
            changes.push_back({item.at, post.speed});
    }
    std::vector<double> cuts = {0.0, partLength};
    for (const auto &change : changes)
        cuts.push_back(change.first);
    for (const auto &zone : zones) {
        cuts.push_back(zone.first);
        cuts.push_back(zone.second);
    }
    std::sort(cuts.begin(), cuts.end());
    std::vector<SpeedLimit> limits;
    size_t change = 0;
    float post = -1.0f;
    for (size_t k = 0; k + 1 < cuts.size(); ++k) {
        const double from = std::max(0.0, cuts[k]), to = std::min(partLength, cuts[k + 1]);
        while (change < changes.size() && changes[change].first <= from + 1e-6)
            post = changes[change++].second;
        if (to - from < 1e-3)
            continue;
        float speed = post;
        for (const auto &zone : zones)
            if (zone.first < to - 1e-6 && zone.second > from + 1e-6)
                speed = speed > 0.0f ? std::min(speed, zoneSpeed) : zoneSpeed;
        if (speed <= 0.0f)
            continue;
        if (!limits.empty() && std::fabs(limits.back().to - from) < 1e-6 && limits.back().speed == speed)
            limits.back().to = to;
        else
            limits.push_back({from, to, speed});
    }
    return limits;
}

Rates RatesOf(const std::vector<Vehicle> &vehicles, float top) {
    Rates rates;
    // A freight wagon makes it a freight train (Open Rails).
    bool freight = false;
    for (const Vehicle &vehicle : vehicles)
        freight = freight || vehicle.type() == 2;
    rates.passenger = !freight;
    if (freight) {
        rates.acceleration = 0.5f;
        rates.deceleration = 0.5f;
        return rates;
    }
    rates.acceleration = 1.0f;
    rates.deceleration = top > 55.0f ? 2.5f : top > 40.0f ? 1.5f : 1.0f;
    // An EMU: electric power cars with passengers at both ends.
    const auto powerCar = [](const Vehicle &vehicle) { return vehicle.type() == 4 && vehicle.passengerCapacity(); };
    if (rates.deceleration == 1.0f && !vehicles.empty() && powerCar(vehicles.front()) && powerCar(vehicles.back())) {
        rates.acceleration = 1.5f;
        rates.deceleration = 2.0f;
    }
    return rates;
}

void SetRates(TrainPlan &train, float topSpeed) {
    const Rates rates = RatesOf(train.vehicles, topSpeed >= 0.0f ? topSpeed : train.maxSpeed);
    train.passenger = rates.passenger;
    train.acceleration = rates.acceleration * train.accelerationFactor;
    train.deceleration = rates.deceleration * train.decelerationFactor;
}

Performance PerformanceOf(const std::vector<Vehicle> &vehicles) {
    Performance p;
    for (size_t i = 0; i < vehicles.size(); ++i) {
        const Vehicle &vehicle = vehicles[i];
        p.mass += vehicle.mass();
        p.force += vehicle.force();
        p.power += vehicle.power();
        p.davisA += vehicle.davis(0);
        p.davisB += vehicle.davis(1);
        p.davisC += i == 0 ? vehicle.davisLeading() : vehicle.davis(2);
    }
    return p;
}

Driving DrivingOf(const std::vector<Vehicle> &vehicles) {
    bool known = !vehicles.empty();
    double force = 0.0, power = 0.0;
    for (const Vehicle &vehicle : vehicles) {
        known = known && vehicle.mass() > 0.0;
        force += vehicle.force();
        power += vehicle.power();
    }
    return known && (force > 0.0 || power > 0.0) ? Driving::Physics : Driving::Simple;
}

void FinishPart(const SimulationInput &input, const TrainPlan &plan, const RouteTrack &part, Projection &way,
                std::vector<SpeedLimit> &limits) {
    way = input.network->project(part);
    // The zones whose both ends this part passes.
    std::vector<std::pair<double, double>> passed;
    for (const SimulationInput::Zone &zone : input.zones) {
        double a = 0.0, b = 0.0;
        if (part.find(zone.node[0], zone.distance[0], a) && part.find(zone.node[1], zone.distance[1], b))
            passed.push_back({std::min(a, b), std::max(a, b)});
    }
    limits = SpeedLimits(way, *input.network, plan.passenger, passed, input.restrictedSpeed, part.length());
}

namespace {

// One consist of a train, the other way round (a timetable's `$reverse`)
// or not.
struct ConsistPart {
    const Consist *consist = nullptr;
    bool reverse = false;
};

// A train's vehicles from its consists in order (one turned: its vehicles
// the other way, each flipped), its length, its performance and driving
// (task 03), its top speed: the consist files' MaxVelocity (as Open Rails
// takes it, the lowest of several), else the slowest engine's. The
// player's train's vehicles are consist 0 of the wagon lists. A train not
// driven (a timetable's static one) gets its vehicles and length only.
void vehiclesOf(SimulationInput &input, TrainPlan &plan, const std::vector<ConsistPart> &parts, bool player,
                const QString &name, bool driven = true) {
    float engineSpeed = 0.0f;
    float fileSpeed = 0.0f;
    QStringList missing;
    for (const ConsistPart &part : parts) {
        std::vector<Vehicle> vehicles;
        for (const Consist::EngItem &item : part.consist->engItems) {
            Vehicle vehicle = VehicleOf(item);
            if (player)
                vehicle.id = unsigned(item.uid);
            if (vehicle.maxSpeed() > 0.0f)
                engineSpeed = engineSpeed > 0.0f ? std::min(engineSpeed, vehicle.maxSpeed()) : vehicle.maxSpeed();
            if (vehicle.mass() <= 0.0) {
                const Eng *eng = engOf(item.eng);
                const QString what = eng != nullptr ? eng->name : QString::number(item.eng);
                if (!missing.contains(what))
                    missing << what;
            }
            vehicles.push_back(vehicle);
        }
        if (part.reverse) {
            std::reverse(vehicles.begin(), vehicles.end());
            for (Vehicle &vehicle : vehicles)
                vehicle.flip = !vehicle.flip;
        }
        for (const Vehicle &vehicle : vehicles) {
            plan.length += vehicle.length();
            plan.vehicles.push_back(vehicle);
        }
        if (part.consist->fileMaxVelocity > 0.1f)
            fileSpeed = fileSpeed > 0.0f ? std::min(fileSpeed, part.consist->fileMaxVelocity) : part.consist->fileMaxVelocity;
    }
    if (!driven)
        return;
    // Its performance (task 03), the leading vehicle's air drag its own.
    plan.performance = PerformanceOf(plan.vehicles);
    // Driven with force and mass where every vehicle has its mass and the
    // engines their force or power (task 03), else simply.
    if (missing.isEmpty() && (plan.performance.force > 0.0 || plan.performance.power > 0.0)) {
        plan.driving = Driving::Physics;
    } else {
        plan.driving = Driving::Simple;
        input.warnings << (missing.isEmpty()
                           ? QString("%1: driven simply (its engines have no force or power)").arg(name)
                           : QString("%1: driven simply (no mass for %2)").arg(name, missing.join(", ")));
    }
    plan.maxSpeed = fileSpeed > 0.1f ? fileSpeed : engineSpeed > 0.0f ? engineSpeed : input.routeSpeedLimit;
}

// The train's route on its path: it stands with its rear at the path's
// start, the whole train ahead of it, as Open Rails places trains
// (Simulator.InitializePlayerTrain, AI.cs; task 06). The track behind, the
// train's length, is its route's lead-in: where its first part is shorter
// than the train, it stands back on it.
bool routeOf(SimulationInput &input, TDB *tdb, TrainPlan &plan, Path *path, const QString &name) {
    plan.route = BuildRoute(tdb, path->nodes(), path->legs(), double(plan.length) + 1.0);
    if (plan.route.parts.empty() || plan.route.parts.front().length() <= 0.0) {
        input.warnings << QString("%1: its path has no track").arg(name);
        return false;
    }
    plan.startFront = std::min(plan.route.pathStart + plan.length, plan.route.parts.front().length());
    return true;
}

// A station stop at a pass of the path by a platform: centred on the
// platform, a longer train overhanging both ends; false beyond the route.
bool stopAt(const TrainPlan &plan, const Path::PathObject &pass, StopPlan &stop) {
    double nearEnd = 1e12, farEnd = -1e12;
    for (const QPair<int, float> &met : pass.items) {
        nearEnd = std::min(nearEnd, double(met.second));
        farEnd = std::max(farEnd, double(met.second));
    }
    double nearRoute = 0.0;
    int nearPart = 0;
    if (!plan.route.place(farEnd, stop.part, stop.front) || !plan.route.place(nearEnd, nearPart, nearRoute))
        return false;
    if (nearPart != stop.part)
        nearRoute = 0.0;
    stop.platformEnd = stop.front;
    stop.platformStart = nearRoute;
    const double platformLength = stop.front - nearRoute;
    stop.front = std::clamp(stop.front - 0.5 * (platformLength - plan.length), 0.0,
                            plan.route.parts[size_t(stop.part)].length());
    return true;
}

// The route parts' ways and speed limits, and the path's passing paths.
void finishTrain(SimulationInput &input, TDB *tdb, TrainPlan &plan, Path *path) {
    const QVector<PathTrack::Node> &nodes = path->nodes();
    const QVector<PathTrack::Leg> &legs = path->legs();
    for (const RouteTrack &part : plan.route.parts) {
        plan.projection.emplace_back();
        plan.limits.emplace_back();
        FinishPart(input, plan, part, plan.projection.back(), plan.limits.back());
    }
    // Passing paths: each where the main route leaves it and rejoins
    // it, and its own track between.
    {
        QVector<double> nodeDown(nodes.size(), 0.0);
        for (int k = 1; k < nodes.size() && k - 1 < legs.size(); ++k)
            nodeDown[k] = nodeDown[k - 1] + std::fabs(double(legs[k - 1].to) - double(legs[k - 1].from));
        for (const PathSnapshot::Passing &side : path->passingPaths()) {
            if (side.from < 0 || side.to >= nodes.size() || side.from >= side.to)
                continue;
            QVector<PathTrack::Node> around;
            around << nodes[side.from] << side.nodes << nodes[side.to];
            const QVector<PathTrack::Leg> sideLegs = PathTrack::legs(tdb, around);
            bool broken = sideLegs.isEmpty();
            for (const PathTrack::Leg &leg : sideLegs)
                broken = broken || leg.broken;
            PassingPlan passing;
            int partTo = -1;
            if (broken || !plan.route.place(nodeDown[side.from], passing.part, passing.from)
                    || !plan.route.place(nodeDown[side.to], partTo, passing.to) || partTo != passing.part
                    || passing.to <= passing.from)
                continue;
            for (const PathTrack::Leg &leg : sideLegs)
                passing.track.append(leg.track, leg.from, leg.to);
            if (passing.track.length() > 0.0)
                plan.passing.push_back(passing);
        }
        std::sort(plan.passing.begin(), plan.passing.end(), [](const PassingPlan &a, const PassingPlan &b) {
            return a.part != b.part ? a.part < b.part : a.from < b.from;
        });
    }
}

// A platform of station `key` on vector node `track` beyond a path's end
// there (`edge`; `away` +1 or -1, the way along the node from the path): a
// station on the path's route as Open Rails counts one, its route holding
// the whole track section at each end of the path (sections split at
// signals only, Signals.InsertNode). The nearest such platform: an item of
// it and its ends along the node, nearer first; none when there is none or
// a signal is between.
const TRitem *platformBeyond(TDB *tdb, int track, float edge, float away, const QString &key, float &nearEnd,
                             float &farEnd) {
    auto it = tdb->trackNodes.find(track);
    const TRnode *vector = it != tdb->trackNodes.end() ? it->second : nullptr;
    float nearest = 1e30f, signal = 1e30f;
    const TRitem *platform = nullptr;
    for (int i = 0; vector != nullptr && i < vector->iTri; ++i) {
        auto found = tdb->trackItems.find(vector->trItemRef[i]);
        TRitem *item = found != tdb->trackItems.end() ? found->second : nullptr;
        if (item == nullptr)
            continue;
        const float beyond = (item->getTrackPosition() - edge) * away;
        if (beyond <= 0.0f)
            continue;
        if (item->type == "signalitem") {
            signal = std::min(signal, beyond);
            continue;
        }
        if (item->platformTrItemData == nullptr || item->type != "platformitem" || beyond >= nearest
                || item->stationName.trimmed().toLower() != key)
            continue;
        auto partner = tdb->trackItems.find(int(item->platformTrItemData[1]));
        if (partner == tdb->trackItems.end() || partner->second == nullptr
                || tdb->findTrItemNodeId(int(partner->first)) != track)
            continue;
        const float other = (partner->second->getTrackPosition() - edge) * away;
        nearest = std::min(beyond, other);
        nearEnd = edge + away * nearest;
        farEnd = edge + away * std::max(beyond, other);
        platform = item;
    }
    return nearest < signal ? platform : nullptr;
}

// Each platform of station `key` the path reverses inside: an item of it
// on the leg before a reversal, its other on the same node beyond (task 08,
// phase 7); `take` is given the item and the distances down the path to it
// and to the reversal. The first it takes; none when it takes none.
const TRitem *reversalPlatform(TDB *tdb, const QVector<PathTrack::Leg> &legs, const QString &key,
                               const std::function<bool(const TRitem *, double, double)> &take) {
    double down = 0.0;
    for (int i = 0; i + 1 < legs.size(); ++i) {
        const PathTrack::Leg &leg = legs[i], &next = legs[i + 1];
        const double legStart = down;
        down += std::fabs(double(leg.to) - double(leg.from));
        if (leg.broken || next.track != leg.track || std::fabs(next.from - leg.to) >= PathTrack::SamePlace
                || next.forward() == leg.forward())
            continue;
        auto it = tdb->trackNodes.find(leg.track);
        const TRnode *vector = it != tdb->trackNodes.end() ? it->second : nullptr;
        const float away = leg.forward() ? 1.0f : -1.0f;
        for (int t = 0; vector != nullptr && t < vector->iTri; ++t) {
            auto found = tdb->trackItems.find(vector->trItemRef[t]);
            TRitem *item = found != tdb->trackItems.end() ? found->second : nullptr;
            if (item == nullptr || item->type != "platformitem" || item->platformTrItemData == nullptr
                    || item->stationName.trimmed().toLower() != key)
                continue;
            const float at = item->getTrackPosition();
            if ((at - leg.from) * away < -PathTrack::SamePlace || (at - leg.to) * away > 0.0f)
                continue;
            auto partner = tdb->trackItems.find(int(item->platformTrItemData[1]));
            if (partner == tdb->trackItems.end() || partner->second == nullptr
                    || tdb->findTrItemNodeId(int(partner->first)) != leg.track
                    || (partner->second->getTrackPosition() - leg.to) * away <= 0.0f)
                continue;
            if (take(item, legStart + std::fabs(double(at) - double(leg.from)), down))
                return item;
        }
    }
    return nullptr;
}

// A platform of station `key` the path reverses inside: Open Rails' route
// holds the platform's track section, the stop on the subpath before the
// reversal (TTTrain.CalculateStationStopPosition, the path not through the
// station); `stop` there centred on the platform up to the reversal. The
// first such reversal's; none when there is none.
const TRitem *reversalInside(TDB *tdb, const QVector<PathTrack::Leg> &legs, const QString &key,
                             const TrainPlan &plan, StopPlan &stop) {
    return reversalPlatform(tdb, legs, key, [&plan, &stop](const TRitem *, double itemDown, double reversalDown) {
        int nearPart = 0;
        double nearAt = 0.0;
        if (!plan.route.place(itemDown, nearPart, nearAt) || !plan.route.place(reversalDown - 0.01, stop.part, stop.front)
                || nearPart != stop.part)
            return false;
        stop.platformStart = nearAt;
        stop.platformEnd = stop.front;
        stop.front = std::clamp(stop.front - 0.5 * (stop.front - nearAt - plan.length), 0.0, stop.front);
        return true;
    });
}

// An Open Rails timetable's command of that name among `commands`, the
// last given, or none.
const OrTimetable::Command *commandIn(const QVector<OrTimetable::Command> &commands, const QString &name) {
    const OrTimetable::Command *found = nullptr;
    for (const OrTimetable::Command &command : commands)
        if (command.name == name)
            found = &command;
    return found;
}

// A timetable stop's switches from its station's row and then its own cell
// (task 08; the cell's win, Open Rails): the exit signal held (`$hold`,
// `$nohold`; `$forcehold` the first signal beyond the platform), waited for
// before it leaves (`$nowaitsignal`, `$waitsignal`; `$forcewait` the first
// signal beyond), stopped closer to (`$closeupsignal`), the platform's end
// at it (`$extendplatformtosignal`).
void stopSwitches(StopPlan &stop, const QVector<OrTimetable::Command> &commands) {
    for (const OrTimetable::Command &command : commands) {
        if (command.name == "hold") {
            stop.holdsExit = true;
        } else if (command.name == "nohold") {
            stop.holdsExit = false;
        } else if (command.name == "forcehold") {
            stop.holdsExit = true;
            stop.exit = StopPlan::Exit::First;
        } else if (command.name == "nowaitsignal") {
            stop.waitsForSignal = false;
        } else if (command.name == "waitsignal") {
            stop.waitsForSignal = true;
        } else if (command.name == "forcewait") {
            stop.waitsForSignal = true;
            stop.exit = StopPlan::Exit::First;
        } else if (command.name == "closeupsignal") {
            // Open Rails' TTTrain.keepDistanceCloseupSignalM.
            stop.signalClearing = 7.0;
        } else if (command.name == "extendplatformtosignal") {
            stop.extendToSignal = true;
        }
    }
}

// A number 0 to 99 drawn from a text (FNV-1a): a request stop's need the
// same in every run (Open Rails draws at random).
int drawOf(const QString &text) {
    quint32 hash = 2166136261u;
    for (const QChar c : text) {
        hash = (hash ^ c.unicode()) * 16777619u;
    }
    return int(hash % 100u);
}

// Task 08, phase 8: a timetable stop's commands that make its times and
// place (Open Rails' StopInfo.BuildStopInfo; the station's row's added
// after the cell's): `$stoptime=n` the least time standing (the station's
// last, so it wins), `$terminal` (either), `$restrictplatformtosignal`
// (either), `$keepclear /front /rear /force` and `$endstop` (the cell's).
// A request stop's need (`$req /pu /pd`, the cell's after the station's):
// a pick-up with /pu per cent, a set-down with /pd, 25 each by default
// (RequestStop.SetStopDetails, each "<= p" of 0 to 99), drawn from the
// train's and station's names.
void stopCommands(StopPlan &stop, int &least, const QVector<OrTimetable::Command> &cell,
                  const QVector<OrTimetable::Command> &station, const QString &train) {
    for (const QVector<OrTimetable::Command> *list : {&cell, &station})
        for (const OrTimetable::Command &command : *list) {
            if (command.name == "stoptime" && !command.values.isEmpty()) {
                bool ok = false;
                const int value = command.values.first().toInt(&ok);
                if (ok)
                    least = value;
            } else if (command.name == "terminal") {
                stop.terminal = true;
            } else if (command.name == "restrictplatformtosignal") {
                stop.restrictToSignal = true;
            }
        }
    for (const OrTimetable::Command &command : cell) {
        if (command.name == "endstop") {
            stop.endStop = true;
        } else if (command.name == "keepclear" && !command.qualifiers.isEmpty()) {
            // The first of /front and /rear says which; the value is the
            // first qualifier's (Open Rails reads CommandQualifiers[0]).
            int side = 0;
            for (const OrTimetable::Qualifier &qualifier : command.qualifiers) {
                if (side == 0 && (qualifier.name == "front" || qualifier.name == "rear"))
                    side = qualifier.name == "front" ? 1 : 2;
                if (qualifier.name == "force")
                    stop.forcePosition = true;
            }
            bool ok = false;
            const double value = command.qualifiers.first().values.isEmpty()
                    ? 0.0 : command.qualifiers.first().values.first().toDouble(&ok);
            if (side == 1 && ok)
                stop.keepClearFront = value;
            else if (side == 2 && ok)
                stop.keepClearRear = value;
            if (!ok || side == 0)
                stop.forcePosition = false;
        }
    }
    if (stop.request) {
        int pickUp = 25, setDown = 25;
        for (const QVector<OrTimetable::Command> *list : {&station, &cell})
            for (const OrTimetable::Command &command : *list)
                if (command.name == "req")
                    for (const OrTimetable::Qualifier &qualifier : command.qualifiers) {
                        bool ok = false;
                        const int value = qualifier.values.isEmpty() ? -1 : qualifier.values.first().toInt(&ok);
                        if (ok && qualifier.name == "pu")
                            pickUp = value;
                        else if (ok && qualifier.name == "pd")
                            setDown = value;
                    }
        const QString key = train + "@" + stop.platform;
        stop.needed = (pickUp > 0 && drawOf(key + "/pu") <= pickUp) || (setDown > 0 && drawOf(key + "/pd") <= setDown);
    }
}

// Task 08, phase 8: where a timetable train's stops stand on its route's
// way (TTTrain.CalculateStationStopPosition): the platform's end 7 m short
// of the last signal inside it where it has none at its end
// (`$restrictplatformtosignal`), the stop centred on that; at the
// platform's start end (its rear 15 m, half Open Rails' 30 m clearing,
// inside it) where its route has no junction before it, else 2.5 m short
// of the end where the route has none after it, else 15 m short
// (`$terminal`); so much kept clear ahead (`$keepclear /front`: no more
// than the train leaves, all of it with /force) or behind (`/rear`: up to
// 30 m and 1 m short of the signal at the platform's end; past it with
// /force).
void placeStops(const SimulationInput &input, TrainPlan &plan) {
    if (input.network == nullptr)
        return;
    const std::vector<TrackSection> &sections = input.network->sections();
    for (StopPlan &stop : plan.stops) {
        if (!stop.terminal && !stop.restrictToSignal && stop.keepClearFront < 0.0 && stop.keepClearRear < 0.0)
            continue;
        if (size_t(stop.part) >= plan.projection.size() || size_t(stop.part) >= plan.route.parts.size())
            continue;
        const Projection &way = plan.projection[size_t(stop.part)];
        const double partLength = plan.route.parts[size_t(stop.part)].length();
        // The signal at the platform's end: one in the section its end is in.
        double sectionEnd = stop.platformEnd;
        for (const ProjectedSection &pass : way.sections)
            if (pass.enter <= stop.platformEnd + 1e-6 && pass.leave >= stop.platformEnd - 1e-6) {
                sectionEnd = pass.leave;
                break;
            }
        double endSignal = -1.0;
        for (const ProjectedSignal &signal : way.stopSignals)
            if (signal.at >= stop.platformEnd - 0.5 && signal.at <= sectionEnd + 0.5) {
                endSignal = signal.at;
                break;
            }
        const double length = plan.length;
        const auto centred = [&] {
            return std::clamp(stop.platformEnd - 0.5 * (stop.platformEnd - stop.platformStart - length), 0.0,
                              stop.platformEnd);
        };
        if (stop.restrictToSignal && endSignal < 0.0) {
            double inside = -1.0;
            for (const ProjectedSignal &signal : way.stopSignals)
                if (signal.at > stop.platformStart + 0.5 && signal.at < stop.platformEnd - 0.5)
                    inside = signal.at;
            if (inside > 0.0) {
                stop.platformEnd = inside - 7.0;
                endSignal = inside;
                stop.front = centred();
            }
        }
        if (stop.terminal) {
            bool junctionBefore = false, junctionAfter = false;
            for (const ProjectedSection &pass : way.sections) {
                const bool junction = sections[size_t(pass.section)].kind == TrackSection::Junction;
                junctionBefore = junctionBefore || (junction && pass.leave <= stop.platformStart + 1e-6);
                junctionAfter = junctionAfter || (junction && pass.enter >= stop.platformEnd - 1e-6);
            }
            stop.front = !junctionBefore ? stop.platformStart + 15.0 + length
                    : !junctionAfter ? stop.platformEnd - 2.5 : stop.platformEnd - 15.0;
        } else if (stop.keepClearFront >= 0.0) {
            const double clear = stop.forcePosition
                    ? stop.keepClearFront
                    : std::min(stop.keepClearFront, stop.platformEnd - length - stop.platformStart);
            if (stop.forcePosition || clear > 0.0)
                stop.front = stop.platformEnd - clear;
            stop.forcePosition = false;
        } else if (stop.keepClearRear >= 0.0) {
            if (stop.forcePosition) {
                stop.front = stop.platformStart + stop.keepClearRear + length;
            } else {
                const double end = endSignal >= 0.0
                        ? endSignal - (stop.signalClearing >= 0.0 ? stop.signalClearing : 30.0) - 1.0
                        : stop.platformEnd;
                stop.front = std::min(stop.platformStart + stop.keepClearRear + length, end);
            }
        }
        stop.front = std::clamp(stop.front, 0.0, partLength);
    }
}

// Where train `a`'s route, from route distance `at` on its part `part` on,
// joins train `b`'s again after running apart (Open Rails'
// TTTrain.ProcessWaitRequest for `$follow`): the first section of each
// stretch both run the same way after one they do not share; none where
// the first section they share is run the other way. `key` tells a
// section's way: along a track; through a junction or crossing from its
// input or towards it (from either branch: Open Rails' direction). For
// `$wait` (phase 8): sections shared either way (`anyWay`), the first point
// only (`single`).
std::vector<FollowPlan> followPoints(const SimulationInput &input, const TrainPlan &a, int part, double at,
                                     const TrainPlan &b, bool anyWay = false, bool single = false) {
    std::vector<FollowPlan> points;
    if (input.network == nullptr)
        return points;
    const std::vector<TrackSection> &sections = input.network->sections();
    const auto key = [&](const ProjectedSection &pass) {
        return sections[size_t(pass.section)].kind == TrackSection::Track ? (pass.forward ? 1 : 0)
                                                                           : (pass.pinIn == 0 ? 2 : 3);
    };
    struct Theirs {
        int part = 0;
        double leave = 0.0;
        int key = 0;
    };
    std::unordered_map<int, Theirs> theirs;
    for (size_t p = 0; p < b.projection.size(); ++p)
        for (const ProjectedSection &pass : b.projection[p].sections)
            theirs.emplace(pass.section, Theirs{int(p), pass.leave, key(pass)});
    struct Ours {
        int part = 0;
        const ProjectedSection *pass = nullptr;
    };
    std::vector<Ours> ours;
    for (size_t p = size_t(std::max(0, part)); p < a.projection.size(); ++p)
        for (const ProjectedSection &pass : a.projection[p].sections)
            if (int(p) > part || pass.leave > at)
                ours.push_back(Ours{int(p), &pass});
    // The way the first shared section is run decides (Open Rails'
    // sameDirection); `$follow` only the same way.
    size_t i = 0;
    while (i < ours.size() && theirs.find(ours[i].pass->section) == theirs.end())
        ++i;
    if (i >= ours.size())
        return points;
    const bool sameWay = theirs.at(ours[i].pass->section).key == key(*ours[i].pass);
    if (!sameWay && !anyWay)
        return points;
    const auto shared = [&](size_t k) {
        auto it = theirs.find(ours[k].pass->section);
        return it != theirs.end() && (!sameWay || it->second.key == key(*ours[k].pass));
    };
    i = 0;
    while (i < ours.size() && shared(i))
        ++i;
    while (i < ours.size()) {
        while (i < ours.size() && !shared(i))
            ++i;
        if (i >= ours.size())
            break;
        const Theirs &there = theirs.at(ours[i].pass->section);
        FollowPlan point;
        point.train = b.id;
        point.section = ours[i].pass->section;
        point.part = ours[i].part;
        point.at = ours[i].pass->enter;
        point.otherPart = there.part;
        point.otherLeave = there.leave;
        points.push_back(point);
        if (single)
            break;
        while (i < ours.size() && shared(i))
            ++i;
    }
    return points;
}

// The track a train covers standing with its rear at its path's start (a
// timetable's static train, Open Rails' placing): from its front back.
std::vector<RouteInterval> standingTrack(TDB *tdb, Path *path, double length) {
    std::vector<RouteInterval> track;
    const TrainRoute route = BuildRoute(tdb, path->nodes(), path->legs(), 0.0);
    if (route.parts.empty())
        return track;
    const RouteTrack &part = route.parts.front();
    const double from = route.pathStart, to = std::min(route.pathStart + length, part.length());
    for (const RouteInterval &interval : part.intervals()) {
        const double a = std::max(from, interval.start), b = std::min(to, interval.start + interval.length());
        if (b <= a)
            continue;
        const float sign = interval.forward() ? 1.0f : -1.0f;
        RouteInterval piece;
        piece.node = interval.node;
        piece.from = interval.from + sign * float(a - interval.start);
        piece.to = interval.from + sign * float(b - interval.start);
        track.insert(track.begin(), RouteInterval{piece.node, piece.to, piece.from, 0.0});
    }
    return track;
}

}

QVector<PathStation> TimetablePathStations(TDB *tdb, Path *path, const QStringList &keys) {
    QVector<PathStation> out;
    if (tdb == nullptr || path == nullptr)
        return out;
    const QVector<PathTrack::Leg> &legs = path->legs();
    if (legs.isEmpty())
        return out;
    // Each with the distance down the path it is met at.
    QVector<QPair<double, PathStation>> met;
    QSet<QString> passed;
    for (const Path::PathObject *pass : path->pathObjects) {
        if (pass == nullptr || pass->items.isEmpty())
            continue;
        double down = 1e12;
        for (const QPair<int, float> &item : pass->items)
            down = std::min(down, double(item.second));
        const QString name = pass->name.trimmed();
        met.push_back({down, PathStation{name, name.toLower()}});
        passed.insert(name.toLower());
    }
    double length = 0.0;
    for (const PathTrack::Leg &leg : legs)
        length += std::fabs(double(leg.to) - double(leg.from));
    const PathTrack::Leg &first = legs.first(), &last = legs.last();
    for (const QString &key : keys) {
        if (key.isEmpty() || passed.contains(key))
            continue;
        float nearEnd = 0.0f, farEnd = 0.0f;
        const TRitem *platform = first.broken ? nullptr
                : platformBeyond(tdb, first.track, first.from, first.forward() ? -1.0f : 1.0f, key, nearEnd, farEnd);
        double down = -1.0;
        if (platform == nullptr && !last.broken) {
            platform = platformBeyond(tdb, last.track, last.to, last.forward() ? 1.0f : -1.0f, key, nearEnd, farEnd);
            down = length + 1.0;
        }
        if (platform == nullptr)
            platform = reversalPlatform(tdb, legs, key, [&down](const TRitem *, double itemDown, double) {
                down = itemDown;
                return true;
            });
        if (platform != nullptr) {
            met.push_back({down, PathStation{platform->stationName.trimmed(), key}});
            passed.insert(key);
        }
    }
    std::stable_sort(met.begin(), met.end(),
                     [](const QPair<double, PathStation> &a, const QPair<double, PathStation> &b) { return a.first < b.first; });
    QSet<QString> listed;
    for (const QPair<double, PathStation> &one : met)
        if (!one.second.key.isEmpty() && !listed.contains(one.second.key)) {
            listed.insert(one.second.key);
            out.push_back(one.second);
        }
    return out;
}

bool TimetableTrainDrivable(const OrTimetable::Train &train) {
    const QString consists = Game::root + "/TRAINS/CONSISTS";
    for (const OrTimetable::ConsistPart &part : train.consists) {
        const int id = ConLib::addCon(consists + "/", OrTimetable::ConsistFile(part.name));
        auto found = ConLib::con.find(id);
        const Consist *consist = found != ConLib::con.end() ? found->second : nullptr;
        if (consist == nullptr || consist->loaded != 1)
            continue;
        for (const Consist::EngItem &item : consist->engItems)
            if (const Eng *eng = engOf(item.eng); eng != nullptr && eng->definition->wagonTypeId >= 4)
                return true;
    }
    return false;
}

void TimetableTrains(SimulationInput &input, Activity *set, TDB *tdb) {
    const QString routeDirectory = ActivityTrains::RouteDirectory(set);
    const QString consists = Game::root + "/TRAINS/CONSISTS";
    int traffic = 0;
    // What the trains made name of other trains, for the second pass:
    // `$triggers` and `$forms`, where each `$follow` and `$wait` applies
    // from (with their qualifiers), `$activate` (when), `$create /ahead`.
    struct Follow {
        QString train;
        int part = 0;
        double at = 0.0;
        bool wait = false;
        FollowPlan qualifiers;
    };
    struct Activate {
        QString train;
        TrainPlan::Activation when = TrainPlan::Activation::Start;
        QString station;
    };
    struct Made {
        int plan = -1;
        const OrTimetable *timetable = nullptr;
        QString triggers;
        bool forms = false;
        bool activated = false;
        QString ahead;
        std::vector<Follow> follows;
        std::vector<Activate> activates;
    };
    // A `$follow` or `$wait` command's qualifiers (minutes in the
    // timetable): /maxdelay, /owndelay, /notstarted.
    const auto followOf = [](const OrTimetable::Command &command, const QString &other, int part, double at) {
        Follow follow{other, part, at, command.name == "wait", FollowPlan()};
        bool ok = false;
        if (const QStringList maxDelay = command.qualifier("maxdelay"); !maxDelay.isEmpty()) {
            const int minutes = maxDelay.first().toInt(&ok);
            if (ok)
                follow.qualifiers.maxDelay = minutes * 60;
        }
        if (const QStringList ownDelay = command.qualifier("owndelay"); !ownDelay.isEmpty()) {
            const int minutes = ownDelay.first().toInt(&ok);
            if (ok)
                follow.qualifiers.ownDelay = minutes * 60;
        }
        follow.qualifiers.notStarted = command.has("notstarted");
        return follow;
    };
    std::vector<Made> made;
    for (const std::shared_ptr<OrTimetable> &timetable : set->timetables) {
        for (const OrTimetable::Train &train : timetable->trains) {
            const int index = traffic++;
            const bool player = !train.isStatic && !set->playerTrain.isEmpty()
                    && train.name.compare(set->playerTrain, Qt::CaseInsensitive) == 0;
            const QString name = player ? QString("%1 (player)").arg(train.name) : train.name;
            std::vector<ConsistPart> parts;
            bool whole = !train.consists.isEmpty();
            for (const OrTimetable::ConsistPart &part : train.consists) {
                const int id = ConLib::addCon(consists + "/", OrTimetable::ConsistFile(part.name));
                auto found = ConLib::con.find(id);
                const Consist *consist = found != ConLib::con.end() ? found->second : nullptr;
                if (consist == nullptr || consist->loaded != 1 || consist->engItems.isEmpty()) {
                    input.warnings << QString("%1: no consist %2").arg(name, part.name);
                    whole = false;
                    break;
                }
                parts.push_back(ConsistPart{consist, part.reverse});
            }
            Path *path = ActLib::GetPathByName(train.pathName(), routeDirectory);
            if (!whole || path == nullptr || path->nodes().size() < 2) {
                if (path == nullptr || path->nodes().size() < 2)
                    input.warnings << QString("%1: no path %2").arg(name, train.path);
                continue;
            }
            TrainPlan plan;
            plan.id = int(input.trains.size());
            plan.player = player;
            plan.service = train.name;
            plan.serviceTime = player ? -1 : train.startTime;
            plan.timetableTrain = true;
            plan.traffic = player ? -1 : index;
            plan.consist = train.consist;
            vehiclesOf(input, plan, parts, player, name, !train.isStatic);
            // A static train (Open Rails: from the timetable's start, its
            // rear at its path's start): a standing train of the core.
            if (train.isStatic) {
                StaticConsist standing;
                standing.name = train.name;
                standing.vehicles = plan.vehicles;
                standing.length = float(plan.length);
                standing.track = standingTrack(tdb, path, plan.length);
                if (!standing.track.empty())
                    input.staticConsists.push_back(standing);
                else
                    input.warnings << QString("%1: a static train of no length (its vehicles not read) or off its "
                                              "path's track, not placed").arg(name);
                continue;
            }
            // The timetable's #speed row: a lower top speed; its #note's
            // factors on the rates (`$acc`, `$dec`).
            if (train.speed > 0.0f)
                plan.maxSpeed = std::min(plan.maxSpeed, train.speed);
            plan.efficiency = 1.0f;
            plan.drivingEfficiency = player ? 0.9f : plan.efficiency;
            if (const OrTimetable::Command *acc = commandIn(train.notes, "acc"); acc != nullptr && !acc->values.isEmpty())
                plan.accelerationFactor = std::max(0.01f, acc->values.first().toFloat());
            if (const OrTimetable::Command *dec = commandIn(train.notes, "dec"); dec != nullptr && !dec->values.isEmpty())
                plan.decelerationFactor = std::max(0.01f, dec->values.first().toFloat());
            SetRates(plan, std::min(input.routeSpeedLimit, plan.maxSpeed * plan.efficiency));
            // The speed row's `$cruise` (in its unit, as `$max`) and
            // `$maxdelay` (minutes): its top speed while on time (phase 8).
            const float speedUnit = timetable->speedRow == "#speedmph" ? 0.44704f
                    : timetable->speedRow == "#speedkph" ? 1.0f / 3.6f : 1.0f;
            if (const OrTimetable::Command *cruise = commandIn(train.speedCommands, "cruise");
                    cruise != nullptr && !cruise->values.isEmpty() && cruise->values.first().toFloat() > 0.0f)
                plan.cruiseSpeed = cruise->values.first().toFloat() * speedUnit;
            if (const OrTimetable::Command *maxDelay = commandIn(train.speedCommands, "maxdelay");
                    maxDelay != nullptr && !maxDelay->values.isEmpty())
                plan.cruiseMaxDelay = maxDelay->values.first().toInt() * 60;
            // Times past midnight as Open Rails takes them: the next day's
            // for a train started with `$next`, and a time before 08:00 of a
            // train starting after 16:00 (TTTrain's rule).
            const int nextDay = commandIn(train.startCommands, "next") != nullptr ? 86400 : 0;
            const auto when = [&](int time) {
                if (time < 0)
                    return time;
                if (train.startTime >= 16 * 3600 && time < 8 * 3600)
                    time += 86400;
                return time + nextDay;
            };
            plan.startTime = std::max(0, train.startTime) + nextDay;
            // `$create[=time]`: put on its track standing from then (before
            // the first train's start without a time).
            if (const OrTimetable::Command *create = commandIn(train.startCommands, "create")) {
                const int time = create->values.isEmpty() ? 0 : when(OrTimetable::ParseTime(create->values.first()));
                if (time >= 0 && time < plan.startTime)
                    plan.createTime = time;
            }
            if (!routeOf(input, tdb, plan, path, name))
                continue;
            // #dispose: the last of its forming commands counts (Open Rails):
            // `$static` stays at its path's end, `$triggers` forms that train,
            // `$forms` too, of this one's vehicles (phase 8); `$activate`
            // starts one when its run ends.
            Made one;
            one.timetable = timetable.get();
            for (const OrTimetable::Command &command : train.dispose) {
                if (command.name == "static") {
                    plan.staysAtEnd = true;
                    one.triggers.clear();
                } else if (command.name == "triggers" || command.name == "forms" || command.name == "stable"
                           || command.name == "pool") {
                    plan.staysAtEnd = false;
                    one.forms = command.name == "forms";
                    one.triggers = (command.name == "triggers" || command.name == "forms") && !command.values.isEmpty()
                            ? command.values.first() : QString();
                } else if (command.name == "activate" && !command.values.isEmpty()) {
                    one.activates.push_back(Activate{command.values.first(), TrainPlan::Activation::End, QString()});
                }
            }
            // #start's `$activated` (started by another's `$activate`) and
            // `$create /ahead=` (put on its track ahead of that train).
            one.activated = commandIn(train.startCommands, "activated") != nullptr;
            if (const OrTimetable::Command *create = commandIn(train.startCommands, "create"))
                if (const QStringList ahead = create->qualifier("ahead"); !ahead.isEmpty())
                    one.ahead = ahead.first();
            // `$follow` and `$wait` in #note: from its path's start;
            // `$activate` there when it starts.
            for (const OrTimetable::Command &command : train.notes) {
                if (command.name == "follow" || command.name == "wait")
                    for (const QString &other : command.values)
                        one.follows.push_back(followOf(command, other, 0, plan.route.pathStart));
                else if (command.name == "activate" && !command.values.isEmpty())
                    one.activates.push_back(Activate{command.values.first(), TrainPlan::Activation::Start, QString()});
            }
            // Station stops: each timetable stop at the path's first pass by
            // a platform of its station, in the path's order; a passing
            // time is no stop. Standing at least the booked dwell, or the
            // platform's least waiting time without one, and no longer
            // than that least time when late (Open Rails' timetable trains,
            // TTTrain.ComputeTrainBoardingTime).
            std::vector<bool> used(size_t(train.stops.size()), false);
            const auto book = [&](StopPlan &stop, const OrTimetable::Stop &booked, int least) {
                stop.arrival = when(booked.timing.arrival);
                stop.departure = when(booked.timing.departure);
                stop.platform = timetable->stations[booked.station].name;
                // `*` and `x` (phase 8): it may leave before its time; a
                // request stop; the least time standing (`$stoptime`).
                stop.departsEarly = booked.timing.early;
                stop.request = booked.timing.request;
                stopCommands(stop, least, booked.commands, timetable->stations[booked.station].commands, train.name);
                const int dwell = stop.arrival >= 0 && stop.departure >= 0 ? stop.departure - stop.arrival : 0;
                stop.boarding = dwell > 0 ? std::min(dwell, least) : least;
                stop.efficiency = plan.efficiency;
                // Open Rails' timetable mode: an exit signal at any distance
                // on the platform's node, not held, waited for before
                // leaving, unless the station's row or the cell says
                // otherwise.
                stop.exit = StopPlan::Exit::Timetable;
                stop.holdsExit = false;
                stop.waitsForSignal = true;
                stopSwitches(stop, timetable->stations[booked.station].commands);
                stopSwitches(stop, booked.commands);
            };
            for (const Path::PathObject *pass : path->pathObjects) {
                if (pass == nullptr)
                    continue;
                const QString key = pass->name.trimmed().toLower();
                int k = -1;
                for (int s = 0; s < train.stops.size() && k < 0; ++s)
                    if (!used[size_t(s)] && timetable->stations[train.stops[s].station].key == key)
                        k = s;
                if (k < 0)
                    continue;
                used[size_t(k)] = true;
                const OrTimetable::Stop &booked = train.stops[k];
                if (booked.timing.arrival < 0 && booked.timing.departure < 0 && !booked.timing.request
                        && !booked.timing.early)
                    continue;
                StopPlan stop;
                if (!stopAt(plan, *pass, stop)) {
                    input.warnings << QString("%1: the stop at %2 is beyond its route").arg(name, pass->name);
                    continue;
                }
                const TRitem *item = platformItem(tdb, pass->trItemId);
                book(stop, booked, item != nullptr ? item->platformMinWaitingTime : 0);
                // Its doors open on the platform's side (task shapes 12),
                // the side read from the platform's end the train leaves it
                // at (the end it meets first opened them on the track's
                // side: Prudnik's 0749 at Katowice); Open Rails' timetable
                // trains open none.
                stop.platformItem = pass->trItemId;
                plan.stops.push_back(stop);
            }
            // A station not passed with a platform behind the path's start
            // or beyond its end, in the track section there: on the route
            // in Open Rails. Behind the start the train is at the station
            // from the start (its rear past the platform's end,
            // TTTrain.CheckStationPosition) and stands there until it
            // departs; beyond the end it runs on to the platform and stops;
            // one the path reverses inside, it stops at before reversing.
            const QVector<PathTrack::Leg> &legs = path->legs();
            for (int s = 0; s < train.stops.size(); ++s) {
                const OrTimetable::Stop &booked = train.stops[s];
                if (used[size_t(s)] || (booked.timing.arrival < 0 && booked.timing.departure < 0
                                        && !booked.timing.request && !booked.timing.early))
                    continue;
                const QString key = timetable->stations[booked.station].key;
                float nearEnd = 0.0f, farEnd = 0.0f;
                const PathTrack::Leg &first = legs.first(), &last = legs.last();
                RouteTrack &end = plan.route.parts.back();
                const TRitem *platform = first.broken ? nullptr
                        : platformBeyond(tdb, first.track, first.from, first.forward() ? -1.0f : 1.0f, key, nearEnd, farEnd);
                const bool atStart = platform != nullptr;
                if (!atStart && !plan.route.broken && !last.broken && !end.intervals().empty()
                        && end.intervals().back().node == last.track
                        && std::fabs(end.intervals().back().to - last.to) < PathTrack::SamePlace)
                    platform = platformBeyond(tdb, last.track, last.to, last.forward() ? 1.0f : -1.0f, key, nearEnd, farEnd);
                if (platform == nullptr) {
                    StopPlan stop;
                    if (const TRitem *inside = reversalInside(tdb, legs, key, plan, stop)) {
                        used[size_t(s)] = true;
                        book(stop, booked, inside->platformMinWaitingTime);
                        stop.platformItem = int(inside->trItemId);
                        plan.stops.insert(std::find_if(plan.stops.begin(), plan.stops.end(),
                                                       [&stop](const StopPlan &other) {
                                                           return other.part > stop.part
                                                               || (other.part == stop.part && other.front > stop.front);
                                                       }),
                                          stop);
                        continue;
                    }
                    input.warnings << QString("%1: the stop at %2 is not on its path")
                                          .arg(name, timetable->stations[booked.station].name);
                    continue;
                }
                used[size_t(s)] = true;
                StopPlan stop;
                if (atStart) {
                    stop.part = 0;
                    stop.front = stop.platformEnd = stop.platformStart = plan.startFront;
                } else {
                    stop.part = int(plan.route.parts.size()) - 1;
                    stop.platformStart = end.length() + std::fabs(double(nearEnd) - double(last.to));
                    end.append(last.track, last.to, farEnd);
                    stop.platformEnd = end.length();
                    stop.front = std::clamp(stop.platformEnd - 0.5 * (stop.platformEnd - stop.platformStart - plan.length),
                                            0.0, stop.platformEnd);
                }
                book(stop, booked, platform->platformMinWaitingTime);
                stop.platformItem = int(platform->trItemId);
                plan.stops.insert(atStart ? plan.stops.begin() : plan.stops.end(), stop);
            }
            for (size_t k = 0; k < plan.stops.size(); ++k)
                plan.stops[k].entry = int(k);
            // `$follow` and `$wait` in a station's cell: from where its path
            // passes the station's platform (its path's start where it does
            // not); `$activate` there when it arrives (`/depart`: leaves).
            for (const OrTimetable::Stop &cell : train.stops) {
                for (const OrTimetable::Command &command : cell.commands)
                    if (command.name == "activate" && !command.values.isEmpty())
                        one.activates.push_back(Activate{command.values.first(),
                                                         command.has("depart") ? TrainPlan::Activation::Departure
                                                                               : TrainPlan::Activation::Arrival,
                                                         timetable->stations[cell.station].name});
                const OrTimetable::Command *follow = commandIn(cell.commands, "follow");
                const OrTimetable::Command *wait = commandIn(cell.commands, "wait");
                if (follow == nullptr && wait == nullptr)
                    continue;
                int part = 0;
                double at = plan.route.pathStart;
                const QString key = timetable->stations[cell.station].key;
                for (const Path::PathObject *pass : path->pathObjects)
                    if (pass != nullptr && pass->name.trimmed().toLower() == key && !pass->items.isEmpty()) {
                        double down = 1e12;
                        for (const QPair<int, float> &met : pass->items)
                            down = std::min(down, double(met.second));
                        if (!plan.route.place(down, part, at)) {
                            part = 0;
                            at = plan.route.pathStart;
                        }
                        break;
                    }
                for (const OrTimetable::Command *command : {follow, wait})
                    if (command != nullptr)
                        for (const QString &other : command->values)
                            one.follows.push_back(followOf(*command, other, part, at));
            }
            finishTrain(input, tdb, plan, path);
            placeStops(input, plan);
            one.plan = plan.id;
            made.push_back(one);
            input.trains.push_back(std::move(plan));
        }
    }
    // The second pass: trains named by others (in the same timetable).
    const auto named = [&](const Made &by, const QString &name) -> int {
        const QString key = name.trimmed();
        for (const Made &other : made)
            if (other.timetable == by.timetable
                    && input.trains[size_t(other.plan)].service.compare(key, Qt::CaseInsensitive) == 0)
                return other.plan;
        return -1;
    };
    for (const Made &one : made) {
        TrainPlan &plan = input.trains[size_t(one.plan)];
        // `$triggers`: that train is formed when this one's run ends;
        // `$forms` of this one's vehicles.
        if (!one.triggers.isEmpty()) {
            const int formed = named(one, one.triggers);
            if (formed >= 0 && input.trains[size_t(formed)].formedBy < 0) {
                input.trains[size_t(formed)].formedBy = plan.id;
                input.trains[size_t(formed)].formedOfVehicles = one.forms;
            } else {
                input.warnings << QString("%1: $%2=%3 forms no train")
                                      .arg(plan.service, one.forms ? "forms" : "triggers", one.triggers);
            }
        }
        // `$follow` and `$wait`: where its route joins that train's again.
        for (const Follow &follow : one.follows) {
            const int other = named(one, follow.train);
            if (other < 0 || other == plan.id) {
                input.warnings << QString("%1: $%2=%3 follows no train")
                                      .arg(plan.service, follow.wait ? "wait" : "follow", follow.train);
                continue;
            }
            for (FollowPlan point : followPoints(input, plan, follow.part, follow.at, input.trains[size_t(other)],
                                                 follow.wait, follow.wait)) {
                point.maxDelay = follow.qualifiers.maxDelay;
                point.ownDelay = follow.qualifiers.ownDelay;
                point.notStarted = follow.qualifiers.notStarted;
                plan.follows.push_back(point);
            }
        }
        // `$activate`: that train, `$activated`, starts when this one
        // starts, arrives at or leaves that stop, or ends.
        for (const Activate &activate : one.activates) {
            const int other = named(one, activate.train);
            const auto activated = std::find_if(made.begin(), made.end(),
                                                [other](const Made &m) { return m.plan == other; });
            if (other < 0 || activated == made.end() || !activated->activated) {
                input.warnings << QString("%1: $activate=%2 activates no train ($activated)")
                                      .arg(plan.service, activate.train);
                continue;
            }
            TrainPlan &target = input.trains[size_t(other)];
            if (target.activatedBy >= 0)
                continue;
            int stop = -1;
            for (size_t k = 0; k < plan.stops.size() && stop < 0 && !activate.station.isEmpty(); ++k)
                if (plan.stops[k].platform == activate.station)
                    stop = int(k);
            if (!activate.station.isEmpty() && stop < 0) {
                input.warnings << QString("%1: $activate=%2 at %3, no stop there")
                                      .arg(plan.service, activate.train, activate.station);
                continue;
            }
            target.activatedBy = plan.id;
            target.activation = activate.when;
            target.activationStop = stop;
        }
        // `$create /ahead=`: put on its track ahead of that train.
        if (!one.ahead.isEmpty()) {
            const int other = named(one, one.ahead);
            if (other >= 0 && other != plan.id)
                plan.createAhead = other;
            else
                input.warnings << QString("%1: $create /ahead=%2 names no train").arg(plan.service, one.ahead);
        }
        std::sort(plan.follows.begin(), plan.follows.end(), [](const FollowPlan &a, const FollowPlan &b) {
            return a.part != b.part ? a.part < b.part : a.at < b.at;
        });
    }
    // `$activated` and nothing activates it: it starts at its time (Open
    // Rails, with a message).
    for (const Made &one : made)
        if (one.activated && input.trains[size_t(one.plan)].activatedBy < 0)
            input.warnings << QString("%1: $activated, but no train activates it: it starts at its time")
                                  .arg(input.trains[size_t(one.plan)].service);
}

std::shared_ptr<SimulationInput> MakeInput(Activity *activity, TDB *tdb, float routeSpeedLimit,
                                                 std::shared_ptr<const TrackGeometry> geometry,
                                                 std::shared_ptr<const TrackNetwork> network, float restrictedSpeed,
                                                 bool work) {
    auto input = std::make_shared<SimulationInput>();
    input->runId = nextRun++;
    input->routeSpeedLimit = routeSpeedLimit > 0.0f ? routeSpeedLimit : 44.4444f;
    if (restrictedSpeed > 0.0f)
        input->restrictedSpeed = restrictedSpeed;
    input->geometry = geometry != nullptr ? geometry : std::make_shared<const TrackGeometry>(tdb);
    if (network == nullptr) {
        bool openRails = false;
        const std::shared_ptr<const SigCfg> config = SimulationSigCfg(tdb, &openRails);
        network = std::make_shared<const TrackNetwork>(tdb, *input->geometry, config.get(), openRails);
    }
    input->network = network;
    // The signal scripts' problems (task 05), for the user.
    input->warnings << network->scriptWarnings;
    if (activity == nullptr || tdb == nullptr)
        return input;
    if (activity->header != nullptr) {
        input->activity = activity->header->name;
        input->startTime = activity->header->startTime[0] * 3600 + activity->header->startTime[1] * 60
                + activity->header->startTime[2];
    }
    // An Open Rails timetable set (task 08): its trains, the chosen one the
    // player's (its start the run's); no events, zones or work orders yet.
    if (activity->isTimetables()) {
        TimetableTrains(*input, activity, tdb);
        for (const TrainPlan &plan : input->trains)
            if (plan.player)
                input->startTime = plan.startTime;
        return input;
    }
    const QString routePath = ContentPath::parentDirectory(activity->path);
    // The activity's restricted speed zones: their two ends on the track.
    for (ActivityObject *object : activity->restrictedSpeedZone) {
        float start[4], end[4];
        if (object == nullptr || !object->getSpeedZoneEnds(start, end))
            continue;
        SimulationInput::Zone zone;
        if (placeOnTrack(tdb, start, zone.node[0], zone.distance[0])
                && placeOnTrack(tdb, end, zone.node[1], zone.distance[1]))
            input->zones.push_back(zone);
    }
    for (const ActivityTrains::Train &train : ActivityTrains::List(activity)) {
        if (train.kind == ActivityTrains::PatternOnly)
            continue;
        const bool player = train.kind == ActivityTrains::Player;
        const QString name = player ? QString("%1 (player)").arg(train.service)
                                    : QString("%1 %2").arg(train.service).arg(train.time);
        Service *service = ActLib::GetServiceByName(train.service, routePath);
        Path *path = service != nullptr ? ActLib::GetPathByName(service->pathId, routePath) : nullptr;
        if (service == nullptr || path == nullptr || path->nodes().size() < 2) {
            input->warnings << QString("%1: %2").arg(name, service == nullptr ? "no service" : "no path");
            continue;
        }
        const int consistId = ConLib::addCon(Game::root + "/TRAINS/CONSISTS/", service->trainConfig + ".con");
        auto found = ConLib::con.find(consistId);
        const Consist *consist = found != ConLib::con.end() ? found->second : nullptr;
        if (consist == nullptr || consist->loaded != 1 || consist->engItems.isEmpty()) {
            input->warnings << QString("%1: no consist %2").arg(name, service->trainConfig);
            continue;
        }
        TrainPlan plan;
        plan.id = int(input->trains.size());
        plan.player = player;
        plan.service = train.service;
        plan.serviceTime = player ? -1 : train.time;
        plan.traffic = player ? -1 : train.index;
        plan.consist = service->trainConfig;
        vehiclesOf(*input, plan, {ConsistPart{consist, false}}, player, name);
        plan.efficiency = service->efficiency > 0.0f ? service->efficiency : 1.0f;
        plan.drivingEfficiency = player ? 0.9f : plan.efficiency;
        SetRates(plan, std::min(input->routeSpeedLimit, plan.maxSpeed * plan.efficiency));
        ActivityServiceDefinition *definition = ActivityTrains::Definition(activity, train);
        if (definition != nullptr && !definition->efficiency.isEmpty() && definition->efficiency.last() > 0.0f)
            plan.efficiency = definition->efficiency.last();
        plan.startTime = player ? input->startTime : train.time;
        plan.startSpeed = std::max(0.0f, service->startingSpeed);

        if (!routeOf(*input, tdb, plan, path, name))
            continue;

        // Station stops: the timetable's, each at its pass of the platform.
        const ActivityTimetable *timetable = train.timetable;
        if (timetable != nullptr) {
            const int count = std::min({timetable->platformStartID.size(), timetable->arrivalTime.size(),
                                        timetable->departTime.size()});
            QVector<Service::StationStop> stops;
            for (int i = 0; i < count; ++i) {
                Service::StationStop stop;
                stop.platformStartID = timetable->platformStartID[i];
                stop.skipCount = i < timetable->skipCount.size() ? timetable->skipCount[i] : 0;
                stop.distanceDownPath = i < timetable->distanceDownPath.size() ? timetable->distanceDownPath[i] : 0;
                stops.push_back(stop);
            }
            QVector<Path::PathObject> passes;
            for (const Path::PathObject *pass : path->pathObjects)
                if (pass != nullptr)
                    passes.push_back(*pass);
            const QVector<int> passOf = ServiceStops::Passes(stops, passes);
            int passengerCars = 0;
            for (const Vehicle &vehicle : plan.vehicles)
                passengerCars += vehicle.type() == 1 ? 1 : 0;
            for (int i = 0; i < count; ++i) {
                const TRitem *item = platformItem(tdb, stops[i].platformStartID);
                const QString platform = item != nullptr ? item->stationName : QString::number(stops[i].platformStartID);
                if (passOf[i] < 0 || passOf[i] >= passes.size()) {
                    input->warnings << QString("%1: the stop at %2 is not on the path").arg(name, platform);
                    continue;
                }
                StopPlan stop;
                if (!stopAt(plan, passes[passOf[i]], stop)) {
                    input->warnings << QString("%1: the stop at %2 is beyond its route").arg(name, platform);
                    continue;
                }
                const bool timed = timetable->arrivalTime[i] != 0 || timetable->departTime[i] != 0;
                stop.arrival = timed ? timetable->arrivalTime[i] : -1;
                stop.departure = timed ? timetable->departTime[i] : -1;
                const int passengers = passengersWaiting(activity, tdb, stops[i].platformStartID);
                stop.boarding = passengerCars > 0 ? std::max(20, 10 * passengers / passengerCars) : 20;
                stop.efficiency = definition != nullptr && i < definition->efficiency.size()
                        && definition->efficiency[i] > 0.0f ? definition->efficiency[i] : plan.efficiency;
                stop.platform = platform;
                stop.platformItem = stops[i].platformStartID;
                stop.entry = i;
                plan.stops.push_back(stop);
            }
        }

        // Waiting points: the path's wait nodes, where the front stops.
        const QVector<PathTrack::Node> &nodes = path->nodes();
        const QVector<PathTrack::Leg> &legs = path->legs();
        double down = 0.0;
        for (int k = 1; k < nodes.size() && k - 1 < legs.size(); ++k) {
            down += std::fabs(double(legs[k - 1].to) - double(legs[k - 1].from));
            if (nodes[k].type != PathTrack::NodeType::Wait)
                continue;
            WaitPlan wait;
            wait.value = nodes[k].waitTime;
            if (plan.route.place(down, wait.part, wait.front))
                plan.waits.push_back(wait);
        }
        finishTrain(*input, tdb, plan, path);
        input->trains.push_back(std::move(plan));
    }
    input->staticConsists = StaticConsists(activity, tdb);
    input->events = Events(activity, tdb, *input, input->warnings);
    // The player's work orders, planned and run by the autopilot (task 07,
    // part B).
    if (work)
        ApplyWork(*input);
    return input;
}

}
