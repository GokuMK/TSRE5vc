#include <tsre/geo/osm/OsmConverter.h>
#include <tsre/geo/osm/OsmMultipolygon.h>
#include <tsre/geo/osm/SortedPbfStore.h>
#include <QFile>
#include <QTemporaryDir>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>

using namespace Osm;

namespace {

// Ways on a 1e-7 degree integer grid; node ids are derived from coordinates so shared
// corners share ids, as in OSM.
struct Builder {
    std::map<int64_t, WayGeometry> ways;
    int64_t node(int x, int y) { return int64_t(x) * 100000 + y + 1; }
    void way(int64_t id, std::vector<std::pair<int, int>> pts) {
        WayGeometry g;
        for (auto p : pts) { g.refs.push_back(node(p.first, p.second)); g.locations.push_back(Location(p.first * 1000, p.second * 1000)); }
        ways[id] = g;
    }
    std::function<const WayGeometry *(int64_t)> lookup() const {
        return [this](int64_t id) -> const WayGeometry * { auto it = ways.find(id); return it == ways.end() ? nullptr : &it->second; };
    }
};

RelationData relation(std::vector<std::pair<int64_t, std::string>> members) {
    RelationData r;
    r.id = 1;
    r.tags = {{"type", "multipolygon"}, {"natural", "water"}};
    for (const auto &m : members) r.members.push_back({m.first, ItemType::Way, m.second});
    return r;
}

double area(const std::vector<Location> &ring) { return signedArea(ring) / 1e6; }

}

void runMultipolygonTests(const std::function<void(bool, const char *)> &check) {
    Builder b;
    b.way(1, {{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}});
    auto r = assembleMultipolygon(relation({{1, "outer"}}), b.lookup());
    check(r.ok() && r.polygons.size() == 1 && r.polygons[0].outer.size() == 5 && area(r.polygons[0].outer) == 100, "one closed outer way is one polygon");

    // Outer split in three, the middle one drawn backwards; a hole drawn counter-clockwise.
    b.way(10, {{0, 0}, {20, 0}, {20, 10}});
    b.way(11, {{0, 20}, {20, 20}, {20, 10}});
    b.way(12, {{0, 20}, {0, 0}});
    b.way(13, {{5, 5}, {8, 5}, {8, 8}, {5, 8}, {5, 5}});
    r = assembleMultipolygon(relation({{10, "outer"}, {11, "outer"}, {12, "outer"}, {13, "inner"}}), b.lookup());
    check(r.ok() && r.polygons.size() == 1 && r.polygons[0].outer.size() == 6 && r.polygons[0].outer.front() == r.polygons[0].outer.back()
              && r.brokenRings == 0,
          "an outer split over three ways, one reversed, joins into one closed ring");
    check(r.polygons[0].inners.size() == 1 && area(r.polygons[0].outer) == 400 && area(r.polygons[0].inners[0]) == -9,
          "outer rings run counter-clockwise and holes clockwise");

    // Two separate lakes, each with an island; the holes go to the right outer.
    b.way(20, {{100, 0}, {110, 0}, {110, 10}, {100, 10}, {100, 0}});
    b.way(21, {{102, 2}, {104, 2}, {104, 4}, {102, 4}, {102, 2}});
    b.way(22, {{2, 2}, {3, 2}, {3, 3}, {2, 3}, {2, 2}});
    r = assembleMultipolygon(relation({{1, "outer"}, {20, "outer"}, {21, "inner"}, {22, "inner"}}), b.lookup());
    bool assigned = r.polygons.size() == 2;
    for (const Polygon &p : r.polygons) {
        assigned &= p.inners.size() == 1;
        if (assigned) assigned &= pointInRing(p.inners[0][0], p.outer);
    }
    check(assigned, "each hole goes to the outer ring that contains it");

    // A lake with an island that has its own pond: the island is a second outer polygon.
    b.way(30, {{0, 0}, {50, 0}, {50, 50}, {0, 50}, {0, 0}});
    b.way(31, {{10, 10}, {40, 10}, {40, 40}, {10, 40}, {10, 10}});
    b.way(32, {{20, 20}, {30, 20}, {30, 30}, {20, 30}, {20, 20}});
    r = assembleMultipolygon(relation({{30, "outer"}, {31, "inner"}, {32, "outer"}}), b.lookup());
    check(r.polygons.size() == 2 && r.polygons[0].outer.size() == 5 && area(r.polygons[0].outer) == 100 && r.polygons[0].inners.empty()
              && area(r.polygons[1].outer) == 2500 && r.polygons[1].inners.size() == 1,
          "an island inside a hole is its own polygon and the hole belongs to the big outer");

    // Gaps: a missing member and an unclosed chain are dropped and counted.
    b.way(40, {{0, 0}, {10, 0}, {10, 10}});
    r = assembleMultipolygon(relation({{40, "outer"}, {999, "outer"}}), b.lookup());
    check(!r.ok() && r.brokenRings == 2, "missing members and open chains give no polygon");
    r = assembleMultipolygon(relation({{1, "outer"}, {40, "outer"}}), b.lookup());
    check(r.ok() && r.polygons.size() == 1 && r.brokenRings == 1, "a broken ring does not spoil the closed ones");
    r = assembleMultipolygon(relation({{13, "inner"}}), b.lookup());
    check(!r.ok() && r.orphanInners == 1, "an inner ring without an outer is dropped");
    r = assembleMultipolygon(relation({{1, ""}, {13, "label"}}), b.lookup());
    check(r.ok() && r.polygons[0].inners.empty(), "an empty role counts as outer and other roles are ignored");
    // Two outer rings touching at one corner node still close separately.
    b.way(50, {{60, 0}, {70, 0}, {70, 10}, {60, 0}});
    b.way(51, {{60, 0}, {50, 10}, {50, 0}, {60, 0}});
    r = assembleMultipolygon(relation({{50, "outer"}, {51, "outer"}}), b.lookup());
    check(r.polygons.size() == 2 && r.brokenRings == 0, "closed rings sharing a node stay separate");

    // Through the store: a relation's member ways come from a converted file.
    QTemporaryDir dir;
    HeaderInfo h;
    h.requiredFeatures << "OsmSchema-V0.6" << "DenseNodes";
    h.optionalFeatures << "Sort.Type_then_ID";
    BlockBuilder nodes, ways, rels;
    const Tag none[] = {{"", ""}};
    std::map<int64_t, Location> nodeLoc;
    for (const auto &w : b.ways)
        for (size_t i = 0; i < w.second.refs.size(); ++i) nodeLoc[w.second.refs[i]] = Location(w.second.locations[i].x + 186000000, w.second.locations[i].y + 543000000);
    for (const auto &n : nodeLoc) nodes.addNode(n.first, n.second, none, 0);
    for (int64_t id : {10, 11, 12, 13}) ways.addWay(id, none, 0, b.ways[id].refs.data(), nullptr, b.ways[id].refs.size());
    const Tag lake[] = {{"type", "multipolygon"}, {"natural", "water"}};
    const BlockBuilder::MemberIn members[] = {{10, ItemType::Way, "outer"}, {11, ItemType::Way, "outer"}, {12, ItemType::Way, "outer"}, {13, ItemType::Way, "inner"}};
    rels.addRelation(5, lake, 2, members, 4);
    std::string a, w2, rr;
    nodes.build(a); ways.build(w2); rels.build(rr);
    const std::string bytes = encodeBlobFrame("OSMHeader", encodeHeaderBlock(h), {}, 6) + encodeBlobFrame("OSMData", a, {}, 6)
                              + encodeBlobFrame("OSMData", w2, {}, 6) + encodeBlobFrame("OSMData", rr, {}, 6);
    QFile f(dir.filePath("lake.osm.pbf"));
    f.open(QIODevice::WriteOnly); f.write(bytes.data(), qint64(bytes.size())); f.close();
    ConvertOptions options; options.threads = 2;
    ConvertStats stats; QString error;
    SortedPbfStore store;
    check(convertPbf(dir.filePath("lake.osm.pbf"), dir.filePath("lake.tsre.osm.pbf"), options, stats, error) && store.open(QStringList{dir.filePath("lake.tsre.osm.pbf")}, error),
          "lake fixture converts and opens");
    std::vector<RelationData> found;
    Filter relationsOnly; relationsOnly.types = Relations;
    store.forEach(Box::fromDegrees(18, 54, 19, 55), relationsOnly, [&](const Feature &r) { found.push_back(RelationData::from(r)); }, error);
    std::vector<MultipolygonResult> results;
    check(found.size() == 1 && found[0].value("natural") == "water" && assembleMultipolygons(store, found, results, error)
              && results.size() == 1 && results[0].ok() && results[0].polygons[0].inners.size() == 1 && area(results[0].polygons[0].outer) == 400,
          "multipolygons assemble from member ways fetched through the store");
}

// Opt-in local data: --multipolygons <minLon> <minLat> <maxLon> <maxLat> <converted files...>
int assembleArea(const QStringList &args) {
    const Box area = Box::fromDegrees(args[0].toDouble(), args[1].toDouble(), args[2].toDouble(), args[3].toDouble());
    SortedPbfStore store;
    QString error;
    if (!store.open(args.mid(4), error)) { std::cerr << error.toStdString() << '\n'; return 1; }
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<RelationData> relations;
    Filter filter; filter.types = Relations;
    store.forEach(area, filter, [&](const Feature &f) {
        const auto type = f.value("type");
        if (type == "multipolygon" || (type == "boundary" && f.value("boundary") != "administrative")) relations.push_back(RelationData::from(f));
    }, error);
    std::vector<MultipolygonResult> results;
    if (!assembleMultipolygons(store, relations, results, error)) { std::cerr << error.toStdString() << '\n'; return 1; }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    size_t ok = 0, polygons = 0, holes = 0, broken = 0, orphans = 0;
    for (const auto &r : results) {
        ok += r.ok(); polygons += r.polygons.size(); broken += r.brokenRings > 0; orphans += r.orphanInners;
        for (const auto &p : r.polygons) holes += p.inners.size();
    }
    std::cout << relations.size() << " multipolygon relations in " << s << " s: " << ok << " assembled (" << polygons << " polygons, " << holes
              << " holes), " << broken << " with broken rings, " << orphans << " orphan inner rings\n";
    return 0;
}
