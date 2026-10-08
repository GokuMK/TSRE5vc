/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/geo/osm/OsmOverview.h>
#include <tsre/geo/osm/OsmDirectory.h>
#include <tsre/geo/osm/OsmMultipolygon.h>
#include <tsre/geo/osm/OsmGeneralize.h>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Osm {

namespace {

const QString OverviewMarker = QStringLiteral("TSRE-Overview-1");
constexpr size_t MaxBlockEntities = 8000;
constexpr size_t MaxBlockPoints = 400000;  // keeps encoded blocks well below the 32 MiB limit

using Tags = std::vector<std::pair<std::string, std::string>>;

Tags copyTags(const PrimitiveBlock &b, uint32_t first, uint32_t count) {
    Tags t;
    for (uint32_t i = 0; i < count; ++i) { const Tag tag = b.tag(first + i); t.emplace_back(std::string(tag.key), std::string(tag.value)); }
    return t;
}
std::string_view valueOf(const PrimitiveBlock &b, uint32_t first, uint32_t count, std::string_view key) {
    for (uint32_t i = 0; i < count; ++i) { const Tag t = b.tag(first + i); if (t.key == key) return t.value; }
    return {};
}

// Area of a closed ring in km2, with the longitude scale of the ring's latitude.
double ringAreaKm2(const Location *p, size_t n) {
    if (n < 4) return 0;
    double a = 0, lat = 0;
    for (size_t i = 0; i + 1 < n; ++i) { a += double(p[i].x) * p[i + 1].y - double(p[i + 1].x) * p[i].y; lat += p[i].lat(); }
    lat /= double(n - 1);
    const double kmPerUnitY = 110.574 / CoordinateScale, kmPerUnitX = 111.320 * std::cos(lat * M_PI / 180) / CoordinateScale;
    return std::abs(a / 2) * kmPerUnitX * kmPerUnitY;
}

QString identity(const QString &convertedPath, const OverviewConfig &config, size_t level) {
    const QFileInfo info(convertedPath);
    return QStringLiteral("tsre-overview level=%1 rules=%2 size=%3 mtime=%4 name=%5")
        .arg(QString::fromStdString(config.levels[level].name), config.hash)
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch())
        .arg(info.fileName());
}

struct OvNode { int64_t id; Location location; Tags tags; };
struct OvWay { int64_t id; Tags tags; std::vector<int64_t> refs; std::vector<Location> locations; Box box; bool selected; uint32_t sourcePoints; };
struct OvRelation { RelationData data; double minArea; int generalize = -1; };

using AreaRings = std::vector<std::vector<Location>>;  // outer, then holes

struct Level {
    std::vector<OvNode> nodes;
    std::vector<OvWay> ways;
    std::vector<OvRelation> candidates;
    std::vector<int64_t> memberWays;  // sorted
    std::vector<std::vector<AreaRings>> generalize;  // per rule: the areas to merge
};

bool writeLevel(const QString &path, const QString &source, const Box &bbox, Level &lv, const std::vector<RelationData> &relations,
                OverviewStats &stats, QString &error) {
    struct Entry { uint64_t sort; int64_t id; ItemType kind; size_t index; Box box; };
    std::vector<Entry> entries;
    auto add = [&](ItemType kind, int64_t id, size_t index, const Box &box) {
        entries.push_back({uint64_t(kind) << 56 | Sorted::place(box).key, id, kind, index, box});
    };
    for (size_t i = 0; i < lv.nodes.size(); ++i) { Box b; b.extend(lv.nodes[i].location); add(ItemType::Node, lv.nodes[i].id, i, b); }
    for (size_t i = 0; i < lv.ways.size(); ++i) add(ItemType::Way, lv.ways[i].id, i, lv.ways[i].box);
    for (size_t i = 0; i < relations.size(); ++i) add(ItemType::Relation, relations[i].id, i, relations[i].extent);
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.sort != b.sort ? a.sort < b.sort : a.id < b.id; });

    HeaderInfo header;
    header.bbox = bbox;
    header.requiredFeatures << QStringLiteral("OsmSchema-V0.6") << QStringLiteral("DenseNodes");
    header.optionalFeatures << QStringLiteral("LocationsOnWays") << Sorted::FeatureMarker << OverviewMarker;
    header.writingProgram = QStringLiteral("TSRE5");
    header.source = source;
    QFile out(path + QStringLiteral(".part"));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { error = QStringLiteral("Cannot create %1").arg(out.fileName()); return false; }
    auto write = [&](const std::string &frame) { return !frame.empty() && out.write(frame.data(), qint64(frame.size())) == qint64(frame.size()); };
    bool ok = write(encodeBlobFrame("OSMHeader", encodeHeaderBlock(header), {}, 1));
    BlockBuilder b;
    std::vector<Tag> tags;
    std::vector<BlockBuilder::MemberIn> members;
    auto tagsOf = [&](const Tags &t) { tags.clear(); for (const auto &p : t) tags.push_back({p.first, p.second}); };
    for (size_t a = 0; ok && a < entries.size();) {
        b.clear();
        Sorted::BlockIndex index;
        index.kind = entries[a].kind;
        size_t e = a, points = 0;
        for (; e < entries.size() && e - a < MaxBlockEntities && entries[e].kind == entries[a].kind && points < MaxBlockPoints; ++e) {
            const Entry &en = entries[e];
            index.bounds.extend(en.box);
            if (en.kind == ItemType::Node) {
                const OvNode &n = lv.nodes[en.index];
                tagsOf(n.tags);
                b.addNode(n.id, n.location, tags.data(), tags.size());
            } else if (en.kind == ItemType::Way) {
                const OvWay &w = lv.ways[en.index];
                tagsOf(w.tags);
                b.addWay(w.id, tags.data(), tags.size(), w.refs.data(), w.locations.data(), w.refs.size());
                points += w.refs.size();
            } else {
                const RelationData &r = relations[en.index];
                tagsOf(r.tags);
                members.clear();
                for (const auto &m : r.members) members.push_back({m.ref, m.type, m.role});
                b.addRelation(r.id, tags.data(), tags.size(), members.data(), members.size(), r.extent);
                points += r.members.size();
            }
        }
        std::string raw;
        b.build(raw);
        ok = write(encodeBlobFrame("OSMData", raw, Sorted::encodeIndex(index), 1));
        a = e;
    }
    stats.bytes = uint64_t(out.size());
    out.close();
    if (!ok) { out.remove(); error = QStringLiteral("Cannot write %1").arg(path); return false; }
    QFile::remove(path);
    if (!QFile::rename(path + QStringLiteral(".part"), path)) { error = QStringLiteral("Cannot rename to %1").arg(path); return false; }
    return true;
}

}

bool OverviewConfig::load(const QJsonObject &section, QString &error) {
    levels.clear();
    for (const QJsonValue &lv : section.value("levels").toArray()) {
        const QJsonObject o = lv.toObject();
        OverviewLevel level;
        level.name = o.value("name").toString().toStdString();
        level.fromMetersPerPixel = o.value("fromMetersPerPixel").toDouble(-1);
        level.toleranceMeters = o.value("toleranceMeters").toDouble(0);
        if (level.name.empty() || level.fromMetersPerPixel <= 0 || level.toleranceMeters < 0) { error = QStringLiteral("overview level needs name, fromMetersPerPixel and toleranceMeters"); return false; }
        for (const QJsonValue &rv : o.value("rules").toArray()) {
            const QJsonObject r = rv.toObject();
            OverviewRule rule;
            for (const QJsonValue &t : r.value("tags").toArray()) rule.tags.push_back(t.toString().toStdString());
            for (const QJsonValue &t : r.value("unless").toArray()) rule.unless.push_back(t.toString().toStdString());
            if (r.contains("types")) {
                rule.types = 0;
                for (const QJsonValue &t : r.value("types").toArray()) {
                    const QString s = t.toString();
                    rule.types |= s == "node" ? Nodes : s == "way" ? Ways : s == "relation" ? Relations : 0;
                }
            }
            rule.minAreaKm2 = r.value("minAreaKm2").toDouble(0);
            if (r.contains("generalize")) {
                const QJsonObject g = r.value("generalize").toObject();
                rule.generalizeCellMeters = g.value("cellMeters").toDouble(0);
                rule.generalizeCloseMeters = g.value("closeMeters").toDouble(0);
                rule.generalizeMinHoleKm2 = g.value("minHoleKm2").toDouble(0.05);
                rule.generalizeTag = g.value("tag").toString().toStdString();
                const size_t eq = rule.generalizeTag.find('=');
                if (rule.generalizeCellMeters <= 0 || eq == std::string::npos || eq == 0 || eq + 1 == rule.generalizeTag.size()) {
                    error = QStringLiteral("overview generalize needs cellMeters and a key=value tag");
                    return false;
                }
            }
            for (const std::string &p : rule.tags)
                if (p.find('=') == std::string::npos) { error = QStringLiteral("overview tag must be key=value or key=*: %1").arg(QString::fromStdString(p)); return false; }
            if (rule.tags.empty() || !rule.types) { error = QStringLiteral("overview rule needs tags and types"); return false; }
            level.rules.push_back(std::move(rule));
        }
        levels.push_back(std::move(level));
    }
    std::sort(levels.begin(), levels.end(), [](const OverviewLevel &a, const OverviewLevel &b) { return a.fromMetersPerPixel < b.fromMetersPerPixel; });
    hash = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(section).toJson(QJsonDocument::Compact), QCryptographicHash::Sha1).toHex().left(12));
    return true;
}

const OverviewConfig &OverviewConfig::standard() {
    static const OverviewConfig config = [] {
        OverviewConfig c;
        QFile f(QStringLiteral(":/osm/osm-map-classes.json"));
        QString error;
        if (!f.open(QIODevice::ReadOnly) || !c.load(QJsonDocument::fromJson(f.readAll()).object().value("overview").toObject(), error))
            qWarning().noquote() << "OSM overview rules:" << (error.isEmpty() ? QStringLiteral("cannot read the class table") : error);
        return c;
    }();
    return config;
}

QString overviewPathFor(const QString &convertedPath, const std::string &level) {
    const QFileInfo info(convertedPath);
    QString base = info.fileName();
    for (const QString &suffix : {Sorted::ConvertedSuffix, QStringLiteral(".osm.pbf")})
        if (base.endsWith(suffix, Qt::CaseInsensitive)) { base.chop(suffix.size()); break; }
    return info.dir().filePath(base + QStringLiteral(".tsre.overview.") + QString::fromStdString(level) + QStringLiteral(".pbf"));
}

bool overviewUpToDate(const QString &convertedPath, const OverviewConfig &config, size_t level) {
    if (level >= config.levels.size()) return false;
    HeaderInfo h;
    QString error;
    return PbfFile::readHeader(overviewPathFor(convertedPath, config.levels[level].name), h, error) && h.hasFeature(OverviewMarker)
           && h.source == identity(convertedPath, config, level);
}

std::vector<uint32_t> simplifyIndices(const Location *p, uint32_t n, double tolerance) {
    std::vector<uint32_t> kept;
    if (n <= 2 || tolerance <= 0) { for (uint32_t i = 0; i < n; ++i) kept.push_back(i); return kept; }
    // Local metres around the first point.
    const double lat0 = p[0].lat() * M_PI / 180;
    const double sx = 111320.0 * std::cos(lat0) / CoordinateScale, sy = 110574.0 / CoordinateScale;
    auto xy = [&](uint32_t i) { return std::make_pair(p[i].x * sx, p[i].y * sy); };
    std::vector<bool> keep(n, false);
    keep[0] = keep[n - 1] = true;
    std::vector<std::pair<uint32_t, uint32_t>> stack;
    if (p[0] == p[n - 1]) {
        // Closed ring: split at the point farthest from the start.
        uint32_t far = 1; double best = -1;
        const auto a = xy(0);
        for (uint32_t i = 1; i + 1 < n; ++i) { const auto b = xy(i); const double d = (b.first - a.first) * (b.first - a.first) + (b.second - a.second) * (b.second - a.second); if (d > best) { best = d; far = i; } }
        keep[far] = true;
        stack.push_back({0, far});
        stack.push_back({far, n - 1});
    } else {
        stack.push_back({0, n - 1});
    }
    const double tol2 = tolerance * tolerance;
    while (!stack.empty()) {
        const auto [s, e] = stack.back();
        stack.pop_back();
        if (e <= s + 1) continue;
        const auto a = xy(s), b = xy(e);
        const double dx = b.first - a.first, dy = b.second - a.second, len2 = dx * dx + dy * dy;
        uint32_t worst = 0; double worstD = -1;
        for (uint32_t i = s + 1; i < e; ++i) {
            const auto c = xy(i);
            double d;
            if (len2 == 0) d = (c.first - a.first) * (c.first - a.first) + (c.second - a.second) * (c.second - a.second);
            else {
                const double t = std::clamp(((c.first - a.first) * dx + (c.second - a.second) * dy) / len2, 0.0, 1.0);
                const double px = a.first + t * dx - c.first, py = a.second + t * dy - c.second;
                d = px * px + py * py;
            }
            if (d > worstD) { worstD = d; worst = i; }
        }
        if (worstD > tol2) { keep[worst] = true; stack.push_back({s, worst}); stack.push_back({worst, e}); }
    }
    for (uint32_t i = 0; i < n; ++i) if (keep[i]) kept.push_back(i);
    return kept;
}

bool buildOverviews(const QString &convertedPath, const OverviewConfig &config, std::vector<OverviewStats> &stats,
                    QString &error, int threads, const std::atomic_bool *cancel) {
    stats.assign(config.levels.size(), OverviewStats());
    if (config.levels.empty()) return true;
    PbfFile file;
    if (!file.open(convertedPath, error, convertedPath + QStringLiteral(".idx"))) return false;
    if (!file.header().hasFeature(Sorted::FeatureMarker)) { error = QStringLiteral("%1 is not a converted OSM file").arg(convertedPath); return false; }
    std::vector<size_t> nodeBlocks, wayBlocks, relationBlocks;
    for (size_t i = 0; i < file.blobs().size(); ++i) {
        Sorted::BlockIndex index;
        if (file.blobs()[i].type != BlobType::Data || !Sorted::decodeIndex(file.blobs()[i].indexData, index)) continue;
        (index.kind == ItemType::Node ? nodeBlocks : index.kind == ItemType::Way ? wayBlocks : relationBlocks).push_back(i);
    }
    const size_t L = config.levels.size();
    if (threads <= 0) threads = int(std::max(1u, std::thread::hardware_concurrency()));
    std::mutex mutex;
    QString firstError;
    std::atomic_bool failed{false};
    auto stopped = [&] { return failed.load() || (cancel && cancel->load()); };
    // Runs fn(block, level data per worker) over blocks on worker threads.
    auto parallel = [&](const std::vector<size_t> &blocks, uint32_t parts, const std::function<void(const PrimitiveBlock &, std::vector<Level> &)> &fn,
                        std::vector<std::vector<Level>> &perWorker) {
        perWorker.assign(size_t(threads), std::vector<Level>(L));
        std::atomic<size_t> next{0};
        std::vector<std::thread> pool;
        for (int w = 0; w < threads; ++w)
            pool.emplace_back([&, w] {
                PrimitiveBlock b;
                QString e;
                for (size_t k; !stopped() && (k = next++) < blocks.size();) {
                    if (!file.readBlock(blocks[k], b, parts, e)) { std::lock_guard<std::mutex> l(mutex); if (!failed.exchange(true)) firstError = e; return; }
                    fn(b, perWorker[size_t(w)]);
                }
            });
        for (auto &t : pool) t.join();
    };
    std::vector<Level> levels(L);
    std::vector<std::vector<Level>> parts;

    // Relations: multipolygons selected by a rule; their member ways are needed for assembly.
    parallel(relationBlocks, DecodeRelations, [&](const PrimitiveBlock &b, std::vector<Level> &out) {
        for (const auto &r : b.relations) {
            const std::string_view type = valueOf(b, r.tagFirst, r.tagCount, "type");
            if (type != "multipolygon" && type != "boundary") continue;
            for (size_t l = 0; l < L; ++l) {
                double minArea = -1;
                int generalize = -1;
                const auto &rules = config.levels[l].rules;
                for (size_t k = 0; k < rules.size(); ++k) {
                    const OverviewRule &rule = rules[k];
                    if (!(rule.types & Relations) || !rule.matchesTags(r.tagCount, [&](uint32_t i) { return b.tag(r.tagFirst + i); })) continue;
                    // A generalizing rule takes the area for merging; its own size does not matter.
                    if (rule.generalizes()) { generalize = int(k); minArea = 0; break; }
                    minArea = minArea < 0 ? rule.minAreaKm2 : std::min(minArea, rule.minAreaKm2);
                }
                if (minArea < 0) continue;
                OvRelation rel;
                rel.minArea = minArea;
                rel.generalize = generalize;
                rel.data.id = r.id;
                rel.data.tags = copyTags(b, r.tagFirst, r.tagCount);
                for (uint32_t m = 0; m < r.memberCount; ++m) {
                    const Member &mem = b.members[r.memberFirst + m];
                    const std::string_view role = b.role(mem);
                    if (mem.type != ItemType::Way || (role != "outer" && role != "inner" && !role.empty())) continue;
                    rel.data.members.push_back({mem.ref, mem.type, std::string(role)});
                    out[l].memberWays.push_back(mem.ref);
                }
                out[l].candidates.push_back(std::move(rel));
            }
        }
    }, parts);
    for (auto &p : parts)
        for (size_t l = 0; l < L; ++l) {
            for (auto &c : p[l].candidates) levels[l].candidates.push_back(std::move(c));
            levels[l].memberWays.insert(levels[l].memberWays.end(), p[l].memberWays.begin(), p[l].memberWays.end());
        }
    for (auto &lv : levels) {
        std::sort(lv.memberWays.begin(), lv.memberWays.end());
        lv.memberWays.erase(std::unique(lv.memberWays.begin(), lv.memberWays.end()), lv.memberWays.end());
    }

    // Nodes selected by a rule (places, stations).
    parallel(nodeBlocks, DecodeNodes, [&](const PrimitiveBlock &b, std::vector<Level> &out) {
        for (const auto &n : b.nodes)
            for (size_t l = 0; l < L; ++l)
                for (const OverviewRule &rule : config.levels[l].rules)
                    if ((rule.types & Nodes) && rule.minAreaKm2 <= 0 && rule.matchesTags(n.tagCount, [&](uint32_t i) { return b.tag(n.tagFirst + i); })) {
                        out[l].nodes.push_back({n.id, n.location, copyTags(b, n.tagFirst, n.tagCount)});
                        break;
                    }
    }, parts);
    for (auto &p : parts) for (size_t l = 0; l < L; ++l) for (auto &n : p[l].nodes) levels[l].nodes.push_back(std::move(n));

    // Ways: selected by a rule or needed by a candidate multipolygon; simplified per level.
    parallel(wayBlocks, DecodeWays, [&](const PrimitiveBlock &b, std::vector<Level> &out) {
        for (const auto &w : b.ways) {
            if (w.refCount < 2 || !b.wayLocations) continue;
            const Location *loc = b.refLocations.data() + w.refFirst;
            const bool closed = w.refCount >= 4 && b.refs[w.refFirst] == b.refs[w.refFirst + w.refCount - 1];
            double area = -1;
            for (size_t l = 0; l < L; ++l) {
                bool selected = false;
                const auto &rules = config.levels[l].rules;
                for (size_t k = 0; k < rules.size(); ++k) {
                    const OverviewRule &rule = rules[k];
                    if (!(rule.types & Ways) || !rule.matchesTags(w.tagCount, [&](uint32_t i) { return b.tag(w.tagFirst + i); })) continue;
                    if (rule.generalizes()) {
                        if (closed) {
                            out[l].generalize.resize(rules.size());
                            out[l].generalize[k].push_back(AreaRings{std::vector<Location>(loc, loc + w.refCount)});
                        }
                        break;
                    }
                    if (rule.minAreaKm2 > 0) {
                        if (!closed) continue;
                        if (area < 0) area = ringAreaKm2(loc, w.refCount);
                        if (area < rule.minAreaKm2) continue;
                    }
                    selected = true;
                    break;
                }
                const bool member = std::binary_search(levels[l].memberWays.begin(), levels[l].memberWays.end(), w.id);
                if (!selected && !member) continue;
                OvWay way;
                way.id = w.id;
                way.selected = selected;
                way.sourcePoints = w.refCount;
                way.tags = copyTags(b, w.tagFirst, w.tagCount);
                for (uint32_t i : simplifyIndices(loc, w.refCount, config.levels[l].toleranceMeters)) {
                    way.refs.push_back(b.refs[w.refFirst + i]);
                    way.locations.push_back(loc[i]);
                    way.box.extend(loc[i]);
                }
                out[l].ways.push_back(std::move(way));
            }
        }
    }, parts);
    if (stopped()) { error = failed ? firstError : QStringLiteral("Overview build cancelled"); return false; }
    for (auto &p : parts)
        for (size_t l = 0; l < L; ++l) {
            for (auto &w : p[l].ways) levels[l].ways.push_back(std::move(w));
            levels[l].generalize.resize(config.levels[l].rules.size());
            for (size_t k = 0; k < p[l].generalize.size(); ++k)
                for (auto &a : p[l].generalize[k]) levels[l].generalize[k].push_back(std::move(a));
        }
    // Generalized areas get ids of their own, negative and distinct per file and level,
    // so overlapping files never drop each other's areas as duplicates.
    int64_t fileTag = 0;
    {
        uint32_t h = 2166136261u;  // FNV-1a of the file name: the same on every run
        for (char c : QFileInfo(convertedPath).fileName().toUtf8()) { h ^= uint8_t(c); h *= 16777619u; }
        fileTag = int64_t(h & 0xfffff);
    }

    for (size_t l = 0; l < L; ++l) {
        Level &lv = levels[l];
        // Multipolygons: keep those that assemble and are large enough; drop member ways nobody needs.
        std::unordered_map<int64_t, size_t> byId;
        for (size_t i = 0; i < lv.ways.size(); ++i) byId[lv.ways[i].id] = i;
        std::vector<WayGeometry> geometry(lv.ways.size());
        auto lookup = [&](int64_t id) -> const WayGeometry * {
            auto it = byId.find(id);
            if (it == byId.end()) return nullptr;
            WayGeometry &g = geometry[it->second];
            if (g.refs.empty()) { g.refs = lv.ways[it->second].refs; g.locations = lv.ways[it->second].locations; }
            return &g;
        };
        std::vector<RelationData> kept;
        std::vector<bool> needed(lv.ways.size(), false);
        for (OvRelation &c : lv.candidates) {
            const MultipolygonResult mp = assembleMultipolygon(c.data, lookup);
            double area = 0;
            Box extent;
            for (const Polygon &p : mp.polygons) {
                area += ringAreaKm2(p.outer.data(), p.outer.size());
                for (const auto &in : p.inners) area -= ringAreaKm2(in.data(), in.size());
                for (const Location &v : p.outer) extent.extend(v);
            }
            if (c.generalize >= 0) {
                for (const Polygon &p : mp.polygons) {
                    AreaRings rings{p.outer};
                    rings.insert(rings.end(), p.inners.begin(), p.inners.end());
                    lv.generalize[size_t(c.generalize)].push_back(std::move(rings));
                }
                continue;
            }
            if (!mp.ok() || area < c.minArea) continue;
            c.data.extent = extent;
            for (const auto &m : c.data.members) { auto it = byId.find(m.ref); if (it != byId.end()) needed[it->second] = true; }
            kept.push_back(std::move(c.data));
        }
        std::vector<OvWay> ways;
        for (size_t i = 0; i < lv.ways.size(); ++i) if (lv.ways[i].selected || needed[i]) ways.push_back(std::move(lv.ways[i]));
        lv.ways = std::move(ways);
        OverviewStats &st = stats[l];
        int64_t nextId = 0;
        auto newId = [&] { return -((fileTag << 40) | (int64_t(l) << 36) | ++nextId); };
        for (size_t k = 0; k < lv.generalize.size(); ++k) {
            const OverviewRule &rule = config.levels[l].rules[k];
            if (lv.generalize[k].empty()) continue;
            if (stopped()) { error = QStringLiteral("Overview build cancelled"); return false; }
            GeneralizeOptions options;
            options.cellMeters = rule.generalizeCellMeters;
            options.closeMeters = rule.generalizeCloseMeters;
            options.minAreaKm2 = rule.minAreaKm2;
            options.minHoleKm2 = rule.generalizeMinHoleKm2;
            options.toleranceMeters = config.levels[l].toleranceMeters;
            st.generalizedIn += lv.generalize[k].size();
            const std::vector<GeneralizedArea> areas = generalizeAreas(lv.generalize[k], options);
            std::vector<AreaRings>().swap(lv.generalize[k]);
            const size_t eq = rule.generalizeTag.find('=');
            const std::pair<std::string, std::string> tag(rule.generalizeTag.substr(0, eq), rule.generalizeTag.substr(eq + 1));
            auto ringWay = [&](const std::vector<Location> &ring, bool tagged) {
                OvWay w;
                w.id = newId();
                if (tagged) w.tags.push_back(tag);
                w.selected = true;
                w.sourcePoints = uint32_t(ring.size());
                w.locations = ring;
                for (size_t i = 0; i + 1 < ring.size(); ++i) w.refs.push_back(newId());
                w.refs.push_back(w.refs.front());
                for (const Location &v : ring) w.box.extend(v);
                return w;
            };
            for (const GeneralizedArea &a : areas) {
                if (a.holes.empty()) { lv.ways.push_back(ringWay(a.outer, true)); continue; }
                RelationData rel;
                rel.id = newId();
                rel.tags = {{"type", "multipolygon"}, tag};
                lv.ways.push_back(ringWay(a.outer, false));
                rel.members.push_back({lv.ways.back().id, ItemType::Way, "outer"});
                rel.extent = lv.ways.back().box;
                for (const auto &h : a.holes) {
                    lv.ways.push_back(ringWay(h, false));
                    rel.members.push_back({lv.ways.back().id, ItemType::Way, "inner"});
                }
                kept.push_back(std::move(rel));
            }
            st.generalizedAreas += areas.size();
        }
        st.nodes = lv.nodes.size(); st.ways = lv.ways.size(); st.relations = kept.size();
        for (const OvWay &w : lv.ways) { st.pointsIn += w.sourcePoints; st.pointsOut += w.refs.size(); }
        const QString path = overviewPathFor(convertedPath, config.levels[l].name);
        if (!writeLevel(path, identity(convertedPath, config, l), file.header().bbox, lv, kept, st, error)) return false;
    }
    return true;
}

bool OsmLayers::open(const OsmDirectory &directory, const OverviewConfig &config, QString &error) {
    config_ = config;
    levels_.clear();
    if (!detail_.open(directory, error)) return false;
    const auto converted = directory.convertedFiles();
    for (size_t l = 0; l < config.levels.size(); ++l) {
        QStringList files;
        bool complete = !converted.empty();
        for (const DirectoryEntry *e : converted) {
            if (!overviewUpToDate(e->path, config, l)) { complete = false; break; }
            files << overviewPathFor(e->path, config.levels[l].name);
        }
        std::unique_ptr<SortedPbfStore> store;
        if (complete) {
            store = std::make_unique<SortedPbfStore>();
            if (!store->open(files, error)) return false;
        }
        levels_.push_back(std::move(store));
    }
    return true;
}

std::shared_ptr<const OsmLayers> sharedLayers(const QString &directory, QString &error) {
    static std::mutex mutex;
    static std::shared_ptr<OsmLayers> layers;
    static QString signature;
    OsmDirectory scanned;
    if (!scanned.scan(directory, error)) return nullptr;
    const OverviewConfig &config = OverviewConfig::standard();
    QString now = directory + QLatin1Char(';');
    for (const DirectoryEntry *e : scanned.convertedFiles()) {
        now += e->path + QLatin1Char('|') + QString::number(e->size) + QLatin1Char('|')
               + QString::number(QFileInfo(e->path).lastModified().toMSecsSinceEpoch());
        for (size_t l = 0; l < config.levels.size(); ++l) now += overviewUpToDate(e->path, config, l) ? QLatin1Char('+') : QLatin1Char('-');
        now += QLatin1Char(';');
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (layers && now == signature) return layers;
    layers.reset();
    signature.clear();
    if (scanned.convertedFiles().empty()) { error = QStringLiteral("No converted OSM files in %1").arg(directory); return nullptr; }
    auto opened = std::make_shared<OsmLayers>();
    if (!opened->open(scanned, config, error)) return nullptr;
    layers = opened;
    signature = now;
    return layers;
}

int OsmLayers::levelForScale(double metersPerPixel) const {
    int level = -1;
    for (size_t l = 0; l < config_.levels.size(); ++l)
        if (metersPerPixel >= config_.levels[l].fromMetersPerPixel) level = int(l);
    return level;
}

const OsmStore &OsmLayers::forScale(double metersPerPixel) const {
    // The finest available level at or below the wanted one; detail when none is built.
    for (int l = levelForScale(metersPerPixel); l >= 0; --l)
        if (levels_[size_t(l)]) return *levels_[size_t(l)];
    return detail_;
}

}
