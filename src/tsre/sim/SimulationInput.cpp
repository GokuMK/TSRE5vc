#include <tsre/sim/SimulationInput.h>

#include <QDir>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
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

}

void SetRates(TrainPlan &train) {
    bool passenger = false;
    for (const Vehicle &vehicle : train.vehicles)
        passenger = passenger || vehicle.type == 1;
    if (!passenger) {
        train.acceleration = 0.5f;
        train.deceleration = 0.5f;
        return;
    }
    train.acceleration = 1.0f;
    train.deceleration = train.maxSpeed > 55.0f ? 2.5f : train.maxSpeed > 40.0f ? 1.5f : 1.0f;
}

std::shared_ptr<const SimulationInput> MakeInput(Activity *activity, TDB *tdb, float routeSpeedLimit,
                                                 std::shared_ptr<const TrackGeometry> geometry) {
    auto input = std::make_shared<SimulationInput>();
    input->runId = nextRun++;
    input->routeSpeedLimit = routeSpeedLimit > 0.0f ? routeSpeedLimit : 44.4444f;
    input->geometry = geometry != nullptr ? geometry : std::make_shared<const TrackGeometry>(tdb);
    if (activity == nullptr || tdb == nullptr)
        return input;
    if (activity->header != nullptr) {
        input->activity = activity->header->name;
        input->startTime = activity->header->startTime[0] * 3600 + activity->header->startTime[1] * 60
                + activity->header->startTime[2];
    }
    const QString routePath = ContentPath::parentDirectory(activity->path);
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
        plan.consist = service->trainConfig;
        float engineSpeed = 0.0f;
        for (const Consist::EngItem &item : consist->engItems) {
            Vehicle vehicle;
            vehicle.flip = item.flip;
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
        input->trains.push_back(std::move(plan));
    }
    return input;
}

}
