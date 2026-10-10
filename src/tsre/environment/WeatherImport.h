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

// A weather from historical data (task environment 01, step 3): Open-Meteo's
// archive (ERA5, ECMWF IFS) sampled over the route's area, hour by hour, and
// turned into keyframes with per-cell maps.
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
    double spacing = 0.0;  // m between samples: about the model's grid
    QDate first;           // the first day it covers
    QString attribution;
};
QVector<Model> models();
Model model(const QString &id);

struct Request {
    QString model = QStringLiteral("era5");
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

QString url(const Request &request, const QVector<Sample> &samples);

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

// The weather: an absolute weather from the request's start, keyframes every
// hour, maps on a grid covering the area. gridBearing: the true bearing of
// the route grid's north (degrees), for the wind directions.
bool build(const Request &request, const QVector<Sample> &samples, int columns, int rows,
           const QVector<Series> &series, double gridBearing, Weather &weather, QString &error);

// Downloads a URL, from the disk cache when it was fetched before (the
// archive does not change). cancelled is polled while waiting.
bool fetch(const QString &url, QByteArray &out, QString &error, const std::function<bool()> &cancelled = {});
}

#endif
