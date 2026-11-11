/*  This file is part of TSRE5 (task maszyna 06).
 *
 *  It is a port of parts of MaSzyna's TMoverParameters
 *  (McZapkie/Mover.cpp at 66a940d4, https://github.com/eu07/maszyna:
 *  dizel_EngageSwitch, dizel_EngageChange, dizel_AutoGearCheck,
 *  dizel_fillcheck, dizel_Momentum, dizel_MomentumRetarder, the DieselEngine
 *  parts of TractionForce and dizel_Update, the .fiz keys they read), and so
 *  is under the Mozilla Public License 2.0, as MaSzyna's code is: this
 *  Source Code Form is subject to the terms of the Mozilla Public License,
 *  v. 2.0. If a copy of the MPL was not distributed with this file, You can
 *  obtain one at http://mozilla.org/MPL/2.0/.
 *
 *  MaSzyna's names and order are kept so the two can be compared; its units
 *  too inside (rev/s, km/h, N·m), SI at the edge. Its quirks are kept:
 *  TC_LR is the lockup's starting rate, Moment / AIM goes straight into
 *  rev/s, a controller position without fuel lets the engine stop.
 */

#include <tsre/maszyna/MaszynaDiesel.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <new>
#include <utility>
#include <vector>
#include <QRegularExpression>
#include <tsre/maszyna/MaszynaFiz.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/script/LuaApi.h>
#include <tsre/sim/LocoScript.h>
#include <tsre/trains/VehicleDefinition.h>

namespace MaszynaDiesel {

namespace {

// MaSzyna's MotorParametersArraySize (MOVER.h:103): gears 0 to 10.
constexpr int MotorParametersArraySize = 10;
// The longest step of MaSzyna's physics (drivermode.h: 10 ms).
constexpr double MaxStep = 0.01;

template <typename T> T sign(T x) { return x < T(0) ? T(-1) : x > T(0) ? T(1) : T(0); }
double square(double x) { return x * x; }
double safe_clamp(double value, double lo, double hi) {
    return hi < lo ? std::clamp(value, hi, lo) : std::clamp(value, lo, hi);
}

// TableInterpolation (Mover.cpp:257): linear, extended past both ends.
double TableInterpolation(const std::map<double, double> &Map, double Parameter) {
    if (Map.empty())
        return 0.0;
    if (Map.size() == 1)
        return Map.begin()->second;
    auto lower = Map.lower_bound(Parameter);
    auto upper = lower;
    if (lower != Map.begin())
        --lower;
    else
        ++upper;
    if (upper == Map.end()) {
        --lower;
        --upper;
    }
    const double ratio = (upper->second - lower->second) / (upper->first - lower->first);
    return lower->second + (Parameter - lower->first) * ratio;
}

// A gear (MaSzyna's TMotorParameters for a DieselEngine: mIsat its ratio,
// fi and mfi the speeds (km/h) down and up from it, fi0 and mfi0 those at no
// demand, Isat the fill held back).
struct Gear {
    double mIsat = 0.0, fi = 0.0, mfi = 0.0, fi0 = 0.0, mfi0 = 0.0, Isat = 0.0;
    bool AutoSwitch = false;
};

// A controller position's fill and clutch (DList's rows: Relay R Mn).
struct Position {
    double R = 0.0;
    int Mn = 0;
};

struct Config {
    // Cntrl.
    int MainCtrlPosNo = 0, ScndCtrlPosNo = 0, EIMCtrlType = 0, NoPowerPos = 0;
    bool AutoRelayFlag = false;
    // Engine: (rev/s, km/h, MaSzyna's units).
    double Ratio = 1.0, Efficiency = 1.0, WheelDiameter = 1.0;
    double dizel_nmin = 0.0, dizel_nmin_hdrive = 0.0, dizel_nmin_hdrive_factor = 0.0, dizel_nmin_retarder = 0.0;
    double dizel_nmax_cutoff = 0.0, dizel_nreg_acc = 0.0, dizel_AIM = 1.0;
    double engageupspeed = 0.5, engagedownspeed = 0.9;
    bool hydro_TC = false;
    double hydro_TC_TMMax = 0.0, hydro_TC_CouplingPoint = 0.0, hydro_TC_LockupTorque = 0.0,
           hydro_TC_LockupRate = 0.0, hydro_TC_FillRateInc = 0.0, hydro_TC_FillRateDec = 0.0,
           hydro_TC_TorqueInIn = 0.0, hydro_TC_TorqueInOut = 0.0, hydro_TC_TorqueOutOut = 0.0,
           hydro_TC_LockupSpeed = 0.0, hydro_TC_UnlockSpeed = 0.0, dizel_maxVelANS = 0.0;
    bool hydro_R = false, hydro_R_Clutch = false, hydro_R_WithIndividual = false;
    int hydro_R_Placement = 0;
    double hydro_R_TorqueInIn = 0.0, hydro_R_MaxTorque = 0.0, hydro_R_MaxPower = 0.0, hydro_R_FillRateInc = 0.0,
           hydro_R_FillRateDec = 0.0, hydro_R_MinVel = 0.0, hydro_R_EngageVel = 0.0, hydro_R_ClutchSpeed = 0.0;
    // DList: N·m, rev/s.
    double dizel_Mmax = 0.0, dizel_nMmax = 0.0, dizel_Mnmax = 0.0, dizel_nmax = 0.0, dizel_Mstand = 0.0;
    // MotorParamTable's header.
    double dizel_minVelfullengage = 0.0, dizel_engageDia = 0.0, dizel_engageMaxForce = 0.0,
           dizel_engagefriction = 0.0;
    std::vector<Position> RList;
    std::vector<Gear> MotorParam;
    std::map<double, double> dizel_Momentum_Table, dizel_vel2nmax_Table, hydro_TC_Table;

    const Gear &motor_param(int position) const {
        return MotorParam[size_t(std::clamp(position, 0, int(MotorParam.size()) - 1))];
    }
    const Position &rlist(int position) const {
        static const Position none;
        return RList.empty() ? none : RList[size_t(std::clamp(position, 0, int(RList.size()) - 1))];
    }
};

// The state kept between steps (the script's list of numbers).
struct State {
    double enrot = 0.0, dizel_n_old = 0.0, dizel_engage = 0.0, dizel_engagestate = 0.0, dizel_fill = 0.0,
           ScndCtrlActualPos = 0.0, dizel_automaticgearstatus = 0.0, hydro_TC_Fill = 0.0, hydro_TC_LockupRate = 0.0,
           hydro_TC_Lockup = 0.0, hydro_R_Fill = 0.0, hydro_R_n = 0.0, dizel_nreg_min = 0.0, dizel_nreg_max = 0.0,
           eAngle = 0.0, dizel_spinup = 0.0, mains = 0.0;
    static constexpr int Count = 17;
    double *at(int i) {
        double *fields[Count] = {&enrot, &dizel_n_old, &dizel_engage, &dizel_engagestate, &dizel_fill,
                                 &ScndCtrlActualPos, &dizel_automaticgearstatus, &hydro_TC_Fill,
                                 &hydro_TC_LockupRate, &hydro_TC_Lockup, &hydro_R_Fill, &hydro_R_n,
                                 &dizel_nreg_min, &dizel_nreg_max, &eAngle, &dizel_spinup, &mains};
        return fields[i];
    }
};

// One step's controls and what the step gives besides the force.
struct Controls {
    int MainCtrlPos = 0, ScndCtrlPos = 0, DirActive = 0;
    double eimic_real = 0.0, localBrake = 0.0;
    bool Mains = false, fuel = false;
};
struct Results {
    double Mm = 0.0, hydro_R_Torque = 0.0;
    bool stalled = false;
};

bool yes(const MaszynaFiz::Section *s, QStringView key) {
    return s != nullptr && s->text(key).trimmed().toLower() == QLatin1String("yes");
}
double num(const MaszynaFiz::Section *s, QStringView key, double otherwise = 0.0) {
    return s != nullptr ? s->number(key, otherwise) : otherwise;
}
double cell(const QVector<MaszynaFiz::Value> &row, int i, double otherwise = 0.0) {
    return i < row.size() && row[i].isNumber ? row[i].number : otherwise;
}

std::shared_ptr<const Config> readConfig(const VehicleDefinition &definition) {
    auto c = std::make_shared<Config>();
    c->MotorParam.resize(MotorParametersArraySize + 1);
    if (definition.maszyna == nullptr)
        return c;
    const MaszynaFiz &f = definition.maszyna->physics();
    const MaszynaFiz::Section *cntrl = f.section(u"Cntrl"), *engine = f.section(u"Engine"),
                              *wheels = f.section(u"Wheels"), *dlist = f.section(u"DList"),
                              *mpt = f.section(u"MotorParamTable"), *mpt0 = f.section(u"MotorParamTable0");
    c->MainCtrlPosNo = int(std::lround(num(cntrl, u"MCPN")));
    c->ScndCtrlPosNo = std::min(int(std::lround(num(cntrl, u"SCPN"))), MotorParametersArraySize);
    c->EIMCtrlType = int(std::lround(num(cntrl, u"EIMCtrlType")));
    c->AutoRelayFlag = cntrl != nullptr && cntrl->text(u"AutoRelay").trimmed().toLower() == QLatin1String("yes");
    // Trans=a:b, the motors turning b/a times the wheels (Transmision.Ratio).
    const QRegularExpressionMatch trans =
            QRegularExpression(QStringLiteral("([\\d.]+):([\\d.]+)")).match(engine ? engine->text(u"Trans") : QString());
    if (trans.hasMatch() && trans.captured(1).toDouble() > 0.0)
        c->Ratio = trans.captured(2).toDouble() / trans.captured(1).toDouble();
    c->Efficiency = num(engine, u"TransEff", 1.0);
    c->WheelDiameter = num(wheels, u"D", 1.0);
    // LoadFIZ_Engine's DieselEngine (Mover.cpp:11134): RPMs in rpm written.
    c->dizel_nmin = num(engine, u"nmin") / 60.0;
    c->dizel_nmin_hdrive = num(engine, u"nmin_hdrive") / 60.0;
    if (c->dizel_nmin_hdrive == 0.0)
        c->dizel_nmin_hdrive = c->dizel_nmin;
    c->dizel_nmin_hdrive_factor = num(engine, u"nmin_hdrive_factor") / 60.0;
    c->dizel_nmin_retarder = num(engine, u"nmin_retarder") / 60.0;
    if (c->dizel_nmin_retarder == 0.0)
        c->dizel_nmin_retarder = c->dizel_nmin;
    c->dizel_nmax_cutoff = num(engine, u"nmax_cutoff") / 60.0;
    c->dizel_nreg_acc = num(engine, u"nreg_acc") / 60.0;
    c->dizel_AIM = num(engine, u"AIM", 1.0);
    c->engageupspeed = num(engine, u"EUS", 0.5);
    c->engagedownspeed = num(engine, u"EDS", 0.9);
    c->hydro_TC = yes(engine, u"IsTC");
    if (c->hydro_TC) {
        c->hydro_TC_TMMax = num(engine, u"TC_TMMax");
        c->hydro_TC_CouplingPoint = num(engine, u"TC_CP");
        c->hydro_TC_LockupTorque = num(engine, u"TC_LT");
        c->hydro_TC_LockupRate = num(engine, u"TC_LR");
        c->hydro_TC_FillRateInc = num(engine, u"TC_FRI");
        c->hydro_TC_FillRateDec = num(engine, u"TC_FRD");
        c->hydro_TC_TorqueInIn = num(engine, u"TC_TII");
        c->hydro_TC_TorqueInOut = num(engine, u"TC_TIO");
        c->hydro_TC_TorqueOutOut = num(engine, u"TC_TOO");
        // TSRE's reader gives these two in m/s.
        c->hydro_TC_LockupSpeed = num(engine, u"TC_LS") * 3.6;
        c->hydro_TC_UnlockSpeed = num(engine, u"TC_ULS") * 3.6;
        c->dizel_maxVelANS = num(engine, u"MaxVelANS");
        c->hydro_R = yes(engine, u"IsRetarder");
        if (c->hydro_R) {
            c->hydro_R_Placement = int(std::lround(num(engine, u"R_Place")));
            c->hydro_R_TorqueInIn = num(engine, u"R_TII");
            c->hydro_R_MaxTorque = num(engine, u"R_MT");
            c->hydro_R_MaxPower = num(engine, u"R_MP");
            c->hydro_R_FillRateInc = num(engine, u"R_FRI");
            c->hydro_R_FillRateDec = num(engine, u"R_FRD");
            c->hydro_R_MinVel = num(engine, u"R_MinVel");
            c->hydro_R_EngageVel = num(engine, u"R_EngageVel");
            c->hydro_R_Clutch = yes(engine, u"R_IsClutch");
            c->hydro_R_ClutchSpeed = num(engine, u"R_ClutchSpeed");
            c->hydro_R_WithIndividual = yes(engine, u"R_WithIndividual");
        }
    }
    // LoadFIZ_DList: TSRE's reader gives nMmax and nmax in rpm.
    c->dizel_Mmax = num(dlist, u"Mmax");
    c->dizel_nMmax = num(dlist, u"nMmax") / 60.0;
    c->dizel_Mnmax = num(dlist, u"Mnmax");
    c->dizel_nmax = num(dlist, u"nmax") / 60.0;
    c->dizel_Mstand = num(dlist, u"Mstand");
    if (c->dizel_nMmax == c->dizel_nmax)
        c->dizel_nMmax = c->dizel_nmax - 1.0 / 60.0;
    if (dlist != nullptr)
        for (const auto &row : dlist->rows)
            c->RList.push_back({cell(row, 1), int(std::lround(cell(row, 2)))});
    // LoadFIZ_MotorParamTable's DieselEngine header, and its rows: idx mIsat
    // fi mfi [AutoSwitch]; MotorParamTable0's idx mIsat fi0 fi mfi0 mfi Isat
    // [AutoSwitch] (read after, as in the files).
    const MaszynaFiz::Section *header = mpt != nullptr ? mpt : mpt0;
    c->dizel_minVelfullengage = num(header, u"minVelfullengage");
    c->dizel_engageDia = num(header, u"engageDia");
    c->dizel_engageMaxForce = num(header, u"engageMaxForce");
    c->dizel_engagefriction = num(header, u"engagefriction");
    if (mpt != nullptr)
        for (const auto &row : mpt->rows) {
            const int idx = int(std::lround(cell(row, 0, -1.0)));
            if (idx < 0 || idx > MotorParametersArraySize || row.size() < 4)
                continue;
            Gear &g = c->MotorParam[size_t(idx)];
            g = Gear();
            g.mIsat = cell(row, 1);
            g.fi = cell(row, 2);
            g.mfi = cell(row, 3);
            g.AutoSwitch = std::lround(cell(row, 4)) == 1;
        }
    if (mpt0 != nullptr)
        for (const auto &row : mpt0->rows) {
            const int idx = int(std::lround(cell(row, 0, -1.0)));
            if (idx < 0 || idx > MotorParametersArraySize || row.size() < 7)
                continue;
            Gear &g = c->MotorParam[size_t(idx)];
            g.mIsat = cell(row, 1);
            g.fi0 = cell(row, 2);
            g.fi = cell(row, 3);
            g.mfi0 = cell(row, 4);
            g.mfi = cell(row, 5);
            g.Isat = cell(row, 6);
            g.AutoSwitch = std::lround(cell(row, 7)) == 1;
        }
    // DMList (rpm, N·m), V2NList (km/h, here m/s; rpm), HTCList.
    if (const MaszynaFiz::Section *s = f.section(u"DMList"))
        for (const auto &row : s->rows)
            c->dizel_Momentum_Table.emplace(cell(row, 0) / 60.0, cell(row, 1));
    if (const MaszynaFiz::Section *s = f.section(u"V2NList"))
        for (const auto &row : s->rows)
            c->dizel_vel2nmax_Table.emplace(cell(row, 0) * 3.6, cell(row, 1) / 60.0);
    if (const MaszynaFiz::Section *s = f.section(u"HTCList"))
        for (const auto &row : s->rows)
            c->hydro_TC_Table.emplace(cell(row, 0), cell(row, 1));
    // MainCtrlNoPowerPos (Mover.cpp:2650): UniCtrlNoPowerPos for type 3,
    // the position before the first with traction (12043).
    if (c->EIMCtrlType == 1 || c->EIMCtrlType == 2) {
        c->NoPowerPos = 3;
    } else if (c->EIMCtrlType == 3) {
        if (const MaszynaFiz::Section *s = f.section(u"UCList"))
            for (int idx = 0; idx < c->MainCtrlPosNo && idx < s->rows.size(); ++idx)
                if (cell(s->rows[idx], 3) > 0.0) {
                    c->NoPowerPos = std::max(0, idx - 1);
                    break;
                }
    }
    return c;
}

// The diesel's step (MaSzyna's), in MaSzyna's units.
struct Diesel {
    const Config &c;
    State &s;
    const Controls &k;
    Results &r;
    double Vel = 0.0; // km/h

    bool IsMainCtrlNoPowerPos() const { return k.MainCtrlPos <= c.NoPowerPos; }
    int MainCtrlPowerPos() const { return k.MainCtrlPos - c.NoPowerPos; }
    int gear() const { return int(std::lround(s.ScndCtrlActualPos)); }

    bool dizel_EngageSwitch(double state) {
        if (state <= 1 && state >= 0 && state != s.dizel_engagestate) {
            s.dizel_engagestate = state;
            return true;
        }
        return false;
    }

    bool dizel_EngageChange(double dt) {
        bool DEC = false;
        const double engagespeed = s.dizel_engage - s.dizel_engagestate > 0 ? c.engagedownspeed : c.engageupspeed;
        if (dt > 0.2)
            dt = 0.1;
        if (std::abs(s.dizel_engage - s.dizel_engagestate) < 0.11) {
            if (s.dizel_engage != s.dizel_engagestate) {
                DEC = true;
                s.dizel_engage = s.dizel_engagestate;
            }
        } else {
            s.dizel_engage = s.dizel_engage + engagespeed * dt * (s.dizel_engagestate - s.dizel_engage);
        }
        return DEC;
    }

    bool dizel_AutoGearCheck() {
        bool OK = false;
        const Gear &g = c.motor_param(gear());
        const double VelUp = g.mfi0 != 0.0 ? g.mfi0 + (g.mfi - g.mfi0) * std::max(0.0, k.eimic_real) : g.mfi;
        const double VelDown = g.fi0 != 0.0 && k.eimic_real <= 0.0 ? g.fi0 : g.fi;
        if (g.AutoSwitch && k.Mains) {
            if (c.rlist(k.MainCtrlPos).Mn == 0 && !c.hydro_TC) {
                if (s.dizel_engagestate > 0)
                    dizel_EngageSwitch(0);
                if (IsMainCtrlNoPowerPos() && gear() > 0)
                    s.dizel_automaticgearstatus = -1;
            } else if (g.AutoSwitch && s.dizel_automaticgearstatus == 0) {
                if (Vel > VelUp) {
                    if (gear() < c.ScndCtrlPosNo) {
                        s.dizel_automaticgearstatus = 1;
                        OK = true;
                    }
                } else if (Vel < VelDown) {
                    if (gear() > 0) {
                        s.dizel_automaticgearstatus = -1;
                        OK = true;
                    }
                }
            }
            if (s.dizel_engage < 0.1 && s.dizel_automaticgearstatus != 0) {
                s.ScndCtrlActualPos += s.dizel_automaticgearstatus == 1 ? 1 : -1;
                s.dizel_automaticgearstatus = 0;
                dizel_EngageSwitch(1.0);
                OK = true;
            }
        }
        if (k.Mains) {
            const Position &p = c.rlist(k.MainCtrlPos);
            if (c.EIMCtrlType > 0) {
                if (s.dizel_automaticgearstatus == 0) {
                    if ((c.hydro_TC && s.hydro_TC_Fill > 0.01) || k.eimic_real > 0.005)
                        dizel_EngageSwitch(1.0);
                    else if (Vel > c.hydro_R_EngageVel && c.hydro_R && s.hydro_R_Fill > 0.01)
                        dizel_EngageSwitch(0.5);
                    else
                        dizel_EngageSwitch(0.0);
                } else {
                    dizel_EngageSwitch(0.0);
                }
            } else if (s.dizel_automaticgearstatus == 0) {
                switch (p.Mn) {
                case 1:
                    dizel_EngageSwitch(0.5);
                    break;
                case 2:
                    dizel_EngageSwitch(1.0);
                    break;
                case 3:
                    dizel_EngageSwitch(Vel > c.dizel_minVelfullengage ? 1.0 : 0.5);
                    break;
                case 4:
                    dizel_EngageSwitch(Vel > c.dizel_minVelfullengage ? 1.0 : 0.66);
                    break;
                case 5:
                    dizel_EngageSwitch(Vel > c.dizel_minVelfullengage ? 1.0 : 0.35 * (1 + p.R) * p.R);
                    break;
                default:
                    dizel_EngageSwitch(c.hydro_TC && s.hydro_TC_Fill > 0.01 ? 1.0 : 0.0);
                }
            } else {
                dizel_EngageSwitch(0.0);
            }
            if (!(c.motor_param(gear()).mIsat > 0))
                dizel_EngageSwitch(0.0);
            if (!c.AutoRelayFlag)
                s.ScndCtrlActualPos = std::clamp(k.ScndCtrlPos, 0, c.ScndCtrlPosNo);
        }
        return OK;
    }

    double dizel_fillcheck(int mcp, double dt) {
        double realfill = 0.0;
        if (k.Mains && c.MainCtrlPosNo > 0 && k.fuel) {
            // The ignition's enrichment never comes (MaSzyna clears the flag
            // before this is reached).
            if (c.EIMCtrlType > 0) {
                realfill = std::max(0.0, std::min(k.eimic_real, 1 - c.motor_param(gear()).Isat));
                if (k.eimic_real > 0.005 && s.hydro_TC_Lockup == 0.0) {
                    s.dizel_nreg_min = std::min(s.dizel_nreg_min + 2.5 * dt,
                                                c.dizel_nmin_hdrive + k.eimic_real * c.dizel_nmin_hdrive_factor);
                } else {
                    if (Vel < c.hydro_R_EngageVel && c.hydro_R && s.hydro_R_Fill > 0.01)
                        s.dizel_nreg_min = std::min(s.dizel_nreg_min + 5.0 * dt, c.dizel_nmin_retarder);
                    else
                        s.dizel_nreg_min = c.dizel_nmin;
                }
                if (!c.dizel_vel2nmax_Table.empty() && s.hydro_TC_Lockup == 0.0)
                    s.dizel_nreg_max = std::min(std::min(s.dizel_nreg_max, s.enrot) + c.dizel_nreg_acc * dt,
                                                TableInterpolation(c.dizel_vel2nmax_Table, Vel));
                else
                    s.dizel_nreg_max = c.dizel_nmax;
            } else {
                s.dizel_nreg_max = c.dizel_nmax;
                realfill = c.rlist(mcp).R;
            }
            if (c.dizel_nmax_cutoff > 0) {
                double nreg = 0.0;
                if (c.EIMCtrlType > 0) {
                    nreg = k.eimic_real > 0.005 ? s.dizel_nreg_max : c.dizel_nmin;
                } else {
                    switch (c.rlist(k.MainCtrlPos).Mn) {
                    case 0:
                    case 1:
                        nreg = c.dizel_nmin;
                        break;
                    case 2:
                        nreg = s.dizel_automaticgearstatus == 0 ? s.dizel_nreg_max : c.dizel_nmin;
                        break;
                    case 3:
                        nreg = s.dizel_automaticgearstatus == 0 && Vel > c.dizel_minVelfullengage ? s.dizel_nreg_max
                                                                                                    : c.dizel_nmin;
                        break;
                    case 4:
                        nreg = s.dizel_automaticgearstatus == 0 && Vel > c.dizel_minVelfullengage
                                       ? c.dizel_nmax
                                       : c.dizel_nmin * 0.75 + s.dizel_nreg_max * 0.25;
                        break;
                    case 5:
                        nreg = Vel > c.dizel_minVelfullengage
                                       ? s.dizel_nreg_max
                                       : c.dizel_nmin + 0.8 * (s.dizel_nreg_max - c.dizel_nmin) * c.rlist(mcp).R;
                        break;
                    default:
                        realfill = 0;
                        break;
                    }
                }
                if (s.enrot > nreg)
                    realfill = 0;
                if (s.enrot < s.dizel_nreg_min && c.rlist(mcp).R > 0.001)
                    realfill = 1;
            }
        }
        return std::clamp(realfill, 0.0, 1.0);
    }

    double dizel_MomentumRetarder(double n, double dt) {
        double RetarderRequest = k.Mains ? std::max(0.0, -k.eimic_real) : 0;
        if (c.hydro_R_WithIndividual)
            RetarderRequest = k.localBrake;
        if (Vel < c.hydro_R_MinVel)
            RetarderRequest = 0;
        if (c.hydro_R_Placement == 2 && s.enrot < c.dizel_nmin)
            RetarderRequest = 0;
        const bool hydro_R_ClutchActive = !c.hydro_R_Clutch || RetarderRequest > 0;
        if (!c.hydro_R_Clutch || (hydro_R_ClutchActive && c.hydro_R_ClutchSpeed == 0))
            s.hydro_R_n = n * 60;
        else if (hydro_R_ClutchActive)
            s.hydro_R_n = sign(n) * std::min(std::abs(s.hydro_R_n + c.hydro_R_ClutchSpeed * dt), std::abs(n * 60));
        else
            s.hydro_R_n = 0;
        n = s.hydro_R_n / 60.0;
        if (s.hydro_R_Fill < RetarderRequest)
            s.hydro_R_Fill = std::min(s.hydro_R_Fill + c.hydro_R_FillRateInc * dt, RetarderRequest);
        else
            s.hydro_R_Fill = std::max(s.hydro_R_Fill - c.hydro_R_FillRateDec * dt, RetarderRequest);
        double Moment = c.hydro_R_MaxTorque;
        if (const double pwr = Moment * std::abs(n) * M_PI * 2 * 0.001; pwr > c.hydro_R_MaxPower)
            Moment = Moment * c.hydro_R_MaxPower / pwr;
        const double moment_in = n * n * c.hydro_R_TorqueInIn;
        Moment = std::min(moment_in, Moment * s.hydro_R_Fill);
        r.hydro_R_Torque = Moment;
        return Moment;
    }

    double dizel_Momentum(double Fill, double n, double dt) {
        double Moment = 0, enMoment = 0, gearMoment = 0, eps = 0, newn = 0;
        double TorqueH = 0, TorqueL = 0, TorqueC = 0, hydro_TC_TorqueIn = 0;
        if (c.motor_param(gear()).mIsat < 0.001 || k.DirActive == 0)
            n = s.enrot;
        const double friction = c.dizel_engagefriction;
        const double hydro_TC_nIn = s.enrot;
        double hydro_TC_nOut = s.dizel_n_old;
        if (s.enrot > 0) {
            if (c.dizel_Momentum_Table.size() > 1)
                Moment = TableInterpolation(c.dizel_Momentum_Table, s.enrot) * Fill - c.dizel_Mstand;
            else
                Moment = (c.dizel_Mmax - (c.dizel_Mmax - c.dizel_Mnmax) *
                                                 square((s.enrot - c.dizel_nMmax) / (c.dizel_nMmax - c.dizel_nmax))) *
                                 Fill -
                         c.dizel_Mstand;
            r.Mm = Moment;
            if (c.hydro_R && c.hydro_R_Placement == 2)
                Moment -= dizel_MomentumRetarder(s.enrot, dt);
        } else {
            Moment = -c.dizel_Mstand;
        }
        if (s.enrot < c.dizel_nmin / 10.0 && s.eAngle < M_PI / 2)
            Moment -= c.dizel_Mstand;
        if (s.dizel_spinup != 0.0)
            Moment += c.dizel_Mstand / (0.3 + std::max(0.0, s.enrot / c.dizel_nmin)); // the starter
        if (c.hydro_TC) {
            const bool IsPower = c.EIMCtrlType > 0 ? k.eimic_real > 0.005 : MainCtrlPowerPos() > 0;
            if (IsPower && k.Mains && s.enrot > c.dizel_nmin * 0.9)
                s.hydro_TC_Fill += c.hydro_TC_FillRateInc * dt;
            if ((!IsPower && Vel < c.dizel_maxVelANS) || !k.Mains || s.enrot < c.dizel_nmin * 0.8)
                s.hydro_TC_Fill -= c.hydro_TC_FillRateDec * dt;
            s.hydro_TC_Fill = std::clamp(s.hydro_TC_Fill, 0.0, 1.0);
            if (Vel > c.hydro_TC_LockupSpeed && k.Mains && s.enrot > 0.9 * c.dizel_nmin && IsPower) {
                s.hydro_TC_Lockup = 1.0;
                s.hydro_TC_LockupRate += c.hydro_TC_FillRateInc * dt;
            }
            if (Vel < (IsPower ? c.hydro_TC_LockupSpeed : c.hydro_TC_UnlockSpeed) || !k.Mains ||
                s.enrot < 0.8 * c.dizel_nmin) {
                s.hydro_TC_Lockup = 0.0;
                s.hydro_TC_LockupRate -= c.hydro_TC_FillRateDec * dt;
            }
            s.hydro_TC_LockupRate = std::clamp(s.hydro_TC_LockupRate, 0.0, 1.0);
        } else {
            s.hydro_TC_Fill = 0.0;
            s.hydro_TC_LockupRate = 0.0;
        }
        TorqueC = c.dizel_engageMaxForce * s.dizel_engage * c.dizel_engageDia * friction;
        if (c.hydro_TC) {
            double HydroTorque = 0;
            HydroTorque += hydro_TC_nIn * hydro_TC_nIn * c.hydro_TC_TorqueInIn;
            HydroTorque += (hydro_TC_nIn - hydro_TC_nOut) * c.hydro_TC_TorqueInOut;
            HydroTorque += hydro_TC_nOut * hydro_TC_nOut * c.hydro_TC_TorqueOutOut;
            const double nOut2In = hydro_TC_nOut / std::max(0.01, hydro_TC_nIn);
            double hydro_TC_TMRatio = 0, hydro_TC_TorqueOut = 0;
            if (c.hydro_TC_Table.size() > 1) {
                hydro_TC_TMRatio = TableInterpolation(c.hydro_TC_Table, nOut2In);
                hydro_TC_TorqueOut = HydroTorque * s.hydro_TC_Fill * hydro_TC_TMRatio;
                hydro_TC_TorqueIn = HydroTorque * s.hydro_TC_Fill * std::min(1.0, hydro_TC_TMRatio);
            } else if (nOut2In < c.hydro_TC_CouplingPoint) {
                hydro_TC_TMRatio = 1 + (c.hydro_TC_TMMax - 1) * square(1 - nOut2In / c.hydro_TC_CouplingPoint);
                hydro_TC_TorqueIn = HydroTorque * s.hydro_TC_Fill;
                hydro_TC_TorqueOut = HydroTorque * s.hydro_TC_Fill * hydro_TC_TMRatio;
            } else {
                hydro_TC_TMRatio = (1 - nOut2In) / (1 - c.hydro_TC_CouplingPoint);
                hydro_TC_TorqueIn = HydroTorque * s.hydro_TC_Fill * hydro_TC_TMRatio;
                hydro_TC_TorqueOut = HydroTorque * s.hydro_TC_Fill * hydro_TC_TMRatio;
            }
            TorqueH = hydro_TC_TorqueOut;
            TorqueL = c.hydro_TC_LockupTorque * s.hydro_TC_LockupRate;
        } else {
            TorqueH = 0; // no converter, no torque through it
            TorqueL = 1 + TorqueC * 2; // fixed: the clutch alone
        }
        if (std::abs(Moment) > std::min(TorqueC, TorqueL + std::abs(hydro_TC_TorqueIn)) ||
            std::abs(s.dizel_n_old - s.enrot) > 0.1) {
            // Slipping: speeds apart, or more torque than it holds.
            const double dizel_engagedeltaomega = s.enrot - s.dizel_n_old;
            if (TorqueC > TorqueL) {
                if (TorqueC > TorqueL + std::abs(TorqueH)) {
                    hydro_TC_nOut = n;
                    gearMoment = TorqueL + std::abs(TorqueH) * sign(dizel_engagedeltaomega);
                    enMoment = Moment - (TorqueL + std::abs(hydro_TC_TorqueIn)) * sign(dizel_engagedeltaomega);
                } else {
                    hydro_TC_nOut = s.enrot - (n - s.enrot) * (TorqueC - TorqueL) / TorqueH;
                    gearMoment = TorqueC * sign(dizel_engagedeltaomega);
                    enMoment = Moment - gearMoment;
                }
            } else {
                hydro_TC_nOut = s.enrot;
                gearMoment = TorqueC * sign(dizel_engagedeltaomega);
                enMoment = Moment - gearMoment;
            }
            eps = enMoment / c.dizel_AIM;
            newn = s.enrot + eps * dt;
            if ((newn - n) * (s.enrot - s.dizel_n_old) < 0 && TorqueC > 0.1) // slipped, and stopped slipping
                newn = n;
            if (newn * s.enrot <= 0 && eps * s.enrot < 0) // through zero revolutions
                newn = 0;
            s.enrot = newn;
        } else {
            // Not slipping (yet).
            gearMoment = Moment;
            const double held = std::min(TorqueC, TorqueL + std::abs(hydro_TC_TorqueIn));
            const double enrot_min = s.enrot - (held - Moment) / c.dizel_AIM * dt;
            const double enrot_max = s.enrot + (held + Moment) / c.dizel_AIM * dt;
            s.enrot = safe_clamp(n, enrot_min, enrot_max);
        }
        if (c.hydro_R && c.hydro_R_Placement == 1)
            gearMoment -= dizel_MomentumRetarder(hydro_TC_nOut, dt);
        if (s.enrot <= 0 && s.dizel_spinup == 0.0) {
            if (k.Mains)
                r.stalled = true;
            s.enrot = 0;
        }
        s.dizel_n_old = n;
        return gearMoment;
    }

    // One step of at most 10 ms: TractionForce's DieselEngine part (the
    // force, N, + toward the reverser's way), then dizel_Update's.
    double step(double nrot, double dt) {
        // dizel_Update: the ignition (Mains on) and the starter's spin-up.
        if (k.Mains && s.mains == 0.0) {
            s.enrot = std::max(s.enrot, 0.35 * c.dizel_nmin);
            s.dizel_spinup = 1.0;
        }
        s.mains = k.Mains ? 1.0 : 0.0;
        s.dizel_spinup = s.dizel_spinup != 0.0 && k.Mains && s.enrot < 0.95 * c.dizel_nmin ? 1.0 : 0.0;
        // TractionForce.
        const double dtrans = c.Ratio * c.motor_param(gear()).mIsat;
        const double dmoment = dizel_Momentum(s.dizel_fill, dtrans * nrot * k.DirActive, dt);
        double Mw = dmoment * dtrans * c.Efficiency;
        if (c.hydro_R && c.hydro_R_Placement == 0)
            Mw -= dizel_MomentumRetarder(nrot * c.Ratio, dt) * c.Ratio * c.Efficiency;
        const double Ft = Mw * 2.0 / c.WheelDiameter * k.DirActive;
        s.eAngle += s.enrot * dt;
        if (s.eAngle > M_PI * 2.0)
            s.eAngle = std::fmod(s.eAngle, M_PI * 2.0);
        // dizel_Update.
        dizel_EngageChange(dt);
        dizel_AutoGearCheck();
        constexpr double fillspeed = 2;
        s.dizel_fill = s.dizel_fill + fillspeed * dt * (dizel_fillcheck(k.MainCtrlPos, dt) - s.dizel_fill);
        return Ft;
    }
};

const char *ConfigMeta = "maszyna.diesel.config";
using ConfigPointer = std::shared_ptr<const Config>;

int releaseConfig(lua_State *L) {
    static_cast<ConfigPointer *>(luaL_checkudata(L, 1, ConfigMeta))->~ConfigPointer();
    return 0;
}

// The config of the definition at index 1, read once per definition and
// kept in the functions' first upvalue (as tsre.steam's).
const Config &configAt(lua_State *L) {
    lua_pushvalue(L, 1);
    lua_rawget(L, lua_upvalueindex(1));
    if (lua_type(L, -1) == LUA_TUSERDATA) {
        const Config &c = **static_cast<ConfigPointer *>(lua_touserdata(L, -1));
        lua_pop(L, 1);
        return c;
    }
    lua_pop(L, 1);
    ConfigPointer config = readConfig(Sim::LocoScripts::definitionAt(L, 1));
    auto *slot = static_cast<ConfigPointer *>(lua_newuserdatauv(L, sizeof(ConfigPointer), 0));
    new (slot) ConfigPointer(std::move(config));
    if (luaL_newmetatable(L, ConfigMeta)) {
        lua_pushcfunction(L, releaseConfig);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    lua_pushvalue(L, 1);
    lua_pushvalue(L, -2);
    lua_rawset(L, lua_upvalueindex(1));
    const Config &c = **slot;
    lua_pop(L, 1);
    return c;
}

double number(lua_State *L, int table, const char *name, double otherwise) {
    lua_getfield(L, table, name);
    int ok = 0;
    const double value = lua_tonumberx(L, -1, &ok);
    lua_pop(L, 1);
    return ok && std::isfinite(value) ? value : otherwise;
}

bool flag(lua_State *L, int table, const char *name, bool otherwise = false) {
    lua_getfield(L, table, name);
    const bool value = lua_isnil(L, -1) ? otherwise : lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return value;
}

// The first position with fuel and no clutch (DList: R over 0, Mn 0), the
// engine's idle; the no-power position with a digital controller.
int idlePosition(const Config &c) {
    if (c.EIMCtrlType > 0)
        return c.NoPowerPos;
    for (int i = 0; i < int(c.RList.size()); ++i)
        if (c.RList[size_t(i)].R > 0.001 && c.RList[size_t(i)].Mn == 0)
            return i;
    return 0;
}

// diesel.step(vehicle, dt, state, controls, speed)
int step(lua_State *L) {
    const Config &c = configAt(L);
    const double dt = luaL_checknumber(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    luaL_checktype(L, 4, LUA_TTABLE);
    const double speed = luaL_checknumber(L, 5);

    Controls k;
    k.MainCtrlPos = int(std::lround(number(L, 4, "position", 0.0)));
    k.ScndCtrlPos = int(std::lround(number(L, 4, "gear", 0.0)));
    k.DirActive = int(sign(number(L, 4, "direction", 0.0)));
    k.eimic_real = number(L, 4, "eimic", 0.0);
    k.localBrake = number(L, 4, "localBrake", 0.0);
    k.Mains = flag(L, 4, "mains");
    k.fuel = flag(L, 4, "fuel", true);
    // Not driven (an AI train or a heard shadow): MaSzyna's drivers keep
    // the controller at idle at least, never on a position without fuel.
    if (!flag(L, 4, "cab", true))
        k.MainCtrlPos = std::max(k.MainCtrlPos, idlePosition(c));

    State s;
    if (int(lua_rawlen(L, 3)) == State::Count) {
        for (int i = 0; i < State::Count; ++i) {
            lua_rawgeti(L, 3, i + 1);
            *s.at(i) = lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    } else {
        // A start (a hot one at the engine's RPM): no gear, the clutch open.
        s.enrot = number(L, 4, "rpm", 0.0) / 60.0;
        s.dizel_nreg_min = c.dizel_nmin * 0.98;
        s.dizel_nreg_max = c.dizel_nmax;
        s.hydro_TC_LockupRate = c.hydro_TC_LockupRate;
        s.mains = k.Mains ? 1.0 : 0.0;
        s.dizel_fill = c.rlist(k.MainCtrlPos).R;
    }

    // MaSzyna's units: the wheels' rev/s, km/h; sub-steps of 10 ms or less.
    const double nrot = speed / (M_PI * c.WheelDiameter);
    Results r;
    Diesel d{c, s, k, r};
    d.Vel = std::abs(speed) * 3.6;
    const int steps = std::max(1, int(std::ceil(dt / MaxStep - 1e-9)));
    const double h = dt / steps;
    double force = 0.0;
    for (int i = 0; i < steps; ++i)
        force += d.step(nrot, h);
    force /= steps;

    for (int i = 0; i < State::Count; ++i) {
        lua_pushnumber(L, std::isfinite(*s.at(i)) ? *s.at(i) : 0.0);
        lua_rawseti(L, 3, i + 1);
    }

    lua_pushnumber(L, std::isfinite(force) ? force : 0.0);
    lua_createtable(L, 0, 9);
    const std::pair<const char *, double> values[] = {
            {"rpm", s.enrot * 60.0},
            {"fill", s.dizel_fill},
            {"torque", r.Mm},
            {"gear", s.ScndCtrlActualPos},
            {"clutch", s.dizel_engage},
            {"converter", s.hydro_TC_Fill},
            {"lockup", s.hydro_TC_LockupRate},
            {"retarder", s.hydro_R_Fill}};
    for (const auto &[name, value] : values) {
        lua_pushnumber(L, std::isfinite(value) ? value : 0.0);
        lua_setfield(L, -2, name);
    }
    lua_pushboolean(L, r.stalled);
    lua_setfield(L, -2, "stalled");
    return 2;
}

}

int openModule(lua_State *L) {
    lua_createtable(L, 0, 1);
    // The configs by definition, shared by its functions.
    lua_newtable(L);
    const std::pair<const char *, lua_CFunction> functions[] = {{"step", step}};
    for (const auto &[name, function] : functions) {
        lua_pushvalue(L, -1);
        lua_pushcclosure(L, function, 1);
        lua_setfield(L, -3, name);
    }
    lua_pop(L, 1);
    return 1;
}

}
