/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef WEATHER_H
#define WEATHER_H

#include <tsre/environment/EnvNode.h>

#include <QDateTime>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>
#include <array>

// The weather of an env file (task environment 01): cloud layers, wind,
// precipitation and visibility over time, read from and written to its
// tsre_weather block.
//
// Positions are MSTS world metres: east = tileX * 2048 + x, north =
// tileZ * 2048 + z, with x and z a tile's local coordinates (-1024 to 1024
// about its centre; z points north as in MSTS files). Times are hours from
// the weather's start. Wind directions are degrees the wind comes from,
// clockwise from the route grid's north; speeds m/s.
namespace WeatherLayer {
enum { Low = 0, Mid, High, Count };
QString name(int layer);
int fromName(const QString &name);
}

enum class WeatherPrecipitation { None, Rain, Snow };

struct WeatherLayerValues {
    // Cloud type 0-1: stratus 0, stratocumulus 0.33, cumulus 0.67,
    // cumulonimbus 1 (low); altostratus 0, altocumulus 0.6 (mid);
    // cirrostratus 0, cirrus 0.5, cirrocumulus 1 (high).
    float type = 0.67f;
    float coverage = 0.0f;   // 0-1
    float base = 1000.0f;    // m above sea level
    float top = 2000.0f;
    float density = 1.0f;
};

struct WeatherWind {
    float direction = 270.0f;
    float speed = 0.0f;
};

struct WeatherKeyframe {
    float time = 0.0f;
    std::array<WeatherWind, WeatherLayer::Count> wind;
    WeatherPrecipitation precipitation = WeatherPrecipitation::None;
    float precipitationRate = 0.0f;  // mm/h
    float visibility = 30000.0f;     // m
    // Coverage varied by large-scale noise: amount (0-1) and scale (m).
    float variationAmount = 0.2f;
    float variationScale = 30000.0f;
    std::array<WeatherLayerValues, WeatherLayer::Count> layers;
    QVector<EnvNode> unknown;
};

// Per-cell maps cover the grid: its north-west corner on a tile corner, its
// cells 2048 m times a power of two.
struct WeatherGrid {
    bool valid = false;
    int tileX = 0;
    int tileZ = 0;
    int cellSize = 2048;
    int columns = 0;
    int rows = 0;
    double west() const { return tileX * 2048.0 - 1024.0; }
    double north() const { return tileZ * 2048.0 + 1024.0; }
};

struct WeatherSurface {
    WeatherPrecipitation precipitation = WeatherPrecipitation::None;
    float precipitationRate = 0.0f;
    float snowShare = 0.0f;
    float visibility = 30000.0f;
};

class Weather {
public:
    int version = 1;
    QString preset;
    quint32 seed = 1;
    QString source = QStringLiteral("preset");
    QString attribution;
    QDateTime start;            // invalid: a relative weather, looping
    WeatherGrid grid;
    QString maps;               // directory of the per-cell maps, relative to the env file
    QVector<WeatherKeyframe> keyframes;
    QVector<EnvNode> unknown;

    static QStringList typeNames(int layer);
    static float typeValue(const QString &name, bool *ok = nullptr);

    // The block's contents; warnings for values it could not use.
    static Weather fromNode(const EnvNode &block, QStringList &warnings);
    EnvNode toNode() const;

    bool relative() const { return !start.isValid(); }
    // The time of the last keyframe; a relative weather loops after it.
    double duration() const;

    // The keyframes around a time and the blend between them (0: a, 1: b).
    struct Blend {
        int a = 0;
        int b = 0;
        double weight = 0.0;
    };
    Blend blend(double hours) const;
    WeatherWind wind(double hours, int layer) const;
    // Where the wind blows towards, m/s.
    static void velocity(const WeatherWind &wind, double &east, double &north);
    // How far a layer has moved since the start, m.
    void drift(double hours, int layer, double &east, double &north) const;

    // A layer's values at a place and time: keyframes blended with each
    // field moved by the wind (advection-corrected), per-cell maps where
    // given, and the variation noise.
    WeatherLayerValues layer(double hours, double east, double north, int layer) const;
    WeatherSurface surface(double hours, double east, double north) const;

    // Per-cell maps (16-bit RGBA PNG, keyframes stacked as rows of cells):
    // <layer>.png: coverage, type, base, top; surface.png: precipitation
    // (0.01 mm/h), snow share, visibility (m / 2).
    bool loadMaps(const QString &envDirectory, QString &error);
    bool saveMaps(const QString &envDirectory, QString &error) const;
    bool hasMaps() const { return !mapImages[0].isNull(); }
    // Map access for imports and tests; images are columns x rows x keyframes.
    QImage &mapImage(int index) { return mapImages[index]; }
    const QImage &mapImage(int index) const { return mapImages[index]; }
    static constexpr int SurfaceMap = WeatherLayer::Count;
    static QString mapFileName(int index);
    void allocateMaps();

    // The large-scale variation noise, -1 to 1.
    double variation(double east, double north, double scale) const;

private:
    std::array<QImage, WeatherLayer::Count + 1> mapImages;
    WeatherLayerValues keyframeLayer(int keyframe, double east, double north, int layer) const;
    WeatherSurface keyframeSurface(int keyframe, double east, double north) const;
    bool cell(double east, double north, double &column, double &row) const;
    std::array<double, 4> sampleMap(int index, int keyframe, double column, double row) const;
};

#endif
