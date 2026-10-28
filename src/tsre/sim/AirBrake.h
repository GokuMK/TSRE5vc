#pragma once
#include <algorithm>
#include <vector>

struct VehicleDefinition;

namespace Sim {

// A driven train's air brakes (task driving 02, the buses): after Open
// Rails' single pipe air brake (AirSinglePipe, read 2026-10-28, upstream
// 8aafb3147), simplified: its formulas written afresh, its defaults kept.
// Pressures are gauge, in Pa; volumes m³; rates Pa/s.

// One vehicle's brake on the train's brake pipe: its valve, its auxiliary
// reservoir and its brake cylinder. The valve is the file's
// BrakeEquipmentType: a plain triple valve (it applies when the pipe falls
// below the reservoir and releases fully when it rises above), a distributor
// (graduated: the cylinder follows the pipe's fall from the control
// reservoir), or none (no brake from the pipe, as Open Rails has it). An EP
// brake (BrakeSystemType EP) also fills its cylinder to the demand on the
// train's EP wire. Vacuum brakes have the triple valve until they have a
// model of their own.
class AirBrake {
public:
    enum class Valve { None, TripleValve, Distributor };
    enum class State { Release, Apply, Lap };

    explicit AirBrake(const VehicleDefinition &definition);

    // Charged and released at the pipe's pressure.
    void charge(double pipe);
    // One step on the pipe at it: its valve's air in and out of `pipe`.
    // `ep`: the EP wire's demand, 0 to 1; -2 its holding wire; -1 none.
    void step(double dt, double &pipe, double ep);
    // Its brake force (N) at a speed (m/s): MaxBrakeForce at the reference
    // pressure, with Open Rails' brake shoe friction for older stock.
    double force(double speed) const;

    // A loco's independent (engine) brake: its cylinder at least
    // `pressure` (Open Rails' loco without a relay valve); `bailOff` (Pa/s)
    // lets the automatic brake's air out.
    void independent(double dt, double pressure, double bailOff);

    double pipeVolume() const { return pipeVolume_; }
    // The cylinder: the automatic brake's, or the independent brake's where
    // higher.
    double cylinder() const { return std::max(cylinder_, independent_); }
    double automatic() const { return cylinder_; }
    double auxiliary() const { return auxiliary_; }
    State state() const { return state_; }
    Valve valve() const { return valve_; }
    bool ep() const { return ep_; }

private:
    void stepValve(double dt, double &pipe);

    Valve valve_ = Valve::None;
    bool ep_ = false;
    double maxBrakeForce = 0.0;
    double reference = 0.0, spring = 0.0;
    double maxCylinder = 0.0, serviceMaxCylinder = 0.0;
    double ratio = 2.5;
    double applicationRate = 0.0, serviceApplicationRate = 0.0, releaseRate = 0.0, chargingRate = 0.0;
    double auxiliaryVolume = 0.0, pipeVolume_ = 0.0;
    double sensitivity = 0.0, initialThreshold = 0.0;

    double cylinder_ = 0.0, auxiliary_ = 0.0, control = 0.0, independent_ = 0.0;
    State state_ = State::Release;
};

// The train's brake pipe, one length per vehicle in running order. Air
// flows between neighbours by their pressures' difference (Open Rails'
// pipe time factor sets how fast); each step solves it implicitly along
// the train (backward Euler, one tridiagonal system), so any step is
// stable and no air is lost or made.
class BrakePipe {
public:
    std::vector<double> volume;
    std::vector<double> pressure;
    // s: Open Rails' ORTSBrakePipeTimeFactor (0.0015 s for air).
    double timeFactor = 0.0015;
    void step(double dt);
    void reverse();
};

// A loco's brake valve, worked by its train brake controller: the
// equalising reservoir is set from the controller (without notches: the
// full pressure less the full service drop × its position) and the pipe at
// the loco follows it, charged at the charging rate, vented toward it, or
// vented to nothing in an emergency.
class BrakeValve {
public:
    explicit BrakeValve(const VehicleDefinition &definition);
    // The pipe's air (Pa·m³, + into it) this step at the loco: `pipe` and
    // `volume` the pipe there.
    double step(double dt, double position, bool emergency, double pipe, double volume);

    double maxPressure = 0.0;
    double fullServiceDrop = 0.0;
    double chargingRate = 0.0;
    double emergencyRate = 0.0;
    double serviceTime = 0.0, emergencyTime = 0.0;
    double equalising = 0.0;
};

// Its brake is EP (BrakeSystemType EP, ECP, EP_single_pipe).
bool EpBrakes(const VehicleDefinition &definition);
// The pipe's full pressure a loco's train brake controller gives
// (TrainBrakesControllerMaxSystemPressure; Open Rails' 90 psi).
double BrakeSystemPressure(const VehicleDefinition &definition);
// The pipe's time factor a loco gives (ORTSBrakePipeTimeFactor).
double BrakePipeTimeFactor(const VehicleDefinition &definition);

}
