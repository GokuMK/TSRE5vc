/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#pragma once

#include <tsre/geo/osm/OsmStore.h>
#include <tsre/geo/osm/OsmSortedFormat.h>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace Osm {

class OsmDirectory;

// OsmStore over TSRE sorted PBF files (OsmConverter output). Only the blocks whose
// indexdata bbox meets a query are read; decoded blocks stay in an LRU cache.
class SortedPbfStore : public OsmStore {
public:
    SortedPbfStore();
    ~SortedPbfStore() override;

    // Opens converted files; a file that is not a TSRE sorted PBF is an error.
    bool open(const QStringList &files, QString &error);
    // Opens every converted file of a scanned directory.
    bool open(const OsmDirectory &directory, QString &error);
    void close();

    bool forEach(const Box &area, const Filter &filter, const FeatureCallback &fn, QString &error) const override;
    Box bounds() const override;

    void setThreads(int threads) { threads_ = threads; }          // 0: all hardware threads
    void setCacheBudget(uint64_t bytes);                           // default 512 MiB
    uint64_t cacheBytes() const;
    size_t fileCount() const { return files_.size(); }

    struct Stats { uint64_t blocksRead = 0, blocksCached = 0, featuresReported = 0; };
    Stats lastQuery() const { std::lock_guard<std::mutex> l(mutex_); return last_; }

private:
    struct Block { size_t blob; Sorted::BlockIndex index; };
    struct File {
        std::unique_ptr<PbfFile> pbf;
        std::vector<Block> blocks;
        int64_t timestamp = 0;
        Box bounds;
    };
    using Decoded = std::shared_ptr<const PrimitiveBlock>;
    struct CacheEntry { Decoded block; uint64_t bytes; std::list<uint64_t>::iterator lru; };

    static uint64_t cacheKey(size_t file, size_t blob) { return uint64_t(file) << 40 | uint64_t(blob); }
    Decoded cached(uint64_t key) const;
    void insert(uint64_t key, const Decoded &block) const;

    std::vector<File> files_;  // newest first: duplicates are taken from the newest file
    int threads_ = 0;
    mutable std::mutex mutex_;
    uint64_t budget_ = 512ull * 1024 * 1024;
    mutable uint64_t used_ = 0;
    mutable std::list<uint64_t> lru_;
    mutable std::unordered_map<uint64_t, CacheEntry> cache_;
    mutable Stats last_;
};

}
