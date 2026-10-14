/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/environment/WeatherFog.h>

#include <algorithm>
#include <cmath>

namespace {

// The sun's centre at sunrise and sunset (degrees), with refraction.
constexpr double Horizon = -0.833;
// Sunrise and sunset are searched in steps of a quarter hour, a day and a
// half back.
constexpr double Step = 0.25;
constexpr double Search = 36.0;
// The longest night counted (a polar night counts as this).
constexpr double LongestNight = 16.0;

double smooth(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// When the sun last crossed the horizon before a time, rising or setting;
// NaN when it did not within the search.
double lastCrossing(const WeatherFog::SunElevation &sun, double from, bool rising) {
    double later = sun(from);
    for (double t = from - Step; t >= from - Search; t -= Step) {
        const double earlier = sun(t);
        const bool crossed = rising ? earlier < Horizon && later >= Horizon
                                    : earlier >= Horizon && later < Horizon;
        if (crossed)
            return t + Step * (Horizon - earlier) / (later - earlier);
        later = earlier;
    }
    return std::nan("");
}

// Mist in rain: deep and thin.
WeatherFog::State rainMist(const Weather &weather, double hours, double east, double north) {
    WeatherFog::State s;
    const WeatherSurface surface = weather.surface(hours, east, north);
    if (surface.precipitation != WeatherPrecipitation::Rain)
        return s;
    s.amount = float(0.5 * smooth(0.2, 3.0, surface.precipitationRate));
    if (s.amount < 0.01f)
        return WeatherFog::State();
    s.visibility = 2000.0f;
    s.depth = 150.0f;
    s.patchiness = 0.5f;
    return s;
}

WeatherFog::State mix(const WeatherFog::State &a, const WeatherFog::State &b, float w) {
    // A side without fog keeps the other's shape and takes its amount away.
    if (a.amount <= 0.0f) {
        WeatherFog::State s = b;
        s.amount *= w;
        return s;
    }
    if (b.amount <= 0.0f) {
        WeatherFog::State s = a;
        s.amount *= 1.0f - w;
        return s;
    }
    auto m = [w](float x, float y) { return x + (y - x) * w; };
    WeatherFog::State s;
    s.amount = m(a.amount, b.amount);
    s.visibility = float(std::exp(m(std::log(a.visibility), std::log(b.visibility))));
    s.depth = m(a.depth, b.depth);
    s.base = m(a.base, b.base);
    s.patchiness = m(a.patchiness, b.patchiness);
    return s;
}

} // namespace

WeatherFog::State WeatherFog::radiation(const Weather &weather, double hours, double east, double north,
                                        const SunElevation &sun) {
    State s;
    if (weather.keyframes.isEmpty() || !sun)
        return s;
    // The night that made the fog: the one going on, or the one that ended at
    // the last sunrise.
    const bool night = sun(hours) < Horizon;
    double sunrise = std::nan("");
    double sunset;
    if (night) {
        sunset = lastCrossing(sun, hours, false);
    } else {
        sunrise = lastCrossing(sun, hours, true);
        if (std::isnan(sunrise))
            return s;  // a polar day: no night to cool the air
        sunset = lastCrossing(sun, sunrise, false);
    }
    const double nightEnd = night ? hours : sunrise;
    const double nightStart = std::isnan(sunset) ? nightEnd - LongestNight
                                                 : std::max(sunset, nightEnd - LongestNight);
    const double nightHours = std::max(nightEnd - nightStart, 0.0);

    // The night's sky and wind at the place, hour by hour.
    const int samples = std::max(1, int(std::ceil(nightHours)));
    double cover = 0.0, wind = 0.0;
    for (int i = 0; i < samples; ++i) {
        const double t = nightStart + (i + 0.5) * nightHours / samples;
        const Weather::Frame frame = weather.frame(t);
        cover += std::max(weather.layer(frame, east, north, WeatherLayer::Low).coverage,
                          0.7f * weather.layer(frame, east, north, WeatherLayer::Mid).coverage);
        wind += weather.wind(t, WeatherLayer::Low).speed;
    }
    cover /= samples;
    wind /= samples;
    // Rain in the half day before soaks the ground: the air is damper.
    double rain = 0.0;
    for (double t = nightStart - 12.0; t <= nightStart; t += 1.0)
        rain = std::max(rain, double(weather.surface(t, east, north).precipitationRate));

    // A long clear calm night cools the air near the ground by up to about
    // 8 °C; clouds send the heat back, wind mixes warmer air down. The low
    // wind is the weather's (about 100 m up); at the ground it is weaker.
    const WeatherAir evening = weather.air(nightStart);
    const WeatherAir late = weather.air(nightEnd);
    const double spread = evening.temperature - evening.dewPoint - 1.5 * smooth(0.1, 1.0, rain);
    const double clear = 1.0 - 0.8 * smooth(0.2, 0.9, cover);
    const double mixing = 1.0 - 0.5 * smooth(3.0, 8.0, wind);
    const double cooling = 8.0 * clear * mixing * (1.0 - std::exp(-nightHours / 5.0));
    // What is left of the gap; the weather's own where it is smaller (hourly
    // data already holds the night's cooling).
    const double remaining = std::min(spread - cooling, double(late.temperature - late.dewPoint));
    const double saturated = 1.0 - smooth(0.0, 1.5, remaining);
    const double windMix = 1.0 - smooth(5.0, 8.0, wind);
    s.amount = float(saturated * windMix);
    if (s.amount < 0.01f)
        return State();
    // Cooled below the dew point it deepens and thickens; very light wind
    // keeps it shallow.
    const double excess = std::max(0.0, -remaining);
    s.depth = float(std::clamp(20.0 + 40.0 * excess, 5.0, 250.0) * (0.3 + 0.7 * smooth(0.5, 2.5, wind)));
    const double wetness = std::clamp((1.5 - remaining) / 4.5, 0.0, 1.0);
    s.visibility = float(3000.0 * std::pow(150.0 / 3000.0, wetness));
    s.patchiness = float(0.25 + 0.75 * (1.0 - saturated));
    if (night)
        return s;

    // The sun's warmth since sunrise (hours times the sine of its height):
    // deeper fog takes more; it lifts off the ground as it thins.
    double warmth = 0.0;
    for (double t = sunrise; t < hours; t += Step) {
        const double dt = std::min(Step, hours - t);
        warmth += std::max(0.0, std::sin(sun(t + 0.5 * dt) * M_PI / 180.0)) * dt;
    }
    const double burn = smooth(0.1, 0.4 + s.depth / 100.0, warmth);
    s.amount *= float(1.0 - burn);
    if (s.amount < 0.01f)
        return State();
    s.base = float(s.depth * 0.5 * smooth(0.3, 1.0, burn));
    s.visibility *= float(1.0 + 4.0 * burn);
    s.patchiness = float(std::max(double(s.patchiness), burn));
    return s;
}

WeatherFog::State WeatherFog::at(const Weather &weather, double hours, double east, double north,
                                 const SunElevation &sun) {
    if (weather.keyframes.isEmpty())
        return State();
    bool automaticDone = false;
    State automatic;
    auto side = [&](int keyframe) {
        const WeatherFogSetting &f = weather.keyframes[keyframe].fog;
        if (f.mode == WeatherFogSetting::Off)
            return State();
        if (f.mode == WeatherFogSetting::Set)
            return State{f.amount, f.visibility, f.depth, 0.0f, 0.3f};
        if (!automaticDone) {
            const State fog = radiation(weather, hours, east, north, sun);
            const State mist = rainMist(weather, hours, east, north);
            automatic = mist.amount > fog.amount ? mist : fog;
            automaticDone = true;
        }
        return automatic;
    };
    const Weather::Blend b = weather.blend(hours);
    const State a = side(b.a);
    if (b.b == b.a || b.weight <= 0.0)
        return a;
    return mix(a, side(b.b), float(b.weight));
}
