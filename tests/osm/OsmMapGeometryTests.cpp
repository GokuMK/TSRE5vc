#include <tsre/geo/osm/OsmConverter.h>
#include <tsre/geo/osm/OsmMapGeometry.h>
#include <tsre/geo/osm/SortedPbfStore.h>
#include <QFile>
#include <QTemporaryDir>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>

using namespace Osm;

namespace {

// A local metre grid around (18.6 E, 54.3 N) and its inverse, the projection of the tests.
constexpr double Lon0 = 18.6, Lat0 = 54.3, Kx = 111320.0 * 0.58354, Ky = 110574.0;  // cos(54.3) = 0.58354
Location at(double xm, double ym) { return Location::fromDegrees(Lon0 + xm / Kx, Lat0 + ym / Ky); }
void project(const Location *in, size_t count, float *xz) {
    for (size_t i = 0; i < count; ++i) {
        xz[2 * i] = float((in[i].lon() - Lon0) * Kx);
        xz[2 * i + 1] = float(-(in[i].lat() - Lat0) * Ky);  // z points south
    }
}

// Cross product in x, z of a triangle; the map's front faces are negative.
float cross(const float *a, const float *b, const float *c) {
    return (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0]);
}

// Every non-degenerate triangle of a batch faces the map's eye; area sums the triangles.
bool frontFacing(const std::vector<float> &v, MapBatch::Primitive primitive, double *area = nullptr) {
    const size_t n = v.size() / 3;
    double sum = 0;
    for (size_t i = 0; i + 2 < n; i += primitive == MapBatch::Triangles ? 3 : 1) {
        const float *a = &v[3 * i], *b = &v[3 * (i + 1)], *c = &v[3 * (i + 2)];
        if (primitive == MapBatch::TriangleStrip && i % 2) std::swap(a, b);
        const float x = cross(a, b, c);
        if (std::fabs(x) < 1e-3f) continue;
        if (x > 0) return false;
        sum -= 0.5 * x;
    }
    if (area) *area = sum;
    return true;
}

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

size_t vertexCount(const std::vector<MapBatch> &batches, MapBatch::Primitive primitive) {
    size_t n = 0;
    for (const MapBatch &b : batches) if (b.primitive == primitive) n += b.vertices.size() / 3;
    return n;
}

const MapBatch *find(const std::vector<MapBatch> &batches, Rgb color, MapBatch::Primitive primitive) {
    for (const MapBatch &b : batches) if (b.color == color && b.primitive == primitive) return &b;
    return nullptr;
}

}

void runMapGeometryTests(const std::function<void(bool, const char *)> &check) {
    // Lines and strips.
    const float path[] = {0, 0, 10, 0, 10, 10};
    std::vector<float> lines;
    MapGeometry::appendLines(lines, path, 3, 5);
    check(lines.size() == 12 && lines[1] == 5 && lines[9] == 10 && lines[11] == 10, "one-pixel lines: two vertices a segment");

    std::vector<float> strip;
    MapGeometry::appendStrip(strip, path, 2, false, 2, 0);
    check(strip.size() == 12 && strip[0] == 0 && strip[2] == -1 && strip[5] == 1 && frontFacing(strip, MapBatch::TriangleStrip),
          "a straight strip: two vertices a point, its width across, facing the eye");
    strip.clear();
    MapGeometry::appendStrip(strip, path, 3, false, 2, 0);
    check(strip.size() == 18 && std::fabs(strip[6] - 11) < 1e-4f && std::fabs(strip[8] + 1) < 1e-4f && std::fabs(strip[9] - 9) < 1e-4f
              && std::fabs(strip[11] - 1) < 1e-4f && frontFacing(strip, MapBatch::TriangleStrip),
          "a right-angle join is mitred: one pair, outer corner at (11, -1)");
    const float hairpin[] = {0, 0, 10, 0, 0, 1};
    strip.clear();
    MapGeometry::appendStrip(strip, hairpin, 3, false, 2, 0);
    check(strip.size() == 24, "a join sharper than the mitre limit gets two pairs");
    const float ring[] = {0, 0, 10, 0, 10, 10, 0, 10, 0, 0};
    strip.clear();
    MapGeometry::appendStrip(strip, ring, 5, true, 2, 0);
    check(strip.size() == 30 && std::equal(strip.begin(), strip.begin() + 6, strip.end() - 6) && frontFacing(strip, MapBatch::TriangleStrip),
          "a closed ring: mitred at every corner, ending where it began");
    std::vector<float> two;
    MapGeometry::appendStrip(two, path, 2, false, 2, 0);
    const float other[] = {0, 20, 10, 20};
    MapGeometry::appendStrip(two, other, 2, false, 2, 0);
    check(two.size() == 30 && (two.size() / 3) % 2 == 0 && frontFacing(two, MapBatch::TriangleStrip),
          "strips join with degenerate triangles and keep their winding");
    const float repeated[] = {0, 0, 0, 0, 5, 0, 5, 0, 10, 0};
    strip.clear();
    MapGeometry::appendStrip(strip, repeated, 5, false, 2, 0);
    check(strip.size() == 18, "repeated points are dropped");

    // Fills.
    std::vector<MapRing> rings{{{0, 0}, {0, 10}, {10, 10}, {10, 0}}, {{4, 4}, {6, 4}, {6, 6}, {4, 6}}};
    std::vector<float> fill;
    double area = 0;
    const size_t triangles = MapGeometry::appendFill(fill, rings, 3);
    check(triangles == 8 && fill.size() == 72 && fill[1] == 3 && frontFacing(fill, MapBatch::Triangles, &area) && std::fabs(area - 96) < 1e-3,
          "a square with a hole: eight triangles facing the eye, the hole left out");

    // A converted fixture.
    QTemporaryDir dir;
    Fixture fx;
    fx.way(1, {{"highway", "residential"}}, fx.line({{0, 0}, {500, 0}, {500, 500}}));
    fx.way(2, {{"building", "yes"}}, fx.square(100, 100, 20));
    fx.way(3, {{"highway", "primary"}, {"bridge", "yes"}}, fx.line({{0, 1000}, {1000, 1000}}));
    const auto outer = fx.square(2000, 0, 1000), inner = fx.square(2400, 400, 200);
    fx.way(4, {}, outer);
    fx.way(5, {}, inner);
    const Tag forest[] = {{"type", "multipolygon"}, {"landuse", "forest"}};
    const BlockBuilder::MemberIn members[] = {{4, ItemType::Way, "outer"}, {5, ItemType::Way, "inner"}};
    fx.rels.addRelation(100, forest, 2, members, 2);
    fx.way(6, {{"barrier", "fence"}}, fx.line({{0, 2000}, {100, 2000}}));
    fx.way(7, {{"building", "shed"}}, fx.square(200, 200, 3));
    std::vector<std::pair<double, double>> jitter;
    for (int i = 0; i <= 100; ++i) jitter.push_back({i * 10.0, 3000 + (i % 2) * 1.0});
    fx.way(8, {{"highway", "primary"}}, fx.line(jitter));
    HeaderInfo h;
    h.requiredFeatures << "OsmSchema-V0.6" << "DenseNodes";
    std::string a, b, c;
    fx.nodes.build(a); fx.ways.build(b); fx.rels.build(c);
    const std::string bytes = encodeBlobFrame("OSMHeader", encodeHeaderBlock(h), {}, 6) + encodeBlobFrame("OSMData", a, {}, 6)
                              + encodeBlobFrame("OSMData", b, {}, 6) + encodeBlobFrame("OSMData", c, {}, 6);
    QFile file(dir.filePath("map.osm.pbf"));
    const bool written = file.open(QIODevice::WriteOnly) && file.write(bytes.data(), qint64(bytes.size())) == qint64(bytes.size());
    file.close();
    ConvertOptions options; options.threads = 2;
    ConvertStats stats;
    QString error;
    SortedPbfStore store;
    check(written && convertPbf(dir.filePath("map.osm.pbf"), dir.filePath("map.tsre.osm.pbf"), options, stats, error)
              && store.open(QStringList{dir.filePath("map.tsre.osm.pbf")}, error),
          "write and convert the map fixture");

    const FeatureClasses &classes = FeatureClasses::standard();
    MapGeometry geometry;
    MapGeometry::Options heights;
    heights.baseHeight = 50;
    heights.heightStep = 0.5f;
    const Box all = Box::fromDegrees(Lon0 - 0.1, Lat0 - 0.1, Lon0 + 0.1, Lat0 + 0.1);
    const bool loaded = geometry.load(store, all, 1.0, project, heights, error);
    const Rgb buildingFill = 0xffbeadad, forestFill = classes.style(classes.classify(2, [&](uint32_t i) { return forest[i]; })).fill;
    const MapBatch *house = find(geometry.fills(), buildingFill, MapBatch::Triangles), *wood = find(geometry.fills(), forestFill, MapBatch::Triangles);
    double houseArea = 0, woodArea = 0;
    check(loaded && geometry.stats().polygons == 3 && geometry.stats().triangles == 12 && geometry.stats().culled == 0 && house && wood
              && frontFacing(house->vertices, MapBatch::Triangles, &houseArea) && std::fabs(houseArea - 409) < 2
              && frontFacing(wood->vertices, MapBatch::Triangles, &woodArea) && std::fabs(woodArea - 960000) < 2000,
          "load at 1 m/px: the buildings and the forest with its clearing, projected and filled");
    check(geometry.stats().points > 101 + 3, "at 1 m/px a 1 m zigzag keeps its points (tolerance 0.5 m)");
    std::vector<MapBatch> strokes;
    geometry.strokes(1.0, strokes);
    const Style &residential = classes.style(classes.classify(1, [](uint32_t) { return Tag{"highway", "residential"}; }));
    const MapBatch *casing = find(strokes, residential.casings[0].color, MapBatch::TriangleStrip), *road = find(strokes, residential.line.color, MapBatch::TriangleStrip);
    check(casing && road && road->order > casing->order && road->vertices.size() >= 18 && find(strokes, 0xffa994a5, MapBatch::Lines)
              && frontFacing(road->vertices, MapBatch::TriangleStrip),
          "strokes at 1 m/px: road casing and line as strips, the line over its casing; building outlines as lines");
    float bridgeOrder = 0, maxOrder = 0;
    for (const MapBatch &batch : strokes) maxOrder = std::max(maxOrder, batch.order);
    for (const MapBatch &batch : strokes) if (batch.color == 0xff000000) bridgeOrder = batch.order;
    check(bridgeOrder >= 9 * MapGeometry::OrdersPerSlot && maxOrder < 10 * MapGeometry::OrdersPerSlot
              && std::fabs(house->vertices[1] - (50 + house->order * 0.5f)) < 1e-4f,
          "bridges draw in the top slot; heights follow the order");
    geometry.strokes(5.0, strokes);
    check(find(strokes, residential.casings[0].color, MapBatch::Lines) && !find(strokes, residential.casings[0].color, MapBatch::TriangleStrip),
          "at 5 m/px the 6 m casing is a one-pixel line");

    MapGeometry atTwo;
    check(atTwo.load(store, all, 2.0, project, heights, error) && atTwo.stats().polygons == 2 && atTwo.stats().culled == 1,
          "at 2 m/px the 3 m shed, under 2 pixels across, is left out");
    check(geometry.sameStyles(2.0) && !geometry.sameStyles(3.0) && geometry.sameStyles(0.5), "styles change past a scale range only");
    const bool coarse = geometry.load(store, all, 6.0, project, heights, error);
    geometry.strokes(6.0, strokes);
    check(coarse && geometry.stats().polygons == 1 && !find(geometry.fills(), buildingFill, MapBatch::Triangles)
              && vertexCount(strokes, MapBatch::Lines) > 0 && geometry.stats().polylines == 3 && geometry.stats().points == 3 + 2 + 2,
          "at 6 m/px buildings and fences are left out; roads and the forest stay, the zigzag simplified to its ends");
    const bool far = geometry.load(store, all, 20.0, project, heights, error);
    check(far && geometry.stats().polylines == 2, "at 20 m/px residential roads are left out too");
}

// Opt-in: --map-geometry <minLon> <minLat> <maxLon> <maxLat> <metresPerPixel> <converted files...>
int mapGeometryTiming(const QStringList &args) {
    using Clock = std::chrono::steady_clock;
    const double minLon = args[0].toDouble(), minLat = args[1].toDouble(), maxLon = args[2].toDouble(), maxLat = args[3].toDouble();
    const double mpp = args[4].toDouble();
    const double lon0 = 0.5 * (minLon + maxLon), lat0 = 0.5 * (minLat + maxLat);
    const double kx = 111320.0 * std::cos(lat0 * M_PI / 180), ky = 110574.0;
    const MapProjection local = [&](const Location *in, size_t count, float *xz) {
        for (size_t i = 0; i < count; ++i) {
            xz[2 * i] = float((in[i].lon() - lon0) * kx);
            xz[2 * i + 1] = float(-(in[i].lat() - lat0) * ky);
        }
    };
    SortedPbfStore store;
    QString error;
    if (!store.open(args.mid(5), error)) { std::cerr << error.toStdString() << '\n'; return 1; }
    MapGeometry geometry;
    for (int pass = 0; pass < 2; ++pass) {
        auto t = Clock::now();
        if (!geometry.load(store, Box::fromDegrees(minLon, minLat, maxLon, maxLat), mpp, local, {}, error)) { std::cerr << error.toStdString() << '\n'; return 1; }
        const double loadS = std::chrono::duration<double>(Clock::now() - t).count();
        std::vector<MapBatch> strokes;
        t = Clock::now();
        geometry.strokes(mpp, strokes);
        const double strokeS = std::chrono::duration<double>(Clock::now() - t).count();
        size_t fillVertices = 0, strip = 0, lines = 0;
        for (const MapBatch &b : geometry.fills()) fillVertices += b.vertices.size() / 3;
        for (const MapBatch &b : strokes) (b.primitive == MapBatch::Lines ? lines : strip) += b.vertices.size() / 3;
        const MapGeometry::Stats &s = geometry.stats();
        std::cout << (pass ? "warm" : "cold") << ": load " << loadS << " s (read " << s.readSeconds << ", multipolygons " << s.assembleSeconds
                  << ", triangulate " << s.triangulateSeconds << "), strokes " << strokeS << " s\n"
                  << "  " << s.ways << " ways, " << s.relations << " relations, " << s.polygons << " polygons, " << s.triangles << " triangles, "
                  << s.polylines << " polylines (" << s.points << " points); " << s.culled << " culled, points read " << s.pointsRead << "\n"
                  << "  batches " << geometry.fills().size() << " fill + " << strokes.size() << " stroke; vertices: fill " << fillVertices << ", strip "
                  << strip << ", lines " << lines << "; " << (fillVertices + strip + lines) * 12 / 1048576.0 << " MiB\n";
    }
    return 0;
}
