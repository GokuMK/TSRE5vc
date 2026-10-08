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

// Overview layers for large-scale views. Each level is a small sorted PBF next to a
// converted file, <base>.tsre.overview.<level>.pbf, holding only the features its
// rules select (osm-map-classes.json "overview"), with simplified geometry.
// Built in one pass over the converted file; rebuilt when it or the rules change.

#include <tsre/geo/osm/SortedPbfStore.h>
#include <QJsonObject>
#include <QString>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace Osm {

class OsmDirectory;

struct OverviewRule {
    std::vector<std::string> tags;    // "key=value" or "key=*"; any one selects the feature
    std::vector<std::string> unless;  // any one of these rejects it
    uint32_t types = AllTypes;        // FeatureTypes
    double minAreaKm2 = 0;            // > 0: only closed ways and multipolygons this large

    template <class TagAt> bool matchesTags(uint32_t count, TagAt tagAt) const;
};

struct OverviewLevel {
    std::string name;
    double fromMetersPerPixel = 0;    // used for views at this resolution and coarser
    double toleranceMeters = 0;       // Douglas-Peucker tolerance of its geometry
    std::vector<OverviewRule> rules;
};

struct OverviewConfig {
    std::vector<OverviewLevel> levels;  // ascending fromMetersPerPixel
    QString hash;                       // of the rules; overview files record it

    bool load(const QJsonObject &section, QString &error);
    // The "overview" section of the built-in :/osm/osm-map-classes.json.
    static const OverviewConfig &standard();
};

QString overviewPathFor(const QString &convertedPath, const std::string &level);
// True when the level's file exists and was built from this converted file with these rules.
bool overviewUpToDate(const QString &convertedPath, const OverviewConfig &config, size_t level);

struct OverviewStats {
    uint64_t nodes = 0, ways = 0, relations = 0;
    uint64_t pointsIn = 0, pointsOut = 0;  // way points before and after simplification
    uint64_t bytes = 0;
};

// Builds every level of the converted file (atomic .part output).
bool buildOverviews(const QString &convertedPath, const OverviewConfig &config, std::vector<OverviewStats> &stats,
                    QString &error, int threads = 0, const std::atomic_bool *cancel = nullptr);

// Douglas-Peucker in metres; returns the indices kept (always the first and last).
std::vector<uint32_t> simplifyIndices(const Location *points, uint32_t count, double toleranceMeters);

// The detail store plus one store per overview level, chosen by view resolution.
class OsmLayers {
public:
    // Opens the converted files of a scanned directory and their up-to-date overviews. A level
    // is used only when every converted file has it; otherwise its scales fall back to detail.
    bool open(const OsmDirectory &directory, const OverviewConfig &config, QString &error);
    const OsmStore &forScale(double metersPerPixel) const;
    int levelForScale(double metersPerPixel) const;  // -1: detail
    const OsmStore &detail() const { return detail_; }
    bool hasLevel(size_t level) const { return level < levels_.size() && levels_[level]; }

private:
    OverviewConfig config_;
    SortedPbfStore detail_;
    std::vector<std::unique_ptr<SortedPbfStore>> levels_;
};

// The layers of an OSM directory (standard overview rules), opened once for the process and
// shared by their users (the map mode's OSM layer, the tile map), so they share one block
// cache. Reopened when the directory or its converted and overview files change; null
// when it has no converted files. Thread-safe.
std::shared_ptr<const OsmLayers> sharedLayers(const QString &directory, QString &error);

template <class TagAt> bool OverviewRule::matchesTags(uint32_t count, TagAt tagAt) const {
    auto hit = [&](const std::vector<std::string> &patterns) {
        for (uint32_t i = 0; i < count; ++i) {
            const Tag t = tagAt(i);
            for (const std::string &p : patterns) {
                const size_t eq = p.find('=');
                if (t.key != std::string_view(p).substr(0, eq)) continue;
                const std::string_view v = std::string_view(p).substr(eq + 1);
                if (v == "*" || t.value == v) return true;
            }
        }
        return false;
    };
    return hit(tags) && !hit(unless);
}

}
