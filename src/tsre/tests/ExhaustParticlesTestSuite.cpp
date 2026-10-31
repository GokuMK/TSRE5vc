#include <tsre/tests/ExhaustParticlesTestSuite.h>
#include "TokenTestSupport.h"
#include <tsre/renderer/ExhaustParticles.h>
#include <tsre/trains/CarExhaust.h>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {

using Air = ExhaustParticles::Air;
using Particle = ExhaustParticles::Particle;

constexpr float Frame = 1.0f / 60.0f;
const float Up[3] = {0.0f, 1.0f, 0.0f};

// Task 05's values: a diesel at idle and at full power, a steam stack (its
// beats), cylinder cocks, a safety valve.
const ExhaustOutput Idle{0.83f, 3.0f, 150.0f, 0.05f, 0.1f, 0.0f, 0};
const ExhaustOutput Full{11.3f, 40.0f, 400.0f, 0.05f, 0.25f, 0.0f, 0};
const ExhaustOutput Stack{2.0f, 30.0f, 150.0f, 0.8f, 0.3f, 4.0f, 0};
const ExhaustOutput Cocks{0.3f, 40.0f, 120.0f, 1.0f, 0.0f, 0.0f, 0};
const ExhaustOutput Valve{1.0f, 100.0f, 200.0f, 1.0f, 0.0f, 0.0f, 0};

Air air(float east, float temperature, float dewPoint) {
    Air a;
    a.wind[0] = east;
    a.temperature = temperature;
    a.dewPoint = dewPoint;
    return a;
}

// Frames of the simulation; each emits as `gas` says (its frame's index).
void run(ExhaustParticles &sim, const Air &a, int frames, const std::function<void(int)> &gas) {
    for (int f = 0; f < frames; ++f) {
        sim.beginFrame(Frame, a);
        gas(f);
        sim.endFrame();
    }
}

// One emitter standing at (0, 4, 0), pointing up, a 0.3 m nozzle.
void standing(ExhaustParticles &sim, const Air &a, float seconds, const ExhaustOutput &out) {
    static int owner;
    const float at[3] = {0.0f, 4.0f, 0.0f};
    run(sim, a, int(std::lround(seconds / Frame)), [&](int) { sim.emitGas(&owner, 0, at, Up, 0.3f, out, 1.0f); });
}

// The oldest and youngest particle (with none, a particle of zeros).
const Particle &oldest(const ExhaustParticles &sim) {
    static const Particle none{};
    const auto p = std::max_element(sim.particles().begin(), sim.particles().end(),
                                    [](const Particle &a, const Particle &b) { return a.age < b.age; });
    return p == sim.particles().end() ? none : *p;
}

const Particle &youngest(const ExhaustParticles &sim) {
    static const Particle none{};
    const auto p = std::min_element(sim.particles().begin(), sim.particles().end(),
                                    [](const Particle &a, const Particle &b) { return a.age < b.age; });
    return p == sim.particles().end() ? none : *p;
}

// The oldest age with steam to see (its optical depth 0.05 or more).
float steamLife(const ExhaustParticles &sim) {
    float age = 0.0f;
    for (const Particle &p : sim.particles())
        if (2.0f * p.radius * p.steamDensity >= 0.05f)
            age = std::max(age, p.age);
    return age;
}

// An even flow: Rate puffs a second; hot gas in still air rises, grows and
// thins out, straight up.
void checkStillAir(TokenTest::Suite &test) {
    ExhaustParticles sim;
    standing(sim, air(0.0f, 15.0f, 8.0f), 5.0f, Idle);
    const Particle &old = oldest(sim);
    const Particle &young = youngest(sim);
    float sideways = 0.0f;
    for (const Particle &p : sim.particles())
        sideways = std::max({sideways, std::fabs(p.position[0]), std::fabs(p.position[2])});
    test.check(sim.emitted() >= 99 && sim.emitted() <= 101 && old.position[1] > 6.0f && old.velocity[1] > 0.0f
                       && sideways < 0.001f,
               QString("still air: %1 puffs in 5 s (100); the oldest %2 m above the nozzle, rising %3 m/s, "
                       "%4 m sideways at most")
                       .arg(sim.emitted()).arg(old.position[1] - 4.0f, 0, 'f', 2).arg(old.velocity[1], 0, 'f', 2)
                       .arg(sideways));
    test.check(old.radius > 3.0f * young.radius && old.smokeDensity < 0.02f * young.smokeDensity
                       && young.smokeDensity > 0.3f,
               QString("still air: grown from %1 m to %2 m, its smoke thinned from %3 to %4 1/m")
                       .arg(young.radius, 0, 'f', 2).arg(old.radius, 0, 'f', 2).arg(young.smokeDensity, 0, 'f', 3)
                       .arg(old.smokeDensity, 0, 'f', 4));
}

// In wind the plume bends over and goes with the wind.
void checkWind(TokenTest::Suite &test) {
    ExhaustParticles sim;
    standing(sim, air(5.0f, 15.0f, 8.0f), 10.0f, Idle);
    const Particle &old = oldest(sim);
    float slowest = 99.0f, fastest = 0.0f;
    for (const Particle &p : sim.particles())
        if (p.age > 3.0f) {
            slowest = std::min(slowest, p.velocity[0]);
            fastest = std::max(fastest, p.velocity[0]);
        }
    test.check(old.position[0] > 0.9f * 5.0f * old.age && std::fabs(old.position[2]) < 0.001f && slowest > 4.5f
                       && fastest < 5.5f,
               QString("wind 5 m/s east: the oldest (%1 s) %2 m downwind; those past 3 s at %3-%4 m/s")
                       .arg(old.age, 0, 'f', 1).arg(old.position[0], 0, 'f', 1).arg(slowest, 0, 'f', 2)
                       .arg(fastest, 0, 'f', 2));
}

// A moving train leaves its plume behind: the puffs take its speed at the
// nozzle and lose it to the still air.
void checkMoving(TokenTest::Suite &test) {
    ExhaustParticles sim;
    static int owner;
    float at[3] = {0.0f, 4.0f, 0.0f};
    run(sim, air(0.0f, 15.0f, 8.0f), 300, [&](int f) {
        at[0] = 20.0f * float(f) * Frame;
        sim.emitGas(&owner, 0, at, Up, 0.3f, Full, 1.0f);
    });
    const Particle &old = oldest(sim);
    const Particle &young = youngest(sim);
    float ahead = -1e9f;
    for (const Particle &p : sim.particles())
        ahead = std::max(ahead, p.position[0] - at[0]);
    test.check(old.position[0] < 20.0f && at[0] - young.position[0] < 2.0f && ahead < 0.5f
                       && std::fabs(old.velocity[0]) < 2.0f,
               QString("moving at 20 m/s: the train at %1 m, its oldest puff at %2 m (%3 m/s along), the youngest "
                       "%4 m behind, none ahead (%5 m)")
                       .arg(at[0], 0, 'f', 1).arg(old.position[0], 0, 'f', 1).arg(old.velocity[0], 0, 'f', 2)
                       .arg(at[0] - young.position[0], 0, 'f', 2).arg(ahead, 0, 'f', 2));
    // A jump (the train placed elsewhere) gives no speed.
    ExhaustParticles jumped;
    run(jumped, air(0.0f, 15.0f, 8.0f), 60, [&](int f) {
        at[0] = f < 30 ? 0.0f : 1000.0f;
        jumped.emitGas(&owner, 0, at, Up, 0.3f, Full, 1.0f);
    });
    float fastest = 0.0f;
    for (const Particle &p : jumped.particles())
        fastest = std::max(fastest, std::fabs(p.velocity[0]));
    test.check(fastest < 1.0f, QString("a train placed 1 km away: its puffs at %1 m/s along at most").arg(fastest));
}

// Steam shows while the puff holds more water than its air can: long on a
// cold damp day, short on a warm dry one; superheated, not at the nozzle.
void checkSteam(TokenTest::Suite &test) {
    ExhaustParticles dry, damp;
    standing(dry, air(2.0f, 25.0f, 5.0f), 20.0f, Cocks);
    standing(damp, air(2.0f, 2.0f, 1.0f), 20.0f, Cocks);
    const float shortLife = steamLife(dry), longLife = steamLife(damp);
    test.check(shortLife > 0.05f && shortLife < 2.0f && longLife > 5.0f * shortLife,
               QString("cylinder cocks: steam seen for %1 s at 25 °C, dew point 5 °C; %2 s at 2 °C, dew point 1 °C")
                       .arg(shortLife, 0, 'f', 2).arg(longLife, 0, 'f', 2));
    ExhaustParticles valve;
    standing(valve, air(0.0f, 10.0f, 5.0f), 2.0f, Valve);
    const Particle &nozzle = youngest(valve);
    float thickest = 0.0f;
    for (const Particle &p : valve.particles())
        thickest = std::max(thickest, p.steamDensity);
    test.check(nozzle.age == 0.0f && nozzle.steamDensity == 0.0f && thickest > 3.0f && nozzle.smokeDensity == 0.0f,
               QString("safety valve at 200 °C: clear at the nozzle (%1 1/m), then white (%2 1/m at most), no smoke")
                       .arg(nozzle.steamDensity).arg(thickest, 0, 'f', 1));
    // A stack's exhaust mixed with the fire's gases at 161 °C: dry, clear
    // until the air cools it; wet (its steam's droplets), white at the lip.
    const ExhaustOutput working{3.66f, 3.6f, 161.0f, 0.49f, 0.15f, 0.0f, 0};
    static int owner;
    const float lip[3] = {0.0f, 4.0f, 0.0f};
    ExhaustParticles dryStack, wetStack;
    for (ExhaustParticles *sim : {&dryStack, &wetStack}) {
        const float wetness = sim == &wetStack ? 0.1f : 0.0f;
        run(*sim, air(0.0f, 16.0f, 6.0f), 30, [&](int) { sim->emitGas(&owner, 0, lip, Up, 0.5f, working, 1.0f, wetness); });
    }
    const Particle &dryLip = youngest(dryStack), &wetLip = youngest(wetStack);
    test.check(dryLip.steamDensity == 0.0f && wetLip.steamDensity > 5.0f,
               QString("stack at 161 °C: dry %1 1/m of steam at the lip, wet %2 1/m")
                       .arg(dryLip.steamDensity).arg(wetLip.steamDensity, 0, 'f', 1));
}

// A stack: a puff a beat; its steam goes in dry air, its smoke stays on.
void checkStack(TokenTest::Suite &test) {
    ExhaustParticles sim;
    standing(sim, air(3.0f, 25.0f, 5.0f), 10.0f, Stack);
    const Particle &old = oldest(sim);
    bool steamFirst = false;
    for (const Particle &p : sim.particles())
        steamFirst = steamFirst || (p.steamDensity > 0.0f && p.age < 2.0f);
    test.check(sim.emitted() >= 39 && sim.emitted() <= 41,
               QString("stack at 4 beats a second: %1 puffs in 10 s (40)").arg(sim.emitted()));
    test.check(steamFirst && old.age > 5.0f && old.steamDensity == 0.0f && old.smokeDensity > 0.0f,
               QString("stack in dry air: steam at first, the oldest (%1 s, %2 m) smoke only (%3 1/m, steam %4)")
                       .arg(old.age, 0, 'f', 1).arg(old.radius, 0, 'f', 2).arg(old.smokeDensity, 0, 'f', 4)
                       .arg(old.steamDensity));
}

// Past the limit, new puffs replace the oldest; the limit 0 is off.
void checkLimits(TokenTest::Suite &test) {
    ExhaustParticles sim;
    sim.setLimit(100);
    static int owners[10];
    const float at[3] = {0.0f, 4.0f, 0.0f};
    run(sim, air(0.0f, 15.0f, 8.0f), 300, [&](int) {
        for (int &o : owners)
            sim.emitGas(&o, 0, at, Up, 0.3f, Full, 1.0f);
    });
    const float age = oldest(sim).age;
    test.check(sim.particles().size() == 100 && sim.replaced() > 800 && age < 0.6f,
               QString("limit 100, 10 emitters: %1 live, %2 of %3 replaced, the oldest %4 s")
                       .arg(sim.particles().size()).arg(sim.replaced()).arg(sim.emitted()).arg(age, 0, 'f', 2));
    sim.setLimit(0);
    run(sim, air(0.0f, 15.0f, 8.0f), 10, [&](int) { sim.emitGas(&owners[0], 0, at, Up, 0.3f, Full, 1.0f); });
    test.check(sim.particles().empty(), QString("limit 0: %1 live").arg(sim.particles().size()));
    // The kind's flow shared: half the flow, half the puffs.
    ExhaustParticles whole, half;
    standing(whole, air(0.0f, 15.0f, 8.0f), 5.0f, Full);
    run(half, air(0.0f, 15.0f, 8.0f), 300, [&](int) { half.emitGas(&owners[0], 0, at, Up, 0.3f, Full, 0.5f); });
    const float wholeRadius = youngest(whole).startRadius, halfRadius = youngest(half).startRadius;
    test.check(half.emitted() == whole.emitted() && std::fabs(halfRadius / wholeRadius - std::cbrt(0.5f)) < 0.001f,
               QString("half the flow: %1 puffs (%2), each %3 m across at first (%4)")
                       .arg(half.emitted()).arg(whole.emitted()).arg(2.0f * halfRadius, 0, 'f', 3)
                       .arg(2.0f * wholeRadius, 0, 'f', 3));
}

// The camera to another tile: everything moves by the tile, nothing else
// changes (the emitter's last place too, or it would jump).
void checkShift(TokenTest::Suite &test) {
    ExhaustParticles kept, shifted;
    static int owner;
    float at[3] = {0.0f, 4.0f, 0.0f};
    const Air a = air(3.0f, 15.0f, 8.0f);
    run(kept, a, 240, [&](int f) {
        at[0] = 20.0f * float(f) * Frame;
        kept.emitGas(&owner, 0, at, Up, 0.3f, Full, 1.0f);
    });
    run(shifted, a, 240, [&](int f) {
        if (f == 120)
            shifted.shift(-2048.0f, 0.0f);
        at[0] = 20.0f * float(f) * Frame - (f >= 120 ? 2048.0f : 0.0f);
        shifted.emitGas(&owner, 0, at, Up, 0.3f, Full, 1.0f);
    });
    float worst = kept.particles().size() == shifted.particles().size() ? 0.0f : 1e9f;
    for (size_t i = 0; i < kept.particles().size() && i < shifted.particles().size(); ++i) {
        const Particle &p = kept.particles()[i], &q = shifted.particles()[i];
        worst = std::max({worst, std::fabs(q.position[0] + 2048.0f - p.position[0]),
                          std::fabs(q.position[1] - p.position[1]), std::fabs(q.velocity[0] - p.velocity[0])});
    }
    test.check(worst < 0.01f, QString("a tile shift: %1 puffs, %2 m off at most").arg(shifted.particles().size()).arg(worst));
}

}

int TsreTests::runExhaustParticlesSuite(bool verbose) {
    TokenTest::Suite test{"[tests:exhaust-particles]", verbose};
    checkStillAir(test);
    checkWind(test);
    checkMoving(test);
    checkSteam(test);
    checkStack(test);
    checkLimits(test);
    checkShift(test);
    return test.finish();
}
