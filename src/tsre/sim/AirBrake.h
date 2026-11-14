#pragma once
#include <algorithm>
#include <vector>
#include <tsre/sim/VehicleBrake.h>

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
// train's EP wire. A twin pipe vehicle (Air_twin_pipe, EP) has the main
// reservoir pipe too: a distributor's reservoir recharges from it up to its
// control reservoir (Open Rails' ORTSMainResPipeAuxResCharging, on by
// default), so the brake pipe refills only itself on release. Vacuum brakes
// have the triple valve until they have a model of their own.
// Behind VehicleBrake (task driving 10) for ENG/WAG vehicles, with what
// Open Rails has there that the content uses (task driving 10, step 7): the
// emergency reservoir (BrakeEquipmentType emergency_brake_reservoir, in 1,700
// OR_PSK files) with its emergency valve, and the brake shoes' friction
// (ORTSBrakeShoeFriction, ORTSBrakeShoeType). Its quick service, relay
// valves, retainers, uniform charging, two stages, supply reservoir and
// advanced cylinder are not here: no file of OR_PSK, OR_PSK_NEW, MSTS or
// TS_STARTER_ROUTE has their keys.
class AirBrake : public VehicleBrake {
public:
    enum class Valve { None, TripleValve, Distributor };
    enum class State { Release, Apply, Lap, Emergency };
    // Open Rails' brake shoes (ORTSBrakeShoeType, as it parses it: the
    // names written as its enum's, else Unknown).
    enum class Shoe { Unknown, CastIronP6, CastIronP10, DiscPads, HighFrictionComposite, UserDefined };

    explicit AirBrake(const VehicleDefinition &definition);

    // Charged and released at the pipe's pressure.
    void charge(double pipe) override;
    // One step on the pipe at it: its valve's air in and out of `pipe`, and
    // of the main reservoir pipe at it (`mainPipe`, null without one).
    // `ep`: the EP wire's demand, 0 to 1; -2 its holding wire; -1 none.
    void step(double dt, double &pipe, double ep, double *mainPipe = nullptr);
    void step(double dt, double speed, double &pipe, double ep, double *mainPipe) override {
        (void)speed;
        step(dt, pipe, ep, mainPipe);
    }
    // Its brake force (N) at a speed (m/s): MaxBrakeForce at the reference
    // pressure, by Open Rails' brake shoe friction (its player train's,
    // with advanced adhesion): a known shoe's own (Karwatzki's), else the
    // file's curve against its value at a stand, else for stock whose
    // MaxBrakeForce assumed a friction of 0.2 (Curtius-Kniffler).
    double force(double speed) const override;
    // The shoes' friction at a speed (m/s) and a force on them (N): the
    // share of the force at the shoes that brakes.
    double shoeFriction(double speed, double shoeForce) const;

    // A loco's independent (engine) brake: its cylinder at least
    // `pressure` (Open Rails' loco without a relay valve); `bailOff` (Pa/s)
    // lets the automatic brake's air out.
    void independent(double dt, double pressure, double bailOff) override;

    double pipeVolume() const override { return pipeVolume_; }
    // The cylinder: the automatic brake's, or the independent brake's where
    // higher.
    double cylinder() const override { return std::max(cylinder_, independent_); }
    double automatic() const { return cylinder_; }
    double auxiliary() const { return auxiliary_; }
    double controlReservoir() const { return control; }
    bool emergencyReservoir() const { return emergencyReservoir_; }
    double emergency() const { return emergency_; }
    Shoe shoe() const { return shoe_; }
    State state() const { return state_; }
    Valve valve() const { return valve_; }
    bool ep() const override { return ep_; }
    bool twinPipe() const override { return twin_; }
    // auxiliaryReservoir, a distributor's controlReservoir, the
    // emergencyReservoir where it has one.
    void values(const std::function<void(const char *, double)> &out) const override;

private:
    void stepState(double pipe);
    void stepValve(double dt, double &pipe, double *mainPipe);
    void stepEmergencyReservoir(double dt, double &pipe);

    Valve valve_ = Valve::None;
    bool ep_ = false;
    bool twin_ = false, mainPipeCharging = false;
    double maxBrakeForce = 0.0;
    double reference = 0.0, spring = 0.0;
    double maxCylinder = 0.0, serviceMaxCylinder = 0.0;
    double ratio = 2.5;
    double applicationRate = 0.0, serviceApplicationRate = 0.0, releaseRate = 0.0, chargingRate = 0.0;
    double auxiliaryVolume = 0.0, pipeVolume_ = 0.0;
    double sensitivity = 0.0, initialThreshold = 0.0;
    // Pa/s: Open Rails' ORTSBrakeInsensitivity.
    double insensitivity = 0.0;
    // The emergency reservoir: its volume against the auxiliary's, its
    // charging rate; the pipe's pressure that trips the emergency valve
    // against the emergency reservoir's (Open Rails' legacy valve: 0.75 of
    // what the cylinder would equalise at).
    bool emergencyReservoir_ = false;
    double emergencyRatio = 1.4, emergencyChargingRate = 0.0;
    // The shoes: their kind, the force MaxBrakeForce becomes with a known
    // one, their number, the file's friction curve (km/h, friction).
    Shoe shoe_ = Shoe::Unknown;
    double maxShoeForce = 0.0;
    double shoes = 0.0;
    std::vector<double> frictionSpeed, frictionValue;

    double cylinder_ = 0.0, auxiliary_ = 0.0, control = 0.0, independent_ = 0.0, emergency_ = 0.0;
    State state_ = State::Release;
    // In an emergency: how long (s); Open Rails' dump valve timer has run
    // out (its emergency release may then begin).
    double emergencyClock = 0.0;
    bool emergencyTimedOut = false;
};

// The train's brake pipe, one length per vehicle in running order. Air
// flows between neighbours by their pressures' difference (Open Rails'
// pipe time factor sets how fast), or between two MaSzyna vehicles by
// MaSzyna's nozzle law (Mover.cpp's GetDVc: half of PF(p, p', S) from
// each side) linearised at the step's start; each step solves it
// implicitly along the train (backward Euler, one tridiagonal system), so
// any step is stable and no air is lost or made.
class BrakePipe {
public:
    std::vector<double> volume;
    std::vector<double> pressure;
    // Whether each length joins the next (empty: all do): the main
    // reservoir pipe only between two twin pipe vehicles.
    std::vector<char> joined;
    // How air passes each joint (empty: Open Rails' law at every joint;
    // task driving 10).
    std::vector<VehicleBrake::PipeLaw> laws;
    // MaSzyna's law's orifice at each length (its S in MaSzyna's units:
    // MaszynaBrake::pipeNozzle); a joint takes the mean of its two lengths'.
    // Empty (or 0 at a length): from the length's volume, as a 1" pipe
    // (SpO 0.507) as long as the volume holds.
    std::vector<double> nozzle;
    // s: Open Rails' ORTSBrakePipeTimeFactor (0.0015 s for air).
    double timeFactor = 0.0015;
    void step(double dt);
    void reverse();

private:
    // MaSzyna's flow between length i and i + 1 (m³/s per Pa of difference).
    double maszynaConductance(size_t i) const;
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
// It has the main reservoir pipe (BrakeSystemType Air_twin_pipe, EP, ECP).
bool TwinPipe(const VehicleDefinition &definition);
// The main reservoir pipe's pressure a loco keeps (Open Rails' for a twin
// pipe: its main reservoir's maximum, 130 psi by default, at most
// AirBrakeMaxMainResPipePressure, by default that and 5 psi).
double MainPipePressure(const VehicleDefinition &definition);
// The pipe's full pressure a loco's train brake controller gives
// (TrainBrakesControllerMaxSystemPressure; Open Rails' 90 psi).
double BrakeSystemPressure(const VehicleDefinition &definition);
// The pipe's time factor a loco gives (ORTSBrakePipeTimeFactor).
double BrakePipeTimeFactor(const VehicleDefinition &definition);

}
