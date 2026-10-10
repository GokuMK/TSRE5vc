/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ATMOSPHERE_H
#define ATMOSPHERE_H

#include <array>
#include <vector>

// The Earth's clear atmosphere (task renderer 28), after Hillaire, "A
// Scalable and Production Ready Sky and Atmosphere Rendering Technique",
// EGSR 2020: Rayleigh and Mie scattering, ozone absorption, a ground of
// fixed albedo, single scattering marched and multiple scattering from a
// table. Lengths in km; light relative to the sun's illuminance at the top
// of the atmosphere (1). The renderer's shaders compute the same with the
// tables made here; the CPU side gives the colours the classic shading
// takes (fog, sun) and the tests.
namespace Atmosphere {

using Rgb = std::array<float, 3>;

constexpr double BottomRadius = 6360.0;
constexpr double TopRadius = 6460.0;

// Table sizes: transmittance by (view zenith, height), multiple scattering
// by (sun zenith, height), RGBA float rows.
constexpr int TransmittanceWidth = 256;
constexpr int TransmittanceHeight = 64;
constexpr int MultipleWidth = 32;
constexpr int MultipleHeight = 32;

// Sun's angular radius (degrees).
constexpr double SunRadiusDegrees = 0.2675;

struct Medium {
    Rgb rayleighScattering{5.802e-3f, 13.558e-3f, 33.1e-3f};
    float rayleighHeight = 8.0f;
    float mieScattering = 3.996e-3f;
    float mieExtinction = 4.440e-3f;
    float mieHeight = 1.2f;
    float mieG = 0.8f;
    Rgb ozoneAbsorption{0.650e-3f, 1.881e-3f, 0.085e-3f};
    float ozoneCentre = 25.0f;
    float ozoneHalfWidth = 15.0f;
    Rgb groundAlbedo{0.3f, 0.3f, 0.3f};
};

class Model {
public:
    Model();
    const Medium &medium() const { return m; }
    // RGBA rows, row 0 the lowest coordinate.
    const std::vector<float> &transmittanceTable() const { return transmittance; }
    const std::vector<float> &multipleTable() const { return multiple; }

    // Transmittance from a height (km above the ground) along a direction
    // (cosine of its zenith angle) to the top of the atmosphere; through
    // the planet if it points into it.
    Rgb transmittanceToTop(double height, double cosZenith) const;
    // Sun light reaching a height: the transmittance, faded as the sun's
    // disc sets behind the planet.
    Rgb sunLight(double height, double sunCosZenith) const;
    // Sky light (single and multiple scattering) seen from a height along a
    // direction; up is y. Ground hits end the ray (no ground light).
    Rgb skyRadiance(double height, const float *direction, const float *sunDirection) const;

private:
    Medium m;
    std::vector<float> transmittance;
    std::vector<float> multiple;
};

// One shared model (the tables take a moment).
const Model &model();

// Sky light to the display colour the classic shading works in: an
// exposure, a smooth roll-off to white and the display curve. The sky
// shader does the same.
constexpr float SkyExposure = 16.0f;
Rgb toDisplay(const Rgb &radiance);
// The sky's colour along the horizon, averaged around it (display colour):
// the fog colour of a physical sky. height in km, sun as for skyRadiance.
Rgb horizonColour(double height, const float *sunDirection);
// The zenith's colour (display colour).
Rgb zenithColour(double height, const float *sunDirection);

// The texture coordinates the shaders use too.
void transmittanceUv(double radius, double cosZenith, double &u, double &v);
void transmittanceParameters(double u, double v, double &radius, double &cosZenith);

}

#endif
