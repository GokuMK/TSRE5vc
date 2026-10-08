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

// Data-driven OSM feature classes and their map styles (osm-map-classes.json, a Qt
// resource). Replaces the legacy OSMFeatures string table and the if-chain in
// MapDataOSM::draw(). Widths are in metres so any renderer can scale them.

#include <tsre/geo/osm/OsmStore.h>
#include <QString>
#include <string>
#include <unordered_map>
#include <vector>

namespace Osm {

using Rgb = uint32_t;  // 0xAARRGGBB, same layout as QRgb

struct Stroke {
    Rgb color = 0;
    float width = 0;     // metres; 0 draws one pixel
    bool round = true;   // round caps (joins are always round)
};

struct Style {
    bool hasFill = false;
    Rgb fill = 0;
    bool hasOutline = false;
    Stroke outline;
    std::vector<Stroke> casings;   // under the line, in order
    bool hasLine = false;
    Stroke line;
    // Drawn at this resolution and finer (metres per pixel); 0: at every resolution.
    // Vector views skip features out of range; the fixed-scale tile map ignores it.
    float maxMetersPerPixel = 0;

    bool visibleAt(double metersPerPixel) const { return maxMetersPerPixel <= 0 || metersPerPixel <= maxMetersPerPixel; }
};

struct Classification {
    uint16_t cls = 0;      // 0: no class
    uint8_t layer = 0;     // legacy draw layer of the class
    bool bridge = false;
    bool tunnel = false;
};

class FeatureClasses {
public:
    bool load(const QString &path, QString &error);
    // The built-in table (:/osm/osm-map-classes.json), loaded once.
    static const FeatureClasses &standard();

    Classification classify(const Feature &f) const;
    template <class TagAt> Classification classify(uint32_t count, TagAt tagAt) const;

    // Style of a class (bridge variant when bridge); the default style for unstyled classes.
    const Style &style(const Classification &c) const;
    const Style &defaultStyle() const { return default_; }
    Rgb background() const { return background_; }
    // The distinct maxMetersPerPixel of all styles, ascending: the scales where what is drawn changes.
    std::vector<float> scaleRanges() const;

    size_t classCount() const { return names_.size() - 1; }
    uint16_t classOf(const std::string &tag) const;  // "highway=residential", 0 when unknown
    const std::string &name(uint16_t cls) const { return names_[cls < names_.size() ? cls : 0]; }

private:
    static bool startsWith(std::string_view key, const std::string &prefix);
    static bool startsWith(std::string_view key, const std::vector<std::string> &prefixes);

    std::vector<std::string> names_{""};
    std::vector<uint8_t> layers_{0};
    std::unordered_map<std::string, uint16_t> byTag_;
    std::vector<int> styleOf_{-1};
    std::vector<Style> styles_, bridgeStyles_;
    Style default_;
    Rgb background_ = 0;
    std::vector<std::string> skip_;
    std::string bridgePrefix_, tunnelPrefix_, buildingPrefix_;
    uint16_t buildingClass_ = 0;
};

template <class TagAt> Classification FeatureClasses::classify(uint32_t count, TagAt tagAt) const {
    // Same rules as the legacy MapDataOSM loader: key prefixes are skipped or set flags,
    // any building* key makes a building, then key=value picks the class; the last match wins.
    Classification c;
    std::string kv;
    for (uint32_t i = 0; i < count; ++i) {
        const Tag t = tagAt(i);
        if (startsWith(t.key, skip_)) continue;
        if (startsWith(t.key, bridgePrefix_)) { c.bridge = t.value != "no"; continue; }
        if (startsWith(t.key, tunnelPrefix_)) { c.tunnel = t.value != "no"; continue; }
        if (buildingClass_ && startsWith(t.key, buildingPrefix_)) c.cls = buildingClass_;
        kv.assign(t.key);
        kv += '=';
        kv.append(t.value);
        for (char &ch : kv) if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
        auto it = byTag_.find(kv);
        if (it != byTag_.end()) c.cls = it->second;
    }
    c.layer = layers_[c.cls];
    return c;
}

}
