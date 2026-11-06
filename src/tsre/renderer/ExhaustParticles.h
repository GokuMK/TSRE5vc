/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef EXHAUSTPARTICLES_H
#define EXHAUSTPARTICLES_H

#include <QtGlobal>
#include <unordered_map>
#include <vector>

struct ExhaustOutput;

// Smoke and steam after the nozzle (task renderer 41): particles in the
// scene's space (the camera's tile at the origin), each a puff of the gas a
// nozzle gave out. They leave at the nozzle's speed plus the vehicle's,
// mix with the air they meet (growing, slowing toward the wind, cooling),
// rise while warmer than the air (their condensed water weighing them
// down), and thin out; steam shows while the mixed puff holds more water
// than its air can (the air's dew point), so it hangs longer on a cold damp
// day. The air is turbulent: each puff wanders on its own eddy and the
// large eddies sway whole plumes, and no two puffs leave alike.
// Renderer-neutral: the QRhi pass draws them.
class ExhaustParticles {
public:
    // The air the puffs meet: the wind (m/s, scene axes: x east, y up,
    // z south), its temperature and dew point (°C).
    struct Air {
        float wind[3] = {0.0f, 0.0f, 0.0f};
        float temperature = 15.0f;
        float dewPoint = 8.0f;
    };
    // Whose particles: the trains' and the scenery's (task shapes 15), each
    // with a cap of its own, so a town's chimneys never take a train's
    // plume.
    enum class Pool : unsigned char { Trains = 0, Scenery = 1 };
    struct Particle {
        float position[3];
        float velocity[3];
        // Radius now and at the nozzle (m): the puff has taken in
        // (radius / start)^3 of its first volume of air.
        float radius;
        float startRadius;
        // At the nozzle: temperature (°C), the share of water vapour, soot;
        // the liquid water the steam carries out (kg a kg of the gas: wet
        // steam, white at once).
        float startTemperature;
        float vapour;
        float soot;
        float wet;
        float age;
        // What shows (worked out each step): the extinction (1/m) of its
        // steam (the liquid water) and of its smoke.
        float steamDensity;
        float smokeDensity;
        unsigned color;
        // Its own eddy's velocity (m/s, besides its motion through the air),
        // the water condensed in it (kg a kg), how much the air's stirring
        // grows it (about 1).
        float drift[3];
        float liquid;
        float stir;
        // Its optical depth through the centre below which it is gone (lower
        // as more puffs share its gas: a plume shows their sum).
        float thinOut;
        // An even flow's: the time to its emitter's next puff (s; 0 a
        // stack's beat) and that time times the nozzle's velocity then (m).
        // The next-younger puff lies interval x (nozzle's velocity - this
        // puff's velocity) away: the drawing fills the gap where it shows.
        float interval;
        float trail[3];
        // Where it lies in the drawing's noise (0-1, a period), so a puff
        // keeps its look from frame to frame; an emitter's puffs a little
        // apart, so the many overlapping in a plume keep its shapes.
        float seed;
        // Its pool, and how far its emitter was from the camera (m: a full
        // scenery pool keeps the nearest emitters' puffs).
        Pool pool;
        float sourceDistance;
    };

    static ExhaustParticles &instance();

    // The trains' most particles at once; beyond, a new one replaces their
    // oldest. 0 for both pools turns the simulation off (and drops every
    // particle).
    void setLimit(int particles) { setLimit(Pool::Trains, particles); }
    int limit() const { return limit(Pool::Trains); }
    // A pool's most particles at once. Beyond, the trains' oldest gives way;
    // the scenery's nearest emitters keep theirs: a new puff replaces the
    // puff of the farthest emitter, or is dropped if its own is farther.
    void setLimit(Pool pool, int particles);
    int limit(Pool pool) const { return maxParticles[int(pool)]; }
    int count(Pool pool) const { return counts[int(pool)]; }
    // Both pools' caps by the exhaust setting (core.rendering.exhaust): 0
    // none (the simulation off), 1 (Normal) the trains' 3,000 and the
    // scenery's 1,000, 2 (High) 10,000 and 3,000.
    void setQuality(int quality);
    // Puffs an emitter gives out a second at most (an even flow; a steam
    // stack gives one a beat).
    void setRate(float perSecond) { rate = perSecond; }
    // The air's turbulence and the puffs' differences (1; 0 none: every
    // puff alike on a smooth path, for checks of the physics).
    void setRandomness(float amount) { randomness = amount; }

    // A frame: the particles moved on by `seconds` in this air; emitGas() takes
    // the frame's emissions until endFrame().
    void beginFrame(float seconds, const Air &air);
    void endFrame() { accepting = false; }
    bool acceptsGas() const { return accepting; }
    // An emitter's gas this frame (once a frame each; owner and index name
    // it from frame to frame): its place and direction in the scene's space,
    // its nozzle's diameter (m), the kind's output and this emitter's share
    // of the kind's flow; the share of its steam leaving as droplets (wet
    // steam: a stack's exhaust, the cylinder cocks'; 0 dry: a safety
    // valve's, clear for a hand's breadth). The vehicle's speed comes from
    // its place last frame. Its pool, and its distance from the camera (m)
    // for the scenery's.
    void emitGas(const void *owner, int emitter, const float *position, const float *direction, float diameter,
                 const ExhaustOutput &output, float share, float wetness = 0.0f, Pool pool = Pool::Trains,
                 float distance = 0.0f);
    // The camera moved to another tile: places by dx, dz (m).
    void shift(float dx, float dz);
    void clear();

    const std::vector<Particle> &particles() const { return live; }
    const Air &air() const { return currentAir; }
    // Emitted since the start and dropped for the limit (tests, logs).
    quint64 emitted() const { return emittedCount; }
    quint64 replaced() const { return replacedCount; }

private:
    struct Source {
        float previous[3] = {0.0f, 0.0f, 0.0f};
        quint64 frame = 0;
        bool known = false;
        // Gas given out but not yet a puff (m³); a steam stack's beat phase.
        float carried = 0.0f;
        float beatPhase = 0.0f;
        // The last puff's place in the noise.
        float seed = -1.0f;
    };
    struct SourceKey {
        const void *owner;
        int emitter;
        bool operator==(const SourceKey &o) const { return owner == o.owner && emitter == o.emitter; }
    };
    struct SourceKeyHash {
        size_t operator()(const SourceKey &k) const {
            return std::hash<const void *>()(k.owner) * 31 + size_t(k.emitter);
        }
    };
    void add(const Particle &particle);
    // A pool down to `keep` particles, its youngest kept.
    void trim(Pool pool, int keep);
    void update(Particle &particle, float seconds);
    void shade(Particle &particle) const;
    // The large eddies' velocity at a place (m/s), shared by all puffs.
    void gustAt(const float *position, float turbulence, float *out) const;
    // Random numbers (xorshift, the same run after run): 0-1, and about
    // normal (mean 0, deviation 1).
    float uniform();
    float gaussian();

    std::vector<Particle> live;
    std::unordered_map<SourceKey, Source, SourceKeyHash> sources;
    Air currentAir;
    float frameSeconds = 0.0f;
    quint64 frame = 0;
    bool accepting = false;
    int maxParticles[2] = {3000, 1000};
    int counts[2] = {0, 0};
    float rate = 30.0f;
    float randomness = 1.0f;
    quint32 randomState = 0x9e3779b9u;
    // Seconds since the start, and how far the tiles moved the places (the
    // gusts' field stays put).
    double time = 0.0;
    float origin[2] = {0.0f, 0.0f};
    quint64 emittedCount = 0;
    quint64 replacedCount = 0;
};

#endif
