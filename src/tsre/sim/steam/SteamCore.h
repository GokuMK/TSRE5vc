#pragma once
#include <vector>
#include <tsre/sim/steam/SteamConfig.h>

namespace Sim::Steam {

// A steam loco's state from one step to the next (task driving 04), Open
// Rails' per-step state (docs/tasks/driving/reports/or-steam-loco-spec-step.md
// §12) in its units: psi gauge, lb, BTU, K for the boiler water, kg for the
// fire and the coal, UK gallons for the tender's water. Flags are 0 or 1.
// Kept as numbers in a list so the loco's script carries it (save and
// restore as any script state): the loco's, then each engine's.
#define TSRE_STEAM_STATE(X) \
    X(started) \
    /* The boiler: heat, mass, pressure, the water's temperature behind it. */ \
    X(boilerHeat) X(boilerMass) X(pressure) X(waterTemperature) \
    /* The 240 s smoothed heat (set: it has a value), its last value, the */ \
    /* heat change it gave (kJ). */ \
    X(heatSmoothed) X(heatSmoothedSet) X(heatSmoothedBefore) X(heatChange) \
    /* The share of the boiler that is water; the steam properties at the */ \
    /* pressure of the last step. */ \
    X(waterFraction) X(steamEnthalpy) X(waterEnthalpy) X(steamDensity) X(waterDensity) \
    /* The fire, its burn and feed filters. */ \
    X(fireMass) X(burnSmoothed) X(burnSmoothedSet) X(feedSmoothed) X(feedSmoothedSet) X(fireLoss) \
    X(tenderCoal) X(tenderWater) X(coalOut) X(waterOut) X(fireOut) X(grateLimit) \
    /* Heat and steam out (BTU/s, lb/s), summed until the next boiler */ \
    /* update, and the totals before; the heat in. */ \
    X(heatOut) X(steamOut) X(heatOutBefore) X(steamOutBefore) X(heatIn) \
    /* The water injected last step (lb); the cylinders' steam, the cocks' */ \
    /* steam and their largest pressure (psia), last step's. */ \
    X(waterInput) X(cylinderSteam) X(cockSteam) X(cockPressure) \
    /* The draught's terms for manual firing (lb/s), the blower on. */ \
    X(radiationSteam) X(blowerOn) X(blowerSteam) X(blowerEffect) X(damperEffect) \
    /* The AI fireman. */ \
    X(heatRatio) X(pressureRatio) X(fullHeatRatio) X(maxHeatRatio) X(fullBoilerHeat) X(fullMaxPressureHeat) \
    X(shovelAnyway) X(fireOn) X(fireOff) X(fireOverride) X(safetyHeatOut) X(cockHeatOut) \
    X(fuelBoost) X(fuelBoostReset) X(boostTime) X(boostResetTime) \
    /* Injectors: on, the share of their delivery, locked out and for how */ \
    /* long, the AI fireman's level (0 off to 3 full), sounding, the */ \
    /* delivery's pressure (psi). */ \
    X(injector1On) X(injector2On) X(injector1Fraction) X(injector2Fraction) \
    X(injector1Locked) X(injector2Locked) X(injector1LockTime) X(injector2LockTime) \
    X(injector1Level) X(injector2Level) X(injector1Sound) X(injector2Sound) \
    X(injector1DeliveryPsi) X(injector2DeliveryPsi) \
    X(safety1) X(safety2) X(safety3) X(safety4) X(fusiblePlug) X(priming) \
    /* Superheat: on, and the factor on the cylinders' steam. */ \
    X(superheatSet) X(superheatFactor) \
    /* The exhaust beats; the driving axle's angle (rad). */ \
    X(pulseTracker) X(nextPulse) X(axleAngle)

// Each engine's: its cylinders' steam (lb/s), the cocks' pressure (psia),
// its average force (N).
#define TSRE_STEAM_ENGINE_STATE(X) X(steam) X(cockPressure) X(averageForce)

struct EngineState {
#define TSRE_STEAM_FIELD(name) double name = 0.0;
    TSRE_STEAM_ENGINE_STATE(TSRE_STEAM_FIELD)
};

struct State {
    TSRE_STEAM_STATE(TSRE_STEAM_FIELD)
#undef TSRE_STEAM_FIELD
    std::vector<EngineState> engines;

    // The numbers: the loco's, then each engine's.
#define TSRE_STEAM_COUNT(name) +1
    static constexpr int LocoCount = 0 TSRE_STEAM_STATE(TSRE_STEAM_COUNT);
    static constexpr int EngineCount = 0 TSRE_STEAM_ENGINE_STATE(TSRE_STEAM_COUNT);
#undef TSRE_STEAM_COUNT
    int size() const { return LocoCount + int(engines.size()) * EngineCount; }
    void write(double *numbers) const;
    void read(const double *numbers, int engineCount);
};

// One step's controls (steam.lua's levers and the driver's).
struct Controls {
    // The regulator 0 to 1; the reverser -1 to 1 (its size the cutoff).
    double regulator = 0.0, reverser = 0.0;
    double injector1 = 0.0, injector2 = 0.0;
    double blower = 0.0, damper = 0.0, firingRate = 0.0;
    bool manualFiring = false;
    bool cylinderCocks = false, blowdown = false;
    // Steam for the air brakes' compressor; the vacuum brakes' ejectors.
    bool compressor = false, smallEjector = false, largeEjector = false;
    // The force from the crank angles (Open Rails' player train), else from
    // the mean effective pressure (its AI trains).
    bool crankForce = true;
    // Since the last step: the injectors switched (manual firing), shovels
    // of coal, the AI fireman's overrides, the tender filled.
    bool injector1Switch = false, injector2Switch = false;
    int shovels = 0;
    bool fireOn = false, fireOff = false, fireReset = false;
    bool refill = false;
};

struct Inputs {
    double dt = 0.0;
    // m/s, + toward the loco's front.
    double speed = 0.0;
    // The sine of the slope, + uphill toward its front.
    double gradient = 0.0;
    double outsideTemperatureC = 15.0;
    bool tenderCoupled = true;
    // Steam the train's heating takes (lb/s).
    double heatingSteam = 0.0;
};

// What a step gives, in SI.
struct Outputs {
    // N, + toward the loco's front (the reverser's way).
    double force = 0.0;
    double forward = 1.0;
    double boilerPressure = 0.0, steamChestPressure = 0.0, backPressure = 0.0;
    double meanEffectivePressure = 0.0, indicatedPower = 0.0;
    // The water glass (its share, not limited) and the gradient glass (0-1).
    double waterGlass = 0.0, waterGlassGradient = 0.0;
    double fire = 0.0, fireMass = 0.0, burnRate = 0.0, feedRate = 0.0;
    double tenderCoal = 0.0, tenderWater = 0.0;
    double cylinderSteam = 0.0, steamUsage = 0.0;
    double injector1Flow = 0.0, injector2Flow = 0.0;
    bool injector1 = false, injector2 = false;
    bool safetyValves = false, fusiblePlug = false, priming = false;
    double superheat = 0.0;
    // Open Rails' sound variables.
    double variable1 = 0.0, variable2 = 0.0, variable3 = 0.0;
    // The exhaust (task driving 05), what each kind of its emitters gives
    // out, as CarExhaust's fields: flow (m³/s), speed (m/s), temperature
    // (°C), steam share, soot, beats (a second). The stack: the cylinders'
    // and the blower's steam with the fire's gases, in puffs with the
    // cylinders' beats; the cylinder cocks; the safety valves.
    struct Exhaust {
        double flow = 0.0, speed = 0.0, temperature = 0.0, steam = 0.0, soot = 0.0, beats = 0.0;
    };
    Exhaust stackExhaust, cockExhaust, safetyExhaust;
    // Sound triggers (Open Rails' numbers) and messages, in order.
    std::vector<int> sounds;
    std::vector<const char *> messages;
};

// One engine's indicator diagram (spec §8.3, §8.5), simple expansion: its
// events (shares of the stroke), the steam chest's pressure and the MEP (psi
// gauge), the pressures at its points a to f (psi absolute), its steam a
// second before the superheat's factor (lb/s).
struct Diagram {
    double release = 0.0, compression = 0.0, admission = 0.0;
    double chest = 0.0, pa = 0.0, pb = 0.0, pc = 0.0, pd = 0.0, pe = 0.0, pf = 0.0;
    double mep = 0.0, steam = 0.0;
};
Diagram diagram(const Config &c, const EngineConfig &e, double pressurePsi, double regulator, double cutoff,
                double revsPerS, double backPressurePsi);
// Its force from the crank angles with the driving axle at `axleAngle`
// (lbf, spec §9.3).
double crankForce(const Config &c, const EngineConfig &e, const Diagram &d, double regulator, double cutoff,
                  double revsPerS, double axleAngle);
// The superheat's factor on the cylinders' steam (§5) at `steamLbps` of it
// and engine 0's speed; `set` the superheat's latch; the superheat (°F) in
// `temperatureF`.
double superheatFactor(const Config &c, double steamLbps, double cutoff, double revsPerS, double &set,
                       double &temperatureF);

// Open Rails' hot start (spec §13).
void start(const Config &c, State &s);
// One step (spec §0).
void step(const Config &c, State &s, const Controls &controls, const Inputs &in, Outputs &out);

}
