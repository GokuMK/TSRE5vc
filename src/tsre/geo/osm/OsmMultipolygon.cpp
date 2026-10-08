/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/geo/osm/OsmMultipolygon.h>
#include <algorithm>
#include <unordered_map>

namespace Osm {

RelationData RelationData::from(const Feature &r) {
    RelationData d;
    d.id = r.id;
    d.extent = r.extent;
    for (uint32_t i = 0; i < r.tagCount(); ++i) { const Tag t = r.tag(i); d.tags.emplace_back(std::string(t.key), std::string(t.value)); }
    for (uint32_t i = 0; i < r.memberCount; ++i) d.members.push_back({r.members[i].ref, r.members[i].type, std::string(r.role(r.members[i]))});
    return d;
}

std::string_view RelationData::value(std::string_view key) const {
    for (const auto &t : tags) if (t.first == key) return t.second;
    return {};
}

double signedArea(const std::vector<Location> &ring) {
    double a = 0;
    for (size_t i = 0; i + 1 < ring.size(); ++i)
        a += double(ring[i].x) * ring[i + 1].y - double(ring[i + 1].x) * ring[i].y;
    return a / 2;
}

bool pointInRing(Location p, const std::vector<Location> &ring) {
    bool inside = false;
    for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const Location &a = ring[i], &b = ring[j];
        if ((a.y > p.y) != (b.y > p.y)) {
            const double x = a.x + (double(p.y) - a.y) * (double(b.x) - a.x) / (double(b.y) - a.y);
            if (p.x < x) inside = !inside;
        }
    }
    return inside;
}

namespace {

struct Chain { std::vector<int64_t> refs; std::vector<Location> points; };

// Joins open chains end to end by node id; closed results become rings.
void joinRings(std::vector<Chain> chains, std::vector<std::vector<Location>> &rings, uint32_t &broken) {
    std::vector<bool> used(chains.size(), false);
    std::unordered_multimap<int64_t, size_t> ends;
    for (size_t i = 0; i < chains.size(); ++i) {
        const Chain &c = chains[i];
        if (c.refs.front() == c.refs.back()) continue;
        ends.emplace(c.refs.front(), i);
        ends.emplace(c.refs.back(), i);
    }
    for (size_t i = 0; i < chains.size(); ++i) {
        if (used[i]) continue;
        used[i] = true;
        Chain ring = chains[i];
        while (ring.refs.front() != ring.refs.back()) {
            const int64_t end = ring.refs.back();
            size_t next = size_t(-1);
            auto range = ends.equal_range(end);
            for (auto it = range.first; it != range.second; ++it) if (!used[it->second]) { next = it->second; break; }
            if (next == size_t(-1)) break;
            used[next] = true;
            Chain c = chains[next];
            if (c.refs.front() != end) { std::reverse(c.refs.begin(), c.refs.end()); std::reverse(c.points.begin(), c.points.end()); }
            ring.refs.insert(ring.refs.end(), c.refs.begin() + 1, c.refs.end());
            ring.points.insert(ring.points.end(), c.points.begin() + 1, c.points.end());
        }
        if (ring.refs.front() != ring.refs.back() || ring.points.size() < 4) { ++broken; continue; }
        if (signedArea(ring.points) == 0) { ++broken; continue; }
        rings.push_back(std::move(ring.points));
    }
}

void orient(std::vector<Location> &ring, bool counterClockwise) {
    if ((signedArea(ring) > 0) != counterClockwise) std::reverse(ring.begin(), ring.end());
}

}

MultipolygonResult assembleMultipolygon(const RelationData &relation, const std::function<const WayGeometry *(int64_t)> &way) {
    MultipolygonResult result;
    result.id = relation.id;
    std::vector<Chain> outerChains, innerChains;
    for (const auto &m : relation.members) {
        if (m.type != ItemType::Way) continue;
        const bool inner = m.role == "inner";
        if (!inner && m.role != "outer" && !m.role.empty()) continue;  // e.g. label, admin_centre, subarea
        const WayGeometry *w = way(m.ref);
        if (!w || w->refs.size() < 2 || w->locations.size() != w->refs.size()) { ++result.brokenRings; continue; }
        (inner ? innerChains : outerChains).push_back({w->refs, w->locations});
    }
    std::vector<std::vector<Location>> outers, inners;
    joinRings(std::move(outerChains), outers, result.brokenRings);
    joinRings(std::move(innerChains), inners, result.brokenRings);
    // Smaller outers first, so an inner lands in the smallest outer that contains it.
    std::sort(outers.begin(), outers.end(), [](const auto &a, const auto &b) { return std::abs(signedArea(a)) < std::abs(signedArea(b)); });
    for (auto &o : outers) { orient(o, true); result.polygons.push_back({std::move(o), {}}); }
    for (auto &in : inners) {
        orient(in, false);
        Polygon *owner = nullptr;
        for (Polygon &p : result.polygons) {
            // Any vertex not shared with the outer decides; touching rings share some vertices.
            bool decided = false, inside = false;
            for (const Location &v : in) {
                if (std::find(p.outer.begin(), p.outer.end(), v) != p.outer.end()) continue;
                inside = pointInRing(v, p.outer);
                decided = true;
                break;
            }
            if (decided && inside) { owner = &p; break; }
        }
        if (owner) owner->inners.push_back(std::move(in));
        else ++result.orphanInners;
    }
    return result;
}

bool assembleMultipolygons(const OsmStore &store, const std::vector<RelationData> &relations,
                           std::vector<MultipolygonResult> &results, QString &error) {
    return assembleMultipolygons(store, relations, {}, results, error);
}

bool assembleMultipolygons(const OsmStore &store, const std::vector<RelationData> &relations,
                           const std::unordered_map<int64_t, WayGeometry> &known,
                           std::vector<MultipolygonResult> &results, QString &error, const Box &readAlready) {
    results.clear();
    Filter filter;
    filter.types = Ways;
    filter.readAlready = readAlready;
    Box area;
    for (const RelationData &r : relations) {
        bool missing = false;
        for (const auto &m : r.members)
            if (m.type == ItemType::Way && !known.count(m.ref)) { filter.ids.push_back(m.ref); missing = true; }
        if (missing) area.extend(r.extent);
    }
    std::sort(filter.ids.begin(), filter.ids.end());
    filter.ids.erase(std::unique(filter.ids.begin(), filter.ids.end()), filter.ids.end());
    std::unordered_map<int64_t, WayGeometry> ways;
    if (!filter.ids.empty() && area.valid()) {
        if (!store.forEach(area, filter, [&](const Feature &f) {
                WayGeometry &g = ways[f.id];
                g.refs.assign(f.refs, f.refs + f.refCount);
                g.locations.assign(f.locations, f.locations + f.refCount);
            }, error))
            return false;
    }
    auto lookup = [&](int64_t id) -> const WayGeometry * {
        auto it = known.find(id);
        if (it != known.end()) return &it->second;
        auto read = ways.find(id);
        return read == ways.end() ? nullptr : &read->second;
    };
    for (const RelationData &r : relations) results.push_back(assembleMultipolygon(r, lookup));
    return true;
}

}
