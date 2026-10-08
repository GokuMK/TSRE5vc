// Full conversion: Geofabrik PBF -> spatially sorted PBF with locations on ways (storage model B).
//   convert <in.pbf> <out.pbf> <tmpdir> [threads]
//   scan    <out.pbf>                     read the block bbox table from BlobHeader.indexdata
//   tile    <out.pbf> <bbox> [threads]    decode the blocks of one tile
// Keeps: all tagged ways, all relation member ways, all relations (copied verbatim), tagged nodes.
// Drops: untagged nodes (their coordinates move onto the ways), metadata.
#include <protozero/pbf_reader.hpp>
#include <protozero/pbf_writer.hpp>
#include <libdeflate.h>
#ifdef USE_MINIZ
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <malloc.h>
#include <memory>

using Clock = std::chrono::steady_clock;
static double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }
static long rssMB() { rusage ru; getrusage(RUSAGE_SELF, &ru); return ru.ru_maxrss / 1024; }
static long curRssMB() { FILE* f = fopen("/proc/self/status", "r"); char line[256]; long v = 0; while (fgets(line, sizeof line, f)) if (!strncmp(line, "RssAnon:", 8)) v = atol(line + 8) / 1024; fclose(f); return v; }
static std::atomic<long> peakAnon{0};
static void samplePeak() { long v = curRssMB(); long p = peakAnon.load(); while (v > p && !peakAnon.compare_exchange_weak(p, v)) {} }

template <class F> static void parallelFor(size_t n, int th, F&& f) {
    std::atomic<size_t> next{0}; std::vector<std::thread> ts;
    for (int t = 0; t < th; t++) ts.emplace_back([&, t] { for (size_t i; (i = next++) < n;) f(i, t); });
    for (auto& t : ts) t.join();
}

// ---------------------------------------------------------------- input
static uint64_t vi(const uint8_t*& p) { uint64_t r = 0; int s = 0; for (;;) { uint8_t c = *p++; r |= uint64_t(c & 127) << s; s += 7; if (c < 128) return r; } }
struct Blob { uint64_t start, total; const uint8_t* z; uint32_t zlen, raw; bool data; std::string_view index; };
struct Pbf {
    const uint8_t* m = nullptr; size_t size = 0; std::vector<Blob> blobs; std::vector<uint8_t> headerRaw;
    void open(const char* path) {
        int fd = ::open(path, O_RDONLY); struct stat st; fstat(fd, &st); size = st.st_size;
        m = (const uint8_t*)mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0); ::close(fd);
        size_t off = 0;
        while (off < size) {
            uint64_t start = off;
            uint32_t hl = __builtin_bswap32(*(const uint32_t*)(m + off)); off += 4;
            const uint8_t *p = m + off, *e = p + hl; uint64_t ds = 0; bool data = false; std::string_view idx;
            while (p < e) {
                uint64_t k = vi(p);
                if ((k & 7) == 0) { uint64_t v = vi(p); if ((k >> 3) == 3) ds = v; }
                else { uint64_t n = vi(p); if ((k >> 3) == 1) data = (n == 7 && !memcmp(p, "OSMData", 7)); if ((k >> 3) == 2) idx = {(const char*)p, n}; p += n; }
            }
            off += hl; const uint8_t *q = m + off, *qe = q + ds; Blob b{start, 0, nullptr, 0, 0, data, idx};
            while (q < qe) { uint64_t k = vi(q); if ((k & 7) == 0) { uint64_t v = vi(q); if ((k >> 3) == 2) b.raw = v; } else { uint64_t n = vi(q); if ((k >> 3) == 3) { b.z = q; b.zlen = n; } q += n; } }
            off += ds; b.total = off - start;
            blobs.push_back(b);
        }
    }
};
static void inflateBlob(libdeflate_decompressor* d, const Blob& b, std::vector<uint8_t>& out) {
    out.resize(b.raw);
#ifdef USE_MINIZ
    mz_ulong a = b.raw; mz_uncompress(out.data(), &a, b.z, b.zlen); (void)d;
#else
    size_t a; libdeflate_zlib_decompress(d, b.z, b.zlen, out.data(), b.raw, &a);
#endif
}

struct Block {  // one decoded PrimitiveBlock
    std::vector<uint8_t> raw; std::vector<std::string_view> s; int64_t gran = 100, latoff = 0, lonoff = 0;
    std::vector<protozero::data_view> groups;
    void parse(libdeflate_decompressor* d, const Blob& b) {
        inflateBlob(d, b, raw);
        protozero::pbf_reader blk(reinterpret_cast<const char*>(raw.data()), raw.size());
        while (blk.next()) {
            switch (blk.tag()) {
                case 1: { protozero::pbf_reader st(blk.get_view()); while (st.next(1)) { auto v = st.get_view(); s.emplace_back(v.data(), v.size()); } break; }
                case 2: groups.push_back(blk.get_view()); break;
                case 17: gran = blk.get_int32(); break;
                case 19: latoff = blk.get_int64(); break;
                case 20: lonoff = blk.get_int64(); break;
                default: blk.skip();
            }
        }
    }
    int32_t lat(int64_t v) const { return (int32_t)((latoff + gran * v) / 100); }
    int32_t lon(int64_t v) const { return (int32_t)((lonoff + gran * v) / 100); }
    // 0 unknown, 1 nodes, 2 ways, 3 relations
    int kind() const {
        for (auto& g : groups) { protozero::pbf_reader r(g); while (r.next()) { int t = r.tag(); if (t == 1 || t == 2) return 1; if (t == 3) return 2; if (t == 4) return 3; r.skip(); } }
        return 0;
    }
};

// ---------------------------------------------------------------- record encoding (temp buckets)
static void putVar(std::string& o, uint64_t v) { while (v >= 128) { o.push_back(char(v | 128)); v >>= 7; } o.push_back(char(v)); }
static uint64_t zz(int64_t v) { return (uint64_t(v) << 1) ^ uint64_t(v >> 63); }
static int64_t unzz(uint64_t v) { return int64_t(v >> 1) ^ -int64_t(v & 1); }
static uint64_t getVar(const uint8_t*& p) { return vi(p); }

static const double LEVEL_DEG[] = {1.0 / 32, 1.0 / 8, 0.5, 2.0, 8.0};
static uint64_t morton(uint32_t x, uint32_t y) { uint64_t m = 0; for (int i = 0; i < 16; i++) m |= (uint64_t)((x >> i) & 1) << (2 * i) | (uint64_t)((y >> i) & 1) << (2 * i + 1); return m; }
// Sort key and bucket for a bbox in 1e-7 deg.
static void placeBox(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint64_t& key, uint32_t& bucket) {
    int L = 0;
    for (; L < 4; L++) {
        double d = LEVEL_DEG[L];
        if (std::floor(x1 * 1e-7 / d) - std::floor(x0 * 1e-7 / d) <= 1 && std::floor(y1 * 1e-7 / d) - std::floor(y0 * 1e-7 / d) <= 1) break;
    }
    uint32_t cx = (uint32_t)(((double)x0 + x1) / 2 * 1e-7 * 32 + 8192), cy = (uint32_t)(((double)y0 + y1) / 2 * 1e-7 * 32 + 8192);
    uint64_t m = morton(cx, cy);
    key = (uint64_t)L << 40 | m;
    // L0/L1 features share 1-degree buckets (m >> 10 == morton of the 1-degree cell); bigger ones get a bucket per level.
    bucket = L <= 1 ? (uint32_t)(m >> 10) : 0xFFFFFF00u + L;
}

struct Buckets {
    std::string dir; std::mutex mx; std::unordered_map<uint32_t, FILE*> files; std::unordered_map<uint32_t, std::unique_ptr<std::mutex>> locks; uint64_t bytes = 0;
    void write(uint32_t b, const std::string& data) {
        FILE* f; std::mutex* l;
        { std::lock_guard<std::mutex> g(mx); auto it = files.find(b);
          if (it == files.end()) { char n[64]; snprintf(n, sizeof n, "/b%08x.tmp", b); f = fopen((dir + n).c_str(), "w+b"); files[b] = f; locks[b] = std::make_unique<std::mutex>(); }
          else f = it->second;
          l = locks[b].get(); bytes += data.size(); }
        std::lock_guard<std::mutex> g(*l); fwrite(data.data(), 1, data.size(), f);
    }
};
struct LocalBuckets {  // per-thread staging, flushed at 1 MB
    Buckets& B; std::unordered_map<uint32_t, std::string> buf;
    explicit LocalBuckets(Buckets& b) : B(b) {}
    void add(uint32_t b, const std::string& rec) { auto& s = buf[b]; uint32_t n = rec.size(); s.append((const char*)&n, 4); s += rec; if (s.size() > (1 << 20)) { B.write(b, s); s.clear(); } }
    void flush() { for (auto& p : buf) if (!p.second.empty()) B.write(p.first, p.second); buf.clear(); }
};

// ---------------------------------------------------------------- output
struct Out {
    FILE* f; std::mutex mx; uint64_t blobs = 0;
    void blob(const char* type, const std::string& raw, const std::string& indexdata, libdeflate_compressor* c, std::string& z) {
        z.resize(libdeflate_zlib_compress_bound(c, raw.size()));
        size_t zl = libdeflate_zlib_compress(c, raw.data(), raw.size(), z.data(), z.size()); z.resize(zl);
        std::string blobMsg; { protozero::pbf_writer w{blobMsg}; w.add_int32(2, raw.size()); w.add_bytes(3, z); }
        std::string hdr; { protozero::pbf_writer w{hdr}; w.add_string(1, type); if (!indexdata.empty()) w.add_bytes(2, indexdata); w.add_int32(3, blobMsg.size()); }
        uint32_t hl = __builtin_bswap32((uint32_t)hdr.size());
        frame.clear(); frame.append((const char*)&hl, 4); frame += hdr; frame += blobMsg;
    }
    std::string frame;
};
struct Encoded { std::string bytes; };
static std::string encodeFrame(const char* type, const std::string& raw, const std::string& indexdata, libdeflate_compressor* c) {
#ifdef USE_MINIZ
    mz_ulong zl = mz_compressBound(raw.size()); std::string z(zl, '\0');
    mz_compress2((unsigned char*)z.data(), &zl, (const unsigned char*)raw.data(), raw.size(), getenv("LEVEL") ? atoi(getenv("LEVEL")) : 6); z.resize(zl); (void)c;
#else
    std::string z(libdeflate_zlib_compress_bound(c, raw.size()), '\0');
    z.resize(libdeflate_zlib_compress(c, raw.data(), raw.size(), z.data(), z.size()));
#endif
    std::string blobMsg; { protozero::pbf_writer w{blobMsg}; w.add_int32(2, raw.size()); w.add_bytes(3, z); }
    std::string hdr; { protozero::pbf_writer w{hdr}; w.add_string(1, type); if (!indexdata.empty()) w.add_bytes(2, indexdata); w.add_int32(3, blobMsg.size()); }
    uint32_t hl = __builtin_bswap32((uint32_t)hdr.size());
    std::string frame; frame.append((const char*)&hl, 4); frame += hdr; frame += blobMsg; return frame;
}

struct Rec {  // parsed temp record, views into the bucket buffer
    uint64_t key; uint8_t kind; int64_t id; std::vector<std::pair<std::string_view, std::string_view>> tags;
    const uint8_t* geo; uint32_t n;  // node: x,y ; way: n refs then n x,y (delta zz varints)
};
static Rec parseRec(const uint8_t* p) {
    Rec r; r.kind = *p++; memcpy(&r.key, p, 8); p += 8; r.id = unzz(getVar(p));
    uint64_t nt = getVar(p);
    for (uint64_t i = 0; i < nt; i++) { uint64_t kl = getVar(p); std::string_view k((const char*)p, kl); p += kl; uint64_t vl = getVar(p); std::string_view v((const char*)p, vl); p += vl; r.tags.emplace_back(k, v); }
    r.n = r.kind == 1 ? 1 : (uint32_t)getVar(p); r.geo = p; return r;
}

struct StringTable {
    std::unordered_map<std::string_view, uint32_t> map; std::vector<std::string_view> list{std::string_view()};
    uint32_t id(std::string_view s) { auto it = map.find(s); if (it != map.end()) return it->second; uint32_t i = list.size(); list.push_back(s); map.emplace(s, i); return i; }
    void write(protozero::pbf_writer& blk) { protozero::pbf_writer st{blk, 1}; for (auto& s : list) st.add_bytes(1, s.data(), s.size()); }
};

// Encode one PrimitiveBlock from records [a,b) of one kind; returns raw block + bbox indexdata.
static void encodeBlock(const std::vector<Rec>& recs, size_t a, size_t b, std::string& raw, std::string& idx) {  // recs[a..b)
    StringTable st; int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    std::string group;
    {
        protozero::pbf_writer g{group};
        if (recs[a].kind == 1) {
            std::vector<int64_t> ids, lats, lons; std::vector<int32_t> kv; int64_t pid = 0, px = 0, py = 0;
            for (size_t i = a; i < b; i++) {
                auto& r = recs[i]; const uint8_t* p = r.geo; int32_t x = (int32_t)unzz(getVar(p)), y = (int32_t)unzz(getVar(p));
                ids.push_back(r.id - pid); pid = r.id; lons.push_back(x - px); px = x; lats.push_back(y - py); py = y;
                for (auto& t : r.tags) { kv.push_back(st.id(t.first)); kv.push_back(st.id(t.second)); } kv.push_back(0);
                x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
            protozero::pbf_writer dn{g, 2};
            dn.add_packed_sint64(1, ids.begin(), ids.end()); dn.add_packed_sint64(8, lats.begin(), lats.end());
            dn.add_packed_sint64(9, lons.begin(), lons.end()); dn.add_packed_int32(10, kv.begin(), kv.end());
        } else {
            std::vector<uint32_t> ks, vs; std::vector<int64_t> refs, lats, lons;
            for (size_t i = a; i < b; i++) {
                auto& r = recs[i]; ks.clear(); vs.clear(); refs.clear(); lats.clear(); lons.clear();
                for (auto& t : r.tags) { ks.push_back(st.id(t.first)); vs.push_back(st.id(t.second)); }
                const uint8_t* p = r.geo; for (uint32_t j = 0; j < r.n; j++) refs.push_back(unzz(getVar(p)));  // already deltas
                for (uint32_t j = 0; j < r.n; j++) {
                    int64_t dx = unzz(getVar(p)), dy = unzz(getVar(p)); lons.push_back(dx); lats.push_back(dy);
                }
                int32_t x = 0, y = 0; for (uint32_t j = 0; j < r.n; j++) { x += (int32_t)lons[j]; y += (int32_t)lats[j]; x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
                protozero::pbf_writer w{g, 3};
                w.add_int64(1, r.id); w.add_packed_uint32(2, ks.begin(), ks.end()); w.add_packed_uint32(3, vs.begin(), vs.end());
                w.add_packed_sint64(8, refs.begin(), refs.end()); w.add_packed_sint64(9, lats.begin(), lats.end()); w.add_packed_sint64(10, lons.begin(), lons.end());
            }
        }
    }
    raw.clear();
    { protozero::pbf_writer blk{raw}; st.write(blk); blk.add_message(2, group); }
    idx.assign(1, char(recs[a].kind)); idx.append((const char*)&x0, 4); idx.append((const char*)&y0, 4); idx.append((const char*)&x1, 4); idx.append((const char*)&y1, 4);
}

static int cmdConvert(int argc, char** argv) {
    const char* in = argv[2]; const char* out = argv[3]; std::string tmp = argv[4]; int th = argc > 5 ? atoi(argv[5]) : (int)std::thread::hardware_concurrency();
    auto t0 = Clock::now(); Pbf f; f.open(in);
    std::vector<libdeflate_decompressor*> ds(th); for (auto& d : ds) d = libdeflate_alloc_decompressor();
    std::vector<libdeflate_compressor*> cs(th); for (auto& c : cs) c = libdeflate_alloc_compressor(getenv("LEVEL") ? atoi(getenv("LEVEL")) : 6);

    // 1. Classify blocks (cheap: first group tag), relations are at the end of a Type_then_ID file.
    auto t = Clock::now();
    std::vector<uint8_t> kind(f.blobs.size());
    parallelFor(f.blobs.size(), th, [&](size_t i, int k) { if (!f.blobs[i].data) return; Block b; b.parse(ds[k], f.blobs[i]); kind[i] = b.kind(); });
    std::vector<size_t> nodeB, wayB, relB; for (size_t i = 0; i < kind.size(); i++) (kind[i] == 1 ? nodeB : kind[i] == 2 ? wayB : relB).push_back(i);
    double tClassify = since(t);
    // Classifying needs an inflate of every block; a production version reads the first group tag only
    // from a partial inflate, or relies on Sort.Type_then_ID and bisects. Reported separately.

    // 2. Relations: member way ids.
    t = Clock::now();
    std::vector<std::vector<int64_t>> memPer(relB.size());
    parallelFor(relB.size(), th, [&](size_t i, int k) {
        if (!f.blobs[relB[i]].data) return; Block b; b.parse(ds[k], f.blobs[relB[i]]);
        for (auto& g : b.groups) { protozero::pbf_reader gr(g); while (gr.next(4)) { protozero::pbf_reader r(gr.get_view()); std::vector<int64_t> mem; std::vector<int32_t> types;
            while (r.next()) { if (r.tag() == 9) { int64_t a = 0; for (auto v : r.get_packed_sint64()) { a += v; mem.push_back(a); } } else if (r.tag() == 10) { for (auto v : r.get_packed_enum()) types.push_back(v); } else r.skip(); }
            for (size_t j = 0; j < mem.size() && j < types.size(); j++) if (types[j] == 1) memPer[i].push_back(mem[j]); } }
    });
    std::vector<int64_t> members; for (auto& v : memPer) members.insert(members.end(), v.begin(), v.end());
    std::sort(members.begin(), members.end()); members.erase(std::unique(members.begin(), members.end()), members.end());
    double tRel = since(t);

    // 3. Nodes: per-block sorted (id, x, y) arrays; tagged nodes go to buckets.
    t = Clock::now();
    Buckets B; B.dir = tmp;
    struct NodeArr { std::vector<int64_t> id; std::vector<int32_t> xy; };
    std::vector<NodeArr> nodes(nodeB.size()); std::atomic<uint64_t> tagged{0}, nodeCount{0};
    std::vector<std::unique_ptr<LocalBuckets>> lbs; for (int k = 0; k < th; k++) lbs.push_back(std::make_unique<LocalBuckets>(B));
    parallelFor(nodeB.size(), th, [&](size_t i, int k) {
        Block b; b.parse(ds[k], f.blobs[nodeB[i]]); auto& A = nodes[i]; std::string rec;
        for (auto& g : b.groups) { protozero::pbf_reader gr(g); while (gr.next(2)) { protozero::pbf_reader dn(gr.get_view());
            std::vector<int64_t> la, lo; std::vector<int32_t> kv;
            while (dn.next()) {
                switch (dn.tag()) {
                    case 1: { int64_t a = 0; for (auto v : dn.get_packed_sint64()) { a += v; A.id.push_back(a); } break; }
                    case 8: { int64_t a = 0; for (auto v : dn.get_packed_sint64()) { a += v; la.push_back(a); } break; }
                    case 9: { int64_t a = 0; for (auto v : dn.get_packed_sint64()) { a += v; lo.push_back(a); } break; }
                    case 10: for (auto v : dn.get_packed_int32()) kv.push_back(v); break;
                    default: dn.skip();
                }
            }
            size_t base = A.xy.size() / 2; A.xy.resize(A.id.size() * 2);
            for (size_t j = 0; j < la.size(); j++) { A.xy[2 * (base + j)] = b.lon(lo[j]); A.xy[2 * (base + j) + 1] = b.lat(la[j]); }
            size_t kp = 0;
            for (size_t j = 0; j < la.size() && kp < kv.size(); j++) {
                size_t s = kp; while (kp < kv.size() && kv[kp] != 0) kp += 2; size_t e = kp; kp++;
                if (e == s) continue;
                int32_t x = A.xy[2 * (base + j)], y = A.xy[2 * (base + j) + 1]; uint64_t key; uint32_t bk; placeBox(x, y, x, y, key, bk);
                rec.clear(); rec.push_back(1); rec.append((const char*)&key, 8); putVar(rec, zz(A.id[base + j]));
                putVar(rec, (e - s) / 2);
                for (size_t q = s; q < e; q += 2) { auto& kk = b.s[kv[q]]; auto& vv = b.s[kv[q + 1]]; putVar(rec, kk.size()); rec += kk; putVar(rec, vv.size()); rec += vv; }
                putVar(rec, zz(x)); putVar(rec, zz(y));
                lbs[k]->add(bk, rec); tagged++;
            }
        } }
        A.id.shrink_to_fit(); A.xy.shrink_to_fit(); nodeCount += A.id.size();
    });
    std::vector<int64_t> firstId(nodes.size()); for (size_t i = 0; i < nodes.size(); i++) firstId[i] = nodes[i].id.empty() ? INT64_MAX : nodes[i].id.front();
    double tNodes = since(t); long rssNodes = curRssMB(); samplePeak();

    // 4. Ways: keep tagged or relation members; resolve locations; bucket by place.
    t = Clock::now();
    std::atomic<uint64_t> kept{0}, dropped{0}, missingLoc{0}, refsOut{0};
    parallelFor(wayB.size(), th, [&](size_t i, int k) {
        Block b; b.parse(ds[k], f.blobs[wayB[i]]); std::string rec; std::vector<int64_t> refs; std::vector<uint32_t> ks, vs;
        for (auto& g : b.groups) { protozero::pbf_reader gr(g); while (gr.next(3)) { protozero::pbf_reader w(gr.get_view());
            int64_t id = 0; refs.clear(); ks.clear(); vs.clear();
            while (w.next()) {
                switch (w.tag()) {
                    case 1: id = w.get_int64(); break;
                    case 2: for (auto v : w.get_packed_uint32()) ks.push_back(v); break;
                    case 3: for (auto v : w.get_packed_uint32()) vs.push_back(v); break;
                    case 8: { int64_t a = 0; for (auto v : w.get_packed_sint64()) { a += v; refs.push_back(a); } break; }
                    default: w.skip();
                }
            }
            if (ks.empty() && !std::binary_search(members.begin(), members.end(), id)) { dropped++; continue; }
            std::string geo; int32_t px = 0, py = 0, x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN; int64_t pr = 0; std::string refEnc; uint32_t n = 0;
            for (auto r : refs) {
                auto it = std::upper_bound(firstId.begin(), firstId.end(), r); if (it == firstId.begin()) { missingLoc++; continue; }
                auto& A = nodes[it - firstId.begin() - 1]; auto jt = std::lower_bound(A.id.begin(), A.id.end(), r);
                if (jt == A.id.end() || *jt != r) { missingLoc++; continue; }
                size_t j = jt - A.id.begin(); int32_t x = A.xy[2 * j], y = A.xy[2 * j + 1];
                putVar(refEnc, zz(r - pr)); pr = r; putVar(geo, zz(x - px)); putVar(geo, zz(y - py)); px = x; py = y; n++;
                x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
            if (n == 0) { dropped++; continue; }
            uint64_t key; uint32_t bk; placeBox(x0, y0, x1, y1, key, bk);
            rec.clear(); rec.push_back(2); rec.append((const char*)&key, 8); putVar(rec, zz(id)); putVar(rec, ks.size());
            for (size_t q = 0; q < ks.size() && q < vs.size(); q++) { auto& kk = b.s[ks[q]]; auto& vv = b.s[vs[q]]; putVar(rec, kk.size()); rec += kk; putVar(rec, vv.size()); rec += vv; }
            putVar(rec, n); rec += refEnc; rec += geo;
            lbs[k]->add(bk, rec); kept++; refsOut += n;
        } }
    });
    for (auto& l : lbs) l->flush();
    double tWays = since(t); long rssWays = curRssMB(); samplePeak();
    { std::vector<NodeArr>().swap(nodes); std::vector<int64_t>().swap(firstId); }
    malloc_trim(0);
    long rssFreed = curRssMB();

    // 5. Per bucket: load, sort, encode blocks in parallel, write in order. Then relation blobs verbatim.
    t = Clock::now();
    FILE* of = fopen(out, "wb");
    {
        // header block with LocationsOnWays
        std::string hb; { protozero::pbf_writer h{hb};
            if (!f.blobs.empty() && !f.blobs[0].data) { Block hdr; hdr.raw.resize(f.blobs[0].raw); size_t a; libdeflate_zlib_decompress(ds[0], f.blobs[0].z, f.blobs[0].zlen, hdr.raw.data(), hdr.raw.size(), &a);
                protozero::pbf_reader r(reinterpret_cast<const char*>(hdr.raw.data()), hdr.raw.size()); while (r.next()) { if (r.tag() == 1) { auto v = r.get_view(); h.add_message(1, v.data(), v.size()); } else r.skip(); } }
            h.add_string(4, "OsmSchema-V0.6"); h.add_string(4, "DenseNodes"); h.add_string(5, "LocationsOnWays"); h.add_string(16, "tsre-osm-convert-bench"); }
        std::string fr = encodeFrame("OSMHeader", hb, "", cs[0]); fwrite(fr.data(), 1, fr.size(), of);
    }
    std::vector<uint32_t> bids; for (auto& p : B.files) bids.push_back(p.first); std::sort(bids.begin(), bids.end());
    uint64_t outBlocks = 0, maxBucket = 0; const size_t G = 4;
    struct Item { uint64_t sort; uint32_t off; };
    for (size_t g0 = 0; g0 < bids.size(); g0 += G) {
        size_t g1 = std::min(bids.size(), g0 + G);
        std::vector<std::vector<uint8_t>> bufs(g1 - g0); std::vector<std::vector<Item>> items(g1 - g0);
        parallelFor(g1 - g0, th, [&](size_t i, int) {
            FILE* bf = B.files[bids[g0 + i]]; fflush(bf); long sz = ftell(bf); auto& buf = bufs[i];
            buf.resize(sz); fseek(bf, 0, SEEK_SET); fread(buf.data(), 1, sz, bf); fclose(bf);
            for (uint32_t p = 0; p < sz;) { uint32_t n; memcpy(&n, &buf[p], 4); uint64_t key; memcpy(&key, &buf[p + 5], 8);
                items[i].push_back({(uint64_t)buf[p + 4] << 56 | key, p + 4}); p += 4 + n; }
            std::sort(items[i].begin(), items[i].end(), [](const Item& a, const Item& b) { return a.sort < b.sort; });
        });
        for (auto& b : bufs) maxBucket = std::max<uint64_t>(maxBucket, b.size());
        samplePeak();
        struct Range { size_t bucket, a, e; };
        std::vector<Range> ranges;
        for (size_t i = 0; i < items.size(); i++) { auto& it = items[i];
            for (size_t a = 0; a < it.size();) { size_t e = a; while (e < it.size() && e - a < 8000 && (it[e].sort >> 56) == (it[a].sort >> 56)) e++; ranges.push_back({i, a, e}); a = e; } }
        std::vector<std::string> frames(ranges.size());
        parallelFor(ranges.size(), th, [&](size_t r, int k) {
            auto& R = ranges[r]; std::vector<Rec> recs; recs.reserve(R.e - R.a);
            for (size_t j = R.a; j < R.e; j++) recs.push_back(parseRec(bufs[R.bucket].data() + items[R.bucket][j].off));
            std::string raw, idx; encodeBlock(recs, 0, recs.size(), raw, idx); frames[r] = encodeFrame("OSMData", raw, idx, cs[k]);
        });
        for (auto& fr : frames) fwrite(fr.data(), 1, fr.size(), of);
        outBlocks += frames.size();
    }
    for (auto i : relB) if (f.blobs[i].data) fwrite(f.m + f.blobs[i].start, 1, f.blobs[i].total, of);
    fclose(of);
    for (auto bid : bids) { char n[64]; snprintf(n, sizeof n, "/b%08x.tmp", bid); unlink((tmp + n).c_str()); }
    double tWrite = since(t);
    struct stat st; stat(out, &st);
    printf("convert %s (%d thr): classify %.2fs | relations %.2fs (%zu member ways) | nodes %.2fs (%lu nodes, %lu tagged; RSS %ld MB) | ways %.2fs (kept %lu, dropped %lu, refs %lu, missing locs %lu; RSS %ld MB, after free %ld MB) | sort+encode+write %.2fs (%zu buckets, largest %.0f MB, %lu blocks) | temp %.0f MB | total %.2fs | out %.0f MB (in %.0f MB) | peak anon %ld MB, peak RSS incl. mapped input %ld MB\n",
           in, th, tClassify, tRel, members.size(), tNodes, (unsigned long)nodeCount, (unsigned long)tagged, rssNodes, tWays, (unsigned long)kept, (unsigned long)dropped,
           (unsigned long)refsOut, (unsigned long)missingLoc, rssWays, rssFreed, tWrite, bids.size(), maxBucket / 1e6, (unsigned long)outBlocks, B.bytes / 1e6, since(t0), st.st_size / 1e6, f.size / 1e6, peakAnon.load(), rssMB());
    return 0;
}

// ---------------------------------------------------------------- reading the converted file
struct IndexedBlob { uint8_t kind; int32_t x0, y0, x1, y1; size_t blob; };
static std::vector<IndexedBlob> readIndex(const Pbf& f) {
    std::vector<IndexedBlob> v;
    for (size_t i = 0; i < f.blobs.size(); i++) { auto& b = f.blobs[i]; if (!b.data) continue; IndexedBlob e{3, INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX, i};
        if (b.index.size() == 17) { e.kind = b.index[0]; memcpy(&e.x0, b.index.data() + 1, 4); memcpy(&e.y0, b.index.data() + 5, 4); memcpy(&e.x1, b.index.data() + 9, 4); memcpy(&e.y1, b.index.data() + 13, 4); }
        v.push_back(e); }
    return v;
}
static int cmdScan(int argc, char** argv) {
    auto t = Clock::now(); Pbf f; f.open(argv[2]); auto idx = readIndex(f); double s = since(t);
    size_t n[4] = {}; for (auto& e : idx) n[e.kind]++;
    printf("scan %s: %.3fs, %zu blocks (nodes %zu, ways %zu, relations %zu), table %zu bytes\n", argv[2], s, idx.size(), n[1], n[2], n[3], idx.size() * sizeof(IndexedBlob));
    return 0;
}
static int cmdTile(int argc, char** argv) {
    auto t0 = Clock::now(); Pbf f; f.open(argv[2]); auto idx = readIndex(f); double tOpen = since(t0);
    int32_t qx0 = atof(argv[3]) * 1e7, qy0 = atof(argv[4]) * 1e7, qx1 = atof(argv[5]) * 1e7, qy1 = atof(argv[6]) * 1e7; int th = argc > 7 ? atoi(argv[7]) : 12;
    std::vector<size_t> hit; uint64_t z = 0;
    for (auto& e : idx) if (e.kind != 3 && !(e.x1 < qx0 || e.x0 > qx1 || e.y1 < qy0 || e.y0 > qy1)) { hit.push_back(e.blob); z += f.blobs[e.blob].zlen; }
    std::vector<libdeflate_decompressor*> ds(th); for (auto& d : ds) d = libdeflate_alloc_decompressor();
    std::atomic<uint64_t> ways{0}, pts{0}, nodesIn{0};
    auto t = Clock::now();
    parallelFor(hit.size(), th, [&](size_t i, int k) {
        Block b; b.parse(ds[k], f.blobs[hit[i]]);
        for (auto& g : b.groups) { protozero::pbf_reader gr(g); while (gr.next()) {
            if (gr.tag() == 3) { protozero::pbf_reader w(gr.get_view()); std::vector<int64_t> la, lo;
                while (w.next()) { if (w.tag() == 9) { int64_t a = 0; for (auto v : w.get_packed_sint64()) { a += v; la.push_back(a); } } else if (w.tag() == 10) { int64_t a = 0; for (auto v : w.get_packed_sint64()) { a += v; lo.push_back(a); } } else w.skip(); }
                bool in = false; for (size_t j = 0; j < la.size() && !in; j++) { int32_t x = b.lon(lo[j]), y = b.lat(la[j]); in = x >= qx0 && x <= qx1 && y >= qy0 && y <= qy1; }
                if (in) { ways++; pts += la.size(); } }
            else if (gr.tag() == 2) { protozero::pbf_reader dn(gr.get_view()); std::vector<int64_t> la, lo;
                while (dn.next()) { if (dn.tag() == 8) { int64_t a = 0; for (auto v : dn.get_packed_sint64()) { a += v; la.push_back(a); } } else if (dn.tag() == 9) { int64_t a = 0; for (auto v : dn.get_packed_sint64()) { a += v; lo.push_back(a); } } else dn.skip(); }
                for (size_t j = 0; j < la.size(); j++) { int32_t x = b.lon(lo[j]), y = b.lat(la[j]); if (x >= qx0 && x <= qx1 && y >= qy0 && y <= qy1) nodesIn++; } }
            else gr.skip(); } }
    });
    printf("tile (%d thr): open+index %.4fs | %zu of %zu blocks, %.1f MB z | decode %.4fs | ways %lu (pts %lu), tagged nodes %lu\n", th, tOpen, hit.size(), idx.size(), z / 1e6, since(t), (unsigned long)ways, (unsigned long)pts, (unsigned long)nodesIn);
    return 0;
}

int main(int argc, char** argv) {
    std::string c = argc > 1 ? argv[1] : "";
    if (c == "convert") return cmdConvert(argc, argv);
    if (c == "scan") return cmdScan(argc, argv);
    if (c == "tile") return cmdTile(argc, argv);
    fprintf(stderr, "usage: see header\n"); return 1;
}
