#pragma once
#include <functional>
#include <memory>

struct VehicleDefinition;

namespace Sim {

// A vehicle's brake on the train's lines (task driving 10): what the core
// steps for every vehicle of a driven or heard train, whatever model is
// behind it (Open Rails' after AirBrake for ENG/WAG, MaSzyna's valves for a
// .fiz). SI at this interface: pressures gauge in Pa, volumes m³, forces N,
// speeds m/s; a model may keep its own units inside.
class VehicleBrake {
public:
    // How air passes between this vehicle's length of a pipe and its
    // neighbour's: Open Rails' (by the pressures' difference and the pipe's
    // time factor) or MaSzyna's (its nozzle flow). A joint takes MaSzyna's
    // only between two vehicles that both ask for it.
    enum class PipeLaw { OpenRails, Maszyna };

    virtual ~VehicleBrake() = default;

    // Charged and released at the brake pipe's pressure.
    virtual void charge(double pipe) = 0;
    // One step on the pipe at it, the vehicle at `speed` (m/s; a speed
    // relay's): its valve's air in and out of `pipe`, and of the main
    // reservoir pipe at it (`mainPipe`, null without one). `ep`: the EP
    // wire's demand, 0 to 1; -2 its holding wire; -1 none.
    virtual void step(double dt, double speed, double &pipe, double ep, double *mainPipe) = 0;
    // A loco's independent (engine) brake: its cylinder at least `pressure`;
    // `bailOff` (Pa/s) lets the automatic brake's air out.
    virtual void independent(double dt, double pressure, double bailOff) = 0;
    // Its brake force at the rails (N) at a speed (m/s).
    virtual double force(double speed) const = 0;

    // Its length of the brake pipe (m³).
    virtual double pipeVolume() const = 0;
    // Its cylinder (the highest of its cylinders' pressures).
    virtual double cylinder() const = 0;
    // It is worked by the EP wire; it has the main reservoir pipe.
    virtual bool ep() const = 0;
    virtual bool twinPipe() const = 0;
    virtual PipeLaw pipeLaw() const { return PipeLaw::OpenRails; }
    // Its orifice to a neighbour's pipe under MaSzyna's law (its S, in
    // MaSzyna's units; the brake pipe's, or the main reservoir pipe's): 0
    // for none, the pipe then takes it from the length's volume.
    virtual double pipeNozzle(bool mainPipe) const {
        (void)mainPipe;
        return 0.0;
    }

    // Its ED brake channel (MaSzyna's TBrake GetEDBCP and SetED, task
    // driving 10 step 6): the pressure its brake asks of an ED brake (Pa;
    // -1 for none) and the ED brake's share of its air brake (0 to 1: the
    // air let off as the ED brake brakes), from its vehicle's model.
    virtual double edPressure() const { return -1.0; }
    virtual void ed(double share) { (void)share; }

    // What it shows (for the cab's gauges, the HUD and logs), each value by
    // its name, beside brakePipe and brakeCylinder the core gives.
    virtual void values(const std::function<void(const char *name, double value)> &out) const = 0;
};

// The brake a vehicle's definition gives it.
std::unique_ptr<VehicleBrake> MakeVehicleBrake(const VehicleDefinition &definition);

}
