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

// The TSRE spatially sorted PBF: a standard PBF with LocationsOnWays whose
// blocks are ordered by place and carry their bbox in BlobHeader.indexdata.
// See docs/tasks/geo/osm-data-design.md.

#include <tsre/geo/osm/OsmTypes.h>
#include <QString>
#include <cstring>
#include <string>

namespace Osm::Sorted {

inline const QString FeatureMarker = QStringLiteral("TSRE-Spatial-1");
inline const QString ConvertedSuffix = QStringLiteral(".tsre.osm.pbf");

// indexdata: version, kind (ItemType), bbox minX minY maxX maxY as int32 little-endian.
constexpr uint8_t IndexVersion = 1;
constexpr size_t IndexSize = 18;

struct BlockIndex {
    ItemType kind = ItemType::Node;
    Box bounds;  // an invalid box means "unknown extent": the block matches every query
};

inline void putLe32(std::string &out, int32_t v) {
    const uint32_t u = uint32_t(v);
    const char b[4] = {char(u), char(u >> 8), char(u >> 16), char(u >> 24)};
    out.append(b, 4);
}
inline int32_t getLe32(const char *p) {
    const auto *u = reinterpret_cast<const uint8_t *>(p);
    return int32_t(uint32_t(u[0]) | uint32_t(u[1]) << 8 | uint32_t(u[2]) << 16 | uint32_t(u[3]) << 24);
}

inline std::string encodeIndex(const BlockIndex &index) {
    std::string out;
    out.push_back(char(IndexVersion));
    out.push_back(char(index.kind));
    putLe32(out, index.bounds.minX); putLe32(out, index.bounds.minY);
    putLe32(out, index.bounds.maxX); putLe32(out, index.bounds.maxY);
    return out;
}
inline bool decodeIndex(const std::string &data, BlockIndex &index) {
    if (data.size() != IndexSize || uint8_t(data[0]) != IndexVersion || uint8_t(data[1]) > 2) return false;
    index.kind = ItemType(uint8_t(data[1]));
    index.bounds.minX = getLe32(&data[2]); index.bounds.minY = getLe32(&data[6]);
    index.bounds.maxX = getLe32(&data[10]); index.bounds.maxY = getLe32(&data[14]);
    return true;
}
inline bool matches(const BlockIndex &index, const Box &query) {
    return !index.bounds.valid() || index.bounds.intersects(query);
}

// Identity of the downloaded file a converted file was made from (HeaderBlock.source).
struct SourceIdentity {
    QString name;
    int64_t size = 0;
    int64_t timestamp = 0;  // osmosis replication timestamp of the source, 0 when absent
    bool operator==(const SourceIdentity &o) const { return name == o.name && size == o.size && timestamp == o.timestamp; }
    bool operator!=(const SourceIdentity &o) const { return !(*this == o); }

    QString toString() const {
        return QStringLiteral("tsre-source size=%1 timestamp=%2 name=%3").arg(size).arg(timestamp).arg(name);
    }
    static bool fromString(const QString &s, SourceIdentity &out) {
        static const QString prefix = QStringLiteral("tsre-source size=");
        if (!s.startsWith(prefix)) return false;
        const int t = s.indexOf(QStringLiteral(" timestamp="), prefix.size());
        const int n = s.indexOf(QStringLiteral(" name="), t < 0 ? 0 : t);
        if (t < 0 || n < 0) return false;
        bool sizeOk = false, timeOk = false;
        out.size = s.mid(prefix.size(), t - prefix.size()).toLongLong(&sizeOk);
        out.timestamp = s.mid(t + 11, n - t - 11).toLongLong(&timeOk);
        out.name = s.mid(n + 6);  // last field: names may contain spaces
        return sizeOk && timeOk && !out.name.isEmpty();
    }
};

// Placement of an entity by its bbox. Level L is the finest grid (1/32, 1/8, 1/2, 2 degrees)
// on which the bbox spans at most 2x2 cells; level 4 takes everything bigger.
// Blocks follow the bucket order, entities inside a bucket follow the key.
struct Placement { uint64_t key = 0; uint32_t bucket = 0; };

inline uint64_t morton(uint32_t x, uint32_t y) {
    uint64_t m = 0;
    for (int i = 0; i < 16; ++i) m |= uint64_t((x >> i) & 1) << (2 * i) | uint64_t((y >> i) & 1) << (2 * i + 1);
    return m;
}
inline Placement place(const Box &b) {
    static const double levelDegrees[] = {1.0 / 32, 1.0 / 8, 0.5, 2.0};
    int level = 0;
    for (; level < 4; ++level) {
        const double d = levelDegrees[level] * CoordinateScale;
        if (std::floor(b.maxX / d) - std::floor(b.minX / d) <= 1 && std::floor(b.maxY / d) - std::floor(b.minY / d) <= 1) break;
    }
    // 1/32 degree cell of the bbox centre, offset to stay positive (cells -8192..8191 cover the globe).
    const double cx = (double(b.minX) + b.maxX) / 2 / CoordinateScale, cy = (double(b.minY) + b.maxY) / 2 / CoordinateScale;
    const uint64_t m = morton(uint32_t(std::floor(cx * 32) + 8192), uint32_t(std::floor(cy * 32) + 8192));
    Placement p;
    p.key = uint64_t(level) << 40 | m;
    // Morton of the 1-degree cell == m >> 10; small features share those buckets, big ones get one per level.
    p.bucket = level <= 1 ? uint32_t(m >> 10) : 0xFFFFFF00u + uint32_t(level);
    return p;
}
constexpr uint32_t UnplacedBucket = 0xFFFFFFFFu;  // entities without coordinates (e.g. relations with no resolvable member)

}
