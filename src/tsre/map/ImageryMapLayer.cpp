/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ImageryMapLayer.h"
#include "MapView.h"
#include "OsmMapLayer.h"
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QImage>
#include <QOpenGLFunctions>
#include <QSet>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <settings/SettingsAccess.h>
#include <tsre/Game.h>
#include <tsre/geo/ImagerySource.h>
#include <tsre/geo/osm/OsmThread.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>

namespace {

constexpr double Pi = 3.14159265358979323846;
// A tile that failed is asked for again after this long.
constexpr qint64 RetryFailedMs = 30000;
// The fetch thread keeps its connection this long after the last tile.
constexpr qint64 KeepConnectionMs = 30000;

bool trace() {
    static const bool on = qEnvironmentVariableIsSet("TSRE_MAP_TRACE");
    return on;
}

// Mesh cells a tile side: enough that the projection's bend stays under a pixel.
int cellsFor(int zoom) {
    return zoom <= 4 ? 16 : zoom <= 7 ? 8 : zoom <= 10 ? 4 : 2;
}

}

struct ImageryMapLayer::Source {
    Imagery::Dataset dataset;
    QString root;
    int tilesPerRow = 8;  // in a page
};

// The tile ground positions: (cells + 1)^2 points, row by row from the north-west
// corner, relative to the tile they were computed for.
struct ImageryMapLayer::Mesh {
    int cells = 0;
    int tileX = 0, tileZ = 0;
    std::vector<float> xz;
};

struct ImageryMapLayer::Page {
    Texture *texture = nullptr;
    int textureId = -1;
};

struct ImageryMapLayer::Slot {
    uint64_t key = 0;
    bool used = false;
    uint64_t lastUsed = 0;
};

// One tile drawn: the tile's place, and the loaded tile whose pixels it shows (itself
// or an ancestor, of which it shows its part).
struct ImageryMapLayer::Draw {
    uint64_t tile = 0;
    uint64_t image = 0;
    bool operator==(const Draw &o) const { return tile == o.tile && image == o.image; }
};

// The tiles the view misses, fetched on a thread through Imagery::fetchTiles (the
// cache and requests of Load Imagery). The newest list replaces the waiting one.
class ImageryMapLayer::Fetcher {
public:
    struct Delivery {
        uint64_t key = 0;
        QImage image;  // RGBA8888
        QString error;
    };
    Fetcher(Imagery::Dataset dataset, QString root, std::function<void()> ready)
        : dataset(std::move(dataset)), root(std::move(root)), ready(std::move(ready)), thread([this] { run(); }) {}
    ~Fetcher() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        cancel = true;
        wake.notify_one();
        thread.join();
    }
    void want(const std::vector<uint64_t> &keys) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            wanted.assign(keys.begin(), keys.end());
        }
        wake.notify_one();
    }
    std::vector<Delivery> take() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<Delivery> out;
        out.swap(deliveries);
        for (const Delivery &d : out)
            pending.remove(d.key);
        return out;
    }
    bool busy() {
        std::lock_guard<std::mutex> lock(mutex);
        return !wanted.empty() || !pending.isEmpty();
    }

private:
    void run();

    const Imagery::Dataset dataset;
    const QString root;
    const std::function<void()> ready;
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
    std::atomic_bool cancel{false};
    std::deque<uint64_t> wanted;
    QSet<uint64_t> pending;  // fetching, or delivered and not taken
    std::vector<Delivery> deliveries;
    Osm::Thread thread;
};

void ImageryMapLayer::Fetcher::run() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return stop || !wanted.empty(); });
            if (stop)
                return;
        }
        QElapsedTimer quiet;
        quiet.start();
        Imagery::fetchTiles(dataset, root, ParallelRequests, cancel,
            [this, &quiet](Imagery::TileAddress &tile) {
                std::lock_guard<std::mutex> lock(mutex);
                while (!wanted.empty()) {
                    const uint64_t k = wanted.front();
                    wanted.pop_front();
                    if (pending.contains(k))
                        continue;
                    pending.insert(k);
                    tile = {zoomOf(k), columnOf(k), rowOf(k)};
                    quiet.restart();
                    return true;
                }
                return false;
            },
            [this](Imagery::TileFetch &&fetched) {
                Delivery d;
                d.key = key(fetched.tile.zoom, fetched.tile.column, fetched.tile.row);
                if (fetched.error.isEmpty())
                    d.image = fetched.image.convertToFormat(QImage::Format_RGBA8888);
                else
                    d.error = fetched.error;
                bool first;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    first = deliveries.empty();
                    deliveries.push_back(std::move(d));
                }
                if (first && ready)
                    ready();
            },
            [this, &quiet] {
                std::lock_guard<std::mutex> lock(mutex);
                return !stop && (!wanted.empty() || quiet.elapsed() < KeepConnectionMs);
            });
    }
}

ImageryMapLayer::ImageryMapLayer() = default;

ImageryMapLayer::~ImageryMapLayer() = default;

void ImageryMapLayer::setReadyCallback(std::function<void()> callback) {
    ready = std::move(callback);
}

bool ImageryMapLayer::prepare(QString &error) {
    if (Game::GeoCoordConverter == nullptr) {
        error = QStringLiteral("the route has no geographic reference");
        return false;
    }
    const QString root = Settings::string("core.paths.geoData", SettingType::Directory);
    if (source && source->root == root)
        return true;
    QString catalogueError;
    const QVector<Imagery::Dataset> catalogue = Imagery::datasets(catalogueError);
    // The catalogue's default world source, else the first world tile source: tiles in
    // Web Mercator, no key, a size the pages divide into.
    auto usable = [](const Imagery::Dataset &d) {
        return d.provider == QLatin1String("wmts-kvp-webmercator") && d.apiKeySecret.isEmpty()
                && d.tilePixels >= 64 && PageSize % d.tilePixels == 0 && d.minLongitude <= -179.0
                && d.maxLongitude >= 179.0;
    };
    const QString defaultId = Imagery::defaultDistantSourceId(catalogue);
    const Imagery::Dataset *chosen = nullptr;
    for (const Imagery::Dataset &d : catalogue)
        if (d.id == defaultId && usable(d))
            chosen = &d;
    for (const Imagery::Dataset &d : catalogue)
        if (chosen == nullptr && usable(d))
            chosen = &d;
    if (chosen == nullptr) {
        error = catalogueError.isEmpty() ? QStringLiteral("the imagery catalogue has no world tile source")
                                         : catalogueError;
        return false;
    }
    fetcher.reset();
    source = std::make_unique<Source>();
    source->dataset = *chosen;
    source->root = root;
    source->tilesPerRow = PageSize / chosen->tilePixels;
    // Tiles already loaded are of the same source unless it changed.
    fetcher = std::make_unique<Fetcher>(source->dataset, root, ready);
    asked.clear();
    failedAt.clear();
    qInfo().noquote() << QStringLiteral("Imagery map: %1, cache %2").arg(chosen->name,
            root.isEmpty() ? QStringLiteral("off (no geodata directory)") : root);
    return true;
}

void ImageryMapLayer::pause() {
    if (fetcher && !asked.empty()) {
        fetcher->want({});
        asked.clear();
    }
}

bool ImageryMapLayer::busy() const {
    return fetcher && fetcher->busy();
}

QString ImageryMapLayer::attribution() const {
    if (!source)
        return QString();
    const Imagery::Dataset &d = source->dataset;
    QString text = d.name;
    if (!d.attribution.isEmpty())
        text += QStringLiteral(" © ") + d.attribution;
    if (!d.license.isEmpty())
        text += QStringLiteral(" (%1)").arg(d.license);
    return text;
}

int ImageryMapLayer::allocateSlot() {
    if (freeSlots.empty() && int(pages.size()) < MaxPages) {
        auto page = std::make_unique<Page>();
        page->texture = new Texture(PageSize, PageSize, 32);
        page->texture->pathid = QStringLiteral("tsre-map-imagery-%1-%2.:atlas")
                .arg(quintptr(this), 0, 16).arg(pages.size());
        page->textureId = TexLib::addTex(page->texture);
        page->texture->GLTextures();
        const int perPage = source->tilesPerRow * source->tilesPerRow;
        const int first = int(pages.size()) * perPage;
        tileSlots.resize(size_t(first + perPage));
        for (int i = first + perPage - 1; i >= first; --i)
            freeSlots.push_back(i);
        pages.push_back(std::move(page));
    }
    if (!freeSlots.empty()) {
        const int s = freeSlots.back();
        freeSlots.pop_back();
        return s;
    }
    // Full: the tile drawn longest ago, if not drawn in the last frame.
    int oldest = -1;
    for (int i = 0; i < int(tileSlots.size()); ++i)
        if (tileSlots[size_t(i)].used && (oldest < 0 || tileSlots[size_t(i)].lastUsed < tileSlots[size_t(oldest)].lastUsed))
            oldest = i;
    if (oldest < 0 || tileSlots[size_t(oldest)].lastUsed + 1 >= frame)
        return -1;
    slotOf.remove(tileSlots[size_t(oldest)].key);
    tileSlots[size_t(oldest)].used = false;
    return oldest;
}

void ImageryMapLayer::upload(int slot, const unsigned char *rgba) {
    const int perPage = source->tilesPerRow * source->tilesPerRow;
    const int size = source->dataset.tilePixels;
    const int i = slot % perPage;
    pages[size_t(slot / perPage)]->texture->updateRegion((i % source->tilesPerRow) * size,
                                                         (i / source->tilesPerRow) * size, size, size, rgba);
}

void ImageryMapLayer::takeDeliveries() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (Fetcher::Delivery &d : fetcher->take()) {
        if (!d.error.isEmpty()) {
            failedAt.insert(d.key, now);
            if (!warned)
                qWarning().noquote() << "Imagery map:" << d.error;
            warned = true;
            continue;
        }
        failedAt.remove(d.key);
        if (slotOf.contains(d.key) || d.image.width() != source->dataset.tilePixels)
            continue;
        const int s = allocateSlot();
        if (s < 0)
            continue;
        upload(s, d.image.constBits());
        Slot &slot = tileSlots[size_t(s)];
        slot.key = d.key;
        slot.used = true;
        slot.lastUsed = frame;
        slotOf.insert(d.key, s);
    }
}

const ImageryMapLayer::Mesh &ImageryMapLayer::meshFor(uint64_t k) {
    auto found = meshes.find(k);
    if (found != meshes.end())
        return *found;
    if (meshes.size() > 20000)
        meshes.clear();
    Mesh mesh;
    const int zoom = zoomOf(k), column = columnOf(k), row = rowOf(k);
    mesh.cells = cellsFor(zoom);
    mesh.tileX = meshTile[0];
    mesh.tileZ = meshTile[1];
    mesh.xz.resize(size_t((mesh.cells + 1) * (mesh.cells + 1) * 2));
    const double tiles = std::ldexp(1.0, zoom);
    float *out = mesh.xz.data();
    for (int j = 0; j <= mesh.cells; ++j) {
        const double y = (row + double(j) / mesh.cells) / tiles;
        const double lat = std::atan(std::sinh(Pi * (1.0 - 2.0 * y))) * 180.0 / Pi;
        for (int i = 0; i <= mesh.cells; ++i) {
            const double lon = (column + double(i) / mesh.cells) / tiles * 360.0 - 180.0;
            OsmMapLayer::toGround(Game::GeoCoordConverter, lat, lon, mesh.tileX, mesh.tileZ, out[0], out[1]);
            out += 2;
        }
    }
    return *meshes.insert(k, std::move(mesh));
}

// The tiles the view shows at the scale's zoom, each drawn by itself, its children or
// an ancestor; and the missing ones, nearest the centre first.
bool ImageryMapLayer::collect(const MapView &view, std::vector<Draw> &draws, std::vector<uint64_t> &missing) {
    GeoWorldCoordinateConverter *converter = Game::GeoCoordConverter;
    if (converter == nullptr)
        return false;
    const Imagery::Dataset &d = source->dataset;
    // New meshes are kept relative to the view's tile, so floats keep their precision.
    meshTile[0] = view.tileX;
    meshTile[1] = view.tileZ;
    float rect[4];
    OsmMapLayer::viewRect(view, rect);
    const Osm::Box box = OsmMapLayer::areaOf(converter, view.tileX, view.tileZ, rect);
    const double minLon = box.minX / Osm::CoordinateScale, maxLon = box.maxX / Osm::CoordinateScale;
    const double minLat = box.minY / Osm::CoordinateScale, maxLat = box.maxY / Osm::CoordinateScale;
    const int capacity = MaxPages * source->tilesPerRow * source->tilesPerRow;
    int zoom = std::clamp(Imagery::chooseZoom(d, 0.5 * (minLat + maxLat), view.metresPerPixel), d.minZoom, d.maxZoom);
    struct Visible { uint64_t key; float distance; };
    std::vector<Visible> visible;
    const float margin = 2.0f;
    for (;; --zoom) {
        visible.clear();
        const QPointF a = Imagery::webMercatorPixel({maxLat, minLon}, zoom, d.tilePixels);
        const QPointF b = Imagery::webMercatorPixel({minLat, maxLon}, zoom, d.tilePixels);
        const int last = (1 << zoom) - 1;
        const int c0 = std::clamp(int(std::floor(a.x() / d.tilePixels)), 0, last);
        const int c1 = std::clamp(int(std::floor(b.x() / d.tilePixels)), 0, last);
        const int r0 = std::clamp(int(std::floor(a.y() / d.tilePixels)), 0, last);
        const int r1 = std::clamp(int(std::floor(b.y() / d.tilePixels)), 0, last);
        // Far more tiles than fit (the view's box when turned): a coarser zoom.
        if (qint64(c1 - c0 + 1) * (r1 - r0 + 1) > qint64(capacity) * 4 && zoom > d.minZoom)
            continue;
        for (int r = r0; r <= r1; ++r)
                for (int c = c0; c <= c1; ++c) {
                    const uint64_t k = key(zoom, c, r);
                    const Mesh &mesh = meshFor(k);
                    const float ox = float(mesh.tileX - view.tileX) * 2048.0f;
                    const float oz = float(mesh.tileZ - view.tileZ) * 2048.0f;
                    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
                    for (size_t p = 0; p < mesh.xz.size(); p += 2) {
                        float sx, sy;
                        view.screenAt(mesh.xz[p] + ox, mesh.xz[p + 1] + oz, sx, sy);
                        x0 = std::min(x0, sx);
                        x1 = std::max(x1, sx);
                        y0 = std::min(y0, sy);
                        y1 = std::max(y1, sy);
                    }
                    if (x1 < -margin || y1 < -margin || x0 > view.width + margin || y0 > view.height + margin)
                        continue;
                    const float dx = 0.5f * (x0 + x1 - view.width), dy = 0.5f * (y0 + y1 - view.height);
                    visible.push_back({k, dx * dx + dy * dy});
                }
        // Half the pages at most: the rest keep the tiles of the last views.
        if (int(visible.size()) * 2 <= capacity || zoom <= d.minZoom)
            break;
    }
    std::sort(visible.begin(), visible.end(), [](const Visible &a, const Visible &b) { return a.distance < b.distance; });
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const Visible &v : visible) {
        const uint64_t k = v.key;
        if (slotOf.contains(k)) {
            draws.push_back({k, k});
            continue;
        }
        const auto failed = failedAt.constFind(k);
        if (failed == failedAt.constEnd() || now - *failed > RetryFailedMs)
            missing.push_back(k);
        const int c = columnOf(k), r = rowOf(k);
        // Zooming out: the children, when all four are loaded.
        if (zoom < d.maxZoom) {
            const uint64_t children[4] = {key(zoom + 1, 2 * c, 2 * r), key(zoom + 1, 2 * c + 1, 2 * r),
                                          key(zoom + 1, 2 * c, 2 * r + 1), key(zoom + 1, 2 * c + 1, 2 * r + 1)};
            if (std::all_of(children, children + 4, [&](uint64_t ck) { return slotOf.contains(ck); })) {
                for (uint64_t ck : children)
                    draws.push_back({ck, ck});
                continue;
            }
        }
        // Else the nearest loaded ancestor's part.
        for (int up = 1; up <= MaxFallbackLevels && zoom - up >= d.minZoom; ++up) {
            const uint64_t ak = key(zoom - up, c >> up, r >> up);
            if (slotOf.contains(ak)) {
                draws.push_back({k, ak});
                break;
            }
        }
    }
    return true;
}

void ImageryMapLayer::build(const MapView &view, const std::vector<Draw> &draws) {
    builtTile[0] = view.tileX;
    builtTile[1] = view.tileZ;
    const int perPage = source->tilesPerRow * source->tilesPerRow;
    const float size = float(source->dataset.tilePixels);
    std::vector<std::vector<float>> vertices(pages.size());
    for (const Draw &draw : draws) {
        const Mesh &mesh = meshFor(draw.tile);
        const int s = slotOf.value(draw.image);
        const int i = s % perPage;
        std::vector<float> &out = vertices[size_t(s / perPage)];
        // The tile's part of the image (all of it, or an ancestor's share), inset by
        // half a texel so neighbours in the page do not bleed in.
        const int up = zoomOf(draw.tile) - zoomOf(draw.image);
        const double share = std::ldexp(1.0, -up);
        const double fu = (columnOf(draw.tile) - (double(columnOf(draw.image)) * (1 << up))) * share;
        const double fv = (rowOf(draw.tile) - (double(rowOf(draw.image)) * (1 << up))) * share;
        const float px = float((i % source->tilesPerRow) * size), py = float((i / source->tilesPerRow) * size);
        auto u = [&](double t) { return float((px + 0.5 + (fu + t * share) * (size - 1.0)) / PageSize); };
        auto v = [&](double t) { return float((py + 0.5 + (fv + t * share) * (size - 1.0)) / PageSize); };
        const float ox = float(mesh.tileX - builtTile[0]) * 2048.0f, oz = float(mesh.tileZ - builtTile[1]) * 2048.0f;
        const int n = mesh.cells, row = n + 1;
        const float *p = mesh.xz.data();
        // Wound as the map's ribbons (front faces have a negative cross in x, z).
        const float *nw = p, *ne = p + 2 * n, *se = p + 2 * (n * row + n);
        const float cross = (ne[0] - nw[0]) * (se[1] - nw[1]) - (ne[1] - nw[1]) * (se[0] - nw[0]);
        static const int orders[2][6] = {{0, 2, 1, 0, 3, 2}, {0, 1, 2, 0, 2, 3}};
        const int *order = orders[cross > 0 ? 0 : 1];
        for (int j = 0; j < n; ++j)
            for (int k = 0; k < n; ++k) {
                const int corner[4][2] = {{k, j}, {k + 1, j}, {k + 1, j + 1}, {k, j + 1}};
                for (int o = 0; o < 6; ++o) {
                    const int ci = corner[order[o]][0], cj = corner[order[o]][1];
                    const float *g = p + 2 * (cj * row + ci);
                    out.insert(out.end(), {g[0] + ox, Height, g[1] + oz, u(double(ci) / n), v(double(cj) / n), 0.0f});
                }
            }
    }
    objects.resize(pages.size());
    for (size_t pg = 0; pg < pages.size(); ++pg) {
        if (!objects[pg])
            objects[pg] = std::make_unique<OglObj>();
        objects[pg]->setMaterialTextureId(pages[pg]->textureId);
        objects[pg]->init(vertices[pg].data(), int(vertices[pg].size()), RenderItem::VT, GL_TRIANGLES);
    }
}

void ImageryMapLayer::pushRenderItems(RenderQueue &queue, const MapView &view) {
    ++frame;
    if (!source) {
        QString error;
        if (!prepare(error))
            return;
    }
    QElapsedTimer timer;
    timer.start();
    takeDeliveries();
    std::vector<Draw> draws;
    std::vector<uint64_t> missing;
    if (!collect(view, draws, missing))
        return;
    if (missing != asked) {
        fetcher->want(missing);
        asked = missing;
    }
    for (const Draw &d : draws)
        tileSlots[size_t(slotOf.value(d.image))].lastUsed = frame;
    if (draws != drawn) {
        build(view, draws);
        drawn = draws;
        if (trace())
            qInfo().noquote() << "map-trace imagery zoom" << (draws.empty() ? -1 : zoomOf(draws.front().tile))
                              << "drawn" << draws.size() << "missing" << missing.size() << "loaded" << slotOf.size()
                              << "ms" << timer.nsecsElapsed() / 1e6;
    }
    if (objects.empty())
        return;
    float *transform = queue.transform();
    Mat4::identity(transform);
    Mat4::translate(transform, transform, float(builtTile[0] - view.tileX) * 2048.0f, 0.0f,
                    float(builtTile[1] - view.tileZ) * 2048.0f);
    for (auto &object : objects)
        object->pushRenderItem(queue);
    Mat4::identity(transform);
}
