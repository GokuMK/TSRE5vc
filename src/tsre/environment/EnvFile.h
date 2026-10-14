/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ENVFILE_H
#define ENVFILE_H

#include <tsre/environment/EnvNode.h>
#include <tsre/environment/Weather.h>

#include <QString>
#include <QStringList>
#include <QVector>
#include <array>
#include <optional>

// An MSTS environment (.env) file read completely (task environment 01): fog,
// sky layers, horizon, satellites, wind, water, precipitation, and TSRE's
// weather block. Every block keeps the tokens the model does not know
// (other tools' extensions, commented-out blocks such as _world_wind_layers)
// as token trees, in order after the known ones, so writing loses nothing.
// Optional fields are written only when they were read or set. Lists keep
// their declared count, which MSTS files do not always match.
namespace Env {

struct TexSlot {
    QString file;
    int a = 1;
    int b = 0;
};

struct Shader {
    QString name;
    int texSlotCount = -1;     // -1: no terrain_texslots block
    QVector<TexSlot> texSlots;
    int uvCalcCount = -1;
    QVector<std::array<int, 4>> uvCalcs;
    QVector<EnvNode> unknown;
};

struct AnimFrame {
    std::optional<std::array<float, 2>> uvScroll;
    std::optional<std::array<float, 2>> uvTiles;
    std::optional<std::array<float, 4>> uvStamp;
    QVector<EnvNode> unknown;
};

struct AnimShader {
    std::optional<float> frameLength;
    int frameCount = -1;       // -1: no world_anim_shader_frames block
    QVector<AnimFrame> frames;
    std::optional<Shader> shader;
    QVector<EnvNode> unknown;
};

struct SkyLayer {
    std::optional<std::array<QString, 2>> fadeIn;   // times, hh:mm:ss
    std::optional<std::array<QString, 2>> fadeOut;
    bool hasTop = false;
    int topFaces = 0;
    float topRadius = 0.0f;
    float topHeight = 0.0f;
    QVector<EnvNode> topUnknown;
    bool hasEdge = false;
    int edgeStepCount = -1;
    QVector<std::array<float, 2>> edgeSteps;        // height, radius
    QVector<EnvNode> edgeUnknown;
    std::optional<AnimShader> shader;
    QVector<EnvNode> unknown;
};

struct Satellite {
    std::optional<float> lowScale, highScale, risePosition;
    std::optional<QString> riseTime, setTime;
    // Colours ARGB: direct rise, high, set; ambient rise, high, set.
    std::array<std::optional<quint32>, 6> colours;
    std::optional<int> light;
    std::optional<float> fog;
    std::optional<AnimShader> shader;
    QVector<EnvNode> unknown;
};

struct Sky {
    std::optional<int> layersBehindSatellites;
    bool hasLayers = false;
    int layerCount = 0;
    QVector<SkyLayer> layers;
    bool hasHorizon = false;
    std::optional<Shader> horizon;
    QVector<EnvNode> horizonUnknown;
    bool hasSatellites = false;
    int satelliteCount = 0;
    QVector<Satellite> satellites;
    QVector<EnvNode> unknown;
};

struct WindLayer {
    std::optional<float> maxHeight;
    std::optional<std::array<float, 3>> direction;
    std::optional<float> speed;
    std::optional<float> turbulence;
    QVector<EnvNode> unknown;
};

struct Wind {
    bool hasLayers = false;
    int layerCount = 0;
    QVector<WindLayer> layers;
    QVector<EnvNode> unknown;
};

struct WaterLayer {
    std::optional<float> height;
    std::optional<int> skyReflection;
    std::optional<AnimShader> shader;
    QVector<EnvNode> unknown;
};

struct Water {
    std::optional<int> patchMapSize;
    std::optional<QString> patchMapFile;
    std::optional<float> waveHeight, waveSpeed;
    bool hasLayers = false;
    int layerCount = 0;
    QVector<WaterLayer> layers;
    QVector<EnvNode> unknown;
};

struct Precipitation {
    std::optional<QString> type;      // world_precipitation_type_rain, _snow
    std::optional<float> density, speed;
    QVector<EnvNode> unknown;
};

struct World {
    std::optional<float> fogDistance;
    std::optional<quint32> fogDayColour, fogNightColour;
    std::optional<Sky> sky;
    std::optional<Water> water;
    std::optional<Wind> wind;
    std::optional<Precipitation> precipitation;
    QVector<EnvNode> unknown;
};

// The weather kind of a TRK slot (SpringClear, SummerRain ...).
enum class SlotWeather { Unknown, Clear, Rain, Snow };

class File {
public:
    QString header = QStringLiteral("SIMISA@@@@@@@@@@JINX0w0t______");
    std::optional<World> world;
    std::optional<Weather> weather;
    QVector<EnvNode> unknown;          // other top-level items
    QStringList warnings;

    bool read(const QString &path, QString &error);
    bool parse(const QString &text, QString &error);
    bool save(const QString &path, QString &error) const;
    QVector<EnvNode> toNodes() const;
    void fromNodes(const QVector<EnvNode> &items);

    // The file's weather block, or a weather derived from its MSTS content:
    // the slot's weather and the precipitation pick a preset, the fog
    // distance gives the visibility, the first wind layer the low wind.
    Weather weatherOrDerived(SlotWeather slot = SlotWeather::Unknown) const;
};

}

#endif
