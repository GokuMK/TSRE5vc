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
#include <tuple>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/sim/EventResults.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/SignalType.h>
#include <tsre/Game.h>
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
    : pathBand(std::make_unique<OglObj>()), aiBand(std::make_unique<OglObj>()),
      chosenBand(std::make_unique<OglObj>()), halos(std::make_unique<OglObj>()),
      borders(std::make_unique<OglObj>()),
      simulatedHalos(std::make_unique<OglObj>()),
      simulatedBorders(std::make_unique<OglObj>()), aspectBorders(std::make_unique<OglObj>()) {
    for (auto &area : areas)
        area = std::make_unique<OglObj>();
    for (auto &fill : fills)
        fill = std::make_unique<OglObj>();
    for (auto &fill : simulatedFills)
        fill = std::make_unique<OglObj>();
    for (auto &track : simulatedTrack)
        track = std::make_unique<OglObj>();
    for (auto &fill : aspectFills)
        fill = std::make_unique<OglObj>();
}

ActivityMapLayer::~ActivityMapLayer() = default;

QColor ActivityMapLayer::colour(const MapPalette &palette, int group) {
    switch (group) {
    case Path: return palette.pathNode;
    case PathStart: return palette.pathStart;
    case PathEnd: return palette.pathEnd;
    case PathBroken: return palette.pathBroken;
    case Vehicles: return palette.wagon;
    case Engines: return palette.engine;
    case SpeedZone: return palette.speedZone;
    case FailedSignal: return palette.failedSignal;
    case Event: return palette.event;
    case EventFired: return palette.eventFired;
    case EventInactive: return palette.eventInactive;
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
    list.push_back(current->playerService());
    count(current->shownAITrain);
    count(Sim::SimulationController::instance()->shows(current) ? 1 : 0);
    // The last runs' event results (task 06).
    count(qsizetype(Sim::EventResultStore::instance()->version()));
    for (int i : current->shownAITrains())
        list.push_back(current->aiTrainService(i));
    return list;
}

void ActivityMapLayer::build(const MapView &view, const MapPalette &palette, Route *route,
                             bool activity, bool paths) {
    static const bool trace = qEnvironmentVariableIsSet("TSRE_MAP_TRACE");
    QElapsedTimer timer;
    timer.start();
    MapFeatures path, aiPaths, chosenPath, consists, zones, failedSignals, selected;
    // Location events: as they are, fired, not active (task 06).
    MapFeatures events[3];
    const int tileX = view.tileX, tileZ = view.tileZ;
    // The selected paths and the activity's player path, each once.
    std::vector<::Path *> shownPaths;
    if (route != nullptr && paths)
        for (::Path *selected : route->path)
            if (selected != nullptr && selected->isSelected())
                shownPaths.push_back(selected);
    Activity *current = activity && route != nullptr ? route->getCurrentActivity() : nullptr;
    Service *player = current != nullptr ? current->playerService() : nullptr;
    // A simulation draws the activity's trains where they are (simulated
    // vehicles, built each frame).
    const bool atStart = !Sim::SimulationController::instance()->shows(current);
    if (player != nullptr && shows(MapActivityKind::PlayerPath)) {
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
        if (shows(MapActivityKind::LooseConsists))
            collect(current->activityObjects, consists);
        if (shows(MapActivityKind::SpeedZones))
            collect(current->restrictedSpeedZone, zones);
        if (shows(MapActivityKind::FailedSignals))
            collect(current->activityFailedSignal, failedSignals);
        if (shows(MapActivityKind::Events)) {
            // By the last run's results while they are for the activity as it is.
            bool fresh = false;
            const Sim::EventResults *results = Sim::EventResultStore::instance()->of(current, &fresh);
            for (int i = 0; i < current->event.size(); ++i) {
                ActivityEvent *event = current->event[i];
                if (event == nullptr)
                    continue;
                int state = 0;
                if (results != nullptr && fresh && size_t(i) < results->events.size() && results->events[size_t(i)].inRun) {
                    const Sim::EventState &run = results->events[size_t(i)].state;
                    state = run.fired > 0 ? 1 : run.level <= 0 ? 2 : 0;
                }
                event->getMapFeatures(events[state], tileX, tileZ);
            }
        }
        const bool trains = shows(MapActivityKind::Trains);
        if (player != nullptr && atStart && trains) {
            MapFeatures own;
            player->getMapFeatures(own, tileX, tileZ);
            consists.append(own);
            Consist *train = player->getConsistPointer();
            if (train != nullptr && train->isSelected())
                selected.append(own);
        }
        // The AI trains shown: paths (the chosen one apart) and consists.
        for (int i : current->shownAITrains()) {
            Service *service = current->aiTrainService(i);
            ::Path *trainPath = service->getPathPointer();
            if (trainPath != nullptr && shows(MapActivityKind::AIPaths))
                trainPath->getMapFeatures(i == current->shownAITrain ? chosenPath : aiPaths, tileX, tileZ);
            if (!atStart || !trains)
                continue;
            MapFeatures own;
            service->getMapFeatures(own, tileX, tileZ);
            consists.append(own);
            Consist *train = service->getConsistPointer();
            if (train != nullptr && train->isSelected())
                selected.append(own);
        }
    }

    const float mpp = view.metresPerPixel;
    const float border = BorderPixels * mpp;
    const float radius = 0.5f * MarkerPixels * mpp;
    std::vector<float> bandVertices, aiBandVertices, chosenBandVertices, haloVertices,
            borderVertices, areaVertices[3], fillVertices[GroupCount];
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
    circles(path.starts, PathStart);
    circles(path.ends, PathEnd);
    lines(path.brokenLines, PathBroken);
    circles(path.brokenPoints, PathBroken);
    // AI trains' paths: their bands under the player's, nodes as the
    // player's.
    for (auto [features, vertices, height] : {std::make_tuple(&aiPaths, &aiBandVertices, AIPathHeight),
                                               std::make_tuple(&chosenPath, &chosenBandVertices, ChosenPathHeight)}) {
        setHeight(features->lines, height);
        TrackMapLayer::appendRibbons(*vertices, features->lines.data(), int(features->lines.size() / 6),
                                     PathPixels * mpp);
        circles(features->points, Path);
        circles(features->starts, PathStart);
        circles(features->ends, PathEnd);
        lines(features->brokenLines, PathBroken);
        circles(features->brokenPoints, PathBroken);
    }
    // Consists.
    const int vehicleCount = int(consists.vehicles.size()) / MapFeatures::VehicleFloats;
    appendVehicles(consists, mpp, borderVertices, fillVertices[Vehicles], fillVertices[Engines]);
    // Speed zones and failed failedSignals.
    lines(zones.lines, SpeedZone);
    circles(zones.points, SpeedZone);
    circles(failedSignals.points, FailedSignal);
    // Location events: squares, north up, and rings at the trigger radius.
    std::vector<float> ring;
    for (int state = 0; state < 3; ++state) {
        const int group = state == 1 ? EventFired : state == 2 ? EventInactive : Event;
        const MapFeatures &shown = events[state];
        for (size_t i = 0; i + 2 < shown.points.size(); i += 3) {
            const float x = shown.points[i], z = shown.points[i + 2];
            TrackMapLayer::appendSquare(borderVertices, x, BorderHeight, z,
                                        MarkerPixels * mpp + 2.0f * border, 1, 0, 0, -1);
            TrackMapLayer::appendSquare(fillVertices[group], x, FillHeight + group, z,
                                        MarkerPixels * mpp, 1, 0, 0, -1);
        }
        for (size_t i = 0; i + 2 < shown.areas.size(); i += 3) {
            const float x = shown.areas[i], z = shown.areas[i + 1], r = shown.areas[i + 2];
            ring.clear();
            for (int s = 0; s < RingSegments; ++s)
                for (int end = 0; end < 2; ++end) {
                    const float angle = (s + end) * 6.28318531f / RingSegments;
                    ring.insert(ring.end(), {x + r * std::cos(angle), AreaHeight,
                                             z + r * std::sin(angle)});
                }
            TrackMapLayer::appendRibbons(areaVertices[state], ring.data(), RingSegments, BorderPixels * mpp);
        }
    }

    // Halos around what is selected.
    const float halo = border + HaloPixels * mpp;
    setHeight(selected.lines, HaloHeight);
    TrackMapLayer::appendRibbons(haloVertices, selected.lines.data(),
                                 int(selected.lines.size() / 6), LinePixels * mpp + 2.0f * halo);
    for (size_t i = 0; i + 2 < selected.points.size(); i += 3)
        TrackItemMapLayer::appendCircle(haloVertices, selected.points[i], HaloHeight,
                                        selected.points[i + 2], radius + halo);
    appendHalos(selected, mpp, haloVertices);

    auto upload = [](OglObj &object, std::vector<float> &vertices, const QColor &colour) {
        object.setMaterial(float(colour.redF()), float(colour.greenF()), float(colour.blueF()));
        object.init(vertices.data(), int(vertices.size()), RenderItem::V, GL_TRIANGLES);
    };
    upload(*pathBand, bandVertices, palette.path);
    upload(*aiBand, aiBandVertices, palette.aiPath);
    upload(*chosenBand, chosenBandVertices, palette.aiPathChosen);
    upload(*halos, haloVertices, palette.selection);
    upload(*borders, borderVertices, palette.itemBorder);
    upload(*areas[0], areaVertices[0], palette.event);
    upload(*areas[1], areaVertices[1], palette.eventFired);
    upload(*areas[2], areaVertices[2], palette.eventInactive);
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
                          << failedSignals.points.size() / 3 << "events"
                          << (events[0].points.size() + events[1].points.size() + events[2].points.size()) / 3
                          << "ms" << timer.nsecsElapsed() / 1e6;
}

void ActivityMapLayer::pushRenderItems(RenderQueue &queue, const MapView &view,
                                       const MapPalette &palette, Route *route, bool activity,
                                       bool paths, unsigned kinds) {
    if (!activity && !paths)
        return;
    shownKinds = kinds;
    std::vector<const void *> current = sources(route, activity, paths);
    const float scale = view.metresPerPixel / std::max(builtMetresPerPixel, 1e-6f);
    if (!valid || view.tileX != builtTileX || view.tileZ != builtTileZ || scale > RebuildScale
            || scale < 1.0f / RebuildScale || palette.name != builtPalette
            || current != builtSources || kinds != builtKinds) {
        build(view, palette, route, activity, paths);
        builtSources = std::move(current);
        builtKinds = kinds;
    }
    aiBand->pushRenderItem(queue);
    chosenBand->pushRenderItem(queue);
    pathBand->pushRenderItem(queue);
    halos->pushRenderItem(queue);
    for (auto &area : areas)
        area->pushRenderItem(queue);
    borders->pushRenderItem(queue);
    for (auto &fill : fills)
        fill->pushRenderItem(queue);
    pushSimulated(queue, view, palette, route, activity);
}

void ActivityMapLayer::appendVehicles(const MapFeatures &features, float mpp, std::vector<float> &border,
                                      std::vector<float> &vehicleFill, std::vector<float> &engineFill) {
    const float margin = BorderPixels * mpp;
    const int count = int(features.vehicles.size()) / MapFeatures::VehicleFloats;
    for (int i = 0; i < count; ++i) {
        const float *vehicle = features.vehicles.data() + i * MapFeatures::VehicleFloats;
        const bool engine = vehicle[6] > 0.5f;
        // Wagons a little shorter than they are, so the borders part them.
        float shorter[MapFeatures::VehicleFloats];
        std::copy(vehicle, vehicle + MapFeatures::VehicleFloats, shorter);
        shorter[4] = std::max(0.0f, shorter[4] - 2.0f * margin);
        appendVehicle(border, shorter, BorderHeight, margin, MinVehiclePixels * mpp);
        appendVehicle(engine ? engineFill : vehicleFill, shorter, FillHeight + (engine ? Engines : Vehicles),
                      0.0f, MinVehiclePixels * mpp);
    }
}

void ActivityMapLayer::appendHalos(const MapFeatures &features, float mpp, std::vector<float> &out) {
    const float halo = (BorderPixels + HaloPixels) * mpp;
    for (size_t i = 0; i + MapFeatures::VehicleFloats <= features.vehicles.size();
         i += MapFeatures::VehicleFloats)
        appendVehicle(out, features.vehicles.data() + i, HaloHeight, halo, MinVehiclePixels * mpp);
}

// The simulated trains, built again for each frame of the simulation (and
// when the view moved): few vertices, so it does not wait for the rest.
void ActivityMapLayer::pushSimulated(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                                     Route *route, bool activity) {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    Activity *current = activity && route != nullptr ? route->getCurrentActivity() : nullptr;
    if (!simulation->shows(current))
        return;
    const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->viewFrame();
    if (frame.get() != simulatedFrame || simulation->chosen() != simulatedChosen || view.tileX != simulatedTileX
            || view.tileZ != simulatedTileZ || view.metresPerPixel != simulatedMetresPerPixel
            || palette.name != simulatedPalette) {
        simulatedFrame = frame.get();
        simulatedChosen = simulation->chosen();
        simulatedTileX = view.tileX;
        simulatedTileZ = view.tileZ;
        simulatedMetresPerPixel = view.metresPerPixel;
        simulatedPalette = palette.name;
        const float mpp = view.metresPerPixel;
        MapFeatures vehicles, chosen;
        simulation->getMapFeatures(vehicles, view.tileX, view.tileZ);
        if (simulatedChosen >= 0)
            simulation->getMapFeatures(chosen, view.tileX, view.tileZ, simulatedChosen);
        std::vector<float> haloVertices, borderVertices, fillVertices[2];
        appendHalos(chosen, mpp, haloVertices);
        appendVehicles(vehicles, mpp, borderVertices, fillVertices[0], fillVertices[1]);
        auto upload = [](OglObj &object, std::vector<float> &vertices, const QColor &colour) {
            object.setMaterial(float(colour.redF()), float(colour.greenF()), float(colour.blueF()));
            object.init(vertices.data(), int(vertices.size()), RenderItem::V, GL_TRIANGLES);
        };
        upload(*simulatedHalos, haloVertices, palette.selection);
        upload(*simulatedBorders, borderVertices, palette.itemBorder);
        upload(*simulatedFills[0], fillVertices[0], colour(palette, Vehicles));
        upload(*simulatedFills[1], fillVertices[1], colour(palette, Engines));
    }
    if (shows(MapActivityKind::Trains)) {
        simulatedHalos->pushRenderItem(queue);
        simulatedBorders->pushRenderItem(queue);
        for (auto &fill : simulatedFills)
            fill->pushRenderItem(queue);
    }
    if (!shows(MapActivityKind::SimulatedTrack))
        return;
    if (frame->sectionUses.get() != trackUses || frame->aspects.get() != trackAspects || view.tileX != trackTileX
            || view.tileZ != trackTileZ || view.metresPerPixel != trackMetresPerPixel || palette.name != trackPalette) {
        trackUses = frame->sectionUses.get();
        trackAspects = frame->aspects.get();
        trackTileX = view.tileX;
        trackTileZ = view.tileZ;
        trackMetresPerPixel = view.metresPerPixel;
        trackPalette = palette.name;
        buildSimulatedTrack(view, palette);
    }
    for (auto &track : simulatedTrack)
        track->pushRenderItem(queue);
    aspectBorders->pushRenderItem(queue);
    for (auto &fill : aspectFills)
        fill->pushRenderItem(queue);
}

void ActivityMapLayer::buildSimulatedTrack(const MapView &view, const MapPalette &palette) {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const std::shared_ptr<const Sim::SimulationInput> input = simulation->input();
    const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->viewFrame();
    TDB *tdb = Game::trackDB;
    const float mpp = view.metresPerPixel;
    std::vector<float> trackVertices[2], borderVertices, fillVertices[3];
    if (input != nullptr && input->network != nullptr && tdb != nullptr) {
        const Sim::TrackNetwork &network = *input->network;
        // Occupied and reserved track sections (junctions and crossings are
        // points: the track either side shows them).
        if (frame->sectionUses != nullptr) {
            const std::vector<Sim::SimulationCore::SectionUse> &uses = *frame->sectionUses;
            std::vector<float> segments[2];
            for (size_t k = 0; k < uses.size() && k < network.sections().size(); ++k) {
                const Sim::TrackSection &section = network.sections()[k];
                if (uses[k].holder == Sim::NoHolder || section.kind != Sim::TrackSection::Track)
                    continue;
                tdb->getTrackSegments(segments[uses[k].occupied ? 0 : 1], section.node, section.from, section.to,
                                      view.tileX, view.tileZ);
            }
            for (int i = 0; i < 2; ++i) {
                setHeight(segments[i], SimulatedTrackHeight + i);
                TrackMapLayer::appendRibbons(trackVertices[i], segments[i].data(), int(segments[i].size() / 6),
                                             1.5f * LinePixels * mpp);
            }
        }
        // Each signal by its least restrictive head (NORMAL heads where it
        // has them): stop, caution, clear.
        if (placesRun != input->runId || signalPlaces.size() != network.signalGroups().size()) {
            placesRun = input->runId;
            signalPlaces.assign(network.signalGroups().size(), Place());
            for (size_t g = 0; g < network.signalGroups().size(); ++g) {
                const Sim::SignalGroup &group = network.signalGroups()[g];
                // Its place and one a metre further the node's way (or back
                // at the node's end).
                float at[8], next[8];
                const float step = group.distance >= 1.0f ? -1.0f : 1.0f;
                if (!tdb->getDrawPositionOnTrNode(at, group.node, group.distance)
                        || !tdb->getDrawPositionOnTrNode(next, group.node, group.distance + step))
                    continue;
                Place &place = signalPlaces[g];
                place.known = true;
                place.tileX = int(at[5]);
                place.tileZ = int(at[6]);
                place.x = at[0];
                place.z = at[2];
                // On the map (the item layer's tile convention).
                float dx = float(int(next[5]) - place.tileX) * 2048.0f + next[0] - place.x;
                float dz = -float(int(next[6]) - place.tileZ) * 2048.0f - (next[2] - place.z);
                const float length = std::hypot(dx, dz);
                if (length > 1e-4f) {
                    const float sign = (step > 0.0f) == group.forward ? 1.0f : -1.0f;
                    place.dx = sign * dx / length;
                    place.dz = sign * dz / length;
                }
            }
        }
        if (frame->aspects != nullptr) {
            const std::vector<int> &aspects = *frame->aspects;
            const float size = 1.6f * MarkerPixels * mpp;
            const float border = 2.0f * BorderPixels * mpp;
            // An arrowhead about its centre, pointing (dx, dz), grown by `grow`.
            auto arrow = [](std::vector<float> &out, float x, float y, float z, float dx, float dz, float length,
                            float grow) {
                const float tip = 0.55f * length + grow, back = 0.45f * length + grow, half = 0.42f * length + grow;
                const float px = -dz, pz = dx;
                // Wound as the map's other shapes (appendCircle).
                const float triangle[9] = {x + dx * tip, y, z + dz * tip,
                                           x - dx * back - px * half, y, z - dz * back - pz * half,
                                           x - dx * back + px * half, y, z - dz * back + pz * half};
                out.insert(out.end(), triangle, triangle + 9);
            };
            for (size_t g = 0; g < network.signalGroups().size() && g < signalPlaces.size(); ++g) {
                const Sim::SignalGroup &group = network.signalGroups()[g];
                if (!signalPlaces[g].known)
                    continue;
                int best = -1;
                for (int head : group.heads)
                    if (size_t(head) < aspects.size() && (!group.normal || network.isNormal(network.heads()[size_t(head)])))
                        best = std::max(best, aspects[size_t(head)]);
                if (best < 0)
                    continue;
                const int kind = best <= SignalType::STOP ? 0 : best < SignalType::CLEAR_1 ? 1 : 2;
                const Place &place = signalPlaces[g];
                const float x = float(place.tileX - view.tileX) * 2048.0f + place.x;
                const float z = float(-place.tileZ - view.tileZ) * 2048.0f - place.z;
                arrow(borderVertices, x, AspectBorderHeight, z, place.dx, place.dz, size, border);
                arrow(fillVertices[kind], x, AspectHeight + kind, z, place.dx, place.dz, size, 0.0f);
            }
        }
    }
    auto upload = [](OglObj &object, std::vector<float> &vertices, const QColor &colour) {
        object.setMaterial(float(colour.redF()), float(colour.greenF()), float(colour.blueF()));
        object.init(vertices.data(), int(vertices.size()), RenderItem::V, GL_TRIANGLES);
    };
    upload(*simulatedTrack[0], trackVertices[0], palette.occupied);
    upload(*simulatedTrack[1], trackVertices[1], palette.reserved);
    upload(*aspectBorders, borderVertices, palette.itemBorder);
    upload(*aspectFills[0], fillVertices[0], palette.aspectStop);
    upload(*aspectFills[1], fillVertices[1], palette.aspectCaution);
    upload(*aspectFills[2], fillVertices[2], palette.aspectClear);
}

void ActivityMapLayer::pushSelection(MapSelection &selection, const MapView &view, Route *route,
                                     bool activity, unsigned kinds) const {
    auto shown = [kinds](MapActivityKind kind) { return MapLayers::has(kinds, int(kind)); };
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
    for (int i = 0; i < current->activityObjects.size() && shown(MapActivityKind::LooseConsists); ++i) {
        ActivityObject *object = current->activityObjects[i];
        if (object == nullptr || !SelectionIdCodec::tryActivityObject(i, 0, id))
            continue;
        MapFeatures own;
        object->getMapFeatures(own, tileX, tileZ);
        vehicles(own, id);
    }
    for (int i = 0; i < current->restrictedSpeedZone.size() && shown(MapActivityKind::SpeedZones); ++i) {
        ActivityObject *object = current->restrictedSpeedZone[i];
        if (object == nullptr || !SelectionIdCodec::tryActivityObject(3000 + i, 0, id))
            continue;
        MapFeatures own;
        object->getMapFeatures(own, tileX, tileZ);
        points(own, id, true);
    }
    for (int i = 0; i < current->activityFailedSignal.size() && shown(MapActivityKind::FailedSignals); ++i) {
        ActivityObject *object = current->activityFailedSignal[i];
        if (object == nullptr || !SelectionIdCodec::tryActivityObject(3500 + i, 0, id))
            continue;
        MapFeatures own;
        object->getMapFeatures(own, tileX, tileZ);
        points(own, id, false);
    }
    if (!shown(MapActivityKind::Trains))
        return;
    // Simulated trains where they are, with the ids of their trains.
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    if (simulation->shows(current)) {
        for (int train = 0; train < int(simulation->viewFrame()->trains.size()); ++train) {
            if (!SelectionIdCodec::tryActivityService(simulation->serviceId(train), 0, id))
                continue;
            MapFeatures own;
            simulation->getMapFeatures(own, tileX, tileZ, train);
            vehicles(own, id);
        }
        return;
    }
    Service *player = current->playerService();
    if (player != nullptr && SelectionIdCodec::tryActivityService(0, 0, id)) {
        MapFeatures own;
        player->getMapFeatures(own, tileX, tileZ);
        vehicles(own, id);
    }
    // AI trains: their place in the activity's traffic + 1, as in 3D.
    for (int i : current->shownAITrains()) {
        if (!SelectionIdCodec::tryActivityService(i + 1, 0, id))
            continue;
        MapFeatures own;
        current->aiTrainService(i)->getMapFeatures(own, tileX, tileZ);
        vehicles(own, id);
    }
}
