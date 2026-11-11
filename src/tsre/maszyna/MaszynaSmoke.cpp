/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "MaszynaSmoke.h"
#include <tsre/maszyna/E3dFile.h>
#include <algorithm>

namespace MaszynaSmoke {

namespace {

// The presets, the default first. Vehicles' from MaSzyna's templates as the
// header says (default: 50 puffs a second, 0.3-0.8 m, 1.25-1.8 m/s, opacity
// 0.35-0.7, against TSRE's exhaust of 0.5 m³/s at 10 m/s, soot 0.4, from a
// 0.3 m nozzle); flow and speed are scales on the engine's, its temperature
// and steam the engine's own. Scenery's are first guesses at the gas they
// show, not checked yet (task shapes 18 draws them).
const Preset Presets[] = {
    // name, kind, nozzle, {flow m³/s, speed m/s, °C, steam, soot, beats, colour}
    {"default", "exhaust", 0.3f, {0.5f, 10.0f, 300.0f, 0.1f, 0.4f, 0.0f, 0xff3c4240u}, true},
    // 30 puffs, 0.6-1.5 m, 0.9-2.0 m/s, opacity 0.4-1.0: the ST44's heavy smoke.
    {"st44", "exhaust", 0.6f, {1.093f, 9.51f, 300.0f, 0.1f, 0.533f, 0.0f, 0xff232531u}, true},
    // 30 puffs, 0.2-0.6 m, 0.25-1.7 m/s, opacity 0.3-0.7.
    {"su45", "exhaust", 0.2f, {0.159f, 6.39f, 300.0f, 0.1f, 0.381f, 0.0f, 0xff32383bu}, true},
    {"tractor", "exhaust", 0.2f, {0.159f, 6.39f, 300.0f, 0.1f, 0.381f, 0.0f, 0xff32383bu}, true},
    // 15 puffs, 0.4-0.6 m, 1.25-1.35 m/s, opacity 0.05-0.1: barely seen.
    {"light", "exhaust", 0.4f, {0.124f, 8.52f, 300.0f, 0.1f, 0.057f, 0.0f, 0xff505050u}, true},
    // 40 puffs, 0.1-0.7 m, 0.2-1.0 m/s, opacity 0.2-0.5.
    {"medium", "exhaust", 0.1f, {0.212f, 3.93f, 300.0f, 0.1f, 0.267f, 0.0f, 0xff61635cu}, true},
    // 6 puffs, 0.2-0.4 m, no launch speed, opacity 0.1-1.9: a bus's soot.
    {"bus", "exhaust", 0.2f, {0.018f, 1.0f, 300.0f, 0.1f, 0.762f, 0.0f, 0xff0a0a0au}, true},
    // 10 puffs, 0.3-1.7 m, 1.25-1.7 m/s, opacity 0.1-0.7.
    {"st45", "exhaust", 0.3f, {0.331f, 9.67f, 300.0f, 0.1f, 0.305f, 0.0f, 0xff413e44u}, true},
    // Scenery: a chimney's white smoke, cooling towers' vapour (sizes 10-17
    // and 10-40 m), a bonfire.
    {"chimney", "exhaust", 0.6f, {0.3f, 3.0f, 120.0f, 0.4f, 0.05f, 0.0f, 0xfff0f0f0u}, false},
    {"vapor_coolingtower", "steam", 20.0f, {400.0f, 3.0f, 35.0f, 1.0f, 0.0f, 0.0f, 0xffffffffu}, false},
    {"vapor_coolingtower70", "steam", 30.0f, {1500.0f, 6.0f, 35.0f, 1.0f, 0.0f, 0.0f, 0xffffffffu}, false},
    {"ognisko", "exhaust", 1.0f, {0.1f, 1.0f, 300.0f, 0.2f, 0.15f, 0.0f, 0xff323232u}, false},
};

const QLatin1String Prefix("smokesource_");

}

const Preset &defaults() {
    return Presets[0];
}

const Preset &preset(QStringView name) {
    if (name.startsWith(Prefix, Qt::CaseInsensitive))
        name = name.mid(Prefix.size());
    for (const Preset &p : Presets)
        if (name.compare(QLatin1String(p.name), Qt::CaseInsensitive) == 0)
            return p;
    return defaults();
}

ExhaustOutput adapt(const ExhaustOutput &engine, const Preset &preset) {
    const ExhaustOutput &d = defaults().output;
    ExhaustOutput out = engine;
    out.flow = engine.flow * preset.output.flow / d.flow;
    out.speed = engine.speed * preset.output.speed / d.speed;
    out.soot = std::clamp(engine.soot * preset.output.soot / d.soot, 0.0f, 1.0f);
    if (out.color == 0)
        out.color = preset.output.color;
    return out;
}

std::vector<Source> find(const E3dModel &model) {
    std::vector<Source> out;
    for (const E3dModel::Part &part : model.parts) {
        if (part.name < 0 || part.name >= model.names.size())
            continue;
        const QString &name = model.names[part.name];
        if (!name.startsWith(Prefix, Qt::CaseInsensitive))
            continue;
        Source s;
        // Its origin through its parents (MaSzyna's offset); MaSzyna's x is
        // the left, the car shape's the right.
        s.position[0] = -part.world[12];
        s.position[1] = part.world[13];
        s.position[2] = part.world[14];
        s.preset = &preset(name);
        out.push_back(s);
    }
    return out;
}

}
