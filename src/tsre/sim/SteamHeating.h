#pragma once
#include <vector>

struct VehicleDefinition;

namespace Sim {

// A vehicle's part of its train's steam heating (task driving 06), from its
// file: Open Rails' keys where given, else defaults from its size.
struct HeatingSpec {
    // It carries the heating pipe: engines, tenders, carriages, and any
    // vehicle with heating keys or heating hoses (a freight wagon has none:
    // the pipe ends there). A heated car: a carriage, ORTSWagonSpecialType
    // Heated, or one that sets its compartment's temperature.
    bool pipe = false, heated = false;
    // An engine fitted for it: MaxSteamHeatingPressure (Pa, 0 none).
    double maxPressure = 0.0;
    // Its pipe (its coupled length) and the pipe's and hoses' sizes (m):
    // Open Rails' defaults, a 1.5 in bore lagged to 2.4 in, hoses 2.05 in.
    double length = 0.0;
    double bore = 0.0381, pipeOuter = 0.061, hoseOuter = 0.052;
    // A heated car's compartment: its air (m³), sides, roof (and as much
    // floor) and ends (m²), the windows' share of the sides, its radiators
    // (W/K of steam's temperature over the air's) and its set temperature.
    double volume = 0.0, sides = 0.0, roof = 0.0, ends = 0.0, windows = 0.275;
    double radiators = 0.0, setTemperature = 20.0;
    // It shows its couplings' leaks and its traps' steam (its emitters).
    bool hoseEmitter = false, pipeTrapEmitter = false, radiatorTrapEmitter = false;
};
HeatingSpec ReadHeating(const VehicleDefinition &definition);

// A heated car's losses (W/K): through its body (the outside's film by the
// air's speed), and by its air changes (1.5 an hour standing, 0.4 more for
// each m/s, 20 more with its doors open at a station).
double BodyLoss(const HeatingSpec &car, double speed);
double AirChangeLoss(const HeatingSpec &car, double speed, bool doorsOpen);

// Saturated steam at a gauge pressure (Pa): its temperature (°C), the heat
// its condensing gives (J/kg) and its water's heat over water's at 100 °C
// (J/kg); Open Rails' steam tables.
double SaturationTemperature(double pressure);
double LatentHeat(double pressure);
double WaterHeatOver100(double pressure);

// A train's steam heating (task driving 06): the heating pipe along its
// vehicles in the consist's order, fed by the one whose model holds the
// highest pressure on it (a loco's reducing valve, a heating boiler), and
// each heated car's compartment. The host's, beside the brake pipe, stepped
// every Step: heating changes over minutes.
//
// Each step the pressures are solved out from the feeding vehicle: each
// length of pipe carries the steam taken behind it, falling by its friction
// (isothermal, the steam's density at its pressure), so the last cars get
// the least. What takes steam: the pipe's steel and hoses (their losses to
// the air, and warming a cold pipe), the radiators (each car's thermostatic
// valve over 2 °C below its set temperature), the hose couplings' leaks.
// The condensed steam drains through traps; part of it flashes to steam.
class SteamHeating {
public:
    static constexpr double Step = 0.1;

    struct Vehicle {
        HeatingSpec spec;
        // Its couplings' leak, 0.5 to 1 of the most (as Open Rails varies it).
        double leak = 1.0;
        // The pipe's pressure at it (Pa gauge) and its steel's temperature
        // (°C); a heated car's air and fittings (°C) and valve (0 to 1).
        double pressure = 0.0, pipe = 15.0;
        double air = 15.0, fittings = 15.0, valve = 0.0;
        // Heat (W) the steam gives here to the pipe and to the radiators.
        double pipeHeat = 0.0, radiatorHeat = 0.0;
        // Steam (kg/s): taken here (the pipe, the radiators, the coupling
        // toward the feeding vehicle), through its length from the feeding
        // vehicle's side, and given out by it (the feeding vehicle: all).
        double demand = 0.0, through = 0.0, supplied = 0.0;
        // What shows (kg/s of steam): its couplings' leaks, the flash steam
        // of its pipe's and its radiators' traps.
        double hoseLeak = 0.0, pipeTrap = 0.0, radiatorTrap = 0.0;
    };

    // Its vehicles, started warm (the compartments at their set temperature,
    // the pipe hot, as a train in service in the cold) or at the air's.
    void setUp(std::vector<HeatingSpec> specs, double air, bool warm, unsigned seed);
    // Anything to step: a heated car, or a fitted engine on the pipe.
    bool active() const { return active_; }
    // One step of `dt`: each vehicle's supply (its model's heatingSupply,
    // Pa), the outside air (°C), the train's speed (m/s), its doors open.
    void step(double dt, const std::vector<double> &supply, double air, double speed, bool doorsOpen);

    std::vector<Vehicle> vehicles;
    // The feeding vehicle, -1 none; the pipe it feeds (first to last).
    int source = -1, first = -1, last = -1;

private:
    void demandAt(size_t i, double air, double speed);
    void solveSide(int end, int dir);
    bool active_ = false;
};

}
