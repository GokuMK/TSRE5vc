/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/environment/Atmosphere.h>

#include <algorithm>
#include <cmath>

namespace Atmosphere {

namespace {

struct Vec {
    double x = 0.0, y = 0.0, z = 0.0;
};
Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec operator*(Vec a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double length(Vec a) { return std::sqrt(dot(a, a)); }

struct Colour {
    double r = 0.0, g = 0.0, b = 0.0;
    double &operator[](int i) { return i == 0 ? r : i == 1 ? g : b; }
    double operator[](int i) const { return i == 0 ? r : i == 1 ? g : b; }
};
Colour operator+(Colour a, Colour b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
Colour operator-(Colour a, Colour b) { return {a.r - b.r, a.g - b.g, a.b - b.b}; }
Colour operator*(Colour a, Colour b) { return {a.r * b.r, a.g * b.g, a.b * b.b}; }
Colour operator*(Colour a, double s) { return {a.r * s, a.g * s, a.b * s}; }
Colour operator/(Colour a, Colour b) { return {a.r / b.r, a.g / b.g, a.b / b.b}; }
Colour exp(Colour a) { return {std::exp(a.r), std::exp(a.g), std::exp(a.b)}; }
Colour colour(const Rgb &c) { return {c[0], c[1], c[2]}; }
Rgb rgb(const Colour &c) { return {float(c.r), float(c.g), float(c.b)}; }

struct Sample {
    Colour scattering;  // Rayleigh and Mie
    Colour rayleigh;
    double mie = 0.0;
    Colour extinction;
};

Sample sampleMedium(const Medium &m, double radius) {
    const double h = std::max(0.0, radius - BottomRadius);
    Sample s;
    const double rayleighDensity = std::exp(-h / m.rayleighHeight);
    const double mieDensity = std::exp(-h / m.mieHeight);
    const double ozoneDensity = std::max(0.0, 1.0 - std::abs(h - m.ozoneCentre) / m.ozoneHalfWidth);
    s.rayleigh = colour(m.rayleighScattering) * rayleighDensity;
    s.mie = m.mieScattering * mieDensity;
    s.scattering = s.rayleigh + Colour{s.mie, s.mie, s.mie};
    const double mieExtinction = m.mieExtinction * mieDensity;
    s.extinction = s.rayleigh + Colour{mieExtinction, mieExtinction, mieExtinction}
            + colour(m.ozoneAbsorption) * ozoneDensity;
    return s;
}

// Distance from p along a unit direction to a sphere around the planet's
// centre: the far hit from inside, the near one from outside; -1 if none.
double sphereDistance(Vec p, Vec d, double radius) {
    const double b = dot(p, d);
    const double c = dot(p, p) - radius * radius;
    const double discriminant = b * b - c;
    if (discriminant < 0.0)
        return -1.0;
    const double root = std::sqrt(discriminant);
    const double near = -b - root, far = -b + root;
    if (near >= 0.0)
        return near;
    return far >= 0.0 ? far : -1.0;
}

double rayleighPhase(double cosAngle) {
    return 3.0 / (16.0 * M_PI) * (1.0 + cosAngle * cosAngle);
}

// Cornette-Shanks, as Hillaire's reference.
double miePhase(double g, double cosAngle) {
    const double k = 3.0 / (8.0 * M_PI) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + cosAngle * cosAngle) / std::pow(1.0 + g * g - 2.0 * g * cosAngle, 1.5);
}

Colour bilinear(const std::vector<float> &table, int width, int height, double u, double v) {
    const double x = std::clamp(u * width - 0.5, 0.0, width - 1.0);
    const double y = std::clamp(v * height - 0.5, 0.0, height - 1.0);
    const int x0 = int(x), y0 = int(y);
    const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const double fx = x - x0, fy = y - y0;
    auto at = [&](int i, int j) {
        const float *p = &table[size_t(j * width + i) * 4];
        return Colour{p[0], p[1], p[2]};
    };
    return (at(x0, y0) * (1.0 - fx) + at(x1, y0) * fx) * (1.0 - fy)
            + (at(x0, y1) * (1.0 - fx) + at(x1, y1) * fx) * fy;
}

} // namespace

void transmittanceUv(double radius, double cosZenith, double &u, double &v) {
    const double h = std::sqrt(std::max(0.0, TopRadius * TopRadius - BottomRadius * BottomRadius));
    const double rho = std::sqrt(std::max(0.0, radius * radius - BottomRadius * BottomRadius));
    const double discriminant = radius * radius * (cosZenith * cosZenith - 1.0) + TopRadius * TopRadius;
    const double d = std::max(0.0, -radius * cosZenith + std::sqrt(std::max(0.0, discriminant)));
    const double dMin = TopRadius - radius;
    const double dMax = rho + h;
    u = (d - dMin) / (dMax - dMin);
    v = rho / h;
}

void transmittanceParameters(double u, double v, double &radius, double &cosZenith) {
    const double h = std::sqrt(TopRadius * TopRadius - BottomRadius * BottomRadius);
    const double rho = h * v;
    radius = std::sqrt(rho * rho + BottomRadius * BottomRadius);
    const double dMin = TopRadius - radius;
    const double dMax = rho + h;
    const double d = dMin + u * (dMax - dMin);
    cosZenith = d == 0.0 ? 1.0 : (h * h - rho * rho - d * d) / (2.0 * radius * d);
    cosZenith = std::clamp(cosZenith, -1.0, 1.0);
}

namespace {

} // namespace

namespace {

Colour tableTransmittance(const std::vector<float> &table, double radius, double cosZenith) {
    double u, v;
    transmittanceUv(radius, cosZenith, u, v);
    return bilinear(table, TransmittanceWidth, TransmittanceHeight, u, v);
}

Colour tableMultiple(const std::vector<float> &table, double radius, double sunCosZenith) {
    return bilinear(table, MultipleWidth, MultipleHeight, sunCosZenith * 0.5 + 0.5,
                    (radius - BottomRadius) / (TopRadius - BottomRadius));
}

} // namespace

Model::Model() {
    // Transmittance: optical depth to the top of the atmosphere.
    transmittance.resize(size_t(TransmittanceWidth) * TransmittanceHeight * 4);
    for (int j = 0; j < TransmittanceHeight; ++j) {
        for (int i = 0; i < TransmittanceWidth; ++i) {
            double radius, mu;
            transmittanceParameters((i + 0.5) / TransmittanceWidth, (j + 0.5) / TransmittanceHeight, radius, mu);
            const Vec p{0.0, radius, 0.0};
            const Vec d{std::sqrt(std::max(0.0, 1.0 - mu * mu)), mu, 0.0};
            const double distance = std::max(0.0, sphereDistance(p, d, TopRadius));
            constexpr int Steps = 40;
            Colour depth;
            for (int s = 0; s < Steps; ++s) {
                const double t = (s + 0.5) / Steps * distance;
                depth = depth + sampleMedium(m, length(p + d * t)).extinction * (distance / Steps);
            }
            const Colour t = exp(depth * -1.0);
            float *out = &transmittance[size_t(j * TransmittanceWidth + i) * 4];
            out[0] = float(t.r);
            out[1] = float(t.g);
            out[2] = float(t.b);
            out[3] = 1.0f;
        }
    }
    // Multiple scattering (Hillaire 2020, section 5.5): the light scattered
    // twice towards a point from a sphere of directions, its series summed
    // as a geometric one, isotropic.
    multiple.resize(size_t(MultipleWidth) * MultipleHeight * 4);
    constexpr int Directions = 8;
    constexpr int Steps = 20;
    const double isotropic = 1.0 / (4.0 * M_PI);
    for (int j = 0; j < MultipleHeight; ++j) {
        for (int i = 0; i < MultipleWidth; ++i) {
            const double sunCos = (i + 0.5) / MultipleWidth * 2.0 - 1.0;
            const double radius = BottomRadius + std::max(0.01, (j + 0.5) / MultipleHeight * (TopRadius - BottomRadius));
            const Vec p{0.0, radius, 0.0};
            const Vec sun{std::sqrt(std::max(0.0, 1.0 - sunCos * sunCos)), sunCos, 0.0};
            Colour luminance, transfer;
            for (int a = 0; a < Directions; ++a) {
                for (int b = 0; b < Directions; ++b) {
                    const double theta = 2.0 * M_PI * (a + 0.5) / Directions;
                    const double phi = std::acos(1.0 - 2.0 * (b + 0.5) / Directions);
                    const Vec d{std::cos(theta) * std::sin(phi), std::cos(phi), std::sin(theta) * std::sin(phi)};
                    const double ground = sphereDistance(p, d, BottomRadius);
                    const double distance = ground > 0.0 ? ground : std::max(0.0, sphereDistance(p, d, TopRadius));
                    Colour throughput{1.0, 1.0, 1.0};
                    const double dt = distance / Steps;
                    for (int s = 0; s < Steps; ++s) {
                        const Vec q = p + d * ((s + 0.3) * dt);
                        const double qr = length(q);
                        const Sample medium = sampleMedium(m, qr);
                        const Colour sampleT = exp(medium.extinction * -dt);
                        const double qSunCos = dot(q, sun) / qr;
                        const bool shadowed = sphereDistance(q, sun, BottomRadius) > 0.0;
                        const Colour sunT = shadowed ? Colour{} : tableTransmittance(transmittance, qr, qSunCos);
                        const Colour lit = sunT * medium.scattering * isotropic;
                        luminance = luminance + throughput * ((lit - lit * sampleT) / medium.extinction);
                        transfer = transfer + throughput * ((medium.scattering - medium.scattering * sampleT)
                                                            / medium.extinction);
                        throughput = throughput * sampleT;
                    }
                    if (ground > 0.0) {
                        const Vec q = p + d * ground;
                        const double qr = length(q);
                        const double qSunCos = dot(q, sun) / qr;
                        const Colour sunT = tableTransmittance(transmittance, qr, qSunCos);
                        luminance = luminance + throughput * sunT * colour(m.groundAlbedo)
                                * (std::max(0.0, qSunCos) / M_PI);
                    }
                }
            }
            const double share = 1.0 / (Directions * Directions);
            luminance = luminance * share;
            transfer = transfer * share;
            float *out = &multiple[size_t(j * MultipleWidth + i) * 4];
            for (int c = 0; c < 3; ++c)
                out[c] = float(luminance[c] / (1.0 - transfer[c]));
            out[3] = 1.0f;
        }
    }
}

Rgb Model::transmittanceToTop(double height, double cosZenith) const {
    return rgb(tableTransmittance(transmittance, BottomRadius + std::max(0.0, height), cosZenith));
}

Rgb Model::sunLight(double height, double sunCosZenith) const {
    const double radius = BottomRadius + std::max(0.0, height);
    const double sinHorizon = BottomRadius / radius;
    const double cosHorizon = -std::sqrt(std::max(0.0, 1.0 - sinHorizon * sinHorizon));
    const double sunSin = std::sin(SunRadiusDegrees * M_PI / 180.0);
    const double t = std::clamp((sunCosZenith - cosHorizon + sunSin) / (2.0 * sunSin), 0.0, 1.0);
    return rgb(tableTransmittance(transmittance, radius, sunCosZenith) * (t * t * (3.0 - 2.0 * t)));
}

Rgb Model::skyRadiance(double height, const float *direction, const float *sunDirection) const {
    const double radius = BottomRadius + std::max(0.001, height);
    const Vec p{0.0, radius, 0.0};
    Vec d{direction[0], direction[1], direction[2]};
    d = d * (1.0 / length(d));
    Vec sun{sunDirection[0], sunDirection[1], sunDirection[2]};
    sun = sun * (1.0 / length(sun));
    const double ground = sphereDistance(p, d, BottomRadius);
    const double distance = ground > 0.0 ? ground : sphereDistance(p, d, TopRadius);
    if (distance <= 0.0)
        return {0.0f, 0.0f, 0.0f};
    const double cosAngle = dot(d, sun);
    const double phaseR = rayleighPhase(cosAngle);
    const double phaseM = miePhase(m.mieG, cosAngle);
    constexpr int Steps = 32;
    const double dt = distance / Steps;
    Colour throughput{1.0, 1.0, 1.0}, luminance;
    for (int s = 0; s < Steps; ++s) {
        const Vec q = p + d * ((s + 0.3) * dt);
        const double qr = length(q);
        const Sample medium = sampleMedium(m, qr);
        const Colour sampleT = exp(medium.extinction * -dt);
        const double qSunCos = dot(q, sun) / qr;
        const bool shadowed = sphereDistance(q, sun, BottomRadius) > 0.0;
        const Colour sunT = shadowed ? Colour{} : tableTransmittance(transmittance, qr, qSunCos);
        const Colour single = sunT * (medium.rayleigh * phaseR + Colour{medium.mie, medium.mie, medium.mie} * phaseM);
        const Colour lit = single + tableMultiple(multiple, qr, qSunCos) * medium.scattering;
        luminance = luminance + throughput * ((lit - lit * sampleT) / medium.extinction);
        throughput = throughput * sampleT;
    }
    return rgb(luminance);
}

const Model &model() {
    static const Model shared;
    return shared;
}

Rgb toDisplay(const Rgb &radiance) {
    Rgb out;
    for (int c = 0; c < 3; ++c)
        out[c] = float(std::pow(1.0 - std::exp(-double(radiance[c]) * SkyExposure), 1.0 / 2.2));
    return out;
}

Rgb horizonColour(double height, const float *sunDirection) {
    constexpr int Directions = 8;
    Rgb sum{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < Directions; ++i) {
        const double a = 2.0 * M_PI * i / Directions;
        const double e = 2.0 * M_PI / 180.0;
        const float d[3] = {float(std::cos(e) * std::sin(a)), float(std::sin(e)), float(-std::cos(e) * std::cos(a))};
        const Rgb c = toDisplay(model().skyRadiance(height, d, sunDirection));
        for (int k = 0; k < 3; ++k)
            sum[k] += c[k] / Directions;
    }
    return sum;
}

Rgb zenithColour(double height, const float *sunDirection) {
    const float up[3] = {0.0f, 1.0f, 0.0f};
    return toDisplay(model().skyRadiance(height, up, sunDirection));
}

}
