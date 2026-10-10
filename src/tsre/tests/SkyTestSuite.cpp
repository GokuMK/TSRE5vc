/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tests/SkyTestSuite.h>

#include <tsre/environment/Atmosphere.h>

#include <QDebug>
#include <QString>
#include <cmath>

namespace {

struct Checker {
    int passed = 0;
    int failed = 0;
    bool verbose = false;
    void check(bool ok, const QString &name) {
        if (ok) {
            ++passed;
            if (verbose) qInfo().noquote() << "[tests:sky] PASS" << name;
        } else {
            ++failed;
            qWarning().noquote() << "[tests:sky] FAIL" << name;
        }
    }
};

QString text(const Atmosphere::Rgb &c) {
    return QString("%1 %2 %3").arg(c[0], 0, 'g', 4).arg(c[1], 0, 'g', 4).arg(c[2], 0, 'g', 4);
}

void direction(double elevationDegrees, double azimuthDegrees, float *out) {
    const double e = elevationDegrees * M_PI / 180.0, a = azimuthDegrees * M_PI / 180.0;
    out[0] = float(std::cos(e) * std::sin(a));
    out[1] = float(std::sin(e));
    out[2] = float(-std::cos(e) * std::cos(a));
}

}

int runSkySuite(bool verbose) {
    Checker c;
    c.verbose = verbose;
    const Atmosphere::Model &model = Atmosphere::model();

    // The table's coordinates turn back into its parameters.
    double u, v, radius, mu;
    Atmosphere::transmittanceUv(Atmosphere::BottomRadius + 1.5, 0.3, u, v);
    Atmosphere::transmittanceParameters(u, v, radius, mu);
    c.check(std::abs(radius - Atmosphere::BottomRadius - 1.5) < 1e-6 && std::abs(mu - 0.3) < 1e-6,
            QString("transmittance-uv (%1 %2)").arg(radius - Atmosphere::BottomRadius).arg(mu));

    // Straight up from sea level about 0.6-0.9 of the light gets through,
    // blue least; along the horizon much less, and redder.
    const Atmosphere::Rgb zenith = model.transmittanceToTop(0.0, 1.0);
    c.check(zenith[0] > 0.8f && zenith[0] < 0.95f && zenith[2] > 0.55f && zenith[2] < zenith[1]
                    && zenith[1] < zenith[0],
            "transmittance-zenith " + text(zenith));
    const Atmosphere::Rgb horizon = model.transmittanceToTop(0.0, 0.02);
    c.check(horizon[0] < zenith[0] * 0.6f && horizon[2] < horizon[0] * 0.5f, "transmittance-horizon " + text(horizon));
    // The sun gone under the horizon (with its disc) gives no light.
    const Atmosphere::Rgb set = model.sunLight(0.0, std::sin(-2.0 * M_PI / 180.0));
    c.check(set[0] == 0.0f && set[2] == 0.0f, "sun-set " + text(set));

    // Noon: a blue sky, brighter towards the horizon; at sunset the horizon
    // under the sun is red-orange and the sky across from it bluer; at night
    // there is no light.
    float sun[3], view[3];
    direction(50.0, 180.0, sun);
    direction(90.0, 0.0, view);
    const Atmosphere::Rgb noonZenith = model.skyRadiance(0.1, view, sun);
    direction(5.0, 0.0, view);
    const Atmosphere::Rgb noonHorizon = model.skyRadiance(0.1, view, sun);
    c.check(noonZenith[2] > noonZenith[1] && noonZenith[1] > noonZenith[0] && noonZenith[2] > 0.01f,
            "sky-noon-zenith " + text(noonZenith));
    c.check(noonHorizon[0] + noonHorizon[1] + noonHorizon[2] > noonZenith[0] + noonZenith[1] + noonZenith[2]
                    && noonHorizon[2] / noonHorizon[0] < noonZenith[2] / noonZenith[0],
            "sky-noon-horizon " + text(noonHorizon));
    direction(1.0, 180.0, sun);
    direction(3.0, 180.0, view);
    const Atmosphere::Rgb sunset = model.skyRadiance(0.1, view, sun);
    direction(3.0, 0.0, view);
    const Atmosphere::Rgb opposite = model.skyRadiance(0.1, view, sun);
    c.check(sunset[0] > sunset[2] && sunset[0] > opposite[0], "sky-sunset " + text(sunset) + " / " + text(opposite));
    direction(-20.0, 180.0, sun);
    direction(45.0, 0.0, view);
    const Atmosphere::Rgb night = model.skyRadiance(0.1, view, sun);
    c.check(night[0] + night[1] + night[2] < 1e-5f, "sky-night " + text(night));

    qInfo() << "[tests:sky] cases=" << (c.passed + c.failed) << "passed=" << c.passed << "failed=" << c.failed;
    return c.failed == 0 ? 0 : 1;
}
