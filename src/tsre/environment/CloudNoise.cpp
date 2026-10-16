/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/environment/CloudNoise.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <thread>

namespace CloudNoise {

namespace {

quint32 hash(int x, int y, int z, quint32 seed) {
    quint32 h = quint32(x) * 0x8da6b343u ^ quint32(y) * 0xd8163841u ^ quint32(z) * 0xcb1ab31fu ^ seed * 0x9e3779b9u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

float unit(quint32 h) {
    return float(h & 0xffffffu) / float(0x1000000);
}

int wrap(int i, int period) {
    const int r = i % period;
    return r < 0 ? r + period : r;
}

float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Gradient at a lattice corner: one of 12 cube edge directions.
float corner(int x, int y, int z, int period, float dx, float dy, float dz) {
    static const float g[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
                                   {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
    const quint32 h = hash(wrap(x, period), wrap(y, period), wrap(z, period), 17u + quint32(period)) % 12u;
    return g[h][0] * dx + g[h][1] * dy + g[h][2] * dz;
}

float gradient(float x, float y, float z, int period) {
    const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const int ix = int(fx), iy = int(fy), iz = int(fz);
    const float dx = x - fx, dy = y - fy, dz = z - fz;
    const float u = fade(dx), v = fade(dy), w = fade(dz);
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float x00 = lerp(corner(ix, iy, iz, period, dx, dy, dz), corner(ix + 1, iy, iz, period, dx - 1, dy, dz), u);
    const float x10 = lerp(corner(ix, iy + 1, iz, period, dx, dy - 1, dz),
                           corner(ix + 1, iy + 1, iz, period, dx - 1, dy - 1, dz), u);
    const float x01 = lerp(corner(ix, iy, iz + 1, period, dx, dy, dz - 1),
                           corner(ix + 1, iy, iz + 1, period, dx - 1, dy, dz - 1), u);
    const float x11 = lerp(corner(ix, iy + 1, iz + 1, period, dx, dy - 1, dz - 1),
                           corner(ix + 1, iy + 1, iz + 1, period, dx - 1, dy - 1, dz - 1), u);
    return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
}

float remap(float value, float low, float high, float newLow, float newHigh) {
    return newLow + (value - low) / (high - low) * (newHigh - newLow);
}

// Fills size^3 RGBA8 texels on all cores.
std::vector<quint8> volume(int size, const std::function<void(float, float, float, float *)> &texel) {
    std::vector<quint8> out(size_t(size) * size * size * 4);
    const int threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&, t] {
            for (int z = t; z < size; z += threads)
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x) {
                        float v[4] = {0, 0, 0, 0};
                        texel((x + 0.5f) / size, (y + 0.5f) / size, (z + 0.5f) / size, v);
                        quint8 *p = &out[((size_t(z) * size + y) * size + x) * 4];
                        for (int c = 0; c < 4; ++c)
                            p[c] = quint8(std::lround(std::clamp(v[c], 0.0f, 1.0f) * 255.0f));
                    }
        });
    }
    for (std::thread &w : workers)
        w.join();
    return out;
}

} // namespace

float worley(float x, float y, float z, int cells) {
    const float px = x * cells, py = y * cells, pz = z * cells;
    const int cx = int(std::floor(px)), cy = int(std::floor(py)), cz = int(std::floor(pz));
    float nearest = 1e9f;
    for (int k = -1; k <= 1; ++k)
        for (int j = -1; j <= 1; ++j)
            for (int i = -1; i <= 1; ++i) {
                const int x0 = cx + i, y0 = cy + j, z0 = cz + k;
                const quint32 h = hash(wrap(x0, cells), wrap(y0, cells), wrap(z0, cells), quint32(cells));
                const float fx = x0 + unit(h), fy = y0 + unit(h * 0x27d4eb2du + 1u),
                            fz = z0 + unit(h * 0x165667b1u + 7u);
                const float d = (fx - px) * (fx - px) + (fy - py) * (fy - py) + (fz - pz) * (fz - pz);
                nearest = std::min(nearest, d);
            }
    return 1.0f - std::clamp(std::sqrt(nearest), 0.0f, 1.0f);
}

float perlin(float x, float y, float z, int frequency, int octaves) {
    float sum = 0.0f, amplitude = 1.0f, total = 0.0f;
    int period = frequency;
    for (int o = 0; o < octaves; ++o) {
        sum += amplitude * gradient(x * period, y * period, z * period, period);
        total += amplitude;
        amplitude *= 0.5f;
        period *= 2;
    }
    return std::clamp(sum / total * 0.9f + 0.5f, 0.0f, 1.0f);
}

std::vector<quint8> shape(int size) {
    return volume(size, [](float x, float y, float z, float *v) {
        const float worleyBase = worley(x, y, z, 4) * 0.625f + worley(x, y, z, 8) * 0.25f
                + worley(x, y, z, 16) * 0.125f;
        v[0] = std::clamp(remap(perlin(x, y, z, 4, 3), 0.0f, 1.0f, worleyBase, 1.0f), 0.0f, 1.0f);
        const float w8 = worley(x, y, z, 8), w16 = worley(x, y, z, 16), w32 = worley(x, y, z, 32),
                    w64 = worley(x, y, z, 64);
        v[1] = w8 * 0.625f + w16 * 0.25f + w32 * 0.125f;
        v[2] = w16 * 0.625f + w32 * 0.25f + w64 * 0.125f;
        v[3] = w32 * 0.75f + w64 * 0.25f;
    });
}

std::vector<quint8> curl(int size) {
    // The potential on the texels, then its curl by central differences
    // (wrapping), scaled to the largest.
    std::vector<float> potential(size_t(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            potential[size_t(y) * size + x] = perlin((x + 0.5f) / size, (y + 0.5f) / size, 0.37f, 4, 3);
    auto at = [&](int x, int y) { return potential[size_t(wrap(y, size)) * size + wrap(x, size)]; };
    std::vector<float> field(size_t(size) * size * 2);
    float largest = 1e-6f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float vx = at(x, y + 1) - at(x, y - 1);
            const float vy = -(at(x + 1, y) - at(x - 1, y));
            field[(size_t(y) * size + x) * 2] = vx;
            field[(size_t(y) * size + x) * 2 + 1] = vy;
            largest = std::max(largest, std::max(std::abs(vx), std::abs(vy)));
        }
    std::vector<quint8> out(size_t(size) * size * 4);
    for (size_t i = 0; i < size_t(size) * size; ++i) {
        for (int c = 0; c < 2; ++c)
            out[i * 4 + c] = quint8(std::lround(std::clamp(0.5f + 0.5f * field[i * 2 + c] / largest, 0.0f, 1.0f) * 255.0f));
        out[i * 4 + 2] = 128;
        out[i * 4 + 3] = 255;
    }
    return out;
}

std::vector<quint16> blue(int size) {
    const int n = size * size;
    // Each texel's energy: a Gaussian (sigma 1.5 texels) of the points
    // around it, wrapped.
    constexpr int Radius = 6;
    constexpr int Width = 2 * Radius + 1;
    float kernel[Width * Width];
    for (int dy = -Radius; dy <= Radius; ++dy)
        for (int dx = -Radius; dx <= Radius; ++dx)
            kernel[(dy + Radius) * Width + dx + Radius] = std::exp(-float(dx * dx + dy * dy) / (2.0f * 1.5f * 1.5f));
    std::vector<char> on(n, 0);
    std::vector<float> energy(n, 0.0f);
    auto spread = [&](std::vector<float> &field, int i, float sign) {
        const int x = i % size, y = i / size;
        for (int dy = -Radius; dy <= Radius; ++dy)
            for (int dx = -Radius; dx <= Radius; ++dx)
                field[((y + dy + size) % size) * size + (x + dx + size) % size]
                        += sign * kernel[(dy + Radius) * Width + dx + Radius];
    };
    // The densest point of those set (want) or the emptiest place of those not.
    auto extreme = [&](const std::vector<float> &field, char want, bool densest) {
        int best = -1;
        for (int i = 0; i < n; ++i)
            if (on[i] == want && (best < 0 || (densest ? field[i] > field[best] : field[i] < field[best])))
                best = i;
        return best;
    };
    // A tenth of the texels at random (always the same), relaxed: the
    // densest point moved to the emptiest place until it stays.
    std::mt19937 random(0x5eedu);
    int count = 0;
    while (count < n / 10) {
        const int i = int(random() % quint32(n));
        if (!on[i]) {
            on[i] = 1;
            spread(energy, i, 1.0f);
            ++count;
        }
    }
    for (int guard = 0; guard < n; ++guard) {
        const int densest = extreme(energy, 1, true);
        on[densest] = 0;
        spread(energy, densest, -1.0f);
        const int emptiest = extreme(energy, 0, false);
        on[emptiest] = 1;
        spread(energy, emptiest, 1.0f);
        if (emptiest == densest)
            break;
    }
    std::vector<int> rank(n, 0);
    // The first ranks: those points, the densest taken away last.
    {
        const std::vector<char> keep = on;
        const std::vector<float> keepEnergy = energy;
        for (int r = count - 1; r >= 0; --r) {
            const int densest = extreme(energy, 1, true);
            on[densest] = 0;
            spread(energy, densest, -1.0f);
            rank[densest] = r;
        }
        on = keep;
        energy = keepEnergy;
    }
    // Then to half: points in the emptiest places.
    int r = count;
    for (; r < n / 2; ++r) {
        const int emptiest = extreme(energy, 0, false);
        on[emptiest] = 1;
        spread(energy, emptiest, 1.0f);
        rank[emptiest] = r;
    }
    // The rest: the places left as the points, their densest set first.
    std::vector<float> empty(n, 0.0f);
    for (int i = 0; i < n; ++i)
        if (!on[i])
            spread(empty, i, 1.0f);
    for (; r < n; ++r) {
        const int densest = extreme(empty, 0, true);
        on[densest] = 1;
        spread(empty, densest, -1.0f);
        rank[densest] = r;
    }
    std::vector<quint16> out(n);
    for (int i = 0; i < n; ++i)
        out[i] = quint16(std::lround((rank[i] + 0.5) / n * 65535.0));
    return out;
}

std::vector<quint8> detail(int size) {
    return volume(size, [](float x, float y, float z, float *v) {
        const float w2 = worley(x, y, z, 2), w4 = worley(x, y, z, 4), w8 = worley(x, y, z, 8),
                    w16 = worley(x, y, z, 16);
        v[0] = w2 * 0.625f + w4 * 0.25f + w8 * 0.125f;
        v[1] = w4 * 0.625f + w8 * 0.25f + w16 * 0.125f;
        v[2] = w8 * 0.75f + w16 * 0.25f;
        v[3] = 1.0f;
    });
}

}
