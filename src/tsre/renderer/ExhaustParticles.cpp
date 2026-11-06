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
// has cooled). A plume shows its puffs' sum: at 20 puffs a second (a stack:
// one a beat); the more and smaller the puffs (a steam stack's beat in
// several), the lower each one's share. (Lower for all, they lived ten times as many and cost 4 ms
// for faint, huge puffs on a winter's day.)
constexpr float MaxAge = 90.0f;
constexpr float MaxRadius = 40.0f;
constexpr float ThinOut = 0.005f;
constexpr float ThinOutRate = 20.0f;
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
// The air's turbulence near the ground: its eddies' velocity (m/s) about
// 0.25 in calm air, more by a share of the wind's speed; an eddy carries a
// puff for some seconds (more for a larger puff, larger eddies); a puff on
// its eddy takes in air faster (its radius by this share of the eddy's
// speed). The large eddies sway whole plumes (half that velocity).
constexpr float CalmTurbulence = 0.25f;
constexpr float WindTurbulence = 0.15f;
constexpr float EddyStir = 0.15f;
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

void ExhaustParticles::setLimit(Pool pool, int particles) {
    maxParticles[int(pool)] = std::max(0, particles);
    if (maxParticles[0] == 0 && maxParticles[1] == 0) {
        clear();
        return;
    }
    if (counts[int(pool)] > maxParticles[int(pool)])
        trim(pool, maxParticles[int(pool)]);
}

void ExhaustParticles::trim(Pool pool, int keep) {
    // The pool's youngest kept, the other pool's untouched.
    std::stable_sort(live.begin(), live.end(), [pool](const Particle &a, const Particle &b) {
        const bool ownA = a.pool == pool, ownB = b.pool == pool;
        return ownA != ownB ? ownB : (ownA && a.age < b.age);
    });
    const size_t others = live.size() - size_t(counts[int(pool)]);
    live.resize(others + size_t(std::max(0, keep)));
    counts[int(pool)] = std::max(0, keep);
}

void ExhaustParticles::clear() {
    live.clear();
    sources.clear();
    counts[0] = counts[1] = 0;
    accepting = false;
}

void ExhaustParticles::shift(float dx, float dz) {
    origin[0] += dx;
    origin[1] += dz;
    for (Particle &p : live) {
        p.position[0] += dx;
        p.position[2] += dz;
    }
    for (auto &entry : sources) {
        entry.second.previous[0] += dx;
        entry.second.previous[2] += dz;
    }
}

float ExhaustParticles::uniform() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return float(randomState >> 8) / 16777216.0f;
}

float ExhaustParticles::gaussian() {
    // The sum of four uniform numbers, scaled to a deviation of 1.
    return (uniform() + uniform() + uniform() + uniform() - 2.0f) * 1.7320508f;
}

void ExhaustParticles::gustAt(const float *position, float turbulence, float *out) const {
    // A few sines across the place, drifting with the wind and changing
    // slowly, wavelengths of some 30-60 m.
    const Air &a = currentAir;
    const float t = float(std::fmod(time, 3600.0));
    const float x = position[0] - origin[0] - a.wind[0] * t;
    const float z = position[2] - origin[1] - a.wind[2] * t;
    const float y = position[1];
    const float amount = 0.5f * turbulence;
    out[0] = amount * (std::sin(0.21f * x + 0.05f * y + 0.6f * t + 1.3f) + 0.6f * std::sin(0.13f * z - 0.45f * t + 4.1f));
    out[1] = 0.5f * amount * std::sin(0.17f * x + 0.15f * z + 0.5f * t + 2.0f);
    out[2] = amount * (std::sin(0.19f * z + 0.07f * y + 0.55f * t + 0.7f) + 0.6f * std::sin(0.11f * x + 0.4f * t + 5.3f));
}

void ExhaustParticles::beginFrame(float seconds, const Air &air) {
    ++frame;
    currentAir = air;
    frameSeconds = std::clamp(seconds, 0.0f, FrameCap);
    time += frameSeconds;
    accepting = maxParticles[0] > 0 || maxParticles[1] > 0;
    if (!accepting)
        return;
    for (size_t i = 0; i < live.size();) {
        Particle &p = live[i];
        update(p, frameSeconds);
        const bool gone = p.age > MaxAge || p.radius > MaxRadius
                || (p.age > ThinOutAge && 2.0f * p.radius * (p.steamDensity + p.smokeDensity) < p.thinOut);
        if (gone) {
            --counts[int(p.pool)];
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

void ExhaustParticles::update(Particle &p, float seconds) {
    const Air &a = currentAir;
    const float windSpeed = length(a.wind);
    const float turbulence = randomness * (CalmTurbulence + WindTurbulence * windSpeed);
    float gust[3] = {0.0f, 0.0f, 0.0f};
    if (turbulence > 0.0f)
        gustAt(p.position, turbulence, gust);
    float left = seconds;
    while (left > 0.0f) {
        float relative[3];
        for (int i = 0; i < 3; ++i)
            relative[i] = p.velocity[i] - a.wind[i];
        const float growth = Spread * length(relative) + (Stirring + WindStirring * windSpeed) * p.stir
                + EddyStir * length(p.drift);
        const float dt = std::min({left, StepSeconds, StepGrowth * p.radius / growth});
        left = dt < left ? left - dt : 0.0f;
        const float grown = p.radius + growth * dt;
        // The air taken in shares the puff's momentum through the air.
        const float kept = cube(p.radius / grown);
        p.radius = grown;
        // Warmer than its air, it rises: the mixture's buoyancy, less its
        // condensed water's weight (a cooled wet plume sinks).
        const float mixed = cube(p.startRadius / p.radius);
        const float temperature = a.temperature + (p.startTemperature - a.temperature) * mixed;
        const float buoyancy = Gravity * ((temperature - a.temperature) / (temperature + 273.15f) - p.liquid);
        for (int i = 0; i < 3; ++i)
            relative[i] *= kept;
        relative[1] += buoyancy * dt;
        // Its eddy: kicked at random, fading over the eddy's time (larger
        // for a larger puff), less up and down than across.
        if (turbulence > 0.0f) {
            const float lagrangian = 2.0f + p.radius;
            const float kick = turbulence * std::sqrt(2.0f * dt / lagrangian);
            for (int i = 0; i < 3; ++i)
                p.drift[i] += -p.drift[i] * dt / lagrangian + kick * gaussian() * (i == 1 ? 0.7f : 1.0f);
        }
        for (int i = 0; i < 3; ++i) {
            p.velocity[i] = a.wind[i] + relative[i];
            p.position[i] += (p.velocity[i] + p.drift[i] + gust[i]) * dt;
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
    p.liquid = liquid;
    p.smokeDensity = (1.0f - p.vapour) * (SmokeHaze + SootExtinction * p.soot) * mixed;
}

void ExhaustParticles::add(const Particle &particle) {
    ++emittedCount;
    const int pool = int(particle.pool);
    if (counts[pool] < maxParticles[pool]) {
        live.push_back(particle);
        ++counts[pool];
        return;
    }
    ++replacedCount;
    if (maxParticles[pool] <= 0)
        return;
    if (particle.pool == Pool::Trains) {
        // Full: the trains' oldest gives way.
        auto oldest = live.end();
        for (auto p = live.begin(); p != live.end(); ++p)
            if (p->pool == Pool::Trains && (oldest == live.end() || p->age > oldest->age))
                oldest = p;
        if (oldest != live.end())
            *oldest = particle;
        return;
    }
    // Full scenery: the nearest emitters keep their puffs. The farthest
    // emitter's (its oldest) gives way, unless this one's is farther.
    auto worst = live.end();
    for (auto p = live.begin(); p != live.end(); ++p)
        if (p->pool == Pool::Scenery
                && (worst == live.end() || p->sourceDistance > worst->sourceDistance
                    || (p->sourceDistance == worst->sourceDistance && p->age > worst->age)))
            worst = p;
    if (worst != live.end() && particle.sourceDistance <= worst->sourceDistance)
        *worst = particle;
}

void ExhaustParticles::emitGas(const void *owner, int emitter, const float *position, const float *direction,
                               float diameter, const ExhaustOutput &output, float share, float wetness, Pool pool,
                               float distance) {
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
    // Puffs a second sharing the gas (for each one's share of the plume);
    // an even flow's time from one puff to the next.
    float perSecond = rate;
    float interval = 0.0f;
    if (output.beats > 0.0f) {
        // A steam stack: each beat's gas in a few puffs leaving together
        // (one ball a beat drew a string of beads apart in the turbulence);
        // the beats slow, more of them.
        s.beatPhase += output.beats * frameSeconds;
        const int beats = int(s.beatPhase);
        s.beatPhase -= float(beats);
        const int split = std::clamp(int(std::ceil(rate / output.beats)), 1, 6);
        puffs = beats * split;
        volume = std::max(flow / (output.beats * float(split)), smallest);
        perSecond = output.beats * float(split);
    } else {
        volume = std::max(flow / std::max(rate, 1.0f), smallest);
        s.carried += flow * frameSeconds;
        puffs = int(s.carried / volume);
        s.carried -= float(puffs) * volume;
        interval = volume / flow;
    }
    if (puffs > MaxPuffsAFrame) {
        volume *= float(puffs) / float(MaxPuffsAFrame);
        puffs = MaxPuffsAFrame;
    }
    // An emitter's first puff somewhere in the noise, its next ones a step on.
    if (s.seed < 0.0f)
        s.seed = float((quint64(quintptr(owner)) * 2654435761ull + quint64(emitter) * 40503u) % 65536u) / 65536.0f;
    const float turbulence = randomness * (CalmTurbulence + WindTurbulence * length(currentAir.wind));
    for (int k = 0; k < puffs; ++k) {
        // Spread along the way the nozzle went this frame, the older ones
        // moved on for the time since they left.
        const float f = float(k + 1) / float(puffs);
        Particle p;
        p.pool = pool;
        p.sourceDistance = distance;
        // No two alike: its way (some 6°) and speed, its size (a stack's
        // beats unequal), its heat, how the air stirs it, the eddy it
        // leaves on.
        float way[3];
        for (int i = 0; i < 3; ++i)
            way[i] = direction[i] + 0.1f * randomness * gaussian();
        const float wayLength = length(way);
        for (float &w : way)
            w = wayLength > 1e-6f ? w / wayLength : 0.0f;
        const float speed = output.speed * std::max(0.0f, 1.0f + 0.15f * randomness * gaussian());
        const float sizeSpread = 0.5f;
        const float size = std::max(0.3f, 1.0f + sizeSpread * randomness * (2.0f * uniform() - 1.0f));
        for (int i = 0; i < 3; ++i) {
            p.position[i] = from[i] + (position[i] - from[i]) * f;
            p.velocity[i] = vehicle[i] + way[i] * speed;
            p.drift[i] = 0.5f * turbulence * gaussian();
        }
        p.radius = std::max(nozzle, std::cbrt(3.0f * volume * size / (4.0f * Pi)));
        p.startRadius = p.radius;
        p.startTemperature = currentAir.temperature + (output.temperature - currentAir.temperature)
                * (1.0f + 0.08f * randomness * (2.0f * uniform() - 1.0f));
        p.stir = 1.0f + 0.4f * randomness * (2.0f * uniform() - 1.0f);
        p.liquid = 0.0f;
        p.thinOut = ThinOut * std::min(1.0f, (output.beats > 0.0f ? output.beats : ThinOutRate) / std::max(perSecond, 1.0f));
        p.interval = interval;
        for (int i = 0; i < 3; ++i)
            p.trail[i] = interval * vehicle[i];
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
