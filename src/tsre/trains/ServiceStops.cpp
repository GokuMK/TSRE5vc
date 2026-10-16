#include <tsre/trains/ServiceStops.h>

#include <cmath>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/trains/Traffic.h>

namespace {

using Stops = QVector<Service::StationStop>;

int partnerOf(int trItemId) {
    if (Game::trackDB == NULL)
        return trItemId;
    const auto found = Game::trackDB->trackItems.find(trItemId);
    if (found == Game::trackDB->trackItems.end() || found->second == NULL
            || found->second->platformTrItemData == NULL)
        return trItemId;
    return int(found->second->platformTrItemData[1]);
}

// Which entry of a copy (its platforms and skip counts) each stop of
// `before` is: the same platform and count, else the same platform, else
// the same place when the copy has as many entries.
QVector<int> entries(const QVector<int> &platform, const QVector<int> &skip, const Stops &before) {
    QVector<int> entry(before.size(), -1);
    QVector<bool> used(platform.size(), false);
    for (int i = 0; i < before.size(); ++i)
        for (int k = 0; k < platform.size(); ++k)
            if (!used[k] && platform[k] == before[i].platformStartID && k < skip.size()
                    && skip[k] == before[i].skipCount) {
                entry[i] = k;
                used[k] = true;
                break;
            }
    for (int i = 0; i < before.size(); ++i)
        for (int k = 0; entry[i] < 0 && k < platform.size(); ++k)
            if (!used[k] && platform[k] == before[i].platformStartID) {
                entry[i] = k;
                used[k] = true;
            }
    if (platform.size() == before.size())
        for (int i = 0; i < before.size(); ++i)
            if (entry[i] < 0 && !used[i]) {
                entry[i] = i;
                used[i] = true;
            }
    return entry;
}

// A timetable (the player's, or a traffic instance) on the new stops.
bool followTimetable(ActivityTimetable *timetable, const Stops &before, const Stops &after, const QVector<int> &from,
                     const std::function<void()> &keep) {
    const QVector<int> entry = entries(timetable->platformStartID, timetable->skipCount, before);
    QVector<int> arrival, depart, skip, platform;
    QVector<float> distance;
    int left = timetable->time;
    for (int j = 0; j < after.size(); ++j) {
        const int k = from[j] >= 0 ? entry[from[j]] : -1;
        const int arrives = k >= 0 && k < timetable->arrivalTime.size() ? timetable->arrivalTime[k] : left;
        const int leaves = k >= 0 && k < timetable->departTime.size() ? timetable->departTime[k] : arrives;
        arrival.push_back(arrives);
        depart.push_back(leaves);
        skip.push_back(after[j].skipCount);
        distance.push_back(after[j].distanceDownPath);
        platform.push_back(after[j].platformStartID);
        left = leaves;
    }
    if (arrival == timetable->arrivalTime && depart == timetable->departTime && skip == timetable->skipCount
            && distance == timetable->distanceDownPath && platform == timetable->platformStartID)
        return false;
    keep();
    timetable->arrivalTime = arrival;
    timetable->departTime = depart;
    timetable->skipCount = skip;
    timetable->distanceDownPath = distance;
    timetable->platformStartID = platform;
    timetable->setModified(true);
    return true;
}

// An activity's service instance (its efficiency per stop) on the new stops.
bool followDefinition(ActivityServiceDefinition &definition, Service *service, const Stops &before,
                      const Stops &after, const QVector<int> &from, const std::function<void()> &keep) {
    const QVector<int> entry = entries(definition.platformStartId, definition.skipCount, before);
    QVector<float> efficiency, distance;
    QVector<int> skip, platform;
    for (int j = 0; j < after.size(); ++j) {
        const int k = from[j] >= 0 ? entry[from[j]] : -1;
        efficiency.push_back(k >= 0 && k < definition.efficiency.size() ? definition.efficiency[k] : service->efficiency);
        skip.push_back(after[j].skipCount);
        distance.push_back(after[j].distanceDownPath);
        platform.push_back(after[j].platformStartID);
    }
    if (efficiency == definition.efficiency && skip == definition.skipCount
            && distance == definition.distanceDownPath && platform == definition.platformStartId)
        return false;
    keep();
    definition.efficiency = efficiency;
    definition.skipCount = skip;
    definition.distanceDownPath = distance;
    definition.platformStartId = platform;
    definition.setChanged();
    return true;
}

}

int ServiceStops::Platform(int trItemId) {
    return std::min(trItemId, partnerOf(trItemId));
}

QVector<int> ServiceStops::Passes(const Stops &stops, const QVector<Path::PathObject> &passes) {
    QVector<int> found(stops.size(), -1);
    for (int i = 0; i < stops.size(); ++i) {
        const Service::StationStop &stop = stops[i];
        const int partner = partnerOf(stop.platformStartID);
        const auto atPlatform = [&](const Path::PathObject &pass) {
            return pass.meets(stop.platformStartID) || pass.meets(partner);
        };
        if (stop.skipCount >= 0 && stop.skipCount < passes.size() && atPlatform(passes[stop.skipCount])) {
            found[i] = stop.skipCount;
            continue;
        }
        float nearest = 1e12f;
        for (int v = 0; v < passes.size(); ++v) {
            float at = passes[v].distanceDownPath;
            if (!passes[v].meets(stop.platformStartID, &at) && !passes[v].meets(partner))
                continue;
            if (std::fabs(at - stop.distanceDownPath) < nearest) {
                nearest = std::fabs(at - stop.distanceDownPath);
                found[i] = v;
            }
        }
    }
    return found;
}

QStringList ServiceStops::Follow(Service *service, const Stops &before, const QVector<int> &from) {
    QStringList names;
    if (service == NULL)
        return names;
    const Stops &after = service->stationStop;
    const auto named = [service](const QString &name) {
        return name.compare(service->nameId, Qt::CaseInsensitive) == 0;
    };
    for (Activity *activity : ActLib::Act) {
        if (activity == NULL || activity->loaded != 1 || activity->routeHashid != service->routeHashid)
            continue;
        bool kept = false, changed = false;
        const auto keep = [&] {
            if (!kept)
                Undo::PushActivityData(activity);
            kept = true;
        };
        ActivityServiceDefinition *player = activity->playerServiceDefinition;
        if (player != NULL && !player->empty && named(player->name)) {
            if (player->trafficDefinition != NULL)
                changed = followTimetable(player->trafficDefinition, before, after, from, keep) || changed;
            changed = followDefinition(*player, service, before, after, from, keep) || changed;
        }
        if (activity->traffic != NULL)
            for (ActivityServiceDefinition &instance : activity->traffic->service)
                if (named(instance.name))
                    changed = followDefinition(instance, service, before, after, from, keep) || changed;
        if (changed)
            names << activity->name;
    }
    for (Traffic *traffic : ActLib::Traffics) {
        if (traffic == NULL || traffic->loaded != 1 || traffic->routeHashid != service->routeHashid)
            continue;
        bool kept = false, changed = false;
        const auto keep = [&] {
            if (!kept)
                Undo::PushTrafficData(traffic);
            kept = true;
        };
        for (ActivityTimetable *instance : traffic->service)
            if (instance != NULL && !instance->empty && named(instance->name))
                changed = followTimetable(instance, before, after, from, keep) || changed;
        if (changed)
            names << traffic->name;
    }
    return names;
}
