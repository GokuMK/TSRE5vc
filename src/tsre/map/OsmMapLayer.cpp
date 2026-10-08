/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "OsmMapLayer.h"
#include "MapPalette.h"
#include "MapView.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QOpenGLFunctions>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <tsre/Game.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/geo/osm/OsmMapGeometry.h>
#include <tsre/geo/osm/OsmOverview.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>

namespace {

bool trace() {
    static const bool on = qEnvironmentVariableIsSet("TSRE_MAP_TRACE");
    return on;
}

// The overview level a scale uses by the standard rules; -1 for the detail file.
// OsmLayers falls back to finer data when a level is not built, which needs no reload.
int levelOf(double metresPerPixel) {
    const Osm::OverviewConfig &config = Osm::OverviewConfig::standard();
    int level = -1;
    for (size_t l = 0; l < config.levels.size(); ++l)
        if (metresPerPixel >= config.levels[l].fromMetersPerPixel)
            level = int(l);
    return level;
}

// Whether the class table draws the same styles at both scales.
bool sameStyles(double a, double b) {
    static const std::vector<float> ranges = Osm::FeatureClasses::standard().scaleRanges();
    for (float t : ranges)
        if ((a <= t) != (b <= t))
            return false;
    return true;
}

int glPrimitive(Osm::MapBatch::Primitive primitive) {
    switch (primitive) {
    case Osm::MapBatch::TriangleStrip: return GL_TRIANGLE_STRIP;
    case Osm::MapBatch::Lines: return GL_LINES;
    default: return GL_TRIANGLES;
    }
}

}

struct OsmMapLayer::Job {
    enum Kind { Load, Strokes };
    Kind kind = Load;
    uint64_t id = 0;
    QString directory;
    int tileX = 0;
    int tileZ = 0;
    float rect[4] = {0, 0, 0, 0};
    double metresPerPixel = 1;
    std::shared_ptr<const Osm::MapGeometry> geometry;  // Strokes: what to stroke
};

struct OsmMapLayer::Result {
    Job::Kind kind = Job::Load;
    uint64_t id = 0;
    bool ok = false;
    QString error;
    int tileX = 0;
    int tileZ = 0;
    double metresPerPixel = 1;
    // Load: the geometry loaded; Strokes: the geometry stroked.
    std::shared_ptr<const Osm::MapGeometry> geometry;
    std::vector<Osm::MapBatch> fills;
    std::vector<Osm::MapBatch> strokes;
};

// One thread running the newest job; a newer load cancels a running one.
class OsmMapLayer::Worker {
public:
    Worker() : thread([this] { run(); }) {}
    ~Worker() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
            cancel = true;
        }
        wake.notify_one();
        thread.join();
    }
    void post(Job job) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            // A waiting load stays: it brings its own strokes.
            if (job.kind == Job::Strokes && hasPending && pending.kind == Job::Load)
                return;
            if (job.kind == Job::Load && working && running == Job::Load)
                cancel = true;
            pending = std::move(job);
            hasPending = true;
        }
        wake.notify_one();
    }
    bool take(Result &out) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!hasResult)
            return false;
        out = std::move(result);
        hasResult = false;
        return true;
    }
    bool busy() {
        std::lock_guard<std::mutex> lock(mutex);
        return hasPending || working;
    }
    std::function<void()> ready;

private:
    void run();
    Result load(const Job &job);

    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
    bool hasPending = false;
    bool working = false;
    Job::Kind running = Job::Strokes;
    std::atomic_bool cancel{false};
    Job pending;
    bool hasResult = false;
    Result result;
    std::thread thread;
};

void OsmMapLayer::Worker::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return stop || hasPending; });
            if (stop)
                return;
            job = std::move(pending);
            hasPending = false;
            working = true;
            running = job.kind;
            cancel = false;
        }
        Result r;
        if (job.kind == Job::Load) {
            r = load(job);
        } else {
            r.kind = Job::Strokes;
            r.id = job.id;
            r.metresPerPixel = job.metresPerPixel;
            r.geometry = job.geometry;
            job.geometry->strokes(job.metresPerPixel, r.strokes);
            r.ok = true;
        }
        bool deliver = true;
        {
            std::lock_guard<std::mutex> lock(mutex);
            working = false;
            // A cancelled load is dropped; a newer job is waiting.
            deliver = r.ok || !cancel;
            if (deliver) {
                result = std::move(r);
                hasResult = true;
            }
        }
        if (deliver && ready)
            ready();
    }
}

OsmMapLayer::Result OsmMapLayer::Worker::load(const Job &job) {
    Result r;
    r.kind = Job::Load;
    r.id = job.id;
    r.tileX = job.tileX;
    r.tileZ = job.tileZ;
    r.metresPerPixel = job.metresPerPixel;
    QElapsedTimer timer;
    timer.start();
    GeoWorldCoordinateConverter *converter = Game::GeoCoordConverter;
    if (converter == nullptr) {
        r.error = QStringLiteral("the route has no geographic reference");
        return r;
    }
    const std::shared_ptr<const Osm::OsmLayers> layers = Osm::sharedLayers(job.directory, r.error);
    if (!layers)
        return r;
    const Osm::Box area = areaOf(converter, job.tileX, job.tileZ, job.rect);
    const int tileX = job.tileX, tileZ = job.tileZ;
    const Osm::MapProjection project = [converter, tileX, tileZ](const Osm::Location *in, size_t count,
                                                                  float *xz) {
        for (size_t i = 0; i < count; ++i)
            toGround(converter, in[i].lat(), in[i].lon(), tileX, tileZ, xz[2 * i], xz[2 * i + 1]);
    };
    Osm::MapGeometry::Options options;
    options.baseHeight = BaseHeight;
    options.heightStep = HeightStep;
    const auto geometry = std::make_shared<Osm::MapGeometry>();
    if (!geometry->load(layers->forScale(job.metresPerPixel), area, job.metresPerPixel, project,
                        options, r.error, &cancel))
        return r;
    const qint64 loadNs = timer.nsecsElapsed();
    r.fills = geometry->takeFills();
    geometry->strokes(job.metresPerPixel, r.strokes);
    r.geometry = geometry;
    r.ok = true;
    if (trace()) {
        const Osm::MapGeometry::Stats &s = geometry->stats();
        qInfo().noquote() << "osm-map load lon" << area.minX / Osm::CoordinateScale
                          << area.maxX / Osm::CoordinateScale << "lat" << area.minY / Osm::CoordinateScale
                          << area.maxY / Osm::CoordinateScale << "m/px" << job.metresPerPixel << "level"
                          << layers->levelForScale(job.metresPerPixel) << "ms" << loadNs / 1e6
                          << "(read" << s.readSeconds * 1e3 << "multipolygons"
                          << s.assembleSeconds * 1e3 << "simplify, project, triangulate" << s.processSeconds * 1e3
                          << ") total ms" << timer.nsecsElapsed() / 1e6 << "triangles" << s.triangles
                          << "polylines" << s.polylines << "culled" << s.culled;
    }
    return r;
}

// What is drawn: one object a batch, and the tile and scale it was built for.
struct OsmMapLayer::Drawn {
    struct Batch {
        std::unique_ptr<OglObj> object;
        Osm::Rgb color = 0;
    };
    std::shared_ptr<const Osm::MapGeometry> geometry;
    uint64_t loadId = 0;
    int tileX = 0;
    int tileZ = 0;
    std::vector<Batch> fills;
    std::vector<Batch> strokes;
    float fillAlpha = 1.0f;
};

OsmMapLayer::OsmMapLayer() : worker(std::make_unique<Worker>()), drawn(std::make_unique<Drawn>()) {}

OsmMapLayer::~OsmMapLayer() = default;

void OsmMapLayer::setReadyCallback(std::function<void()> callback) {
    // Set before the first job, so the worker never reads it while it changes.
    worker->ready = std::move(callback);
}

void OsmMapLayer::invalidate() {
    invalid = true;
}

bool OsmMapLayer::busy() const {
    return worker->busy();
}

void OsmMapLayer::toGround(GeoWorldCoordinateConverter *converter, double lat, double lon, int tileX,
                           int tileZ, float &x, float &z) {
    IghCoordinate igh;
    PreciseTileCoordinate tile;
    converter->ConvertToInternal(lat, lon, &igh);
    converter->ConvertToTile(&igh, &tile);
    // Converter tiles count z the other way (NaviWindow's jump); positions run 0 to 1.
    x = float(double(tile.TileX - tileX) * 2048.0 + tile.X * 2048.0 - 1024.0);
    z = float(double(-tile.TileZ - tileZ) * 2048.0 + tile.Z * 2048.0 - 1024.0);
}

void OsmMapLayer::toLatLon(GeoWorldCoordinateConverter *converter, int tileX, int tileZ, double x,
                           double z, double &lat, double &lon) {
    const int dx = int(std::floor((x + 1024.0) / 2048.0)), dz = int(std::floor((z + 1024.0) / 2048.0));
    const double fx = (x - dx * 2048.0 + 1024.0) / 2048.0, fz = (z - dz * 2048.0 + 1024.0) / 2048.0;
    IghCoordinate igh;
    LatitudeLongitudeCoordinate latLon;
    converter->ConvertToInternal(tileX + dx, -(tileZ + dz), fx, fz, &igh);
    converter->ConvertToLatLon(&igh, &latLon);
    lat = latLon.Latitude;
    lon = latLon.Longitude;
}

Osm::Box OsmMapLayer::areaOf(GeoWorldCoordinateConverter *converter, int tileX, int tileZ,
                             const float *rect) {
    double minLat = 1e9, maxLat = -1e9, minLon = 1e9, maxLon = -1e9;
    // A 5 x 5 grid: the projection bends edges, so corners alone could miss a bulge.
    for (int i = 0; i <= 4; ++i)
        for (int j = 0; j <= 4; ++j) {
            double lat, lon;
            toLatLon(converter, tileX, tileZ, rect[0] + (rect[1] - rect[0]) * i / 4.0,
                     rect[2] + (rect[3] - rect[2]) * j / 4.0, lat, lon);
            minLat = std::min(minLat, lat);
            maxLat = std::max(maxLat, lat);
            minLon = std::min(minLon, lon);
            maxLon = std::max(maxLon, lon);
        }
    const double padLat = 0.01 * (maxLat - minLat), padLon = 0.01 * (maxLon - minLon);
    return Osm::Box::fromDegrees(minLon - padLon, minLat - padLat, maxLon + padLon, maxLat + padLat);
}

void OsmMapLayer::viewRect(const MapView &view, float *rect) {
    rect[0] = rect[2] = 1e30f;
    rect[1] = rect[3] = -1e30f;
    const float corners[4][2] = {{0, 0}, {float(view.width), 0}, {0, float(view.height)},
                                 {float(view.width), float(view.height)}};
    for (const auto &corner : corners) {
        float x, z;
        view.groundAt(corner[0], corner[1], x, z);
        rect[0] = std::min(rect[0], x);
        rect[1] = std::max(rect[1], x);
        rect[2] = std::min(rect[2], z);
        rect[3] = std::max(rect[3], z);
    }
}

void OsmMapLayer::apply(Result &result, const MapPalette &palette, bool transparentAreas) {
    if (!result.ok) {
        if (!result.error.isEmpty())
            qWarning().noquote() << "OSM map layer:" << result.error;
        return;
    }
    auto upload = [](std::vector<Drawn::Batch> &objects, std::vector<Osm::MapBatch> &batches,
                     float alpha) {
        objects.resize(batches.size());
        for (size_t i = 0; i < batches.size(); ++i) {
            Osm::MapBatch &b = batches[i];
            Drawn::Batch &d = objects[i];
            if (!d.object)
                d.object = std::make_unique<OglObj>();
            d.color = b.color;
            d.object->setMaterial(((b.color >> 16) & 255) / 255.0f, ((b.color >> 8) & 255) / 255.0f,
                                  (b.color & 255) / 255.0f, alpha);
            d.object->init(b.vertices.data(), int(b.vertices.size()), RenderItem::V,
                           glPrimitive(b.primitive));
            std::vector<float>().swap(b.vertices);
        }
    };
    if (result.kind == Job::Load) {
        drawn->geometry = result.geometry;
        drawn->loadId = result.id;
        drawn->tileX = result.tileX;
        drawn->tileZ = result.tileZ;
        drawn->fillAlpha = transparentAreas ? palette.osmAreaAlpha : 1.0f;
        upload(drawn->fills, result.fills, drawn->fillAlpha);
    } else if (drawn->geometry != result.geometry) {
        return;  // stroked from geometry since replaced
    }
    upload(drawn->strokes, result.strokes, 1.0f);
}

void OsmMapLayer::request(const MapView &view, const QString &directory) {
    float rect[4];
    viewRect(view, rect);
    const double mpp = view.metresPerPixel;
    const int level = levelOf(mpp);
    // The view in the coordinates of the tile last asked for.
    const float ox = float(view.tileX - requestedTile[0]) * 2048.0f;
    const float oz = float(view.tileZ - requestedTile[1]) * 2048.0f;
    const bool covered = rect[0] + ox >= requestedRect[0] && rect[1] + ox <= requestedRect[1]
            && rect[2] + oz >= requestedRect[2] && rect[3] + oz <= requestedRect[3];
    const double scale = mpp / std::max(requestedScale, 1e-9);
    const bool reload = !requested || invalid || directory != requestedDirectory || !covered
            || level != requestedLevel || scale > ReloadScale || scale < 1.0 / ReloadScale
            || !sameStyles(mpp, requestedScale);
    static uint64_t nextId = 0;
    if (reload) {
        Job job;
        job.kind = Job::Load;
        job.id = ++nextId;
        job.directory = directory;
        job.tileX = view.tileX;
        job.tileZ = view.tileZ;
        const float mx = Margin * (rect[1] - rect[0]), mz = Margin * (rect[3] - rect[2]);
        const float built[4] = {rect[0] - mx, rect[1] + mx, rect[2] - mz, rect[3] + mz};
        std::copy(built, built + 4, job.rect);
        job.metresPerPixel = mpp;
        worker->post(std::move(job));
        requested = true;
        invalid = false;
        requestedDirectory = directory;
        requestedTile[0] = view.tileX;
        requestedTile[1] = view.tileZ;
        std::copy(built, built + 4, requestedRect);
        requestedScale = mpp;
        requestedLevel = level;
        requestedStrokeScale = mpp;
        requestedLoad = nextId;
        return;
    }
    // Same data: only the line widths follow the scale.
    const double strokeScale = mpp / std::max(requestedStrokeScale, 1e-9);
    if (drawn->geometry != nullptr && drawn->loadId == requestedLoad
            && (strokeScale > RestrokeScale || strokeScale < 1.0 / RestrokeScale)) {
        Job job;
        job.kind = Job::Strokes;
        job.id = ++nextId;
        job.metresPerPixel = mpp;
        job.geometry = drawn->geometry;
        worker->post(std::move(job));
        requestedStrokeScale = mpp;
    }
}

void OsmMapLayer::pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                                  const QString &directory, bool transparentAreas) {
    Result result;
    while (worker->take(result))
        apply(result, palette, transparentAreas);
    request(view, directory);

    const float alpha = transparentAreas ? palette.osmAreaAlpha : 1.0f;
    if (alpha != drawn->fillAlpha) {
        drawn->fillAlpha = alpha;
        for (Drawn::Batch &b : drawn->fills)
            b.object->setMaterial(((b.color >> 16) & 255) / 255.0f, ((b.color >> 8) & 255) / 255.0f,
                                  (b.color & 255) / 255.0f, alpha);
    }
    if (drawn->fills.empty() && drawn->strokes.empty())
        return;
    // Built relative to its own tile: shift it to the view's.
    float *transform = queue.transform();
    Mat4::identity(transform);
    Mat4::translate(transform, transform, float(drawn->tileX - view.tileX) * 2048.0f, 0.0f,
                    float(drawn->tileZ - view.tileZ) * 2048.0f);
    for (Drawn::Batch &b : drawn->fills)
        b.object->pushRenderItem(queue);
    for (Drawn::Batch &b : drawn->strokes)
        b.object->pushRenderItem(queue);
    Mat4::identity(transform);
}
