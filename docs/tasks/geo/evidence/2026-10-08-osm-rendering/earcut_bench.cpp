// Triangulation cost of the filled OSM areas in a view: earcut.hpp, and a convex-fan shortcut.
// earcut_bench <minLon> <minLat> <maxLon> <maxLat> <converted files...>
#include <tsre/geo/osm/SortedPbfStore.h>
#include <tsre/geo/osm/OsmMultipolygon.h>
#include <tsre/geo/osm/OsmClasses.h>
#include <mapbox/earcut.hpp>
#include <QCoreApplication>
#include <QStringList>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>

using namespace Osm;
using Point = std::array<double, 2>;
using Clock = std::chrono::steady_clock;
static double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    const Box area = Box::fromDegrees(args[0].toDouble(), args[1].toDouble(), args[2].toDouble(), args[3].toDouble());
    const double lon0 = 0.5 * (args[0].toDouble() + args[2].toDouble()), lat0 = 0.5 * (args[1].toDouble() + args[3].toDouble());
    const double kx = 111320.0 * std::cos(lat0 * M_PI / 180), ky = 110540.0;
    auto local = [&](Location l) { return Point{(l.lon() - lon0) * kx, (l.lat() - lat0) * ky}; };

    SortedPbfStore store;
    QString error;
    if (!store.open(args.mid(4), error)) { std::cerr << error.toStdString() << '\n'; return 1; }
    const FeatureClasses &classes = FeatureClasses::standard();

    // polygons: rings in local metres, first ring outer.
    std::vector<std::vector<std::vector<Point>>> polygons;
    std::vector<RelationData> relations;
    size_t linePoints = 0;
    auto t = Clock::now();
    store.forEach(area, Filter(), [&](const Feature &f) {
        if (f.type == ItemType::Relation) {
            const auto type = f.value("type");
            if (type == "multipolygon") relations.push_back(RelationData::from(f));
            return;
        }
        if (f.type != ItemType::Way || f.refCount < 4) return;
        const Classification c = classes.classify(f);
        if (!c.cls) return;
        const Style &s = classes.style(c);
        if (s.hasLine || !s.casings.empty()) linePoints += f.refCount;
        if (!s.hasFill || f.refs[0] != f.refs[f.refCount - 1]) return;
        std::vector<Point> ring;
        ring.reserve(f.refCount - 1);
        for (uint32_t i = 0; i + 1 < f.refCount; ++i) ring.push_back(local(f.locations[i]));
        polygons.push_back({std::move(ring)});
    }, error);
    const double readS = since(t);
    t = Clock::now();
    std::vector<MultipolygonResult> results;
    assembleMultipolygons(store, relations, results, error);
    size_t mpPolys = 0;
    for (const auto &r : results)
        for (const auto &p : r.polygons) {
            std::vector<std::vector<Point>> rings;
            auto add = [&](const std::vector<Location> &ring) {
                std::vector<Point> out;
                for (size_t i = 0; i + 1 < ring.size(); ++i) out.push_back(local(ring[i]));
                rings.push_back(std::move(out));
            };
            add(p.outer);
            for (const auto &in : p.inners) add(in);
            polygons.push_back(std::move(rings));
            ++mpPolys;
        }
    const double mpS = since(t);

    size_t vertices = 0, holes = 0, convexCount = 0;
    for (const auto &p : polygons) { for (const auto &r : p) vertices += r.size(); holes += p.size() - 1; }

    auto convex = [](const std::vector<Point> &r) {
        const size_t n = r.size();
        if (n < 3) return false;
        int sign = 0;
        for (size_t i = 0; i < n; ++i) {
            const Point &a = r[i], &b = r[(i + 1) % n], &c = r[(i + 2) % n];
            const double cross = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);
            if (cross == 0) continue;
            const int s = cross > 0 ? 1 : -1;
            if (sign && s != sign) return false;
            sign = s;
        }
        return sign != 0;
    };

    for (int pass = 0; pass < 2; ++pass) {
        size_t triangles = 0, failed = 0;
        t = Clock::now();
        for (const auto &p : polygons) {
            const auto idx = mapbox::earcut<uint32_t>(p);
            triangles += idx.size() / 3;
            size_t n = 0; for (const auto &r : p) n += r.size();
            if (idx.empty() && n >= 3) ++failed;
        }
        const double earS = since(t);
        size_t fanTriangles = 0; convexCount = 0;
        t = Clock::now();
        for (const auto &p : polygons) {
            if (p.size() == 1 && convex(p[0])) { ++convexCount; fanTriangles += p[0].size() - 2; continue; }
            fanTriangles += mapbox::earcut<uint32_t>(p).size() / 3;
        }
        const double fanS = since(t);
        if (pass)
            std::cout << "read " << readS << " s, multipolygons " << mpS << " s (" << mpPolys << " polygons)\n"
                      << polygons.size() << " filled polygons, " << vertices << " vertices, " << holes << " holes; "
                      << linePoints << " points on stroked ways\n"
                      << "earcut: " << earS << " s, " << triangles << " triangles, " << failed << " empty results, "
                      << vertices / earS / 1e6 << " M vertices/s\n"
                      << "convex fan + earcut: " << fanS << " s (" << convexCount << " convex), " << fanTriangles << " triangles\n";
    }
    return 0;
}
