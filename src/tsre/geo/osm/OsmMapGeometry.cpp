/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/geo/osm/OsmMapGeometry.h>
#include <tsre/geo/osm/OsmMultipolygon.h>
#include <tsre/geo/osm/OsmOverview.h>
#include <earcut/earcut.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <thread>
#include <tuple>

namespace Osm {

namespace {

using Clock = std::chrono::steady_clock;
double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// Slots as the tile map orders them (MapDataOSM): bridges on top, then by the class's layer.
int slotOf(const Classification &c) { return c.bridge ? 9 : 9 - std::min<int>(c.layer, 9); }

bool hasStrokes(const Style &s) { return s.hasOutline || !s.casings.empty() || s.hasLine; }

// The larger side of the bounding box of x, z points.
float extent(const float *xz, size_t count) {
    float x0 = xz[0], x1 = xz[0], z0 = xz[1], z1 = xz[1];
    for (size_t i = 1; i < count; ++i) {
        x0 = std::min(x0, xz[2 * i]); x1 = std::max(x1, xz[2 * i]);
        z0 = std::min(z0, xz[2 * i + 1]); z1 = std::max(z1, xz[2 * i + 1]);
    }
    return std::max(x1 - x0, z1 - z0);
}

// The orders of a slot's parts.
constexpr float FillOrder = 0, OutlineOrder = 1, CasingOrder = 2, CasingStep = 0.2f, LineOrder = 3;

}

MapGeometry::MapGeometry(const FeatureClasses &classes) : classes_(classes), scaleRanges_(classes.scaleRanges()) {}

void MapGeometry::appendLines(std::vector<float> &out, const float *xz, size_t count, float y) {
    for (size_t i = 0; i + 1 < count; ++i) {
        const float *a = xz + 2 * i, *b = a + 2;
        out.insert(out.end(), {a[0], y, a[1], b[0], y, b[1]});
    }
}

void MapGeometry::appendStrip(std::vector<float> &out, const float *xz, size_t count, bool closed, float width, float y) {
    // Distinct points; a closed polyline drops its repeated closing point and wraps around.
    std::vector<std::array<float, 2>> p;
    p.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const std::array<float, 2> q{xz[2 * i], xz[2 * i + 1]};
        if (p.empty() || std::fabs(q[0] - p.back()[0]) + std::fabs(q[1] - p.back()[1]) > 1e-4f) p.push_back(q);
    }
    if (closed && p.size() > 1 && std::fabs(p.front()[0] - p.back()[0]) + std::fabs(p.front()[1] - p.back()[1]) <= 1e-4f) p.pop_back();
    const size_t n = p.size();
    if (n < 2) return;
    closed = closed && n > 2;
    const float half = 0.5f * width;
    // Unit sideways vector of the segment from point i to the next: (-dz, dx).
    auto normal = [&](size_t i, float &nx, float &nz) {
        const std::array<float, 2> &a = p[i], &b = p[(i + 1) % n];
        const float dx = b[0] - a[0], dz = b[1] - a[1];
        const float length = std::sqrt(dx * dx + dz * dz);
        nx = -dz / length;
        nz = dx / length;
    };
    bool first = true;
    auto pair = [&](const std::array<float, 2> &q, float ox, float oz) {
        const float left[3] = {q[0] - ox, y, q[1] - oz}, right[3] = {q[0] + ox, y, q[1] + oz};
        if (first) {
            // Continue the batch's strip: repeat its last vertex and this strip's first, keeping
            // an even count so this strip's triangles keep their winding.
            if (!out.empty()) {
                const float previous[3] = {out[out.size() - 3], out[out.size() - 2], out[out.size() - 1]};
                out.insert(out.end(), previous, previous + 3);
                out.insert(out.end(), left, left + 3);
            }
            first = false;
        }
        out.insert(out.end(), left, left + 3);
        out.insert(out.end(), right, right + 3);
    };
    const size_t last = closed ? n : n - 1;  // closed: the first point again at the end
    for (size_t k = 0; k <= last; ++k) {
        const size_t i = k % n;
        const bool hasIn = closed || k > 0, hasOut = closed || k < n - 1;
        float inX = 0, inZ = 0, outX = 0, outZ = 0;
        if (hasIn) normal((i + n - 1) % n, inX, inZ);
        if (hasOut) normal(i, outX, outZ);
        if (!hasIn) { pair(p[i], outX * half, outZ * half); continue; }
        if (!hasOut) { pair(p[i], inX * half, inZ * half); continue; }
        float mx = inX + outX, mz = inZ + outZ;
        const float ml = std::sqrt(mx * mx + mz * mz);
        const float cosHalf = ml > 1e-6f ? (mx * inX + mz * inZ) / ml : 0.0f;
        if (cosHalf * MiterLimit < 1.0f) {
            // Too sharp to mitre: end the incoming segment square and start the outgoing one.
            pair(p[i], inX * half, inZ * half);
            pair(p[i], outX * half, outZ * half);
            continue;
        }
        const float scale = half / (cosHalf * ml);
        pair(p[i], mx * scale, mz * scale);
    }
}

// Per thread, owned by the caller: no thread_local objects, whose destructors at thread exit
// are fragile with MinGW (the Windows build).
struct MapGeometry::FillScratch {
    mapbox::detail::Earcut<uint32_t> earcut;
    std::vector<const std::array<float, 2> *> points;
};

size_t MapGeometry::appendFill(std::vector<float> &out, const std::vector<MapRing> &rings, float y, FillScratch *scratch) {
    if (rings.empty() || rings.front().size() < 3) return 0;
    FillScratch local;
    FillScratch &sc = scratch ? *scratch : local;
    auto &earcut = sc.earcut;
    earcut(rings);
    // Indices run over the rings in order.
    auto &points = sc.points;
    points.clear();
    for (const MapRing &r : rings) for (const auto &q : r) points.push_back(&q);
    const std::vector<uint32_t> &idx = earcut.indices;
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        const std::array<float, 2> *a = points[idx[t]], *b = points[idx[t + 1]], *c = points[idx[t + 2]];
        // The map's front faces have a negative cross product in x, z (as its ribbons).
        const float cross = ((*b)[0] - (*a)[0]) * ((*c)[1] - (*a)[1]) - ((*b)[1] - (*a)[1]) * ((*c)[0] - (*a)[0]);
        if (cross > 0) std::swap(b, c);
        out.insert(out.end(), {(*a)[0], y, (*a)[1], (*b)[0], y, (*b)[1], (*c)[0], y, (*c)[1]});
    }
    return idx.size() / 3;
}

MapGeometry::Group &MapGeometry::Output::group(int slot, const Style *style, int64_t chunk) {
    auto [it, added] = groupIndex.try_emplace({slot, style, chunk}, groups.size());
    if (added) {
        groups.emplace_back();
        groups.back().slot = slot;
        groups.back().style = style;
        groups.back().chunk = chunk;
    }
    return groups[it->second];
}

void MapGeometry::Output::addPolyline(Group &g, const float *xz, size_t count, bool closed) {
    g.starts.push_back(uint32_t(g.points.size() / 2));
    g.closed.push_back(closed);
    g.points.insert(g.points.end(), xz, xz + 2 * count);
    ++stats.polylines;
    stats.points += count;
}

MapBatch &MapGeometry::Output::fillBatch(int slot, Rgb color, int64_t chunk) {
    auto [it, added] = fillIndex.try_emplace({slot, color, chunk}, fills.size());
    if (added) {
        fills.emplace_back();
        fills.back().chunk = chunk;
        fills.back().order = slot * OrdersPerSlot + FillOrder;
        fills.back().color = color;
        fills.back().primitive = MapBatch::Triangles;
    }
    return fills[it->second];
}

void MapGeometry::Output::append(Output &&other) {
    for (Group &g : other.groups) {
        Group &to = group(g.slot, g.style, g.chunk);
        const uint32_t offset = uint32_t(to.points.size() / 2);
        for (uint32_t start : g.starts) to.starts.push_back(start + offset);
        to.closed.insert(to.closed.end(), g.closed.begin(), g.closed.end());
        to.points.insert(to.points.end(), g.points.begin(), g.points.end());
    }
    for (MapBatch &b : other.fills) {
        const int slot = int(b.order / OrdersPerSlot);
        std::vector<float> &to = fillBatch(slot, b.color, b.chunk).vertices;
        to.insert(to.end(), b.vertices.begin(), b.vertices.end());
    }
    stats.polygons += other.stats.polygons;
    stats.triangles += other.stats.triangles;
    stats.polylines += other.stats.polylines;
    stats.points += other.stats.points;
    stats.culled += other.stats.culled;
    stats.pointsRead += other.stats.pointsRead;
}

bool MapGeometry::sameStyles(double metersPerPixel) const {
    for (float t : scaleRanges_)
        if ((loadedMetersPerPixel_ <= t) != (metersPerPixel <= t)) return false;
    return true;
}

bool MapGeometry::load(const OsmStore &store, const Box &area, double metersPerPixel, const MapProjection &project,
                       const Options &options, QString &error, const std::atomic_bool *cancel) {
    out_ = Output();
    stats_ = Stats();
    options_ = options;
    loadedMetersPerPixel_ = metersPerPixel;
    auto height = [&](float order) { return options_.baseHeight + order * options_.heightStep; };
    auto cancelled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    const float minExtent = float(MinPixels * metersPerPixel);
    const double tolerance = SimplifyPixels * metersPerPixel;
    auto chunkOf = [&](const float *xz) -> int64_t {
        if (options_.chunkMeters <= 0) return 0;
        const int64_t cx = int64_t(std::floor(xz[0] / options_.chunkMeters)), cz = int64_t(std::floor(xz[1] / options_.chunkMeters));
        return (cx << 32) ^ (cz & 0xffffffff);
    };

    // Reading is sequential and only classifies and copies; the rest runs on all threads.
    struct WayJob { const Style *style; int slot; bool closed; size_t first; uint32_t count; };
    std::vector<WayJob> wayJobs;
    std::vector<Location> wayPoints;
    std::vector<RelationData> relations;
    std::vector<Classification> relationClasses;
    const auto start = Clock::now();
    // Relations first (a few blocks): the drawn multipolygons and their member ways, which
    // the ways pass keeps so assembly reads only the members outside the area.
    Filter relationFilter;
    relationFilter.types = Relations;
    if (!store.forEach(area, relationFilter, [&](const Feature &f) {
            if (f.value("type") != "multipolygon") return;
            const Classification c = classes_.classify(f);
            const Style &s = classes_.style(c);
            if (!c.cls || !s.hasFill || !s.visibleAt(metersPerPixel)) return;
            relations.push_back(RelationData::from(f));
            relationClasses.push_back(c);
        }, error))
        return false;
    std::vector<int64_t> memberIds;
    for (const RelationData &r : relations)
        for (const auto &m : r.members) if (m.type == ItemType::Way) memberIds.push_back(m.ref);
    std::sort(memberIds.begin(), memberIds.end());
    memberIds.erase(std::unique(memberIds.begin(), memberIds.end()), memberIds.end());
    std::unordered_map<int64_t, WayGeometry> members;
    Filter filter;
    filter.types = Ways;
    const bool ok = store.forEach(area, filter, [&](const Feature &f) {
        if (cancelled() || f.refCount < 2) return;
        if (std::binary_search(memberIds.begin(), memberIds.end(), f.id)) {
            WayGeometry &g = members[f.id];
            g.refs.assign(f.refs, f.refs + f.refCount);
            g.locations.assign(f.locations, f.locations + f.refCount);
        }
        // Untagged ways are relation members; their relation draws them.
        if (!f.tagCount()) return;
        const Classification c = classes_.classify(f);
        const Style &s = classes_.style(c);
        if (!s.visibleAt(metersPerPixel)) return;
        const bool closed = f.refCount >= 4 && f.refs[0] == f.refs[f.refCount - 1];
        if (!(s.hasFill && closed) && !hasStrokes(s)) return;
        wayJobs.push_back({&s, slotOf(c), closed, wayPoints.size(), f.refCount});
        wayPoints.insert(wayPoints.end(), f.locations, f.locations + f.refCount);
    }, error);
    stats_.readSeconds = since(start);
    if (!ok) return false;
    if (cancelled()) { error = QStringLiteral("cancelled"); return false; }
    stats_.ways = wayJobs.size();

    auto t = Clock::now();
    std::vector<MultipolygonResult> polygons;
    if (!relations.empty() && !assembleMultipolygons(store, relations, members, polygons, error, area)) return false;
    std::unordered_map<int64_t, WayGeometry>().swap(members);
    stats_.relations = relations.size();
    stats_.assembleSeconds = since(t);
    struct PolygonJob { const Style *style; int slot; const Polygon *polygon; };
    std::vector<PolygonJob> polygonJobs;
    for (size_t r = 0; r < polygons.size(); ++r)
        for (const Polygon &p : polygons[r].polygons)
            polygonJobs.push_back({&classes_.style(relationClasses[r]), slotOf(relationClasses[r]), &p});

    t = Clock::now();
    // Per thread: the points to draw of a way or ring, all or simplified (ends kept).
    struct Scratch { std::vector<Location> simplified; std::vector<float> xz; std::vector<MapRing> rings; FillScratch fill; };
    auto simplify = [&](Output &o, Scratch &sc, const Location *in, uint32_t count, uint32_t &kept) -> const Location * {
        o.stats.pointsRead += count;
        kept = count;
        if (tolerance < SimplifyFromMeters || count <= 2) return in;
        const std::vector<uint32_t> idx = simplifyIndices(in, count, tolerance);
        sc.simplified.clear();
        for (uint32_t i : idx) sc.simplified.push_back(in[i]);
        kept = uint32_t(sc.simplified.size());
        return sc.simplified.data();
    };
    auto doWay = [&](Output &o, Scratch &sc, size_t i) {
        const WayJob &j = wayJobs[i];
        const Style &s = *j.style;
        uint32_t count = 0;
        const Location *points = simplify(o, sc, wayPoints.data() + j.first, j.count, count);
        sc.xz.resize(2 * size_t(count));
        project(points, count, sc.xz.data());
        // Closed ways only: an open way may be one short piece of a long road or river.
        if (j.closed && extent(sc.xz.data(), count) < minExtent) { ++o.stats.culled; return; }
        const int64_t chunk = chunkOf(sc.xz.data());
        if (s.hasFill && j.closed && count >= 4) {
            sc.rings.resize(1);
            MapRing &ring = sc.rings.front();
            ring.resize(count - 1);
            std::copy(sc.xz.begin(), sc.xz.end() - 2, &ring.front()[0]);
            o.stats.triangles += appendFill(o.fillBatch(j.slot, s.fill, chunk).vertices, sc.rings, height(j.slot * OrdersPerSlot + FillOrder), &sc.fill);
            ++o.stats.polygons;
        }
        if (hasStrokes(s)) o.addPolyline(o.group(j.slot, &s, chunk), sc.xz.data(), count, j.closed);
    };
    auto doPolygon = [&](Output &o, Scratch &sc, size_t i) {
        const PolygonJob &j = polygonJobs[i];
        const Style &s = *j.style;
        // Projected with the repeated closing point, which the strokes keep and the fill
        // drops; false when simplified below a triangle.
        auto ring = [&](const std::vector<Location> &locations, MapRing &out) {
            uint32_t count = 0;
            const Location *points = simplify(o, sc, locations.data(), uint32_t(locations.size()), count);
            if (count < 4) return false;
            out.resize(count);
            project(points, count, &out.front()[0]);
            return true;
        };
        sc.rings.resize(1);
        if (!ring(j.polygon->outer, sc.rings[0]) || extent(&sc.rings[0].front()[0], sc.rings[0].size()) < minExtent) { ++o.stats.culled; return; }
        const int64_t chunk = chunkOf(&sc.rings[0].front()[0]);
        for (const auto &inner : j.polygon->inners) {
            sc.rings.emplace_back();
            if (!ring(inner, sc.rings.back())) sc.rings.pop_back();
        }
        for (MapRing &r : sc.rings) {
            if (hasStrokes(s)) o.addPolyline(o.group(j.slot, &s, chunk), &r.front()[0], r.size(), true);
            r.pop_back();
        }
        o.stats.triangles += appendFill(o.fillBatch(j.slot, s.fill, chunk).vertices, sc.rings, height(j.slot * OrdersPerSlot + FillOrder), &sc.fill);
        ++o.stats.polygons;
    };
    std::atomic_bool failed{false};
    // Contiguous shares, merged in order: the same result on any number of threads.
    const size_t threads = size_t(options.threads > 0 ? options.threads : std::max(1u, std::thread::hardware_concurrency()));
    auto parallel = [&](size_t count, const std::function<void(Output &, Scratch &, size_t)> &fn) {
        const size_t n = std::max<size_t>(1, std::min(threads, count / 64));
        std::vector<Output> parts(n);
        std::vector<std::thread> pool;
        for (size_t p = 0; p < n; ++p)
            pool.emplace_back([&, p] {
                // An exception may not leave a thread: out of memory fails the load instead.
                try {
                    Scratch sc;
                    for (size_t i = count * p / n, end = count * (p + 1) / n; i < end && !cancelled() && !failed; ++i) fn(parts[p], sc, i);
                } catch (const std::bad_alloc &) {
                    failed = true;
                }
            });
        for (std::thread &th : pool) th.join();
        for (Output &part : parts) out_.append(std::move(part));
    };
    parallel(wayJobs.size(), doWay);
    std::vector<Location>().swap(wayPoints);
    parallel(polygonJobs.size(), doPolygon);
    stats_.processSeconds = since(t);
    if (failed) { error = QStringLiteral("not enough memory for the OSM data of this view"); return false; }
    const size_t ways = stats_.ways, relationCount = stats_.relations;
    const double read = stats_.readSeconds, assemble = stats_.assembleSeconds, process = stats_.processSeconds;
    stats_ = out_.stats;
    stats_.ways = ways;
    stats_.relations = relationCount;
    stats_.readSeconds = read;
    stats_.assembleSeconds = assemble;
    stats_.processSeconds = process;
    if (cancelled()) { error = QStringLiteral("cancelled"); return false; }
    return true;
}

void MapGeometry::strokes(double metersPerPixel, std::vector<MapBatch> &out) const {
    out.clear();
    std::map<std::tuple<float, Rgb, uint8_t, int64_t>, size_t> index;
    auto batch = [&](float order, const Stroke &stroke, int64_t chunk) -> std::pair<MapBatch *, float> {
        const bool strip = stroke.width > 0 && stroke.width >= StripPixels * metersPerPixel;
        const MapBatch::Primitive primitive = strip ? MapBatch::TriangleStrip : MapBatch::Lines;
        auto [it, added] = index.try_emplace({order, stroke.color, uint8_t(primitive), chunk}, out.size());
        if (added) {
            out.emplace_back();
            out.back().chunk = chunk;
            out.back().order = order;
            out.back().color = stroke.color;
            out.back().primitive = primitive;
        }
        return {&out[it->second], strip ? stroke.width : 0.0f};
    };
    auto draw = [&](const Group &g, float order, const Stroke &stroke) {
        auto [b, width] = batch(order, stroke, g.chunk);
        const float y = options_.baseHeight + order * options_.heightStep;
        for (size_t i = 0; i < g.starts.size(); ++i) {
            const size_t first = g.starts[i];
            const size_t end = i + 1 < g.starts.size() ? g.starts[i + 1] : g.points.size() / 2;
            const float *xz = g.points.data() + 2 * first;
            if (width > 0) appendStrip(b->vertices, xz, end - first, g.closed[i], width, y);
            else appendLines(b->vertices, xz, end - first, y);
        }
    };
    for (const Group &g : out_.groups) {
        const Style &s = *g.style;
        const float base = g.slot * OrdersPerSlot;
        if (s.hasOutline) draw(g, base + OutlineOrder, s.outline);
        for (size_t c = 0; c < s.casings.size(); ++c) draw(g, base + CasingOrder + c * CasingStep, s.casings[c]);
        if (s.hasLine) draw(g, base + LineOrder, s.line);
    }
}

}
