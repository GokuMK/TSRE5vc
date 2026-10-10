/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#ifndef MOONPOSITION_H
#define MOONPOSITION_H
#include <QDate>
// Where the moon stands and how much of it is lit, for a place and time:
// the main terms of Meeus' lunar series ("Astronomical Algorithms", chapter
// 47), good to a few tenths of a degree, with the moon's parallax at its
// mean distance and no atmospheric refraction. Enough to draw it on the sky.
namespace MoonPosition {
struct Result {
    // Degrees above the horizon (negative below it), as seen from the place.
    double elevation = 0.0;
    // Degrees clockwise from north.
    double azimuth = 0.0;
    // Geocentric ecliptic longitude and latitude in degrees.
    double eclipticLongitude = 0.0;
    double eclipticLatitude = 0.0;
    // Angle between the sun and the moon seen from the earth, in degrees:
    // 0 at new moon, 180 at full moon.
    double elongation = 0.0;
    // Share of the disc that is lit, 0 (new) to 1 (full).
    double illuminatedFraction = 0.0;
};
// latitude and longitude in degrees (east positive); utcHours since midnight
// UTC of date.
Result compute(double latitude, double longitude, const QDate &date, double utcHours);
// The same for a local mean solar time at the place, as SunPosition::atSolarTime.
Result atSolarTime(double latitude, double longitude, const QDate &date, double solarHours);
// The direction towards the moon in TSRE's world space: x east, y up, z south.
void direction(const Result &moon, float *out);
}
#endif
