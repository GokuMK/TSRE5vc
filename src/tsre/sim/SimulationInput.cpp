#include <tsre/sim/SimulationInput.h>

#include <QDir>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/TRitem.h>
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
#include <tsre/trains/Path.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/ServiceStops.h>

namespace Sim {

namespace {

std::atomic<unsigned int> nextRun{1};

constexpr double PoundForce = 4.44822162;
constexpr double Mile = 0.44704;

// A vehicle's resistance as Davis coefficients (N, N/(m/s), N/(m/s)²):
// Open Rails' ORTSDavis values where the file has them; else MSTS's
// Friction, where its second speed is a low one (FCalc's), fitted by least
// squares from there to 80 mph; else the Davis equation for plain bearings
// (four axles, a frontal area of 10 m², the leading vehicle's air drag
// higher).
void resistanceOf(const Eng &eng, bool leading, double davis[3]) {
    if (eng.hasDavis) {
        std::copy(eng.davis, eng.davis + 3, davis);
        return;
    }
    const double *f = eng.friction;
    if (eng.hasFriction && f[2] >= 0.0 && f[2] <= 10.0 * Mile) {
        // Its force above the second speed.
        const auto force = [f](double v) {
            return f[0] * std::pow(f[2], f[1]) + f[3] * f[2] + f[3] * (std::pow(v, f[4] + 1.0) - std::pow(f[2], f[4] + 1.0));
        };
        // Normal equations of a + b v + c v².
        double m[3][4] = {};
        for (double v = std::max(f[2], 0.5); v <= 80.0 * Mile; v += 0.5) {
            const double row[3] = {1.0, v, v * v};
            const double y = force(v);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c)
                    m[r][c] += row[r] * row[c];
                m[r][3] += row[r] * y;
            }
        }
        for (int k = 0; k < 3; ++k) {
            for (int r = k + 1; r < 3; ++r) {
                const double scale = m[r][k] / m[k][k];
                for (int c = k; c < 4; ++c)
                    m[r][c] -= scale * m[k][c];
            }
        }
        for (int k = 2; k >= 0; --k) {
            double sum = m[k][3];
            for (int c = k + 1; c < 3; ++c)
                sum -= m[k][c] * davis[c];
            davis[k] = sum / m[k][k];
        }
        return;
    }
    const double tons = eng.massKg / 907.18474;
    const double areaFt2 = 10.0 / 0.09290304;
    davis[0] = (1.3 * tons + 29.0 * 4.0) * PoundForce;
    davis[1] = 0.045 * tons * PoundForce / Mile;
    davis[2] = (leading ? 0.0005 : 0.00035) * areaFt2 * PoundForce / (Mile * Mile);
}

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

float vehicleLength(int engId) {
    if (engId < 0 || Game::currentEngLib == nullptr)
        return 0.0f;
    auto e = Game::currentEngLib->eng.find(engId);
    return e != Game::currentEngLib->eng.end() && e->second != nullptr ? e->second->getFullWidth() : 0.0f;
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
            const float length = vehicleLength(object->con->engItems[v].eng);
            if (v == 0)
                first = length;
            consist.length += length;
            consist.cars.push_back(unsigned(object->id) * 65536u + unsigned(object->con->engItems[v].uid));
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

void SetRates(TrainPlan &train, float topSpeed) {
    // A freight wagon makes it a freight train (Open Rails).
    bool freight = false;
    for (const Vehicle &vehicle : train.vehicles)
        freight = freight || vehicle.type == 2;
    train.passenger = !freight;
    if (freight) {
        train.acceleration = 0.5f;
        train.deceleration = 0.5f;
        return;
    }
    train.acceleration = 1.0f;
    const float top = topSpeed >= 0.0f ? topSpeed : train.maxSpeed;
    train.deceleration = top > 55.0f ? 2.5f : top > 40.0f ? 1.5f : 1.0f;
    // An EMU: electric power cars with passengers at both ends.
    const auto powerCar = [](const Vehicle &vehicle) { return vehicle.type == 4 && vehicle.passengerCapacity; };
    if (train.deceleration == 1.0f && !train.vehicles.empty() && powerCar(train.vehicles.front())
            && powerCar(train.vehicles.back())) {
        train.acceleration = 1.5f;
        train.deceleration = 2.0f;
    }
}

std::shared_ptr<const SimulationInput> MakeInput(Activity *activity, TDB *tdb, float routeSpeedLimit,
                                                 std::shared_ptr<const TrackGeometry> geometry,
                                                 std::shared_ptr<const TrackNetwork> network, float restrictedSpeed) {
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
    const QString routePath = ContentPath::parentDirectory(activity->path);
    // The activity's restricted speed zones: their two ends on the track.
    struct Zone {
        int node[2];
        float distance[2];
    };
    std::vector<Zone> zones;
    for (ActivityObject *object : activity->restrictedSpeedZone) {
        float start[4], end[4];
        if (object == nullptr || !object->getSpeedZoneEnds(start, end))
            continue;
        Zone zone;
        if (placeOnTrack(tdb, start, zone.node[0], zone.distance[0])
                && placeOnTrack(tdb, end, zone.node[1], zone.distance[1]))
            zones.push_back(zone);
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
        float engineSpeed = 0.0f;
        QStringList missing;
        for (const Consist::EngItem &item : consist->engItems) {
            Vehicle vehicle;
            vehicle.flip = item.flip;
            vehicle.eng = item.eng;
            // The player's train is consist 0 of the wagon lists.
            if (player)
                vehicle.id = unsigned(item.uid);
            Eng *eng = nullptr;
            if (item.eng >= 0 && Game::currentEngLib != nullptr) {
                auto e = Game::currentEngLib->eng.find(item.eng);
                eng = e != Game::currentEngLib->eng.end() ? e->second : nullptr;
            }
            if (eng != nullptr) {
                vehicle.length = eng->getFullWidth();
                vehicle.type = eng->wagonTypeId;
                vehicle.passengerCapacity = eng->passengerCapacity;
                const float speed = float(eng->maxSpeedMps);
                if (eng->wagonTypeId >= 4 && speed > 0.0f)
                    engineSpeed = engineSpeed > 0.0f ? std::min(engineSpeed, speed) : speed;
            }
            plan.length += vehicle.length;
            plan.vehicles.push_back(vehicle);
            // Its share of the train's performance (task 03).
            if (eng == nullptr || eng->massKg <= 0.0) {
                const QString what = eng != nullptr ? eng->name : QString::number(item.eng);
                if (!missing.contains(what))
                    missing << what;
                continue;
            }
            Performance &p = plan.performance;
            p.mass += eng->massKg;
            if (eng->wagonTypeId >= 4) {
                p.force += eng->forceN;
                p.power += eng->powerW;
            }
            double davis[3] = {0.0, 0.0, 0.0};
            resistanceOf(*eng, plan.vehicles.size() == 1, davis);
            p.davisA += davis[0];
            p.davisB += davis[1];
            p.davisC += davis[2];
        }
        // Driven with force and mass where every vehicle has its mass and the
        // engines their force or power (task 03), else simply.
        if (missing.isEmpty() && (plan.performance.force > 0.0 || plan.performance.power > 0.0)) {
            plan.driving = Driving::Physics;
        } else {
            plan.driving = Driving::Simple;
            input->warnings << (missing.isEmpty()
                                ? QString("%1: driven simply (its engines have no force or power)").arg(name)
                                : QString("%1: driven simply (no mass for %2)").arg(name, missing.join(", ")));
        }
        // The consist file's MaxVelocity (as Open Rails takes it), else its
        // slowest engine's.
        plan.maxSpeed = consist->fileMaxVelocity > 0.1f ? consist->fileMaxVelocity
                      : engineSpeed > 0.0f ? engineSpeed : input->routeSpeedLimit;
        plan.efficiency = service->efficiency > 0.0f ? service->efficiency : 1.0f;
        plan.drivingEfficiency = player ? 0.9f : plan.efficiency;
        SetRates(plan, std::min(input->routeSpeedLimit, plan.maxSpeed * plan.efficiency));
        ActivityServiceDefinition *definition = ActivityTrains::Definition(activity, train);
        if (definition != nullptr && !definition->efficiency.isEmpty() && definition->efficiency.last() > 0.0f)
            plan.efficiency = definition->efficiency.last();
        plan.startTime = player ? input->startTime : train.time;

        // The train stands with its rear at the path's start, the whole train
        // ahead of it, as Open Rails places trains (Simulator.Initialize-
        // PlayerTrain, AI.cs; task 06). The track behind, the train's length,
        // is its route's lead-in: where its first part is shorter than the
        // train, it stands back on it.
        plan.route = BuildRoute(tdb, path->nodes(), path->legs(), double(plan.length) + 1.0);
        if (plan.route.parts.empty() || plan.route.parts.front().length() <= 0.0) {
            input->warnings << QString("%1: its path has no track").arg(name);
            continue;
        }
        plan.startFront = std::min(plan.route.pathStart + plan.length, plan.route.parts.front().length());

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
                passengerCars += vehicle.type == 1 ? 1 : 0;
            for (int i = 0; i < count; ++i) {
                const TRitem *item = platformItem(tdb, stops[i].platformStartID);
                const QString platform = item != nullptr ? item->stationName : QString::number(stops[i].platformStartID);
                if (passOf[i] < 0 || passOf[i] >= passes.size()) {
                    input->warnings << QString("%1: the stop at %2 is not on the path").arg(name, platform);
                    continue;
                }
                double nearEnd = 1e12, farEnd = -1e12;
                for (const QPair<int, float> &met : passes[passOf[i]].items) {
                    nearEnd = std::min(nearEnd, double(met.second));
                    farEnd = std::max(farEnd, double(met.second));
                }
                StopPlan stop;
                double nearRoute = 0.0;
                int nearPart = 0;
                if (!plan.route.place(farEnd, stop.part, stop.front)
                        || !plan.route.place(nearEnd, nearPart, nearRoute)) {
                    input->warnings << QString("%1: the stop at %2 is beyond its route").arg(name, platform);
                    continue;
                }
                if (nearPart != stop.part)
                    nearRoute = 0.0;
                stop.platformEnd = stop.front;
                stop.platformStart = nearRoute;
                // Centred on the platform; a longer train overhangs both ends.
                const double platformLength = stop.front - nearRoute;
                stop.front = std::clamp(stop.front - 0.5 * (platformLength - plan.length), 0.0,
                                        plan.route.parts[size_t(stop.part)].length());
                const bool timed = timetable->arrivalTime[i] != 0 || timetable->departTime[i] != 0;
                stop.arrival = timed ? timetable->arrivalTime[i] : -1;
                stop.departure = timed ? timetable->departTime[i] : -1;
                const int passengers = passengersWaiting(activity, tdb, stops[i].platformStartID);
                stop.boarding = passengerCars > 0 ? std::max(20, 10 * passengers / passengerCars) : 20;
                stop.efficiency = definition != nullptr && i < definition->efficiency.size()
                        && definition->efficiency[i] > 0.0f ? definition->efficiency[i] : plan.efficiency;
                stop.platform = platform;
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
        for (const RouteTrack &part : plan.route.parts) {
            plan.projection.push_back(input->network->project(part));
            // The zones whose both ends this part passes.
            std::vector<std::pair<double, double>> passed;
            for (const Zone &zone : zones) {
                double a = 0.0, b = 0.0;
                if (part.find(zone.node[0], zone.distance[0], a) && part.find(zone.node[1], zone.distance[1], b))
                    passed.push_back({std::min(a, b), std::max(a, b)});
            }
            plan.limits.push_back(SpeedLimits(plan.projection.back(), *input->network, plan.passenger, passed,
                                              input->restrictedSpeed, part.length()));
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
        input->trains.push_back(std::move(plan));
    }
    input->staticConsists = StaticConsists(activity, tdb);
    input->events = Events(activity, tdb, *input, input->warnings);
    return input;
}

}
