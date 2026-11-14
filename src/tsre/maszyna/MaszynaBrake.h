/*  This file is part of TSRE5 (task driving 10, step 4).
 *
 *  It is a port of parts of MaSzyna's TMoverParameters (McZapkie/Mover.cpp
 *  at 66a940d4, https://github.com/eu07/maszyna: LoadFIZ_Brake's keys, the
 *  brake's creation in CheckLocomotiveParameters, IncBrakeMult and
 *  DecBrakeMult, UpdateBrakePressure, the vehicle's part of
 *  UpdatePipePressure and UpdateScndPipePressure, BrakeForce), and so is
 *  under the Mozilla Public License 2.0, as MaSzyna's code is: this Source
 *  Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 *  If a copy of the MPL was not distributed with this file, You can obtain
 *  one at http://mozilla.org/MPL/2.0/.
 */

#ifndef MASZYNABRAKE_H
#define MASZYNABRAKE_H

#include <memory>
#include <string>
#include <tsre/sim/VehicleBrake.h>

struct VehicleDefinition;
namespace MaszynaHamulce {
class TBrake;
}

// A MaSzyna (.fiz) vehicle's brake behind TSRE's interface (task driving
// 10): MaSzyna's distributor (MaszynaHamulce, the class Mover.cpp builds for
// its BrakeValve), its cylinders, rigging and blocks' friction, stepped as
// TMoverParameters steps them. MaSzyna's units inside (bar, litres, bar·l of
// air, km/h, kN); SI at this interface. Its length of the brake pipe holds
// MaSzyna's Pipe (capacity (max(L, 14) + 0.5)·Spg litres), the air its valve
// takes or gives going into it; the flow between vehicles is the pipe
// solver's (BrakePipe, MaSzyna's law between two of these). Not here: the
// driver's handles (the loco's script), the emergency valves, ED blending
// beyond SetED, the wheels sliding (the core's).
class MaszynaBrake : public Sim::VehicleBrake {
public:
    // The distributors ported (Mover.cpp's switch on BrakeValve).
    enum class Valve { West, KE, NESt3, LSt, EStED, EP2, EP1 };

    // The brake of a .fiz vehicle whose valve is one of those, Pneumatic or
    // ElectroPneumatic; null for the rest (no .fiz, no valve, a valve not
    // ported, a vehicle MaSzyna refuses).
    static std::unique_ptr<MaszynaBrake> make(const VehicleDefinition &definition);
    ~MaszynaBrake() override;

    // A new valve, Init at the pipe's pressure (CheckLocomotiveParameters'
    // ready state: the cylinders empty).
    void charge(double pipe) override;
    // UpdateBrakePressure, UpdatePipePressure's vehicle part (its leaks,
    // SetLBP, PLC, SetEPS, GetPF) and UpdateScndPipePressure's GetHPFlow,
    // in steps of at most 0.01 s (MaSzyna's).
    void step(double dt, double speed, double &pipe, double ep, double *mainPipe) override;
    // The local brake's pressure (LocBrakePress, SetLBP, from the next
    // step); bail off: the releaser held (BrakeReleaser 1, then 0).
    void independent(double dt, double pressure, double bailOff) override;
    // BrakeForce: the blocks' force (BCM, BCS, BSA, BRE with its hysteresis)
    // times the friction by its BM at the speed, at the rails.
    double force(double speed) const override;

    double pipeVolume() const override;
    double cylinder() const override;
    bool ep() const override { return ep_; }
    bool twinPipe() const override { return twin; }
    PipeLaw pipeLaw() const override { return PipeLaw::Maszyna; }
    // auxiliaryReservoir, controlReservoir (a valve with one), valveChamber
    // (the distributor's komora wstępna), Pa.
    void values(const std::function<void(const char *name, double value)> &out) const override;

    // MaSzyna's orifice to a neighbour's pipe, its S: the brake pipe's in
    // GetDVc, Spg / (1 + 0.015 / Spg · L); the main reservoir pipe's in
    // UpdateScndPipePressure, Spz · 0.75 (BrakePipe::nozzle, for the core
    // to give).
    double pipeNozzle(bool mainPipe = false) const override { return mainPipe ? 0.5067 * 0.75 : nozzle; }
    // The ED brake's share (SetED, task driving 10 step 6), and the
    // pressure the train brake asks of an ED brake (GetEDBCP, Pa).
    void setED(double share);
    void ed(double share) override { setED(share); }
    double edPressure() const override;
    Valve valve() const { return kind; }
    const MaszynaHamulce::TBrake &hamulec() const { return *Hamulec; }

private:
    MaszynaBrake() = default;
    void create();
    void stepOnce(double dt, double vel, double &pipe, double ep, double *mainPipe);

    Valve kind = Valve::West;
    std::unique_ptr<MaszynaHamulce::TBrake> Hamulec;
    bool ep_ = false, twin = false;
    // The .fiz, MaSzyna's units.
    std::string BrakeValveParams;
    int BrakeValveSize = 0;
    double MaxBrakePress[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
    double BrakeCylRadius = 0.0, BrakeCylDist = 0.0, BrakeCylSpring = 0.0, BrakeSlckAdj = 0.0, BrakeRigEff = 0.0;
    double BrakeCylMult[3] = {0.0, 0.0, 0.0};
    double BrakeVVolume = 0.0, RapidMult = 1.0, RapidVel = 55.0, P2FTrans = 0.0;
    int BrakeCylNo = 0, NBpA = 0, NAxles = 0, BrakeMethod = 0, BrakeDelays = 0, BrakeDelayFlag = 0;
    int LoadFlag = 0;
    double MBPM = 1.0, Mass = 0.0, HighPipePress = 5.0, LowPipePress = 3.5, AirLeakRate = 0.01;
    bool localHandle = false;
    double capacity = 1.0, nozzle = 0.0;
    // Its state.
    double LocBrakePress = 0.0, EDstate = 0.0;
    bool releasing = false;
    double Ntotal = 0.0;
};

#endif // MASZYNABRAKE_H
