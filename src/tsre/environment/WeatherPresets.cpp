/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/environment/WeatherPresets.h>

namespace {

using L = WeatherLayerValues;

struct Layers {
    L low, mid, high;
};

WeatherKeyframe frame(float time, const Layers &layers, float windDirection, float lowWind,
                      WeatherPrecipitation precipitation = WeatherPrecipitation::None,
                      float rate = 0.0f, float visibility = 40000.0f, float variation = 0.2f) {
    WeatherKeyframe k;
    k.time = time;
    // Winds strengthen and veer with height.
    k.wind[WeatherLayer::Low] = {windDirection, lowWind};
    k.wind[WeatherLayer::Mid] = {windDirection + 10.0f, lowWind * 2.0f};
    k.wind[WeatherLayer::High] = {windDirection + 20.0f, lowWind * 4.0f};
    k.precipitation = precipitation;
    k.precipitationRate = rate;
    k.visibility = visibility;
    k.variationAmount = variation;
    k.variationScale = 30000.0f;
    k.layers = {layers.low, layers.mid, layers.high};
    return k;
}

// type, coverage, base, top, density
const L None{0.67f, 0.0f, 1000.0f, 2000.0f, 1.0f};
const L Cirrus{0.5f, 0.15f, 9000.0f, 9500.0f, 0.3f};

QVector<WeatherKeyframe> make(const QString &name) {
    const auto P = WeatherPrecipitation::None;
    if (name == "clear")
        return {frame(0, {None, None, L{0.5f, 0.05f, 9000, 9400, 0.2f}}, 270, 4)};
    if (name == "fair_cumulus")
        return {frame(0, {L{0.67f, 0.3f, 1000, 2000, 1.0f}, None, Cirrus}, 270, 5)};
    if (name == "scattered")
        return {frame(0, {L{0.67f, 0.45f, 900, 2400, 1.0f}, L{0.6f, 0.15f, 3500, 4200, 0.6f},
                          L{0.5f, 0.25f, 9000, 9500, 0.3f}}, 260, 6, P, 0, 35000)};
    if (name == "broken")
        return {frame(0, {L{0.33f, 0.7f, 800, 1600, 1.0f}, L{0.6f, 0.3f, 3200, 4000, 0.7f},
                          L{0.5f, 0.3f, 9000, 9500, 0.3f}}, 250, 7, P, 0, 25000)};
    if (name == "overcast")
        return {frame(0, {L{0.0f, 0.95f, 500, 1100, 0.8f}, L{0.0f, 0.6f, 3000, 4500, 0.7f},
                          L{0.0f, 0.5f, 8500, 9500, 0.3f}}, 240, 7, P, 0, 15000, 0.05f)};
    if (name == "rain")
        return {frame(0, {L{0.0f, 1.0f, 400, 1500, 1.0f}, L{0.0f, 0.9f, 2500, 5000, 0.9f},
                          L{0.0f, 0.6f, 8000, 9500, 0.3f}}, 220, 8,
                      WeatherPrecipitation::Rain, 2.0f, 6000, 0.05f)};
    if (name == "snow")
        return {frame(0, {L{0.0f, 1.0f, 300, 1300, 1.0f}, L{0.0f, 0.9f, 2000, 4500, 0.9f},
                          L{0.0f, 0.5f, 7500, 9000, 0.3f}}, 20, 6,
                      WeatherPrecipitation::Snow, 1.0f, 3000, 0.05f)};
    if (name == "thunderstorm")
        return {frame(0, {L{1.0f, 0.3f, 800, 11000, 1.2f}, L{0.6f, 0.3f, 3500, 4500, 0.7f},
                          L{0.5f, 0.4f, 9500, 10500, 0.4f}}, 240, 10,
                      WeatherPrecipitation::Rain, 8.0f, 8000, 0.35f)};
    if (name == "front") {
        // A warm front passing over about 15 hours: cirrus thickening, the
        // cloud lowering into rain, then clearing behind it.
        return {
            frame(0, {L{0.67f, 0.25f, 1100, 2000, 1.0f}, None, L{0.5f, 0.2f, 9000, 9500, 0.3f}}, 230, 5),
            frame(3, {L{0.67f, 0.2f, 1200, 2000, 1.0f}, L{0.0f, 0.3f, 4000, 5000, 0.6f},
                      L{0.0f, 0.7f, 8000, 9500, 0.3f}}, 210, 6, P, 0, 30000),
            frame(6, {L{0.33f, 0.6f, 900, 1800, 1.0f}, L{0.0f, 0.8f, 3000, 5000, 0.8f},
                      L{0.0f, 0.8f, 8000, 9500, 0.3f}}, 200, 8, P, 0, 15000, 0.1f),
            frame(8, {L{0.0f, 1.0f, 400, 1500, 1.0f}, L{0.0f, 0.95f, 2500, 5000, 0.9f},
                      L{0.0f, 0.6f, 8000, 9500, 0.3f}}, 200, 10, WeatherPrecipitation::Rain, 3.0f, 5000, 0.05f),
            frame(11, {L{0.33f, 0.7f, 700, 1600, 1.0f}, L{0.6f, 0.4f, 3000, 4000, 0.7f},
                       L{0.5f, 0.3f, 9000, 9500, 0.3f}}, 260, 9, WeatherPrecipitation::Rain, 0.3f, 15000),
            frame(15, {L{0.67f, 0.4f, 1000, 2200, 1.0f}, L{0.6f, 0.1f, 3500, 4200, 0.6f},
                       L{0.5f, 0.15f, 9000, 9500, 0.3f}}, 280, 7, P, 0, 45000),
        };
    }
    return {};
}

} // namespace

QStringList WeatherPresets::names() {
    return {"clear", "fair_cumulus", "scattered", "broken", "overcast", "rain", "snow",
            "thunderstorm", "front"};
}

bool WeatherPresets::exists(const QString &name) {
    return names().contains(name);
}

QVector<WeatherKeyframe> WeatherPresets::keyframes(const QString &name) {
    return make(name);
}

WeatherKeyframe WeatherPresets::at(const QString &name, double hours) {
    const QVector<WeatherKeyframe> frames = make(name);
    if (frames.isEmpty())
        return WeatherKeyframe();
    if (hours <= frames.front().time)
        return frames.front();
    for (int i = 1; i < frames.size(); ++i) {
        if (hours > frames[i].time)
            continue;
        const WeatherKeyframe &a = frames[i - 1], &b = frames[i];
        const float w = float((hours - a.time) / (b.time - a.time));
        // The nearer keyframe's precipitation type; numbers blended.
        WeatherKeyframe k = w < 0.5f ? a : b;
        auto mix = [w](float x, float y) { return x + (y - x) * w; };
        k.time = float(hours);
        k.precipitationRate = mix(a.precipitationRate, b.precipitationRate);
        k.visibility = mix(a.visibility, b.visibility);
        k.variationAmount = mix(a.variationAmount, b.variationAmount);
        for (int l = 0; l < WeatherLayer::Count; ++l) {
            k.wind[l].speed = mix(a.wind[l].speed, b.wind[l].speed);
            k.wind[l].direction = mix(a.wind[l].direction, b.wind[l].direction);
            k.layers[l].type = mix(a.layers[l].type, b.layers[l].type);
            k.layers[l].coverage = mix(a.layers[l].coverage, b.layers[l].coverage);
            k.layers[l].base = mix(a.layers[l].base, b.layers[l].base);
            k.layers[l].top = mix(a.layers[l].top, b.layers[l].top);
            k.layers[l].density = mix(a.layers[l].density, b.layers[l].density);
        }
        return k;
    }
    return frames.back();
}

Weather WeatherPresets::weather(const QString &name, quint32 seed) {
    Weather w;
    w.preset = name;
    w.seed = seed;
    w.keyframes = make(name);
    return w;
}
