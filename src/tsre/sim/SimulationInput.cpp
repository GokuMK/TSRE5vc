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

void SetRates(TrainPlan &train) {
    bool passenger = false;
    for (const Vehicle &vehicle : train.vehicles)
        passenger = passenger || vehicle.type == 1;
    train.passenger = passenger;
    if (!passenger) {
        train.acceleration = 0.5f;
        train.deceleration = 0.5f;
        return;
    }
    train.acceleration = 1.0f;
    train.deceleration = train.maxSpeed > 55.0f ? 2.5f : train.maxSpeed > 40.0f ? 1.5f : 1.0f;
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
        for (const Consist::EngItem &item : consist->engItems) {
            Vehicle vehicle;
            vehicle.flip = item.flip;
            vehicle.eng = item.eng;
            Eng *eng = nullptr;
            if (item.eng >= 0 && Game::currentEngLib != nullptr) {
                auto e = Game::currentEngLib->eng.find(item.eng);
                eng = e != Game::currentEngLib->eng.end() ? e->second : nullptr;
            }
            if (eng != nullptr) {
                vehicle.length = eng->getFullWidth();
                vehicle.type = eng->wagonTypeId;
                if (eng->wagonTypeId >= 4 && eng->maxSpeed > 0.0f)
                    engineSpeed = engineSpeed > 0.0f ? std::min(engineSpeed, eng->maxSpeed) : eng->maxSpeed;
            }
            plan.length += vehicle.length;
            plan.vehicles.push_back(vehicle);
        }
        plan.maxSpeed = consist->maxVelocity[0] > 0.1f ? consist->maxVelocity[0]
                      : engineSpeed > 0.0f ? engineSpeed : input->routeSpeedLimit;
        SetRates(plan);
        plan.efficiency = service->efficiency > 0.0f ? service->efficiency : 1.0f;
        ActivityServiceDefinition *definition = ActivityTrains::Definition(activity, train);
        if (definition != nullptr && !definition->efficiency.isEmpty() && definition->efficiency.last() > 0.0f)
            plan.efficiency = definition->efficiency.last();
        plan.startTime = player ? input->startTime : train.time;

        // The train stands behind the path's start, its first vehicle across
        // it; moved ahead when the track behind ends first.
        const float firstHalf = plan.vehicles.front().length * 0.5f;
        plan.route = BuildRoute(tdb, path->nodes(), path->legs(), double(plan.length - firstHalf) + 1.0);
        if (plan.route.parts.empty() || plan.route.parts.front().length() <= 0.0) {
            input->warnings << QString("%1: its path has no track").arg(name);
            continue;
        }
        plan.startFront = plan.route.pathStart + firstHalf;
        if (plan.route.leadInEnded)
            plan.startFront = std::max(plan.startFront, double(plan.length) + 1.0);
        plan.startFront = std::min(plan.startFront, plan.route.parts.front().length());

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
        input->trains.push_back(std::move(plan));
    }
    input->staticConsists = StaticConsists(activity, tdb);
    return input;
}

}
