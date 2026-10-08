#include <tsre/geo/osm/OsmPbf.h>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

using namespace Osm;

void runConverterTests(const std::function<void(bool, const char *)> &check);
void runDirectoryTests(const std::function<void(bool, const char *)> &check);
int convertFile(const QStringList &args);
int verifyFile(const QStringList &args);
int scanDirectory(const QString &path);

namespace {

std::string block(BlockBuilder &b) { std::string s; b.build(s); return s; }

PrimitiveBlock decode(const std::string &raw, bool &ok, uint32_t parts = DecodeAll) {
    PrimitiveBlock p;
    p.raw.assign(raw.begin(), raw.end());
    QString error;
    ok = decodePrimitiveBlock(p, parts, error);
    return p;
}

bool writeFile(const QString &path, const std::string &bytes) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes.data(), qint64(bytes.size())) == qint64(bytes.size());
}

HeaderInfo sampleHeader() {
    HeaderInfo h;
    h.bbox = Box::fromDegrees(16.68672, 53.4806, 19.66069, 55.08543);
    h.requiredFeatures << "OsmSchema-V0.6" << "DenseNodes";
    h.optionalFeatures << "LocationsOnWays" << "TSRE-Spatial-1";
    h.writingProgram = "tsre-test";
    h.source = QString::fromUtf8("pomorskie-261006.osm.pbf;118 MB;źródło");
    h.replicationTimestamp = 1791318066;
    h.replicationSequence = 4321;
    h.replicationBaseUrl = "https://download.geofabrik.de/europe/poland/pomorskie-updates";
    return h;
}

void protoTests(const std::function<void(bool, const char *)> &check) {
    using namespace Proto;
    std::string out;
    Writer w(out);
    const uint64_t values[] = {0, 1, 127, 128, 300, 1ull << 32, uint64_t(INT64_MAX), UINT64_MAX};
    const int64_t signedValues[] = {0, -1, 1, INT64_MIN, INT64_MAX, -300};
    for (uint64_t v : values) w.varint(1, v);
    for (int64_t v : signedValues) w.svarint(2, v);
    w.bytes(3, std::string_view("tsre"));
    w.key(4, Wire::Fixed32); out.append("abcd", 4);
    w.key(5, Wire::Fixed64); out.append("abcdefgh", 8);
    w.varint(6, 9);
    Reader r(reinterpret_cast<const uint8_t *>(out.data()), out.size());
    size_t vi = 0, si = 0; bool ok = true; std::string_view text; uint64_t last = 0;
    while (r.next()) {
        switch (r.field()) {
            case 1: ok &= r.varint() == values[vi++]; break;
            case 2: ok &= r.svarint() == signedValues[si++]; break;
            case 3: text = r.bytes().view(); break;
            case 6: last = r.varint(); break;
            default: r.skip();
        }
    }
    check(r.ok() && ok && vi == 8 && si == 6 && text == "tsre" && last == 9, "protobuf varint, zigzag, bytes and skipped fixed fields round trip");

    const std::string truncated = out.substr(0, 3);
    Reader t(reinterpret_cast<const uint8_t *>(truncated.data()), truncated.size());
    while (t.next()) t.skip();
    std::string badLength; Writer bl(badLength); bl.key(1, Wire::Length); Writer::putVarint(badLength, 1000);
    Reader l(reinterpret_cast<const uint8_t *>(badLength.data()), badLength.size());
    while (l.next()) l.bytes();
    const uint8_t badWire[] = {0x0b};  // field 1, wire type 3 (groups are not supported)
    Reader g(badWire, 1);
    check(!t.ok() && !l.ok() && !g.next() && !g.ok(), "protobuf reader rejects truncated varints, overlong lengths and group wire types");

    std::string packed; Writer pw(packed);
    const std::vector<int64_t> ids{5, 3, 1000000000000ll, -7, INT64_MAX, INT64_MIN};
    pw.packedDelta(1, ids.begin(), ids.end());
    Reader pr(reinterpret_cast<const uint8_t *>(packed.data()), packed.size());
    std::vector<int64_t> back;
    uint64_t sum = 0;
    bool packedOk = pr.next() && forEachSVarint(pr.bytes(), [&](int64_t d) { sum += uint64_t(d); back.push_back(int64_t(sum)); });
    check(packedOk && back == ids, "packed delta coding round trips extreme ids");
}

void blockTests(const std::function<void(bool, const char *)> &check) {
    BlockBuilder b;
    const Tag cafe[] = {{"amenity", "cafe"}, {"name", "Kawiarnia Żuraw"}};
    const Tag none[] = {{"", ""}};
    b.addNode(10, Location::fromDegrees(18.6466, 54.3520), cafe, 2);
    b.addNode(11, Location::fromDegrees(-0.1, -33.9), none, 0);
    b.addNode(9, Location::fromDegrees(179.9999999, 89.9), cafe, 1);
    const Tag rail[] = {{"railway", "rail"}, {"usage", "main"}, {"name", "Tczew–Gdańsk"}};
    const int64_t refs[] = {10, 11, 9, 10};
    const Location locs[] = {Location::fromDegrees(18.6466, 54.3520), Location::fromDegrees(-0.1, -33.9),
                             Location::fromDegrees(179.9999999, 89.9), Location::fromDegrees(18.6466, 54.3520)};
    b.addWay(100, rail, 3, refs, locs, 4);
    b.addWay(101, cafe, 0, refs + 1, locs + 1, 2);
    const BlockBuilder::MemberIn members[] = {{100, ItemType::Way, "outer"}, {10, ItemType::Node, ""}, {7, ItemType::Relation, "subarea"}};
    const Tag mp[] = {{"type", "multipolygon"}, {"landuse", "forest"}};
    b.addRelation(1000, mp, 2, members, 3);
    const Box expected = [&] { Box x; for (const Location &l : locs) x.extend(l); return x; }();
    check(b.count() == 6 && b.bounds() == expected, "block builder counts entities and tracks bounds");
    bool ok = false;
    const std::string raw = block(b);
    PrimitiveBlock p = decode(raw, ok);
    check(ok, "decode a block written by the builder");
    check(p.nodes.size() == 3 && p.nodes[0].id == 10 && p.nodes[1].id == 11 && p.nodes[2].id == 9
              && p.nodes[0].location == Location::fromDegrees(18.6466, 54.3520) && p.nodes[1].location == Location::fromDegrees(-0.1, -33.9)
              && p.nodes[2].location == Location::fromDegrees(179.9999999, 89.9),
          "dense node ids and coordinates round trip, including negative and unsorted values");
    check(p.nodes[0].tagCount == 2 && p.tag(p.nodes[0].tagFirst).key == "amenity" && p.tag(p.nodes[0].tagFirst + 1).value == "Kawiarnia Żuraw"
              && p.nodes[1].tagCount == 0 && p.nodes[2].tagCount == 1 && p.tag(p.nodes[2].tagFirst).value == "cafe",
          "dense node tags keep per-node grouping and UTF-8 values");
    bool waysOk = p.ways.size() == 2 && p.wayLocations && p.ways[0].id == 100 && p.ways[0].refCount == 4 && p.ways[0].tagCount == 3
                  && p.tag(p.ways[0].tagFirst + 2).value == "Tczew–Gdańsk" && p.ways[1].tagCount == 0 && p.ways[1].refCount == 2;
    for (uint32_t i = 0; waysOk && i < 4; ++i) waysOk = p.refs[p.ways[0].refFirst + i] == refs[i] && p.refLocations[p.ways[0].refFirst + i] == locs[i];
    waysOk = waysOk && p.refs[p.ways[1].refFirst] == 11 && p.refLocations[p.ways[1].refFirst + 1] == locs[2];
    check(waysOk, "way refs, tags and LocationsOnWays coordinates round trip");
    const auto &rel = p.relations.size() == 1 ? p.relations[0] : PrimitiveBlock::Relation{};
    bool relOk = rel.id == 1000 && rel.memberCount == 3 && rel.tagCount == 2;
    for (uint32_t i = 0; relOk && i < 3; ++i) {
        const Member &m = p.members[rel.memberFirst + i];
        relOk = m.ref == members[i].ref && m.type == members[i].type && p.role(m) == members[i].role;
    }
    check(relOk, "relation members keep id, type and role");

    PrimitiveBlock waysOnly = decode(raw, ok, DecodeWays);
    check(ok && waysOnly.nodes.empty() && waysOnly.ways.size() == 2 && waysOnly.relations.empty(), "parts not requested are skipped");

    BlockBuilder plain;
    plain.addWay(5, rail, 1, refs, nullptr, 3);
    PrimitiveBlock pw = decode(block(plain), ok);
    check(ok && pw.ways.size() == 1 && !pw.wayLocations && pw.refLocations.empty() && pw.refs.size() == 3, "ways without locations decode as plain refs");
    check(blockParts(waysOnly.raw) == DecodeAll && blockParts(pw.raw) == DecodeWays, "blockParts reports every kind present");

    // A block mixing ways with and without locations keeps refLocations parallel to refs.
    BlockBuilder mixed;
    mixed.addWay(1, rail, 1, refs, nullptr, 2);
    mixed.addWay(2, rail, 1, refs, locs, 3);
    PrimitiveBlock pm = decode(block(mixed), ok);
    check(ok && pm.wayLocations && pm.refLocations.size() == pm.refs.size() && !pm.refLocations[0].valid() && pm.refLocations[3] == locs[1],
          "mixed blocks keep locations parallel to refs");

    // String index outside the table is rejected.
    std::string badRaw;
    {
        Proto::Writer w(badRaw);
        std::string table; Proto::Writer(table).bytes(1, std::string_view());
        w.bytes(1, table);
        std::string way, group;
        Proto::Writer wm(way); wm.varint(1, 1); const uint32_t k[] = {5}; wm.packedVarint(2, k, k + 1); wm.packedVarint(3, k, k + 1);
        Proto::Writer(group).bytes(3, way);
        w.bytes(2, group);
    }
    decode(badRaw, ok);
    check(!ok, "tags referring outside the string table are rejected");
    std::string badDense;
    {
        Proto::Writer w(badDense);
        std::string table; Proto::Writer(table).bytes(1, std::string_view());
        w.bytes(1, table);
        std::string dense, group;
        Proto::Writer d(dense); const int64_t ids[] = {1, 2}, lat[] = {3}; d.packedDelta(1, ids, ids + 2); d.packedDelta(8, lat, lat + 1); d.packedDelta(9, ids, ids + 2);
        Proto::Writer(group).bytes(2, dense);
        w.bytes(2, group);
    }
    decode(badDense, ok);
    check(!ok, "dense nodes with mismatched array lengths are rejected");
}

void fileTests(const std::function<void(bool, const char *)> &check) {
    QTemporaryDir dir;
    check(dir.isValid(), "temporary directory");
    const HeaderInfo header = sampleHeader();
    BlockBuilder b;
    const Tag t[] = {{"highway", "residential"}};
    const int64_t refs[] = {1, 2};
    const Location locs[] = {Location::fromDegrees(18.1, 54.1), Location::fromDegrees(18.2, 54.2)};
    b.addWay(77, t, 1, refs, locs, 2);
    std::string raw; b.build(raw);
    const std::string index("\x01\x02" "bbox-sixteen-byt", 18);
    std::string bytes = encodeBlobFrame("OSMHeader", encodeHeaderBlock(header), {}, 6);
    bytes += encodeBlobFrame("OSMData", raw, index, 6);
    bytes += encodeBlobFrame("TSREUnknown", "ignored", {}, 6);
    bytes += encodeBlobFrame("OSMData", raw, {}, -1);
    const QString path = dir.filePath("sample.osm.pbf");
    check(writeFile(path, bytes), "write sample PBF");
    PbfFile f;
    QString error;
    check(f.open(path, error), "open a PBF written by encodeBlobFrame");
    const HeaderInfo &h = f.header();
    check(h.bbox == header.bbox && h.requiredFeatures == header.requiredFeatures && h.optionalFeatures == header.optionalFeatures
              && h.writingProgram == header.writingProgram && h.source == header.source && h.replicationTimestamp == header.replicationTimestamp
              && h.replicationSequence == header.replicationSequence && h.replicationBaseUrl == header.replicationBaseUrl && h.hasFeature("TSRE-Spatial-1"),
          "header block fields round trip");
    check(f.blobs().size() == 4 && f.blobs()[1].type == BlobType::Data && f.blobs()[1].indexData == index
              && f.blobs()[2].type == BlobType::Unknown && f.blobs()[3].indexData.empty() && f.size() == qint64(bytes.size()),
          "block table lists frames, unknown types and indexdata");
    PrimitiveBlock p;
    const bool zlibOk = f.readBlock(1, p, DecodeAll, error) && p.ways.size() == 1 && p.ways[0].id == 77 && p.refLocations[1] == locs[1];
    const bool rawOk = f.readBlock(3, p, DecodeAll, error) && p.ways.size() == 1 && p.tag(p.ways[0].tagFirst).value == "residential";
    check(zlibOk && rawOk, "zlib and uncompressed blobs decode");
    f.close();

    const QString truncatedPath = dir.filePath("truncated.osm.pbf");
    writeFile(truncatedPath, bytes.substr(0, bytes.size() - 5));
    check(!f.open(truncatedPath, error) && !f.isOpen(), "truncated file is rejected");
    std::string noHeader = encodeBlobFrame("OSMData", raw, {}, 6);
    writeFile(dir.filePath("noheader.osm.pbf"), noHeader);
    check(!f.open(dir.filePath("noheader.osm.pbf"), error), "file without a leading header block is rejected");
    HeaderInfo future = header; future.requiredFeatures << "Sort.FutureFeature";
    writeFile(dir.filePath("future.osm.pbf"), encodeBlobFrame("OSMHeader", encodeHeaderBlock(future), {}, 6));
    check(!f.open(dir.filePath("future.osm.pbf"), error) && error.contains("Sort.FutureFeature"), "unknown required feature is rejected");
    std::string huge("\x00\x01\x00\x01", 4);  // header length 65537
    writeFile(dir.filePath("huge.osm.pbf"), huge + std::string(70000, '\0'));
    check(!f.open(dir.filePath("huge.osm.pbf"), error), "blob header over 64 KiB is rejected");

    // Blob with LZMA data only (field 4) is reported as unsupported.
    std::string lzmaBlob; { Proto::Writer w(lzmaBlob); w.varint(2, 10); w.bytes(4, std::string_view("xxxx")); }
    std::vector<uint8_t> out;
    check(!inflateBlob(Proto::Span(reinterpret_cast<const uint8_t *>(lzmaBlob.data()), lzmaBlob.size()), out, error)
              && error.contains("Unsupported"),
          "non-zlib compression is rejected");
    std::string bigBlob; { Proto::Writer w(bigBlob); w.varint(2, MaxBlobSize + 1); w.bytes(3, std::string_view("xxxx")); }
    check(!inflateBlob(Proto::Span(reinterpret_cast<const uint8_t *>(bigBlob.data()), bigBlob.size()), out, error),
          "declared raw size over 32 MiB is rejected");
    check(encodeBlobFrame("OSMData", std::string(MaxBlobSize + 1, 'x'), {}, 1).empty(), "writer refuses blocks over 32 MiB");
}

// Opt-in: --count <file.osm.pbf> [nodes ways relations tags]
int countFile(const QString &path, const QStringList &expect) {
    const auto start = std::chrono::steady_clock::now();
    PbfFile f;
    QString error;
    if (!f.open(path, error)) { std::cerr << error.toStdString() << '\n'; return 1; }
    const double openSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::atomic<size_t> next{1};
    std::atomic<uint64_t> nodes{0}, ways{0}, relations{0}, tags{0}, refs{0}, members{0}, locations{0};
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads;
    const unsigned n = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned t = 0; t < n; ++t)
        threads.emplace_back([&] {
            PrimitiveBlock b;
            QString e;
            for (size_t i; (i = next++) < f.blobs().size();) {
                if (f.blobs()[i].type != BlobType::Data) continue;
                if (!f.readBlock(i, b, DecodeAll, e)) { std::cerr << e.toStdString() << '\n'; failed = true; return; }
                nodes += b.nodes.size(); ways += b.ways.size(); relations += b.relations.size();
                tags += b.tagPairs.size() / 2; refs += b.refs.size(); members += b.members.size();
                if (b.wayLocations) for (const Location &l : b.refLocations) locations += l.valid();
            }
        });
    for (auto &t : threads) t.join();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << path.toStdString() << ": open " << openSeconds << " s, total " << seconds << " s (" << n << " threads), "
              << f.blobs().size() << " blobs, nodes " << nodes << ", ways " << ways << ", relations " << relations << ", tags " << tags
              << ", way refs " << refs << ", members " << members << ", way locations " << locations << '\n';
    if (failed) return 1;
    if (expect.size() == 4) {
        const bool match = expect[0].toULongLong() == nodes && expect[1].toULongLong() == ways
                           && expect[2].toULongLong() == relations && expect[3].toULongLong() == tags;
        std::cout << (match ? "counts match\n" : "COUNTS DIFFER\n");
        return match ? 0 : 1;
    }
    return 0;
}

}

int main(int argc, char **argv) {
    QStringList args;
    for (int i = 1; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
    if (args.size() >= 2 && args[0] == "--count") return countFile(args[1], args.mid(2));
    if (args.size() >= 3 && args[0] == "--convert") return convertFile(args.mid(1));
    if (args.size() == 3 && args[0] == "--verify") return verifyFile(args.mid(1));
    if (args.size() == 2 && args[0] == "--scan") return scanDirectory(args[1]);
    int checks = 0, failures = 0;
    const auto check = [&](bool condition, const char *name) {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
    };
    protoTests(check);
    blockTests(check);
    fileTests(check);
    runConverterTests(check);
    runDirectoryTests(check);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
