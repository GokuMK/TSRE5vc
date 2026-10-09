/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "WaterWaves.h"
#include <settings/SettingsAccess.h>
#include <tsre/renderer/Renderer.h>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

const double WaterWaves::Periods[WaterWaves::Cascades] = {2048.0 / 7.0, 2048.0 / 53.0, 2048.0 / 389.0};
const double WaterWaves::BandEdges[WaterWaves::Cascades - 1] = {8.0, 1.0};

namespace {

const double TwoPi = 6.283185307179586;
const double Gravity = 9.81;
// Waves shorter than about this (m) are damped: capillary waves, and the
// limit of the smallest cascade.
const double ShortestWave = 0.01;
// Share of a wave's energy across the wind (0) to along it (1), and the
// share kept by waves running against the wind.
const double AcrossWind = 0.15;
const double AgainstWind = 0.3;

// The generator's own uniform and normal numbers, the same with every
// compiler (std::normal_distribution is not).
struct Random {
    uint64_t state;
    explicit Random(uint64_t seed) : state(seed) {}
    double uniform() {
        // splitmix64
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return (double(z >> 11) + 0.5) / 9007199254740992.0;
    }
    std::complex<double> gaussian() {
        // Box-Muller: two independent normal numbers.
        const double r = std::sqrt(-2.0 * std::log(uniform()));
        const double a = TwoPi * uniform();
        return {r * std::cos(a), r * std::sin(a)};
    }
};

// In-place inverse FFT (exponent +i) of n = 2^k values, unnormalised.
void inverseFft(std::complex<float> *data, int n) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (int length = 2; length <= n; length <<= 1) {
        const double angle = TwoPi / length;
        const std::complex<float> step(float(std::cos(angle)), float(std::sin(angle)));
        for (int i = 0; i < n; i += length) {
            std::complex<float> w(1.0f, 0.0f);
            for (int k = 0; k < length / 2; ++k) {
                const std::complex<float> u = data[i + k];
                const std::complex<float> v = data[i + k + length / 2] * w;
                data[i + k] = u + v;
                data[i + k + length / 2] = u - v;
                w *= step;
            }
        }
    }
}

// Inverse 2D FFT of a Size x Size array, rows along x.
void inverseFft2(std::complex<float> *data, int n) {
    for (int row = 0; row < n; ++row)
        inverseFft(data + size_t(row) * n, n);
    std::vector<std::complex<float>> column(n);
    for (int x = 0; x < n; ++x) {
        for (int z = 0; z < n; ++z)
            column[z] = data[size_t(z) * n + x];
        inverseFft(column.data(), n);
        for (int z = 0; z < n; ++z)
            data[size_t(z) * n + x] = column[z];
    }
}

// Wave number of index i of an n-point transform: 0..n/2-1, then -n/2..-1.
int waveIndex(int i, int n) {
    return i < n / 2 ? i : i - n;
}

}

WaterWaves::Wind WaterWaves::settingsWind() {
    static quint64 frame = ~quint64(0);
    static Wind wind;
    if (frame != Renderer::frameNumber()) {
        frame = Renderer::frameNumber();
        wind.speed = std::clamp(Settings::floating("core.rendering.water.windSpeed"), 0.0, 30.0);
        wind.direction = Settings::floating("core.rendering.water.windDirection");
    }
    return wind;
}

double WaterWaves::meanSquareSlope(double windSpeed) {
    return 0.003 + 0.00512 * std::clamp(windSpeed, 0.0, 30.0);
}

WaterWaves::Field::Field(const Wind &wind) : wind(wind) {
    const int n = Size;
    // A light breeze still makes ripples; the spectrum's shape needs some.
    const double speed = std::max(wind.speed, 0.5);
    const double peakLength = speed * speed / Gravity;
    const double direction = wind.direction * TwoPi / 360.0;
    // North is -z where the world is drawn (MSTS z turned round).
    const double windX = std::sin(direction), windZ = -std::cos(direction);
    work.resize(size_t(n) * n);
    for (int c = 0; c < Cascades; ++c) {
        const double dk = TwoPi / Periods[c];
        const double low = c == 0 ? 0.0 : TwoPi / BandEdges[c - 1];
        const double high = c == Cascades - 1 ? 1e30 : TwoPi / BandEdges[c];
        start[c].assign(size_t(n) * n, {0.0, 0.0});
        omega[c].assign(size_t(n) * n, 0.0);
        Random random(20261009u + 7919u * c);
        for (int z = 0; z < n; ++z)
            for (int x = 0; x < n; ++x) {
                const size_t i = size_t(z) * n + x;
                const std::complex<double> noise = random.gaussian();
                const int a = waveIndex(x, n), b = waveIndex(z, n);
                // The Nyquist row and column have no pair for -k.
                if (std::abs(a) == n / 2 || std::abs(b) == n / 2 || (a == 0 && b == 0))
                    continue;
                const double kx = a * dk, kz = b * dk;
                const double k = std::sqrt(kx * kx + kz * kz);
                if (k < low || k >= high)
                    continue;
                const double along = (kx * windX + kz * windZ) / k;
                double spread = AcrossWind + (1.0 - AcrossWind) * along * along;
                if (along < 0.0)
                    spread *= AgainstWind;
                // Phillips spectrum, damped below ShortestWave.
                const double kl = k * peakLength;
                const double phillips = std::exp(-1.0 / (kl * kl)) / (k * k * k * k) * spread
                        * std::exp(-k * k * ShortestWave * ShortestWave);
                start[c][i] = noise * std::sqrt(phillips / 2.0) * dk;
                omega[c][i] = std::sqrt(Gravity * k);
            }
    }
    // Scale the waves to the mean square slope of the wind.
    std::vector<float> texels(size_t(Cascades) * n * n * 4);
    double total = 0.0;
    for (int c = 0; c < Cascades; ++c) {
        cascade(c, 0.0, texels.data() + size_t(c) * n * n * 4);
        double sum = 0.0;
        for (size_t i = 0; i < size_t(n) * n; ++i)
            sum += texels[size_t(c) * n * n * 4 + i * 4 + 2] + texels[size_t(c) * n * n * 4 + i * 4 + 3];
        cascadeSlopeVariance[c] = sum / (double(n) * n);
        total += cascadeSlopeVariance[c];
    }
    if (total <= 0.0)
        return;
    const double target = meanSquareSlope(wind.speed);
    const double scale = std::sqrt(target / total);
    for (int c = 0; c < Cascades; ++c) {
        for (std::complex<double> &value : start[c])
            value *= scale;
        cascadeSlopeVariance[c] *= target / total;
    }
}

void WaterWaves::Field::cascade(int c, double seconds, float *texels) {
    const int n = Size;
    const double dk = TwoPi / Periods[c];
    const std::complex<double> *h0 = start[c].data();
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const size_t i = size_t(z) * n + x;
            const size_t opposite = size_t((n - z) % n) * n + size_t((n - x) % n);
            if (h0[i] == 0.0 && h0[opposite] == 0.0) {
                work[i] = 0.0f;
                continue;
            }
            // h(k, t): the wave along k and the one along -k, each turning
            // at its own frequency, so the field stays real.
            const double phase = omega[c][i] * seconds;
            const std::complex<double> turn(std::cos(phase), std::sin(phase));
            const std::complex<double> h = h0[i] * turn + std::conj(h0[opposite]) * std::conj(turn);
            // Slopes i kx h (real part) and i kz h (imaginary part) in one
            // transform: both are real fields.
            const double kx = waveIndex(x, n) * dk, kz = waveIndex(z, n) * dk;
            const std::complex<double> slopes = std::complex<double>(0.0, kx) * h - kz * h;
            work[i] = std::complex<float>(float(slopes.real()), float(slopes.imag()));
        }
    inverseFft2(work.data(), n);
    for (size_t i = 0; i < size_t(n) * n; ++i) {
        const float sx = work[i].real(), sz = work[i].imag();
        texels[i * 4 + 0] = sx;
        texels[i * 4 + 1] = sz;
        texels[i * 4 + 2] = sx * sx;
        texels[i * 4 + 3] = sz * sz;
    }
}

void WaterWaves::Field::compute(double seconds, std::vector<float> &texels) {
    const size_t layer = size_t(Size) * Size * 4;
    texels.resize(layer * Cascades);
    for (int c = 0; c < Cascades; ++c)
        cascade(c, seconds, texels.data() + layer * c);
}

WaterWaves &WaterWaves::shared() {
    static WaterWaves waves;
    return waves;
}

WaterWaves::~WaterWaves() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    wake.notify_all();
    if (worker.joinable())
        worker.join();
}

size_t WaterWaves::levelOffset(int level) {
    size_t offset = 0;
    for (int l = 0; l < level; ++l)
        offset += size_t(Cascades) * (Size >> l) * (Size >> l) * 4;
    return offset;
}

void WaterWaves::publish(std::vector<float> &texels) {
    // Each level the mean of 2 x 2 texels of the one above: the mean slope
    // and the mean of the squares, from which the shader takes the variance.
    std::vector<float> levels(levelOffset(Levels));
    std::copy(texels.begin(), texels.end(), levels.begin());
    for (int l = 1; l < Levels; ++l) {
        const int side = Size >> l, above = side * 2;
        for (int c = 0; c < Cascades; ++c) {
            const float *from = levels.data() + levelOffset(l - 1) + size_t(c) * above * above * 4;
            float *to = levels.data() + levelOffset(l) + size_t(c) * side * side * 4;
            for (int z = 0; z < side; ++z)
                for (int x = 0; x < side; ++x)
                    for (int k = 0; k < 4; ++k)
                        to[(size_t(z) * side + x) * 4 + k] = 0.25f
                                * (from[(size_t(2 * z) * above + 2 * x) * 4 + k]
                                   + from[(size_t(2 * z) * above + 2 * x + 1) * 4 + k]
                                   + from[(size_t(2 * z + 1) * above + 2 * x) * 4 + k]
                                   + from[(size_t(2 * z + 1) * above + 2 * x + 1) * 4 + k]);
        }
    }
    std::vector<qfloat16> half(levels.size());
    qFloatToFloat16(half.data(), levels.data(), qsizetype(levels.size()));
    std::lock_guard<std::mutex> lock(mutex);
    ready.swap(half);
    ++readySerial;
}

void WaterWaves::run() {
    std::unique_ptr<Field> field;
    std::vector<float> texels;
    for (;;) {
        double seconds;
        Wind wind;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return stopping || requested; });
            if (stopping)
                return;
            requested = false;
            seconds = requestTime;
            wind = requestWind;
        }
        if (field == nullptr || field->wind != wind)
            field = std::make_unique<Field>(wind);
        field->compute(seconds, texels);
        publish(texels);
    }
}

bool WaterWaves::update(double seconds, const Wind &wind, bool frozen, quint64 &serial,
                        std::vector<qfloat16> &texels) {
    bool now;
    {
        std::lock_guard<std::mutex> lock(mutex);
        now = frozen || readySerial == 0 || computedWind != wind;
    }
    if (now) {
        // Tests, the first frame and a new wind: the waves of this time.
        if (!computed || computedTime != seconds || computedWind != wind) {
            Field field(wind);
            std::vector<float> texels;
            field.compute(seconds, texels);
            publish(texels);
            computed = true;
            computedTime = seconds;
            computedWind = wind;
        }
    } else if (seconds != lastTime) {
        // Once a frame: the worker computes the waves of the next one.
        if (lastTime >= 0.0 && seconds > lastTime)
            frameStep = std::clamp(0.8 * frameStep + 0.2 * (seconds - lastTime), 1.0 / 240.0, 0.25);
        {
            std::lock_guard<std::mutex> lock(mutex);
            requested = true;
            requestTime = seconds + frameStep;
            requestWind = wind;
        }
        if (!worker.joinable())
            worker = std::thread(&WaterWaves::run, this);
        wake.notify_one();
    }
    lastTime = seconds;
    std::lock_guard<std::mutex> lock(mutex);
    if (readySerial == serial)
        return false;
    serial = readySerial;
    texels = ready;
    return true;
}

WaterWaveTexture::~WaterWaveTexture() {
    QOpenGLContext *current = QOpenGLContext::currentContext();
    if (texture != 0 && current != nullptr && current == context)
        current->functions()->glDeleteTextures(1, &texture);
}

bool WaterWaveTexture::bind(QOpenGLFunctions *f, int unit, double seconds,
                            const WaterWaves::Wind &wind, bool frozen) {
    QOpenGLContext *current = QOpenGLContext::currentContext();
    if (current == nullptr)
        return false;
    QOpenGLExtraFunctions *e = current->extraFunctions();
    if (current != context) {
        // A texture of another context cannot be used or deleted here.
        texture = 0;
        context = current;
        serial = 0;
    }
    f->glActiveTexture(GL_TEXTURE0 + unit);
    const int n = WaterWaves::Size;
    if (texture == 0) {
        f->glGenTextures(1, &texture);
        f->glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
        for (int level = 0; level < WaterWaves::Levels; ++level)
            e->glTexImage3D(GL_TEXTURE_2D_ARRAY, level, GL_RGBA16F, n >> level, n >> level,
                            WaterWaves::Cascades, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        f->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, WaterWaves::Levels - 1);
        f->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        f->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        f->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Grazing views keep the waves sharper with anisotropic filtering.
        if (current->hasExtension("GL_EXT_texture_filter_anisotropic")
                || current->hasExtension("GL_ARB_texture_filter_anisotropic"))
            f->glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
    } else {
        f->glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    }
    if (WaterWaves::shared().update(seconds, wind, frozen, serial, texels)) {
        for (int level = 0; level < WaterWaves::Levels; ++level)
            e->glTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, 0, n >> level, n >> level,
                               WaterWaves::Cascades, GL_RGBA, GL_HALF_FLOAT,
                               texels.data() + WaterWaves::levelOffset(level));
    }
    f->glActiveTexture(GL_TEXTURE0);
    return true;
}
