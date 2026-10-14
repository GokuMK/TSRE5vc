/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/environment/WeatherDetail.h>
#include <tsre/environment/Weather.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

quint32 hash(qint64 x, qint64 y, quint32 seed) {
    quint32 h = quint32(x) * 0x8da6b343u ^ quint32(y) * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

// 256 unit gradients evenly around the circle; a corner's hash picks one
// (the renderer's shader uses the same table).
struct Gradients {
    double x[256], y[256];
    Gradients() {
        for (int i = 0; i < 256; ++i) {
            x[i] = std::cos(i * (2.0 * M_PI / 256.0));
            y[i] = std::sin(i * (2.0 * M_PI / 256.0));
        }
    }
};
const Gradients gradients;

double corner(qint64 ix, qint64 iy, quint32 seed, double dx, double dy) {
    const quint32 i = hash(ix, iy, seed) >> 24;
    return gradients.x[i] * dx + gradients.y[i] * dy;
}

// Each octave turned and shifted, so their lattices do not line up.
struct Octaves {
    double c[8], s[8];
    Octaves() {
        for (int i = 0; i < 8; ++i) {
            c[i] = std::cos(0.5 + i * 1.3);
            s[i] = std::sin(0.5 + i * 1.3);
        }
    }
};
const Octaves octaveTurns;

// Stratus sheets break into large patches, cumulus into small cells,
// cumulonimbus into large towers.
const double cloudScales[] = {5000.0, 2500.0, 1200.0, 3000.0};

// A cloud type between two of the scales: which two, and how far along.
void scalePair(float type, int &i, double &along) {
    const double t = std::clamp(double(type), 0.0, 1.0) * 3.0;
    i = std::min(2, int(t));
    along = t - i;
}

double fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

} // namespace

double WeatherNoise::gradient(double x, double y, quint32 seed) {
    const double fx = std::floor(x), fy = std::floor(y);
    const qint64 ix = qint64(fx), iy = qint64(fy);
    const double dx = x - fx, dy = y - fy;
    const double u = fade(dx), v = fade(dy);
    const double a = corner(ix, iy, seed, dx, dy);
    const double b = corner(ix + 1, iy, seed, dx - 1.0, dy);
    const double c = corner(ix, iy + 1, seed, dx, dy - 1.0);
    const double d = corner(ix + 1, iy + 1, seed, dx - 1.0, dy - 1.0);
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}

double WeatherNoise::fractal(double x, double y, int octaves, quint32 seed) {
    double sum = 0.0, amplitude = 1.0, total = 0.0;
    for (int i = 0; i < octaves && i < 8; ++i) {
        const double rx = x * octaveTurns.c[i] - y * octaveTurns.s[i];
        const double ry = x * octaveTurns.s[i] + y * octaveTurns.c[i];
        sum += amplitude * gradient(rx + i * 17.31, ry - i * 9.77, seed + quint32(i) * 0x9e3779b9u);
        total += amplitude;
        amplitude *= 0.5;
        x *= 2.0;
        y *= 2.0;
    }
    return sum / total * 1.6;
}

double WeatherNoise::fractal(double x, double y, int octaves, quint32 seed, double texel) {
    if (texel <= 0.0)
        return fractal(x, y, octaves, seed);
    // The spread of the full noise: the octaves' amplitudes add as squares.
    double sum = 0.0, amplitude = 1.0, squares = 0.0, fullSquares = 0.0, fullTotal = 0.0, cell = 1.0;
    for (int i = 0; i < octaves && i < 8; ++i) {
        const double weight = std::clamp((cell / texel - 2.0) / 2.0, 0.0, 1.0);
        if (weight > 0.0) {
            const double rx = x * octaveTurns.c[i] - y * octaveTurns.s[i];
            const double ry = x * octaveTurns.s[i] + y * octaveTurns.c[i];
            sum += weight * amplitude * gradient(rx + i * 17.31, ry - i * 9.77, seed + quint32(i) * 0x9e3779b9u);
            squares += weight * weight * amplitude * amplitude;
        }
        fullSquares += amplitude * amplitude;
        fullTotal += amplitude;
        amplitude *= 0.5;
        cell *= 0.5;
        x *= 2.0;
        y *= 2.0;
    }
    if (squares <= 0.0)
        return 0.0;
    return sum / std::sqrt(squares) * std::sqrt(fullSquares) / fullTotal * 1.6;
}

const std::vector<double> &WeatherNoise::quantiles() {
    // The quantiles of the four-octave noise, measured once over many
    // points: the noise's distribution does not depend on the seed.
    static const std::vector<double> table = [] {
        std::vector<double> samples;
        samples.reserve(1 << 16);
        for (int i = 0; i < (1 << 16); ++i) {
            const double x = (hash(i, 1, 7) & 0xffffff) / 1024.0;
            const double y = (hash(i, 2, 7) & 0xffffff) / 1024.0;
            samples.push_back(fractal(x, y, 4, 12345));
        }
        std::sort(samples.begin(), samples.end());
        std::vector<double> table(257);
        for (int i = 0; i <= 256; ++i)
            table[i] = samples[std::min<size_t>(samples.size() - 1, size_t(i) * (samples.size() - 1) / 256)];
        return table;
    }();
    return table;
}

double WeatherNoise::distribution(double value) {
    const std::vector<double> &quantiles = WeatherNoise::quantiles();
    if (value <= quantiles.front())
        return 0.0;
    if (value >= quantiles.back())
        return 1.0;
    const auto upper = std::upper_bound(quantiles.begin(), quantiles.end(), value);
    const size_t i = size_t(upper - quantiles.begin());
    const double low = quantiles[i - 1], high = quantiles[i];
    const double within = high > low ? (value - low) / (high - low) : 0.0;
    return (double(i - 1) + within) / 256.0;
}

double WeatherDetail::cloudScale(float type) {
    int i;
    double along;
    scalePair(type, i, along);
    return cloudScales[i] + (cloudScales[i + 1] - cloudScales[i]) * along;
}

double WeatherDetail::cover(const Weather &weather, double hours, double east, double north, int layer) {
    return cover(weather, weather.frame(hours), east, north, layer);
}

double WeatherDetail::cover(const Weather &weather, const Weather::Frame &frame, double east, double north,
                            int layer, double minimumEdge, double texel) {
    if (frame.empty)
        return 0.0;
    const WeatherLayerValues values = weather.layer(frame, east, north, layer);
    if (values.coverage <= 0.0f)
        return 0.0;
    // The noise at the two fixed scales the type lies between, mixed: a
    // scale following the type would move the far-from-origin noise by
    // whole cells from texel to texel as the type changes.
    int i;
    double along;
    scalePair(values.type, i, along);
    const double e = east - frame.drift[layer][0], n = north - frame.drift[layer][1];
    auto noiseAt = [&](int s) {
        const double scale = cloudScales[s];
        return WeatherNoise::fractal(e / scale, n / scale, 4,
                                     weather.seed * 3u + quint32(layer) + 101u + quint32(s) * 7919u, texel / scale);
    };
    double noise;
    if (along <= 0.001)
        noise = noiseAt(i);
    else if (along >= 0.999)
        noise = noiseAt(i + 1);
    else
        // Two independent noises mixed spread less: weighted back up.
        noise = (noiseAt(i) * (1.0 - along) + noiseAt(i + 1) * along)
                / std::sqrt((1.0 - along) * (1.0 - along) + along * along);
    const double even = WeatherNoise::distribution(noise);
    // A ramp centred on the threshold: its mean over an even distribution is
    // the share above the threshold, the coverage.
    const double threshold = 1.0 - values.coverage;
    const double edge = std::max(0.04 + 0.08 * (1.0 - values.type), minimumEdge);
    const double t = std::clamp((even - threshold + edge) / (2.0 * edge), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double WeatherDetail::imageEdge(double texel, float type) {
    return std::clamp(1.5 * texel / cloudScale(type), 0.0, 0.3);
}

double WeatherDetail::imageDetail(double texel, float type) {
    const double scale = cloudScale(type);
    return std::clamp((scale / 3.0 - texel) / (scale / 3.0 - scale / 10.0), 0.0, 1.0);
}

WeatherSurface WeatherDetail::precipitation(const Weather &weather, const Weather::Frame &frame, double east,
                                            double north, double cells) {
    WeatherSurface s = weather.surface(frame, east, north);
    if (s.precipitationRate <= 0.0f || frame.empty)
        return s;
    // Heavier and lighter areas tens of km across, drifting with the low
    // clouds; their mean factor is 1.
    const auto &d = frame.drift[WeatherLayer::Low];
    const double even = WeatherNoise::distribution(
            WeatherNoise::fractal((east - d[0]) / 12000.0, (north - d[1]) / 12000.0, 4, weather.seed * 5u + 211u));
    double rate = s.precipitationRate * (0.2 + 1.6 * even);
    // Showers fall from the cells of convective clouds.
    const WeatherLayerValues low = weather.layer(frame, east, north, WeatherLayer::Low);
    const double convective = std::clamp((low.type - 0.45) / 0.35, 0.0, 1.0);
    if (convective > 0.0 && low.coverage > 0.0f && cells > 0.0) {
        const double under = cover(weather, frame, east, north, WeatherLayer::Low)
                / std::max(double(low.coverage), 0.1);
        const double factor = 1.0 - convective + convective * std::min(under, 3.0);
        rate *= 1.0 + (factor - 1.0) * cells;
    }
    s.precipitationRate = float(rate);
    if (s.precipitationRate < 0.05f)
        s.precipitation = WeatherPrecipitation::None;
    return s;
}
