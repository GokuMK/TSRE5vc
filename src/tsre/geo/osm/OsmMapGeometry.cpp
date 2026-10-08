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

size_t MapGeometry::appendFill(std::vector<float> &out, const std::vector<MapRing> &rings, float y) {
    if (rings.empty() || rings.front().size() < 3) return 0;
    static thread_local mapbox::detail::Earcut<uint32_t> earcut;
    earcut(rings);
    // Indices run over the rings in order.
    static thread_local std::vector<const std::array<float, 2> *> points;
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

MapGeometry::Group &MapGeometry::group(int slot, const Style *style) {
    auto [it, added] = groupIndex_.try_emplace({slot, style}, groups_.size());
    if (added) {
        groups_.emplace_back();
        groups_.back().slot = slot;
        groups_.back().style = style;
    }
    return groups_[it->second];
}

void MapGeometry::addPolyline(Group &g, const float *xz, size_t count, bool closed) {
    g.starts.push_back(uint32_t(g.points.size() / 2));
    g.closed.push_back(closed);
    g.points.insert(g.points.end(), xz, xz + 2 * count);
    ++stats_.polylines;
    stats_.points += count;
}

MapBatch &MapGeometry::fillBatch(int slot, Rgb color) {
    auto [it, added] = fillIndex_.try_emplace((uint64_t(slot) << 32) | color, fills_.size());
    if (added) {
        fills_.emplace_back();
        fills_.back().order = slot * OrdersPerSlot + FillOrder;
        fills_.back().color = color;
        fills_.back().primitive = MapBatch::Triangles;
    }
    return fills_[it->second];
}

bool MapGeometry::sameStyles(double metersPerPixel) const {
    for (float t : scaleRanges_)
        if ((loadedMetersPerPixel_ <= t) != (metersPerPixel <= t)) return false;
    return true;
}

bool MapGeometry::load(const OsmStore &store, const Box &area, double metersPerPixel, const MapProjection &project,
                       const Options &options, QString &error, const std::atomic_bool *cancel) {
    groups_.clear();
    groupIndex_.clear();
    fills_.clear();
    fillIndex_.clear();
    stats_ = Stats();
    options_ = options;
    loadedMetersPerPixel_ = metersPerPixel;
    auto height = [&](float order) { return options_.baseHeight + order * options_.heightStep; };
    auto cancelled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    const float minExtent = float(MinPixels * metersPerPixel);
    const double tolerance = SimplifyPixels * metersPerPixel;
    std::vector<Location> simplified;
    // The points to draw of a way or ring: all, or the simplified ones (end points kept).
    auto simplify = [&](const Location *in, uint32_t count, uint32_t &out) -> const Location * {
        stats_.pointsRead += count;
        out = count;
        if (tolerance < SimplifyFromMeters || count <= 2) return in;
        const std::vector<uint32_t> kept = simplifyIndices(in, count, tolerance);
        simplified.clear();
        for (uint32_t i : kept) simplified.push_back(in[i]);
        out = uint32_t(simplified.size());
        return simplified.data();
    };

    std::vector<RelationData> relations;
    std::vector<Classification> relationClasses;
    std::vector<float> xz;
    std::vector<MapRing> rings(1);
    double triangulate = 0;
    Filter filter;
    filter.types = Ways | Relations;
    const auto start = Clock::now();
    const bool ok = store.forEach(area, filter, [&](const Feature &f) {
        if (cancelled()) return;
        if (f.type == ItemType::Relation) {
            if (f.value("type") != "multipolygon") return;
            const Classification c = classes_.classify(f);
            const Style &s = classes_.style(c);
            if (!c.cls || !s.hasFill || !s.visibleAt(metersPerPixel)) return;
            relations.push_back(RelationData::from(f));
            relationClasses.push_back(c);
            return;
        }
        // Untagged ways are relation members; their relation draws them.
        if (!f.tagCount() || f.refCount < 2) return;
        const Classification c = classes_.classify(f);
        const Style &s = classes_.style(c);
        if (!s.visibleAt(metersPerPixel)) return;
        const bool closed = f.refCount >= 4 && f.refs[0] == f.refs[f.refCount - 1];
        if (!(s.hasFill && closed) && !hasStrokes(s)) return;
        ++stats_.ways;
        uint32_t count = 0;
        const Location *points = simplify(f.locations, f.refCount, count);
        const bool area = s.hasFill && closed && count >= 4;
        xz.resize(2 * size_t(count));
        project(points, count, xz.data());
        // Closed ways only: an open way may be one short piece of a long road or river.
        if (closed && extent(xz.data(), count) < minExtent) { ++stats_.culled; return; }
        const int slot = slotOf(c);
        if (area) {
            MapRing &ring = rings.front();
            ring.resize(count - 1);
            std::copy(xz.begin(), xz.end() - 2, &ring.front()[0]);
            rings.resize(1);
            const auto t = Clock::now();
            stats_.triangles += appendFill(fillBatch(slot, s.fill).vertices, rings, height(slot * OrdersPerSlot + FillOrder));
            triangulate += since(t);
            ++stats_.polygons;
        }
        if (hasStrokes(s)) addPolyline(group(slot, &s), xz.data(), count, closed);
    }, error);
    stats_.readSeconds = since(start) - triangulate;
    if (!ok) return false;
    if (cancelled()) { error = QStringLiteral("cancelled"); return false; }

    auto t = Clock::now();
    std::vector<MultipolygonResult> polygons;
    if (!relations.empty() && !assembleMultipolygons(store, relations, polygons, error)) return false;
    stats_.relations = relations.size();
    stats_.assembleSeconds = since(t);
    for (size_t r = 0; r < polygons.size() && !cancelled(); ++r) {
        const Style &s = classes_.style(relationClasses[r]);
        const int slot = slotOf(relationClasses[r]);
        for (const Polygon &p : polygons[r].polygons) {
            // Projected with the repeated closing point, which the strokes keep and the fill
            // drops; false when simplified below a triangle.
            auto ring = [&](const std::vector<Location> &locations, MapRing &out) {
                uint32_t count = 0;
                const Location *points = simplify(locations.data(), uint32_t(locations.size()), count);
                if (count < 4) return false;
                out.resize(count);
                project(points, count, &out.front()[0]);
                return true;
            };
            rings.resize(1);
            if (!ring(p.outer, rings[0]) || extent(&rings[0].front()[0], rings[0].size()) < minExtent) { ++stats_.culled; continue; }
            for (const auto &inner : p.inners) {
                rings.emplace_back();
                if (!ring(inner, rings.back())) rings.pop_back();
            }
            for (MapRing &r : rings) {
                if (hasStrokes(s)) addPolyline(group(slot, &s), &r.front()[0], r.size(), true);
                r.pop_back();
            }
            const auto tt = Clock::now();
            stats_.triangles += appendFill(fillBatch(slot, s.fill).vertices, rings, height(slot * OrdersPerSlot + FillOrder));
            triangulate += since(tt);
            ++stats_.polygons;
        }
    }
    stats_.triangulateSeconds = triangulate;
    if (cancelled()) { error = QStringLiteral("cancelled"); return false; }
    return true;
}

void MapGeometry::strokes(double metersPerPixel, std::vector<MapBatch> &out) const {
    out.clear();
    std::map<std::tuple<float, Rgb, uint8_t>, size_t> index;
    auto batch = [&](float order, const Stroke &stroke) -> std::pair<MapBatch *, float> {
        const bool strip = stroke.width > 0 && stroke.width >= StripPixels * metersPerPixel;
        const MapBatch::Primitive primitive = strip ? MapBatch::TriangleStrip : MapBatch::Lines;
        auto [it, added] = index.try_emplace({order, stroke.color, uint8_t(primitive)}, out.size());
        if (added) {
            out.emplace_back();
            out.back().order = order;
            out.back().color = stroke.color;
            out.back().primitive = primitive;
        }
        return {&out[it->second], strip ? stroke.width : 0.0f};
    };
    auto draw = [&](const Group &g, float order, const Stroke &stroke) {
        auto [b, width] = batch(order, stroke);
        const float y = options_.baseHeight + order * options_.heightStep;
        for (size_t i = 0; i < g.starts.size(); ++i) {
            const size_t first = g.starts[i];
            const size_t end = i + 1 < g.starts.size() ? g.starts[i + 1] : g.points.size() / 2;
            const float *xz = g.points.data() + 2 * first;
            if (width > 0) appendStrip(b->vertices, xz, end - first, g.closed[i], width, y);
            else appendLines(b->vertices, xz, end - first, y);
        }
    };
    for (const Group &g : groups_) {
        const Style &s = *g.style;
        const float base = g.slot * OrdersPerSlot;
        if (s.hasOutline) draw(g, base + OutlineOrder, s.outline);
        for (size_t c = 0; c < s.casings.size(); ++c) draw(g, base + CasingOrder + c * CasingStep, s.casings[c]);
        if (s.hasLine) draw(g, base + LineOrder, s.line);
    }
}

}
