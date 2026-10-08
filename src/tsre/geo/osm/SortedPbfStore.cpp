/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/geo/osm/SortedPbfStore.h>
#include <tsre/geo/osm/OsmDirectory.h>
#include <QFileInfo>
#include <algorithm>
#include <atomic>
#include <thread>
#include <unordered_set>

namespace Osm {

namespace {

uint64_t blockBytes(const PrimitiveBlock &b) {
    return b.raw.capacity() + b.strings.capacity() * sizeof(std::string_view) + b.tagPairs.capacity() * sizeof(uint32_t)
           + b.nodes.capacity() * sizeof(PrimitiveBlock::Node) + b.ways.capacity() * sizeof(PrimitiveBlock::Way)
           + b.relations.capacity() * sizeof(PrimitiveBlock::Relation) + b.refs.capacity() * sizeof(int64_t)
           + b.refLocations.capacity() * sizeof(Location) + b.members.capacity() * sizeof(Member) + sizeof(PrimitiveBlock);
}

uint64_t dedupeKey(ItemType type, int64_t id) { return uint64_t(type) << 62 | (uint64_t(id) & ((1ull << 62) - 1)); }

}

SortedPbfStore::SortedPbfStore() = default;
SortedPbfStore::~SortedPbfStore() { close(); }

void SortedPbfStore::close() {
    std::lock_guard<std::mutex> l(mutex_);
    cache_.clear();
    lru_.clear();
    used_ = 0;
    files_.clear();
}

bool SortedPbfStore::open(const OsmDirectory &directory, QString &error) {
    QStringList paths;
    for (const DirectoryEntry *e : directory.convertedFiles()) paths << e->path;
    return open(paths, error);
}

bool SortedPbfStore::open(const QStringList &paths, QString &error) {
    close();
    std::vector<File> files;
    for (const QString &path : paths) {
        File f;
        f.pbf = std::make_unique<PbfFile>();
        if (!f.pbf->open(path, error, path + QStringLiteral(".idx"))) return false;
        if (!f.pbf->header().hasFeature(Sorted::FeatureMarker)) { error = QStringLiteral("%1 is not a converted OSM file").arg(path); return false; }
        f.timestamp = f.pbf->header().replicationTimestamp;
        const auto &blobs = f.pbf->blobs();
        for (size_t i = 0; i < blobs.size(); ++i) {
            if (blobs[i].type != BlobType::Data) continue;
            Block b{i, {}};
            if (!Sorted::decodeIndex(blobs[i].indexData, b.index)) { error = QStringLiteral("%1: block %2 has no TSRE index").arg(path).arg(i); return false; }
            f.bounds.extend(b.index.bounds);
            f.blocks.push_back(b);
        }
        files.push_back(std::move(f));
    }
    std::stable_sort(files.begin(), files.end(), [](const File &a, const File &b) { return a.timestamp > b.timestamp; });
    std::lock_guard<std::mutex> l(mutex_);
    files_ = std::move(files);
    return true;
}

Box SortedPbfStore::bounds() const {
    Box b;
    for (const File &f : files_) b.extend(f.bounds);
    return b;
}

void SortedPbfStore::setCacheBudget(uint64_t bytes) {
    std::lock_guard<std::mutex> l(mutex_);
    budget_ = bytes;
    while (used_ > budget_ && !lru_.empty()) {
        auto it = cache_.find(lru_.back());
        used_ -= it->second.bytes;
        cache_.erase(it);
        lru_.pop_back();
    }
}

uint64_t SortedPbfStore::cacheBytes() const {
    std::lock_guard<std::mutex> l(mutex_);
    return used_;
}

SortedPbfStore::Decoded SortedPbfStore::cached(uint64_t key) const {
    std::lock_guard<std::mutex> l(mutex_);
    auto it = cache_.find(key);
    if (it == cache_.end()) return nullptr;
    lru_.splice(lru_.begin(), lru_, it->second.lru);
    return it->second.block;
}

void SortedPbfStore::insert(uint64_t key, const Decoded &block) const {
    std::lock_guard<std::mutex> l(mutex_);
    if (cache_.count(key)) return;
    const uint64_t bytes = blockBytes(*block);
    if (bytes > budget_) return;
    lru_.push_front(key);
    cache_[key] = {block, bytes, lru_.begin()};
    used_ += bytes;
    while (used_ > budget_ && !lru_.empty()) {
        auto it = cache_.find(lru_.back());
        used_ -= it->second.bytes;
        cache_.erase(it);
        lru_.pop_back();
    }
}

bool SortedPbfStore::forEach(const Box &area, const Filter &filter, const FeatureCallback &fn, QString &error) const {
    struct Want { size_t file; size_t blob; Decoded block; };
    std::vector<Want> wanted;
    size_t filesUsed = 0;
    for (size_t fi = 0; fi < files_.size(); ++fi) {
        const File &f = files_[fi];
        if (!f.bounds.intersects(area)) continue;
        const size_t before = wanted.size();
        for (const Block &b : f.blocks) {
            if (!(filter.types & (1u << uint32_t(b.index.kind)))) continue;
            // Blocks without bounds hold relations with no resolvable member: nothing to place.
            if (!b.index.bounds.valid() || !b.index.bounds.intersects(area) || filter.readAlready.containsBox(b.index.bounds)) continue;
            wanted.push_back({fi, b.blob, cached(cacheKey(fi, b.blob))});
        }
        filesUsed += wanted.size() > before;
    }

    // Decode the missing blocks in parallel.
    std::vector<size_t> missing;
    for (size_t i = 0; i < wanted.size(); ++i) if (!wanted[i].block) missing.push_back(i);
    if (!missing.empty()) {
        const int hw = int(std::max(1u, std::thread::hardware_concurrency()));
        const size_t threads = std::min<size_t>(missing.size(), size_t(threads_ > 0 ? threads_ : hw));
        std::atomic<size_t> next{0};
        std::atomic_bool failed{false};
        std::mutex errorMutex;
        auto work = [&] {
            for (size_t k; !failed && (k = next++) < missing.size();) {
                Want &w = wanted[missing[k]];
                auto block = std::make_shared<PrimitiveBlock>();
                QString e;
                if (!files_[w.file].pbf->readBlock(w.blob, *block, DecodeAll, e)) {
                    std::lock_guard<std::mutex> l(errorMutex);
                    if (!failed.exchange(true)) error = e;
                    return;
                }
                w.block = block;
                insert(cacheKey(w.file, w.blob), w.block);
            }
        };
        std::vector<std::thread> pool;
        for (size_t t = 1; t < threads; ++t) pool.emplace_back(work);
        work();
        for (auto &t : pool) t.join();
        if (failed) return false;
    }

    // Report on this thread, in file and block order.
    const bool dedupe = filesUsed > 1;
    std::unordered_set<uint64_t> seen;
    auto firstTime = [&](ItemType type, int64_t id) { return !dedupe || seen.insert(dedupeKey(type, id)).second; };
    auto idWanted = [&](int64_t id) { return filter.ids.empty() || std::binary_search(filter.ids.begin(), filter.ids.end(), id); };
    uint64_t reported = 0;
    Feature f;
    for (const Want &w : wanted) {
        const PrimitiveBlock &b = *w.block;
        if (filter.types & Nodes) {
            for (const auto &n : b.nodes) {
                if (!area.contains(n.location) || !idWanted(n.id)) continue;
                f = Feature();
                f.type = ItemType::Node; f.id = n.id; f.location = n.location;
                f.extent = Box(); f.extent.extend(n.location);
                f.bind(&b, n.tagFirst, n.tagCount);
                if (!filter.matchesTags(f) || !firstTime(f.type, f.id)) continue;
                fn(f); ++reported;
            }
        }
        if (filter.types & Ways) {
            for (const auto &way : b.ways) {
                if (!idWanted(way.id) || !b.wayLocations) continue;
                Box extent;
                for (uint32_t i = 0; i < way.refCount; ++i) extent.extend(b.refLocations[way.refFirst + i]);
                if (!extent.intersects(area)) continue;
                f = Feature();
                f.type = ItemType::Way; f.id = way.id; f.extent = extent;
                f.refs = b.refs.data() + way.refFirst; f.locations = b.refLocations.data() + way.refFirst; f.refCount = way.refCount;
                f.bind(&b, way.tagFirst, way.tagCount);
                if (!filter.matchesTags(f) || !firstTime(f.type, f.id)) continue;
                fn(f); ++reported;
            }
        }
        if (filter.types & Relations) {
            for (const auto &r : b.relations) {
                if (!idWanted(r.id) || !r.extent.intersects(area)) continue;
                f = Feature();
                f.type = ItemType::Relation; f.id = r.id; f.extent = r.extent;
                f.members = b.members.data() + r.memberFirst; f.memberCount = r.memberCount;
                f.bind(&b, r.tagFirst, r.tagCount);
                if (!filter.matchesTags(f) || !firstTime(f.type, f.id)) continue;
                fn(f); ++reported;
            }
        }
    }
    std::lock_guard<std::mutex> l(mutex_);
    last_.blocksRead = missing.size();
    last_.blocksCached = wanted.size() - missing.size();
    last_.featuresReported = reported;
    return true;
}

}
