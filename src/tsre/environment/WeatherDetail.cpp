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

double corner(qint64 ix, qint64 iy, quint32 seed, double dx, double dy) {
    const double angle = hash(ix, iy, seed) * (2.0 * M_PI / 4294967296.0);
    return std::cos(angle) * dx + std::sin(angle) * dy;
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
    for (int i = 0; i < octaves; ++i) {
        // Each octave turned and shifted, so their lattices do not line up.
        const double turn = 0.5 + i * 1.3;
        const double rx = x * std::cos(turn) - y * std::sin(turn);
        const double ry = x * std::sin(turn) + y * std::cos(turn);
        sum += amplitude * gradient(rx + i * 17.31, ry - i * 9.77, seed + quint32(i) * 0x9e3779b9u);
        total += amplitude;
        amplitude *= 0.5;
        x *= 2.0;
        y *= 2.0;
    }
    return sum / total * 1.6;
}

double WeatherNoise::distribution(double value) {
    // The quantiles of the four-octave noise, measured once over many
    // points: the noise's distribution does not depend on the seed.
    static const std::vector<double> quantiles = [] {
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
    // Stratus sheets break into large patches, cumulus into small cells,
    // cumulonimbus into large towers.
    static const double scales[] = {5000.0, 2500.0, 1200.0, 3000.0};
    const double t = std::clamp(double(type), 0.0, 1.0) * 3.0;
    const int i = std::min(2, int(t));
    return scales[i] + (scales[i + 1] - scales[i]) * (t - i);
}

double WeatherDetail::cover(const Weather &weather, double hours, double east, double north, int layer) {
    const WeatherLayerValues values = weather.layer(hours, east, north, layer);
    if (values.coverage <= 0.0f)
        return 0.0;
    double driftEast = 0.0, driftNorth = 0.0;
    weather.drift(hours, layer, driftEast, driftNorth);
    const double scale = cloudScale(values.type);
    const double noise = WeatherNoise::fractal((east - driftEast) / scale, (north - driftNorth) / scale, 4,
                                               weather.seed * 3u + quint32(layer) + 101u);
    const double even = WeatherNoise::distribution(noise);
    // A ramp centred on the threshold: its mean over an even distribution is
    // the share above the threshold, the coverage.
    const double threshold = 1.0 - values.coverage;
    const double edge = 0.04 + 0.08 * (1.0 - values.type);
    const double t = std::clamp((even - threshold + edge) / (2.0 * edge), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
