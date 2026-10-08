/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ActivityMapLayer.h"
#include "MapPalette.h"
#include "MapSelection.h"
#include "MapView.h"
#include "TrackItemMapLayer.h"
#include "TrackMapLayer.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QOpenGLFunctions>
#include <algorithm>
#include <cmath>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityEvent.h>
#include <tsre/trains/ActivityObject.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/Service.h>
#include <tsre/world/Route.h>

namespace {

// Geometry is rebuilt when the scale changed by more than this factor, so
// markers keep about their size in pixels.
constexpr float RebuildScale = 1.25f;
constexpr int RingSegments = 48;

void setHeight(std::vector<float> &points, float y) {
    for (size_t i = 1; i < points.size(); i += 3)
        points[i] = y;
}

}

ActivityMapLayer::ActivityMapLayer()
    : pathBand(std::make_unique<OglObj>()), halos(std::make_unique<OglObj>()),
      borders(std::make_unique<OglObj>()),
      areas(std::make_unique<OglObj>()) {
    for (auto &fill : fills)
        fill = std::make_unique<OglObj>();
}

ActivityMapLayer::~ActivityMapLayer() = default;

QColor ActivityMapLayer::colour(const MapPalette &palette, int group) {
    switch (group) {
    case Path: return palette.pathNode;
    case Vehicles: return palette.wagon;
    case Engines: return palette.engine;
    case SpeedZone: return palette.speedZone;
    case FailedSignal: return palette.failedSignal;
    case Event: return palette.event;
    default: return palette.itemBorder;
    }
}

void ActivityMapLayer::appendVehicle(std::vector<float> &out, const float *vehicle, float y,
                                     float margin, float minimum) {
    const float halfLength = 0.5f * std::max(vehicle[4], minimum) + margin;
    const float halfWidth = 0.5f * std::max(vehicle[5], minimum) + margin;
    const float dx = vehicle[2], dz = vehicle[3];
    const float a[2] = {vehicle[0] - dx * halfLength, vehicle[1] - dz * halfLength};
    const float b[2] = {vehicle[0] + dx * halfLength, vehicle[1] + dz * halfLength};
    // Sideways, wound like TrackMapLayer's ribbons.
    const float sx = -dz * halfWidth, sz = dx * halfWidth;
    const float corners[4][3] = {{a[0] - sx, y, a[1] - sz},
                                 {a[0] + sx, y, a[1] + sz},
                                 {b[0] + sx, y, b[1] + sz},
                                 {b[0] - sx, y, b[1] - sz}};
    for (int corner : {0, 1, 2, 0, 2, 3})
        out.insert(out.end(), corners[corner], corners[corner] + 3);
}

std::vector<const void *> ActivityMapLayer::sources(Route *route, bool activity,
                                                    bool paths) const {
    // What the map shows: when any of it changes, the map builds again.
    std::vector<const void *> list;
    if (route == nullptr)
        return list;
    list.push_back(route);
    if (paths)
        for (const ::Path *path : route->path)
            if (path != nullptr && const_cast<::Path *>(path)->isSelected())
                list.push_back(path);
    Activity *current = activity ? route->getCurrentActivity() : nullptr;
    if (current == nullptr)
        return list;
    list.push_back(current);
    auto count = [&list](qsizetype size) { list.push_back(reinterpret_cast<const void *>(size)); };
    count(current->activityObjects.size());
    count(current->restrictedSpeedZone.size());
    count(current->activityFailedSignal.size());
    count(current->event.size());
    for (const ActivityObject *object : current->activityObjects)
        if (object != nullptr && object->con != nullptr) {
            list.push_back(object->con);
            count(object->con->isOnTrack ? 1 : 0);
        }
    if (current->playerServiceDefinition != nullptr)
        list.push_back(current->playerServiceDefinition->servicePointer);
    return list;
}

void ActivityMapLayer::build(const MapView &view, const MapPalette &palette, Route *route,
                             bool activity, bool paths) {
    static const bool trace = qEnvironmentVariableIsSet("TSRE_MAP_TRACE");
    QElapsedTimer timer;
    timer.start();
    MapFeatures path, consists, zones, failedSignals, events, selected;
    const int tileX = view.tileX, tileZ = view.tileZ;
    // The selected paths and the activity's player path, each once.
    std::vector<::Path *> shownPaths;
    if (route != nullptr && paths)
        for (::Path *selected : route->path)
            if (selected != nullptr && selected->isSelected())
                shownPaths.push_back(selected);
    Activity *current = activity && route != nullptr ? route->getCurrentActivity() : nullptr;
    Service *player = current != nullptr && current->playerServiceDefinition != nullptr
            ? current->playerServiceDefinition->servicePointer : nullptr;
    if (player != nullptr) {
        ::Path *playerPath = player->getPathPointer();
        if (playerPath != nullptr
                && std::find(shownPaths.begin(), shownPaths.end(), playerPath) == shownPaths.end())
            shownPaths.push_back(playerPath);
    }
    for (::Path *shown : shownPaths)
        shown->getMapFeatures(path, tileX, tileZ);
    if (current != nullptr) {
        // Each object's features, and again for the halo when selected.
        auto collect = [&](const QVector<ActivityObject *> &objects, MapFeatures &into) {
            for (ActivityObject *object : objects) {
                if (object == nullptr)
                    continue;
                MapFeatures own;
                object->getMapFeatures(own, tileX, tileZ);
                into.append(own);
                if (object->isSelected())
                    selected.append(own);
            }
        };
        collect(current->activityObjects, consists);
        collect(current->restrictedSpeedZone, zones);
        collect(current->activityFailedSignal, failedSignals);
        for (ActivityEvent &event : current->event)
            event.getMapFeatures(events, tileX, tileZ);
        if (player != nullptr) {
            MapFeatures own;
            player->getMapFeatures(own, tileX, tileZ);
            consists.append(own);
            Consist *train = player->getConsistPointer();
            if (train != nullptr && train->isSelected())
                selected.append(own);
        }
    }

    const float mpp = view.metresPerPixel;
    const float border = BorderPixels * mpp;
    const float radius = 0.5f * MarkerPixels * mpp;
    std::vector<float> bandVertices, haloVertices, borderVertices, areaVertices,
            fillVertices[GroupCount];
    auto lines = [&](std::vector<float> &segments, int group) {
        const int count = int(segments.size() / 6);
        setHeight(segments, BorderHeight);
        TrackMapLayer::appendRibbons(borderVertices, segments.data(), count,
                                     (LinePixels + 2.0f * BorderPixels) * mpp);
        setHeight(segments, FillHeight + group);
        TrackMapLayer::appendRibbons(fillVertices[group], segments.data(), count, LinePixels * mpp);
    };
    auto circles = [&](const std::vector<float> &points, int group) {
        for (size_t i = 0; i + 2 < points.size(); i += 3) {
            TrackItemMapLayer::appendCircle(borderVertices, points[i], BorderHeight, points[i + 2],
                                            radius + border);
            TrackItemMapLayer::appendCircle(fillVertices[group], points[i], FillHeight + group,
                                            points[i + 2], radius);
        }
    };

    // Paths: a band under the track, nodes on top.
    setHeight(path.lines, PathHeight);
    TrackMapLayer::appendRibbons(bandVertices, path.lines.data(), int(path.lines.size() / 6),
                                 PathPixels * mpp);
    circles(path.points, Path);
    // Consists.
    const int vehicleCount = int(consists.vehicles.size()) / MapFeatures::VehicleFloats;
    for (int i = 0; i < vehicleCount; ++i) {
        const float *vehicle = consists.vehicles.data() + i * MapFeatures::VehicleFloats;
        const int group = vehicle[6] > 0.5f ? Engines : Vehicles;
        // Wagons a little shorter than they are, so the borders part them.
        float shorter[MapFeatures::VehicleFloats];
        std::copy(vehicle, vehicle + MapFeatures::VehicleFloats, shorter);
        shorter[4] = std::max(0.0f, shorter[4] - 2.0f * border);
        appendVehicle(borderVertices, shorter, BorderHeight, border, MinVehiclePixels * mpp);
        appendVehicle(fillVertices[group], shorter, FillHeight + group, 0.0f,
                      MinVehiclePixels * mpp);
    }
    // Speed zones and failed failedSignals.
    lines(zones.lines, SpeedZone);
    circles(zones.points, SpeedZone);
    circles(failedSignals.points, FailedSignal);
    // Location events: squares, north up, and rings at the trigger radius.
    for (size_t i = 0; i + 2 < events.points.size(); i += 3) {
        const float x = events.points[i], z = events.points[i + 2];
        TrackMapLayer::appendSquare(borderVertices, x, BorderHeight, z,
                                    MarkerPixels * mpp + 2.0f * border, 1, 0, 0, -1);
        TrackMapLayer::appendSquare(fillVertices[Event], x, FillHeight + Event, z,
                                    MarkerPixels * mpp, 1, 0, 0, -1);
    }
    std::vector<float> ring;
    for (size_t i = 0; i + 2 < events.areas.size(); i += 3) {
        const float x = events.areas[i], z = events.areas[i + 1], r = events.areas[i + 2];
        ring.clear();
        for (int s = 0; s < RingSegments; ++s)
            for (int end = 0; end < 2; ++end) {
                const float angle = (s + end) * 6.28318531f / RingSegments;
                ring.insert(ring.end(), {x + r * std::cos(angle), AreaHeight,
                                         z + r * std::sin(angle)});
            }
        TrackMapLayer::appendRibbons(areaVertices, ring.data(), RingSegments, BorderPixels * mpp);
    }

    // Halos around what is selected.
    const float halo = border + HaloPixels * mpp;
    setHeight(selected.lines, HaloHeight);
    TrackMapLayer::appendRibbons(haloVertices, selected.lines.data(),
                                 int(selected.lines.size() / 6), LinePixels * mpp + 2.0f * halo);
    for (size_t i = 0; i + 2 < selected.points.size(); i += 3)
        TrackItemMapLayer::appendCircle(haloVertices, selected.points[i], HaloHeight,
                                        selected.points[i + 2], radius + halo);
    for (size_t i = 0; i + MapFeatures::VehicleFloats <= selected.vehicles.size();
         i += MapFeatures::VehicleFloats)
        appendVehicle(haloVertices, selected.vehicles.data() + i, HaloHeight, halo,
                      MinVehiclePixels * mpp);

    auto upload = [](OglObj &object, std::vector<float> &vertices, const QColor &colour) {
        object.setMaterial(float(colour.redF()), float(colour.greenF()), float(colour.blueF()));
        object.init(vertices.data(), int(vertices.size()), RenderItem::V, GL_TRIANGLES);
    };
    upload(*pathBand, bandVertices, palette.path);
    upload(*halos, haloVertices, palette.selection);
    upload(*borders, borderVertices, palette.itemBorder);
    upload(*areas, areaVertices, palette.event);
    for (int group = 0; group < GroupCount; ++group)
        upload(*fills[group], fillVertices[group], colour(palette, group));

    valid = true;
    builtTileX = view.tileX;
    builtTileZ = view.tileZ;
    builtMetresPerPixel = view.metresPerPixel;
    builtPalette = palette.name;
    if (trace)
        qInfo().noquote() << "map-trace activity m/px" << mpp << "path segments"
                          << path.lines.size() / 6 << "vehicles" << vehicleCount << "zones"
                          << zones.points.size() / 6 << "failed signals"
                          << failedSignals.points.size() / 3 << "events" << events.points.size() / 3
                          << "ms" << timer.nsecsElapsed() / 1e6;
}

void ActivityMapLayer::pushRenderItems(RenderQueue &queue, const MapView &view,
                                       const MapPalette &palette, Route *route, bool activity,
                                       bool paths) {
    if (!activity && !paths)
        return;
    std::vector<const void *> current = sources(route, activity, paths);
    const float scale = view.metresPerPixel / std::max(builtMetresPerPixel, 1e-6f);
    if (!valid || view.tileX != builtTileX || view.tileZ != builtTileZ || scale > RebuildScale
            || scale < 1.0f / RebuildScale || palette.name != builtPalette
            || current != builtSources) {
        build(view, palette, route, activity, paths);
        builtSources = std::move(current);
    }
    pathBand->pushRenderItem(queue);
    halos->pushRenderItem(queue);
    areas->pushRenderItem(queue);
    borders->pushRenderItem(queue);
    for (auto &fill : fills)
        fill->pushRenderItem(queue);
}

void ActivityMapLayer::pushSelection(MapSelection &selection, const MapView &view, Route *route,
                                     bool activity) const {
    Activity *current = activity && route != nullptr ? route->getCurrentActivity() : nullptr;
    if (current == nullptr)
        return;
    const float mpp = view.metresPerPixel;
    const float margin = (BorderPixels + MapSelection::MarginPixels) * mpp;
    const float radius = 0.5f * MarkerPixels * mpp + margin;
    // The ids of 3D (Activity::pushRenderItems): objects by index, speed
    // zones from 3000, failed signals from 3500; the parts are vehicles in
    // a consist and 1 and 3 for a speed zone's ends.
    auto vehicles = [&](const MapFeatures &features, quint32 id) {
        const int count = int(features.vehicles.size()) / MapFeatures::VehicleFloats;
        for (int i = 0; i < count; ++i) {
            quint32 part = 0;
            if (SelectionIdCodec::tryWithPart(id, i, part))
                appendVehicle(selection.shape(part),
                              features.vehicles.data() + i * MapFeatures::VehicleFloats,
                              FillHeight + Vehicles, margin, MinVehiclePixels * mpp);
        }
    };
    auto points = [&](const MapFeatures &features, quint32 id, bool ends) {
        for (size_t i = 0; i + 2 < features.points.size(); i += 3) {
            quint32 part = id;
            if (ends && !SelectionIdCodec::tryWithPart(id, i == 0 ? 1 : 3, part))
                continue;
            TrackItemMapLayer::appendCircle(selection.shape(part), features.points[i],
                                            FillHeight + SpeedZone, features.points[i + 2],
                                            radius);
        }
    };
    const int tileX = view.tileX, tileZ = view.tileZ;
    quint32 id = 0;
    for (int i = 0; i < current->activityObjects.size(); ++i) {
        ActivityObject *object = current->activityObjects[i];
        if (object == nullptr || !SelectionIdCodec::tryActivityObject(i, 0, id))
            continue;
        MapFeatures own;
        object->getMapFeatures(own, tileX, tileZ);
        vehicles(own, id);
    }
    for (int i = 0; i < current->restrictedSpeedZone.size(); ++i) {
        ActivityObject *object = current->restrictedSpeedZone[i];
        if (object == nullptr || !SelectionIdCodec::tryActivityObject(3000 + i, 0, id))
            continue;
        MapFeatures own;
        object->getMapFeatures(own, tileX, tileZ);
        points(own, id, true);
    }
    for (int i = 0; i < current->activityFailedSignal.size(); ++i) {
        ActivityObject *object = current->activityFailedSignal[i];
        if (object == nullptr || !SelectionIdCodec::tryActivityObject(3500 + i, 0, id))
            continue;
        MapFeatures own;
        object->getMapFeatures(own, tileX, tileZ);
        points(own, id, false);
    }
    Service *player = current->playerServiceDefinition != nullptr
            ? current->playerServiceDefinition->servicePointer : nullptr;
    if (player != nullptr && SelectionIdCodec::tryActivityService(0, 0, id)) {
        MapFeatures own;
        player->getMapFeatures(own, tileX, tileZ);
        vehicles(own, id);
    }
}
