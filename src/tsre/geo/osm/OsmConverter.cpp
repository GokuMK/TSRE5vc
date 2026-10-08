/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/geo/osm/OsmConverter.h>
#include <tsre/geo/osm/OsmPbf.h>
#include <tsre/geo/osm/OsmSortedFormat.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Osm {

namespace {

using Clock = std::chrono::steady_clock;
using Sorted::Placement;
double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

constexpr size_t MaxBlockEntities = 8000;
constexpr size_t MaxBlockRecordBytes = 4 * 1024 * 1024;  // keeps encoded blocks far below the 32 MiB limit
constexpr size_t StagingFlushBytes = 256 * 1024;
// Buckets are loaded, sorted and encoded together up to this much temporary data.
constexpr uint64_t WriteGroupBytes = 1024ull * 1024 * 1024;
constexpr size_t WriteGroupBuckets = 16;

// ---------------------------------------------------------------- shared state

struct Job {
    const ConvertOptions &options;
    const ConvertProgress &progress;
    const std::atomic_bool *cancel;
    int threads = 1;
    std::mutex errorMutex;
    QString firstError;
    std::atomic_bool failed{false};

    Job(const ConvertOptions &o, const ConvertProgress &p, const std::atomic_bool *c) : options(o), progress(p), cancel(c) {}
    bool cancelled() const { return cancel && cancel->load(); }
    bool stop() const { return failed.load() || cancelled(); }
    void fail(const QString &e) {
        std::lock_guard<std::mutex> l(errorMutex);
        if (!failed.exchange(true)) firstError = e;
    }
    // Runs body(i, worker) for i < n on the worker threads; the calling thread reports progress.
    bool parallel(size_t n, ConvertPhase phase, const std::function<bool(size_t, int)> &body, bool report = true) {
        std::atomic<size_t> next{0}, done{0};
        std::vector<std::thread> workers;
        const int count = int(std::min<size_t>(size_t(threads), std::max<size_t>(n, 1)));
        for (int w = 0; w < count; ++w)
            workers.emplace_back([&, w] {
                for (size_t i; !stop() && (i = next++) < n; ++done)
                    if (!body(i, w)) return;
            });
        while (done.load() < n && !stop()) {
            if (progress && report) progress(phase, n ? double(done.load()) / n : 1.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        for (auto &t : workers) t.join();
        if (progress && report && !stop()) progress(phase, 1.0);
        return !stop();
    }
};

// ---------------------------------------------------------------- temporary records
// u8 kind | u64 key | 4 x i32 bbox | zz id | n tags (len key, len value)* | geometry
//   node: zz x, zz y
//   way: n, n x zz ref delta, n x (zz dx, zz dy)
//   relation: n, n x (u8 type, zz ref delta, len role)

void putVar(std::string &o, uint64_t v) { Proto::Writer::putVarint(o, v); }
void putZz(std::string &o, int64_t v) { putVar(o, Proto::zigzag(v)); }
void putStr(std::string &o, std::string_view s) { putVar(o, s.size()); o.append(s.data(), s.size()); }
template <class T> void putRaw(std::string &o, const T &v) { o.append(reinterpret_cast<const char *>(&v), sizeof(T)); }
constexpr size_t RecordHeader = 1 + 8 + 16;

struct RecordReader {
    const uint8_t *p, *end;
    bool ok = true;
    uint64_t var() {
        uint64_t r = 0;
        for (int s = 0; s < 64; s += 7) {
            if (p >= end) { ok = false; return 0; }
            const uint8_t c = *p++;
            r |= uint64_t(c & 0x7f) << s;
            if (c < 0x80) return r;
        }
        ok = false;
        return 0;
    }
    int64_t zz() { return Proto::unzigzag(var()); }
    std::string_view str() {
        const uint64_t n = var();
        if (!ok || n > uint64_t(end - p)) { ok = false; return {}; }
        std::string_view s(reinterpret_cast<const char *>(p), size_t(n)); p += n;
        return s;
    }
};

void beginRecord(std::string &rec, ItemType kind, int64_t id, const PrimitiveBlock &b, uint32_t tagFirst, uint32_t tagCount) {
    rec.clear();
    rec.push_back(char(kind));
    rec.append(RecordHeader - 1, '\0');  // key and bbox, patched when placed
    putZz(rec, id);
    putVar(rec, tagCount);
    for (uint32_t t = 0; t < tagCount; ++t) { const Tag tag = b.tag(tagFirst + t); putStr(rec, tag.key); putStr(rec, tag.value); }
}
void patchRecord(std::string &rec, const Placement &p, const Box &box) {
    std::memcpy(&rec[1], &p.key, 8);
    std::memcpy(&rec[9], &box.minX, 4); std::memcpy(&rec[13], &box.minY, 4);
    std::memcpy(&rec[17], &box.maxX, 4); std::memcpy(&rec[21], &box.maxY, 4);
}
Box recordBox(const uint8_t *rec) {
    Box b;
    std::memcpy(&b.minX, rec + 9, 4); std::memcpy(&b.minY, rec + 13, 4);
    std::memcpy(&b.maxX, rec + 17, 4); std::memcpy(&b.maxY, rec + 21, 4);
    return b;
}

// ---------------------------------------------------------------- buckets on disk

class Buckets {
public:
    explicit Buckets(QString dir) : dir_(std::move(dir)) {}
    QString path(uint32_t bucket) const { return dir_ + QStringLiteral("/b%1.tmp").arg(bucket, 8, 16, QLatin1Char('0')); }
    bool append(uint32_t bucket, const std::string &data) {
        std::mutex *m;
        {
            std::lock_guard<std::mutex> l(mutex_);
            auto &slot = locks_[bucket];
            if (!slot) slot = std::make_unique<std::mutex>();
            m = slot.get();
            bytes_ += data.size();
            sizes_[bucket] += data.size();
        }
        std::lock_guard<std::mutex> l(*m);
        // Opened per flush: a continent has thousands of buckets, more than the open-file limit.
        QFile f(path(bucket));
        return f.open(QIODevice::WriteOnly | QIODevice::Append) && f.write(data.data(), qint64(data.size())) == qint64(data.size());
    }
    std::vector<uint32_t> ids() const {
        std::vector<uint32_t> v;
        for (const auto &p : locks_) v.push_back(p.first);
        std::sort(v.begin(), v.end());
        return v;
    }
    uint64_t bytes() const { return bytes_; }
    uint64_t bytes(uint32_t bucket) const { auto it = sizes_.find(bucket); return it == sizes_.end() ? 0 : it->second; }

private:
    QString dir_;
    std::mutex mutex_;
    std::unordered_map<uint32_t, std::unique_ptr<std::mutex>> locks_;
    std::unordered_map<uint32_t, uint64_t> sizes_;
    uint64_t bytes_ = 0;
};

class Staging {
public:
    Staging(Buckets &b, Job &job) : buckets_(b), job_(job) {}
    bool add(uint32_t bucket, const std::string &rec) {
        std::string &s = pending_[bucket];
        const uint32_t n = uint32_t(rec.size());
        putRaw(s, n);
        s += rec;
        if (s.size() < StagingFlushBytes) return true;
        const bool ok = buckets_.append(bucket, s);
        s.clear();
        if (!ok) job_.fail(QStringLiteral("Cannot write temporary conversion data"));
        return ok;
    }
    bool flush() {
        for (auto &p : pending_)
            if (!p.second.empty() && !buckets_.append(p.first, p.second)) { job_.fail(QStringLiteral("Cannot write temporary conversion data")); return false; }
        pending_.clear();
        return true;
    }

private:
    Buckets &buckets_;
    Job &job_;
    std::unordered_map<uint32_t, std::string> pending_;
};

// ---------------------------------------------------------------- node locations

class NodeTable {
public:
    void resize(size_t blocks) { ids_.resize(blocks); locs_.resize(blocks); }
    std::vector<int64_t> &ids(size_t b) { return ids_[b]; }
    std::vector<Location> &locations(size_t b) { return locs_[b]; }
    // Sorted input gives ascending, disjoint blocks; anything else is merged into one sorted block.
    void finish() {
        bool sorted = true;
        int64_t last = std::numeric_limits<int64_t>::min();
        for (const auto &v : ids_) {
            if (v.empty()) continue;
            if (v.front() <= last || !std::is_sorted(v.begin(), v.end())) { sorted = false; break; }
            last = v.back();
        }
        if (!sorted) {
            std::vector<std::pair<int64_t, Location>> all;
            for (size_t b = 0; b < ids_.size(); ++b) {
                for (size_t i = 0; i < ids_[b].size(); ++i) all.push_back({ids_[b][i], locs_[b][i]});
                std::vector<int64_t>().swap(ids_[b]); std::vector<Location>().swap(locs_[b]);
            }
            std::sort(all.begin(), all.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            ids_.assign(1, {}); locs_.assign(1, {});
            ids_[0].reserve(all.size()); locs_[0].reserve(all.size());
            for (const auto &p : all) { ids_[0].push_back(p.first); locs_[0].push_back(p.second); }
        }
        // Drop empty blocks so the first-id search stays exact.
        for (size_t b = 0; b < ids_.size();) {
            if (ids_[b].empty()) { ids_.erase(ids_.begin() + b); locs_.erase(locs_.begin() + b); } else ++b;
        }
        first_.clear();
        for (const auto &v : ids_) first_.push_back(v.front());
    }
    bool find(int64_t id, Location &out) const {
        auto it = std::upper_bound(first_.begin(), first_.end(), id);
        if (it == first_.begin()) return false;
        const size_t b = size_t(it - first_.begin() - 1);
        const auto &v = ids_[b];
        auto j = std::lower_bound(v.begin(), v.end(), id);
        if (j == v.end() || *j != id) return false;
        out = locs_[b][size_t(j - v.begin())];
        return true;
    }
    uint64_t bytes() const {
        uint64_t n = 0;
        for (size_t b = 0; b < ids_.size(); ++b) n += ids_[b].capacity() * sizeof(int64_t) + locs_[b].capacity() * sizeof(Location);
        return n;
    }
    void clear() { std::vector<std::vector<int64_t>>().swap(ids_); std::vector<std::vector<Location>>().swap(locs_); first_.clear(); }

private:
    std::vector<std::vector<int64_t>> ids_;
    std::vector<std::vector<Location>> locs_;
    std::vector<int64_t> first_;
};

// Index of an id in a sorted unique vector, or npos.
size_t indexOf(const std::vector<int64_t> &v, int64_t id) {
    auto it = std::lower_bound(v.begin(), v.end(), id);
    return it != v.end() && *it == id ? size_t(it - v.begin()) : size_t(-1);
}

// ---------------------------------------------------------------- block classification

struct BlockSets { std::vector<size_t> nodes, ways, relations; };

bool classifyBlocks(const PbfFile &f, Job &job, BlockSets &sets) {
    std::vector<size_t> data;
    for (size_t i = 0; i < f.blobs().size(); ++i) if (f.blobs()[i].type == BlobType::Data) data.push_back(i);
    std::vector<uint32_t> parts(data.size(), 0);
    auto probe = [&](size_t i, std::vector<uint8_t> &raw) -> bool {
        QString e;
        if (!f.readBlob(data[i], raw, e)) { job.fail(e); return false; }
        parts[i] = blockParts(raw);
        if (!parts[i]) { job.fail(QStringLiteral("Malformed PBF block %1").arg(data[i])); return false; }
        return true;
    };
    if (f.header().hasFeature(QStringLiteral("Sort.Type_then_ID"))) {
        // Nodes, then ways, then relations: find the two boundaries by bisection.
        std::vector<uint8_t> raw;
        auto firstWith = [&](uint32_t mask) -> size_t {
            size_t lo = 0, hi = data.size();
            while (lo < hi) {
                const size_t mid = (lo + hi) / 2;
                if (!parts[mid] && !probe(mid, raw)) return size_t(-1);
                if (parts[mid] & mask) hi = mid; else lo = mid + 1;
            }
            return lo;
        };
        const size_t firstLater = firstWith(DecodeWays | DecodeRelations);
        const size_t firstRelation = firstLater == size_t(-1) ? size_t(-1) : firstWith(DecodeRelations);
        if (firstLater == size_t(-1) || firstRelation == size_t(-1)) return false;
        for (size_t i : {firstLater, firstRelation})
            if (i < data.size() && !parts[i] && !probe(i, raw)) return false;
        for (size_t i = 0; i < data.size(); ++i) {
            if (parts[i]) continue;
            parts[i] = i < firstLater ? DecodeNodes : i < firstRelation ? DecodeWays : DecodeRelations;
        }
    } else {
        std::vector<std::vector<uint8_t>> scratch(size_t(job.threads));
        if (!job.parallel(data.size(), ConvertPhase::Scan, [&](size_t i, int w) { return probe(i, scratch[size_t(w)]); })) return false;
    }
    for (size_t i = 0; i < data.size(); ++i) {
        if (parts[i] & DecodeNodes) sets.nodes.push_back(data[i]);
        if (parts[i] & DecodeWays) sets.ways.push_back(data[i]);
        if (parts[i] & DecodeRelations) sets.relations.push_back(data[i]);
    }
    return true;
}

// ---------------------------------------------------------------- output blocks

struct Item { uint64_t sort; int64_t id; uint64_t offset; uint32_t size; };

struct ParsedRecord {
    ItemType kind = ItemType::Node;
    Box box;
    int64_t id = 0;
    std::vector<Tag> tags;
    Location location;
    std::vector<int64_t> refs;
    std::vector<Location> locations;
    std::vector<BlockBuilder::MemberIn> members;
};

bool parseRecord(const uint8_t *data, size_t size, ParsedRecord &r) {
    if (size < RecordHeader) return false;
    r.kind = ItemType(data[0]);
    r.box = recordBox(data);
    RecordReader in{data + RecordHeader, data + size};
    r.id = in.zz();
    r.tags.clear(); r.refs.clear(); r.locations.clear(); r.members.clear();
    const uint64_t tags = in.var();
    for (uint64_t i = 0; in.ok && i < tags; ++i) { const auto k = in.str(); const auto v = in.str(); r.tags.push_back({k, v}); }
    if (r.kind == ItemType::Node) {
        r.location.x = int32_t(in.zz()); r.location.y = int32_t(in.zz());
    } else if (r.kind == ItemType::Way) {
        const uint64_t n = in.var();
        int64_t ref = 0;
        for (uint64_t i = 0; in.ok && i < n; ++i) { ref += in.zz(); r.refs.push_back(ref); }
        int64_t x = 0, y = 0;
        for (uint64_t i = 0; in.ok && i < n; ++i) { x += in.zz(); y += in.zz(); r.locations.push_back({int32_t(x), int32_t(y)}); }
    } else {
        const uint64_t n = in.var();
        int64_t ref = 0;
        for (uint64_t i = 0; in.ok && i < n; ++i) {
            if (in.p >= in.end) { in.ok = false; break; }
            const uint8_t type = *in.p++;
            ref += in.zz();
            r.members.push_back({ref, ItemType(type), in.str()});
        }
    }
    return in.ok && in.p == in.end;
}

bool writeBuckets(Buckets &buckets, Job &job, QFile &out, ConvertStats &stats) {
    const std::vector<uint32_t> ids = buckets.ids();
    size_t written = 0;
    for (size_t g0 = 0, g1 = 0; g0 < ids.size() && !job.stop(); g0 = g1) {
        uint64_t groupBytes = 0;
        for (g1 = g0; g1 < ids.size() && g1 - g0 < WriteGroupBuckets && (g1 == g0 || groupBytes + buckets.bytes(ids[g1]) <= WriteGroupBytes); ++g1)
            groupBytes += buckets.bytes(ids[g1]);
        std::vector<QByteArray> data(g1 - g0);
        std::vector<std::vector<Item>> items(g1 - g0);
        const bool loaded = job.parallel(data.size(), ConvertPhase::Write, [&](size_t i, int) {
            QFile f(buckets.path(ids[g0 + i]));
            if (!f.open(QIODevice::ReadOnly)) { job.fail(QStringLiteral("Cannot read temporary conversion data")); return false; }
            data[i] = f.readAll();
            f.close();
            f.remove();
            const auto *p = reinterpret_cast<const uint8_t *>(data[i].constData());
            const size_t n = size_t(data[i].size());
            for (size_t off = 0; off + 4 <= n;) {
                uint32_t len; std::memcpy(&len, p + off, 4);
                if (len < RecordHeader || off + 4 + len > n) { job.fail(QStringLiteral("Corrupt temporary conversion data")); return false; }
                uint64_t key; std::memcpy(&key, p + off + 5, 8);
                RecordReader idReader{p + off + 4 + RecordHeader, p + off + 4 + len};
                items[i].push_back({uint64_t(p[off + 4]) << 56 | key, idReader.zz(), uint64_t(off + 4), len});
                off += 4 + len;
            }
            std::sort(items[i].begin(), items[i].end(), [](const Item &a, const Item &b) { return a.sort != b.sort ? a.sort < b.sort : a.id < b.id; });
            return true;
        }, false);
        if (!loaded) return false;
        struct Range { size_t bucket, a, e; };
        std::vector<Range> ranges;
        for (size_t i = 0; i < items.size(); ++i) {
            const auto &v = items[i];
            for (size_t a = 0; a < v.size();) {
                size_t e = a, bytes = 0;
                while (e < v.size() && e - a < MaxBlockEntities && (v[e].sort >> 56) == (v[a].sort >> 56) && (e == a || bytes + v[e].size <= MaxBlockRecordBytes))
                    bytes += v[e++].size;
                ranges.push_back({i, a, e});
                a = e;
            }
        }
        std::vector<std::string> frames(ranges.size());
        std::vector<ParsedRecord> parsed(size_t(job.threads));
        std::vector<BlockBuilder> builders(size_t(job.threads));
        const bool ok = job.parallel(ranges.size(), ConvertPhase::Write, [&](size_t r, int w) {
            const Range &range = ranges[r];
            const auto *base = reinterpret_cast<const uint8_t *>(data[range.bucket].constData());
            BlockBuilder &b = builders[size_t(w)];
            ParsedRecord &rec = parsed[size_t(w)];
            b.clear();
            Sorted::BlockIndex index;
            for (size_t j = range.a; j < range.e; ++j) {
                const Item &it = items[range.bucket][j];
                if (!parseRecord(base + it.offset, it.size, rec)) { job.fail(QStringLiteral("Corrupt temporary conversion data")); return false; }
                index.kind = rec.kind;
                index.bounds.extend(rec.box);
                if (rec.kind == ItemType::Node) b.addNode(rec.id, rec.location, rec.tags.data(), rec.tags.size());
                else if (rec.kind == ItemType::Way) b.addWay(rec.id, rec.tags.data(), rec.tags.size(), rec.refs.data(), rec.locations.data(), rec.refs.size());
                else b.addRelation(rec.id, rec.tags.data(), rec.tags.size(), rec.members.data(), rec.members.size());
            }
            // One unplaced record makes the whole block unbounded.
            for (size_t j = range.a; j < range.e; ++j)
                if (!recordBox(base + items[range.bucket][j].offset).valid()) { index.bounds = Box(); break; }
            std::string raw;
            b.build(raw);
            frames[r] = encodeBlobFrame("OSMData", raw, Sorted::encodeIndex(index), job.options.compressionLevel);
            if (frames[r].empty()) { job.fail(QStringLiteral("Cannot encode an output block")); return false; }
            return true;
        }, false);
        if (!ok) return false;
        for (const std::string &frame : frames) {
            if (out.write(frame.data(), qint64(frame.size())) != qint64(frame.size())) { job.fail(QStringLiteral("Cannot write %1: %2").arg(out.fileName(), out.errorString())); return false; }
            stats.outputBytes += frame.size();
        }
        stats.outputBlocks += frames.size();
        written += g1 - g0;
        if (job.progress) job.progress(ConvertPhase::Write, double(written) / ids.size());
    }
    return !job.stop();
}

}

ConvertEstimate estimateConversion(int64_t sourceBytes, int threads) {
    // Poland (2.1 GB): 3.7 GiB node table, 6.2 GiB temporary data, 2.2 GiB output at level 1;
    // staging buffers and up to 1 GiB of write groups on top.
    if (threads <= 0) threads = int(std::max(1u, std::thread::hardware_concurrency()));
    ConvertEstimate e;
    // Staging buffers and write groups only fill up for large sources.
    const int64_t buffers = std::min<int64_t>(sourceBytes / 2, 256ll * 1024 * 1024 + int64_t(threads) * 64 * 1024 * 1024);
    e.memoryBytes = sourceBytes * 2 + buffers + 32ll * 1024 * 1024;
    e.tempBytes = sourceBytes * 7 / 2;  // Poland: 6.65 GB from 2.1 GB
    e.outputBytes = sourceBytes + sourceBytes / 6;
    return e;
}

bool convertPbf(const QString &sourcePath, const QString &outputPath, const ConvertOptions &options,
                ConvertStats &stats, QString &error, const ConvertProgress &progress, const std::atomic_bool *cancel) {
    const auto start = Clock::now();
    stats = ConvertStats();
    Job job(options, progress, cancel);
    job.threads = options.threads > 0 ? options.threads : int(std::max(1u, std::thread::hardware_concurrency()));

    PbfFile source;
    if (!source.open(sourcePath, error)) return false;
    if (source.header().hasFeature(Sorted::FeatureMarker)) { error = QStringLiteral("%1 is already converted").arg(sourcePath); return false; }

    const QString partPath = outputPath + QStringLiteral(".part");
    const QString tempPath = options.tempDirectory.isEmpty() ? outputPath + QStringLiteral(".tmp")
                                                             : QDir(options.tempDirectory).filePath(QFileInfo(outputPath).fileName() + QStringLiteral(".tmp"));
    QDir(tempPath).removeRecursively();
    if (!QDir().mkpath(tempPath)) { error = QStringLiteral("Cannot create %1").arg(tempPath); return false; }
    QFile out(partPath);
    auto cleanup = [&](bool keepOutput) {
        if (out.isOpen()) out.close();
        if (!keepOutput) QFile::remove(partPath);
        QDir(tempPath).removeRecursively();
    };
    auto finishError = [&]() {
        error = job.cancelled() ? QStringLiteral("Conversion cancelled") : job.firstError;
        cleanup(false);
        return false;
    };

    Buckets buckets(tempPath);
    std::vector<std::unique_ptr<Staging>> staging;
    for (int w = 0; w < job.threads; ++w) staging.push_back(std::make_unique<Staging>(buckets, job));
    auto flushStaging = [&] { for (auto &s : staging) if (!s->flush()) return false; return true; };

    // 1. Which blocks hold nodes, ways and relations.
    auto t = Clock::now();
    if (progress) progress(ConvertPhase::Scan, 0);
    BlockSets sets;
    if (!classifyBlocks(source, job, sets)) return finishError();
    stats.scanSeconds = since(t);

    // 2. Relations: kept in memory as records until their bbox is known; collect members.
    t = Clock::now();
    std::vector<std::vector<std::string>> relRecords(sets.relations.size());
    std::vector<std::vector<int64_t>> relIds(sets.relations.size()), wayMembers(sets.relations.size()), nodeMembers(sets.relations.size());
    std::vector<PrimitiveBlock> blocks(size_t(job.threads));
    if (!job.parallel(sets.relations.size(), ConvertPhase::Relations, [&](size_t i, int w) {
            PrimitiveBlock &b = blocks[size_t(w)];
            QString e;
            if (!source.readBlock(sets.relations[i], b, DecodeRelations, e)) { job.fail(e); return false; }
            std::string rec;
            for (const auto &r : b.relations) {
                beginRecord(rec, ItemType::Relation, r.id, b, r.tagFirst, r.tagCount);
                putVar(rec, r.memberCount);
                int64_t prev = 0;
                for (uint32_t m = 0; m < r.memberCount; ++m) {
                    const Member &mem = b.members[r.memberFirst + m];
                    rec.push_back(char(mem.type));
                    putZz(rec, int64_t(uint64_t(mem.ref) - uint64_t(prev)));
                    prev = mem.ref;
                    putStr(rec, b.role(mem));
                    if (mem.type == ItemType::Way) wayMembers[i].push_back(mem.ref);
                    else if (mem.type == ItemType::Node) nodeMembers[i].push_back(mem.ref);
                }
                relRecords[i].push_back(rec);
                relIds[i].push_back(r.id);
            }
            return true;
        }))
        return finishError();
    auto flatten = [](std::vector<std::vector<int64_t>> &parts) {
        std::vector<int64_t> all;
        for (auto &p : parts) { all.insert(all.end(), p.begin(), p.end()); std::vector<int64_t>().swap(p); }
        std::sort(all.begin(), all.end());
        all.erase(std::unique(all.begin(), all.end()), all.end());
        return all;
    };
    const std::vector<int64_t> memberWays = flatten(wayMembers), memberNodes = flatten(nodeMembers);
    stats.relationSeconds = since(t);

    // 3. Nodes: location table for every node; tagged nodes become output records.
    t = Clock::now();
    NodeTable nodes;
    nodes.resize(sets.nodes.size());
    std::atomic<uint64_t> sourceNodes{0}, taggedNodes{0}, tagCount{0};
    if (!job.parallel(sets.nodes.size(), ConvertPhase::Nodes, [&](size_t i, int w) {
            PrimitiveBlock &b = blocks[size_t(w)];
            QString e;
            if (!source.readBlock(sets.nodes[i], b, DecodeNodes, e)) { job.fail(e); return false; }
            auto &ids = nodes.ids(i);
            auto &locs = nodes.locations(i);
            ids.reserve(b.nodes.size()); locs.reserve(b.nodes.size());
            std::string rec;
            uint64_t tagged = 0, tags = 0;
            for (const auto &n : b.nodes) {
                ids.push_back(n.id); locs.push_back(n.location);
                if (!n.tagCount || !n.location.valid()) continue;
                beginRecord(rec, ItemType::Node, n.id, b, n.tagFirst, n.tagCount);
                putZz(rec, n.location.x); putZz(rec, n.location.y);
                Box box; box.extend(n.location);
                const Placement p = Sorted::place(box);
                patchRecord(rec, p, box);
                if (!staging[size_t(w)]->add(p.bucket, rec)) return false;
                ++tagged; tags += n.tagCount;
            }
            sourceNodes += b.nodes.size(); taggedNodes += tagged; tagCount += tags;
            return true;
        }) || !flushStaging())
        return finishError();
    nodes.finish();
    stats.sourceNodes = sourceNodes; stats.taggedNodes = taggedNodes;
    stats.nodeTableBytes = nodes.bytes();
    std::vector<Location> memberNodeLocations(memberNodes.size());
    for (size_t i = 0; i < memberNodes.size(); ++i) nodes.find(memberNodes[i], memberNodeLocations[i]);
    stats.nodeSeconds = since(t);

    // 4. Ways: keep tagged ways and relation members, with their coordinates.
    t = Clock::now();
    std::vector<Box> memberWayBoxes(memberWays.size());
    std::atomic<uint64_t> sourceWays{0}, keptWays{0}, droppedWays{0}, wayRefs{0}, missingRefs{0};
    if (!job.parallel(sets.ways.size(), ConvertPhase::Ways, [&](size_t i, int w) {
            PrimitiveBlock &b = blocks[size_t(w)];
            QString e;
            if (!source.readBlock(sets.ways[i], b, DecodeWays, e)) { job.fail(e); return false; }
            std::string rec, geometry, refPart;
            uint64_t kept = 0, dropped = 0, refsOut = 0, missing = 0, tags = 0;
            for (const auto &way : b.ways) {
                const size_t member = indexOf(memberWays, way.id);
                if (!way.tagCount && member == size_t(-1)) { ++dropped; continue; }
                beginRecord(rec, ItemType::Way, way.id, b, way.tagFirst, way.tagCount);
                geometry.clear();
                Box box;
                uint32_t n = 0;
                int64_t prevRef = 0, px = 0, py = 0;
                refPart.clear();
                for (uint32_t r = 0; r < way.refCount; ++r) {
                    const int64_t ref = b.refs[way.refFirst + r];
                    Location l;
                    if (!nodes.find(ref, l) || !l.valid()) { ++missing; continue; }
                    putZz(refPart, int64_t(uint64_t(ref) - uint64_t(prevRef))); prevRef = ref;
                    putZz(geometry, l.x - px); putZz(geometry, l.y - py); px = l.x; py = l.y;
                    box.extend(l);
                    ++n;
                }
                if (!n) { ++dropped; continue; }
                putVar(rec, n);
                rec += refPart;
                rec += geometry;
                const Placement p = Sorted::place(box);
                patchRecord(rec, p, box);
                if (!staging[size_t(w)]->add(p.bucket, rec)) return false;
                if (member != size_t(-1)) memberWayBoxes[member] = box;  // ids are unique: one writer per slot
                ++kept; refsOut += n; tags += way.tagCount;
            }
            sourceWays += b.ways.size(); keptWays += kept; droppedWays += dropped; wayRefs += refsOut; missingRefs += missing; tagCount += tags;
            return true;
        }) || !flushStaging())
        return finishError();
    stats.sourceWays = sourceWays; stats.keptWays = keptWays; stats.droppedWays = droppedWays;
    stats.wayRefs = wayRefs; stats.missingRefs = missingRefs;
    nodes.clear();
    stats.waySeconds = since(t);

    // 5. Relation extents from their members; relation members are resolved a few levels deep.
    t = Clock::now();
    std::vector<std::pair<int64_t, std::pair<size_t, size_t>>> relationIndex;
    for (size_t i = 0; i < relIds.size(); ++i)
        for (size_t j = 0; j < relIds[i].size(); ++j) relationIndex.push_back({relIds[i][j], {i, j}});
    std::sort(relationIndex.begin(), relationIndex.end());
    std::vector<Box> relationBoxes(relationIndex.size());
    std::vector<std::vector<int64_t>> relationMembers(relationIndex.size());
    for (size_t k = 0; k < relationIndex.size(); ++k) {
        const std::string &rec = relRecords[relationIndex[k].second.first][relationIndex[k].second.second];
        ParsedRecord parsed;
        if (!parseRecord(reinterpret_cast<const uint8_t *>(rec.data()), rec.size(), parsed)) { job.fail(QStringLiteral("Corrupt relation record")); return finishError(); }
        for (const auto &m : parsed.members) {
            if (m.type == ItemType::Way) { const size_t i = indexOf(memberWays, m.ref); if (i != size_t(-1)) relationBoxes[k].extend(memberWayBoxes[i]); }
            else if (m.type == ItemType::Node) { const size_t i = indexOf(memberNodes, m.ref); if (i != size_t(-1) && memberNodeLocations[i].valid()) relationBoxes[k].extend(memberNodeLocations[i]); }
            else relationMembers[k].push_back(m.ref);
        }
    }
    for (int depth = 0; depth < 4; ++depth) {
        bool changed = false;
        for (size_t k = 0; k < relationIndex.size(); ++k) {
            for (int64_t ref : relationMembers[k]) {
                auto it = std::lower_bound(relationIndex.begin(), relationIndex.end(), std::make_pair(ref, std::make_pair(size_t(0), size_t(0))));
                if (it == relationIndex.end() || it->first != ref) continue;
                const Box &sub = relationBoxes[size_t(it - relationIndex.begin())];
                Box before = relationBoxes[k];
                relationBoxes[k].extend(sub);
                changed |= !(before == relationBoxes[k]);
            }
        }
        if (!changed) break;
    }
    {
        Staging &s = *staging[0];
        for (size_t k = 0; k < relationIndex.size(); ++k) {
            std::string &rec = relRecords[relationIndex[k].second.first][relationIndex[k].second.second];
            const Box &box = relationBoxes[k];
            Placement p;
            if (box.valid()) p = Sorted::place(box);
            else { p.bucket = Sorted::UnplacedBucket; ++stats.unplacedRelations; }
            patchRecord(rec, p, box);
            ParsedRecord parsed;
            parseRecord(reinterpret_cast<const uint8_t *>(rec.data()), rec.size(), parsed);
            tagCount += parsed.tags.size();
            if (!s.add(p.bucket, rec)) return finishError();
            std::string().swap(rec);
        }
        if (!s.flush()) return finishError();
    }
    stats.relations = relationIndex.size();
    stats.tags = tagCount;
    stats.tempBytes = buckets.bytes();
    relRecords.clear();
    stats.relationSeconds += since(t);

    // 6. Sorted output.
    t = Clock::now();
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { error = QStringLiteral("Cannot create %1: %2").arg(partPath, out.errorString()); cleanup(false); return false; }
    HeaderInfo header;
    header.bbox = source.header().bbox;
    header.requiredFeatures << QStringLiteral("OsmSchema-V0.6") << QStringLiteral("DenseNodes");
    header.optionalFeatures << QStringLiteral("LocationsOnWays") << Sorted::FeatureMarker;
    header.writingProgram = QStringLiteral("TSRE5");
    Sorted::SourceIdentity identity;
    identity.name = QFileInfo(sourcePath).fileName();
    identity.size = source.size();
    identity.timestamp = source.header().replicationTimestamp;
    header.source = identity.toString();
    header.replicationTimestamp = source.header().replicationTimestamp;
    header.replicationSequence = source.header().replicationSequence;
    header.replicationBaseUrl = source.header().replicationBaseUrl;
    const std::string headerFrame = encodeBlobFrame("OSMHeader", encodeHeaderBlock(header), {}, options.compressionLevel);
    if (out.write(headerFrame.data(), qint64(headerFrame.size())) != qint64(headerFrame.size())) { job.fail(QStringLiteral("Cannot write %1").arg(partPath)); return finishError(); }
    stats.outputBytes = headerFrame.size();
    if (!writeBuckets(buckets, job, out, stats)) return finishError();
    out.close();
    source.close();
    stats.writeSeconds = since(t);

    // 7. Check the result opens as a converted file, then put it in place.
    {
        PbfFile check;
        QString e;
        if (!check.open(partPath, e) || !check.header().hasFeature(Sorted::FeatureMarker) || check.blobs().size() != stats.outputBlocks + 1) {
            job.fail(QStringLiteral("Converted file failed verification: %1").arg(e));
            return finishError();
        }
    }
    if (QFile::exists(outputPath) && !QFile::remove(outputPath)) { job.fail(QStringLiteral("Cannot replace %1").arg(outputPath)); return finishError(); }
    if (!QFile::rename(partPath, outputPath)) { job.fail(QStringLiteral("Cannot rename %1 to %2").arg(partPath, outputPath)); return finishError(); }
    cleanup(true);
    stats.totalSeconds = since(start);
    return true;
}

}
