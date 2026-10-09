/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#include <tsre/geo/MoonPosition.h>
#include <algorithm>
#include <cmath>

namespace MoonPosition {

namespace {
constexpr double Pi = 3.14159265358979323846;
double radians(double degrees) { return degrees * Pi / 180.0; }
double degrees(double radians) { return radians * 180.0 / Pi; }
double normalized(double degrees) {
    const double d = std::fmod(degrees, 360.0);
    return d < 0.0 ? d + 360.0 : d;
}
// Mean equatorial horizontal parallax of the moon (degrees).
constexpr double MeanParallax = 0.9507;
}

Result compute(double latitude, double longitude, const QDate &date, double utcHours) {
    Result result;
    const double julianDay = double(date.toJulianDay()) - 0.5 + utcHours / 24.0;
    const double t = (julianDay - 2451545.0) / 36525.0;
    // Mean elements (Meeus 47.1-47.5), degrees.
    const double lp = normalized(218.3164477 + 481267.88123421 * t);
    const double d = radians(normalized(297.8501921 + 445267.1114034 * t));
    const double m = radians(normalized(357.5291092 + 35999.0502909 * t));
    const double mp = radians(normalized(134.9633964 + 477198.8675055 * t));
    const double f = radians(normalized(93.2720950 + 483202.0175233 * t));
    // The largest periodic terms of longitude and latitude (table 47.A, 47.B).
    const double lambda = lp + 6.288774 * std::sin(mp) + 1.274027 * std::sin(2 * d - mp)
            + 0.658314 * std::sin(2 * d) + 0.213618 * std::sin(2 * mp) - 0.185116 * std::sin(m)
            - 0.114332 * std::sin(2 * f) + 0.058793 * std::sin(2 * d - 2 * mp)
            + 0.057066 * std::sin(2 * d - m - mp) + 0.053322 * std::sin(2 * d + mp)
            + 0.045758 * std::sin(2 * d - m) - 0.040923 * std::sin(m - mp) - 0.034720 * std::sin(d)
            - 0.030383 * std::sin(m + mp);
    const double beta = 5.128122 * std::sin(f) + 0.280602 * std::sin(mp + f)
            + 0.277693 * std::sin(mp - f) + 0.173237 * std::sin(2 * d - f)
            + 0.055413 * std::sin(2 * d - mp + f) + 0.046271 * std::sin(2 * d - mp - f)
            + 0.032573 * std::sin(2 * d + f) + 0.017198 * std::sin(2 * mp + f);
    result.eclipticLongitude = normalized(lambda);
    result.eclipticLatitude = beta;

    // Sun's apparent longitude (as SunPosition), for the phase.
    const double sunMean = 280.46646 + t * (36000.76983 + t * 0.0003032);
    const double centre = std::sin(m) * (1.914602 - t * (0.004817 + 0.000014 * t))
            + std::sin(2.0 * m) * (0.019993 - 0.000101 * t) + std::sin(3.0 * m) * 0.000289;
    const double sunLongitude = sunMean + centre;
    const double cosElongation = std::cos(radians(beta)) * std::cos(radians(lambda - sunLongitude));
    result.elongation = degrees(std::acos(std::clamp(cosElongation, -1.0, 1.0)));
    // Phase angle about 180 degrees minus the elongation (the sun is far
    // beyond the moon); the lit share follows from it.
    const double phaseAngle = radians(180.0 - result.elongation);
    result.illuminatedFraction = (1.0 + std::cos(phaseAngle)) / 2.0;

    // Equatorial coordinates, then the hour angle at the place.
    const double obliquity = radians(23.439291 - 0.0130042 * t);
    const double l = radians(lambda), b = radians(beta);
    const double rightAscension = std::atan2(std::sin(l) * std::cos(obliquity) - std::tan(b) * std::sin(obliquity),
                                             std::cos(l));
    const double declination = std::asin(std::sin(b) * std::cos(obliquity)
                                         + std::cos(b) * std::sin(obliquity) * std::sin(l));
    const double siderealTime = normalized(280.46061837 + 360.98564736629 * (julianDay - 2451545.0)
                                           + t * t * 0.000387933 + longitude);
    const double hourAngle = radians(siderealTime) - rightAscension;
    const double lat = radians(latitude);
    const double sinElevation = std::sin(lat) * std::sin(declination)
            + std::cos(lat) * std::cos(declination) * std::cos(hourAngle);
    const double elevation = std::asin(std::clamp(sinElevation, -1.0, 1.0));
    // Seen from the earth's surface the moon stands lower by its parallax.
    result.elevation = degrees(elevation) - MeanParallax * std::cos(elevation);
    result.azimuth = normalized(degrees(std::atan2(std::sin(hourAngle),
                                                   std::cos(hourAngle) * std::sin(lat)
                                                   - std::tan(declination) * std::cos(lat))) + 180.0);
    return result;
}

Result atSolarTime(double latitude, double longitude, const QDate &date, double solarHours) {
    double utc = solarHours - longitude / 15.0;
    QDate day = date;
    while (utc < 0.0) {
        utc += 24.0;
        day = day.addDays(-1);
    }
    while (utc >= 24.0) {
        utc -= 24.0;
        day = day.addDays(1);
    }
    return compute(latitude, longitude, day, utc);
}

void direction(const Result &moon, float *out) {
    const double elevation = radians(moon.elevation);
    const double azimuth = radians(moon.azimuth);
    out[0] = float(std::sin(azimuth) * std::cos(elevation));
    out[1] = float(std::sin(elevation));
    out[2] = float(-std::cos(azimuth) * std::cos(elevation));
}

}
