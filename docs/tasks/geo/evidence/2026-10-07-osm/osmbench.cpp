// OSM performance benchmark for the TSRE5 osm-data design stage.
//   xml    <files...>                      current path: QXmlStreamReader loadData() + draw() (code spliced from MapDataOSM.cpp)
//   import <pbf> <cache> [bbox] [--tags] [--nomp]  PBF -> multi-level grid cache (libosmium, multipolygons assembled)
//   query  <cache> <bbox> <res> [png]       mmap cache, gather one tile, draw with the current styles
//   batch  <cache> <bbox> <tileDeg> <res> <threads>  render every tile of an area
#include <osmium/io/pbf_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>
#include <osmium/index/map/sparse_mem_array.hpp>
#include <osmium/area/assembler.hpp>
#include <osmium/area/multipolygon_manager.hpp>
#include <osmium/relations/relations_manager.hpp>
#include <osmium/geom/coordinates.hpp>

#include <tsre/geo/OSMFeatures.h>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QFile>
#include <QXmlStreamReader>
#include <QDebug>
#include <QVector>
#include <QLoggingCategory>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>

using Clock = std::chrono::steady_clock;
static double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }
static long rssMB() { rusage ru; getrusage(RUSAGE_SELF, &ru); return ru.ru_maxrss / 1024; }

struct BBox { double minlon, minlat, maxlon, maxlat; };
static BBox parseBox(char** a) { return {atof(a[0]), atof(a[1]), atof(a[2]), atof(a[3])}; }

// ---------------------------------------------------------------- classification (same rules as MapDataOSM::loadData)
static bool startsWithCI(const char* s, const char* p) { for (; *p; ++s, ++p) if (toupper((unsigned char)*s) != *p) return false; return true; }
struct Cls { uint16_t type = 0; uint8_t val2 = 0; };
static Cls classify(const osmium::TagList& tags) {
    Cls c; std::string f;
    for (const auto& t : tags) {
        const char* k = t.key();
        if (startsWithCI(k, "ADDR") || startsWithCI(k, "NAME") || startsWithCI(k, "ONEWAY") || startsWithCI(k, "MAXSPEED") || startsWithCI(k, "SURFACE")) continue;
        if (startsWithCI(k, "BRIDGE")) { c.val2 = 7; continue; }
        if (startsWithCI(k, "TUNNEL")) { c.val2 = 6; continue; }
        if (startsWithCI(k, "AMENITY") || startsWithCI(k, "BARRIER") || startsWithCI(k, "WOOD") || startsWithCI(k, "SPORT")) continue;
        if (startsWithCI(k, "BUILDING")) c.type = (uint16_t)OSMFeatures::LIST["BUILDING_YES"];
        f.assign(k); f += '_'; f += t.value();
        for (auto& ch : f) ch = toupper((unsigned char)ch);
        auto it = OSMFeatures::LIST.find(f);
        if (it != OSMFeatures::LIST.end() && it->second != 0) c.type = (uint16_t)it->second;
    }
    return c;
}
static uint8_t layerSlot(const Cls& c) {
    int l = c.type < OSMFeatures::LAYER.size() ? OSMFeatures::LAYER[c.type] : 0;
    if (l > 9) l = 9;
    return c.val2 == 7 ? 9 : 9 - l;
}

// ---------------------------------------------------------------- drawing shim (body of MapDataOSM::draw spliced in)
struct Projector {
    double lon0, lat0, kx, ky;
    Projector(const BBox& b, int res) : lon0(b.minlon), lat0(b.maxlat), kx(res / (b.maxlon - b.minlon)), ky(res / (b.maxlat - b.minlat)) {}
    // A little trig per point, standing in for the IGH -> tile conversion of MapDataOSM::r().
    inline void r(int& x, int& y, double lat, double lon) const {
        double m = std::log(std::tan(M_PI / 4 + lat * M_PI / 360)), m0 = std::log(std::tan(M_PI / 4 + lat0 * M_PI / 360));
        x = (int)((lon - lon0) * kx);
        y = (int)((m0 - m) * ky * 180.0 / M_PI / (1.0 / std::cos(lat0 * M_PI / 180)));
    }
};

struct DrawState {
    QColor* color; QColor* roadBorder; QPen* p; QPainter* gg; QBrush* brush; float height, width;
    void setColor(QColor* c) { setColor(c->red(), c->green(), c->blue()); }
    void setColor(int r, int g, int b) { color->setRgb(r, g, b); p->setColor(*color); gg->setPen(*p); brush->setColor(*color); }
    void setPenSettings(QPen* pen) { p->setCapStyle(pen->capStyle()); p->setJoinStyle(pen->joinStyle()); p->setWidthF(pen->widthF()); gg->setPen(*p); }
    void begin(QImage* img) {
        gg = new QPainter(); gg->begin(img); gg->setRenderHint(QPainter::RenderHint::Antialiasing, false);
        color = new QColor(); roadBorder = new QColor(); p = new QPen; brush = new QBrush; brush->setStyle(Qt::SolidPattern); setColor(0, 0, 255);
        height = img->height(); width = img->width();
    }
    void end() { gg->end(); delete gg; delete color; delete roadBorder; delete p; delete brush; }
};

// Shim A: the current in-memory model (Node map + Way refs), parser and draw() as they are today.
struct CurrentModel : DrawState {
    struct Node { int64_t id; float lat, lon; unsigned short type = 0; unsigned char val1 = 0, val2 = 0;
        Node() {} Node(int64_t i, float a, float o) : id(i), lat(a), lon(o) {} };
    struct Way { int64_t id; unsigned short type = 0; unsigned char val1 = 0, val2 = 0; QVector<int64_t> ref;
        Way(int64_t i = 0) : id(i) {} };
    std::unordered_map<int64_t, Node*> nodes;
    QVector<Way*> ways[10];
    const Projector* proj;
    void r(int& x, int& y, float lat, float lon) { proj->r(x, y, lat, lon); }
    void loadData(QByteArray* data) {
#include "xml_body.inc"
    }
    void drawAll() {
#include "draw_head.inc"
#include "draw_points_current.inc"
#include "draw_tail.inc"
        (void)fail; (void)tf;
    }
};

// Shim B: geometry straight from the cache, same style switch.
struct CacheWay { uint16_t type; uint8_t val2; const int32_t* pts; uint32_t n; };
struct CacheModel : DrawState {
    std::vector<CacheWay> ways[10];
    const Projector* proj;
    void r(int& x, int& y, double lat, double lon) { proj->r(x, y, lat, lon); }
    void drawAll() {
        std::vector<CacheWay*> wl[10];
        for (int l = 0; l < 10; l++) for (auto& w : ways[l]) wl[l].push_back(&w);
        struct V { std::vector<CacheWay*>* ways; } v{wl}; auto* self = &v; using Way = CacheWay;
#define this self
#include "draw_head.inc"
        for (uint32_t i = 0; i < w->n; i++) {
            r(drawX, drawY, w->pts[2 * i + 1] * 1e-7, w->pts[2 * i] * 1e-7);
            ww.push_back(QPoint(drawX, drawY));
            poly.push_back(QPoint(drawX, drawY));
        }
#include "draw_tail.inc"
#undef this
        (void)fail; (void)tf;
    }
};

// ---------------------------------------------------------------- cache format
// Multi-level grid. A feature lives on the finest level where its bbox spans <= 2x2 cells.
static const double LEVEL_DEG[] = {1.0 / 32, 1.0 / 8, 0.5, 2.0, 8.0};
static const int LEVELS = 5;
static inline uint64_t cellKey(int level, int cx, int cy) { return (uint64_t)level << 56 | (uint64_t)(uint32_t)(cx + 100000) << 28 | (uint32_t)(cy + 100000); }
struct IndexEntry { uint64_t key; uint64_t first; uint32_t count; uint32_t pad; };
struct Header { char magic[8]; uint64_t nrec, recBytes, nidx, nrefs; };
// record: u64 id | u16 type | u8 val2 | u8 slot | u8 kind | u8 pad[3] | u32 nrings | (u32 npts, i32 lon,lat * npts)* | u32 tagBytes | tags

struct Builder {
    std::vector<uint8_t> rec;
    std::unordered_map<uint64_t, std::vector<uint64_t>> cells;
    bool keepTags = false; uint64_t nrec = 0, npts = 0, multiCell = 0;
    BBox clip; bool useClip = false;
    template <class T> void put(const T& v) { const uint8_t* p = (const uint8_t*)&v; rec.insert(rec.end(), p, p + sizeof(T)); }
    void add(uint64_t id, Cls c, uint8_t kind, const std::vector<std::vector<int32_t>>& rings, const osmium::TagList& tags) {
        int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
        for (auto& r : rings) for (size_t i = 0; i < r.size(); i += 2) { x0 = std::min(x0, r[i]); x1 = std::max(x1, r[i]); y0 = std::min(y0, r[i + 1]); y1 = std::max(y1, r[i + 1]); }
        if (x0 > x1) return;
        if (useClip && (x1 * 1e-7 < clip.minlon || x0 * 1e-7 > clip.maxlon || y1 * 1e-7 < clip.minlat || y0 * 1e-7 > clip.maxlat)) return;
        uint64_t off = rec.size();
        put(id); put(c.type); put(c.val2); put(layerSlot(c)); put(kind); uint8_t pad[3] = {}; rec.insert(rec.end(), pad, pad + 3);
        put((uint32_t)rings.size());
        for (auto& r : rings) { put((uint32_t)(r.size() / 2)); const uint8_t* p = (const uint8_t*)r.data(); rec.insert(rec.end(), p, p + r.size() * 4); npts += r.size() / 2; }
        if (keepTags) {
            size_t at = rec.size(); put((uint32_t)0);
            for (auto& t : tags) { rec.insert(rec.end(), t.key(), t.key() + strlen(t.key()) + 1); rec.insert(rec.end(), t.value(), t.value() + strlen(t.value()) + 1); }
            uint32_t n = rec.size() - at - 4; memcpy(&rec[at], &n, 4);
        } else put((uint32_t)0);
        while (rec.size() % 4) rec.push_back(0);
        nrec++;
        for (int L = 0; L < LEVELS; L++) {
            double d = LEVEL_DEG[L];
            int cx0 = (int)std::floor(x0 * 1e-7 / d), cx1 = (int)std::floor(x1 * 1e-7 / d), cy0 = (int)std::floor(y0 * 1e-7 / d), cy1 = (int)std::floor(y1 * 1e-7 / d);
            if ((cx1 - cx0 <= 1 && cy1 - cy0 <= 1) || L == LEVELS - 1) {
                if (cx1 != cx0 || cy1 != cy0) multiCell++;
                for (int cx = cx0; cx <= cx1; cx++) for (int cy = cy0; cy <= cy1; cy++) cells[cellKey(L, cx, cy)].push_back(off);
                break;
            }
        }
    }
    void write(const char* path, double& secs) {
        auto t = Clock::now();
        std::vector<IndexEntry> idx; std::vector<uint64_t> refs;
        std::vector<uint64_t> keys; for (auto& c : cells) keys.push_back(c.first); std::sort(keys.begin(), keys.end());
        for (auto k : keys) { auto& v = cells[k]; idx.push_back({k, refs.size(), (uint32_t)v.size(), 0}); refs.insert(refs.end(), v.begin(), v.end()); }
        Header h{{'T', 'S', 'R', 'E', 'O', 'S', 'M', '1'}, nrec, rec.size(), idx.size(), refs.size()};
        FILE* f = fopen(path, "wb");
        fwrite(&h, sizeof h, 1, f); fwrite(idx.data(), sizeof(IndexEntry), idx.size(), f); fwrite(refs.data(), 8, refs.size(), f); fwrite(rec.data(), 1, rec.size(), f);
        fclose(f);
        secs = since(t);
    }
};

using LocIndex = osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location>;

struct WayHandler : osmium::handler::Handler {
    Builder& b; uint64_t kept = 0, skipped = 0;
    std::vector<std::vector<int32_t>> rings{1};
    explicit WayHandler(Builder& bb) : b(bb) {}
    int mode = getenv("STAGE") ? atoi(getenv("STAGE")) : 9;
    void way(const osmium::Way& w) {
        if (mode == 0) return;
        Cls c = classify(w.tags());
        if (mode == 1) return;
        if (c.type == 0) { skipped++; return; }
        auto& r = rings[0]; r.clear();
        for (auto& n : w.nodes()) if (n.location().valid()) { r.push_back(n.location().x()); r.push_back(n.location().y()); }
        if (r.size() < 4) { skipped++; return; }
        if (mode == 2) return;
        b.add(w.positive_id(), c, 0, rings, w.tags()); kept++;
    }
};

static int cmdImport(int argc, char** argv) {
    const char* pbf = argv[2]; const char* out = argv[3];
    Builder b; bool mp = true; int ai = 4;
    if (argc > ai + 3 && argv[ai][0] != '-') { b.clip = parseBox(argv + ai); b.useClip = true; ai += 4; }
    for (; ai < argc; ai++) { if (!strcmp(argv[ai], "--tags")) b.keepTags = true; if (!strcmp(argv[ai], "--nomp")) mp = false; }
    auto t0 = Clock::now();
    osmium::area::Assembler::config_type acfg; acfg.create_way_polygons = false; acfg.ignore_invalid_locations = true;
    osmium::area::MultipolygonManager<osmium::area::Assembler> mpm{acfg};
    double tRel = 0;
    if (mp) { auto t = Clock::now(); osmium::relations::read_relations(osmium::io::File{pbf}, mpm); tRel = since(t); }
    auto t1 = Clock::now();
    LocIndex idx; osmium::handler::NodeLocationsForWays<LocIndex> loc{idx}; loc.ignore_errors();
    WayHandler wh{b}; uint64_t areas = 0;
    osmium::io::Reader rd{pbf, osmium::io::read_meta::no};
    if (mp) {
        osmium::apply(rd, loc, wh, mpm.handler([&](osmium::memory::Buffer&& buf) {
            for (auto& a : buf.select<osmium::Area>()) {
                Cls c = classify(a.tags()); if (c.type == 0) continue;
                std::vector<std::vector<int32_t>> rings;
                for (auto& o : a.outer_rings()) {
                    rings.emplace_back(); for (auto& n : o) { rings.back().push_back(n.location().x()); rings.back().push_back(n.location().y()); }
                    for (auto& in : a.inner_rings(o)) { rings.emplace_back(); for (auto& n : in) { rings.back().push_back(n.location().x()); rings.back().push_back(n.location().y()); } }
                }
                b.add(a.positive_id(), c, 1, rings, a.tags()); areas++;
            }
        }));
    } else if (getenv("NOLOC")) osmium::apply(rd, wh); else osmium::apply(rd, loc, wh);
    rd.close();
    double tMain = since(t1);
    double tWrite; b.write(out, tWrite);
    struct stat st; stat(out, &st);
    printf("import %s: relations pass %.2fs, nodes+ways pass %.2fs, write %.2fs, total %.2fs | ways kept %lu skipped %lu, mp areas %lu, records %lu, points %lu, multi-cell %lu, cells %zu | file %.0f MB, peak RSS %ld MB\n",
           pbf, tRel, tMain, tWrite, since(t0), wh.kept, wh.skipped, areas, b.nrec, b.npts, b.multiCell, b.cells.size(), st.st_size / 1e6, rssMB());
    return 0;
}

// ---------------------------------------------------------------- cache reader
struct Cache {
    const uint8_t* base = nullptr; size_t size = 0; Header h; const IndexEntry* idx; const uint64_t* refs; const uint8_t* rec;
    bool open(const char* path) {
        int fd = ::open(path, O_RDONLY); if (fd < 0) return false; struct stat st; fstat(fd, &st); size = st.st_size;
        base = (const uint8_t*)mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0); ::close(fd);
        memcpy(&h, base, sizeof h); idx = (const IndexEntry*)(base + sizeof h); refs = (const uint64_t*)(idx + h.nidx); rec = (const uint8_t*)(refs + h.nrefs);
        return true;
    }
    template <class F> void query(const BBox& q, F&& f) const {
        std::vector<uint64_t> offs;
        for (int L = 0; L < LEVELS; L++) {
            double d = LEVEL_DEG[L];
            int cx0 = (int)std::floor(q.minlon / d) - (L ? 0 : 0), cx1 = (int)std::floor(q.maxlon / d), cy0 = (int)std::floor(q.minlat / d), cy1 = (int)std::floor(q.maxlat / d);
            for (int cx = cx0 - 1; cx <= cx1; cx++) for (int cy = cy0 - 1; cy <= cy1; cy++) {  // -1: features stored in a neighbour cell reach into this one
                uint64_t k = cellKey(L, cx, cy);
                auto it = std::lower_bound(idx, idx + h.nidx, k, [](const IndexEntry& e, uint64_t k) { return e.key < k; });
                if (it != idx + h.nidx && it->key == k) offs.insert(offs.end(), refs + it->first, refs + it->first + it->count);
            }
        }
        std::sort(offs.begin(), offs.end()); offs.erase(std::unique(offs.begin(), offs.end()), offs.end());
        for (auto o : offs) {
            const uint8_t* p = rec + o; uint16_t type; memcpy(&type, p + 8, 2); uint8_t val2 = p[10], slot = p[11], kind = p[12];
            uint32_t nr; memcpy(&nr, p + 16, 4); p += 20;
            for (uint32_t i = 0; i < nr; i++) {
                uint32_t n; memcpy(&n, p, 4); const int32_t* pts = (const int32_t*)(p + 4); p += 4 + n * 8;
                // cheap bbox reject per ring
                int32_t x0 = INT32_MAX, x1 = INT32_MIN, y0 = INT32_MAX, y1 = INT32_MIN;
                for (uint32_t j = 0; j < n; j++) { x0 = std::min(x0, pts[2 * j]); x1 = std::max(x1, pts[2 * j]); y0 = std::min(y0, pts[2 * j + 1]); y1 = std::max(y1, pts[2 * j + 1]); }
                if (x1 * 1e-7 < q.minlon || x0 * 1e-7 > q.maxlon || y1 * 1e-7 < q.minlat || y0 * 1e-7 > q.maxlat) continue;
                f(CacheWay{type, val2, pts, n}, slot, kind);
            }
        }
    }
};

struct TileResult { double tQuery, tDraw; size_t ways, pts; };
static TileResult renderTile(const Cache& c, const BBox& q, int res, const char* png) {
    CacheModel m; Projector pr(q, res); m.proj = &pr;
    auto t = Clock::now(); size_t pts = 0, n = 0;
    c.query(q, [&](const CacheWay& w, uint8_t slot, uint8_t) { m.ways[slot].push_back(w); pts += w.n; n++; });
    double tq = since(t);
    QImage img(res, res, QImage::Format_RGB888);
    t = Clock::now(); m.begin(&img); m.drawAll(); m.end(); double td = since(t);
    if (png) img.save(png);
    return {tq, td, n, pts};
}

static int cmdQuery(int argc, char** argv) {
    auto t0 = Clock::now(); Cache c; c.open(argv[2]); double tOpen = since(t0);
    BBox q = parseBox(argv + 3); int res = atoi(argv[7]);
    TileResult r1 = renderTile(c, q, res, argc > 8 ? argv[8] : nullptr);
    TileResult r2 = renderTile(c, q, res, nullptr);
    printf("query: open %.4fs | first: gather %.4fs draw %.3fs | warm: gather %.4fs draw %.3fs | ways %zu pts %zu | %dx%d\n",
           tOpen, r1.tQuery, r1.tDraw, r2.tQuery, r2.tDraw, r1.ways, r1.pts, res, res);
    return 0;
}

static int cmdBatch(int argc, char** argv) {
    Cache c; c.open(argv[2]); BBox a = parseBox(argv + 3); double td = atof(argv[7]); int res = atoi(argv[8]); int th = atoi(argv[9]);
    double tdLon = td / std::cos((a.minlat + a.maxlat) / 2 * M_PI / 180);
    std::vector<BBox> tiles;
    for (double y = a.minlat; y < a.maxlat; y += td) for (double x = a.minlon; x < a.maxlon; x += tdLon) tiles.push_back({x, y, x + tdLon, y + td});
    std::atomic<size_t> next{0}; std::atomic<uint64_t> pts{0};
    auto t0 = Clock::now();
    std::vector<std::thread> ts;
    for (int i = 0; i < th; i++) ts.emplace_back([&] { for (size_t k; (k = next++) < tiles.size();) pts += renderTile(c, tiles[k], res, nullptr).pts; });
    for (auto& t : ts) t.join();
    double s = since(t0);
    printf("batch: %zu tiles %dx%d, %d threads: %.2fs total, %.3fs/tile wall, %.1f tiles/s, %lu pts, peak RSS %ld MB\n", tiles.size(), res, res, th, s, s / tiles.size(), tiles.size() / s, (unsigned long)pts, rssMB());
    return 0;
}

static int cmdXml(int argc, char** argv) {
    // argv: xml <minlon minlat maxlon maxlat> <res> files...
    BBox q = parseBox(argv + 2); int res = atoi(argv[6]);
    CurrentModel m; Projector pr(q, res); m.proj = &pr;
    auto t = Clock::now(); size_t bytes = 0;
    for (int i = 7; i < argc; i++) { QFile f(argv[i]); f.open(QFile::ReadOnly); QByteArray d = f.readAll(); bytes += d.size(); m.loadData(&d); }
    double tParse = since(t);
    QImage img(res, res, QImage::Format_RGB888);
    t = Clock::now(); m.begin(&img); m.drawAll(); m.end(); double tDraw = since(t);
    size_t nw = 0; for (auto& l : m.ways) nw += l.size();
    img.save("current.png");
    printf("current path: XML %.0f MB parse %.2fs, draw %.3fs (%dx%d), nodes %zu ways %zu, peak RSS %ld MB\n", bytes / 1e6, tParse, tDraw, res, res, m.nodes.size(), nw, rssMB());
    return 0;
}


// Parallel import: nodes -> sorted (id,loc) array on the main thread, way buffers resolved by T workers.
#include <mutex>
#include <condition_variable>
#include <deque>
static int cmdPImport(int argc, char** argv) {
    const char* pbf = argv[2]; int th = atoi(argv[3]);
    auto t0 = Clock::now();
    std::vector<uint64_t> ids; std::vector<osmium::Location> locs;
    std::deque<osmium::memory::Buffer> q; std::mutex mx; std::condition_variable cv; bool done = false;
    std::vector<Builder> builders(th); std::vector<std::thread> ws; std::vector<uint64_t> kept(th);
    double tNodes = 0; bool started = false;
    auto worker = [&](int k) {
        Builder& b = builders[k]; std::vector<std::vector<int32_t>> rings{1};
        for (;;) {
            osmium::memory::Buffer buf;
            { std::unique_lock<std::mutex> l(mx); cv.wait(l, [&] { return done || !q.empty(); }); if (q.empty()) return; buf = std::move(q.front()); q.pop_front(); }
            for (auto& w : buf.select<osmium::Way>()) {
                Cls c = classify(w.tags()); if (c.type == 0) continue;
                auto& r = rings[0]; r.clear();
                for (auto& n : w.nodes()) {
                    auto it = std::lower_bound(ids.begin(), ids.end(), (uint64_t)n.positive_ref());
                    if (it != ids.end() && *it == (uint64_t)n.positive_ref()) { auto& L = locs[it - ids.begin()]; r.push_back(L.x()); r.push_back(L.y()); }
                }
                if (r.size() < 4) continue;
                b.add(w.positive_id(), c, 0, rings, w.tags()); kept[k]++;
            }
        }
    };
    osmium::io::Reader rd{pbf, osmium::osm_entity_bits::node | osmium::osm_entity_bits::way, osmium::io::read_meta::no};
    while (osmium::memory::Buffer buf = rd.read()) {
        bool hasWay = false;
        for (auto& o : buf.select<osmium::OSMObject>()) {
            if (o.type() == osmium::item_type::node) { auto& n = static_cast<const osmium::Node&>(o); ids.push_back(n.positive_id()); locs.push_back(n.location()); }
            else { hasWay = true; break; }
        }
        if (hasWay) {
            if (!started) { started = true; tNodes = since(t0); for (int k = 0; k < th; k++) ws.emplace_back(worker, k); }
            std::unique_lock<std::mutex> l(mx); q.push_back(std::move(buf)); cv.notify_one();
        }
    }
    { std::lock_guard<std::mutex> l(mx); done = true; } cv.notify_all();
    for (auto& t : ws) t.join();
    rd.close();
    uint64_t K = 0, P = 0; for (int k = 0; k < th; k++) { K += kept[k]; P += builders[k].npts; }
    printf("pimport %s threads=%d: node array %.2fs (%zu nodes, %.0f MB), total %.2fs, ways kept %lu, points %lu, peak RSS %ld MB\n",
           pbf, th, tNodes, ids.size(), ids.size() * 16 / 1e6, since(t0), (unsigned long)K, (unsigned long)P, rssMB());
    return 0;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QLoggingCategory::setFilterRules("*.debug=false");
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "import") return cmdImport(argc, argv);
    if (cmd == "query") return cmdQuery(argc, argv);
    if (cmd == "batch") return cmdBatch(argc, argv);
    if (cmd == "xml") return cmdXml(argc, argv);
    if (cmd == "pimport") return cmdPImport(argc, argv);
    fprintf(stderr, "usage: see header\n");
    return 1;
}
