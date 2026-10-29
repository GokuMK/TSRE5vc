#pragma once
#include <array>
#include <memory>
#include <vector>
#include <tsre/sim/steam/SteamTables.h>

class VehicleDefinition;

namespace Sim::Steam {

enum class LocoType { Unknown, Simple, Geared, Compound };
enum class GearType { None, Fixed, Select };
enum class EngineKind { Adhesion, Booster, Rack };
enum class Fuel { Coal, Wood, Oil };
enum class InjectorType { Live, Exhaust };
enum class BoilerOrientation { Horizontal, CabForward, Vertical, Sloping };

// One steam engine of a loco (its cylinders, Open Rails' SteamEngine): one
// for an MSTS loco, several for a Mallet or Garratt, a booster.
struct EngineConfig {
    EngineKind kind = EngineKind::Adhesion;
    int cylinders = 2;
    double diameterIn = 0.0, strokeIn = 0.0;
    int lpCylinders = 2;
    double lpDiameterIn = 0.0, lpStrokeIn = 0.0;
    // Its axle's wheel radius.
    double wheelRadiusIn = 0.0;
    int axle = 0;
    double boosterCutoff = 0.3, boosterThrottleCutoff = 0.3, boosterGearRatio = 1.0;
    bool counterPressureBraking = false;
    double maxTractiveEffortLbf = 0.0;
    double maxIndicatedHp = 0.0;
    double excessRodBalanceLb = 0.0;
    // Per step constants: a piston's area and the steam a stroke sweeps.
    double pistonAreaFt2 = 0.0, sweptVolumeFt3 = 0.0;
    double lpPistonAreaFt2 = 0.0, lpSweptVolumeFt3 = 0.0;
};

struct SafetyValves {
    int count = 0;
    double diameterIn = 0.0;
    // One valve's effective area (in²).
    double areaIn2 = 0.0;
    double maxDischargeLbps = 0.0;
    // Each valve's lift and reseat pressures.
    std::array<double, 4> openPsi{}, closePsi{};
};

// A steam loco's settings, as Open Rails sets them up at load
// (docs/tasks/driving/reports/or-steam-loco-spec-load.md §3): its ENG's
// keys with Open Rails' defaults and estimates, in Open Rails' units (psi
// gauge, lb, ft, in, °F, BTU; fire and tender masses in kg). Built once per
// vehicle definition, shared, never changed.
struct Config {
    LocoType type = LocoType::Unknown;
    GearType gear = GearType::None;
    Fuel fuel = Fuel::Coal;
    std::vector<EngineConfig> engines;
    // The MSTS cylinder count (the exhaust beats).
    int mstsCylinders = 2;

    // The car.
    double massKg = 0.0, lengthM = 0.0, wheelRadiusM = 0.0;
    int driveAxles = 1;
    double driveWheelWeightKg = 0.0;
    bool tenderRequired = true;

    // Boiler.
    double maxBoilerPressurePsi = 0.0;
    double boilerVolumeFt3 = 0.0;
    double saturationTempK = 0.0;
    double grateAreaFt2 = 0.0;
    double evaporationAreaFt2 = 0.0;
    double evaporationRateLbpFt2pH = 0.0;
    // The design output, Smax.
    double maxSteamOutputLbps = 0.0;
    double maxBoilerOutputLbph = 0.0;
    double boilerLengthM = 0.0, boilerDiameterM = 0.0, crownHeightM = 0.0, crownCoverageM = 0.0, glassLengthM = 0.0;
    double boilerAngleRad = 0.0;
    BoilerOrientation orientation = BoilerOrientation::Horizontal;
    // Water levels, fractions of the boiler's diameter from its bottom.
    double waterMinLevel = 0.0, waterGlassMaxLevel = 0.0, waterMaxLevel = 0.96, waterMaxLevelSafe = 0.95;
    double startWaterFraction = 0.0;
    double maxBoilerHeatBtu = 0.0, maxBoilerSafetyHeatBtu = 0.0;
    double boilerSurfaceFt2 = 0.0, insulationCoefficient = 0.4, insulatedFraction = 0.86;
    SafetyValves safetyValves;

    // Fire and fuel.
    double fuelCalorificKJpKg = 33400.0;
    double idealFireMassKg = 0.0, maxFireMassKg = 0.0, maxFuelBurnGrateKgps = 0.0;
    double maxFiringRateLbph = 0.0;
    double grateLimitLbpFt2 = 0.0;
    double firemanMaxFiringRateKgps = 0.0, maxTheoreticalFiringRateKgps = 0.0;
    bool mechanicalStoker = false;
    double shovelMassKg = 6.0;
    double damperFactor = 0.0, blowerFactorLbpspPsi = 0.0;

    // Cylinders.
    double cylinderEfficiency = 1.0;
    double superheatCutoffFactor = 40.0;
    double portOpening = 0.085;
    double clearance = 0.09;
    std::array<double, 4> crankAnglesRad{};
    double maxTractiveEffortLbf = 0.0;
    double maxIndicatedHp = 0.0;
    double maxPistonSpeedFtpMin = 0.0, maxSpeedFactor = 0.0, maxLocoSpeedMph = 0.0;
    double steamPerIhpHourLb = 0.0, maxBoilerOutputHp = 0.0;

    // Superheat.
    bool hasSuperheater = false, saturated = false;
    double superheatTempF = 0.0, superheatRatio = 0.0, superheatAreaFt2 = 0.0;

    // Injectors.
    std::array<InjectorType, 2> injectors{InjectorType::Live, InjectorType::Live};
    std::array<double, 2> injectorSizeMm{};
    std::array<double, 2> injectorFactor{};
    double liveCorrection = 1.0, exhaustCorrection = 1.0, auxiliaryCorrection = 1.0;
    double supplementalFactor = 0.0, auxiliaryFactor = 0.0;

    // Auxiliaries.
    double smallEjectorLbps = 0.0, largeEjectorLbps = 0.0;
    bool steamHeating = false;
    double boosterChokeIn = 0.0, boosterPressureFactor = 0.0;

    // Tender.
    double maxTenderCoalKg = 0.0, maxTenderWaterLb = 0.0, maxTenderWaterUkGallons = 0.0;

    // Start (Open Rails' hot start).
    double startPressurePsi = 0.0, startWaterTempK = 0.0, startFlueTempK = 0.0;
    double startBoilerMassLb = 0.0, startBoilerHeatBtu = 0.0;

    // Tables: the ENG's, else Open Rails' defaults.
    Table boilerEfficiency, initialPressureDrop, backPressure, burnRate;
    Table cylinderExhaust, cylinderCompression, cylinderAdmission;
};

// The config of a definition, Open Rails' first car's (TSRE has no copies).
std::shared_ptr<const Config> readConfig(const VehicleDefinition &definition);

// Boiler heat at a pressure with a water fraction (BTU).
double boilerHeatBtu(const Config &c, double pressurePsi, double waterFraction);

}
