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

// OSM PBF file format: block table scan, blob inflate, PrimitiveBlock decode,
// and the matching block builder and blob framing for writers.
// Specification: https://wiki.openstreetmap.org/wiki/PBF_Format

#include <tsre/geo/osm/OsmProto.h>
#include <tsre/geo/osm/OsmTypes.h>
#include <QFile>
#include <QString>
#include <QStringList>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Osm {

// Limits from the format specification.
constexpr uint32_t MaxBlobHeaderSize = 64 * 1024;
constexpr uint32_t MaxBlobSize = 32 * 1024 * 1024;

struct HeaderInfo {
    Box bbox;  // invalid when the file has none
    QStringList requiredFeatures, optionalFeatures;
    QString writingProgram, source;
    int64_t replicationTimestamp = 0, replicationSequence = 0;
    QString replicationBaseUrl;

    bool hasFeature(const QString &f) const { return requiredFeatures.contains(f) || optionalFeatures.contains(f); }
};

enum class BlobType : uint8_t { Header, Data, Unknown };

struct BlobInfo {
    int64_t offset = 0;      // start of the 4-byte length prefix
    int64_t size = 0;        // whole frame: prefix + BlobHeader + Blob
    int64_t dataOffset = 0;  // start of the Blob message
    uint32_t dataSize = 0;
    BlobType type = BlobType::Unknown;
    std::string indexData;   // BlobHeader.indexdata, empty when absent
};

struct Tag { std::string_view key, value; };

struct Member {
    int64_t ref = 0;
    uint32_t role = 0;  // string table index
    ItemType type = ItemType::Node;
};

// One decoded PrimitiveBlock. Entities keep file order within their kind;
// ranges index the shared arrays below. Views point into raw.
struct PrimitiveBlock {
    struct Node { int64_t id; Location location; uint32_t tagFirst, tagCount; };
    struct Way { int64_t id; uint32_t tagFirst, tagCount, refFirst, refCount; };
    struct Relation { int64_t id; uint32_t tagFirst, tagCount, memberFirst, memberCount; };

    std::vector<uint8_t> raw;
    std::vector<std::string_view> strings;
    std::vector<uint32_t> tagPairs;            // key, value string indices
    std::vector<Node> nodes;
    std::vector<Way> ways;
    std::vector<Relation> relations;
    std::vector<int64_t> refs;
    std::vector<Location> refLocations;        // parallel to refs when wayLocations
    std::vector<Member> members;
    bool wayLocations = false;                 // ways carry LocationsOnWays coordinates

    Tag tag(uint32_t pair) const { return {strings[tagPairs[2 * pair]], strings[tagPairs[2 * pair + 1]]}; }
    std::string_view role(const Member &m) const { return strings[m.role]; }
    void clear();
};

enum DecodeParts : uint32_t { DecodeNodes = 1, DecodeWays = 2, DecodeRelations = 4, DecodeAll = 7 };

// Decodes block.raw in place. Parts not requested are skipped without parsing.
bool decodePrimitiveBlock(PrimitiveBlock &block, uint32_t parts, QString &error);
// Entity kinds present in a block (DecodeParts mask) without decoding entities; 0 when empty or malformed.
uint32_t blockParts(const std::vector<uint8_t> &raw);

bool decodeHeaderBlock(Proto::Span raw, HeaderInfo &header, QString &error);
// Inflates one Blob message (raw or zlib) into out.
bool inflateBlob(Proto::Span blob, std::vector<uint8_t> &out, QString &error);

// Read-only access to a PBF file. open() reads only the frame headers, so it
// stays fast when the file is not in the page cache. readBlob() is thread-safe.
class PbfFile {
public:
    PbfFile() = default;
    ~PbfFile();
    PbfFile(const PbfFile &) = delete;
    PbfFile &operator=(const PbfFile &) = delete;

    bool open(const QString &path, QString &error);
    // Reads only the leading header block: cheap enough for scanning a directory.
    static bool readHeader(const QString &path, HeaderInfo &header, QString &error);
    void close();  // releases the mapping; required before renaming or deleting on Windows
    bool isOpen() const { return file_.isOpen(); }

    const HeaderInfo &header() const { return header_; }
    const std::vector<BlobInfo> &blobs() const { return blobs_; }
    int64_t size() const { return size_; }

    bool readBlob(size_t index, std::vector<uint8_t> &raw, QString &error) const;
    // Inflate and decode in one call; block.raw is reused.
    bool readBlock(size_t index, PrimitiveBlock &block, uint32_t parts, QString &error) const;

private:
    bool ensureMapped(QString &error) const;

    mutable QFile file_;
    mutable const uint8_t *map_ = nullptr;
    int64_t size_ = 0;
    HeaderInfo header_;
    std::vector<BlobInfo> blobs_;
};

// Builds one PrimitiveBlock. Nodes become a DenseNodes group, ways and relations
// their own groups. Strings are copied into the block's string table.
class BlockBuilder {
public:
    struct MemberIn { int64_t ref; ItemType type; std::string_view role; };

    void clear();
    size_t count() const { return nodes_.size() + ways_.size() + relations_.size(); }
    const Box &bounds() const { return bounds_; }

    void addNode(int64_t id, Location location, const Tag *tags, size_t tagCount);
    // locations may be null; when given, every way in the block must have them.
    void addWay(int64_t id, const Tag *tags, size_t tagCount, const int64_t *refs, const Location *locations, size_t refCount);
    void addRelation(int64_t id, const Tag *tags, size_t tagCount, const MemberIn *members, size_t memberCount);

    // Serialised PrimitiveBlock (granularity 100, no offsets: plain 1e-7 units).
    void build(std::string &out);

private:
    uint32_t string(std::string_view s);
    void addTags(const Tag *tags, size_t n, uint32_t &first, uint32_t &count);

    struct Node { int64_t id; Location location; uint32_t tagFirst, tagCount; };
    struct Way { int64_t id; uint32_t tagFirst, tagCount, refFirst, refCount; };
    struct Relation { int64_t id; uint32_t tagFirst, tagCount, memberFirst, memberCount; };
    struct MemberRec { int64_t ref; uint32_t role; ItemType type; };

    std::deque<std::string> stringStorage_;  // stable addresses for the views below
    std::unordered_map<std::string_view, uint32_t> stringIndex_;
    std::vector<std::string_view> strings_;
    std::vector<uint32_t> tagPairs_;
    std::vector<Node> nodes_;
    std::vector<Way> ways_;
    std::vector<Relation> relations_;
    std::vector<int64_t> refs_;
    std::vector<Location> refLocations_;
    std::vector<MemberRec> members_;
    bool wayLocations_ = false;
    Box bounds_;
};

std::string encodeHeaderBlock(const HeaderInfo &header);
// Complete frame: length prefix, BlobHeader (type, optional indexdata), Blob.
// level 0..9 compresses with zlib; level < 0 stores the block uncompressed.
std::string encodeBlobFrame(std::string_view type, const std::string &raw, std::string_view indexData, int level);

}
