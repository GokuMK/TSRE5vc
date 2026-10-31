/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/renderer/ExhaustParticles.h>
#include <tsre/trains/CarExhaust.h>
#include <algorithm>
#include <cmath>

namespace {

constexpr float Gravity = 9.81f;
constexpr float Pi = 3.14159265358979f;
// Integration steps at most this long (s); a frame longer than the cap is
// taken as the cap (a stall, a jump).
constexpr float StepSeconds = 0.05f;
constexpr float FrameCap = 0.25f;
// A step grows a puff by this share of its radius at most (a fast jet takes
// shorter steps).
constexpr float StepGrowth = 0.1f;
// How fast a puff takes in the air about it: its radius grows by Spread
// times its speed through the air (a jet's half angle of some 7°), plus
// the air's own stirring, more in wind (m/s).
constexpr float Spread = 0.12f;
constexpr float Stirring = 0.04f;
constexpr float WindStirring = 0.03f;
// A puff is gone past this age (s) or size (m: spread smoke is the fog
// volume's, task 41 step 7), or once its optical depth through the centre
// is below ThinOut (past ThinOutAge: superheated steam shows only once it
// has cooled).
constexpr float MaxAge = 90.0f;
constexpr float MaxRadius = 40.0f;
constexpr float ThinOut = 0.005f;
constexpr float ThinOutAge = 0.5f;
// Wet steam's droplets evaporate in the mixed puff within some time (s): a
// stack's exhaust leaves the chimney as a core of wet steam before it mixes
// with the fire's hot gases, white at the lip.
constexpr float WetLife = 0.6f;
// Extinction (1/m) of the smoke at the nozzle: a haze without soot, 3 at
// soot 1 (a diesel's opacity meter reads 0.5-3 for visible smoke). Of the
// steam by its liquid water (kg a kg of the mixture): 1.2 kg/m³ of air,
// droplets of some 6 µm (3 LWC / (2 ρw r)).
constexpr float SmokeHaze = 0.1f;
constexpr float SootExtinction = 3.0f;
constexpr float SteamExtinction = 300.0f;
// A vehicle moved farther than this in a frame jumped (placed, the camera
// went to it): no speed from it.
constexpr float MaxVehicleSpeed = 150.0f;
// Puffs one emitter gives out in a frame at most; more are made larger.
constexpr int MaxPuffsAFrame = 16;
// Each puff's step through the drawing's noise from its emitter's last one.
constexpr float SeedStep = 0.05f;
constexpr double Pressure = 1013.25;

// Water vapour the air holds at a temperature (kg a kg; Magnus, over
// water), all of it at boiling and above.
float saturation(float celsius) {
    const double e = 6.112 * std::exp(17.62 * celsius / (243.12 + celsius));
    return e >= Pressure ? 1.0f : float(0.622 * e / (Pressure - 0.378 * e));
}

float length(const float *v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

float cube(float x) {
    return x * x * x;
}

}

ExhaustParticles &ExhaustParticles::instance() {
    static ExhaustParticles particles;
    return particles;
}

void ExhaustParticles::setLimit(int particles) {
    maxParticles = std::max(0, particles);
    if (maxParticles == 0) {
        clear();
        return;
    }
    if (int(live.size()) > maxParticles) {
        // The youngest kept.
        std::sort(live.begin(), live.end(), [](const Particle &a, const Particle &b) { return a.age < b.age; });
        live.resize(size_t(maxParticles));
    }
}

void ExhaustParticles::clear() {
    live.clear();
    sources.clear();
    accepting = false;
}

void ExhaustParticles::shift(float dx, float dz) {
    for (Particle &p : live) {
        p.position[0] += dx;
        p.position[2] += dz;
    }
    for (auto &entry : sources) {
        entry.second.previous[0] += dx;
        entry.second.previous[2] += dz;
    }
}

void ExhaustParticles::beginFrame(float seconds, const Air &air) {
    ++frame;
    currentAir = air;
    frameSeconds = std::clamp(seconds, 0.0f, FrameCap);
    accepting = maxParticles > 0;
    if (maxParticles <= 0)
        return;
    for (size_t i = 0; i < live.size();) {
        Particle &p = live[i];
        update(p, frameSeconds);
        const bool gone = p.age > MaxAge || p.radius > MaxRadius
                || (p.age > ThinOutAge && 2.0f * p.radius * (p.steamDensity + p.smokeDensity) < ThinOut);
        if (gone) {
            live[i] = live.back();
            live.pop_back();
        } else {
            ++i;
        }
    }
    // Emitters not seen for a while (their train gone, out of range).
    if ((frame & 255) == 0)
        for (auto entry = sources.begin(); entry != sources.end();)
            entry = frame - entry->second.frame > 600 ? sources.erase(entry) : std::next(entry);
}

void ExhaustParticles::update(Particle &p, float seconds) const {
    const Air &a = currentAir;
    const float windSpeed = length(a.wind);
    float left = seconds;
    while (left > 0.0f) {
        float relative[3];
        for (int i = 0; i < 3; ++i)
            relative[i] = p.velocity[i] - a.wind[i];
        const float growth = Spread * length(relative) + Stirring + WindStirring * windSpeed;
        const float dt = std::min({left, StepSeconds, StepGrowth * p.radius / growth});
        left = dt < left ? left - dt : 0.0f;
        const float grown = p.radius + growth * dt;
        // The air taken in shares the puff's momentum through the air.
        const float kept = cube(p.radius / grown);
        p.radius = grown;
        // Warmer than its air, it rises: the mixture's buoyancy.
        const float mixed = cube(p.startRadius / p.radius);
        const float temperature = a.temperature + (p.startTemperature - a.temperature) * mixed;
        const float buoyancy = Gravity * (temperature - a.temperature) / (temperature + 273.15f);
        for (int i = 0; i < 3; ++i)
            relative[i] *= kept;
        relative[1] += buoyancy * dt;
        for (int i = 0; i < 3; ++i) {
            p.velocity[i] = a.wind[i] + relative[i];
            p.position[i] += p.velocity[i] * dt;
        }
    }
    p.age += seconds;
    shade(p);
}

void ExhaustParticles::shade(Particle &p) const {
    const Air &a = currentAir;
    // The share of the puff that left the nozzle; the rest is air taken in.
    const float mixed = cube(p.startRadius / p.radius);
    const float temperature = a.temperature + (p.startTemperature - a.temperature) * mixed;
    // Its water: the nozzle's vapour and the air's (at its dew point); what
    // its temperature cannot hold shows as steam.
    const float water = mixed * p.vapour + (1.0f - mixed) * saturation(std::min(a.dewPoint, a.temperature));
    const float liquid = std::max({0.0f, water - saturation(temperature), p.wet * mixed * std::exp(-p.age / WetLife)});
    p.steamDensity = SteamExtinction * liquid;
    p.smokeDensity = (1.0f - p.vapour) * (SmokeHaze + SootExtinction * p.soot) * mixed;
}

void ExhaustParticles::add(const Particle &particle) {
    ++emittedCount;
    if (int(live.size()) < maxParticles) {
        live.push_back(particle);
        return;
    }
    // Full: the oldest gives way.
    auto oldest = std::max_element(live.begin(), live.end(),
                                   [](const Particle &a, const Particle &b) { return a.age < b.age; });
    *oldest = particle;
    ++replacedCount;
}

void ExhaustParticles::emitGas(const void *owner, int emitter, const float *position, const float *direction,
                               float diameter, const ExhaustOutput &output, float share, float wetness) {
    if (!accepting)
        return;
    Source &s = sources[SourceKey{owner, emitter}];
    if (s.frame == frame)
        return;
    float vehicle[3] = {0.0f, 0.0f, 0.0f};
    float from[3] = {position[0], position[1], position[2]};
    if (s.known && frameSeconds > 0.0f) {
        float moved[3];
        for (int i = 0; i < 3; ++i)
            moved[i] = position[i] - s.previous[i];
        if (length(moved) <= MaxVehicleSpeed * frameSeconds)
            for (int i = 0; i < 3; ++i) {
                vehicle[i] = moved[i] / frameSeconds;
                from[i] = s.previous[i];
            }
    }
    std::copy(position, position + 3, s.previous);
    s.frame = frame;
    s.known = true;

    const float flow = output.flow * share;
    if (flow <= 0.0f || frameSeconds <= 0.0f) {
        s.carried = 0.0f;
        return;
    }
    const float nozzle = std::max(diameter, 0.05f) * 0.5f;
    const float smallest = 4.0f / 3.0f * Pi * nozzle * nozzle * nozzle;
    int puffs = 0;
    float volume = 0.0f;
    if (output.beats > 0.0f) {
        // A steam stack: a puff a beat, the beat's gas.
        s.beatPhase += output.beats * frameSeconds;
        puffs = int(s.beatPhase);
        s.beatPhase -= float(puffs);
        volume = std::max(flow / output.beats, smallest);
    } else {
        volume = std::max(flow / std::max(rate, 1.0f), smallest);
        s.carried += flow * frameSeconds;
        puffs = int(s.carried / volume);
        s.carried -= float(puffs) * volume;
    }
    if (puffs > MaxPuffsAFrame) {
        volume *= float(puffs) / float(MaxPuffsAFrame);
        puffs = MaxPuffsAFrame;
    }
    const float startRadius = std::max(nozzle, std::cbrt(3.0f * volume / (4.0f * Pi)));
    // An emitter's first puff somewhere in the noise, its next ones a step on.
    if (s.seed < 0.0f)
        s.seed = float((quint64(quintptr(owner)) * 2654435761ull + quint64(emitter) * 40503u) % 65536u) / 65536.0f;
    for (int k = 0; k < puffs; ++k) {
        // Spread along the way the nozzle went this frame, the older ones
        // moved on for the time since they left.
        const float f = float(k + 1) / float(puffs);
        Particle p;
        for (int i = 0; i < 3; ++i) {
            p.position[i] = from[i] + (position[i] - from[i]) * f;
            p.velocity[i] = vehicle[i] + direction[i] * output.speed;
        }
        p.radius = startRadius;
        p.startRadius = startRadius;
        p.startTemperature = output.temperature;
        p.vapour = std::clamp(output.steam, 0.0f, 1.0f);
        p.soot = std::clamp(output.soot, 0.0f, 1.0f);
        p.wet = std::clamp(wetness, 0.0f, 1.0f) * p.vapour;
        p.age = 0.0f;
        p.color = output.color;
        s.seed = s.seed + SeedStep - std::floor(s.seed + SeedStep);
        p.seed = s.seed;
        update(p, (1.0f - f) * frameSeconds);
        add(p);
    }
}
