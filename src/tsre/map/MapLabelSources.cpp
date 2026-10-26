/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "MapLabelSources.h"
#include "TrackItemMapLayer.h"
#include <QHash>
#include <QSet>
#include <QTime>
#include <cmath>
#include <tsre/coords/Coords.h>
#include <tsre/coords/CoordsRoutePlaces.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/sim/EventResults.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/SimulationTexts.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityEvent.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Service.h>

namespace MapLabelSources {

namespace {

// Above every name that stays in place; the simulation's messages beside
// the trains above them.
constexpr double TrainPriority = 5e12;
constexpr double EventPriority = 4e12;
constexpr double StationPriority = 3e12;
constexpr double PlatformPriority = 1e12;
constexpr double SidingPriority = 9e11;
// A marker set is shown for a purpose: above OSM names (placeLabelPriority, up to
// about 6e10), below the route's own names.
constexpr double MarkerPriority = 1e11;

// Points in the view's tile convention, averaged: x and z relative to the first
// point's tile, so tiles do not lose precision.
struct Average {
    int tileX = 0, tileZ = 0;
    double x = 0, z = 0;
    int count = 0;
    void add(int tx, int tz, float px, float pz) {
        if (count == 0) { tileX = tx; tileZ = tz; }
        x += double(tx - tileX) * 2048.0 + px;
        z += double(tz - tileZ) * 2048.0 + pz;
        ++count;
    }
    MapLabel label(const QString &text) const {
        MapLabel l;
        l.tileX = tileX;
        l.tileZ = tileZ;
        l.x = float(x / count);
        l.z = float(z / count);
        l.text = text;
        return l;
    }
};

}

void appendMarkers(std::vector<MapLabel> &out, const Coords *markers) {
    // The "Route: Stations" and "Route: Sidings" sets repeat the track database's
    // names, which the map labels from the database itself.
    if (markers == nullptr || !markers->loaded || dynamic_cast<const CoordsRoutePlaces *>(markers) != nullptr)
        return;
    for (const Coords::Marker &m : markers->markerList) {
        if (m.tileX.isEmpty() || m.name.isEmpty())
            continue;
        MapLabel label;
        // Marker tiles count z as the converter does; the map as the camera.
        label.tileX = m.tileX[0];
        label.tileZ = -m.tileZ[0];
        label.x = float(m.x[0]);
        label.z = float(m.z[0]);
        label.text = m.name;
        label.priority = MarkerPriority + placeLabelPriority(m.featureCode, m.population, &label.major);
        label.kind = MapLabelKind::Marker;
        out.push_back(std::move(label));
    }
}

void appendTrackDatabase(std::vector<MapLabel> &out, TDB *database, bool platforms, bool sidings) {
    if (database == nullptr || !database->loaded || (!platforms && !sidings))
        return;
    const QHash<int, TrackItemMapLayer::ItemPlace> places = TrackItemMapLayer::itemPlaces(database);
    // Database positions to the view's convention: tile z and z negated.
    auto add = [&](Average &average, const TRitem *item) {
        const auto place = places.constFind(int(item->trItemId));
        if (place != places.constEnd())
            average.add(place->tileX, -place->tileZ, place->x, -place->z);
    };
    QHash<QString, Average> stations;
    QHash<QString, int> platformsOfStation;
    QSet<unsigned int> done;
    for (const auto &entry : database->trackItems) {
        const TRitem *item = entry.second;
        if (item == nullptr)
            continue;
        const bool platform = platforms && item->type == QLatin1String("platformitem");
        const bool siding = sidings && item->type == QLatin1String("sidingitem");
        if (!platform && !siding)
            continue;
        if (platform && !item->stationName.isEmpty()) {
            add(stations[item->stationName], item);
            ++platformsOfStation[item->stationName];
        }
        // A platform or siding once, between its two ends (the other end's ID).
        if (done.contains(item->trItemId) || item->platformName.isEmpty())
            continue;
        done.insert(item->trItemId);
        Average ends;
        add(ends, item);
        if (item->platformTrItemData != nullptr) {
            const unsigned int other = item->platformTrItemData[1];
            const auto found = database->trackItems.find(int(other));
            if (found != database->trackItems.end() && found->second != nullptr) {
                add(ends, found->second);
                done.insert(other);
            }
        }
        if (ends.count == 0 || (platform && item->platformName == item->stationName))
            continue;
        MapLabel label = ends.label(item->platformName);
        label.kind = platform ? MapLabelKind::Platform : MapLabelKind::Siding;
        label.priority = platform ? PlatformPriority : SidingPriority;
        label.maxMetresPerPixel = platform ? PlatformMetresPerPixel : SidingMetresPerPixel;
        out.push_back(std::move(label));
    }
    for (auto it = stations.cbegin(); it != stations.cend(); ++it) {
        if (it->count == 0)
            continue;
        MapLabel label = it->label(it.key());
        label.kind = MapLabelKind::Station;
        label.major = true;
        // Larger stations first.
        label.priority = StationPriority + platformsOfStation.value(it.key());
        out.push_back(std::move(label));
    }
}

void appendActivity(std::vector<MapLabel> &out, Activity *activity) {
    if (activity == nullptr)
        return;
    bool fresh = false;
    const Sim::EventResults *results = Sim::EventResultStore::instance()->of(activity, &fresh);
    for (int i = 0; i < activity->event.size(); ++i) {
        const ActivityEvent *e = activity->event[i];
        if (e == nullptr)
            continue;
        const ActivityEvent &event = *e;
        if (event.category != ActivityEvent::CategoryLocation || event.location == nullptr || event.name.isEmpty())
            continue;
        // tile x, tile z, x, z (database convention), radius.
        MapLabel label;
        label.tileX = int(event.location[0]);
        label.tileZ = -int(event.location[1]);
        label.x = event.location[2];
        label.z = -event.location[3];
        label.text = event.name;
        if (results != nullptr && fresh && size_t(i) < results->events.size() && results->events[size_t(i)].inRun
                && results->events[size_t(i)].state.fired > 0)
            label.text += QStringLiteral("  ")
                    + QTime(0, 0).addSecs(int(results->events[size_t(i)].state.firedAt) % 86400).toString("HH:mm:ss");
        label.kind = MapLabelKind::Event;
        label.priority = EventPriority;
        out.push_back(std::move(label));
    }
}

void appendTrains(std::vector<MapLabel> &out, Activity *activity) {
    if (activity == nullptr)
        return;
    auto add = [&](int tileX, int tileZ, float x, float z, const QString &name, quint32 selectionId) {
        MapLabel label;
        label.tileX = tileX;
        label.tileZ = tileZ;
        label.x = x;
        label.z = z;
        label.text = name;
        label.kind = MapLabelKind::Train;
        label.priority = TrainPriority;
        label.beside = true;
        label.selectionId = selectionId;
        out.push_back(std::move(label));
    };
    // Named where they begin (the leading vehicle's front). Running: where the
    // simulation has them (its trains are the running ones; standing ones
    // apart).
    const Sim::SimulationController *simulation = Sim::SimulationController::instance();
    if (simulation->shows(activity)) {
        const int count = int(simulation->viewFrame()->trains.size());
        for (int train = 0; train < count; ++train) {
            int tile[2], vehicle = 0;
            float position[3];
            quint32 id = 0;
            if (simulation->trainHead(train, tile, position, &vehicle))
                add(tile[0], tile[1], position[0], position[2], simulation->trainName(train),
                    SelectionIdCodec::tryActivityService(simulation->serviceId(train), vehicle, id) ? id : 0);
        }
        return;
    }
    // Before a run: those the map draws (ActivityMapLayer), at their
    // starts, by the front of their consists' first vehicles (an Eng's
    // first track point).
    auto place = [&](Service *service, const QString &name, int serviceId) {
        Consist *consist = service != nullptr ? service->getConsistPointer() : nullptr;
        float posTW[5];
        if (consist == nullptr || consist->engItems.isEmpty() || !consist->getWagonWorldPosition(0, posTW))
            return;
        // tile x, tile z, x, z (database convention).
        quint32 id = 0;
        add(int(posTW[0]), -int(posTW[1]), posTW[2], -posTW[4], name,
            SelectionIdCodec::tryActivityService(serviceId, 0, id) ? id : 0);
    };
    const QVector<int> shown = activity->shownAITrains();
    for (const ActivityTrains::Train &train : ActivityTrains::List(activity)) {
        if (train.staticTrain)
            continue;
        if (train.kind == ActivityTrains::Player)
            place(activity->playerService(), Sim::TrainName(train.service, true, train.time), 0);
        else if (train.kind == ActivityTrains::AI && shown.contains(train.index))
            place(activity->aiTrainService(train.index),
                  Sim::TrainName(train.service, false, train.orTimetable >= 0 ? -1 : train.time), train.index + 1);
    }
}

}
