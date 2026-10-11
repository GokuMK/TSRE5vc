/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/environment/WeatherGenerator.h>
#include <tsre/environment/WeatherDetail.h>

#include <QRgba64>
#include <algorithm>
#include <cmath>

namespace WeatherGenerator {

namespace {

using L = WeatherLayerValues;

// type, coverage, base, top, density
const L None{0.67f, 0.0f, 1000.0f, 2000.0f, 1.0f};

// A station of the cross-section: the weather at a distance ahead of the
// line (km).
struct Station {
    double ahead;
    L low, mid, high;
    float rain;        // mm/h
    float visibility;  // m
    float windTurn;    // degrees the low wind comes from, against the front's motion
    float windSpeed;   // m/s
};

// Warm front: the warm air climbs over the cold ahead of the line on a
// shallow slope, so its cloud reaches far ahead and lowers towards it:
// cirrus, cirrostratus, altostratus, then nimbostratus and steady rain; a
// warm sector of stratocumulus behind.
const std::vector<Station> &warmFront() {
    static const std::vector<Station> stations = {
        {-250, L{0.33f, 0.6f, 700, 1600, 1.0f}, L{0.6f, 0.2f, 3200, 4000, 0.6f}, L{0.5f, 0.2f, 9000, 9500, 0.3f},
         0.0f, 15000, 20, 9},
        {-40, L{0.0f, 0.85f, 500, 1500, 1.0f}, L{0.0f, 0.4f, 2800, 4500, 0.7f}, L{0.5f, 0.3f, 9000, 9500, 0.3f},
         0.4f, 9000, 10, 10},
        {20, L{0.0f, 1.0f, 400, 3000, 1.0f}, L{0.0f, 0.95f, 2500, 5500, 0.9f}, L{0.0f, 0.7f, 8000, 9500, 0.3f},
         3.0f, 5000, -10, 11},
        {110, L{0.0f, 0.9f, 600, 3000, 1.0f}, L{0.0f, 0.95f, 2800, 5500, 0.9f}, L{0.0f, 0.8f, 8000, 9500, 0.3f},
         1.5f, 8000, -20, 10},
        {220, L{0.33f, 0.4f, 900, 1800, 1.0f}, L{0.0f, 0.8f, 3500, 5500, 0.8f}, L{0.0f, 0.85f, 8000, 9500, 0.3f},
         0.0f, 20000, -30, 9},
        {380, L{0.67f, 0.2f, 1000, 2000, 1.0f}, L{0.0f, 0.3f, 4500, 5500, 0.6f}, L{0.0f, 0.85f, 8000, 9500, 0.3f},
         0.0f, 30000, -40, 7},
        {650, L{0.67f, 0.25f, 1100, 2000, 1.0f}, None, L{0.5f, 0.5f, 8500, 9500, 0.3f},
         0.0f, 40000, -45, 6},
        {950, L{0.67f, 0.25f, 1100, 2000, 1.0f}, None, L{0.5f, 0.1f, 9000, 9500, 0.3f},
         0.0f, 45000, -45, 5},
    };
    return stations;
}

// Cold front: the cold air wedges under the warm, lifting it steeply: a
// narrow band of cumulonimbus and heavy showers at the line, the warm
// sector's stratocumulus ahead, clearing with scattered cumulus behind.
const std::vector<Station> &coldFront() {
    static const std::vector<Station> stations = {
        {-300, L{0.67f, 0.3f, 1100, 2300, 1.0f}, None, L{0.5f, 0.05f, 9000, 9500, 0.3f},
         0.0f, 45000, 40, 8},
        {-70, L{0.67f, 0.35f, 1000, 2600, 1.0f}, L{0.6f, 0.1f, 3500, 4200, 0.6f}, L{0.5f, 0.1f, 9000, 9500, 0.3f},
         0.0f, 40000, 40, 10},
        {-35, L{0.7f, 0.45f, 900, 3500, 1.0f}, L{0.6f, 0.2f, 3500, 4500, 0.6f}, L{0.5f, 0.3f, 9000, 10000, 0.3f},
         0.2f, 30000, 38, 11},
        {-15, L{0.8f, 0.65f, 800, 5000, 1.1f}, L{0.6f, 0.3f, 3500, 4500, 0.7f}, L{0.5f, 0.4f, 9000, 10000, 0.3f},
         2.0f, 12000, 35, 12},
        {0, L{1.0f, 0.9f, 600, 9500, 1.2f}, L{0.0f, 0.6f, 3000, 5000, 0.8f}, L{0.0f, 0.7f, 9000, 10500, 0.4f},
         9.0f, 4000, 20, 13},
        {15, L{1.0f, 0.85f, 700, 9000, 1.2f}, L{0.0f, 0.5f, 3000, 5000, 0.8f}, L{0.0f, 0.6f, 9000, 10500, 0.4f},
         5.0f, 6000, 0, 12},
        {40, L{0.5f, 0.75f, 700, 2500, 1.0f}, L{0.6f, 0.5f, 3000, 4500, 0.7f}, L{0.5f, 0.6f, 9000, 10000, 0.3f},
         0.0f, 10000, -10, 11},
        {80, L{0.33f, 0.7f, 700, 1800, 1.0f}, L{0.6f, 0.5f, 3000, 4500, 0.7f}, L{0.5f, 0.5f, 9000, 10000, 0.3f},
         0.0f, 12000, -20, 10},
        {300, L{0.33f, 0.6f, 700, 1600, 1.0f}, L{0.6f, 0.3f, 3000, 4000, 0.6f}, L{0.5f, 0.3f, 9000, 9500, 0.3f},
         0.0f, 15000, -20, 9},
    };
    return stations;
}

const std::vector<Station> &stations(Scenario scenario) {
    return scenario == WarmFront ? warmFront() : coldFront();
}

L mixLayer(const L &a, const L &b, float w) {
    auto m = [w](float x, float y) { return x + (y - x) * w; };
    // A layer that is not there takes the other's place and type, so heights
    // do not sweep through the sky as it appears.
    const L &shape = a.coverage <= 0.0f ? b : (b.coverage <= 0.0f ? a : (w < 0.5f ? a : b));
    L out = shape;
    out.coverage = m(a.coverage, b.coverage);
    if (a.coverage > 0.0f && b.coverage > 0.0f) {
        out.type = m(a.type, b.type);
        out.base = m(a.base, b.base);
        out.top = m(a.top, b.top);
        out.density = m(a.density, b.density);
    }
    return out;
}

// The cross-section at a distance, stations blended linearly.
Station stationAt(Scenario scenario, double ahead) {
    const std::vector<Station> &s = stations(scenario);
    if (ahead <= s.front().ahead)
        return s.front();
    if (ahead >= s.back().ahead)
        return s.back();
    size_t i = 1;
    while (i < s.size() && s[i].ahead < ahead)
        ++i;
    const Station &a = s[i - 1], &b = s[i];
    const float w = float((ahead - a.ahead) / (b.ahead - a.ahead));
    auto m = [w](float x, float y) { return x + (y - x) * w; };
    Station out = a;
    out.ahead = ahead;
    out.low = mixLayer(a.low, b.low, w);
    out.mid = mixLayer(a.mid, b.mid, w);
    out.high = mixLayer(a.high, b.high, w);
    out.rain = m(a.rain, b.rain);
    out.visibility = m(a.visibility, b.visibility);
    out.windTurn = m(a.windTurn, b.windTurn);
    out.windSpeed = m(a.windSpeed, b.windSpeed);
    return out;
}

double smooth(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Heaps grow through the day: fewer in the morning and at night.
double convectiveDay(double hour) {
    const double h = std::fmod(std::fmod(hour, 24.0) + 24.0, 24.0);
    return 0.45 + 0.65 * smooth(8.0, 13.0, h) * (1.0 - smooth(17.0, 21.0, h));
}

struct Cell {
    L layers[WeatherLayer::Count];
    double rain = 0.0;
    double visibility = 40000.0;
};

} // namespace

QStringList scenarioNames() {
    return {"cold_front", "warm_front"};
}

WeatherKeyframe section(Scenario scenario, double ahead) {
    const Station s = stationAt(scenario, ahead);
    WeatherKeyframe k;
    k.layers = {s.low, s.mid, s.high};
    k.precipitationRate = s.rain;
    k.precipitation = s.rain < 0.05f ? WeatherPrecipitation::None : WeatherPrecipitation::Rain;
    k.visibility = s.visibility;
    return k;
}

Weather generate(const Settings &settings) {
    Weather weather;
    weather.seed = settings.seed;
    const QString names[ScenarioCount] = {"cold front", "warm front"};
    weather.source = QString("Generated: %1 moving towards %2 degrees at %3 km/h, over the area's centre at %4:00")
            .arg(names[settings.scenario]).arg(settings.heading, 0, 'f', 0).arg(settings.speed, 0, 'f', 0)
            .arg(int(std::lround(settings.arrival)), 2, 10, QChar('0'));
    // A day of its own, looping: relative to the editor's time.
    weather.start = QDateTime();

    WeatherGrid &g = weather.grid;
    g.cellSize = settings.cellSize;
    g.tileX = int(std::floor((settings.west + 1024.0) / 2048.0));
    g.tileZ = int(std::ceil((settings.north - 1024.0) / 2048.0));
    g.columns = std::max(1, int(std::ceil((settings.east - g.west()) / g.cellSize)));
    g.rows = std::max(1, int(std::ceil((g.north() - settings.south) / g.cellSize)));
    g.valid = true;

    const double centreE = (settings.west + settings.east) * 0.5;
    const double centreN = (settings.north + settings.south) * 0.5;
    const double heading = settings.heading * M_PI / 180.0;
    // The front's motion (x east, y north) and its line's direction.
    const double motionE = std::sin(heading), motionN = std::cos(heading);
    const double alongE = motionN, alongN = -motionE;
    const quint32 seed = settings.seed * 7919u + 17u;

    constexpr int Keyframes = 25;
    std::vector<std::vector<Cell>> cells(Keyframes, std::vector<Cell>(size_t(g.columns) * g.rows));
    for (int k = 0; k < Keyframes; ++k) {
        const double hour = k;
        // The line's distance past the centre (km); points ahead of it are
        // positive.
        const double travelled = settings.speed * (hour - settings.arrival);
        for (int row = 0; row < g.rows; ++row)
            for (int column = 0; column < g.columns; ++column) {
                const double e = g.west() + (column + 0.5) * g.cellSize - centreE;
                const double n = g.north() - (row + 0.5) * g.cellSize - centreN;
                const double across = (e * motionE + n * motionN) / 1000.0;
                const double along = (e * alongE + n * alongN) / 1000.0;
                // The line bends and its band varies along its length, both
                // moving with it.
                const double bend = 45.0 * WeatherNoise::fractal(along / 250.0, 0.5, 3, seed);
                const double ahead = across - travelled - bend;
                Station s = stationAt(settings.scenario, ahead);
                const double vary = WeatherNoise::fractal(along / 120.0, (across - travelled) / 120.0, 3, seed + 1u);
                Cell &c = cells[k][size_t(row) * g.columns + column];
                const L *source[WeatherLayer::Count] = {&s.low, &s.mid, &s.high};
                for (int l = 0; l < WeatherLayer::Count; ++l) {
                    L v = *source[l];
                    double cover = v.coverage * (1.0 + 0.3 * vary);
                    // Heaps follow the day; the front's own towers do not.
                    if (v.type >= 0.6f && v.type < 0.9f)
                        cover *= convectiveDay(hour);
                    v.coverage = float(std::clamp(cover, 0.0, 1.0));
                    c.layers[l] = v;
                }
                c.rain = s.rain * std::clamp(1.0 + 0.6 * vary, 0.2, 2.0);
                c.visibility = c.rain > 0.05 ? std::max(1500.0, s.visibility / (1.0 + 0.3 * vary))
                                             : double(s.visibility);
            }
    }

    // Keyframes: the area's means, the wind of the cross-section at the
    // centre.
    for (int k = 0; k < Keyframes; ++k) {
        WeatherKeyframe f;
        f.time = float(k);
        f.variationAmount = 0.03f;
        f.variationScale = 15000.0f;
        double sums[WeatherLayer::Count][5] = {};
        double rain = 0.0, visibility = 0.0;
        for (const Cell &c : cells[k]) {
            rain += c.rain;
            visibility += c.visibility;
            for (int l = 0; l < WeatherLayer::Count; ++l) {
                sums[l][0] += c.layers[l].type;
                sums[l][1] += c.layers[l].coverage;
                sums[l][2] += c.layers[l].base;
                sums[l][3] += c.layers[l].top;
                sums[l][4] += c.layers[l].density;
            }
        }
        const double count = double(cells[k].size());
        for (int l = 0; l < WeatherLayer::Count; ++l)
            f.layers[l] = {float(sums[l][0] / count), float(sums[l][1] / count), float(sums[l][2] / count),
                           float(sums[l][3] / count), float(sums[l][4] / count)};
        f.precipitationRate = float(rain / count);
        f.precipitation = f.precipitationRate < 0.05f ? WeatherPrecipitation::None : WeatherPrecipitation::Rain;
        f.visibility = float(visibility / count);
        const Station centre = stationAt(settings.scenario, -settings.speed * (k - settings.arrival));
        // The wind blows roughly with the front, turning as it passes.
        float from = float(std::fmod(settings.heading + 180.0 + centre.windTurn + 360.0, 360.0));
        f.wind[WeatherLayer::Low] = {from, centre.windSpeed};
        f.wind[WeatherLayer::Mid] = {float(std::fmod(from + 10.0f, 360.0f)), centre.windSpeed * 2.0f};
        f.wind[WeatherLayer::High] = {float(std::fmod(from + 20.0f, 360.0f)), centre.windSpeed * 3.5f};
        weather.keyframes.push_back(f);
    }

    weather.allocateMaps();
    for (int k = 0; k < Keyframes; ++k)
        for (int row = 0; row < g.rows; ++row)
            for (int column = 0; column < g.columns; ++column) {
                const Cell &c = cells[k][size_t(row) * g.columns + column];
                auto put = [&](int index, double a, double b, double cc, double d) {
                    auto *line = reinterpret_cast<QRgba64 *>(weather.mapImage(index).scanLine(k * g.rows + row));
                    auto q = [](double v) { return quint16(std::clamp(std::lround(v), 0L, 65535L)); };
                    line[column] = QRgba64::fromRgba64(q(a), q(b), q(cc), q(d));
                };
                for (int l = 0; l < WeatherLayer::Count; ++l)
                    put(l, c.layers[l].coverage * 65535.0, c.layers[l].type * 65535.0, c.layers[l].base,
                        c.layers[l].top);
                put(Weather::SurfaceMap, c.rain / 0.01, 0.0, c.visibility / 2.0, 65535.0);
            }
    return weather;
}

}
