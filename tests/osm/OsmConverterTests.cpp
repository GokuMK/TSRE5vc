#include <tsre/geo/osm/OsmConverter.h>
#include <tsre/geo/osm/OsmPbf.h>
#include <tsre/geo/osm/OsmSortedFormat.h>
#include <tsre/geo/osm/SortedPbfStore.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <thread>

using namespace Osm;

namespace {

struct FixtureNode { int64_t id; Location location; std::vector<std::pair<std::string, std::string>> tags; };
struct FixtureWay { int64_t id; std::vector<std::pair<std::string, std::string>> tags; std::vector<int64_t> refs; };
struct FixtureMember { int64_t ref; ItemType type; std::string role; };
struct FixtureRelation { int64_t id; std::vector<std::pair<std::string, std::string>> tags; std::vector<FixtureMember> members; };
struct Fixture { std::vector<FixtureNode> nodes; std::vector<FixtureWay> ways; std::vector<FixtureRelation> relations; };

Fixture makeFixture() {
    Fixture f;
    // 40 x 30 grid over 3 x 2 degrees around Gdańsk, every 7th node tagged.
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 40; ++x) {
            const int64_t id = 1000 + y * 40 + x;
            FixtureNode n{id, Location::fromDegrees(17.0 + x * 0.075, 53.5 + y * 0.069), {}};
            if (id % 7 == 0) n.tags = {{"amenity", "bench"}, {"name", "Ławka " + std::to_string(id)}};
            f.nodes.push_back(n);
        }
    auto node = [](int x, int y) { return int64_t(1000 + y * 40 + x); };
    int64_t wid = 10;
    for (int y = 0; y < 30; y += 2)
        for (int x = 0; x + 3 < 40; x += 3)
            f.ways.push_back({wid++, {{"highway", "residential"}}, {node(x, y), node(x + 1, y), node(x + 2, y), node(x + 3, y)}});
    f.ways.push_back({900, {}, {node(5, 5), node(6, 5), node(6, 6), node(5, 6), node(5, 5)}});              // untagged member: kept
    f.ways.push_back({901, {}, {node(7, 7), node(8, 7)}});                                                  // untagged, not a member: dropped
    f.ways.push_back({902, {{"railway", "rail"}}, {node(1, 1), 999999, node(2, 1)}});                       // one missing node
    f.ways.push_back({903, {{"railway", "rail"}}, {888888, 777777}});                                       // only missing nodes: dropped
    f.ways.push_back({904, {{"waterway", "river"}}, {node(0, 0), node(20, 15), node(39, 29)}});             // spans 3 degrees
    f.ways.push_back({905, {{"building", "yes"}}, {node(10, 10), node(11, 10), node(11, 11), node(10, 10)}}); // inner ring
    f.relations.push_back({1, {{"type", "multipolygon"}, {"landuse", "forest"}}, {{900, ItemType::Way, "outer"}, {905, ItemType::Way, "inner"}}});
    f.relations.push_back({2, {{"type", "site"}}, {{node(3, 3), ItemType::Node, "entrance"}, {1, ItemType::Relation, ""}}});
    f.relations.push_back({3, {{"type", "route"}}, {{555555, ItemType::Way, ""}}});                         // unresolvable: unplaced
    f.relations.push_back({4, {{"type", "collection"}}, {{3, ItemType::Relation, "part"}}});               // only an unplaced member
    std::sort(f.ways.begin(), f.ways.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return f;
}

std::vector<Tag> tagsOf(const std::vector<std::pair<std::string, std::string>> &t) {
    std::vector<Tag> v;
    for (const auto &p : t) v.push_back({p.first, p.second});
    return v;
}

// Geofabrik style: Sort.Type_then_ID, small blocks, no locations on ways. Unsorted: blocks
// shuffled, one block mixing nodes and ways, no sort flag.
std::string writeSource(const Fixture &f, bool sorted, int64_t timestamp) {
    HeaderInfo h;
    h.bbox = Box::fromDegrees(16.9, 53.4, 20.1, 55.6);
    h.requiredFeatures << "OsmSchema-V0.6" << "DenseNodes";
    if (sorted) h.optionalFeatures << "Sort.Type_then_ID";
    h.writingProgram = "osmium/1.16.0";
    h.replicationTimestamp = timestamp;
    std::vector<std::string> frames;
    BlockBuilder b;
    std::string raw;
    auto flush = [&] { if (!b.count()) return; b.build(raw); frames.push_back(encodeBlobFrame("OSMData", raw, {}, 6)); b.clear(); };
    for (size_t i = 0; i < f.nodes.size(); ++i) {
        const auto tags = tagsOf(f.nodes[i].tags);
        b.addNode(f.nodes[i].id, f.nodes[i].location, tags.data(), tags.size());
        if (b.count() == 97) flush();
    }
    if (sorted) flush();
    for (const auto &w : f.ways) {
        const auto tags = tagsOf(w.tags);
        b.addWay(w.id, tags.data(), tags.size(), w.refs.data(), nullptr, w.refs.size());
        if (b.count() == 50) flush();
    }
    flush();
    for (const auto &r : f.relations) {
        const auto tags = tagsOf(r.tags);
        std::vector<BlockBuilder::MemberIn> m;
        for (const auto &x : r.members) m.push_back({x.ref, x.type, x.role});
        b.addRelation(r.id, tags.data(), tags.size(), m.data(), m.size());
    }
    flush();
    if (!sorted) std::shuffle(frames.begin(), frames.end(), std::mt19937(42));
    std::string out = encodeBlobFrame("OSMHeader", encodeHeaderBlock(h), {}, 6);
    for (const auto &fr : frames) out += fr;
    return out;
}

bool writeFile(const QString &path, const std::string &bytes) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes.data(), qint64(bytes.size())) == qint64(bytes.size());
}
QByteArray readFile(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }

struct Converted {
    struct Way { std::vector<std::pair<std::string, std::string>> tags; std::vector<int64_t> refs; std::vector<Location> locations; };
    std::map<int64_t, std::pair<Location, std::vector<std::pair<std::string, std::string>>>> nodes;
    std::map<int64_t, Way> ways;
    std::map<int64_t, std::pair<std::vector<std::pair<std::string, std::string>>, std::vector<FixtureMember>>> relations;
    std::vector<std::pair<Sorted::BlockIndex, Box>> blocks;  // index, actual extent of the block's nodes/ways
    std::vector<uint32_t> blockBuckets;                     // max placement bucket per block
    std::map<int64_t, size_t> relationBlock;
    bool ok = true;
};

Converted readConverted(const QString &path, const Fixture *fixture) {
    Converted c;
    PbfFile f;
    QString e;
    if (!f.open(path, e)) { c.ok = false; return c; }
    PrimitiveBlock b;
    for (size_t i = 1; i < f.blobs().size(); ++i) {
        Sorted::BlockIndex index;
        if (!Sorted::decodeIndex(f.blobs()[i].indexData, index) || !f.readBlock(i, b, DecodeAll, e)) { c.ok = false; return c; }
        Box extent;
        uint32_t bucket = 0;
        auto tags = [&](uint32_t first, uint32_t count) {
            std::vector<std::pair<std::string, std::string>> t;
            for (uint32_t k = 0; k < count; ++k) { const Tag tag = b.tag(first + k); t.push_back({std::string(tag.key), std::string(tag.value)}); }
            return t;
        };
        for (const auto &n : b.nodes) {
            c.nodes[n.id] = {n.location, tags(n.tagFirst, n.tagCount)};
            extent.extend(n.location);
            Box nb; nb.extend(n.location);
            bucket = std::max(bucket, Sorted::place(nb).bucket);
        }
        for (const auto &w : b.ways) {
            Converted::Way way{tags(w.tagFirst, w.tagCount), {}, {}};
            Box wb;
            for (uint32_t r = 0; r < w.refCount; ++r) {
                way.refs.push_back(b.refs[w.refFirst + r]);
                way.locations.push_back(b.wayLocations ? b.refLocations[w.refFirst + r] : Location());
                wb.extend(way.locations.back());
            }
            extent.extend(wb);
            bucket = std::max(bucket, Sorted::place(wb).bucket);
            c.ways[w.id] = way;
        }
        for (const auto &r : b.relations) {
            std::vector<FixtureMember> m;
            for (uint32_t k = 0; k < r.memberCount; ++k) {
                const Member &mem = b.members[r.memberFirst + k];
                m.push_back({mem.ref, mem.type, std::string(b.role(mem))});
            }
            c.relations[r.id] = {tags(r.tagFirst, r.tagCount), m};
            c.relationBlock[r.id] = c.blocks.size();
        }
        const int kinds = !b.nodes.empty() + !b.ways.empty() + !b.relations.empty();
        if (kinds != 1) c.ok = false;
        if ((!b.nodes.empty() && index.kind != ItemType::Node) || (!b.ways.empty() && index.kind != ItemType::Way)
            || (!b.relations.empty() && index.kind != ItemType::Relation))
            c.ok = false;
        c.blocks.push_back({index, extent});
        c.blockBuckets.push_back(bucket);
    }
    (void)fixture;
    return c;
}

void converterTests(const std::function<void(bool, const char *)> &check) {
    QTemporaryDir dir;
    const Fixture fx = makeFixture();
    const QString sortedPath = dir.filePath("sample.osm.pbf"), unsortedPath = dir.filePath("shuffled.osm.pbf");
    const std::string sortedBytes = writeSource(fx, true, 1791318066);
    check(writeFile(sortedPath, sortedBytes) && writeFile(unsortedPath, writeSource(fx, false, 1791318066)), "write source fixtures");

    ConvertOptions options;
    options.threads = 4;
    ConvertStats stats;
    QString error;
    std::set<ConvertPhase> phases;
    const QString out1 = dir.filePath("sample" + Sorted::ConvertedSuffix);
    const bool converted = convertPbf(sortedPath, out1, options, stats, error, [&](ConvertPhase p, double) { phases.insert(p); });
    check(converted, "convert a Geofabrik-style file");
    if (!converted) { std::cerr << error.toStdString() << '\n'; return; }
    check(phases.count(ConvertPhase::Nodes) && phases.count(ConvertPhase::Ways) && phases.count(ConvertPhase::Write), "progress reports the phases");
    check(!QFile::exists(out1 + ".part") && !QDir(out1 + ".tmp").exists(), "no temporary files remain");

    uint64_t taggedNodes = 0;
    for (const auto &n : fx.nodes) taggedNodes += !n.tags.empty();
    check(stats.sourceNodes == fx.nodes.size() && stats.taggedNodes == taggedNodes && stats.sourceWays == fx.ways.size()
              && stats.keptWays == fx.ways.size() - 2 && stats.droppedWays == 2 && stats.missingRefs == 3
              && stats.relations == 4 && stats.unplacedRelations == 2,
          "statistics count kept, dropped and unresolved entities");

    PbfFile pf;
    check(pf.open(out1, error) && pf.header().hasFeature(Sorted::FeatureMarker) && pf.header().hasFeature("LocationsOnWays")
              && !pf.header().hasFeature("Sort.Type_then_ID") && pf.header().replicationTimestamp == 1791318066,
          "converted header carries the TSRE marker and replication timestamp");
    Sorted::SourceIdentity id;
    check(Sorted::SourceIdentity::fromString(pf.header().source, id) && id.name == "sample.osm.pbf"
              && id.size == int64_t(sortedBytes.size()) && id.timestamp == 1791318066,
          "converted header records the source identity");
    pf.close();

    const Converted c = readConverted(out1, &fx);
    check(c.ok, "every converted block has a valid index and a single kind");
    bool nodesOk = c.nodes.size() == taggedNodes;
    for (const auto &n : fx.nodes) {
        if (n.tags.empty()) { nodesOk &= !c.nodes.count(n.id); continue; }
        auto it = c.nodes.find(n.id);
        nodesOk &= it != c.nodes.end() && it->second.first == n.location && it->second.second == n.tags;
    }
    check(nodesOk, "tagged nodes keep location and tags; untagged nodes are dropped");
    std::map<int64_t, Location> loc;
    for (const auto &n : fx.nodes) loc[n.id] = n.location;
    bool waysOk = c.ways.size() == fx.ways.size() - 2 && !c.ways.count(901) && !c.ways.count(903);
    for (const auto &w : fx.ways) {
        auto it = c.ways.find(w.id);
        if (it == c.ways.end()) continue;
        std::vector<int64_t> refs;
        for (int64_t r : w.refs) if (loc.count(r)) refs.push_back(r);
        waysOk &= it->second.refs == refs && it->second.tags == w.tags;
        for (size_t i = 0; i < refs.size() && i < it->second.locations.size(); ++i) waysOk &= it->second.locations[i] == loc[refs[i]];
    }
    check(waysOk, "ways keep refs, tags and exact locations; missing nodes are skipped; non-members without tags are dropped");
    bool relsOk = c.relations.size() == 4;
    for (const auto &r : fx.relations) {
        auto it = c.relations.find(r.id);
        relsOk &= it != c.relations.end() && it->second.first == r.tags && it->second.second.size() == r.members.size();
        for (size_t i = 0; relsOk && i < r.members.size(); ++i) {
            const auto &m = it->second.second[i];
            relsOk &= m.ref == r.members[i].ref && m.type == r.members[i].type && m.role == r.members[i].role;
        }
    }
    check(relsOk, "relations keep tags and members with types and roles");

    bool boundsOk = true, orderOk = true;
    for (size_t i = 0; i < c.blocks.size(); ++i) {
        const auto &idx = c.blocks[i].first;
        if (c.blocks[i].second.valid()) boundsOk &= idx.bounds.valid() && idx.bounds.contains({c.blocks[i].second.minX, c.blocks[i].second.minY})
                                                    && idx.bounds.contains({c.blocks[i].second.maxX, c.blocks[i].second.maxY});
        if (i && idx.kind != ItemType::Relation && c.blocks[i - 1].first.kind != ItemType::Relation) orderOk &= c.blockBuckets[i] >= c.blockBuckets[i - 1] || idx.kind != c.blocks[i - 1].first.kind;
    }
    check(boundsOk, "block indexdata bounds contain every node and way of the block");
    check(orderOk, "blocks follow the placement bucket order");
    const auto &forestIndex = c.blocks[c.relationBlock.at(1)].first;
    const auto &forestOuter = c.ways.at(900).locations;
    bool forestOk = forestIndex.bounds.valid();
    for (const Location &l : forestOuter) forestOk &= forestIndex.bounds.contains(l);
    check(forestOk, "relation blocks are bounded by their members' geometry");
    const auto &siteIndex = c.blocks[c.relationBlock.at(2)].first;
    check(siteIndex.bounds.valid() && siteIndex.bounds.contains(forestOuter[0]) && siteIndex.bounds.contains(loc[1000 + 3 * 40 + 3]),
          "relation extents include node members and member relations");
    check(!c.blocks[c.relationBlock.at(3)].first.bounds.valid() && c.relationBlock.at(3) == c.relationBlock.at(4),
          "relations without resolvable members share an unbounded block");

    // Deterministic: other thread counts and an unsorted, shuffled source give identical data blocks.
    auto dataBlocks = [](const QString &path) {
        PbfFile f; QString e;
        std::vector<std::string> v;
        if (!f.open(path, e)) return v;
        const QByteArray all = readFile(path);
        for (size_t i = 1; i < f.blobs().size(); ++i) v.push_back(all.mid(f.blobs()[i].offset, f.blobs()[i].size).toStdString());
        return v;
    };
    const QString out2 = dir.filePath("single" + Sorted::ConvertedSuffix), out3 = dir.filePath("shuffled" + Sorted::ConvertedSuffix);
    options.threads = 1;
    check(convertPbf(sortedPath, out2, options, stats, error) && dataBlocks(out2) == dataBlocks(out1), "output does not depend on the thread count");
    options.threads = 3;
    check(convertPbf(unsortedPath, out3, options, stats, error) && dataBlocks(out3) == dataBlocks(out1),
          "an unsorted source with shuffled and mixed blocks converts to the same data");

    check(!convertPbf(out1, dir.filePath("again" + Sorted::ConvertedSuffix), options, stats, error) && error.contains("already converted"),
          "a converted file is not converted again");
    std::atomic_bool cancel{true};
    const QString out4 = dir.filePath("cancelled" + Sorted::ConvertedSuffix);
    check(!convertPbf(sortedPath, out4, options, stats, error, {}, &cancel) && error.contains("cancelled") && !QFile::exists(out4)
              && !QFile::exists(out4 + ".part") && !QDir(out4 + ".tmp").exists(),
          "cancelling leaves no output or temporary files");
    // Replacing an existing output keeps the old file until the new one is complete.
    check(convertPbf(sortedPath, out2, options, stats, error) && dataBlocks(out2) == dataBlocks(out1), "an existing output is replaced");
    const ConvertEstimate est = estimateConversion(1000), poland = estimateConversion(2104735352, 12);
    check(est.memoryBytes > 2000 && est.memoryBytes < 128ll * 1024 * 1024 && est.tempBytes >= 2800 && est.outputBytes >= 1100
              && poland.memoryBytes > 4700ll * 1024 * 1024 && poland.tempBytes > 6339ll * 1024 * 1024 && poland.outputBytes > 2281ll * 1024 * 1024,
          "estimates scale with the source size and cover the measured Poland conversion");
    // Measured peaks of province files (MB): Malopolska 1466 (1 GiB groups) and 980 (256 MiB),
    // Pomorskie 862 and 708.
    const int64_t mb = 1024 * 1024;
    check(estimateConversion(202329961, 12).memoryBytes >= 1466 * mb && estimateConversion(202329961, 12, 256 * mb).memoryBytes >= 980 * mb
              && estimateConversion(117000000, 12).memoryBytes >= 862 * mb && estimateConversion(117000000, 12, 256 * mb).memoryBytes >= 708 * mb
              && estimateConversion(202329961, 12).memoryBytes < 1700 * mb,
          "estimates cover the measured province conversions, by write group size");
    check(writeGroupBytesFor(0) == 1024 * mb && writeGroupBytesFor(8192 * mb) == 1024 * mb && writeGroupBytesFor(2048 * mb) == 512 * mb
              && writeGroupBytesFor(100 * mb) == 64 * mb,
          "write groups: a quarter of the available memory, 64 MiB to 1 GiB");
}

}


void storeTests(const std::function<void(bool, const char *)> &check) {
    QTemporaryDir dir;
    const Fixture fx = makeFixture();
    const QString src = dir.filePath("sample.osm.pbf"), out = dir.filePath("sample" + Sorted::ConvertedSuffix);
    ConvertOptions options; options.threads = 2;
    ConvertStats stats; QString error;
    check(writeFile(src, writeSource(fx, true, 100)) && convertPbf(src, out, options, stats, error), "store fixture converts");

    SortedPbfStore store;
    check(store.open(QStringList{out}, error) && store.fileCount() == 1, "store opens a converted file");
    check(!SortedPbfStore().open(QStringList{src}, error), "store refuses an unconverted file");

    // Expected answers from the fixture itself.
    std::map<int64_t, const FixtureNode *> nodeById;
    for (const auto &n : fx.nodes) nodeById[n.id] = &n;
    std::map<int64_t, std::vector<Location>> wayPoints;
    std::map<int64_t, Box> wayBox;
    for (const auto &w : fx.ways) {
        std::vector<Location> pts;
        for (int64_t r : w.refs) if (nodeById.count(r)) pts.push_back(nodeById[r]->location);
        if (pts.empty() || (w.tags.empty() && w.id != 900 && w.id != 905)) continue;
        wayPoints[w.id] = pts;
        Box b; for (const Location &l : pts) b.extend(l);
        wayBox[w.id] = b;
    }
    Box r1; r1.extend(wayBox[900]); r1.extend(wayBox[905]);
    Box r2 = r1; r2.extend(nodeById[1000 + 3 * 40 + 3]->location);
    const std::map<int64_t, Box> relBox{{1, r1}, {2, r2}};

    std::mt19937 rng(7);
    std::uniform_real_distribution<double> lon(16.8, 20.2), lat(53.3, 55.7), span(0.01, 1.2);
    bool allMatch = true;
    for (int q = 0; q < 60 && allMatch; ++q) {
        const double x = lon(rng), y = lat(rng), w = span(rng), h = span(rng) * 0.6;
        const Box area = q == 0 ? Box::fromDegrees(10, 40, 30, 60) : Box::fromDegrees(x, y, x + w, y + h);
        std::set<int64_t> nodes, ways, rels, expNodes, expWays, expRels;
        bool geometryOk = true;
        check(store.forEach(area, Filter(), [&](const Feature &f) {
            if (f.type == ItemType::Node) nodes.insert(f.id);
            else if (f.type == ItemType::Way) {
                ways.insert(f.id);
                const auto &pts = wayPoints[f.id];
                geometryOk &= f.refCount == pts.size();
                for (uint32_t i = 0; geometryOk && i < f.refCount; ++i) geometryOk &= f.locations[i] == pts[i];
            } else rels.insert(f.id);
        }, error), "query succeeds");
        for (const auto &n : fx.nodes) if (!n.tags.empty() && area.contains(n.location)) expNodes.insert(n.id);
        for (const auto &wb : wayBox) if (wb.second.intersects(area)) expWays.insert(wb.first);
        for (const auto &rb : relBox) if (rb.second.intersects(area)) expRels.insert(rb.first);
        allMatch = geometryOk && nodes == expNodes && ways == expWays && rels == expRels;
    }
    check(allMatch, "60 random area queries return exactly the features a brute-force scan finds, with their geometry");

    Filter rails; rails.types = Ways; rails.keys = {"railway"};
    std::vector<int64_t> found;
    store.forEach(Box::fromDegrees(10, 40, 30, 60), rails, [&](const Feature &f) { found.push_back(f.id); }, error);
    check(found == std::vector<int64_t>({902}), "type and key filters");
    Filter byId; byId.ids = {11, 902, 1008};
    std::set<int64_t> ids;
    store.forEach(Box::fromDegrees(10, 40, 30, 60), byId, [&](const Feature &f) { ids.insert(f.id); }, error);
    check(ids == std::set<int64_t>({11, 902, 1008}), "id filter");
    Box extent;
    Filter rel1; rel1.types = Relations; rel1.ids = {1};
    std::string landuse;
    store.forEach(Box::fromDegrees(10, 40, 30, 60), rel1, [&](const Feature &f) { extent = f.extent; landuse = std::string(f.value("landuse")); }, error);
    check(extent == r1 && landuse == "forest", "relations carry their member extent and tags");

    store.forEach(Box::fromDegrees(10, 40, 30, 60), Filter(), [](const Feature &) {}, error);
    const auto cold = store.lastQuery();
    store.forEach(Box::fromDegrees(10, 40, 30, 60), Filter(), [](const Feature &) {}, error);
    const auto warm = store.lastQuery();
    check(warm.blocksRead == 0 && warm.blocksCached == cold.blocksRead + cold.blocksCached, "repeated queries are served from the block cache");
    store.setCacheBudget(4096);
    store.forEach(Box::fromDegrees(10, 40, 30, 60), Filter(), [](const Feature &) {}, error);
    check(store.cacheBytes() <= 4096, "the block cache respects its budget");

    // Two overlapping conversions: each feature is reported once, from the newer file.
    Fixture newer = fx;
    for (auto &n : newer.nodes) if (n.id == 1008) n.tags = {{"amenity", "bench"}, {"name", "renamed"}};
    const QString src2 = dir.filePath("newer.osm.pbf"), out2 = dir.filePath("newer" + Sorted::ConvertedSuffix);
    check(writeFile(src2, writeSource(newer, true, 200)) && convertPbf(src2, out2, options, stats, error), "second fixture converts");
    SortedPbfStore both;
    check(both.open(QStringList{out, out2}, error) && both.fileCount() == 2, "store opens overlapping files");
    std::map<int64_t, int> seen;
    std::string name;
    both.forEach(Box::fromDegrees(10, 40, 30, 60), Filter(), [&](const Feature &f) {
        ++seen[int64_t(f.type) << 40 | f.id];
        if (f.type == ItemType::Node && f.id == 1008) name = std::string(f.value("name"));
    }, error);
    bool once = !seen.empty();
    for (const auto &p : seen) once &= p.second == 1;
    check(once && name == "renamed", "overlapping files report each feature once, from the newest file");
}

void runConverterTests(const std::function<void(bool, const char *)> &check) { converterTests(check); storeTests(check); }

// Opt-in local data: --convert <source> <output> [threads] [zlib level]
int convertFile(const QStringList &args) {
    ConvertOptions options;
    if (args.size() > 2) options.threads = args[2].toInt();
    if (args.size() > 3) options.compressionLevel = args[3].toInt();
    if (args.size() > 4) options.writeGroupBytes = args[4].toLongLong() * 1024 * 1024;
    ConvertStats s;
    QString error;
    const char *names[] = {"scan", "relations", "nodes", "ways", "write"};
    int lastPhase = -1;
    const bool ok = convertPbf(args[0], args[1], options, s, error, [&](ConvertPhase p, double) {
        if (int(p) != lastPhase) { lastPhase = int(p); std::cout << "  phase " << names[lastPhase] << '\n' << std::flush; }
    });
    if (!ok) { std::cerr << error.toStdString() << '\n'; return 1; }
    std::cout << "converted in " << s.totalSeconds << " s: scan " << s.scanSeconds << ", relations " << s.relationSeconds << ", nodes " << s.nodeSeconds
              << ", ways " << s.waySeconds << ", write " << s.writeSeconds << "\n  nodes " << s.sourceNodes << " (tagged " << s.taggedNodes << "), ways "
              << s.sourceWays << " (kept " << s.keptWays << ", dropped " << s.droppedWays << "), relations " << s.relations << " (unplaced "
              << s.unplacedRelations << "), way refs " << s.wayRefs << " (missing " << s.missingRefs << "), tags " << s.tags << "\n  node table "
              << s.nodeTableBytes / 1048576 << " MiB, temp " << s.tempBytes / 1048576 << " MiB, output " << s.outputBytes / 1048576 << " MiB in "
              << s.outputBlocks << " blocks\n";
    return 0;
}

// Opt-in local data: --verify <source> <converted>. Every way location must equal the source node;
// tag, relation and member totals must match.
int verifyFile(const QStringList &args) {
    PbfFile src, conv;
    QString e;
    if (!src.open(args[0], e) || !conv.open(args[1], e)) { std::cerr << e.toStdString() << '\n'; return 1; }
    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    auto forEach = [&](const PbfFile &f, uint32_t parts, const std::function<void(const PrimitiveBlock &, size_t)> &fn) {
        std::atomic<size_t> next{1};
        std::vector<std::thread> ts;
        for (unsigned t = 0; t < threads; ++t)
            ts.emplace_back([&] {
                PrimitiveBlock b; QString err;
                for (size_t i; (i = next++) < f.blobs().size();)
                    if (f.blobs()[i].type == BlobType::Data && f.readBlock(i, b, parts, err)) fn(b, i);
            });
        for (auto &t : ts) t.join();
    };
    std::vector<std::vector<std::pair<int64_t, Location>>> perBlock(src.blobs().size());
    std::atomic<uint64_t> srcTags{0}, srcRelations{0}, srcMembers{0};
    forEach(src, DecodeAll, [&](const PrimitiveBlock &b, size_t i) {
        for (const auto &n : b.nodes) perBlock[i].push_back({n.id, n.location});
        srcTags += b.tagPairs.size() / 2; srcRelations += b.relations.size(); srcMembers += b.members.size();
    });
    std::vector<std::pair<int64_t, Location>> nodes;
    for (auto &v : perBlock) { nodes.insert(nodes.end(), v.begin(), v.end()); std::vector<std::pair<int64_t, Location>>().swap(v); }
    std::sort(nodes.begin(), nodes.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::atomic<uint64_t> refs{0}, mismatched{0}, convTags{0}, convRelations{0}, convMembers{0};
    forEach(conv, DecodeAll, [&](const PrimitiveBlock &b, size_t) {
        convTags += b.tagPairs.size() / 2; convRelations += b.relations.size(); convMembers += b.members.size();
        uint64_t bad = 0;
        for (size_t r = 0; r < b.refs.size(); ++r) {
            auto it = std::lower_bound(nodes.begin(), nodes.end(), b.refs[r], [](const auto &p, int64_t id) { return p.first < id; });
            if (!b.wayLocations || it == nodes.end() || it->first != b.refs[r] || it->second != b.refLocations[r]) ++bad;
        }
        refs += b.refs.size(); mismatched += bad;
    });
    std::cout << "verify: way refs " << refs << ", mismatched " << mismatched << ", tags " << convTags << "/" << srcTags << ", relations " << convRelations
              << "/" << srcRelations << ", members " << convMembers << "/" << srcMembers << '\n';
    return mismatched == 0 && convTags == srcTags && convRelations == srcRelations && convMembers == srcMembers ? 0 : 1;
}

// Opt-in local data: --query <minLon> <minLat> <maxLon> <maxLat> <converted files...>
int queryFiles(const QStringList &args) {
    const Box area = Box::fromDegrees(args[0].toDouble(), args[1].toDouble(), args[2].toDouble(), args[3].toDouble());
    auto t0 = std::chrono::steady_clock::now();
    SortedPbfStore store;
    QString error;
    if (!store.open(args.mid(4), error)) { std::cerr << error.toStdString() << '\n'; return 1; }
    const double openSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    for (int pass = 0; pass < 2; ++pass) {
        uint64_t counts[3] = {0, 0, 0}, points = 0;
        t0 = std::chrono::steady_clock::now();
        if (!store.forEach(area, Filter(), [&](const Feature &f) { ++counts[int(f.type)]; points += f.refCount; }, error)) { std::cerr << error.toStdString() << '\n'; return 1; }
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const auto st = store.lastQuery();
        std::cout << (pass ? "warm" : "cold") << ": " << s << " s, blocks read " << st.blocksRead << ", cached " << st.blocksCached << ", nodes " << counts[0]
                  << ", ways " << counts[1] << " (" << points << " points), relations " << counts[2] << '\n';
    }
    std::cout << "open " << openSeconds << " s, cache " << store.cacheBytes() / 1048576 << " MiB\n";
    return 0;
}
