/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "MaszynaLamps.h"
#include <tsre/maszyna/E3dFile.h>
#include <algorithm>
#include <cmath>

namespace MaszynaLamps {

namespace {

struct Known {
    const char *name;
    int end;
    quint32 bits;
    Lamp::Kind kind;
};

// MaSzyna's lamp parts (DynObj.cpp:2338-2370): 1x the front, 2x the rear;
// x1 upper, x2 right, x3 left.
const Known Names[] = {
    {"headlamp11", 1, HeadUpper, Lamp::Head},           {"headlamp12", 1, HeadRight, Lamp::Head},
    {"headlamp13", 1, HeadLeft, Lamp::Head},            {"headlamp21", 2, HeadUpper, Lamp::Head},
    {"headlamp22", 2, HeadRight, Lamp::Head},           {"headlamp23", 2, HeadLeft, Lamp::Head},
    {"highbeam12", 1, HighRight, Lamp::HighBeam},       {"highbeam13", 1, HighLeft, Lamp::HighBeam},
    {"highbeam22", 2, HighRight, Lamp::HighBeam},       {"highbeam23", 2, HighLeft, Lamp::HighBeam},
    {"endsignal12", 1, MarkerRight, Lamp::Marker},      {"endsignal13", 1, MarkerLeft, Lamp::Marker},
    {"endsignal22", 2, MarkerRight, Lamp::Marker},      {"endsignal23", 2, MarkerLeft, Lamp::Marker},
    {"endsignals1", 1, MarkerLeft | MarkerRight, Lamp::Marker},
    {"endsignals2", 2, MarkerLeft | MarkerRight, Lamp::Marker},
    {"endtab1", 1, EndSignals, Lamp::Plate},            {"endtab2", 2, EndSignals, Lamp::Plate},
};

// A head lamp's beam at the normal strength (m); others by the square root
// of their strength (TSRE's beam strength grows with the reach squared),
// within the light grid's reach.
constexpr float NormalReach = 150.0f;
constexpr float BeamAngle = 25.0f * float(M_PI) / 180.0f;
constexpr quint32 Red = 0xffff2010u;

// A high beam's side as its head lamp's bit.
quint32 headOf(quint32 highBits) {
    return ((highBits & HighLeft) ? HeadLeft : 0u) | ((highBits & HighRight) ? HeadRight : 0u);
}

// The part a lamp's place comes from: its lit part, else its dark one, else
// the lamp itself.
int partOf(const E3dModel &model, const QString &name) {
    for (const QString &candidate : {name + QStringLiteral("_on"), name + QStringLiteral("_off"), name}) {
        const auto found = model.byName.constFind(candidate);
        if (found != model.byName.cend())
            return found.value();
    }
    return -1;
}

// A part's centre in the model's space: its vertices' sphere through its
// world matrix (column major), else its origin.
void centreOf(const E3dModel::Part &part, float *out) {
    const float *c = part.boundRadius >= 0.0f ? part.boundCenter : nullptr;
    const float x = c ? c[0] : 0.0f, y = c ? c[1] : 0.0f, z = c ? c[2] : 0.0f;
    const float *m = part.world;
    for (int r = 0; r < 3; ++r)
        out[r] = m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r];
}

EngLights::Light light(EngLights::Type type, int index, quint32 colour, const float *position, float radius,
                       int end) {
    EngLights::Light l;
    l.index = index;
    l.type = type;
    EngLights::State s;
    s.colour = colour;
    std::copy(position, position + 3, s.position);
    s.radius = radius;
    // Out of its end: the front's forward, the rear's back.
    s.azimuth[1] = end == 1 ? 0.0f : 180.0f;
    l.states.push_back(s);
    return l;
}

}

Lamps find(const E3dModel &model, const int rgb[3], float dim, float normal, float high, float highDim) {
    Lamps out;
    if (rgb[0] >= 0 || rgb[1] >= 0 || rgb[2] >= 0) {
        const auto channel = [](int v) { return quint32(std::clamp(v < 0 ? 255 : v, 0, 255)); };
        out.colour = 0xff000000u | channel(rgb[0]) << 16 | channel(rgb[1]) << 8 | channel(rgb[2]);
    }
    if (dim > 0.0f)
        out.dim = dim;
    if (normal > 0.0f)
        out.normal = normal;
    if (high > 0.0f)
        out.high = high;
    if (highDim > 0.0f)
        out.highDim = highDim;
    for (const Known &k : Names) {
        const int part = partOf(model, QLatin1String(k.name));
        if (part < 0 || size_t(part) >= model.parts.size())
            continue;
        Lamp lamp;
        lamp.name = QLatin1String(k.name);
        lamp.end = k.end;
        lamp.bits = k.bits;
        lamp.kind = k.kind;
        float at[3];
        centreOf(model.parts[size_t(part)], at);
        // MaSzyna's x is the left, MSTS's (the car shape's) the right.
        lamp.position[0] = -at[0];
        lamp.position[1] = at[1];
        lamp.position[2] = at[2];
        if (k.kind == Lamp::Marker)
            out.markerLamps[k.end - 1] = true;
        out.lamps.push_back(lamp);
    }
    return out;
}

quint64 lit(const Lamps &lamps, const EngLights::Train &train, const Cab &cab) {
    const int lead = train.reversed ? 2 : 1, trail = 3 - lead;
    const bool on = train.headlight != EngLights::Train::HeadlightOff;
    // The sides with a high beam lamp at each end.
    quint32 highSides[2] = {0, 0};
    for (const Lamp &lamp : lamps.lamps)
        if (lamp.kind == Lamp::HighBeam)
            highSides[lamp.end - 1] |= headOf(lamp.bits);
    quint64 set = 0;
    bool heads = false;
    for (int i = 0; i < lamps.lamps.size() && i < 60; ++i) {
        const Lamp &lamp = lamps.lamps[i];
        // A high beam lamp is its side's head lamp's.
        const quint32 bits = lamp.kind == Lamp::HighBeam ? headOf(lamp.bits) : lamp.bits;
        bool l = false;
        if ((bits & cab.controlled) != 0) {
            l = (cab.ends[lamp.end - 1] & bits) != 0;
            if (lamp.kind == Lamp::HighBeam)
                l = l && cab.high;
            else if (lamp.kind == Lamp::Head && cab.high && (bits & highSides[lamp.end - 1]) != 0)
                l = false;
        } else {
            switch (lamp.kind) {
            case Lamp::Head:
                l = train.first && on && lamp.end == lead;
                break;
            case Lamp::HighBeam:
                break;
            case Lamp::Marker:
                l = train.last && lamp.end == trail;
                break;
            case Lamp::Plate:
                l = train.last && lamp.end == trail && !lamps.markerLamps[lamp.end - 1];
                break;
            }
        }
        if (l) {
            set |= quint64(1) << i;
            heads = heads || lamp.kind == Lamp::Head || lamp.kind == Lamp::HighBeam;
        }
    }
    const bool cabHeads = (cab.controlled & (HeadLeft | HeadUpper | HeadRight)) != 0;
    const bool dimmed = cabHeads ? cab.dim : train.headlight == EngLights::Train::HeadlightDim;
    if (heads && dimmed)
        set |= Dimmed;
    if (heads && cabHeads && cab.high)
        set |= HighBeams;
    return set;
}

std::shared_ptr<EngLights> lights(const Lamps &lamps, quint64 set, bool reversed) {
    auto out = std::make_shared<EngLights>();
    const bool dimmed = (set & Dimmed) != 0;
    const bool highBeam = (set & HighBeams) != 0;
    int index = 0;
    // A cone for each end with lit head lamps, the leading end's first.
    for (int end : {reversed ? 2 : 1, reversed ? 1 : 2}) {
        float sum[3] = {0.0f, 0.0f, 0.0f};
        int count = 0;
        for (int i = 0; i < lamps.lamps.size() && i < 60; ++i) {
            const Lamp &lamp = lamps.lamps[i];
            if (lamp.end != end || !isLit(set, i) || (lamp.kind != Lamp::Head && lamp.kind != Lamp::HighBeam))
                continue;
            for (int r = 0; r < 3; ++r)
                sum[r] += lamp.position[r];
            ++count;
        }
        if (count == 0)
            continue;
        const float strength = highBeam ? (dimmed ? lamps.highDim : lamps.high) : (dimmed ? lamps.dim : lamps.normal);
        const float reach = std::clamp(NormalReach * std::sqrt(strength), 20.0f, 250.0f);
        const float middle[3] = {sum[0] / count, sum[1] / count, sum[2] / count};
        EngLights::Light cone = light(EngLights::Cone, index++, lamps.colour, middle,
                                      2.0f * reach * std::sin(BeamAngle / 2.0f), end);
        cone.states[0].angleRad = BeamAngle;
        out->lights.push_back(cone);
    }
    // A glow at each lit lamp; plates are parts, not lamps.
    for (int i = 0; i < lamps.lamps.size() && i < 60; ++i) {
        const Lamp &lamp = lamps.lamps[i];
        if (!isLit(set, i) || lamp.kind == Lamp::Plate)
            continue;
        const bool marker = lamp.kind == Lamp::Marker;
        out->lights.push_back(light(EngLights::Glow, index++, marker ? Red : lamps.colour, lamp.position,
                                    marker ? 0.15f : 0.25f, lamp.end));
    }
    return out;
}

}
