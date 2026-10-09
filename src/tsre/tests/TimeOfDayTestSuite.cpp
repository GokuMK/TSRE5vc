#include "TimeOfDayTestSuite.h"

#include <QDate>
#include <QDebug>
#include <cmath>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/geo/SunPosition.h>
#include <tsre/world/Daylight.h>

int TsreTests::runTimeOfDaySuite(bool verbose) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) {
        if (ok) {
            ++passed;
            if (verbose) qInfo() << "[tests:time-of-day] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:time-of-day] FAIL" << name;
        }
    };
    auto near = [](double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; };

    // Solstice noon in Warsaw: the sun due south, 90 - latitude + 23.44 up.
    const QDate solstice(2026, 6, 21);
    SunPosition::Result sun = SunPosition::atSolarTime(52.23, 21.01, solstice, 12.0);
    check(near(sun.declination, 23.44, 0.05), "the June solstice declination is 23.44 degrees");
    check(near(sun.elevation, 90.0 - 52.23 + 23.44, 0.6) && near(sun.azimuth, 180.0, 1.5),
          "at mean solar noon the sun stands south, at its height for the latitude");
    // South of the equator the June noon sun stands north and low.
    sun = SunPosition::atSolarTime(-33.87, 151.21, solstice, 12.0);
    check(near(sun.elevation, 90.0 - 33.87 - 23.44, 0.6)
          && (sun.azimuth < 1.5 || sun.azimuth > 358.5),
          "in Sydney in June the noon sun stands north");
    // Equation of time: the sun runs late in February and early in November.
    check(near(SunPosition::compute(0, 0, QDate(2026, 2, 11), 12).equationOfTimeMinutes, -14.2, 0.4)
          && near(SunPosition::compute(0, 0, QDate(2026, 11, 3), 12).equationOfTimeMinutes, 16.4, 0.4),
          "the equation of time follows the year");
    // Morning and evening.
    const SunPosition::Result morning = SunPosition::atSolarTime(52.23, 21.01, solstice, 6.0);
    const SunPosition::Result evening = SunPosition::atSolarTime(52.23, 21.01, solstice, 18.0);
    const SunPosition::Result midnight = SunPosition::atSolarTime(52.23, 21.01, solstice, 0.0);
    check(morning.azimuth > 60.0 && morning.azimuth < 120.0 && evening.azimuth > 240.0
          && evening.azimuth < 300.0 && near(morning.elevation, evening.elevation, 1.0),
          "the sun rises in the east and sets in the west");
    check(midnight.elevation < 0.0 && near(midnight.elevation, -(90.0 - 52.23 - 23.44), 0.6),
          "at midnight the sun is below the northern horizon");
    // Solar time of a place east of Greenwich comes earlier in UTC.
    const SunPosition::Result utcNoon = SunPosition::compute(52.23, 21.01, solstice, 12.0 - 21.01 / 15.0);
    check(near(utcNoon.elevation, SunPosition::atSolarTime(52.23, 21.01, solstice, 12.0).elevation, 1e-9),
          "solar time is UTC shifted by the longitude");

    // World directions: x east, y up, z south.
    SunPosition::Result east;
    east.azimuth = 90.0;
    float direction[3];
    SunPosition::direction(east, direction);
    check(near(direction[0], 1.0, 1e-6) && near(direction[1], 0.0, 1e-6) && near(direction[2], 0.0, 1e-6),
          "an eastern sun lies along +x");
    SunPosition::Result north;
    north.azimuth = 0.0;
    north.elevation = 30.0;
    SunPosition::direction(north, direction);
    check(near(direction[2], -std::cos(M_PI / 6.0), 1e-6) && near(direction[1], 0.5, 1e-6),
          "a northern sun lies along -z, raised by its elevation");

    // MSTS tiles: z and tile z point north, x east, so TSRE's flipped z
    // points south.
    GeoMstsCoordinateConverter msts;
    auto place = [&](int tileX, int tileZ, double x, double z, LatitudeLongitudeCoordinate &out) {
        PreciseTileCoordinate tile(tileX, tileZ, 0.0, 0.0);
        tile.setWxyz(float(x), 0.0f, float(z));
        IghCoordinate internal;
        msts.ConvertToInternal(&tile, &internal);
        msts.ConvertToLatLon(&internal, &out);
    };
    LatitudeLongitudeCoordinate origin, south, eastward;
    place(-6000, 14000, 0.0, 0.0, origin);
    place(-6000, 14000, 0.0, 500.0, south);
    place(-6000, 14000, 500.0, 0.0, eastward);
    // The projection's grid north turns away from true north off its
    // central meridians.
    check(south.Latitude < origin.Latitude && eastward.Longitude > origin.Longitude,
          "TSRE's +z points about south and +x about east");

    // Daylight.
    const float sky[4] = {0.9f, 0.97f, 1.0f, 1.0f};
    const float fog[4] = {0.8f, 0.9f, 1.0f, 1.0f};
    Daylight::Light light = Daylight::forElevation(60.0, sky, fog);
    check(near(light.diffuse[0], 0.7, 1e-5) && near(light.ambient[1], 0.3, 1e-5)
          && near(light.sky[2], 1.0, 1e-5) && near(light.fog[0], 0.8, 1e-5) && light.sunUp,
          "a high sun gives the editor's usual light and the configured colours");
    light = Daylight::forElevation(2.0, sky, fog);
    check(light.diffuse[0] > light.diffuse[2] && light.diffuse[0] < 0.5f && light.sky[0] > light.sky[2],
          "a setting sun is warm and weaker, under a warm sky");
    light = Daylight::forElevation(-25.0, sky, fog);
    check(light.diffuse[0] == 0.0f && light.ambient[0] < 0.05f && light.sky[2] < 0.06f && !light.sunUp,
          "at night the sun is gone and the sky dark");
    check(light.localLights == 1.0f && near(Daylight::forElevation(60.0, sky, fog).localLights, Daylight::DayLocalLights, 1e-6),
          "lamps count fully at night and little by day");
    check(light.signalLights == 1.0f && near(Daylight::forElevation(60.0, sky, fog).signalLights, Daylight::DaySignalLights, 1e-6)
              && Daylight::forElevation(10.0, sky, fog).signalLights > Daylight::forElevation(10.0, sky, fog).localLights,
          "signal lights count fully at night and half by day");
    const float sunsetLamps = Daylight::forElevation(0.0, sky, fog).localLights;
    const float lowSunLamps = Daylight::forElevation(10.0, sky, fog).localLights;
    check(near(sunsetLamps, 1.0, 1e-6) && lowSunLamps > 0.1f && lowSunLamps < 0.3f,
          "lamps are full at sunset and fade in while the sun is low");
    light = Daylight::forElevation(-8.0, sky, fog);
    check(light.sky[2] > light.sky[0] && light.sky[2] > 0.15f && light.diffuse[0] == 0.0f,
          "twilight is blue");

    qInfo() << "[tests:time-of-day] cases=" << (passed + failed) << "passed=" << passed
            << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
