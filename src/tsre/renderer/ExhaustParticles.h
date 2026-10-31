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
// rise while warmer than the air, and thin out; steam shows while the
// mixed puff holds more water than its air can (the air's dew point), so
// it hangs longer on a cold damp day. Renderer-neutral: the QRhi pass draws
// them.
class ExhaustParticles {
public:
    // The air the puffs meet: the wind (m/s, scene axes: x east, y up,
    // z south), its temperature and dew point (°C).
    struct Air {
        float wind[3] = {0.0f, 0.0f, 0.0f};
        float temperature = 15.0f;
        float dewPoint = 8.0f;
    };
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
        // Where it lies in the drawing's noise (0-1, a period), so a puff
        // keeps its look from frame to frame; an emitter's puffs a little
        // apart, so the many overlapping in a plume keep its shapes.
        float seed;
    };

    static ExhaustParticles &instance();

    // Most particles at once; beyond, a new one replaces the oldest. 0 turns
    // the simulation off (and drops every particle).
    void setLimit(int particles);
    int limit() const { return maxParticles; }
    // Puffs an emitter gives out a second at most (an even flow; a steam
    // stack gives one a beat).
    void setRate(float perSecond) { rate = perSecond; }

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
    // its place last frame.
    void emitGas(const void *owner, int emitter, const float *position, const float *direction, float diameter,
                 const ExhaustOutput &output, float share, float wetness = 0.0f);
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
    void update(Particle &particle, float seconds) const;
    void shade(Particle &particle) const;

    std::vector<Particle> live;
    std::unordered_map<SourceKey, Source, SourceKeyHash> sources;
    Air currentAir;
    float frameSeconds = 0.0f;
    quint64 frame = 0;
    bool accepting = false;
    int maxParticles = 3000;
    float rate = 20.0f;
    quint64 emittedCount = 0;
    quint64 replacedCount = 0;
};

#endif
