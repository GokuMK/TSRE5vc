#include <tsre/geo/osm/OsmGeneralize.h>
#include <tsre/geo/osm/OsmMultipolygon.h>
#include <algorithm>
#include <cmath>
#include <functional>

using namespace Osm;

namespace {

// A local metre grid around (16.7 E, 53.15 N), near Pila.
Location at(double xm, double ym) { return Location::fromDegrees(16.7 + xm / (111320.0 * std::cos(53.15 * M_PI / 180)), 53.15 + ym / 110574.0); }
std::vector<Location> square(double x, double y, double side, bool clockwise = false) {
    std::vector<Location> r{at(x, y), at(x + side, y), at(x + side, y + side), at(x, y + side), at(x, y)};
    if (clockwise) std::reverse(r.begin(), r.end());
    return r;
}
double km2(const std::vector<Location> &ring) {
    const double kx = 111320.0 * std::cos(53.15 * M_PI / 180) / CoordinateScale, ky = 110574.0 / CoordinateScale;
    double a = 0;
    for (size_t i = 0; i + 1 < ring.size(); ++i) a += double(ring[i].x) * kx * ring[i + 1].y * ky - double(ring[i + 1].x) * kx * ring[i].y * ky;
    return a / 2e6;  // signed: counter-clockwise positive
}

}

void runGeneralizeTests(const std::function<void(bool, const char *)> &check) {
    GeneralizeOptions o;  // 100 m cells, 100 m closing, 0.25 km2, holes from 0.05 km2
    GeneralizeStats st;

    // Two parcels with their own nodes and a 50 m forest road between them.
    auto merged = generalizeAreas({{square(0, 0, 500)}, {square(550, 0, 500)}}, o, &st);
    check(merged.size() == 1 && merged[0].holes.empty() && km2(merged[0].outer) > 0.5 && km2(merged[0].outer) < 0.6 && merged[0].outer.size() <= 6,
          "parcels a forest road apart merge into one forest, simplified to a rectangle");
    check(st.polygonsIn == 2 && std::fabs(st.areaInKm2 - 0.5) < 0.02, "input area counted");

    // Far apart: two forests; a lone small wood is left out.
    auto apart = generalizeAreas({{square(0, 0, 600)}, {square(2000, 0, 600)}, {square(5000, 0, 300)}}, o);
    check(apart.size() == 2, "forests 1.4 km apart stay two; a 0.09 km2 wood alone is left out");

    // A clearing of 0.16 km2 stays a hole; one of 0.01 km2 is filled.
    auto clearing = generalizeAreas({{square(0, 0, 2000), square(500, 500, 400, true)}}, o);
    auto glade = generalizeAreas({{square(0, 0, 2000), square(500, 500, 100, true)}}, o);
    check(clearing.size() == 1 && clearing[0].holes.size() == 1 && km2(clearing[0].outer) > 0 && km2(clearing[0].holes[0]) < 0
              && std::fabs(-km2(clearing[0].holes[0]) - 0.16) < 0.05 && glade.size() == 1 && glade[0].holes.empty(),
          "a large clearing stays a hole (wound clockwise), a small glade is filled");

    // An island in a lake inside a forest: the forest keeps its hole, the island is its own area.
    auto island = generalizeAreas({{square(0, 0, 3000), square(800, 800, 1400, true)}, {square(1200, 1200, 600)}}, o);
    size_t withHole = 0, plain = 0;
    for (const auto &a : island) (a.holes.empty() ? plain : withHole)++;
    check(island.size() == 2 && withHole == 1 && plain == 1, "an island inside a hole is a separate area, not a second hole");

    // A multipolygon's inner ring is a hole of the input too.
    auto ring = generalizeAreas({{square(0, 0, 1500), square(300, 300, 900, true)}}, o);
    check(ring.size() == 1 && ring[0].holes.size() == 1 && std::fabs(km2(ring[0].outer) + km2(ring[0].holes[0]) - 1.44) < 0.1,
          "inner rings of the input stay holes; the area is kept within 0.1 km2");

    // A thousand touching 100 m parcels in a 1 x 1 km block become one area.
    std::vector<std::vector<std::vector<Location>>> parcels;
    for (int i = 0; i < 10; ++i) for (int j = 0; j < 10; ++j) parcels.push_back({square(i * 100.0, j * 100.0, 100)});
    auto block = generalizeAreas(parcels, o);
    check(block.size() == 1 && std::fabs(km2(block[0].outer) - 1.0) < 0.05, "a block of small parcels becomes one area of their size");
    check(generalizeAreas({}, o).empty(), "no input, no areas");

    // A 60 x 4 km forest with a clearing in every 5 km is cut into pieces of at most a block.
    std::vector<std::vector<Location>> longForest{square(0, 0, 60000)};
    longForest[0] = {at(0, 0), at(60000, 0), at(60000, 4000), at(0, 4000), at(0, 0)};
    for (int k = 0; k < 12; ++k) longForest.push_back(square(k * 5000.0 + 2000, 1500, 600, true));
    GeneralizeStats cut;
    auto pieces = generalizeAreas({longForest}, o, &cut);
    double total = 0, widest = 0;
    size_t holes = 0;
    for (const auto &a : pieces) {
        double piece = km2(a.outer);
        for (const auto &h : a.holes) piece += km2(h);
        total += piece;
        holes += a.holes.size();
        double x0 = 1e18, x1 = -1e18;
        for (const Location &l : a.outer) { x0 = std::min(x0, double(l.x)); x1 = std::max(x1, double(l.x)); }
        widest = std::max(widest, (x1 - x0) / CoordinateScale * 111320.0 * std::cos(53.15 * M_PI / 180));
    }
    check(pieces.size() >= 3 && widest <= o.blockMeters + 200 && std::fabs(total - (240 - 12 * 0.36)) < 3 && holes + 2 >= 12,
          "a 60 km forest comes out in pieces no wider than a block, keeping its area and clearings");
    // A forest reaching only 100 m into the next block keeps that sliver: size is judged whole.
    // Block lines run from the grid's corner, 3 cells (closing + 2) before the area: the
    // first at 25,300 m here, so a forest 25,450 m long has a 150 m x 1 km piece past it.
    const double length = o.blockMeters - 300 + 150;
    auto sliver = generalizeAreas({{{at(0, 0), at(length, 0), at(length, 1000), at(0, 1000), at(0, 0)}}}, o);
    check(sliver.size() >= 2, "the small piece of a large forest beyond a block line is kept");

}
