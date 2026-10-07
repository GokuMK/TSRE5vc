// Hybrid variants: small index in memory, data read back from a PBF.
//   blobs   <pbf> <threads>                             build blob index (type, id range), full decode
//   tile    <pbf> <cache.bin> <bbox> <threads>          way IDs of a tile -> read ways + their nodes from original PBF
//   route   <pbf> <cache.bin> <bbox> <tileDeg> <lruMB>  all tiles of an area, LRU of decoded blobs
//   compact <cache.bin>                                 delta/varint (+zstd per cell) size of the grid cache
//   lowrite <pbf> <out.pbf>                             spatially sorted PBF, typed ways with locations on ways
//   lotile  <lo.pbf> <bbox> <threads>                   tile query against the sorted PBF via a blob bbox index
#include <osmium/io/pbf_input.hpp>
#include <osmium/io/pbf_output.hpp>
#include <osmium/io/writer.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>
#include <osmium/index/map/sparse_mem_array.hpp>
#include <osmium/builder/osm_object_builder.hpp>
#include <osmium/memory/buffer.hpp>
#include <protozero/pbf_reader.hpp>
#include <tsre/geo/OSMFeatures.h>
#include <libdeflate.h>
#include <zstd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

using Clock = std::chrono::steady_clock;
static double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }
static long rssMB() { rusage ru; getrusage(RUSAGE_SELF, &ru); return ru.ru_maxrss / 1024; }
struct BBox { double minlon, minlat, maxlon, maxlat; };
static BBox parseBox(char** a) { return {atof(a[0]), atof(a[1]), atof(a[2]), atof(a[3])}; }

template <class F> static void parallelFor(size_t n, int th, F&& f) {
    std::atomic<size_t> next{0}; std::vector<std::thread> ts;
    for (int t = 0; t < th; t++) ts.emplace_back([&, t] { for (size_t i; (i = next++) < n;) f(i, t); });
    for (auto& t : ts) t.join();
}

// ---------------------------------------------------------------- raw PBF access
static uint64_t vi(const uint8_t*& p) { uint64_t r = 0; int s = 0; for (;;) { uint8_t c = *p++; r |= uint64_t(c & 127) << s; s += 7; if (c < 128) return r; } }
struct Blob { const uint8_t* z; uint32_t zlen, raw; bool data; uint8_t type = 0; int64_t first = 0, last = 0; int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN; };
struct Pbf {
    const uint8_t* m = nullptr; size_t size = 0; std::vector<Blob> blobs;
    void open(const char* path) {
        int fd = ::open(path, O_RDONLY); struct stat st; fstat(fd, &st); size = st.st_size;
        m = (const uint8_t*)mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0); ::close(fd);
        size_t off = 0;
        while (off < size) {
            uint32_t hl = __builtin_bswap32(*(const uint32_t*)(m + off)); off += 4;
            const uint8_t *p = m + off, *e = p + hl; uint64_t ds = 0; bool data = false;
            while (p < e) { uint64_t k = vi(p); if ((k & 7) == 0) { uint64_t v = vi(p); if ((k >> 3) == 3) ds = v; } else { uint64_t n = vi(p); if ((k >> 3) == 1) data = (n == 7 && !memcmp(p, "OSMData", 7)); p += n; } }
            off += hl; const uint8_t *q = m + off, *qe = q + ds; Blob b{nullptr, 0, 0, data};
            while (q < qe) { uint64_t k = vi(q); if ((k & 7) == 0) { uint64_t v = vi(q); if ((k >> 3) == 2) b.raw = v; } else { uint64_t n = vi(q); if ((k >> 3) == 3) { b.z = q; b.zlen = n; } q += n; } }
            if (data) blobs.push_back(b);
            off += ds;
        }
    }
};
static void inflate(libdeflate_decompressor* d, const Blob& b, std::vector<uint8_t>& out) {
    out.resize(b.raw); size_t a; libdeflate_zlib_decompress(d, b.z, b.zlen, out.data(), b.raw, &a);
}

// Decoded block: nodes (sorted ids + locations) and ways (id, refs, optional locations, tags).
struct DWay { int64_t id; std::vector<int64_t> refs; std::vector<int32_t> xy; std::vector<std::pair<std::string_view, std::string_view>> tags; };
struct Decoded {
    std::vector<uint8_t> raw; std::vector<std::string_view> strings;
    std::vector<int64_t> nid; std::vector<int32_t> nxy;  // node id + x,y (1e-7)
    std::vector<DWay> ways; int64_t firstRel = 0, lastRel = 0; uint8_t type = 0;
    size_t bytes() const { return raw.capacity() + nid.capacity() * 8 + nxy.capacity() * 4 + ways.size() * 96; }
};
static void decode(libdeflate_decompressor* d, const Blob& b, Decoded& D, bool wayDetail, const std::vector<int64_t>* want = nullptr) {
    inflate(d, b, D.raw);
    protozero::pbf_reader blk(reinterpret_cast<const char*>(D.raw.data()), D.raw.size());
    int64_t gran = 100, latoff = 0, lonoff = 0; std::vector<protozero::data_view> groups;
    while (blk.next()) {
        switch (blk.tag()) {
            case 1: { protozero::pbf_reader st(blk.get_view()); while (st.next(1)) { auto v = st.get_view(); D.strings.emplace_back(v.data(), v.size()); } break; }
            case 2: groups.push_back(blk.get_view()); break;
            case 17: gran = blk.get_int32(); break;
            case 19: latoff = blk.get_int64(); break;
            case 20: lonoff = blk.get_int64(); break;
            default: blk.skip();
        }
    }
    for (auto& gv : groups) {
        protozero::pbf_reader g(gv);
        while (g.next()) {
            if (g.tag() == 2) {  // DenseNodes
                D.type = 1; protozero::pbf_reader dn(g.get_view());
                while (dn.next()) {
                    switch (dn.tag()) {
                        case 1: { int64_t a = 0; for (auto v : dn.get_packed_sint64()) { a += v; D.nid.push_back(a); } break; }
                        case 8: { int64_t a = 0; size_t i = 0; auto r = dn.get_packed_sint64(); D.nxy.resize(std::max(D.nxy.size(), (size_t)r.size() * 2)); for (auto v : r) { a += v; D.nxy[2 * i++ + 1] = (int32_t)((latoff + gran * a) / 100); } break; }
                        case 9: { int64_t a = 0; size_t i = 0; auto r = dn.get_packed_sint64(); D.nxy.resize(std::max(D.nxy.size(), (size_t)r.size() * 2)); for (auto v : r) { a += v; D.nxy[2 * i++] = (int32_t)((lonoff + gran * a) / 100); } break; }
                        default: dn.skip();
                    }
                }
            } else if (g.tag() == 3) {  // Way
                D.type = 2; protozero::pbf_reader w(g.get_view()); DWay W{}; std::vector<uint32_t> ks, vs; std::vector<int64_t> la, lo;
                bool skip = false;
                while (!skip && w.next()) {
                    switch (w.tag()) {
                        case 1: W.id = w.get_int64(); if (want && !std::binary_search(want->begin(), want->end(), W.id)) skip = true; break;
                        case 2: if (wayDetail) for (auto v : w.get_packed_uint32()) ks.push_back(v); else w.skip(); break;
                        case 3: if (wayDetail) for (auto v : w.get_packed_uint32()) vs.push_back(v); else w.skip(); break;
                        case 8: if (wayDetail) { int64_t a = 0; for (auto v : w.get_packed_sint64()) { a += v; W.refs.push_back(a); } } else w.skip(); break;
                        case 9: { int64_t a = 0; for (auto v : w.get_packed_sint64()) { a += v; la.push_back(a); } break; }
                        case 10: { int64_t a = 0; for (auto v : w.get_packed_sint64()) { a += v; lo.push_back(a); } break; }
                        default: w.skip();
                    }
                }
                if (skip) continue;
                for (size_t i = 0; i < ks.size() && i < vs.size(); i++) W.tags.emplace_back(D.strings[ks[i]], D.strings[vs[i]]);
                for (size_t i = 0; i < la.size() && i < lo.size(); i++) { W.xy.push_back((int32_t)((lonoff + gran * lo[i]) / 100)); W.xy.push_back((int32_t)((latoff + gran * la[i]) / 100)); }
                D.ways.push_back(std::move(W));
            } else if (g.tag() == 4) {
                D.type = 3; protozero::pbf_reader r(g.get_view()); while (r.next()) { if (r.tag() == 1) { int64_t id = r.get_int64(); if (!D.firstRel) D.firstRel = id; D.lastRel = id; } else r.skip(); }
            } else g.skip();
        }
    }
}

// Blob index: type and first/last id of every data blob (one full decode).
static void buildIndex(Pbf& f, int th, double& secs) {
    auto t = Clock::now();
    std::vector<libdeflate_decompressor*> ds(th); for (auto& d : ds) d = libdeflate_alloc_decompressor();
    parallelFor(f.blobs.size(), th, [&](size_t i, int k) {
        Decoded D; decode(ds[k], f.blobs[i], D, false); auto& b = f.blobs[i]; b.type = D.type;
        if (D.type == 1 && !D.nid.empty()) { b.first = D.nid.front(); b.last = D.nid.back(); }
        if (D.type == 2 && !D.ways.empty()) { b.first = D.ways.front().id; b.last = D.ways.back().id; }
        if (D.type == 3) { b.first = D.firstRel; b.last = D.lastRel; }
        for (auto& w : D.ways) for (size_t j = 0; j < w.xy.size(); j += 2) { b.x0 = std::min(b.x0, w.xy[j]); b.x1 = std::max(b.x1, w.xy[j]); b.y0 = std::min(b.y0, w.xy[j + 1]); b.y1 = std::max(b.y1, w.xy[j + 1]); }
    });
    for (auto d : ds) libdeflate_free_decompressor(d);
    secs = since(t);
}
static std::vector<size_t> blobsOfType(const Pbf& f, uint8_t type) { std::vector<size_t> v; for (size_t i = 0; i < f.blobs.size(); i++) if (f.blobs[i].type == type) v.push_back(i); return v; }
static size_t findBlob(const Pbf& f, const std::vector<size_t>& of, int64_t id) {
    auto it = std::upper_bound(of.begin(), of.end(), id, [&](int64_t v, size_t bi) { return v < f.blobs[bi].first; });
    if (it == of.begin()) return SIZE_MAX; --it; return id <= f.blobs[*it].last ? *it : SIZE_MAX;
}

// ---------------------------------------------------------------- grid cache (same layout as osmbench)
static const double LEVEL_DEG[] = {1.0 / 32, 1.0 / 8, 0.5, 2.0, 8.0};
static const int LEVELS = 5;
static inline uint64_t cellKey(int level, int cx, int cy) { return (uint64_t)level << 56 | (uint64_t)(uint32_t)(cx + 100000) << 28 | (uint32_t)(cy + 100000); }
struct IndexEntry { uint64_t key; uint64_t first; uint32_t count; uint32_t pad; };
struct Header { char magic[8]; uint64_t nrec, recBytes, nidx, nrefs; };
struct Cache {
    const uint8_t* base; size_t size; Header h; const IndexEntry* idx; const uint64_t* refs; const uint8_t* rec;
    void open(const char* path) {
        int fd = ::open(path, O_RDONLY); struct stat st; fstat(fd, &st); size = st.st_size;
        base = (const uint8_t*)mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0); ::close(fd);
        memcpy(&h, base, sizeof h); idx = (const IndexEntry*)(base + sizeof h); refs = (const uint64_t*)(idx + h.nidx); rec = (const uint8_t*)(refs + h.nrefs);
    }
    // way ids (kind 0) whose rings touch q
    std::vector<int64_t> wayIds(const BBox& q) const {
        std::vector<uint64_t> offs;
        for (int L = 0; L < LEVELS; L++) {
            double d = LEVEL_DEG[L];
            for (int cx = (int)std::floor(q.minlon / d) - 1; cx <= (int)std::floor(q.maxlon / d); cx++)
                for (int cy = (int)std::floor(q.minlat / d) - 1; cy <= (int)std::floor(q.maxlat / d); cy++) {
                    uint64_t k = cellKey(L, cx, cy);
                    auto it = std::lower_bound(idx, idx + h.nidx, k, [](const IndexEntry& e, uint64_t k) { return e.key < k; });
                    if (it != idx + h.nidx && it->key == k) offs.insert(offs.end(), refs + it->first, refs + it->first + it->count);
                }
        }
        std::sort(offs.begin(), offs.end()); offs.erase(std::unique(offs.begin(), offs.end()), offs.end());
        std::vector<int64_t> ids;
        for (auto o : offs) {
            const uint8_t* p = rec + o; uint64_t id; memcpy(&id, p, 8); if (p[12] != 0) continue;
            uint32_t n; memcpy(&n, p + 20, 4); const int32_t* pts = (const int32_t*)(p + 24);
            int32_t x0 = INT32_MAX, x1 = INT32_MIN, y0 = INT32_MAX, y1 = INT32_MIN;
            for (uint32_t j = 0; j < n; j++) { x0 = std::min(x0, pts[2 * j]); x1 = std::max(x1, pts[2 * j]); y0 = std::min(y0, pts[2 * j + 1]); y1 = std::max(y1, pts[2 * j + 1]); }
            if (x1 * 1e-7 < q.minlon || x0 * 1e-7 > q.maxlon || y1 * 1e-7 < q.minlat || y0 * 1e-7 > q.maxlat) continue;
            ids.push_back((int64_t)id);
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    }
};

// ---------------------------------------------------------------- hybrid tile: ids from index, data from original PBF
struct BlobLru {
    size_t cap, used = 0; uint64_t hits = 0, misses = 0; std::mutex mx;
    std::list<size_t> order; std::unordered_map<size_t, std::pair<std::shared_ptr<Decoded>, std::list<size_t>::iterator>> map;
    explicit BlobLru(size_t c) : cap(c) {}
    std::shared_ptr<Decoded> get(size_t i) { std::lock_guard<std::mutex> l(mx); auto it = map.find(i); if (it == map.end()) { misses++; return nullptr; } hits++; order.splice(order.begin(), order, it->second.second); return it->second.first; }
    void put(size_t i, std::shared_ptr<Decoded> d) {
        std::lock_guard<std::mutex> l(mx); if (!cap || map.count(i)) return;
        order.push_front(i); used += d->bytes(); map[i] = {d, order.begin()};
        while (used > cap && !order.empty()) { size_t v = order.back(); order.pop_back(); used -= map[v].first->bytes(); map.erase(v); }
    }
};
struct HybridStats { size_t ways = 0, wayBlobs = 0, nodeBlobs = 0, pts = 0, missing = 0; uint64_t zbytes = 0; double tWays = 0, tNodes = 0; };
static HybridStats hybridTile(const Pbf& f, const std::vector<size_t>& wb, const std::vector<size_t>& nb, const std::vector<int64_t>& ids, int th,
                              std::vector<libdeflate_decompressor*>& ds, BlobLru& lru) {
    HybridStats s; s.ways = ids.size();
    auto load = [&](size_t bi, int k, bool detail, const std::vector<int64_t>* want = nullptr) {
        auto d = lru.get(bi); if (d) return d;
        d = std::make_shared<Decoded>(); decode(ds[k], f.blobs[bi], *d, detail, lru.cap ? nullptr : want); lru.put(bi, d); return d;
    };
    // ways
    auto t = Clock::now();
    std::unordered_map<size_t, std::vector<int64_t>> perBlob;
    for (auto id : ids) { size_t b = findBlob(f, wb, id); if (b != SIZE_MAX) perBlob[b].push_back(id); }
    std::vector<size_t> wl; for (auto& p : perBlob) { wl.push_back(p.first); s.zbytes += f.blobs[p.first].zlen; }
    std::vector<std::vector<int64_t>> refsPer(wl.size());
    std::vector<size_t> tagCount(wl.size());
    parallelFor(wl.size(), th, [&](size_t i, int k) {
        auto& want = perBlob[wl[i]]; std::sort(want.begin(), want.end()); auto d = load(wl[i], k, true, &want);
        for (auto& w : d->ways) if (std::binary_search(want.begin(), want.end(), w.id)) { refsPer[i].insert(refsPer[i].end(), w.refs.begin(), w.refs.end()); tagCount[i] += w.tags.size(); }
    });
    s.wayBlobs = wl.size(); s.tWays = since(t);
    // nodes
    t = Clock::now();
    std::vector<int64_t> refs; for (auto& r : refsPer) refs.insert(refs.end(), r.begin(), r.end());
    std::sort(refs.begin(), refs.end()); refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
    std::unordered_map<size_t, std::vector<int64_t>> perNode;
    for (auto r : refs) { size_t b = findBlob(f, nb, r); if (b != SIZE_MAX) perNode[b].push_back(r); else s.missing++; }
    std::vector<size_t> nl; for (auto& p : perNode) { nl.push_back(p.first); s.zbytes += f.blobs[p.first].zlen; }
    std::atomic<size_t> found{0};
    parallelFor(nl.size(), th, [&](size_t i, int k) {
        auto d = load(nl[i], k, false);
        for (auto r : perNode[nl[i]]) { auto it = std::lower_bound(d->nid.begin(), d->nid.end(), r); if (it != d->nid.end() && *it == r) found++; }
    });
    s.nodeBlobs = nl.size(); s.pts = found; s.tNodes = since(t);
    return s;
}

static int cmdBlobs(int argc, char** argv) {
    Pbf f; f.open(argv[2]); double s; buildIndex(f, atoi(argv[3]), s);
    size_t n[4] = {}; uint64_t z[4] = {}, r[4] = {};
    for (auto& b : f.blobs) { n[b.type]++; z[b.type] += b.zlen; r[b.type] += b.raw; }
    printf("blob index %s: %.2fs | node blobs %zu (%.0f MB z, avg %.0f KB z / %.0f KB raw) | way blobs %zu (%.0f MB z) | relation blobs %zu (%.0f MB z) | index %zu bytes\n",
           argv[2], s, n[1], z[1] / 1e6, z[1] / 1e3 / n[1], r[1] / 1e3 / n[1], n[2], z[2] / 1e6, n[3], z[3] / 1e6, f.blobs.size() * 24);
    return 0;
}

static int cmdTile(int argc, char** argv) {
    Pbf f; f.open(argv[2]); double s; int th = atoi(argv[8]); buildIndex(f, th, s);
    Cache c; c.open(argv[3]); BBox q = parseBox(argv + 4);
    auto wb = blobsOfType(f, 2), nb = blobsOfType(f, 1);
    std::vector<libdeflate_decompressor*> ds(th); for (auto& d : ds) d = libdeflate_alloc_decompressor();
    auto t = Clock::now(); auto ids = c.wayIds(q); double tIds = since(t);
    BlobLru none(0);
    t = Clock::now(); auto h = hybridTile(f, wb, nb, ids, th, ds, none); double tAll = since(t);
    printf("hybrid tile (%d thr): ids %.4fs, ways %zu in %zu way blobs %.3fs, %zu pts in %zu node blobs %.3fs (of %zu), read %.1f MB z, total %.3fs, missing refs %zu\n",
           th, tIds, h.ways, h.wayBlobs, h.tWays, h.pts, h.nodeBlobs, h.tNodes, nb.size(), h.zbytes / 1e6, tAll + tIds, h.missing);
    return 0;
}

static int cmdRoute(int argc, char** argv) {
    Pbf f; f.open(argv[2]); double s; int th = 12; buildIndex(f, th, s);
    Cache c; c.open(argv[3]); BBox a = parseBox(argv + 4); double td = atof(argv[8]); size_t lruMB = atol(argv[9]);
    if (argc > 10) th = atoi(argv[10]);
    auto wb = blobsOfType(f, 2), nb = blobsOfType(f, 1);
    std::vector<libdeflate_decompressor*> ds(th); for (auto& d : ds) d = libdeflate_alloc_decompressor();
    double tdLon = td / std::cos((a.minlat + a.maxlat) / 2 * M_PI / 180);
    BlobLru lru(lruMB << 20); size_t tiles = 0, wbs = 0, nbs = 0; double worst = 0;
    auto t0 = Clock::now();
    for (double y = a.minlat; y < a.maxlat; y += td) for (double x = a.minlon; x < a.maxlon; x += tdLon) {
        auto t = Clock::now();
        auto h = hybridTile(f, wb, nb, c.wayIds({x, y, x + tdLon, y + td}), th, ds, lru);
        worst = std::max(worst, since(t)); tiles++; wbs += h.wayBlobs; nbs += h.nodeBlobs;
    }
    double tot = since(t0);
    printf("hybrid route, LRU %zu MB, %d thr: %zu tiles %.2fs (%.3fs/tile avg, worst %.3fs), blob decodes %lu (hits %lu), blob touches/tile: way %.0f node %.0f, peak RSS %ld MB\n",
           lruMB, th, tiles, tot, tot / tiles, worst, (unsigned long)lru.misses + (lruMB ? 0 : 0), (unsigned long)lru.hits, (double)wbs / tiles, (double)nbs / tiles, rssMB());
    return 0;
}

// ---------------------------------------------------------------- compact encoding of the grid cache
static void putVar(std::vector<uint8_t>& o, uint64_t v) { while (v >= 128) { o.push_back(uint8_t(v | 128)); v >>= 7; } o.push_back(uint8_t(v)); }
static uint64_t zz(int64_t v) { return (uint64_t(v) << 1) ^ uint64_t(v >> 63); }
static int cmdCompact(int argc, char** argv) {
    Cache c; c.open(argv[2]);
    uint64_t raw = 0, var = 0, zst = 0, idsOnly = 0;
    std::vector<uint8_t> cell, comp; ZSTD_CCtx* cx = ZSTD_createCCtx();
    // Encode every cell's records independently (records shared by 2-4 cells are encoded once per cell, as stored).
    std::unordered_set<uint64_t> seen; uint64_t uniqVar = 0;
    for (uint64_t i = 0; i < c.h.nidx; i++) {
        auto& e = c.idx[i]; cell.clear(); int64_t lastId = 0;
        std::vector<uint64_t> offs(c.refs + e.first, c.refs + e.first + e.count); std::sort(offs.begin(), offs.end());
        for (auto o : offs) {
            const uint8_t* p = c.rec + o; uint64_t id; memcpy(&id, p, 8); uint16_t type; memcpy(&type, p + 8, 2);
            size_t before = cell.size();
            putVar(cell, zz((int64_t)id - lastId)); lastId = id; putVar(cell, type); cell.push_back(p[10]); cell.push_back(p[11] | p[12] << 4);
            uint32_t nr; memcpy(&nr, p + 16, 4); putVar(cell, nr); p += 20;
            for (uint32_t r = 0; r < nr; r++) {
                uint32_t n; memcpy(&n, p, 4); const int32_t* pts = (const int32_t*)(p + 4); p += 4 + n * 8; putVar(cell, n);
                int32_t px = 0, py = 0; for (uint32_t j = 0; j < n; j++) { putVar(cell, zz(pts[2 * j] - px)); putVar(cell, zz(pts[2 * j + 1] - py)); px = pts[2 * j]; py = pts[2 * j + 1]; }
            }
            if (seen.insert(o).second) uniqVar += cell.size() - before;
            idsOnly += 0;
        }
        var += cell.size();
        comp.resize(ZSTD_compressBound(cell.size())); zst += ZSTD_compressCCtx(cx, comp.data(), comp.size(), cell.data(), cell.size(), 3);
    }
    raw = c.size;
    // "ids only" index: per cell delta-varint way/area ids
    uint64_t idIdx = 0;
    for (uint64_t i = 0; i < c.h.nidx; i++) {
        auto& e = c.idx[i]; std::vector<uint64_t> ids;
        for (uint64_t k = 0; k < e.count; k++) { uint64_t id; memcpy(&id, c.rec + c.refs[e.first + k], 8); ids.push_back(id); }
        std::sort(ids.begin(), ids.end()); std::vector<uint8_t> o; uint64_t last = 0; for (auto id : ids) { putVar(o, id - last); last = id; } idIdx += o.size() + 12;
    }
    printf("compact %s: raw %.0f MB | delta+varint %.0f MB (unique records %.0f MB) | +zstd-3 per cell %.0f MB | id-only spatial index (delta varint) %.1f MB\n",
           argv[2], raw / 1e6, var / 1e6, uniqVar / 1e6, zst / 1e6, idIdx / 1e6);
    return 0;
}

// ---------------------------------------------------------------- spatially sorted PBF with locations on ways
struct Cls { uint16_t type = 0; };
static bool typed(const osmium::TagList& tags) {
    // same key families the classifier accepts; cheap check for the writer
    static const char* keys[] = {"highway", "railway", "building", "landuse", "natural", "waterway", "water", "leisure", "amenity", "aeroway", "man_made", "power", "place", "barrier", "boundary", "tourism", "historic", "military", "shop", "sport", "public_transport", "aerialway", "bridge", "tunnel"};
    for (auto& t : tags) for (auto k : keys) if (!strncmp(t.key(), k, strlen(k))) return true;
    return false;
}
using LocIndex = osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location>;
static int cmdLoWrite(int argc, char** argv) {
    auto t0 = Clock::now();
    struct Item { uint64_t key; size_t off; };
    osmium::memory::Buffer store{1 << 26, osmium::memory::Buffer::auto_grow::yes}; std::vector<Item> items;
    LocIndex idx; osmium::handler::NodeLocationsForWays<LocIndex> loc{idx}; loc.ignore_errors();
    struct H : osmium::handler::Handler {
        osmium::memory::Buffer& st; std::vector<Item>& it;
        H(osmium::memory::Buffer& s, std::vector<Item>& i) : st(s), it(i) {}
        void way(const osmium::Way& w) {
            if (!typed(w.tags())) return;
            osmium::Box b; for (auto& n : w.nodes()) if (n.location().valid()) b.extend(n.location()); if (!b.valid()) return;
            // sort key: level (big features last), then Morton order of the 1/32 deg cell of the bbox centre
            int L = 0; for (; L < 4; L++) { double d = LEVEL_DEG[L]; if (std::floor(b.top_right().lon() / d) - std::floor(b.bottom_left().lon() / d) <= 1 && std::floor(b.top_right().lat() / d) - std::floor(b.bottom_left().lat() / d) <= 1) break; }
            uint32_t cx = (uint32_t)((b.bottom_left().lon() + b.top_right().lon()) / 2 * 32 + 8192), cy = (uint32_t)((b.bottom_left().lat() + b.top_right().lat()) / 2 * 32 + 8192);
            uint64_t m = 0; for (int i = 0; i < 16; i++) m |= (uint64_t)((cx >> i) & 1) << (2 * i) | (uint64_t)((cy >> i) & 1) << (2 * i + 1);
            size_t off = st.committed(); st.add_item(w); st.commit(); it.push_back({(uint64_t)L << 40 | m, off});
        }
    } h{store, items};
    osmium::io::Reader rd{argv[2], osmium::osm_entity_bits::node | osmium::osm_entity_bits::way, osmium::io::read_meta::no};
    osmium::apply(rd, loc, h); rd.close();
    double tRead = since(t0); auto t1 = Clock::now();
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.key < b.key; });
    osmium::io::Header hdr; hdr.set("generator", "tsre-osm-bench");
    osmium::io::Writer wr{osmium::io::File{argv[3], "pbf,locations_on_ways=true,add_metadata=false"}, hdr, osmium::io::overwrite::allow};
    for (auto& it : items) wr(store.get<osmium::Way>(it.off));
    wr.close();
    struct stat st; stat(argv[3], &st);
    printf("lowrite: %zu typed ways, read+locations %.2fs, sort+write %.2fs, file %.0f MB, peak RSS %ld MB\n", items.size(), tRead, since(t1), st.st_size / 1e6, rssMB());
    return 0;
}

static int cmdLoTile(int argc, char** argv) {
    Pbf f; f.open(argv[2]); int th = atoi(argv[7]); double s; buildIndex(f, th, s);
    BBox q = parseBox(argv + 3);
    int32_t qx0 = q.minlon * 1e7, qx1 = q.maxlon * 1e7, qy0 = q.minlat * 1e7, qy1 = q.maxlat * 1e7;
    std::vector<size_t> hit; uint64_t z = 0;
    for (size_t i = 0; i < f.blobs.size(); i++) { auto& b = f.blobs[i]; if (b.type == 2 && !(b.x1 < qx0 || b.x0 > qx1 || b.y1 < qy0 || b.y0 > qy1)) { hit.push_back(i); z += b.zlen; } }
    std::vector<libdeflate_decompressor*> ds(th); for (auto& d : ds) d = libdeflate_alloc_decompressor();
    std::atomic<size_t> ways{0}, pts{0};
    auto t = Clock::now();
    parallelFor(hit.size(), th, [&](size_t i, int k) {
        Decoded D; decode(ds[k], f.blobs[hit[i]], D, true);
        for (auto& w : D.ways) { bool in = false; for (size_t j = 0; j < w.xy.size() && !in; j += 2) in = w.xy[j] >= qx0 && w.xy[j] <= qx1 && w.xy[j + 1] >= qy0 && w.xy[j + 1] <= qy1; if (in) { ways++; pts += w.xy.size() / 2; } }
    });
    double tq = since(t);
    printf("lotile (%d thr): index build %.2fs (%zu blobs, index %zu bytes) | tile: %zu of %zu blobs, %.2f MB z, decode %.4fs, ways %zu pts %zu\n",
           th, s, f.blobs.size(), f.blobs.size() * 32, hit.size(), f.blobs.size(), z / 1e6, tq, (size_t)ways, (size_t)pts);
    return 0;
}

int main(int argc, char** argv) {
    std::string c = argc > 1 ? argv[1] : "";
    if (c == "blobs") return cmdBlobs(argc, argv);
    if (c == "tile") return cmdTile(argc, argv);
    if (c == "route") return cmdRoute(argc, argv);
    if (c == "compact") return cmdCompact(argc, argv);
    if (c == "lowrite") return cmdLoWrite(argc, argv);
    if (c == "lotile") return cmdLoTile(argc, argv);
    fprintf(stderr, "usage: see header\n"); return 1;
}
