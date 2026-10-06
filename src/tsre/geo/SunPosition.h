/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SUNPOSITION_H
#define SUNPOSITION_H

#include <QDate>

// Where the sun stands for a place and time, from the NOAA solar position
// equations (accurate to about a hundredth of a degree for current dates;
// no atmospheric refraction).
namespace SunPosition {

struct Result {
    // Degrees above the horizon (negative below it).
    double elevation = 0.0;
    // Degrees clockwise from north.
    double azimuth = 0.0;
    // Declination and the equation of time, for checks.
    double declination = 0.0;
    double equationOfTimeMinutes = 0.0;
};

// latitude and longitude in degrees (east positive); utcHours since midnight
// UTC of date.
Result compute(double latitude, double longitude, const QDate &date, double utcHours);

// The same for a local mean solar time at the place: 12:00 is when the
// mean sun crosses its meridian (UTC = solar time - longitude / 15 h).
Result atSolarTime(double latitude, double longitude, const QDate &date, double solarHours);

// The direction towards the sun in TSRE's world space: x east, y up,
// z south (MSTS z points north and TSRE flips it).
void direction(const Result &sun, float *out);

}

#endif
