/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef WEATHERIMPORT_H
#define WEATHERIMPORT_H

#include <tsre/environment/Weather.h>

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QVector>
#include <functional>

// A weather from weather data (task environment 01, steps 3 and 3b):
// Open-Meteo's models sampled over the route's area, hour by hour, and turned
// into keyframes with per-cell maps. The regional models (1.5-2 km over
// France, the UK, central and northern Europe) show real cloud structures a
// few km across; ERA5 (25 km, from 1940) only each region's cloudiness. Past
// days come from the forecast archive (ERA5: the reanalysis archive), days
// from two days ago to 16 days ahead from the forecast service.
//
// The archive gives cloud cover low (to 2 km), mid (2-6 km) and high, rain,
// snowfall, temperature, dew point, wind at 100 m and the WMO weather code;
// the rest is derived: cloud types from coverage and the weather code (showers:
// cumulus, thunderstorms: cumulonimbus, steady rain or drizzle: stratus), the
// cloud base from the dew point spread (about 125 m a degree above the
// ground), visibility from fog codes and the precipitation, the mid and high
// winds from the 100 m wind.
//
// Positions are MSTS world metres, as in Weather.
namespace WeatherImport {

struct Model {
    QString id;            // Open-Meteo's model name
    QString title;
    double resolution = 0.0;  // m: the model's grid
    QString attribution;
    bool reanalysis = false;  // ERA5: the archive API, no forecasts
};
// The models, finest first.
QVector<Model> models();
Model model(const QString &id);
// The id of the automatic choice: the finest model covering the area,
// found by asking each, finest first.
constexpr const char *Automatic = "automatic";

struct Request {
    QString model = QStringLiteral("era5");
    // m between sample points; 0: the model's grid, but at least 4 km.
    double spacing = 0.0;
    QDate date;
    int startHour = 0;     // UTC
    int hours = 12;        // keyframes every hour from the start, hours + 1 of them
    // The area (MSTS metres), margin included.
    double west = 0.0, north = 0.0, east = 0.0, south = 0.0;
    int cellSize = 4096;   // the weather grid's cells: 2048 m times a power of two
};

// A sample point: where it is on the route and on the globe.
struct Sample {
    double east = 0.0, north = 0.0;
    double latitude = 0.0, longitude = 0.0;
};

// The points sampled: a regular grid over the area at the model's spacing
// (the request's columns are listed first, row by row from the north).
// toLatLon converts a route position; false where it cannot.
using ToLatLon = std::function<bool(double east, double north, double &latitude, double &longitude)>;
bool samples(const Request &request, const ToLatLon &toLatLon, QVector<Sample> &out, int &columns, int &rows,
             QString &error);

// The spacing used: the request's, or the model's grid (at least 4 km).
double spacing(const Request &request);
// The request for samples [first, first + count) (at most 200 a request,
// keeping URLs short).
QString url(const Request &request, const QVector<Sample> &samples, int first = 0, int count = -1);
// Whether the request's answers can be cached: days fully in the past.
bool cacheable(const Request &request);
// The finest model with data at the area's centre and corners; error when
// none (or no network).
bool chooseModel(const Request &request, const ToLatLon &toLatLon, QString &model, QString &error,
                 const std::function<bool()> &cancelled = {});

// One sample's hourly values from the response (from the request's first day,
// 00:00 UTC).
struct Series {
    double elevation = 0.0;
    QVector<double> low, mid, high;          // %
    QVector<double> precipitation, snowfall; // mm, cm
    QVector<double> temperature, dewPoint;   // degrees C
    QVector<double> windSpeed, windDirection; // m/s at 100 m, degrees from (true north)
    QVector<int> weatherCode;
};
bool parse(const QByteArray &json, int sampleCount, QVector<Series> &out, QString &error);
// Every sample's series, in requests of up to 200 points.
bool download(const Request &request, const QVector<Sample> &samples, QVector<Series> &out, QString &error,
              const std::function<bool()> &cancelled = {});

// The weather: an absolute weather from the request's start, keyframes every
// hour, maps on a grid covering the area. gridBearing: the true bearing of
// the route grid's north (degrees), for the wind directions.
bool build(const Request &request, const QVector<Sample> &samples, int columns, int rows,
           const QVector<Series> &series, double gridBearing, Weather &weather, QString &error);

// Downloads a URL, from the disk cache when it was fetched before (the
// archive does not change). cancelled is polled while waiting.
bool fetch(const QString &url, QByteArray &out, QString &error, const std::function<bool()> &cancelled = {},
           bool cache = true);
}

#endif
