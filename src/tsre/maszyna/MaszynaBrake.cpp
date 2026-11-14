/*  This file is part of TSRE5 (task driving 10, step 4).
 *
 *  It is a port of parts of MaSzyna's TMoverParameters (McZapkie/Mover.cpp
 *  at 66a940d4, https://github.com/eu07/maszyna: LoadFIZ_Brake's keys,
 *  BrakeValveDecode, the brake's creation in CheckLocomotiveParameters,
 *  IncBrakeMult and DecBrakeMult, UpdateBrakePressure, the vehicle's part of
 *  UpdatePipePressure and UpdateScndPipePressure, BrakeForce), and so is
 *  under the Mozilla Public License 2.0, as MaSzyna's code is: this Source
 *  Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 *  If a copy of the MPL was not distributed with this file, You can obtain
 *  one at http://mozilla.org/MPL/2.0/.
 *
 *  MaSzyna's names, order and units inside (bar, litres, bar·l of air, km/h,
 *  kN, kg); SI at the edge (the .fiz as MaszynaFiz gives it, the interface).
 *  Its quirks kept: a vehicle without BCM or NBpA brakes with no force, the
 *  load setting passes a pressure as a mass to PLC, the brake force lags the
 *  cylinder a step. Not MaSzyna's: HiPP without a value is 5 bar (MaSzyna
 *  adds ±0.009 at random), no load (LoadAmount 0, as MaSzyna's tool), the
 *  local brake's pressure and the releaser act from the next step (the core
 *  gives them after the step), TSRE's EP wire turned into EPS (below).
 */

#include <tsre/maszyna/MaszynaBrake.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <tsre/maszyna/MaszynaFiz.h>
#include <tsre/maszyna/MaszynaHamulce.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/sim/AirBrake.h>
#include <tsre/trains/VehicleDefinition.h>

using namespace MaszynaHamulce;

namespace {

// MaSzyna's units from MaszynaFiz's SI.
constexpr double Bar = 100000.0;
constexpr double KiloNewton = 1000.0;
constexpr double Kmh = 1.0 / 3.6;
// The longest step of MaSzyna's physics (drivermode.h: 10 ms).
constexpr double MaxStep = 0.01;

// MOVER.h's coupling flags.
constexpr int MainHose = 0x20;

// s2NPW + s2NNW: a capital letter that many powered axles, a digit that many
// others.
int axlesOf(const QString &arrangement) {
    int n = 0;
    for (const QChar c : arrangement) {
        const ushort u = c.unicode();
        if (u >= 65 && u <= 90)
            n += u - 64;
        else if (u >= 49 && u <= 57)
            n += u - 48;
    }
    return n;
}

} // namespace

MaszynaBrake::~MaszynaBrake() = default;

std::unique_ptr<MaszynaBrake> MaszynaBrake::make(const VehicleDefinition &definition) {
    if (definition.maszyna == nullptr)
        return nullptr;
    const MaszynaFiz &fiz = definition.maszyna->physics();
    const MaszynaFiz::Section *brake = fiz.section(u"Brake");
    if (brake == nullptr)
        return nullptr;

    // BrakeValveDecode: an unknown name with "ESt" in it is ESt3.
    const QString valve = brake->text(u"BrakeValve");
    static const std::map<QString, Valve> valves = {
        {"W", Valve::West},    {"K", Valve::West},     {"KE", Valve::KE},   {"NESt3", Valve::NESt3},
        {"ESt3", Valve::NESt3}, {"ESt3AL2", Valve::NESt3}, {"ESt4", Valve::NESt3}, {"LSt", Valve::LSt},
        {"EStED", Valve::EStED}, {"EP2", Valve::EP2},  {"EP1", Valve::EP1}};
    Valve kind;
    if (const auto found = valves.find(valve); found != valves.end())
        kind = found->second;
    else if (valve.contains(QLatin1String("ESt"))) // none of MaSzyna's other names has it
        kind = Valve::NESt3;
    else
        return nullptr;
    // BrakeSystem (Cntrl.): else Individual, which CheckLocomotiveParameters
    // refuses with a valve (so BrakeForce's friction is always the blocks').
    const QString system = fiz.text(u"Cntrl", u"BrakeSystem");
    if (system != QLatin1String("Pneumatic") && system != QLatin1String("ElectroPneumatic"))
        return nullptr;

    std::unique_ptr<MaszynaBrake> b(new MaszynaBrake);
    b->kind = kind;
    b->ep_ = system == QLatin1String("ElectroPneumatic");

    // LoadFIZ_Brake.
    b->BrakeValveParams = valve.toStdString();
    b->NBpA = int(brake->number(u"NBpA"));
    b->BrakeValveSize = int(brake->number(u"Size"));
    b->MaxBrakePress[3] = brake->number(u"MaxBP") / Bar;
    if (b->MaxBrakePress[3] > 0.0) {
        b->BrakeCylNo = int(brake->number(u"BCN"));
        if (b->BrakeCylNo > 0) {
            b->MaxBrakePress[0] = brake->number(u"MaxLBP") / Bar;
            if (b->MaxBrakePress[0] < 0.01)
                b->MaxBrakePress[0] = b->MaxBrakePress[3];
            b->MaxBrakePress[1] = brake->number(u"TareMaxBP") / Bar;
            b->MaxBrakePress[2] = brake->number(u"MedMaxBP") / Bar;
            b->MaxBrakePress[4] = brake->number(u"MaxASBP") / Bar;
            if (b->MaxBrakePress[4] < 0.01)
                b->MaxBrakePress[4] = 0.0;
            b->BrakeCylRadius = brake->number(u"BCR");
            b->BrakeCylDist = brake->number(u"BCD");
            b->BrakeCylSpring = brake->number(u"BCS") / KiloNewton;
            b->BrakeSlckAdj = brake->number(u"BSA") / KiloNewton;
            b->BrakeRigEff = brake->number(u"BRE", 1.0);
            b->BrakeCylMult[0] = brake->number(u"BCM");
            b->BrakeCylMult[1] = brake->number(u"BCMlo");
            b->BrakeCylMult[2] = brake->number(u"BCMHi");
            b->P2FTrans = 100 * M_PI * std::pow(b->BrakeCylRadius, 2); // w kN/bar
            b->LoadFlag = b->BrakeCylMult[1] > 0.0 || b->MaxBrakePress[1] > 0.0 ? 1 : 0;
            b->BrakeVVolume = brake->number(u"BVV") * 1000.0;
            static const std::map<QString, int> methods = {
                {"P10-Bg", bp_P10Bg},     {"P10-Bgu", bp_P10Bgu},   {"FR513", bp_FR513}, {"FR510", bp_FR510},
                {"Cosid", bp_Cosid},      {"P10yBg", bp_P10yBg},    {"P10yBgu", bp_P10yBgu}, {"Disk1", bp_D1},
                {"Disk1+Mg", bp_D1 + bp_MHS}, {"Disk2", bp_D2}};
            const auto method = methods.find(brake->text(u"BM"));
            b->BrakeMethod = method != methods.end() ? method->second : 0;
            b->RapidMult = brake->number(u"RM", 1.0);
            b->RapidVel = brake->number(u"RV", 55.0 * Kmh) / Kmh;
        }
    } else {
        b->P2FTrans = 0;
    }
    b->HighPipePress = brake->number(u"HiPP", 5.0 * Bar) / Bar;
    b->LowPipePress = std::min(b->HighPipePress, 3.5);
    if (brake->value(u"LoPP") != nullptr)
        b->LowPipePress = brake->number(u"LoPP") / Bar;
    if (const MaszynaFiz::Value *leak = brake->value(u"AirLeakRate"); leak != nullptr && leak->isNumber)
        b->AirLeakRate = leak->number * 0.01;

    // LoadFIZ_Cntrl: BrakeDelays, the local brake's handle, MaxBPMass (kg).
    static const std::map<QString, int> delays = {{"GPR", bdelay_G + bdelay_P + bdelay_R},
                                                  {"PR", bdelay_P + bdelay_R},
                                                  {"GP", bdelay_G + bdelay_P},
                                                  {"R", bdelay_R},
                                                  {"P", bdelay_P},
                                                  {"G", bdelay_G},
                                                  {"GPR+Mg", bdelay_G + bdelay_P + bdelay_R + bdelay_M},
                                                  {"PR+Mg", bdelay_P + bdelay_R + bdelay_M}};
    const auto delay = delays.find(fiz.text(u"Cntrl", u"BrakeDelays"));
    b->BrakeDelays = delay != delays.end() ? delay->second : 0;
    const QString locHandle = fiz.text(u"Cntrl", u"LocBrakeHandle");
    b->localHandle = locHandle == QLatin1String("FD1") || locHandle == QLatin1String("Knorr")
            || locHandle == QLatin1String("Westinghouse");
    if (const MaszynaFiz::Section *cntrl = fiz.section(u"Cntrl"); cntrl != nullptr && cntrl->value(u"MaxBPMass") != nullptr)
        b->MBPM = cntrl->number(u"MaxBPMass");

    // Param, Dimensions, Wheels, Load.
    b->Mass = fiz.number(u"Param", u"M");
    const double power = fiz.number(u"Param", u"PWR") / 1000.0;
    const double length = fiz.number(u"Dimensions", u"L");
    b->NAxles = axlesOf(fiz.text(u"Wheels", u"Axle"));
    const double maxLoad = fiz.number(u"Load", u"MaxLoad");

    // Its couplers' AllowedFlag (BuffCoupl. both, BuffCoupl1./2. each;
    // default 3, negative permanent).
    int allowed[2] = {3, 3};
    const auto flag = [&fiz](const char16_t *section, int &out) {
        const MaszynaFiz::Section *s = fiz.section(QStringView(section));
        if (s != nullptr && s->value(u"AllowedFlag") != nullptr) {
            const int f = int(s->number(u"AllowedFlag"));
            out = f < 0 ? -f | 0x80 : f;
        }
    };
    flag(u"BuffCoupl", allowed[0]);
    allowed[1] = allowed[0];
    flag(u"BuffCoupl1", allowed[0]);
    flag(u"BuffCoupl2", allowed[1]);
    b->twin = ((allowed[0] | allowed[1]) & MainHose) != 0;

    // CheckLocomotiveParameters: the auxiliary reservoir.
    if (b->BrakeVVolume == 0 && b->MaxBrakePress[3] > 0)
        b->BrakeVVolume = b->MaxBrakePress[3] / (5.0 - b->MaxBrakePress[3])
                * (b->BrakeCylRadius * b->BrakeCylRadius * b->BrakeCylDist * b->BrakeCylNo * M_PI) * 1000;
    if (b->BrakeVVolume == 0.0)
        b->BrakeVVolume = 0.01;
    // The main pipe's size and the pipe's capacity.
    const double Spg = (b->BrakeDelays & bdelay_G) == bdelay_G
                    && ((b->BrakeDelays & bdelay_R) != bdelay_R || power > 1.0) ? 0.792 : 0.507;
    b->capacity = (std::max(length, 14.0) + 0.5) * Spg * 1;
    b->nozzle = Spg / (1.0 + 0.015 / Spg * length);
    // The load setting (LoadAmount 0).
    const auto incBrakeMult = [&b]() {
        if (b->LoadFlag > 0 && b->MBPM < 2 && b->LoadFlag < 3) {
            if (b->MaxBrakePress[2] > 0 && b->LoadFlag == 1)
                b->LoadFlag = 2;
            else
                b->LoadFlag = 3;
            if (b->BrakeCylMult[2] > 0)
                b->BrakeCylMult[0] = b->BrakeCylMult[2];
        }
    };
    const auto decBrakeMult = [&b]() {
        if (b->LoadFlag > 1 && b->MBPM < 2) {
            if (b->MaxBrakePress[2] > 0 && b->LoadFlag == 3)
                b->LoadFlag = 2;
            else
                b->LoadFlag = 1;
            if (b->BrakeCylMult[1] > 0)
                b->BrakeCylMult[0] = b->BrakeCylMult[1];
        }
    };
    const double LoadAmount = 0.0;
    if (b->LoadFlag > 0) {
        if (LoadAmount < maxLoad * 0.45) {
            incBrakeMult();
            incBrakeMult();
            decBrakeMult();
            if (LoadAmount < maxLoad * 0.35)
                decBrakeMult();
        } else {
            incBrakeMult();
            if (LoadAmount >= maxLoad * 0.55)
                incBrakeMult();
        }
    }
    // taki mini automat
    b->BrakeDelayFlag = bdelay_P;
    if ((b->BrakeDelays & bdelay_G) == bdelay_G && (b->BrakeDelays & bdelay_R) != bdelay_R)
        b->BrakeDelayFlag = bdelay_G;
    if ((b->BrakeDelays & bdelay_R) == bdelay_R && (b->BrakeDelays & bdelay_G) != bdelay_G)
        b->BrakeDelayFlag = bdelay_R;

    b->create();
    b->Hamulec->Init(b->HighPipePress, b->HighPipePress, b->LowPipePress, 0.0, b->BrakeDelayFlag);
    return b;
}

void MaszynaBrake::create() {
    const double mbp = MaxBrakePress[3];
    const auto lp = [this](TBrake &h) {
        if (MBPM < 2) // jesli przystawka wazaca
            h.SetLP(0, MaxBrakePress[3], 0);
        else
            h.SetLP(Mass, MBPM, MaxBrakePress[1]);
    };
    switch (kind) {
    case Valve::West:
        Hamulec = std::make_unique<TWest>(mbp, BrakeCylRadius, BrakeCylDist, BrakeVVolume, BrakeCylNo, BrakeDelays,
                                          BrakeMethod, NAxles, NBpA);
        lp(*Hamulec);
        break;
    case Valve::KE:
        Hamulec = std::make_unique<TKE>(mbp, BrakeCylRadius, BrakeCylDist, BrakeVVolume, BrakeCylNo, BrakeDelays,
                                        BrakeMethod, NAxles, NBpA);
        Hamulec->SetRM(RapidMult);
        lp(*Hamulec);
        break;
    case Valve::NESt3: {
        auto h = std::make_unique<TNESt3>(mbp, BrakeCylRadius, BrakeCylDist, BrakeVVolume, BrakeCylNo, BrakeDelays,
                                          BrakeMethod, NAxles, NBpA);
        h->SetSize(BrakeValveSize, BrakeValveParams);
        Hamulec = std::move(h);
        lp(*Hamulec);
        break;
    }
    case Valve::LSt:
        Hamulec = std::make_unique<TLSt>(mbp, BrakeCylRadius, BrakeCylDist, BrakeVVolume, BrakeCylNo, BrakeDelays,
                                         BrakeMethod, NAxles, NBpA);
        Hamulec->SetRM(RapidMult);
        break;
    case Valve::EStED:
        Hamulec = std::make_unique<TEStED>(mbp, BrakeCylRadius, BrakeCylDist, BrakeVVolume, BrakeCylNo, BrakeDelays,
                                           BrakeMethod, NAxles, NBpA);
        Hamulec->SetRM(RapidMult);
        lp(*Hamulec);
        break;
    case Valve::EP2:
        Hamulec = std::make_unique<TEStEP2>(mbp, BrakeCylRadius, BrakeCylDist, BrakeVVolume, BrakeCylNo, BrakeDelays,
                                            BrakeMethod, NAxles, NBpA);
        Hamulec->SetLP(Mass, MBPM, MaxBrakePress[1]);
        break;
    case Valve::EP1:
        Hamulec = std::make_unique<TEStEP1>(mbp, BrakeCylRadius, BrakeCylDist, BrakeVVolume, BrakeCylNo, BrakeDelays,
                                            BrakeMethod, NAxles, NBpA);
        Hamulec->SetLP(Mass, MBPM, MaxBrakePress[1]);
        Hamulec->SetRM(RapidMult);
        break;
    }
    Hamulec->SetASBP(MaxBrakePress[4]);
    Hamulec->SetRV(RapidVel);
}

void MaszynaBrake::charge(double pipe) {
    create();
    Hamulec->Init(pipe / Bar, HighPipePress, LowPipePress, 0.0, BrakeDelayFlag);
    Hamulec->SetED(EDstate);
    releasing = false;
    Ntotal = 0.0;
}

void MaszynaBrake::step(double dt, double speed, double &pipe, double ep, double *mainPipe) {
    if (dt <= 0.0)
        return;
    const double vel = std::fabs(speed) * 3.6;
    const int steps = std::max(1, int(std::ceil(dt / MaxStep - 1e-9)));
    for (int i = 0; i < steps; ++i)
        stepOnce(dt / steps, vel, pipe, ep, mainPipe);
}

void MaszynaBrake::stepOnce(double dt, double Vel, double &pipe, double ep, double *mainPipe) {
    // UpdateBrakePressure.
    Hamulec->ForceLeak(dt * AirLeakRate * 0.25);
    const double BrakePress = Hamulec->GetBCP();

    // UpdatePipePressure: the pipe's leaks (Pipe->Flow, Act: never below 0).
    double PipePress = pipe / Bar;
    if (PipePress > 1.0)
        PipePress = std::max(0.0, PipePress - PipePress * AirLeakRate * dt / capacity);
    PipePress = std::max(0.0, PipePress - PipePress * 0.001 * dt / capacity);
    switch (kind) {
    case Valve::West:
        if (localHandle)
            Hamulec->SetLBP(LocBrakePress);
        Hamulec->PLC(MBPM < 2 ? MaxBrakePress[LoadFlag] : Mass);
        break;
    case Valve::LSt:
    case Valve::EStED:
        Hamulec->SetLBP(LocBrakePress);
        if (kind == Valve::EStED)
            Hamulec->PLC(MBPM < 2 ? MaxBrakePress[LoadFlag] : Mass);
        break;
    case Valve::EP2:
    case Valve::EP1:
        Hamulec->PLC(Mass);
        break;
    case Valve::NESt3:
        Hamulec->PLC(MBPM < 2 ? MaxBrakePress[LoadFlag] : Mass);
        Hamulec->SetLBP(LocBrakePress);
        break;
    case Valve::KE:
        Hamulec->SetLBP(LocBrakePress);
        Hamulec->PLC(MBPM < 2 ? MaxBrakePress[LoadFlag] : Mass);
        break;
    }
    // The EP brake (SetEPS, MaSzyna's from the EP handles: + apply, -
    // release, 0 hold; EP1 the demand in EPS's fractional part) from TSRE's
    // EP wire (0 to 1 a demand, -2 holding, -1 none): EP2 and Westinghouse
    // apply toward the demand's share of MaxBP and release below it.
    if (kind == Valve::West || kind == Valve::EP2 || kind == Valve::EP1) {
        double EPS = 0.0;
        if (ep >= 0.0) {
            if (kind == Valve::EP1) {
                EPS = std::min(ep, 0.999);
            } else {
                const double target = std::min(ep, 1.0) * MaxBrakePress[3];
                const double now = Hamulec->GetBCP();
                EPS = ep <= 0.0 || target < now - 0.05 ? -1.0 : target > now + 0.05 ? 1.0 : 0.0;
            }
        }
        Hamulec->SetEPS(EPS);
    }
    // Its valve on the pipe.
    const double temp = (Hamulec->GetBrakeStatus() & b_dmg) == b_dmg ? 0.0 : 1.0;
    PipePress = std::max(0.0, PipePress + temp * Hamulec->GetPF(temp * PipePress, dt, Vel) / capacity);
    pipe = PipePress * Bar;

    // UpdateScndPipePressure: the main pipe's leak, the valve's air from it.
    double ScndPipePress = 0.0;
    if (mainPipe != nullptr) {
        ScndPipePress = *mainPipe / Bar;
        if (ScndPipePress > 1.0)
            ScndPipePress = std::max(0.0, ScndPipePress - ScndPipePress * AirLeakRate * dt / capacity);
    }
    const double dv = Hamulec->GetHPFlow(ScndPipePress, dt);
    if (mainPipe != nullptr)
        *mainPipe = std::max(0.0, ScndPipePress + dv / capacity) * Bar;

    // BrakeForce's pressure on the blocks, its hysteresis.
    double u = (BrakePress * P2FTrans - BrakeCylSpring) * BrakeCylMult[0] - BrakeSlckAdj;
    if (u * BrakeRigEff > Ntotal) // histereza na nacisku klockow
        Ntotal = u * BrakeRigEff;
    else {
        u = (BrakePress * P2FTrans - BrakeCylSpring) * BrakeCylMult[0] - BrakeSlckAdj;
        if (u * (2.0 - BrakeRigEff) < Ntotal) // histereza na nacisku klockow
            Ntotal = u * (2.0 - BrakeRigEff);
    }
}

void MaszynaBrake::independent(double dt, double pressure, double bailOff) {
    (void)dt;
    LocBrakePress = std::max(0.0, pressure) / Bar;
    // BrakeReleaser: on while bail off is held.
    const bool held = bailOff > 0.0;
    if (held != releasing) {
        Hamulec->Releaser(held ? 1 : 0);
        releasing = held;
    }
}

double MaszynaBrake::force(double speed) const {
    double K{0};
    auto const NBrakeAxles{NAxles};
    if (NBrakeAxles * NBpA > 0) {
        if (Ntotal > 0) // nie luz
            K += Ntotal; // w kN
        K *= static_cast<double>(BrakeCylNo) / (NBrakeAxles * static_cast<double>(NBpA)); // w kN na os
    }
    // Pneumatic or ElectroPneumatic (make).
    const double u = Hamulec->GetFC(std::fabs(speed) * 3.6, K);
    const double UnitBrakeForce = u * K * 1000.0; // sila na jeden klocek w N
    return UnitBrakeForce * NBrakeAxles * std::max(1, NBpA);
}

double MaszynaBrake::pipeVolume() const {
    return capacity / 1000.0;
}

double MaszynaBrake::cylinder() const {
    return Hamulec->GetBCP() * Bar;
}

void MaszynaBrake::values(const std::function<void(const char *, double)> &out) const {
    out("auxiliaryReservoir", Hamulec->GetBRP() * Bar);
    if (kind != Valve::West)
        out("controlReservoir", Hamulec->GetCRP() * Bar);
    out("valveChamber", Hamulec->GetVRP() * Bar);
}

void MaszynaBrake::setED(double share) {
    EDstate = share;
    Hamulec->SetED(share);
}

double MaszynaBrake::edPressure() const {
    return Hamulec->GetEDBCP() * Bar;
}
