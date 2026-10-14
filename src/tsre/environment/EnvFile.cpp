/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/environment/EnvFile.h>
#include <tsre/environment/WeatherPresets.h>
#include <tsre/fileFunctions/SimisTextReader.h>

#include <cmath>
#include <functional>

using namespace Env;

namespace {

using Handler = std::function<bool(const QString &name, const EnvNode &item)>;

// Whether the model reads an item: a block with contents. An empty block
// (world_sky_satellite_light ( ), a flag in some files) stays a token tree,
// so it is written back as it was.
bool modelled(const EnvNode &item) {
    return item.block && !item.items.isEmpty();
}

// Calls handle for each block of a node by its lower-case name; what it does
// not take goes to unknown, values included.
void each(const EnvNode &node, QVector<EnvNode> &unknown, const Handler &handle) {
    for (const EnvNode &item : node.items)
        if (!modelled(item) || !handle(item.text.toLower(), item))
            unknown.push_back(item);
}

// Like each, after the node's leading count value, returned (0 without).
int eachCounted(const EnvNode &node, QVector<EnvNode> &unknown, const Handler &handle) {
    int count = 0;
    bool first = true;
    for (const EnvNode &item : node.items) {
        double value;
        if (first && !item.block && node.numberAt(0, value)) {
            count = int(value);
            first = false;
            continue;
        }
        first = false;
        if (!modelled(item) || !handle(item.text.toLower(), item))
            unknown.push_back(item);
    }
    return count;
}

float number(const EnvNode &n, int index = 0, float fallback = 0.0f) {
    double v;
    return n.numberAt(index, v) ? float(v) : fallback;
}

int integer(const EnvNode &n, int index = 0, int fallback = 0) {
    double v;
    return n.numberAt(index, v) ? int(v) : fallback;
}

std::optional<quint32> colour(const EnvNode &n) {
    SimisTextReader::Token token;
    token.kind = SimisTextReader::Kind::Atom;
    token.text = n.values().value(0);
    quint32 v;
    if (!SimisTextReader::unsignedInteger(token, v, 16))
        return std::nullopt;
    return v;
}

EnvNode colourNode(const QString &name, quint32 value) {
    return EnvNode::blockOf(name, {EnvNode::value(QString("%1").arg(value, 8, 16, QChar('0')))});
}

EnvNode numberNode(const QString &name, float value) {
    return EnvNode::blockOf(name, {EnvNode::number(value)});
}

EnvNode integerNode(const QString &name, qint64 value) {
    return EnvNode::blockOf(name, {EnvNode::integer(value)});
}

// --- Reading ---

Shader readShader(const EnvNode &n) {
    Shader s;
    bool named = false;
    for (const EnvNode &item : n.items) {
        if (!item.block) {
            if (!named) {
                s.name = item.text;
                named = true;
            } else {
                s.unknown.push_back(item);
            }
            continue;
        }
        named = true;
        const QString name = item.text.toLower();
        if (name == "terrain_texslots") {
            QVector<EnvNode> rest;
            s.texSlotCount = eachCounted(item, rest, [&s](const QString &name, const EnvNode &slot) {
                if (name != "terrain_texslot")
                    return false;
                s.texSlots.push_back({slot.values().value(0), integer(slot, 1, 1), integer(slot, 2, 0)});
                return true;
            });
            if (!rest.isEmpty()) s.unknown.push_back(EnvNode::blockOf(item.text, rest));
        } else if (name == "terrain_uvcalcs") {
            QVector<EnvNode> rest;
            s.uvCalcCount = eachCounted(item, rest, [&s](const QString &name, const EnvNode &calc) {
                if (name != "terrain_uvcalc")
                    return false;
                s.uvCalcs.push_back({integer(calc, 0), integer(calc, 1), integer(calc, 2), integer(calc, 3)});
                return true;
            });
            if (!rest.isEmpty()) s.unknown.push_back(EnvNode::blockOf(item.text, rest));
        } else {
            s.unknown.push_back(item);
        }
    }
    return s;
}

AnimShader readAnimShader(const EnvNode &n) {
    AnimShader a;
    each(n, a.unknown, [&a](const QString &name, const EnvNode &item) {
        if (name == "world_anim_shader_framelen") {
            a.frameLength = number(item);
        } else if (name == "world_anim_shader_frames") {
            QVector<EnvNode> rest;
            a.frameCount = eachCounted(item, rest, [&a](const QString &name, const EnvNode &f) {
                if (name != "world_anim_shader_frame")
                    return false;
                AnimFrame frame;
                each(f, frame.unknown, [&frame](const QString &name, const EnvNode &v) {
                    if (name == "world_anim_shader_frame_uvscroll")
                        frame.uvScroll = std::array<float, 2>{number(v, 0), number(v, 1)};
                    else if (name == "world_anim_shader_frame_uvtiles")
                        frame.uvTiles = std::array<float, 2>{number(v, 0), number(v, 1)};
                    else if (name == "world_anim_shader_frame_uvstamp")
                        frame.uvStamp = std::array<float, 4>{number(v, 0), number(v, 1), number(v, 2), number(v, 3)};
                    else
                        return false;
                    return true;
                });
                a.frames.push_back(frame);
                return true;
            });
            for (const EnvNode &r : rest) a.unknown.push_back(r);
        } else if (name == "world_shader") {
            a.shader = readShader(item);
        } else {
            return false;
        }
        return true;
    });
    return a;
}

SkyLayer readSkyLayer(const EnvNode &n) {
    SkyLayer l;
    each(n, l.unknown, [&l](const QString &name, const EnvNode &item) {
        const QStringList v = item.values();
        if (name == "world_sky_layer_fadein") {
            l.fadeIn = std::array<QString, 2>{v.value(0), v.value(1)};
        } else if (name == "world_sky_layer_fadeout") {
            l.fadeOut = std::array<QString, 2>{v.value(0), v.value(1)};
        } else if (name == "world_sky_layer_top") {
            l.hasTop = true;
            each(item, l.topUnknown, [&l](const QString &name, const EnvNode &t) {
                if (name == "world_sky_layer_top_nfaces") l.topFaces = integer(t);
                else if (name == "world_sky_layer_top_radius") l.topRadius = number(t);
                else if (name == "world_sky_layer_top_height") l.topHeight = number(t);
                else return false;
                return true;
            });
        } else if (name == "world_sky_layer_edge") {
            l.hasEdge = true;
            each(item, l.edgeUnknown, [&l](const QString &name, const EnvNode &e) {
                if (name != "world_sky_layer_edge_steps")
                    return false;
                QVector<EnvNode> rest;
                // Steps alternate height and radius; a height starts a step.
                l.edgeStepCount = eachCounted(e, rest, [&l](const QString &name, const EnvNode &s) {
                    if (name == "world_sky_layer_edge_step_height") {
                        l.edgeSteps.push_back({number(s), 0.0f});
                    } else if (name == "world_sky_layer_edge_step_radius" && !l.edgeSteps.isEmpty()) {
                        l.edgeSteps.back()[1] = number(s);
                    } else {
                        return false;
                    }
                    return true;
                });
                for (const EnvNode &r : rest) l.edgeUnknown.push_back(r);
                return true;
            });
        } else if (name == "world_anim_shader") {
            l.shader = readAnimShader(item);
        } else {
            return false;
        }
        return true;
    });
    return l;
}

Satellite readSatellite(const EnvNode &n) {
    static const char *const colourNames[6] = {
        "world_sky_satellite_dir_rise_colour", "world_sky_satellite_dir_high_colour",
        "world_sky_satellite_dir_set_colour", "world_sky_satellite_amb_rise_colour",
        "world_sky_satellite_amb_high_colour", "world_sky_satellite_amb_set_colour"};
    Satellite s;
    each(n, s.unknown, [&s](const QString &name, const EnvNode &item) {
        for (int i = 0; i < 6; ++i)
            if (name == colourNames[i]) {
                s.colours[i] = colour(item);
                return bool(s.colours[i]);
            }
        if (name == "world_sky_satellite_low_scale") s.lowScale = number(item);
        else if (name == "world_sky_satellite_high_scale") s.highScale = number(item);
        else if (name == "world_sky_satellite_rise_position") s.risePosition = number(item);
        else if (name == "world_sky_satellite_rise_time") s.riseTime = item.values().value(0);
        else if (name == "world_sky_satellite_set_time") s.setTime = item.values().value(0);
        else if (name == "world_sky_satellite_light") s.light = integer(item);
        else if (name == "world_sky_satellite_fog") s.fog = number(item);
        else if (name == "world_anim_shader") s.shader = readAnimShader(item);
        else return false;
        return true;
    });
    return s;
}

Sky readSky(const EnvNode &n) {
    Sky sky;
    each(n, sky.unknown, [&sky](const QString &name, const EnvNode &item) {
        if (name == "world_sky_nlayers_behind_satellites") {
            sky.layersBehindSatellites = integer(item);
        } else if (name == "world_sky_layers") {
            sky.hasLayers = true;
            QVector<EnvNode> rest;
            sky.layerCount = eachCounted(item, rest, [&sky](const QString &name, const EnvNode &l) {
                if (name != "world_sky_layer")
                    return false;
                sky.layers.push_back(readSkyLayer(l));
                return true;
            });
            for (const EnvNode &r : rest) sky.unknown.push_back(r);
        } else if (name == "world_sky_horizon") {
            sky.hasHorizon = true;
            each(item, sky.horizonUnknown, [&sky](const QString &name, const EnvNode &h) {
                if (name != "world_shader" || sky.horizon)
                    return false;
                sky.horizon = readShader(h);
                return true;
            });
        } else if (name == "world_sky_satellites") {
            sky.hasSatellites = true;
            QVector<EnvNode> rest;
            sky.satelliteCount = eachCounted(item, rest, [&sky](const QString &name, const EnvNode &s) {
                if (name != "world_sky_satellite")
                    return false;
                sky.satellites.push_back(readSatellite(s));
                return true;
            });
            for (const EnvNode &r : rest) sky.unknown.push_back(r);
        } else {
            return false;
        }
        return true;
    });
    return sky;
}

Wind readWind(const EnvNode &n) {
    Wind wind;
    each(n, wind.unknown, [&wind](const QString &name, const EnvNode &item) {
        if (name != "world_wind_layers" || wind.hasLayers)
            return false;
        wind.hasLayers = true;
        QVector<EnvNode> rest;
        wind.layerCount = eachCounted(item, rest, [&wind](const QString &name, const EnvNode &l) {
            if (name != "world_wind_layer")
                return false;
            WindLayer layer;
            each(l, layer.unknown, [&layer](const QString &name, const EnvNode &v) {
                if (name == "world_wind_layer_maxheight") layer.maxHeight = number(v);
                else if (name == "world_wind_layer_direction")
                    layer.direction = std::array<float, 3>{number(v, 0), number(v, 1), number(v, 2)};
                else if (name == "world_wind_layer_speed") layer.speed = number(v);
                else if (name == "world_wind_layer_turbulencep") layer.turbulence = number(v);
                else return false;
                return true;
            });
            wind.layers.push_back(layer);
            return true;
        });
        for (const EnvNode &r : rest) wind.unknown.push_back(r);
        return true;
    });
    return wind;
}

Water readWater(const EnvNode &n) {
    Water water;
    each(n, water.unknown, [&water](const QString &name, const EnvNode &item) {
        if (name == "world_water_terrain_patch_map") {
            water.patchMapSize = integer(item);
            water.patchMapFile = item.values().value(1);
        } else if (name == "world_water_wave_height") {
            water.waveHeight = number(item);
        } else if (name == "world_water_wave_speed") {
            water.waveSpeed = number(item);
        } else if (name == "world_water_layers") {
            water.hasLayers = true;
            QVector<EnvNode> rest;
            water.layerCount = eachCounted(item, rest, [&water](const QString &name, const EnvNode &l) {
                if (name != "world_water_layer")
                    return false;
                WaterLayer layer;
                each(l, layer.unknown, [&layer](const QString &name, const EnvNode &v) {
                    if (name == "world_water_layer_height") layer.height = number(v);
                    else if (name == "world_water_layer_sky_reflection") layer.skyReflection = integer(v);
                    else if (name == "world_anim_shader") layer.shader = readAnimShader(v);
                    else return false;
                    return true;
                });
                water.layers.push_back(layer);
                return true;
            });
            for (const EnvNode &r : rest) water.unknown.push_back(r);
        } else {
            return false;
        }
        return true;
    });
    return water;
}

Precipitation readPrecipitation(const EnvNode &n) {
    Precipitation p;
    each(n, p.unknown, [&p](const QString &name, const EnvNode &item) {
        if (name == "world_precipitation_type") p.type = item.values().value(0);
        else if (name == "world_precipitation_density") p.density = number(item);
        else if (name == "world_precipitation_speed") p.speed = number(item);
        else return false;
        return true;
    });
    return p;
}

World readWorld(const EnvNode &n) {
    World w;
    each(n, w.unknown, [&w](const QString &name, const EnvNode &item) {
        if (name == "world_fog_distance") w.fogDistance = number(item);
        else if (name == "world_fog_day_colour") { w.fogDayColour = colour(item); return bool(w.fogDayColour); }
        else if (name == "world_fog_night_colour") { w.fogNightColour = colour(item); return bool(w.fogNightColour); }
        else if (name == "world_sky" && !w.sky) w.sky = readSky(item);
        else if (name == "world_water" && !w.water) w.water = readWater(item);
        else if (name == "world_wind" && !w.wind) w.wind = readWind(item);
        else if (name == "world_precipitation" && !w.precipitation) w.precipitation = readPrecipitation(item);
        else return false;
        return true;
    });
    return w;
}

// --- Writing ---

EnvNode shaderNode(const Shader &s) {
    EnvNode n = EnvNode::blockOf("world_shader");
    if (!s.name.isEmpty())
        n.items << EnvNode::value(s.name);
    if (s.texSlotCount >= 0) {
        EnvNode slotList = EnvNode::blockOf("terrain_texslots", {EnvNode::integer(s.texSlotCount)});
        for (const TexSlot &t : s.texSlots)
            slotList.items << EnvNode::blockOf("terrain_texslot", {EnvNode::value(t.file), EnvNode::integer(t.a),
                                                                EnvNode::integer(t.b)});
        n.items << slotList;
    }
    if (s.uvCalcCount >= 0) {
        EnvNode calcs = EnvNode::blockOf("terrain_uvcalcs", {EnvNode::integer(s.uvCalcCount)});
        for (const auto &c : s.uvCalcs)
            calcs.items << EnvNode::blockOf("terrain_uvcalc", {EnvNode::integer(c[0]), EnvNode::integer(c[1]),
                                                               EnvNode::integer(c[2]), EnvNode::integer(c[3])});
        n.items << calcs;
    }
    n.items += s.unknown;
    return n;
}

EnvNode animShaderNode(const AnimShader &a) {
    EnvNode n = EnvNode::blockOf("world_anim_shader");
    if (a.frameLength)
        n.items << numberNode("world_anim_shader_framelen", *a.frameLength);
    if (a.frameCount >= 0) {
        EnvNode frames = EnvNode::blockOf("world_anim_shader_frames", {EnvNode::integer(a.frameCount)});
        for (const AnimFrame &f : a.frames) {
            EnvNode frame = EnvNode::blockOf("world_anim_shader_frame");
            if (f.uvScroll)
                frame.items << EnvNode::blockOf("world_anim_shader_frame_uvscroll",
                                                {EnvNode::number((*f.uvScroll)[0]), EnvNode::number((*f.uvScroll)[1])});
            if (f.uvTiles)
                frame.items << EnvNode::blockOf("world_anim_shader_frame_uvtiles",
                                                {EnvNode::number((*f.uvTiles)[0]), EnvNode::number((*f.uvTiles)[1])});
            if (f.uvStamp)
                frame.items << EnvNode::blockOf("world_anim_shader_frame_uvstamp",
                                                {EnvNode::number((*f.uvStamp)[0]), EnvNode::number((*f.uvStamp)[1]),
                                                 EnvNode::number((*f.uvStamp)[2]), EnvNode::number((*f.uvStamp)[3])});
            frame.items += f.unknown;
            frames.items << frame;
        }
        n.items << frames;
    }
    if (a.shader)
        n.items << shaderNode(*a.shader);
    n.items += a.unknown;
    return n;
}

EnvNode skyLayerNode(const SkyLayer &l) {
    EnvNode n = EnvNode::blockOf("world_sky_layer");
    if (l.fadeIn)
        n.items << EnvNode::blockOf("world_sky_layer_fadein", {EnvNode::value((*l.fadeIn)[0]),
                                                                EnvNode::value((*l.fadeIn)[1])});
    if (l.fadeOut)
        n.items << EnvNode::blockOf("world_sky_layer_fadeout", {EnvNode::value((*l.fadeOut)[0]),
                                                                 EnvNode::value((*l.fadeOut)[1])});
    if (l.hasTop) {
        EnvNode top = EnvNode::blockOf("world_sky_layer_top",
                                       {integerNode("world_sky_layer_top_nfaces", l.topFaces),
                                        numberNode("world_sky_layer_top_radius", l.topRadius),
                                        numberNode("world_sky_layer_top_height", l.topHeight)});
        top.items += l.topUnknown;
        n.items << top;
    }
    if (l.hasEdge) {
        EnvNode edge = EnvNode::blockOf("world_sky_layer_edge");
        if (l.edgeStepCount >= 0) {
            EnvNode steps = EnvNode::blockOf("world_sky_layer_edge_steps", {EnvNode::integer(l.edgeStepCount)});
            for (const auto &s : l.edgeSteps)
                steps.items << numberNode("world_sky_layer_edge_step_height", s[0])
                            << numberNode("world_sky_layer_edge_step_radius", s[1]);
            edge.items << steps;
        }
        edge.items += l.edgeUnknown;
        n.items << edge;
    }
    if (l.shader)
        n.items << animShaderNode(*l.shader);
    n.items += l.unknown;
    return n;
}

EnvNode satelliteNode(const Satellite &s) {
    static const char *const colourNames[6] = {
        "world_sky_satellite_dir_rise_colour", "world_sky_satellite_dir_high_colour",
        "world_sky_satellite_dir_set_colour", "world_sky_satellite_amb_rise_colour",
        "world_sky_satellite_amb_high_colour", "world_sky_satellite_amb_set_colour"};
    EnvNode n = EnvNode::blockOf("world_sky_satellite");
    if (s.lowScale) n.items << numberNode("world_sky_satellite_low_scale", *s.lowScale);
    if (s.highScale) n.items << numberNode("world_sky_satellite_high_scale", *s.highScale);
    if (s.risePosition) n.items << numberNode("world_sky_satellite_rise_position", *s.risePosition);
    if (s.riseTime) n.items << EnvNode::blockOf("world_sky_satellite_rise_time", {EnvNode::value(*s.riseTime)});
    if (s.setTime) n.items << EnvNode::blockOf("world_sky_satellite_set_time", {EnvNode::value(*s.setTime)});
    for (int i = 0; i < 6; ++i)
        if (s.colours[i]) n.items << colourNode(colourNames[i], *s.colours[i]);
    if (s.light) n.items << integerNode("world_sky_satellite_light", *s.light);
    if (s.fog) n.items << numberNode("world_sky_satellite_fog", *s.fog);
    if (s.shader) n.items << animShaderNode(*s.shader);
    n.items += s.unknown;
    return n;
}

EnvNode skyNode(const Sky &sky) {
    EnvNode n = EnvNode::blockOf("world_sky");
    if (sky.layersBehindSatellites)
        n.items << integerNode("world_sky_nlayers_behind_satellites", *sky.layersBehindSatellites);
    if (sky.hasLayers) {
        EnvNode layers = EnvNode::blockOf("world_sky_layers", {EnvNode::integer(sky.layerCount)});
        for (const SkyLayer &l : sky.layers)
            layers.items << skyLayerNode(l);
        n.items << layers;
    }
    if (sky.hasHorizon) {
        EnvNode horizon = EnvNode::blockOf("world_sky_horizon");
        if (sky.horizon)
            horizon.items << shaderNode(*sky.horizon);
        horizon.items += sky.horizonUnknown;
        n.items << horizon;
    }
    if (sky.hasSatellites) {
        EnvNode satellites = EnvNode::blockOf("world_sky_satellites", {EnvNode::integer(sky.satelliteCount)});
        for (const Satellite &s : sky.satellites)
            satellites.items << satelliteNode(s);
        n.items << satellites;
    }
    n.items += sky.unknown;
    return n;
}

EnvNode windNode(const Wind &wind) {
    EnvNode n = EnvNode::blockOf("world_wind");
    if (wind.hasLayers) {
        EnvNode layers = EnvNode::blockOf("world_wind_layers", {EnvNode::integer(wind.layerCount)});
        for (const WindLayer &l : wind.layers) {
            EnvNode layer = EnvNode::blockOf("world_wind_layer");
            if (l.maxHeight) layer.items << numberNode("world_wind_layer_maxheight", *l.maxHeight);
            if (l.direction)
                layer.items << EnvNode::blockOf("world_wind_layer_direction",
                                                {EnvNode::number((*l.direction)[0]), EnvNode::number((*l.direction)[1]),
                                                 EnvNode::number((*l.direction)[2])});
            if (l.speed) layer.items << numberNode("world_wind_layer_speed", *l.speed);
            if (l.turbulence) layer.items << numberNode("world_wind_layer_turbulencep", *l.turbulence);
            layer.items += l.unknown;
            layers.items << layer;
        }
        n.items << layers;
    }
    n.items += wind.unknown;
    return n;
}

EnvNode waterNode(const Water &water) {
    EnvNode n = EnvNode::blockOf("world_water");
    if (water.patchMapSize || water.patchMapFile) {
        EnvNode map = EnvNode::blockOf("world_water_terrain_patch_map");
        if (water.patchMapSize) map.items << EnvNode::integer(*water.patchMapSize);
        if (water.patchMapFile) map.items << EnvNode::value(*water.patchMapFile);
        n.items << map;
    }
    if (water.waveHeight) n.items << numberNode("world_water_wave_height", *water.waveHeight);
    if (water.waveSpeed) n.items << numberNode("world_water_wave_speed", *water.waveSpeed);
    if (water.hasLayers) {
        EnvNode layers = EnvNode::blockOf("world_water_layers", {EnvNode::integer(water.layerCount)});
        for (const WaterLayer &l : water.layers) {
            EnvNode layer = EnvNode::blockOf("world_water_layer");
            if (l.height) layer.items << numberNode("world_water_layer_height", *l.height);
            if (l.skyReflection) layer.items << integerNode("world_water_layer_sky_reflection", *l.skyReflection);
            if (l.shader) layer.items << animShaderNode(*l.shader);
            layer.items += l.unknown;
            layers.items << layer;
        }
        n.items << layers;
    }
    n.items += water.unknown;
    return n;
}

EnvNode precipitationNode(const Precipitation &p) {
    EnvNode n = EnvNode::blockOf("world_precipitation");
    if (p.type) n.items << EnvNode::blockOf("world_precipitation_type", {EnvNode::value(*p.type)});
    if (p.density) n.items << numberNode("world_precipitation_density", *p.density);
    if (p.speed) n.items << numberNode("world_precipitation_speed", *p.speed);
    n.items += p.unknown;
    return n;
}

EnvNode worldNode(const World &w) {
    EnvNode n = EnvNode::blockOf("world");
    if (w.fogDistance) n.items << numberNode("world_fog_distance", *w.fogDistance);
    if (w.fogDayColour) n.items << colourNode("world_fog_day_colour", *w.fogDayColour);
    if (w.fogNightColour) n.items << colourNode("world_fog_night_colour", *w.fogNightColour);
    if (w.sky) n.items << skyNode(*w.sky);
    if (w.water) n.items << waterNode(*w.water);
    if (w.wind) n.items << windNode(*w.wind);
    if (w.precipitation) n.items << precipitationNode(*w.precipitation);
    n.items += w.unknown;
    return n;
}

} // namespace

void File::fromNodes(const QVector<EnvNode> &items) {
    world.reset();
    weather.reset();
    unknown.clear();
    warnings.clear();
    bool headerRead = false;
    for (const EnvNode &item : items) {
        if (!item.block && !headerRead && item.text.startsWith("SIMISA", Qt::CaseInsensitive)) {
            header = item.text;
            headerRead = true;
        } else if (item.block && item.text.compare("world", Qt::CaseInsensitive) == 0 && !world) {
            world = readWorld(item);
        } else if (item.block && item.text.compare("tsre_weather", Qt::CaseInsensitive) == 0 && !weather) {
            weather = Weather::fromNode(item, warnings);
        } else {
            unknown.push_back(item);
        }
    }
}

QVector<EnvNode> File::toNodes() const {
    QVector<EnvNode> items;
    items << EnvNode::value(header);
    if (world)
        items << worldNode(*world);
    if (weather)
        items << weather->toNode();
    items += unknown;
    return items;
}

bool File::parse(const QString &text, QString &error) {
    QVector<EnvNode> items;
    QStringList textWarnings;
    if (!EnvText::parse(text, items, error, &textWarnings))
        return false;
    fromNodes(items);
    warnings = textWarnings + warnings;
    return true;
}

bool File::read(const QString &path, QString &error) {
    QVector<EnvNode> items;
    QStringList textWarnings;
    if (!EnvText::read(path, items, error, &textWarnings))
        return false;
    fromNodes(items);
    warnings = textWarnings + warnings;
    return true;
}

bool File::save(const QString &path, QString &error) const {
    return EnvText::save(path, toNodes(), error);
}

Weather File::weatherOrDerived(SlotWeather slot) const {
    if (weather)
        return *weather;
    // The slot's weather, or the precipitation the file has.
    QString preset = QStringLiteral("fair_cumulus");
    if (world && world->precipitation && world->precipitation->type) {
        preset = world->precipitation->type->contains("snow", Qt::CaseInsensitive) ? "snow" : "rain";
    } else if (slot == SlotWeather::Rain) {
        preset = QStringLiteral("rain");
    } else if (slot == SlotWeather::Snow) {
        preset = QStringLiteral("snow");
    }
    Weather w = WeatherPresets::weather(preset);
    w.source = QStringLiteral("derived");
    // MSTS set its fog distances for a view of about 2 km: ten times that is
    // the visibility (2000 a fair 20 km, rain's 450 4.5 km, near the presets'
    // 40 and 6 km; read as it is, the env files' weather hid the far terrain
    // in haze; user, 2026-10-14, from a comparison on POLAND).
    constexpr float FogDistanceToVisibility = 10.0f;
    if (world && world->fogDistance && *world->fogDistance > 0.0f)
        for (WeatherKeyframe &k : w.keyframes)
            k.visibility = *world->fogDistance * FogDistanceToVisibility;
    // MSTS wind layers give a direction vector (x east, z north) and a speed.
    if (world && world->wind && !world->wind->layers.isEmpty()) {
        const WindLayer &l = world->wind->layers.front();
        if (l.direction && l.speed && *l.speed > 0.0f) {
            const double east = (*l.direction)[0], north = (*l.direction)[2];
            if (std::hypot(east, north) > 0.0) {
                double from = std::atan2(east, north) * 180.0 / M_PI + 180.0;
                from = std::fmod(from + 360.0, 360.0);
                for (WeatherKeyframe &k : w.keyframes)
                    k.wind[WeatherLayer::Low] = {float(from), *l.speed};
            }
        }
    }
    return w;
}
