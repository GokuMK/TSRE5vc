/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/geo/osm/OsmPbf.h>
#include <mzip/miniz/miniz.h>
#include <cstring>
#if defined(Q_OS_LINUX)
#include <fcntl.h>
#endif

namespace Osm {

using Proto::Reader;
using Proto::Span;
using Proto::Writer;

namespace {

Span spanOf(const std::vector<uint8_t> &v) { return {v.data(), v.size()}; }
QString text(Span s) { return QString::fromUtf8(reinterpret_cast<const char *>(s.data), int(s.size)); }

bool fail(QString &error, const QString &message) { error = message; return false; }

// Granularity and offsets are nanodegrees; TSRE uses OSM's 1e-7 fixed point.
struct CoordinateFormat {
    int64_t granularity = 100, latOffset = 0, lonOffset = 0;
    bool plain() const { return granularity == 100 && latOffset % 100 == 0 && lonOffset % 100 == 0; }
    int32_t lat(int64_t v) const { return plain() ? int32_t(latOffset / 100 + v) : int32_t(std::llround((latOffset + granularity * v) / 100.0)); }
    int32_t lon(int64_t v) const { return plain() ? int32_t(lonOffset / 100 + v) : int32_t(std::llround((lonOffset + granularity * v) / 100.0)); }
};

// Packed delta-coded int64 values, summed with wrap-around like the encoder.
template <class F> bool forEachDelta(Span s, F &&f) {
    uint64_t sum = 0;
    return Proto::forEachSVarint(s, [&](int64_t d) { sum += uint64_t(d); f(int64_t(sum)); });
}

bool readPackedU32(Span s, std::vector<uint32_t> &out) {
    out.clear();
    return Proto::forEachVarint(s, [&](uint64_t v) { out.push_back(uint32_t(v)); });
}

struct GroupScratch {
    std::vector<uint32_t> keys, vals, roles, types;
    std::vector<int64_t> lats, lons;
};

bool appendTags(PrimitiveBlock &b, const std::vector<uint32_t> &keys, const std::vector<uint32_t> &vals, uint32_t &first, uint32_t &count) {
    if (keys.size() != vals.size()) return false;
    first = uint32_t(b.tagPairs.size() / 2);
    count = uint32_t(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) { b.tagPairs.push_back(keys[i]); b.tagPairs.push_back(vals[i]); }
    return true;
}

bool decodeDense(PrimitiveBlock &b, Span dense, const CoordinateFormat &cf) {
    Span ids, lats, lons, keysVals;
    Reader r(dense);
    while (r.next()) {
        switch (r.field()) {
            case 1: ids = r.bytes(); break;
            case 8: lats = r.bytes(); break;
            case 9: lons = r.bytes(); break;
            case 10: keysVals = r.bytes(); break;
            default: r.skip();
        }
    }
    if (!r.ok()) return false;
    const size_t first = b.nodes.size();
    if (!forEachDelta(ids, [&](int64_t id) { b.nodes.push_back({id, {}, 0, 0}); })) return false;
    const size_t count = b.nodes.size() - first;
    size_t i = 0;
    bool ok = forEachDelta(lats, [&](int64_t v) { if (i < count) b.nodes[first + i].location.y = cf.lat(v); ++i; }) && i == count;
    i = 0;
    ok = ok && forEachDelta(lons, [&](int64_t v) { if (i < count) b.nodes[first + i].location.x = cf.lon(v); ++i; }) && i == count;
    if (!ok) return false;
    if (keysVals.empty()) return true;
    if (count == 0) return false;
    // keys_vals: (key, value)* 0 for every node, in node order.
    size_t node = first;
    bool expectValue = false; uint32_t key = 0;
    b.nodes[node].tagFirst = uint32_t(b.tagPairs.size() / 2);
    ok = Proto::forEachVarint(keysVals, [&](uint64_t v) {
        if (node >= b.nodes.size()) { ok = false; return; }
        if (expectValue) { b.tagPairs.push_back(key); b.tagPairs.push_back(uint32_t(v)); b.nodes[node].tagCount++; expectValue = false; }
        else if (v == 0) { if (++node < b.nodes.size()) b.nodes[node].tagFirst = uint32_t(b.tagPairs.size() / 2); }
        else { key = uint32_t(v); expectValue = true; }
    }) && ok && !expectValue;
    // Nodes after a short keys_vals (no closing delimiters) have no tags.
    for (size_t n = node + 1; ok && n < b.nodes.size(); ++n) b.nodes[n].tagFirst = uint32_t(b.tagPairs.size() / 2);
    return ok;
}

bool decodeNode(PrimitiveBlock &b, Span msg, const CoordinateFormat &cf, GroupScratch &g) {
    Reader r(msg);
    PrimitiveBlock::Node n{0, {}, 0, 0};
    g.keys.clear(); g.vals.clear();
    bool ok = true;
    while (r.next()) {
        switch (r.field()) {
            case 1: n.id = r.svarint(); break;
            case 2: ok &= readPackedU32(r.bytes(), g.keys); break;
            case 3: ok &= readPackedU32(r.bytes(), g.vals); break;
            case 8: n.location.y = cf.lat(r.svarint()); break;
            case 9: n.location.x = cf.lon(r.svarint()); break;
            default: r.skip();
        }
    }
    if (!r.ok() || !ok || !appendTags(b, g.keys, g.vals, n.tagFirst, n.tagCount)) return false;
    b.nodes.push_back(n);
    return true;
}

bool decodeWay(PrimitiveBlock &b, Span msg, const CoordinateFormat &cf, GroupScratch &g) {
    Reader r(msg);
    PrimitiveBlock::Way w{0, 0, 0, uint32_t(b.refs.size()), 0};
    Span refs, lats, lons;
    g.keys.clear(); g.vals.clear();
    bool ok = true;
    while (r.next()) {
        switch (r.field()) {
            case 1: w.id = r.int64(); break;
            case 2: ok &= readPackedU32(r.bytes(), g.keys); break;
            case 3: ok &= readPackedU32(r.bytes(), g.vals); break;
            case 8: refs = r.bytes(); break;
            case 9: lats = r.bytes(); break;
            case 10: lons = r.bytes(); break;
            default: r.skip();
        }
    }
    if (!r.ok() || !ok || !appendTags(b, g.keys, g.vals, w.tagFirst, w.tagCount)) return false;
    if (!forEachDelta(refs, [&](int64_t v) { b.refs.push_back(v); })) return false;
    w.refCount = uint32_t(b.refs.size() - w.refFirst);
    if (!lats.empty() || !lons.empty()) {
        g.lats.clear(); g.lons.clear();
        if (!forEachDelta(lats, [&](int64_t v) { g.lats.push_back(v); }) || !forEachDelta(lons, [&](int64_t v) { g.lons.push_back(v); })
            || g.lats.size() != w.refCount || g.lons.size() != w.refCount)
            return false;
        if (!b.wayLocations) {
            // Earlier ways in this block had no locations: keep the arrays parallel.
            b.wayLocations = true;
            b.refLocations.resize(w.refFirst);
        }
        for (size_t i = 0; i < g.lats.size(); ++i) b.refLocations.push_back({cf.lon(g.lons[i]), cf.lat(g.lats[i])});
    } else if (b.wayLocations) {
        b.refLocations.resize(b.refs.size());
    }
    b.ways.push_back(w);
    return true;
}

bool decodeRelation(PrimitiveBlock &b, Span msg, GroupScratch &g) {
    Reader r(msg);
    PrimitiveBlock::Relation rel{0, 0, 0, uint32_t(b.members.size()), 0};
    Span memids;
    g.keys.clear(); g.vals.clear(); g.roles.clear(); g.types.clear();
    bool ok = true;
    while (r.next()) {
        switch (r.field()) {
            case 1: rel.id = r.int64(); break;
            case 2: ok &= readPackedU32(r.bytes(), g.keys); break;
            case 3: ok &= readPackedU32(r.bytes(), g.vals); break;
            case 8: ok &= readPackedU32(r.bytes(), g.roles); break;
            case 9: memids = r.bytes(); break;
            case 10: ok &= readPackedU32(r.bytes(), g.types); break;
            default: r.skip();
        }
    }
    if (!r.ok() || !ok || !appendTags(b, g.keys, g.vals, rel.tagFirst, rel.tagCount)) return false;
    size_t i = 0;
    ok = forEachDelta(memids, [&](int64_t v) {
        if (i >= g.roles.size() || i >= g.types.size() || g.types[i] > 2) { ok = false; ++i; return; }
        b.members.push_back({v, g.roles[i], ItemType(g.types[i])});
        ++i;
    }) && ok && i == g.roles.size() && i == g.types.size();
    if (!ok) return false;
    rel.memberCount = uint32_t(b.members.size() - rel.memberFirst);
    b.relations.push_back(rel);
    return true;
}

}

void PrimitiveBlock::clear() {
    strings.clear(); tagPairs.clear(); nodes.clear(); ways.clear(); relations.clear();
    refs.clear(); refLocations.clear(); members.clear();
    wayLocations = false;
}

bool decodePrimitiveBlock(PrimitiveBlock &b, uint32_t parts, QString &error) {
    b.clear();
    CoordinateFormat cf;
    std::vector<Span> groups;
    Reader r(spanOf(b.raw));
    while (r.next()) {
        switch (r.field()) {
            case 1: {
                Reader st(r.bytes());
                while (st.next()) { if (st.field() == 1) b.strings.push_back(st.bytes().view()); else st.skip(); }
                if (!st.ok()) return fail(error, QStringLiteral("Malformed PBF string table"));
                break;
            }
            case 2: groups.push_back(r.bytes()); break;
            case 17: cf.granularity = int64_t(r.varint()); break;
            case 19: cf.latOffset = r.int64(); break;
            case 20: cf.lonOffset = r.int64(); break;
            default: r.skip();
        }
    }
    if (!r.ok()) return fail(error, QStringLiteral("Malformed PBF PrimitiveBlock"));
    if (cf.granularity <= 0) return fail(error, QStringLiteral("Invalid PBF granularity"));
    GroupScratch g;
    for (const Span &gs : groups) {
        Reader gr(gs);
        bool ok = true;
        while (ok && gr.next()) {
            const uint32_t f = gr.field();
            if (f == 1 && (parts & DecodeNodes)) ok = decodeNode(b, gr.bytes(), cf, g);
            else if (f == 2 && (parts & DecodeNodes)) ok = decodeDense(b, gr.bytes(), cf);
            else if (f == 3 && (parts & DecodeWays)) ok = decodeWay(b, gr.bytes(), cf, g);
            else if (f == 4 && (parts & DecodeRelations)) ok = decodeRelation(b, gr.bytes(), g);
            else gr.skip();
        }
        if (!ok || !gr.ok()) return fail(error, QStringLiteral("Malformed PBF primitive group"));
    }
    const uint32_t strings = uint32_t(b.strings.size());
    for (uint32_t s : b.tagPairs) if (s >= strings) return fail(error, QStringLiteral("PBF tag refers outside the string table"));
    for (const Member &m : b.members) if (m.role >= strings) return fail(error, QStringLiteral("PBF member role refers outside the string table"));
    return true;
}

uint32_t blockParts(const std::vector<uint8_t> &raw) {
    uint32_t parts = 0;
    Reader r(spanOf(raw));
    while (r.next()) {
        if (r.field() != 2) { r.skip(); continue; }
        Reader g(r.bytes());
        while (g.next()) {
            switch (g.field()) {
                case 1: case 2: parts |= DecodeNodes; break;
                case 3: parts |= DecodeWays; break;
                case 4: parts |= DecodeRelations; break;
                default: break;
            }
            g.skip();
        }
        if (!g.ok()) return 0;
    }
    return r.ok() ? parts : 0;
}

bool decodeHeaderBlock(Span raw, HeaderInfo &h, QString &error) {
    h = HeaderInfo();
    Reader r(raw);
    while (r.next()) {
        switch (r.field()) {
            case 1: {
                int64_t v[5] = {0, 0, 0, 0, 0};
                Reader bb(r.bytes());
                while (bb.next()) { if (bb.field() >= 1 && bb.field() <= 4) v[bb.field()] = bb.svarint(); else bb.skip(); }
                if (!bb.ok()) return fail(error, QStringLiteral("Malformed PBF header bbox"));
                // left, right, top, bottom in nanodegrees
                h.bbox.extend(Location(int32_t(v[1] / 100), int32_t(v[4] / 100)));
                h.bbox.extend(Location(int32_t(v[2] / 100), int32_t(v[3] / 100)));
                break;
            }
            case 4: h.requiredFeatures << text(r.bytes()); break;
            case 5: h.optionalFeatures << text(r.bytes()); break;
            case 16: h.writingProgram = text(r.bytes()); break;
            case 17: h.source = text(r.bytes()); break;
            case 32: h.replicationTimestamp = r.int64(); break;
            case 33: h.replicationSequence = r.int64(); break;
            case 34: h.replicationBaseUrl = text(r.bytes()); break;
            default: r.skip();
        }
    }
    if (!r.ok()) return fail(error, QStringLiteral("Malformed PBF header block"));
    static const QStringList supported{QStringLiteral("OsmSchema-V0.6"), QStringLiteral("DenseNodes"), QStringLiteral("HistoricalInformation")};
    for (const QString &f : h.requiredFeatures)
        if (!supported.contains(f)) return fail(error, QStringLiteral("Unsupported PBF required feature: %1").arg(f));
    return true;
}

bool inflateBlob(Span blob, std::vector<uint8_t> &out, QString &error) {
    Reader r(blob);
    uint64_t rawSize = 0;
    Span raw, zlib;
    bool unsupported = false;
    while (r.next()) {
        switch (r.field()) {
            case 1: raw = r.bytes(); break;
            case 2: rawSize = r.varint(); break;
            case 3: zlib = r.bytes(); break;
            case 4: case 5: case 6: case 7: unsupported = true; r.skip(); break;
            default: r.skip();
        }
    }
    if (!r.ok()) return fail(error, QStringLiteral("Malformed PBF blob"));
    if (raw.data) {
        if (raw.size > MaxBlobSize) return fail(error, QStringLiteral("PBF blob exceeds the 32 MiB limit"));
        out.assign(raw.data, raw.data + raw.size);
        return true;
    }
    if (!zlib.data) return fail(error, unsupported ? QStringLiteral("Unsupported PBF blob compression (only zlib is supported)")
                                                   : QStringLiteral("PBF blob has no data"));
    if (rawSize == 0 || rawSize > MaxBlobSize) return fail(error, QStringLiteral("Invalid PBF blob size"));
    out.resize(size_t(rawSize));
    mz_ulong length = mz_ulong(rawSize);
    if (mz_uncompress(out.data(), &length, zlib.data, mz_ulong(zlib.size)) != MZ_OK || length != rawSize)
        return fail(error, QStringLiteral("Cannot decompress PBF blob"));
    return true;
}

// ---------------------------------------------------------------- PbfFile

PbfFile::~PbfFile() { close(); }

void PbfFile::close() {
    if (map_) { file_.unmap(const_cast<uchar *>(map_)); map_ = nullptr; }
    if (file_.isOpen()) file_.close();
    blobs_.clear();
    header_ = HeaderInfo();
    size_ = 0;
}

bool PbfFile::open(const QString &path, QString &error) {
    close();
    file_.setFileName(path);
    if (!file_.open(QIODevice::ReadOnly)) return fail(error, QStringLiteral("Cannot open %1: %2").arg(path, file_.errorString()));
    size_ = file_.size();
#if defined(Q_OS_LINUX)
    // Header walk touches a few bytes per block; readahead would pull in most of the file.
    posix_fadvise(file_.handle(), 0, 0, POSIX_FADV_RANDOM);
#endif
    bool ok = true;
    std::vector<uint8_t> header;
    int64_t off = 0;
    while (ok && off < size_) {
        uint8_t prefix[4];
        if (size_ - off < 4 || !file_.seek(off) || file_.read(reinterpret_cast<char *>(prefix), 4) != 4) { ok = fail(error, QStringLiteral("Truncated PBF frame")); break; }
        const uint32_t headerSize = uint32_t(prefix[0]) << 24 | uint32_t(prefix[1]) << 16 | uint32_t(prefix[2]) << 8 | prefix[3];
        if (headerSize == 0 || headerSize > MaxBlobHeaderSize || int64_t(headerSize) > size_ - off - 4) { ok = fail(error, QStringLiteral("Invalid PBF blob header size")); break; }
        header.resize(headerSize);
        if (file_.read(reinterpret_cast<char *>(header.data()), headerSize) != qint64(headerSize)) { ok = fail(error, QStringLiteral("Truncated PBF blob header")); break; }
        BlobInfo info;
        info.offset = off;
        uint64_t dataSize = 0;
        bool hasSize = false;
        Reader r(spanOf(header));
        while (r.next()) {
            switch (r.field()) {
                case 1: { const auto t = r.bytes().view(); info.type = t == "OSMData" ? BlobType::Data : t == "OSMHeader" ? BlobType::Header : BlobType::Unknown; break; }
                case 2: info.indexData = std::string(r.bytes().view()); break;
                case 3: dataSize = r.varint(); hasSize = true; break;
                default: r.skip();
            }
        }
        if (!r.ok() || !hasSize || dataSize > MaxBlobSize) { ok = fail(error, QStringLiteral("Malformed PBF blob header")); break; }
        info.dataOffset = off + 4 + headerSize;
        info.dataSize = uint32_t(dataSize);
        info.size = 4 + int64_t(headerSize) + int64_t(dataSize);
        if (info.dataOffset + int64_t(dataSize) > size_) { ok = fail(error, QStringLiteral("Truncated PBF blob")); break; }
        blobs_.push_back(std::move(info));
        off += blobs_.back().size;
    }
#if defined(Q_OS_LINUX)
    posix_fadvise(file_.handle(), 0, 0, POSIX_FADV_NORMAL);
#endif
    if (ok && (blobs_.empty() || blobs_.front().type != BlobType::Header)) ok = fail(error, QStringLiteral("PBF file does not start with a header block"));
    if (ok) {
        map_ = file_.map(0, size_);
        if (!map_) ok = fail(error, QStringLiteral("Cannot map %1: %2").arg(path, file_.errorString()));
    }
    std::vector<uint8_t> raw;
    if (ok) ok = readBlob(0, raw, error) && decodeHeaderBlock(spanOf(raw), header_, error);
    if (!ok) {
        error = QStringLiteral("%1: %2").arg(path, error);
        close();
    }
    return ok;
}

bool PbfFile::readBlob(size_t index, std::vector<uint8_t> &raw, QString &error) const {
    if (!map_ || index >= blobs_.size()) return fail(error, QStringLiteral("PBF blob index out of range"));
    const BlobInfo &b = blobs_[index];
    return inflateBlob(Span(map_ + b.dataOffset, b.dataSize), raw, error);
}

bool PbfFile::readBlock(size_t index, PrimitiveBlock &block, uint32_t parts, QString &error) const {
    block.clear();
    return readBlob(index, block.raw, error) && decodePrimitiveBlock(block, parts, error);
}

// ---------------------------------------------------------------- BlockBuilder

void BlockBuilder::clear() {
    stringIndex_.clear(); strings_.clear(); stringStorage_.clear(); tagPairs_.clear();
    nodes_.clear(); ways_.clear(); relations_.clear();
    refs_.clear(); refLocations_.clear(); members_.clear();
    wayLocations_ = false;
    bounds_ = Box();
}

uint32_t BlockBuilder::string(std::string_view s) {
    auto it = stringIndex_.find(s);
    if (it != stringIndex_.end()) return it->second;
    const std::string_view stored = stringStorage_.emplace_back(s);
    const uint32_t index = uint32_t(strings_.size() + 1);  // index 0 is the empty string
    stringIndex_.emplace(stored, index);
    strings_.push_back(stored);
    return index;
}

void BlockBuilder::addTags(const Tag *tags, size_t n, uint32_t &first, uint32_t &count) {
    first = uint32_t(tagPairs_.size() / 2);
    count = uint32_t(n);
    for (size_t i = 0; i < n; ++i) { tagPairs_.push_back(string(tags[i].key)); tagPairs_.push_back(string(tags[i].value)); }
}

void BlockBuilder::addNode(int64_t id, Location location, const Tag *tags, size_t tagCount) {
    Node n{id, location, 0, 0};
    addTags(tags, tagCount, n.tagFirst, n.tagCount);
    nodes_.push_back(n);
    if (location.valid()) bounds_.extend(location);
}

void BlockBuilder::addWay(int64_t id, const Tag *tags, size_t tagCount, const int64_t *refs, const Location *locations, size_t refCount) {
    Way w{id, 0, 0, uint32_t(refs_.size()), uint32_t(refCount)};
    addTags(tags, tagCount, w.tagFirst, w.tagCount);
    refs_.insert(refs_.end(), refs, refs + refCount);
    if (locations) {
        if (!wayLocations_) { wayLocations_ = true; refLocations_.resize(w.refFirst); }
        for (size_t i = 0; i < refCount; ++i) { refLocations_.push_back(locations[i]); if (locations[i].valid()) bounds_.extend(locations[i]); }
    } else if (wayLocations_) {
        refLocations_.resize(refs_.size());
    }
    ways_.push_back(w);
}

void BlockBuilder::addRelation(int64_t id, const Tag *tags, size_t tagCount, const MemberIn *members, size_t memberCount) {
    Relation r{id, 0, 0, uint32_t(members_.size()), uint32_t(memberCount)};
    addTags(tags, tagCount, r.tagFirst, r.tagCount);
    for (size_t i = 0; i < memberCount; ++i) members_.push_back({members[i].ref, string(members[i].role), members[i].type});
    relations_.push_back(r);
}

void BlockBuilder::build(std::string &out) {
    out.clear();
    Writer block(out);
    {
        std::string table;
        Writer st(table);
        st.bytes(1, std::string_view());
        for (std::string_view s : strings_) st.bytes(1, s);
        block.bytes(1, table);
    }
    std::string group, msg;
    std::vector<int64_t> a, b2, c;
    std::vector<uint32_t> keys, vals, kv;
    if (!nodes_.empty()) {
        group.clear(); msg.clear();
        Writer dense(msg);
        a.clear(); b2.clear(); c.clear(); kv.clear();
        bool anyTags = false;
        for (const Node &n : nodes_) { a.push_back(n.id); b2.push_back(n.location.y); c.push_back(n.location.x); anyTags |= n.tagCount > 0; }
        dense.packedDelta(1, a.begin(), a.end());
        dense.packedDelta(8, b2.begin(), b2.end());
        dense.packedDelta(9, c.begin(), c.end());
        if (anyTags) {
            for (const Node &n : nodes_) {
                for (uint32_t t = 0; t < n.tagCount; ++t) { kv.push_back(tagPairs_[2 * (n.tagFirst + t)]); kv.push_back(tagPairs_[2 * (n.tagFirst + t) + 1]); }
                kv.push_back(0);
            }
            dense.packedVarint(10, kv.begin(), kv.end());
        }
        Writer(group).bytes(2, msg);
        block.bytes(2, group);
    }
    if (!ways_.empty()) {
        group.clear();
        Writer gw(group);
        for (const Way &w : ways_) {
            msg.clear();
            Writer m(msg);
            m.varint(1, uint64_t(w.id));
            keys.clear(); vals.clear();
            for (uint32_t t = 0; t < w.tagCount; ++t) { keys.push_back(tagPairs_[2 * (w.tagFirst + t)]); vals.push_back(tagPairs_[2 * (w.tagFirst + t) + 1]); }
            m.packedVarint(2, keys.begin(), keys.end());
            m.packedVarint(3, vals.begin(), vals.end());
            m.packedDelta(8, refs_.begin() + w.refFirst, refs_.begin() + w.refFirst + w.refCount);
            if (wayLocations_) {
                b2.clear(); c.clear();
                for (uint32_t i = 0; i < w.refCount; ++i) { b2.push_back(refLocations_[w.refFirst + i].y); c.push_back(refLocations_[w.refFirst + i].x); }
                m.packedDelta(9, b2.begin(), b2.end());
                m.packedDelta(10, c.begin(), c.end());
            }
            gw.bytes(3, msg);
        }
        block.bytes(2, group);
    }
    if (!relations_.empty()) {
        group.clear();
        Writer gw(group);
        for (const Relation &r : relations_) {
            msg.clear();
            Writer m(msg);
            m.varint(1, uint64_t(r.id));
            keys.clear(); vals.clear();
            for (uint32_t t = 0; t < r.tagCount; ++t) { keys.push_back(tagPairs_[2 * (r.tagFirst + t)]); vals.push_back(tagPairs_[2 * (r.tagFirst + t) + 1]); }
            m.packedVarint(2, keys.begin(), keys.end());
            m.packedVarint(3, vals.begin(), vals.end());
            kv.clear(); a.clear(); keys.clear();
            for (uint32_t i = 0; i < r.memberCount; ++i) {
                const MemberRec &mr = members_[r.memberFirst + i];
                kv.push_back(mr.role); a.push_back(mr.ref); keys.push_back(uint32_t(mr.type));
            }
            m.packedVarint(8, kv.begin(), kv.end());
            m.packedDelta(9, a.begin(), a.end());
            m.packedVarint(10, keys.begin(), keys.end());
            gw.bytes(4, msg);
        }
        block.bytes(2, group);
    }
}

std::string encodeHeaderBlock(const HeaderInfo &h) {
    std::string out;
    Writer w(out);
    if (h.bbox.valid()) {
        std::string bb;
        Writer b(bb);
        b.svarint(1, int64_t(h.bbox.minX) * 100);
        b.svarint(2, int64_t(h.bbox.maxX) * 100);
        b.svarint(3, int64_t(h.bbox.maxY) * 100);
        b.svarint(4, int64_t(h.bbox.minY) * 100);
        w.bytes(1, bb);
    }
    for (const QString &f : h.requiredFeatures) w.bytes(4, f.toUtf8().toStdString());
    for (const QString &f : h.optionalFeatures) w.bytes(5, f.toUtf8().toStdString());
    if (!h.writingProgram.isEmpty()) w.bytes(16, h.writingProgram.toUtf8().toStdString());
    if (!h.source.isEmpty()) w.bytes(17, h.source.toUtf8().toStdString());
    if (h.replicationTimestamp) w.varint(32, uint64_t(h.replicationTimestamp));
    if (h.replicationSequence) w.varint(33, uint64_t(h.replicationSequence));
    if (!h.replicationBaseUrl.isEmpty()) w.bytes(34, h.replicationBaseUrl.toUtf8().toStdString());
    return out;
}

std::string encodeBlobFrame(std::string_view type, const std::string &raw, std::string_view indexData, int level) {
    if (raw.size() > MaxBlobSize) return {};
    std::string blob;
    Writer b(blob);
    b.varint(2, raw.size());
    if (level < 0) {
        b.bytes(1, raw);
    } else {
        mz_ulong length = mz_compressBound(mz_ulong(raw.size()));
        std::string z(size_t(length), '\0');
        if (mz_compress2(reinterpret_cast<unsigned char *>(&z[0]), &length, reinterpret_cast<const unsigned char *>(raw.data()),
                         mz_ulong(raw.size()), std::min(level, 9)) != MZ_OK)
            return {};
        z.resize(size_t(length));
        b.bytes(3, z);
    }
    std::string header;
    Writer h(header);
    h.bytes(1, type);
    if (!indexData.empty()) h.bytes(2, indexData);
    h.varint(3, blob.size());
    if (header.size() > MaxBlobHeaderSize || blob.size() > MaxBlobSize) return {};
    std::string frame;
    frame.reserve(4 + header.size() + blob.size());
    const uint32_t n = uint32_t(header.size());
    const char prefix[4] = {char(n >> 24), char(n >> 16), char(n >> 8), char(n)};
    frame.append(prefix, 4);
    frame += header;
    frame += blob;
    return frame;
}

}
