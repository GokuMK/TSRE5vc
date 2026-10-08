/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/geo/osm/OsmClasses.h>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace Osm {

namespace {

// "#rrggbb" or "#aarrggbb".
bool parseColor(const QJsonValue &v, Rgb &out) {
    const QString s = v.toString();
    if (!s.startsWith('#') || (s.size() != 7 && s.size() != 9)) return false;
    bool ok = false;
    const uint32_t value = s.mid(1).toUInt(&ok, 16);
    if (!ok) return false;
    out = s.size() == 7 ? (0xff000000u | value) : value;
    return true;
}

bool parseStroke(const QJsonObject &o, Stroke &s) {
    s.width = float(o.value("width").toDouble(0));
    s.round = o.value("cap").toString("round") == "round";
    return parseColor(o.value("color"), s.color) && s.width >= 0;
}

bool parseStyle(const QJsonObject &o, Style &s, QString &error) {
    if (o.contains("fill")) { s.hasFill = parseColor(o.value("fill"), s.fill); if (!s.hasFill) { error = "bad fill colour"; return false; } }
    if (o.contains("outline")) { s.hasOutline = parseStroke(o.value("outline").toObject(), s.outline); if (!s.hasOutline) { error = "bad outline"; return false; } }
    if (o.contains("casing")) {
        Stroke c;
        if (!parseStroke(o.value("casing").toObject(), c)) { error = "bad casing"; return false; }
        c.round = false;
        s.casings.push_back(c);
    }
    for (const QJsonValue &v : o.value("casings").toArray()) {
        Stroke c;
        if (!parseStroke(v.toObject(), c)) { error = "bad casing"; return false; }
        c.round = false;
        s.casings.push_back(c);
    }
    if (o.contains("line")) { s.hasLine = parseStroke(o.value("line").toObject(), s.line); if (!s.hasLine) { error = "bad line"; return false; } }
    s.maxMetersPerPixel = float(o.value("maxMetersPerPixel").toDouble(0));
    if (s.maxMetersPerPixel < 0) { error = "bad maxMetersPerPixel"; return false; }
    return true;
}

}

bool FeatureClasses::startsWith(std::string_view key, const std::string &p) {
    if (p.empty() || key.size() < p.size()) return false;
    for (size_t i = 0; i < p.size(); ++i) {
        char c = key[i];
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (c != p[i]) return false;
    }
    return true;
}

bool FeatureClasses::startsWith(std::string_view key, const std::vector<std::string> &prefixes) {
    for (const std::string &p : prefixes) if (startsWith(key, p)) return true;
    return false;
}

bool FeatureClasses::load(const QString &path, QString &error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { error = QStringLiteral("Cannot open %1").arg(path); return false; }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) { error = QStringLiteral("%1: %2").arg(path, pe.errorString()); return false; }
    const QJsonObject root = doc.object();
    if (root.value("version").toInt() != 1) { error = QStringLiteral("%1: unsupported version").arg(path); return false; }
    *this = FeatureClasses();

    const QJsonObject rules = root.value("classification").toObject();
    for (const QJsonValue &v : rules.value("skipKeyPrefixes").toArray()) skip_.push_back(v.toString().toLower().toStdString());
    bridgePrefix_ = rules.value("bridgeKeyPrefix").toString().toLower().toStdString();
    tunnelPrefix_ = rules.value("tunnelKeyPrefix").toString().toLower().toStdString();
    buildingPrefix_ = rules.value("buildingKeyPrefix").toString().toLower().toStdString();

    for (const QJsonValue &v : root.value("classes").toArray()) {
        const QJsonObject o = v.toObject();
        const std::string tag = o.value("tag").toString().toLower().toStdString();
        if (tag.find('=') == std::string::npos || byTag_.count(tag)) { error = QStringLiteral("%1: bad or repeated class %2").arg(path, QString::fromStdString(tag)); return false; }
        byTag_[tag] = uint16_t(names_.size());
        names_.push_back(tag);
        layers_.push_back(uint8_t(std::clamp(o.value("layer").toInt(), 0, 255)));
    }
    buildingClass_ = classOf(rules.value("buildingClass").toString().toLower().toStdString());

    if (!parseColor(root.value("background"), background_)) { error = QStringLiteral("%1: bad background").arg(path); return false; }
    if (!parseStyle(root.value("default").toObject(), default_, error)) { error = QStringLiteral("%1: default style: %2").arg(path, error); return false; }

    // First style that names a class exactly wins; otherwise the first key=* style.
    styleOf_.assign(names_.size(), -1);
    std::vector<int> wildcard(names_.size(), -1);
    for (const QJsonValue &v : root.value("styles").toArray()) {
        const QJsonObject o = v.toObject();
        Style s, b;
        if (!parseStyle(o, s, error)) { error = QStringLiteral("%1: style %2: %3").arg(path).arg(styles_.size()).arg(error); return false; }
        if (o.contains("bridge")) {
            if (!parseStyle(o.value("bridge").toObject(), b, error)) { error = QStringLiteral("%1: bridge style: %2").arg(path, error); return false; }
            if (!o.value("bridge").toObject().contains("maxMetersPerPixel")) b.maxMetersPerPixel = s.maxMetersPerPixel;
        } else {
            b = s;
            Rgb bridgeColor = 0;
            if (!s.casings.empty() && parseColor(o.value("casing").toObject().value("bridgeColor"), bridgeColor)) b.casings.front().color = bridgeColor;
        }
        const int index = int(styles_.size());
        styles_.push_back(s);
        bridgeStyles_.push_back(b);
        for (const QJsonValue &t : o.value("tags").toArray()) {
            const std::string tag = t.toString().toLower().toStdString();
            if (tag.size() > 2 && tag.compare(tag.size() - 2, 2, "=*") == 0) {
                const std::string key = tag.substr(0, tag.size() - 1);  // "key="
                for (size_t c = 1; c < names_.size(); ++c)
                    if (wildcard[c] < 0 && names_[c].compare(0, key.size(), key) == 0) wildcard[c] = index;
            } else {
                const uint16_t c = classOf(tag);
                if (!c) { error = QStringLiteral("%1: style names unknown class %2").arg(path, QString::fromStdString(tag)); return false; }
                if (styleOf_[c] < 0) styleOf_[c] = index;
            }
        }
    }
    for (size_t c = 1; c < names_.size(); ++c) if (styleOf_[c] < 0) styleOf_[c] = wildcard[c];
    return true;
}

const FeatureClasses &FeatureClasses::standard() {
    static const FeatureClasses classes = [] {
        FeatureClasses c;
        QString error;
        if (!c.load(QStringLiteral(":/osm/osm-map-classes.json"), error)) qWarning().noquote() << "OSM classes:" << error;
        return c;
    }();
    return classes;
}

uint16_t FeatureClasses::classOf(const std::string &tag) const {
    auto it = byTag_.find(tag);
    return it == byTag_.end() ? 0 : it->second;
}

Classification FeatureClasses::classify(const Feature &f) const {
    return classify(f.tagCount(), [&](uint32_t i) { return f.tag(i); });
}

const Style &FeatureClasses::style(const Classification &c) const {
    const int s = c.cls < styleOf_.size() ? styleOf_[c.cls] : -1;
    if (s < 0) return default_;
    return c.bridge ? bridgeStyles_[size_t(s)] : styles_[size_t(s)];
}

}
