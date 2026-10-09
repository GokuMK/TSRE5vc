/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef WATERWAVES_H
#define WATERWAVES_H

#include <QtGlobal>
#include <qfloat16.h>
#include <complex>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

class QOpenGLContext;
class QOpenGLFunctions;

// Wind-driven waves for shaded water (Tessendorf's FFT waves): a wave
// spectrum from the wind, evolved in time with each wavelength at its own
// speed (deep water), and turned into surface slopes by an FFT. Three
// cascades of different sizes each hold one band of wavelengths; the water
// shader adds them up. The slopes are computed on the CPU, in a worker
// thread, and the renderers upload them as a texture array.
class WaterWaves {
public:
    // Texels per side of each cascade.
    static const int Size = 128;
    static const int Cascades = 3;
    // Mipmap levels, 128 x 128 down to 1 x 1. Made here, as not every QRhi
    // backend generates mipmaps of texture arrays.
    static const int Levels = 8;
    // Where a level starts in the uploaded texels, in half floats: level
    // after level, each with the cascades one after another.
    static size_t levelOffset(int level);
    // Metres per repeat of each cascade: 2048 / 7, / 53 and / 389. Each
    // divides the 2048 m tile, so the waves continue across tiles, and the
    // divisors share no factor, so the cascades line up only once per tile.
    // The water shader has the same values.
    static const double Periods[Cascades];
    // Wavelengths (m) where one cascade's band ends and the next begins.
    static const double BandEdges[Cascades - 1];

    struct Wind {
        // At 10 m height, m/s.
        double speed = 3.0;
        // Degrees from north towards east, the way the wind blows. North is
        // -z where the world is drawn (MSTS z turned round), east +x.
        double direction = 45.0;
        bool operator==(const Wind &o) const { return speed == o.speed && direction == o.direction; }
        bool operator!=(const Wind &o) const { return !(*this == o); }
    };

    // The wind of the settings (core.rendering.water.windSpeed and
    // .windDirection), read once a frame.
    static Wind settingsWind();

    // Mean square slope (both directions together) of a sea under this
    // wind, after Cox and Munk: the total the spectrum is scaled to.
    static double meanSquareSlope(double windSpeed);

    // The texels of all cascades at a time, computed now: cascade after
    // cascade, rows of Size RGBA texels: the slopes along x and z, and their
    // squares (so mipmaps keep the slope variance).
    struct Field {
        explicit Field(const Wind &wind);
        void compute(double seconds, std::vector<float> &texels);
        // Mean of the squared slopes of each cascade at time 0, after
        // scaling: they add up to meanSquareSlope.
        double cascadeSlopeVariance[Cascades] = {0.0, 0.0, 0.0};
        Wind wind;
    private:
        // Each cascade's spectrum at time 0, and the angular frequency of
        // each wave.
        std::vector<std::complex<double>> start[Cascades];
        std::vector<double> omega[Cascades];
        std::vector<std::complex<float>> work;
        void cascade(int c, double seconds, float *texels);
    };

    // The waves both renderers draw.
    static WaterWaves &shared();

    WaterWaves() = default;
    WaterWaves(const WaterWaves &) = delete;
    WaterWaves &operator=(const WaterWaves &) = delete;
    ~WaterWaves();

    // Once per frame: copies half-float texels of a newer state than
    // `serial` into `texels` (every level, see levelOffset) and returns true,
    // or returns false when there is none. While the animation runs the worker computes
    // the next frame's waves meanwhile, so the result is a frame late; when
    // the time stands still (tests), or there is nothing yet, they are
    // computed now for this time.
    bool update(double seconds, const Wind &wind, bool frozen, quint64 &serial,
                std::vector<qfloat16> &texels);

private:
    void run();
    void publish(std::vector<float> &texels);

    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool stopping = false;
    bool requested = false;
    double requestTime = 0.0;
    Wind requestWind;
    double lastTime = -1.0;
    double frameStep = 1.0 / 60.0;
    // Finished half-float texels and their serial number.
    std::vector<qfloat16> ready;
    quint64 readySerial = 0;
    // Waves of the last synchronous computation (time and wind), so a
    // frozen frame is not computed twice.
    double computedTime = -1.0;
    Wind computedWind;
    bool computed = false;
};

// The wave cascades as an OpenGL texture array (RGBA16F, mipmapped), kept in
// the context that created it.
class WaterWaveTexture {
public:
    WaterWaveTexture() = default;
    WaterWaveTexture(const WaterWaveTexture &) = delete;
    WaterWaveTexture &operator=(const WaterWaveTexture &) = delete;
    // Deletes the texture when its context is current.
    ~WaterWaveTexture();
    // Binds the array on the given unit, creating it and uploading newer
    // waves first; leaves unit 0 active.
    bool bind(QOpenGLFunctions *f, int unit, double seconds, const WaterWaves::Wind &wind,
              bool frozen);

private:
    unsigned int texture = 0;
    QOpenGLContext *context = nullptr;
    quint64 serial = 0;
    std::vector<qfloat16> texels;
};

#endif
