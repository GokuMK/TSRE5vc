/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "SunPosition.h"
#include <algorithm>
#include <cmath>

namespace SunPosition {

namespace {
constexpr double Pi = 3.14159265358979323846;
double radians(double degrees) { return degrees * Pi / 180.0; }
double degrees(double radians) { return radians * 180.0 / Pi; }
}

Result compute(double latitude, double longitude, const QDate &date, double utcHours) {
    Result result;
    // Julian centuries since J2000.0.
    const double julianDay = double(date.toJulianDay()) - 0.5 + utcHours / 24.0;
    const double t = (julianDay - 2451545.0) / 36525.0;
    // Geometric mean longitude and anomaly of the sun, orbit eccentricity.
    const double meanLongitude = std::fmod(280.46646 + t * (36000.76983 + t * 0.0003032), 360.0);
    const double meanAnomaly = 357.52911 + t * (35999.05029 - 0.0001537 * t);
    const double eccentricity = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
    const double m = radians(meanAnomaly);
    const double centre = std::sin(m) * (1.914602 - t * (0.004817 + 0.000014 * t))
            + std::sin(2.0 * m) * (0.019993 - 0.000101 * t) + std::sin(3.0 * m) * 0.000289;
    const double trueLongitude = meanLongitude + centre;
    const double omega = 125.04 - 1934.136 * t;
    const double apparentLongitude = trueLongitude - 0.00569 - 0.00478 * std::sin(radians(omega));
    const double meanObliquity = 23.0 + (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0;
    const double obliquity = meanObliquity + 0.00256 * std::cos(radians(omega));
    const double declination = std::asin(std::sin(radians(obliquity)) * std::sin(radians(apparentLongitude)));
    const double y = std::pow(std::tan(radians(obliquity) / 2.0), 2.0);
    const double l0 = radians(meanLongitude);
    const double equationOfTime = 4.0 * degrees(
                y * std::sin(2.0 * l0) - 2.0 * eccentricity * std::sin(m)
                + 4.0 * eccentricity * y * std::sin(m) * std::cos(2.0 * l0)
                - 0.5 * y * y * std::sin(4.0 * l0) - 1.25 * eccentricity * eccentricity * std::sin(2.0 * m));
    // True solar time and the sun's hour angle at the place.
    double solarMinutes = std::fmod(utcHours * 60.0 + equationOfTime + 4.0 * longitude, 1440.0);
    if (solarMinutes < 0.0)
        solarMinutes += 1440.0;
    const double hourAngle = radians(solarMinutes / 4.0 - 180.0);
    const double lat = radians(latitude);
    const double cosZenith = std::sin(lat) * std::sin(declination)
            + std::cos(lat) * std::cos(declination) * std::cos(hourAngle);
    const double zenith = std::acos(std::clamp(cosZenith, -1.0, 1.0));
    result.elevation = 90.0 - degrees(zenith);
    result.azimuth = std::fmod(degrees(std::atan2(std::sin(hourAngle),
                                                  std::cos(hourAngle) * std::sin(lat)
                                                  - std::tan(declination) * std::cos(lat))) + 180.0,
                               360.0);
    result.declination = degrees(declination);
    result.equationOfTimeMinutes = equationOfTime;
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

void direction(const Result &sun, float *out) {
    const double elevation = radians(sun.elevation);
    const double azimuth = radians(sun.azimuth);
    out[0] = float(std::sin(azimuth) * std::cos(elevation));
    out[1] = float(std::sin(elevation));
    out[2] = float(-std::cos(azimuth) * std::cos(elevation));
}

}
