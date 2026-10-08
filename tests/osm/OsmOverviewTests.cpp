#include <tsre/geo/osm/OsmDirectory.h>
#include <tsre/geo/osm/OsmMultipolygon.h>
#include <tsre/geo/osm/OsmOverview.h>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>

using namespace Osm;

namespace {

// Points on a local metre grid around (18.6 E, 54.3 N).
Location at(double xm, double ym) { return Location::fromDegrees(18.6 + xm / (111320.0 * std::cos(54.3 * M_PI / 180)), 54.3 + ym / 110574.0); }

struct Fixture {
    BlockBuilder nodes, ways, rels;
    int64_t next = 1;
    const Tag none[1] = {{"", ""}};
    std::vector<int64_t> line(std::vector<std::pair<double, double>> pts) {
        std::vector<int64_t> refs;
        for (auto p : pts) { nodes.addNode(next, at(p.first, p.second), none, 0); refs.push_back(next++); }
        return refs;
    }
    std::vector<int64_t> square(double x, double y, double side) {
        auto r = line({{x, y}, {x + side, y}, {x + side, y + side}, {x, y + side}});
        r.push_back(r.front());
        return r;
    }
    void way(int64_t id, std::vector<Tag> tags, const std::vector<int64_t> &refs) { ways.addWay(id, tags.data(), tags.size(), refs.data(), nullptr, refs.size()); }
};

bool writeBytes(const QString &path, const std::string &bytes) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes.data(), qint64(bytes.size())) == qint64(bytes.size());
}

struct Content { std::set<int64_t> nodes, ways, relations; std::map<int64_t, uint32_t> points; };
Content contents(const QString &path) {
    Content c;
    SortedPbfStore s;
    QString e;
    if (!s.open(QStringList{path}, e)) return c;
    s.forEach(s.bounds(), Filter(), [&](const Feature &f) {
        if (f.type == ItemType::Node) c.nodes.insert(f.id);
        else if (f.type == ItemType::Way) { c.ways.insert(f.id); c.points[f.id] = f.refCount; }
        else c.relations.insert(f.id);
    }, e);
    return c;
}

}

void runOverviewTests(const std::function<void(bool, const char *)> &check) {
    const OverviewConfig &standard = OverviewConfig::standard();
    check(standard.levels.size() == 2 && standard.levels[0].name == "regional" && standard.levels[0].fromMetersPerPixel == 20
              && standard.levels[1].name == "national" && standard.levels[1].fromMetersPerPixel == 150 && !standard.hash.isEmpty(),
          "built-in overview levels: regional from 20 m/px, national from 150 m/px");
    OverviewConfig bad;
    QString error;
    check(!bad.load(QJsonDocument::fromJson(R"({"levels":[{"name":"x","fromMetersPerPixel":10,"toleranceMeters":1,"rules":[{"tags":["railway"]}]}]})").object(), error),
          "a rule tag without '=' is rejected");

    // Simplification.
    std::vector<Location> straight;
    for (int i = 0; i <= 100; ++i) straight.push_back(at(i * 10, (i % 2) * 1.0));  // 1 m jitter
    check(simplifyIndices(straight.data(), uint32_t(straight.size()), 5).size() == 2, "jitter below the tolerance collapses to the end points");
    std::vector<Location> zig;
    for (int i = 0; i <= 10; ++i) zig.push_back(at(i * 100, (i % 2) * 50.0));
    check(simplifyIndices(zig.data(), uint32_t(zig.size()), 5).size() == zig.size() && simplifyIndices(zig.data(), uint32_t(zig.size()), 0).size() == zig.size(),
          "bends larger than the tolerance stay");
    std::vector<Location> ring{at(0, 0), at(100, 0), at(100, 100), at(0, 100), at(0, 0)};
    const auto kept = simplifyIndices(ring.data(), uint32_t(ring.size()), 5);
    check(kept.size() == 5 && kept.front() == 0 && kept.back() == 4, "a closed ring keeps its corners and closure");

    // Fixture: one feature per rule decision.
    QTemporaryDir dir;
    Fixture fx;
    std::vector<std::pair<double, double>> wiggle;
    for (int i = 0; i <= 200; ++i) wiggle.push_back({i * 50.0, 20.0 * std::sin(i * 0.7)});
    fx.way(1, {{"railway", "rail"}, {"usage", "main"}}, fx.line({{0, 0}, {5000, 0}, {10000, 100}}));
    fx.way(2, {{"railway", "rail"}, {"service", "siding"}}, fx.line({{0, 50}, {800, 60}}));
    fx.way(3, {{"highway", "primary"}}, fx.line({{0, 2000}, {9000, 2100}}));
    fx.way(4, {{"highway", "residential"}}, fx.line({{100, 3000}, {300, 3000}}));
    fx.way(5, {{"landuse", "forest"}}, fx.square(1000, 4000, 100));                    // 0.01 km2
    fx.way(6, {{"landuse", "forest"}}, fx.square(3000, 4000, 1500));                   // 2.25 km2
    fx.way(7, {{"waterway", "river"}}, fx.line(wiggle));
    const auto lakeOuter = fx.square(6000, 4000, 1500), pondOuter = fx.square(9000, 4000, 150);
    fx.way(8, {}, lakeOuter);
    fx.way(9, {}, pondOuter);
    const Tag lake[] = {{"type", "multipolygon"}, {"natural", "water"}};
    const BlockBuilder::MemberIn lakeMembers[] = {{8, ItemType::Way, "outer"}}, pondMembers[] = {{9, ItemType::Way, "outer"}};
    fx.rels.addRelation(100, lake, 2, lakeMembers, 1);   // 2.25 km2
    fx.rels.addRelation(101, lake, 2, pondMembers, 1);   // 0.0225 km2
    const Tag city[] = {{"place", "city"}, {"name", "Gdańsk"}}, hamlet[] = {{"place", "hamlet"}}, station[] = {{"railway", "station"}};
    fx.nodes.addNode(fx.next++, at(500, 500), city, 2);
    const int64_t cityId = fx.next - 1;
    fx.nodes.addNode(fx.next++, at(700, 500), hamlet, 1);
    fx.nodes.addNode(fx.next++, at(900, 500), station, 1);
    const int64_t stationId = fx.next - 1;
    HeaderInfo h;
    h.requiredFeatures << "OsmSchema-V0.6" << "DenseNodes";
    h.optionalFeatures << "Sort.Type_then_ID";
    std::string a, b, c;
    fx.nodes.build(a); fx.ways.build(b); fx.rels.build(c);
    check(writeBytes(dir.filePath("area.osm.pbf"), encodeBlobFrame("OSMHeader", encodeHeaderBlock(h), {}, 6) + encodeBlobFrame("OSMData", a, {}, 6)
                                                       + encodeBlobFrame("OSMData", b, {}, 6) + encodeBlobFrame("OSMData", c, {}, 6)),
          "write overview fixture");
    OsmDirectory od;
    check(od.scan(dir.path(), error) && od.pendingConversions().size() == 1, "fixture is a pending download");
    ConvertOptions options; options.threads = 2;
    ConvertStats stats;
    check(OsmDirectory::convert(*od.pendingConversions()[0], false, options, stats, error), "converting also builds the overviews");
    const QString converted = dir.filePath("area.tsre.osm.pbf");
    const QString regionalPath = overviewPathFor(converted, "regional"), nationalPath = overviewPathFor(converted, "national");
    check(QFileInfo(regionalPath).fileName() == "area.tsre.overview.regional.pbf" && QFile::exists(regionalPath) && QFile::exists(nationalPath)
              && overviewUpToDate(converted, standard, 0) && overviewUpToDate(converted, standard, 1),
          "overview files sit next to the converted file and are up to date");
    check(od.scan(dir.path(), error) && od.convertedFiles().size() == 1 && od.pendingOverviews(standard).empty(),
          "overview files are not mistaken for downloads or converted files");

    const Content regional = contents(regionalPath), national = contents(nationalPath);
    check(regional.ways == std::set<int64_t>({1, 2, 3, 6, 7, 8}) && regional.relations == std::set<int64_t>({100})
              && regional.nodes == std::set<int64_t>({cityId, stationId}),
          "regional: all railways, main roads, rivers, areas of 5 ha and more with their members, towns and stations");
    check(national.ways == std::set<int64_t>({1, 3, 6, 7, 8}) && national.relations == std::set<int64_t>({100}) && national.nodes == std::set<int64_t>({cityId}),
          "national: no sidings or stations, areas of 1 km2 and more, cities");
    check(regional.points.at(7) < 201 && national.points.at(7) < regional.points.at(7) && regional.points.at(1) == 3,
          "the river is simplified, more on the national level");
    SortedPbfStore nat;
    std::vector<RelationData> rels;
    Filter relFilter; relFilter.types = Relations;
    nat.open(QStringList{nationalPath}, error);
    nat.forEach(nat.bounds(), relFilter, [&](const Feature &f) { rels.push_back(RelationData::from(f)); }, error);
    std::vector<MultipolygonResult> mp;
    check(assembleMultipolygons(nat, rels, mp, error) && mp.size() == 1 && mp[0].ok(), "multipolygons assemble from an overview file");

    // Staleness and scale selection.
    OverviewConfig changed;
    QFile json(":/osm/osm-map-classes.json");
    json.open(QIODevice::ReadOnly);
    QJsonObject section = QJsonDocument::fromJson(json.readAll()).object().value("overview").toObject();
    changed.load(section, error);
    check(changed.hash == standard.hash && overviewUpToDate(converted, changed, 0), "the same rules give the same hash");
    section["comment"] = "changed";
    changed.load(section, error);
    check(changed.hash != standard.hash && !overviewUpToDate(converted, changed, 0), "changed rules make the overview stale");

    OsmLayers layers;
    check(layers.open(od, standard, error) && layers.hasLevel(0) && layers.hasLevel(1), "layers open the detail file and both overview levels");
    check(&layers.forScale(1) == &layers.detail() && layers.levelForScale(1) == -1 && layers.levelForScale(20) == 0 && layers.levelForScale(149) == 0
              && layers.levelForScale(150) == 1 && &layers.forScale(500) != &layers.forScale(30) && &layers.forScale(30) != &layers.detail(),
          "scale picks detail below 20 m/px, regional to 150 m/px, national beyond");
    const auto shared = sharedLayers(dir.path(), error), again = sharedLayers(dir.path(), error);
    QTemporaryDir empty;
    check(shared && shared == again && shared->hasLevel(1) && !sharedLayers(empty.path(), error),
          "shared layers open once while the files stay the same; none without converted files");
    QFile::remove(nationalPath);
    const auto reopened = sharedLayers(dir.path(), error);
    check(reopened && reopened != shared && !reopened->hasLevel(1) && shared->hasLevel(1),
          "shared layers reopen when an overview goes; users of the old ones keep them");
    OsmLayers partial;
    check(partial.open(od, standard, error) && !partial.hasLevel(1) && &partial.forScale(500) != &partial.detail(),
          "a missing national level falls back to the regional one");
    QThread::msleep(20);
    {
        QFile touch(converted);
        touch.open(QIODevice::Append);
        touch.setFileTime(QDateTime::currentDateTime().addSecs(5), QFileDevice::FileModificationTime);
        touch.close();
    }
    check(!overviewUpToDate(converted, standard, 0) && od.scan(dir.path(), error) && od.pendingOverviews(standard).size() == 1,
          "a changed converted file makes its overviews stale");
}

// Opt-in: --overview <converted files...>  builds the overviews and times all railways per level.
int overviewFiles(const QStringList &files) {
    const OverviewConfig &config = OverviewConfig::standard();
    for (const QString &path : files) {
        auto t0 = std::chrono::steady_clock::now();
        std::vector<OverviewStats> stats;
        QString error;
        if (!buildOverviews(path, config, stats, error)) { std::cerr << error.toStdString() << '\n'; return 1; }
        std::cout << QFileInfo(path).fileName().toStdString() << ": overviews built in "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s\n";
        for (size_t l = 0; l < stats.size(); ++l) {
            const OverviewStats &s = stats[l];
            const QString overview = overviewPathFor(path, config.levels[l].name);
            SortedPbfStore store;
            store.open(QStringList{overview}, error);
            Filter rails; rails.types = Ways; rails.keys = {"railway"};
            uint64_t n = 0, pts = 0;
            t0 = std::chrono::steady_clock::now();
            store.forEach(store.bounds(), rails, [&](const Feature &f) { n += f.value("railway") == "rail"; pts += f.refCount; }, error);
            const double cold = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            uint64_t all = 0;
            t0 = std::chrono::steady_clock::now();
            store.forEach(store.bounds(), Filter(), [&](const Feature &) { ++all; }, error);
            const double everything = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::cout << "  " << config.levels[l].name << ": " << s.bytes / 1e6 << " MB, " << s.nodes << " nodes, " << s.ways << " ways, " << s.relations
                      << " relations, points " << s.pointsIn << " -> " << s.pointsOut << " | all railways: " << n << " rail ways, " << pts
                      << " points in " << cold << " s | whole level: " << all << " features in " << everything << " s (warm)\n";
        }
    }
    return 0;
}
