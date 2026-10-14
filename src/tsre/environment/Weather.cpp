/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/environment/Weather.h>
#include <tsre/environment/WeatherDetail.h>
#include <tsre/environment/WeatherPresets.h>

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace {

constexpr double SecondsPerHour = 3600.0;

const char *const LayerNames[WeatherLayer::Count] = {"low", "mid", "high"};

struct TypeName {
    const char *name;
    float value;
};
const TypeName LowTypes[] = {{"stratus", 0.0f}, {"stratocumulus", 0.33f}, {"cumulus", 0.67f},
                             {"cumulonimbus", 1.0f}};
const TypeName MidTypes[] = {{"altostratus", 0.0f}, {"altocumulus", 0.6f}};
const TypeName HighTypes[] = {{"cirrostratus", 0.0f}, {"cirrus", 0.5f}, {"cirrocumulus", 1.0f}};

QString precipitationName(WeatherPrecipitation p) {
    switch (p) {
    case WeatherPrecipitation::Rain: return QStringLiteral("rain");
    case WeatherPrecipitation::Snow: return QStringLiteral("snow");
    default: return QStringLiteral("none");
    }
}

double mixAngle(double a, double b, double w) {
    double d = std::fmod(b - a, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    double r = std::fmod(a + d * w, 360.0);
    return r < 0.0 ? r + 360.0 : r;
}

// Reads a block's values as numbers; false when there are fewer.
bool numbers(const EnvNode &block, int count, double *out) {
    for (int i = 0; i < count; ++i)
        if (!block.numberAt(i, out[i]))
            return false;
    return true;
}

} // namespace

QString WeatherLayer::name(int layer) {
    return layer >= 0 && layer < Count ? QString::fromLatin1(LayerNames[layer]) : QString();
}

int WeatherLayer::fromName(const QString &name) {
    for (int i = 0; i < Count; ++i)
        if (name.compare(QLatin1String(LayerNames[i]), Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

QStringList Weather::typeNames(int layer) {
    QStringList names;
    auto add = [&names](const auto &list) { for (const auto &t : list) names << t.name; };
    if (layer == WeatherLayer::Low) add(LowTypes);
    else if (layer == WeatherLayer::Mid) add(MidTypes);
    else if (layer == WeatherLayer::High) add(HighTypes);
    return names;
}

float Weather::typeValue(const QString &name, bool *ok) {
    auto find = [&name](const auto &list, float &out) {
        for (const auto &t : list)
            if (name.compare(QLatin1String(t.name), Qt::CaseInsensitive) == 0) {
                out = t.value;
                return true;
            }
        return false;
    };
    float value = 0.0f;
    bool found = find(LowTypes, value) || find(MidTypes, value) || find(HighTypes, value);
    if (!found) {
        value = name.toFloat(&found);
        value = std::clamp(value, 0.0f, 1.0f);
    }
    if (ok) *ok = found;
    return value;
}

Weather Weather::fromNode(const EnvNode &block, QStringList &warnings) {
    Weather w;
    w.source.clear();
    // The preset first: keyframes start from its values.
    for (const EnvNode &item : block.items)
        if (item.block && item.text.compare("tsre_weather_preset", Qt::CaseInsensitive) == 0
                && !item.values().isEmpty())
            w.preset = item.values().first();
    if (!w.preset.isEmpty() && !WeatherPresets::exists(w.preset))
        warnings << "Unknown weather preset " + w.preset;

    bool sourceRead = false;
    for (const EnvNode &item : block.items) {
        if (!item.block) {
            warnings << "Skipped value " + item.text + " in tsre_weather";
            continue;
        }
        const QString name = item.text.toLower();
        const QStringList values = item.values();
        double v[5];
        if (name == "tsre_weather_version") {
            if (numbers(item, 1, v)) w.version = int(v[0]);
        } else if (name == "tsre_weather_preset") {
            // Read above.
        } else if (name == "tsre_weather_seed") {
            if (numbers(item, 1, v)) w.seed = quint32(qint64(v[0]));
        } else if (name == "tsre_weather_source") {
            w.source = values.value(0);
            w.attribution = values.value(1);
            sourceRead = true;
        } else if (name == "tsre_weather_start") {
            w.start = QDateTime::fromString(values.value(0), Qt::ISODate);
            if (!w.start.isValid())
                warnings << "Invalid tsre_weather_start " + values.value(0);
            else
                w.start = w.start.toUTC();
        } else if (name == "tsre_weather_grid") {
            if (numbers(item, 5, v)) {
                w.grid.tileX = int(v[0]);
                w.grid.tileZ = int(v[1]);
                w.grid.cellSize = int(v[2]);
                w.grid.columns = int(v[3]);
                w.grid.rows = int(v[4]);
                const double tiles = std::log2(w.grid.cellSize / 2048.0);
                w.grid.valid = w.grid.columns > 0 && w.grid.rows > 0 && w.grid.cellSize > 0
                        && std::abs(tiles - std::round(tiles)) < 1e-9;
                if (!w.grid.valid)
                    warnings << "Invalid tsre_weather_grid: cells must be 2048 m times a power of two";
            } else {
                warnings << "Incomplete tsre_weather_grid";
            }
        } else if (name == "tsre_weather_maps") {
            w.maps = values.value(0);
        } else if (name == "tsre_weather_keyframe") {
            double time = 0.0;
            for (const EnvNode &field : item.items)
                if (field.block && field.text.compare("tsre_weather_time", Qt::CaseInsensitive) == 0)
                    field.numberAt(0, time);
            WeatherKeyframe k = w.preset.isEmpty() ? WeatherKeyframe() : WeatherPresets::at(w.preset, time);
            k.time = float(time);
            k.unknown.clear();
            for (const EnvNode &field : item.items) {
                if (!field.block) {
                    warnings << "Skipped value " + field.text + " in tsre_weather_keyframe";
                    continue;
                }
                const QString f = field.text.toLower();
                const QStringList fv = field.values();
                if (f == "tsre_weather_time") {
                    // Read above.
                } else if (f == "tsre_weather_wind") {
                    const int layer = WeatherLayer::fromName(fv.value(0));
                    double d, s;
                    if (layer >= 0 && field.numberAt(1, d) && field.numberAt(2, s))
                        k.wind[layer] = {float(d), float(s)};
                    else
                        warnings << "Invalid tsre_weather_wind";
                } else if (f == "tsre_weather_precipitation") {
                    const QString type = fv.value(0).toLower();
                    k.precipitation = type == "rain" ? WeatherPrecipitation::Rain
                                    : type == "snow" ? WeatherPrecipitation::Snow
                                                     : WeatherPrecipitation::None;
                    if (type != "rain" && type != "snow" && type != "none")
                        warnings << "Unknown precipitation " + type;
                    double rate = 0.0;
                    field.numberAt(1, rate);
                    k.precipitationRate = float(rate);
                } else if (f == "tsre_weather_visibility") {
                    if (field.numberAt(0, v[0])) k.visibility = float(v[0]);
                } else if (f == "tsre_weather_air") {
                    if (numbers(field, 2, v))
                        k.air = {float(v[0]), float(v[1])};
                    else
                        warnings << "Invalid tsre_weather_air";
                } else if (f == "tsre_weather_fog") {
                    const QString mode = fv.value(0).toLower();
                    if (mode == "auto") {
                        k.fog.mode = WeatherFogSetting::Auto;
                    } else if (mode == "off") {
                        k.fog.mode = WeatherFogSetting::Off;
                    } else if (field.numberAt(0, v[0])) {
                        k.fog.mode = WeatherFogSetting::Set;
                        k.fog.amount = float(std::clamp(v[0], 0.0, 1.0));
                        if (field.numberAt(1, v[1])) k.fog.visibility = float(std::max(v[1], 10.0));
                        if (field.numberAt(2, v[2])) k.fog.depth = float(std::max(v[2], 1.0));
                    } else {
                        warnings << "Invalid tsre_weather_fog " + mode;
                    }
                } else if (f == "tsre_weather_variation") {
                    if (field.numberAt(0, v[0])) k.variationAmount = float(v[0]);
                    if (field.numberAt(1, v[1])) k.variationScale = float(v[1]);
                } else if (f == "tsre_weather_layer") {
                    const int layer = WeatherLayer::fromName(fv.value(0));
                    bool typeOk = false;
                    const float type = typeValue(fv.value(1), &typeOk);
                    if (layer < 0 || !typeOk || fv.size() < 6) {
                        warnings << "Invalid tsre_weather_layer";
                        continue;
                    }
                    double n[4];
                    for (int i = 0; i < 4; ++i)
                        field.numberAt(2 + i, n[i]);
                    k.layers[layer] = {type, float(n[0]), float(n[1]), float(n[2]), float(n[3])};
                } else {
                    k.unknown.push_back(field);
                }
            }
            w.keyframes.push_back(k);
        } else {
            w.unknown.push_back(item);
        }
    }
    if (!sourceRead)
        w.source = QStringLiteral("preset");
    if (w.keyframes.isEmpty() && !w.preset.isEmpty())
        w.keyframes = WeatherPresets::keyframes(w.preset);
    for (int i = 1; i < w.keyframes.size(); ++i)
        if (w.keyframes[i].time <= w.keyframes[i - 1].time)
            warnings << "Weather keyframes are not in time order";
    return w;
}

EnvNode Weather::toNode() const {
    EnvNode block = EnvNode::blockOf("tsre_weather");
    block.items << EnvNode::blockOf("tsre_weather_version", {EnvNode::integer(version)});
    if (!preset.isEmpty())
        block.items << EnvNode::blockOf("tsre_weather_preset", {EnvNode::value(preset)});
    block.items << EnvNode::blockOf("tsre_weather_seed", {EnvNode::integer(seed)});
    block.items << EnvNode::blockOf("tsre_weather_source",
                                    {EnvNode::value(source, true), EnvNode::value(attribution, true)});
    if (start.isValid())
        block.items << EnvNode::blockOf("tsre_weather_start",
                                        {EnvNode::value(start.toUTC().toString(Qt::ISODate))});
    if (grid.valid)
        block.items << EnvNode::blockOf("tsre_weather_grid",
                                        {EnvNode::integer(grid.tileX), EnvNode::integer(grid.tileZ),
                                         EnvNode::integer(grid.cellSize), EnvNode::integer(grid.columns),
                                         EnvNode::integer(grid.rows)});
    if (!maps.isEmpty())
        block.items << EnvNode::blockOf("tsre_weather_maps", {EnvNode::value(maps, true)});
    for (const WeatherKeyframe &k : keyframes) {
        EnvNode frame = EnvNode::blockOf("tsre_weather_keyframe");
        frame.items << EnvNode::blockOf("tsre_weather_time", {EnvNode::number(k.time)});
        for (int l = 0; l < WeatherLayer::Count; ++l)
            frame.items << EnvNode::blockOf("tsre_weather_wind",
                                            {EnvNode::value(WeatherLayer::name(l)),
                                             EnvNode::number(k.wind[l].direction),
                                             EnvNode::number(k.wind[l].speed)});
        frame.items << EnvNode::blockOf("tsre_weather_precipitation",
                                        {EnvNode::value(precipitationName(k.precipitation)),
                                         EnvNode::number(k.precipitationRate)});
        frame.items << EnvNode::blockOf("tsre_weather_visibility", {EnvNode::number(k.visibility)});
        frame.items << EnvNode::blockOf("tsre_weather_air",
                                        {EnvNode::number(k.air.temperature), EnvNode::number(k.air.dewPoint)});
        if (k.fog.mode == WeatherFogSetting::Set)
            frame.items << EnvNode::blockOf("tsre_weather_fog",
                                            {EnvNode::number(k.fog.amount), EnvNode::number(k.fog.visibility),
                                             EnvNode::number(k.fog.depth)});
        else
            frame.items << EnvNode::blockOf("tsre_weather_fog",
                                            {EnvNode::value(k.fog.mode == WeatherFogSetting::Off ? "off" : "auto")});
        frame.items << EnvNode::blockOf("tsre_weather_variation",
                                        {EnvNode::number(k.variationAmount), EnvNode::number(k.variationScale)});
        for (int l = 0; l < WeatherLayer::Count; ++l) {
            const WeatherLayerValues &v = k.layers[l];
            // A type by its name where it has one.
            QString type = EnvNode::floatText(v.type);
            for (const QString &n : typeNames(l))
                if (typeValue(n) == v.type)
                    type = n;
            frame.items << EnvNode::blockOf("tsre_weather_layer",
                                            {EnvNode::value(WeatherLayer::name(l)), EnvNode::value(type),
                                             EnvNode::number(v.coverage), EnvNode::number(v.base),
                                             EnvNode::number(v.top), EnvNode::number(v.density)});
        }
        frame.items += k.unknown;
        block.items << frame;
    }
    block.items += unknown;
    return block;
}

double Weather::duration() const {
    return keyframes.isEmpty() ? 0.0 : double(keyframes.back().time);
}

Weather::Blend Weather::blend(double hours) const {
    Blend b;
    if (keyframes.size() < 2)
        return b;
    double t = hours;
    if (relative() && duration() > 0.0) {
        t = std::fmod(t, duration());
        if (t < 0.0) t += duration();
    }
    if (t <= keyframes.front().time)
        return b;
    if (t >= keyframes.back().time) {
        b.a = b.b = keyframes.size() - 1;
        return b;
    }
    for (int i = 1; i < keyframes.size(); ++i) {
        if (t > keyframes[i].time)
            continue;
        b.a = i - 1;
        b.b = i;
        const double span = keyframes[i].time - keyframes[i - 1].time;
        b.weight = span > 0.0 ? (t - keyframes[i - 1].time) / span : 1.0;
        return b;
    }
    return b;
}

WeatherWind Weather::wind(double hours, int layer) const {
    if (keyframes.isEmpty())
        return WeatherWind();
    const Blend b = blend(hours);
    const WeatherWind &a = keyframes[b.a].wind[layer], &c = keyframes[b.b].wind[layer];
    return {float(mixAngle(a.direction, c.direction, b.weight)),
            float(a.speed + (c.speed - a.speed) * b.weight)};
}

WeatherAir Weather::air(double hours) const {
    if (keyframes.isEmpty())
        return WeatherAir();
    const Blend b = blend(hours);
    const WeatherAir &a = keyframes[b.a].air, &c = keyframes[b.b].air;
    const float w = float(b.weight);
    return {a.temperature + (c.temperature - a.temperature) * w, a.dewPoint + (c.dewPoint - a.dewPoint) * w};
}

void Weather::velocity(const WeatherWind &wind, double &east, double &north) {
    const double towards = (wind.direction + 180.0) * M_PI / 180.0;
    east = std::sin(towards) * wind.speed;
    north = std::cos(towards) * wind.speed;
}

void Weather::drift(double hours, int layer, double &east, double &north) const {
    east = north = 0.0;
    if (keyframes.isEmpty())
        return;
    // The wind changes linearly between keyframes; integrated in steps of at
    // most a quarter of an hour.
    auto integrate = [this, layer](double from, double to, double &e, double &n) {
        const int steps = std::max(1, int(std::ceil(std::abs(to - from) * 4.0)));
        const double h = (to - from) / steps;
        for (int i = 0; i < steps; ++i) {
            double e0, n0, e1, n1;
            velocity(wind(from + h * i, layer), e0, n0);
            velocity(wind(from + h * (i + 1), layer), e1, n1);
            e += (e0 + e1) * 0.5 * h * SecondsPerHour;
            n += (n0 + n1) * 0.5 * h * SecondsPerHour;
        }
    };
    const double length = duration();
    if (relative() && length > 0.0) {
        // Whole loops, then the rest.
        const double loops = std::floor(hours / length);
        double e = 0.0, n = 0.0;
        if (loops != 0.0) {
            integrate(0.0, length, e, n);
            east = e * loops;
            north = n * loops;
        }
        integrate(0.0, hours - loops * length, east, north);
        return;
    }
    integrate(0.0, hours, east, north);
}

double Weather::cell(double east, double north, double &column, double &row) const {
    if (!grid.valid)
        return 0.0;
    const double c = (east - grid.west()) / grid.cellSize - 0.5;
    const double r = (grid.north() - north) / grid.cellSize - 0.5;
    column = std::clamp(c, 0.0, grid.columns - 1.0);
    row = std::clamp(r, 0.0, grid.rows - 1.0);
    // Past the grid the edge would stretch out in stripes.
    const double outside = std::hypot(std::max({0.0, -0.5 - c, c - (grid.columns - 0.5)}),
                                      std::max({0.0, -0.5 - r, r - (grid.rows - 0.5)}));
    const double weight = std::clamp(1.0 - outside / 3.0, 0.0, 1.0);
    return weight * weight * (3.0 - 2.0 * weight);
}

std::array<double, 4> Weather::sampleMap(int index, int keyframe, double column, double row) const {
    const QImage &image = mapImages[index];
    const int c0 = int(std::floor(column)), r0 = int(std::floor(row));
    const int c1 = std::min(c0 + 1, grid.columns - 1), r1 = std::min(r0 + 1, grid.rows - 1);
    const double fc = column - c0, fr = row - r0;
    auto pixel = [&](int c, int r) {
        const QRgba64 p = reinterpret_cast<const QRgba64 *>(image.constScanLine(keyframe * grid.rows + r))[c];
        return std::array<double, 4>{p.red() / 65535.0, p.green() / 65535.0, p.blue() / 65535.0,
                                     p.alpha() / 65535.0};
    };
    const auto a = pixel(c0, r0), b = pixel(c1, r0), c = pixel(c0, r1), d = pixel(c1, r1);
    std::array<double, 4> out;
    for (int i = 0; i < 4; ++i) {
        const double top = a[i] + (b[i] - a[i]) * fc;
        const double bottom = c[i] + (d[i] - c[i]) * fc;
        out[i] = top + (bottom - top) * fr;
    }
    return out;
}

WeatherLayerValues Weather::keyframeLayer(int keyframe, double east, double north, int layer) const {
    WeatherLayerValues v = keyframes[keyframe].layers[layer];
    double column, row;
    const double weight = hasMaps() ? cell(east, north, column, row) : 0.0;
    if (weight > 0.0) {
        const auto m = sampleMap(layer, keyframe, column, row);
        const float w = float(weight);
        auto mix = [w](float keyframe, double map) { return keyframe + (float(map) - keyframe) * w; };
        v.coverage = mix(v.coverage, m[0]);
        v.type = mix(v.type, m[1]);
        v.base = mix(v.base, m[2] * 65535.0);
        v.top = mix(v.top, m[3] * 65535.0);
    }
    return v;
}

WeatherSurface Weather::keyframeSurface(int keyframe, double east, double north) const {
    const WeatherKeyframe &k = keyframes[keyframe];
    WeatherSurface s;
    s.precipitation = k.precipitation;
    s.precipitationRate = k.precipitation == WeatherPrecipitation::None ? 0.0f : k.precipitationRate;
    s.snowShare = k.precipitation == WeatherPrecipitation::Snow ? 1.0f : 0.0f;
    s.visibility = k.visibility;
    double column, row;
    const double weight = hasMaps() ? cell(east, north, column, row) : 0.0;
    if (weight > 0.0) {
        const auto m = sampleMap(SurfaceMap, keyframe, column, row);
        const float w = float(weight);
        auto mix = [w](float keyframe, double map) { return keyframe + (float(map) - keyframe) * w; };
        s.precipitationRate = mix(s.precipitationRate, m[0] * 65535.0 * 0.01);
        s.snowShare = mix(s.snowShare, m[1]);
        s.visibility = mix(s.visibility, m[2] * 65535.0 * 2.0);
        s.precipitation = s.precipitationRate <= 0.0f ? WeatherPrecipitation::None
                        : s.snowShare >= 0.5f ? WeatherPrecipitation::Snow : WeatherPrecipitation::Rain;
    }
    return s;
}

double Weather::variation(double east, double north, double scale) const {
    if (scale <= 0.0)
        return 0.0;
    return std::clamp(WeatherNoise::fractal(east / scale, north / scale, 3, seed * 7u + 13u), -1.0, 1.0);
}

Weather::Frame Weather::frame(double hours) const {
    Frame f;
    f.hours = hours;
    if (keyframes.isEmpty())
        return f;
    f.empty = false;
    f.blend = blend(hours);
    // Where the keyframes' fields have moved to: each from its own time.
    const double local = relative() && duration() > 0.0
            ? hours - std::floor(hours / duration()) * duration() : hours;
    const double hoursA = hours - (local - keyframes[f.blend.a].time);
    const double hoursB = hours + (keyframes[f.blend.b].time - local);
    for (int l = 0; l < WeatherLayer::Count; ++l) {
        double nowE, nowN, aE, aN, bE, bN;
        drift(hours, l, nowE, nowN);
        drift(hoursA, l, aE, aN);
        drift(hoursB, l, bE, bN);
        f.drift[l] = {nowE, nowN};
        f.shiftA[l] = {nowE - aE, nowN - aN};
        f.shiftB[l] = {bE - nowE, bN - nowN};
    }
    const WeatherKeyframe &a = keyframes[f.blend.a], &b = keyframes[f.blend.b];
    const float w = float(f.blend.weight);
    f.variationAmount = a.variationAmount + (b.variationAmount - a.variationAmount) * w;
    f.variationScale = a.variationScale + (b.variationScale - a.variationScale) * w;
    return f;
}

WeatherLayerValues Weather::layer(double hours, double east, double north, int layer) const {
    return this->layer(frame(hours), east, north, layer);
}

WeatherSurface Weather::surface(double hours, double east, double north) const {
    return surface(frame(hours), east, north);
}

WeatherLayerValues Weather::layer(const Frame &f, double east, double north, int layer) const {
    if (f.empty || layer < 0 || layer >= WeatherLayer::Count)
        return WeatherLayerValues{0.67f, 0.0f, 1000.0f, 2000.0f, 1.0f};
    const auto &sa = f.shiftA[layer], &sb = f.shiftB[layer];
    WeatherLayerValues v = keyframeLayer(f.blend.a, east - sa[0], north - sa[1], layer);
    if (f.blend.b != f.blend.a) {
        const WeatherLayerValues w = keyframeLayer(f.blend.b, east + sb[0], north + sb[1], layer);
        const float t = float(f.blend.weight);
        auto mix = [t](float x, float y) { return x + (y - x) * t; };
        v = {mix(v.type, w.type), mix(v.coverage, w.coverage), mix(v.base, w.base), mix(v.top, w.top),
             mix(v.density, w.density)};
    }
    // The variation drifts with the layer from the start, like the clouds.
    if (f.variationAmount > 0.0f && v.coverage > 0.0f && v.coverage < 1.0f)
        v.coverage = float(std::clamp(v.coverage + f.variationAmount
                                      * variation(east - f.drift[layer][0], north - f.drift[layer][1],
                                                  f.variationScale),
                                      0.0, 1.0));
    return v;
}

WeatherSurface Weather::surface(const Frame &f, double east, double north) const {
    if (f.empty)
        return WeatherSurface();
    // Rain falls from the low clouds: it moves with their wind.
    const auto &sa = f.shiftA[WeatherLayer::Low], &sb = f.shiftB[WeatherLayer::Low];
    WeatherSurface s = keyframeSurface(f.blend.a, east - sa[0], north - sa[1]);
    if (f.blend.b == f.blend.a)
        return s;
    const WeatherSurface t = keyframeSurface(f.blend.b, east + sb[0], north + sb[1]);
    const float w = float(f.blend.weight);
    WeatherSurface out;
    out.precipitationRate = s.precipitationRate + (t.precipitationRate - s.precipitationRate) * w;
    out.snowShare = s.snowShare + (t.snowShare - s.snowShare) * w;
    out.visibility = s.visibility + (t.visibility - s.visibility) * w;
    out.precipitation = out.precipitationRate <= 0.0f ? WeatherPrecipitation::None
                      : out.snowShare >= 0.5f ? WeatherPrecipitation::Snow : WeatherPrecipitation::Rain;
    return out;
}

double Weather::editorHours(const QDate &date, double timeOfDay) const {
    if (relative())
        return timeOfDay;
    const QDateTime at(date, QTime(0, 0), Qt::UTC);
    return start.secsTo(at) / 3600.0 + timeOfDay;
}

QString Weather::mapFileName(int index) {
    return index == SurfaceMap ? QStringLiteral("surface.png") : WeatherLayer::name(index) + ".png";
}

void Weather::allocateMaps() {
    if (!grid.valid || keyframes.isEmpty())
        return;
    for (int index = 0; index <= SurfaceMap; ++index) {
        QImage image(grid.columns, grid.rows * keyframes.size(), QImage::Format_RGBA64);
        for (int k = 0; k < keyframes.size(); ++k) {
            const WeatherKeyframe &f = keyframes[k];
            QRgba64 fill;
            if (index == SurfaceMap) {
                const double rate = f.precipitation == WeatherPrecipitation::None ? 0.0 : f.precipitationRate;
                fill = QRgba64::fromRgba64(quint16(std::clamp(rate / 0.01, 0.0, 65535.0)),
                                           f.precipitation == WeatherPrecipitation::Snow ? 65535 : 0,
                                           quint16(std::clamp(f.visibility / 2.0, 0.0, 65535.0)), 65535);
            } else {
                const WeatherLayerValues &v = f.layers[index];
                fill = QRgba64::fromRgba64(quint16(std::lround(std::clamp(v.coverage, 0.0f, 1.0f) * 65535.0)),
                                           quint16(std::lround(std::clamp(v.type, 0.0f, 1.0f) * 65535.0)),
                                           quint16(std::clamp(v.base, 0.0f, 65535.0f)),
                                           quint16(std::clamp(v.top, 0.0f, 65535.0f)));
            }
            for (int r = 0; r < grid.rows; ++r) {
                auto *line = reinterpret_cast<QRgba64 *>(image.scanLine(k * grid.rows + r));
                std::fill(line, line + grid.columns, fill);
            }
        }
        mapImages[index] = image;
    }
}

bool Weather::loadMaps(const QString &envDirectory, QString &error) {
    for (QImage &image : mapImages)
        image = QImage();
    if (maps.isEmpty())
        return true;
    if (!grid.valid || keyframes.isEmpty()) {
        error = "Weather maps need a valid grid and keyframes";
        return false;
    }
    const QDir directory(QDir(envDirectory).filePath(maps));
    std::array<QImage, WeatherLayer::Count + 1> loaded;
    for (int index = 0; index <= SurfaceMap; ++index) {
        const QString path = directory.filePath(mapFileName(index));
        QImage image(path);
        if (image.isNull()) {
            error = "Cannot read weather map " + path;
            return false;
        }
        if (image.width() != grid.columns || image.height() != grid.rows * keyframes.size()) {
            error = QString("Weather map %1 is %2 x %3, expected %4 x %5").arg(path).arg(image.width())
                    .arg(image.height()).arg(grid.columns).arg(grid.rows * keyframes.size());
            return false;
        }
        loaded[index] = image.convertToFormat(QImage::Format_RGBA64);
    }
    mapImages = loaded;
    return true;
}

bool Weather::saveMaps(const QString &envDirectory, QString &error) const {
    if (maps.isEmpty() || !hasMaps())
        return true;
    const QString path = QDir(envDirectory).filePath(maps);
    if (!QDir().mkpath(path)) {
        error = "Cannot create " + path;
        return false;
    }
    for (int index = 0; index <= SurfaceMap; ++index) {
        const QString file = QDir(path).filePath(mapFileName(index));
        if (!mapImages[index].save(file, "PNG")) {
            error = "Cannot write " + file;
            return false;
        }
    }
    return true;
}
