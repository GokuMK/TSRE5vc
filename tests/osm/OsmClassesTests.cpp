#include <tsre/geo/osm/OsmClasses.h>
#include <tsre/geo/osm/SortedPbfStore.h>
#include "legacy/OSMFeatures.h"
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <map>
#include <vector>

using namespace Osm;

namespace {

// The legacy MapDataOSM::loadData() tag rules, kept verbatim as the parity reference.
struct Legacy { int type = 0; int val2 = 0; };
Legacy legacyClassify(const std::vector<Tag> &tags) {
    Legacy r;
    auto starts = [](std::string_view k, const char *p) {
        for (size_t i = 0; p[i]; ++i) if (i >= k.size() || std::toupper((unsigned char)k[i]) != p[i]) return false;
        return true;
    };
    for (const Tag &t : tags) {
        if (starts(t.key, "ADDR") || starts(t.key, "NAME") || starts(t.key, "ONEWAY") || starts(t.key, "MAXSPEED") || starts(t.key, "SURFACE")) continue;
        if (starts(t.key, "BRIDGE")) { r.val2 = 7; continue; }
        if (starts(t.key, "TUNNEL")) { r.val2 = 6; continue; }
        if (starts(t.key, "AMENITY") || starts(t.key, "BARRIER") || starts(t.key, "WOOD") || starts(t.key, "SPORT")) continue;
        if (starts(t.key, "BUILDING")) r.type = OSMFeatures::LIST["BUILDING_YES"];
        std::string f = std::string(t.key) + "_" + std::string(t.value);
        for (char &c : f) c = char(std::toupper((unsigned char)c));
        auto it = OSMFeatures::LIST.find(f);
        if (it != OSMFeatures::LIST.end() && it->second != 0) r.type = it->second;
    }
    return r;
}

std::string legacyTag(int type) {
    for (const auto &p : OSMFeatures::LIST) {
        if (p.second != type) continue;
        std::string n = p.first;
        for (char &c : n) c = char(std::tolower((unsigned char)c));
        for (const char *key : {"man_made", "public_transport"})
            if (n.rfind(std::string(key) + "_", 0) == 0) return std::string(key) + "=" + n.substr(std::strlen(key) + 1);
        const size_t u = n.find('_');
        return n.substr(0, u) + "=" + n.substr(u + 1);
    }
    return {};
}

// New classification agrees with the legacy one, except that bridge=no is not a bridge.
bool agrees(const FeatureClasses &fc, const std::vector<Tag> &tags, std::string *why = nullptr) {
    const Legacy old = legacyClassify(tags);
    const Classification now = fc.classify(uint32_t(tags.size()), [&](uint32_t i) { return tags[i]; });
    bool bridgeNo = false;
    for (const Tag &t : tags) if (t.value == "no" && (t.key.rfind("bridge", 0) == 0 || t.key.rfind("Bridge", 0) == 0 || t.key.rfind("BRIDGE", 0) == 0)) bridgeNo = true;
    const bool sameClass = old.type ? fc.name(now.cls) == legacyTag(old.type) : now.cls == 0;
    const bool sameLayer = now.layer == (old.type < int(OSMFeatures::LAYER.size()) ? OSMFeatures::LAYER[old.type] : 0);
    const bool sameBridge = bridgeNo ? !now.bridge : now.bridge == (old.val2 == 7);
    if (!(sameClass && sameLayer && sameBridge) && why)
        *why = "legacy " + legacyTag(old.type) + " new " + fc.name(now.cls);
    return sameClass && sameLayer && sameBridge;
}

}

void runClassesTests(const std::function<void(bool, const char *)> &check) {
    FeatureClasses fc;
    QString error;
    check(fc.load(QStringLiteral(TSRE_OSM_CLASSES_JSON), error) && fc.classCount() == 636, "the class table loads with every legacy class");
    check(FeatureClasses::standard().classCount() == 636, "the built-in resource loads");

    bool all = true;
    std::string why;
    for (const auto &p : OSMFeatures::LIST) {
        if (!p.second) continue;
        const std::string tag = legacyTag(p.second);
        const size_t eq = tag.find('=');
        std::vector<Tag> tags{{std::string_view(tag).substr(0, eq), std::string_view(tag).substr(eq + 1)}};
        if (!agrees(fc, tags, &why)) { all = false; std::cerr << "  " << tag << ": " << why << '\n'; }
    }
    check(all, "every legacy class name classifies the same way with the same layer");

    const std::vector<std::vector<Tag>> cases = {
        {{"highway", "residential"}, {"name", "Gdańska"}, {"surface", "asphalt"}},
        {{"Highway", "Residential"}},
        {{"building", "house"}, {"building:levels", "3"}},
        {{"building:part", "yes"}},
        {{"building", "something_odd"}},
        {{"railway", "rail"}, {"bridge", "yes"}, {"layer", "1"}},
        {{"railway", "rail"}, {"bridge:structure", "arch"}},
        {{"highway", "service"}, {"tunnel", "yes"}},
        {{"amenity", "school"}},
        {{"landuse", "forest"}, {"natural", "wood"}},
        {{"natural", "wood"}, {"landuse", "forest"}},
        {{"barrier", "fence"}, {"addr:street", "x"}},
        {{"waterway", "riverbank"}},
        {{"unknown", "tag"}},
        {},
    };
    bool mixed = true;
    for (const auto &c : cases) if (!agrees(fc, c, &why)) { mixed = false; std::cerr << "  case: " << why << '\n'; }
    check(mixed, "tag combinations classify like the legacy loader (skipped keys, flags, last match wins)");
    const std::vector<Tag> noBridge{{"railway", "rail"}, {"bridge", "no"}};
    check(!fc.classify(2, [&](uint32_t i) { return noBridge[i]; }).bridge && agrees(fc, noBridge), "bridge=no is not a bridge (legacy treated it as one)");

    auto styleOf = [&](std::vector<Tag> tags) -> const Style & { return fc.style(fc.classify(uint32_t(tags.size()), [&](uint32_t i) { return tags[i]; })); };
    const Style &residential = styleOf({{"highway", "residential"}});
    check(residential.casings.size() == 1 && residential.casings[0].width == 6 && residential.casings[0].color == 0xffb4b4b4 && !residential.casings[0].round
              && residential.hasLine && residential.line.width == 5 && residential.line.color == 0xfffefefe && residential.line.round && !residential.hasFill,
          "road style: grey casing 6 m flat, white line 5 m round (legacy 12 and 10 px)");
    const Style &residentialBridge = styleOf({{"highway", "residential"}, {"bridge", "yes"}});
    check(residentialBridge.casings.size() == 1 && residentialBridge.casings[0].color == 0xff000000 && residentialBridge.line.color == 0xfffefefe,
          "road bridges get a black casing");
    const Style &railBridge = styleOf({{"railway", "rail"}, {"bridge", "yes"}}), &rail = styleOf({{"railway", "rail"}});
    check(rail.casings.empty() && rail.line.color == 0xff464646 && rail.line.width == 2 && railBridge.casings.size() == 2
              && railBridge.casings[0].color == 0xff000000 && railBridge.casings[0].width == 4 && railBridge.casings[1].color == 0xffffffff
              && railBridge.line.color == 0xff5a5a5a,
          "rail bridges: black and white casings under a grey line");
    const Style &house = styleOf({{"building", "house"}}), &riverbank = styleOf({{"waterway", "riverbank"}}), &canal = styleOf({{"waterway", "canal"}});
    check(house.hasFill && house.fill == 0xffbeadad && house.hasOutline && house.outline.width == 0, "buildings: fill and one-pixel outline");
    check(riverbank.hasFill && riverbank.fill == 0xffb5d0d0 && !riverbank.hasLine && canal.hasLine && canal.line.width == 5 && !canal.hasFill,
          "an exact class beats key=* (riverbank fill, other waterways lines)");
    const Style &unknown = styleOf({{"unknown", "tag"}}), &unstyled = styleOf({{"tourism", "hotel"}});
    check(&unknown == &fc.defaultStyle() && &unstyled == &fc.defaultStyle() && unknown.line.color == 0xff323232 && fc.background() == 0xfff1eee8,
          "unclassified and unstyled features use the default thin dark line");

    const Style &footway = styleOf({{"highway", "footway"}}), &primary = styleOf({{"highway", "primary"}});
    check(house.maxMetersPerPixel == 2.5f && house.visibleAt(2.5) && !house.visibleAt(2.6) && footway.maxMetersPerPixel == 5
              && residential.maxMetersPerPixel == 10 && residentialBridge.maxMetersPerPixel == 10
              && primary.maxMetersPerPixel == 0 && primary.visibleAt(500) && fc.defaultStyle().visibleAt(500),
          "scale ranges: buildings from 2.5 m/px, footways 5, residential roads and their bridges 10, main roads always");

    QTemporaryDir dir;
    auto loadJson = [&](const char *styles, FeatureClasses &out) {
        const QString path = dir.filePath(QStringLiteral("classes.json"));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(QByteArray(R"({"version": 1, "classification": {"bridgeKeyPrefix": "bridge"}, "background": "#ffffff", "default": {"line": {"color": "#000000", "width": 0}},
            "classes": [{"tag": "highway=service", "layer": 0}], "styles": [)") + styles + "]}");
        f.close();
        QString e;
        return out.load(path, e);
    };
    FeatureClasses ranged, negative;
    const std::vector<Tag> serviceBridge{{"highway", "service"}, {"bridge", "yes"}};
    const bool rangedLoads = loadJson(R"({"tags": ["highway=service"], "line": {"color": "#ffffff", "width": 2}, "maxMetersPerPixel": 4,
        "bridge": {"line": {"color": "#000000", "width": 2}}})", ranged);
    check(rangedLoads && ranged.style(ranged.classify(2, [&](uint32_t i) { return serviceBridge[i]; })).maxMetersPerPixel == 4
              && !loadJson(R"({"tags": ["highway=service"], "line": {"color": "#ffffff", "width": 2}, "maxMetersPerPixel": -1})", negative),
          "an explicit bridge style inherits the scale range; a negative range is refused");
}

// Opt-in: --classes <converted files...>  compares legacy and new classification of every way and node.
int compareClasses(const QStringList &files) {
    SortedPbfStore store;
    QString error;
    if (!store.open(files, error)) { std::cerr << error.toStdString() << '\n'; return 1; }
    const FeatureClasses &fc = FeatureClasses::standard();
    uint64_t total = 0, differ = 0, bridgeNo = 0;
    std::map<std::string, uint64_t> examples;
    Filter filter; filter.types = Nodes | Ways;
    store.forEach(store.bounds(), filter, [&](const Feature &f) {
        std::vector<Tag> tags;
        for (uint32_t i = 0; i < f.tagCount(); ++i) tags.push_back(f.tag(i));
        ++total;
        std::string why;
        if (!agrees(fc, tags, &why)) { ++differ; if (examples.size() < 10) examples[why]++; }
        for (const Tag &t : tags) if (t.key.rfind("bridge", 0) == 0 && t.value == "no") { ++bridgeNo; break; }
    }, error);
    std::cout << total << " features, " << differ << " classified differently, " << bridgeNo << " with bridge*=no (now not bridges)\n";
    for (const auto &e : examples) std::cout << "  " << e.first << '\n';
    return differ ? 1 : 0;
}
