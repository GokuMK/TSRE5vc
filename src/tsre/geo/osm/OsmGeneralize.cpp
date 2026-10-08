/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/geo/osm/OsmGeneralize.h>
#include <tsre/geo/osm/OsmOverview.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Osm {

namespace {

// Cells: x east, y north. Directions of boundary edges: east, north, west, south.
constexpr int Dx[4] = {1, 0, -1, 0}, Dy[4] = {0, 1, 0, -1};

struct Grid {
    int w = 0, h = 0;
    std::vector<uint8_t> cells;
    uint8_t at(int x, int y) const { return x < 0 || y < 0 || x >= w || y >= h ? 0 : cells[size_t(y) * size_t(w) + size_t(x)]; }
    uint8_t &ref(int x, int y) { return cells[size_t(y) * size_t(w) + size_t(x)]; }
};

// Closing with a square of side 2r+1: dilate, then erode (a cell stays only when the
// whole square around it is filled). Separable, with running counts.
void close(Grid &g, int r) {
    if (r <= 0) return;
    std::vector<uint8_t> tmp(g.cells.size());
    auto pass = [&](const std::vector<uint8_t> &in, std::vector<uint8_t> &out, bool horizontal, bool dilate) {
        const int lines = horizontal ? g.h : g.w, length = horizontal ? g.w : g.h;
        const size_t step = horizontal ? 1 : size_t(g.w), lineStep = horizontal ? size_t(g.w) : 1;
        for (int l = 0; l < lines; ++l) {
            const size_t base = size_t(l) * lineStep;
            int count = 0;
            for (int k = 0; k < std::min(r, length); ++k) count += in[base + size_t(k) * step];
            for (int k = 0; k < length; ++k) {
                if (k + r < length) count += in[base + size_t(k + r) * step];
                if (k - r - 1 >= 0) count -= in[base + size_t(k - r - 1) * step];
                out[base + size_t(k) * step] = dilate ? count > 0 : count == 2 * r + 1;
            }
        }
    };
    pass(g.cells, tmp, true, true);
    pass(tmp, g.cells, false, true);
    pass(g.cells, tmp, true, false);
    pass(tmp, g.cells, false, false);
}

struct Ring {
    std::vector<std::pair<int, int>> corners;  // grid corners where the direction changes
    int64_t area2 = 0;                         // twice the signed area, cells
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    int insideX = 0, insideY = 0;              // a filled cell on its inner side
};

// Even-odd point in ring, the point at a cell centre (x + 0.5, y + 0.5).
bool contains(const Ring &r, int x, int y) {
    const double px = x + 0.5, py = y + 0.5;
    bool in = false;
    const size_t n = r.corners.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = r.corners[i].first, yi = r.corners[i].second, xj = r.corners[j].first, yj = r.corners[j].second;
        if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

}

std::vector<GeneralizedArea> generalizeAreas(const std::vector<std::vector<std::vector<Location>>> &polygons,
                                             const GeneralizeOptions &options, GeneralizeStats *stats) {
    GeneralizeStats st;
    std::vector<GeneralizedArea> out;
    Box bounds;
    for (const auto &p : polygons) for (const auto &ring : p) for (const Location &l : ring) bounds.extend(l);
    st.polygonsIn = polygons.size();
    if (!bounds.valid()) { if (stats) *stats = st; return out; }

    // A grid of about cellMeters, larger when the area would need too many cells.
    const double midLat = 0.5 * (bounds.minY + bounds.maxY) / CoordinateScale;
    const double kx = 111320.0 * std::cos(midLat * M_PI / 180), ky = 110574.0;
    const double widthM = (bounds.maxX - bounds.minX) / CoordinateScale * kx, heightM = (bounds.maxY - bounds.minY) / CoordinateScale * ky;
    double cell = std::max(options.cellMeters, 1.0);
    while ((widthM / cell + 8) * (heightM / cell + 8) > 6e8) cell *= 1.5;
    const int r = options.closeMeters > 0 ? std::max(1, int(std::lround(options.closeMeters / cell))) : 0;
    const int margin = r + 2;
    const double dLon = cell / kx, dLat = cell / ky;
    const double lon0 = bounds.minX / CoordinateScale - margin * dLon, lat0 = bounds.minY / CoordinateScale - margin * dLat;
    Grid g;
    g.w = int(std::ceil(widthM / cell)) + 2 * margin + 1;
    g.h = int(std::ceil(heightM / cell)) + 2 * margin + 1;
    g.cells.assign(size_t(g.w) * size_t(g.h), 0);
    st.cells = g.cells.size();
    const double cellKm2 = cell * cell / 1e6;

    // Fill: cells whose centre is inside, even-odd over a polygon's rings.
    std::vector<std::pair<int, double>> crossings;
    for (const auto &p : polygons) {
        crossings.clear();
        for (const auto &ring : p) {
            if (ring.size() < 4) continue;
            double a = 0;
            for (size_t i = 0; i + 1 < ring.size(); ++i) {
                const double x0 = (ring[i].lon() - lon0) / dLon, y0 = (ring[i].lat() - lat0) / dLat;
                const double x1 = (ring[i + 1].lon() - lon0) / dLon, y1 = (ring[i + 1].lat() - lat0) / dLat;
                a += x0 * y1 - x1 * y0;
                if (y0 == y1) continue;
                const double ya = std::min(y0, y1), yb = std::max(y0, y1);
                // Rows whose centre j + 0.5 lies in [ya, yb).
                for (int j = int(std::ceil(ya - 0.5)); j + 0.5 < yb; ++j)
                    crossings.push_back({j, x0 + (j + 0.5 - y0) * (x1 - x0) / (y1 - y0)});
            }
            st.areaInKm2 += (&ring == &p.front() ? 0.5 : -0.5) * std::fabs(a) * cellKm2;
        }
        std::sort(crossings.begin(), crossings.end());
        for (size_t k = 0; k + 1 < crossings.size(); k += 2) {
            if (crossings[k].first != crossings[k + 1].first) { --k; continue; }  // unpaired: skip one
            const int j = crossings[k].first;
            if (j < 0 || j >= g.h) continue;
            const int from = std::max(0, int(std::ceil(crossings[k].second - 0.5)));
            const int to = std::min(g.w, int(std::ceil(crossings[k + 1].second - 0.5)));
            for (int i = from; i < to; ++i) g.ref(i, j) = 1;
        }
    }
    close(g, r);
    for (uint8_t c : g.cells) st.filledCells += c;

    // Trace the outlines: boundary edges keep filled cells on their left, so outer rings
    // run counter-clockwise and holes clockwise. Diagonal cells are not joined.
    std::vector<uint8_t> visited(g.cells.size(), 0);
    std::vector<Ring> outers, holes;
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x) {
            if (!g.at(x, y)) continue;
            for (int d0 = 0; d0 < 4; ++d0) {
                // The side of cell (x, y) an edge of direction d runs along: east the bottom,
                // north the right, west the top, south the left; the cell across it is empty.
                const int ox = d0 == 1 ? 1 : d0 == 3 ? -1 : 0, oy = d0 == 0 ? -1 : d0 == 2 ? 1 : 0;
                if (g.at(x + ox, y + oy) || (visited[size_t(y) * size_t(g.w) + size_t(x)] & (1 << d0))) continue;
                Ring ring;
                ring.insideX = x;
                ring.insideY = y;
                int cx = x, cy = y, d = d0;
                // Start corner of the edge.
                int vx = cx + (d == 1 || d == 2 ? 1 : 0), vy = cy + (d >= 2 ? 1 : 0);
                ring.minX = ring.maxX = vx;
                ring.minY = ring.maxY = vy;
                int lastD = -1;
                for (;;) {
                    visited[size_t(cy) * size_t(g.w) + size_t(cx)] |= uint8_t(1 << d);
                    if (d != lastD) ring.corners.push_back({vx, vy});
                    lastD = d;
                    const int ex = vx + Dx[d], ey = vy + Dy[d];
                    ring.area2 += int64_t(vx) * ey - int64_t(ex) * vy;
                    vx = ex;
                    vy = ey;
                    ring.minX = std::min(ring.minX, vx); ring.maxX = std::max(ring.maxX, vx);
                    ring.minY = std::min(ring.minY, vy); ring.maxY = std::max(ring.maxY, vy);
                    // The two cells ahead of corner (vx, vy), left and right of direction d.
                    static constexpr int AL[4][2] = {{0, 0}, {-1, 0}, {-1, -1}, {0, -1}};
                    static constexpr int AR[4][2] = {{0, -1}, {0, 0}, {-1, 0}, {-1, -1}};
                    const int lx = vx + AL[d][0], ly = vy + AL[d][1], rx = vx + AR[d][0], ry = vy + AR[d][1];
                    if (!g.at(lx, ly)) {
                        d = (d + 1) % 4;             // turn left around the same cell
                    } else if (g.at(rx, ry)) {
                        cx = rx; cy = ry; d = (d + 3) % 4;  // turn right onto the cell ahead right
                    } else {
                        cx = lx; cy = ly;            // straight on along the cell ahead left
                    }
                    if (cx == x && cy == y && d == d0) break;
                }
                // The first corner was recorded before knowing the closing direction.
                if (lastD == d0 && ring.corners.size() > 1) ring.corners.erase(ring.corners.begin());
                (ring.area2 > 0 ? outers : holes).push_back(std::move(ring));
            }
        }

    // Holes go to the smallest outer around a filled cell next to them.
    const int buckets = 256;
    const double bx = double(buckets) / g.w, by = double(buckets) / g.h;
    std::vector<std::vector<uint32_t>> index(size_t(buckets) * buckets);
    for (uint32_t i = 0; i < outers.size(); ++i) {
        const Ring &o = outers[i];
        for (int y = int(o.minY * by); y <= std::min(buckets - 1, int(o.maxY * by)); ++y)
            for (int x = int(o.minX * bx); x <= std::min(buckets - 1, int(o.maxX * bx)); ++x)
                index[size_t(y) * buckets + size_t(x)].push_back(i);
    }
    std::vector<std::vector<uint32_t>> holesOf(outers.size());
    for (uint32_t h = 0; h < holes.size(); ++h) {
        const Ring &hole = holes[h];
        if (-hole.area2 * 0.5 * cellKm2 < options.minHoleKm2) continue;
        const int px = hole.insideX, py = hole.insideY;
        int64_t best = -1;
        uint32_t owner = 0;
        for (uint32_t i : index[size_t(std::min(buckets - 1, int(py * by))) * buckets + size_t(std::min(buckets - 1, int(px * bx)))]) {
            const Ring &o = outers[i];
            if (px < o.minX || px >= o.maxX || py < o.minY || py >= o.maxY || (best >= 0 && o.area2 >= best) || !contains(o, px, py)) continue;
            best = o.area2;
            owner = i;
        }
        if (best >= 0) holesOf[owner].push_back(h);
    }

    // Back to latitude and longitude, simplified.
    auto toLocations = [&](const Ring &ring) {
        std::vector<Location> pts;
        pts.reserve(ring.corners.size() + 1);
        for (const auto &c : ring.corners) pts.push_back(Location::fromDegrees(lon0 + c.first * dLon, lat0 + c.second * dLat));
        pts.push_back(pts.front());
        std::vector<Location> kept;
        for (uint32_t i : simplifyIndices(pts.data(), uint32_t(pts.size()), std::max(options.toleranceMeters, 0.75 * cell))) kept.push_back(pts[i]);
        return kept;
    };
    for (uint32_t i = 0; i < outers.size(); ++i) {
        double km2 = outers[i].area2 * 0.5 * cellKm2;
        for (uint32_t h : holesOf[i]) km2 += holes[h].area2 * 0.5 * cellKm2;
        if (km2 < options.minAreaKm2) continue;
        GeneralizedArea a;
        a.outer = toLocations(outers[i]);
        if (a.outer.size() < 4) continue;
        for (uint32_t h : holesOf[i]) {
            std::vector<Location> ring = toLocations(holes[h]);
            if (ring.size() >= 4) a.holes.push_back(std::move(ring));
        }
        st.areaOutKm2 += km2;
        st.pointsOut += a.outer.size();
        for (const auto &hr : a.holes) st.pointsOut += hr.size();
        st.holes += a.holes.size();
        out.push_back(std::move(a));
    }
    st.areas = out.size();
    if (stats) *stats = st;
    return out;
}

}
