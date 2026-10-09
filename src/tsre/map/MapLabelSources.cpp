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
#include <cmath>
#include <tsre/coords/Coords.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityEvent.h>

namespace MapLabelSources {

namespace {

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
    if (markers == nullptr || !markers->loaded)
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

void appendTrackDatabase(std::vector<MapLabel> &out, TDB *database) {
    if (database == nullptr || !database->loaded)
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
        const bool platform = item->type == QLatin1String("platformitem");
        const bool siding = item->type == QLatin1String("sidingitem");
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
    for (const ActivityEvent &event : activity->event) {
        if (event.category != ActivityEvent::CategoryLocation || event.location == nullptr || event.name.isEmpty())
            continue;
        // tile x, tile z, x, z (database convention), radius.
        MapLabel label;
        label.tileX = int(event.location[0]);
        label.tileZ = -int(event.location[1]);
        label.x = event.location[2];
        label.z = -event.location[3];
        label.text = event.name;
        label.kind = MapLabelKind::Event;
        label.priority = EventPriority;
        out.push_back(std::move(label));
    }
}

}
