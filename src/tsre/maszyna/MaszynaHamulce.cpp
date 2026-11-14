/*  This file is part of TSRE5 (task driving 10, step 4).
 *
 *  It is a port of MaSzyna's brake distributors, block friction and
 *  driver's handles (McZapkie/hamulce.cpp, McZapkie/Oerlikon_ESt.cpp
 *  and McZapkie/friction.cpp at 66a940d4, https://github.com/eu07/maszyna;
 *  (C) 2007-2014 Maciej Cierniak, youBy), and so is under the Mozilla Public
 *  License 2.0, as MaSzyna's code is: this Source Code Form is subject to
 *  the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
 *  not distributed with this file, You can obtain one at
 *  http://mozilla.org/MPL/2.0/.
 *
 *  Line for line MaSzyna's, its quirks kept (the reservoirs' flows applied
 *  at Act, a relay's pending flow read before it, LSt's high pressure flow
 *  acted on at the next step). abs() on a double is the double one, as in
 *  MaSzyna's build (std::abs here).
 */

#include <tsre/maszyna/MaszynaHamulce.h>
#include <algorithm>
#include <cmath>
#include <typeinfo>

namespace MaszynaHamulce {

namespace {

bool contains(std::string const &String, char const *Substring) {
    return String.find(Substring) != std::string::npos;
}

} // namespace

// ---- friction.cpp ----

double TFricMat::GetFC(double N, double Vel) {
    return 1;
}

double TP10Bg::GetFC(double N, double Vel) {
    return std::exp(-0.022 * N) * (0.19 - 0.095 * std::exp(-Vel * 1.0 / 25.7)) + 0.384 * std::exp(-Vel * 1.0 / 25.7) - 0.028;
}

double TP10Bgu::GetFC(double N, double Vel) {
    return std::exp(-0.017 * N) * (0.18 - 0.09 * std::exp(-Vel * 1.0 / 25.7)) + 0.381 * std::exp(-Vel * 1.0 / 25.7) - 0.022;
}

double TP10yBg::GetFC(double N, double Vel) {
    double A;
    double C;
    double u0;
    double V0;

    A = 2.135 * std::exp(-0.03726 * N) - 0.5;
    C = 0.353 - A * 0.029;
    u0 = 0.41 - C;
    V0 = 25.7 + 20 * A;
    return u0 + C * std::exp(-Vel * 1.0 / V0);
}

double TP10yBgu::GetFC(double N, double Vel) {
    double A;
    double C;
    double u0;
    double V0;

    A = 1.68 * std::exp(-0.02735 * N) - 0.5;
    C = 0.353 - A * 0.044;
    u0 = 0.41 - C;
    V0 = 25.7 + 21 * A;
    return u0 + C * std::exp(-Vel * 1.0 / V0);
}

double TP10::GetFC(double N, double Vel) {
    return 0.60 * ((1.6 * N + 100) * 1.0 / (8.0 * N + 100)) * ((Vel + 100) * 1.0 / (5 * Vel + 100));
}

double TFR513::GetFC(double N, double Vel) {
    return 0.3 - Vel * 0.00081;
}

double TCosid::GetFC(double N, double Vel) {
    return 0.27;
}

double TDisk1::GetFC(double N, double Vel) {
    return 0.2375 + 0.000885 * N - 0.000345 * N * N;
}

double TDisk2::GetFC(double N, double Vel) {
    return 0.27;
}

double TFR510::GetFC(double N, double Vel) {
    return 0.15;
}

// ---- hamulce.cpp ----

static double const DPL = 0.25;

double PF(double const P1, double const P2, double const S, double const DP) {
    double const PH = std::max(P1, P2) + 1.0; // wyzsze cisnienie absolutne
    double const PL = P1 + P2 - PH + 2.0;     // nizsze cisnienie absolutne
    double const sg = PL / PH;                // bezwymiarowy stosunek cisnien
    double const FM = PH * 197.0 * S * Sign(P2 - P1); // najwyzszy mozliwy przeplyw, wraz z kierunkiem
    if (sg > 0.5) {                           // jesli ponizej stosunku krytycznego
        if (PH - PL < DP)                     // niewielka roznica cisnien
            return (1.0 - sg) / DPL * FM * 2.0 * std::sqrt(DP * (PH - DP));
        else
            return FM * 2.0 * std::sqrt(sg * (1.0 - sg));
    } else // powyzej stosunku krytycznego
        return FM;
}

double PF1(double const P1, double const P2, double const S) {
    static double const DPS = 0.001;

    double const PH = std::max(P1, P2) + 1.0;
    double const PL = P1 + P2 - PH + 2.0;
    double const sg = PL / PH;
    double const FM = PH * 197.0 * S * Sign(P2 - P1);
    if (sg > 0.5) {
        if (sg < DPS)
            return (1.0 - sg) / DPS * FM * 2.0 * std::sqrt(DPS * (1.0 - DPS));
        else
            return FM * 2.0 * std::sqrt(sg * (1.0 - sg));
    } else
        return FM;
}

double PFVa(double PH, double PL, double const S, double LIM, double const DP) {
    if (LIM > PL) {
        LIM = LIM + 1;
        PH = PH + 1; // wyzsze cisnienie absolutne
        PL = PL + 1; // nizsze cisnienie absolutne
        double sg = std::min(1.0, PL / PH);
        double FM = PH * 197 * S;
        if (LIM - PL < DP)
            FM = FM * (LIM - PL) / DP; // jesli jestesmy przy nastawieniu, to zawor sie przymyka
        if (sg > 0.5) {
            if (PH - PL < DPL)
                return (PH - PL) / DPL * FM * 2 * std::sqrt(sg * (1 - sg));
            else
                return FM * 2 * std::sqrt(sg * (1 - sg));
        } else
            return FM;
    } else
        return 0;
}

double PFVd(double PH, double PL, double const S, double LIM, double const DP) {
    if (LIM < PH) {
        LIM = LIM + 1;
        PH = PH + 1.0;
        PL = PL + 1.0;
        double sg = std::min(1.0, PL / PH);
        double FM = PH * 197.0 * S;
        if (PH - LIM < 0.1)
            FM = FM * (PH - LIM) / DP;
        if (sg > 0.5) {
            if (PH - PL < DPL)
                return (PH - PL) / DPL * FM * 2.0 * std::sqrt(sg * (1.0 - sg));
            else
                return FM * 2.0 * std::sqrt(sg * (1.0 - sg));
        } else
            return FM;
    } else
        return 0;
}

//---ZBIORNIKI---

double TReservoir::pa() {
    return 0.1 * Vol / Cap;
}

double TReservoir::P() {
    return Vol / Cap;
}

void TReservoir::Flow(double dv) {
    dVol = dVol + dv;
}

void TReservoir::Act() {
    Vol = std::max(0.0, Vol + dVol);
    dVol = 0;
}

void TReservoir::CreateCap(double Capacity) {
    Cap = Capacity;
}

void TReservoir::CreatePress(double Press) {
    Vol = Cap * Press;
    dVol = 0;
}

//---SILOWNIK---

double TBrakeCyl::pa() {
    return P() * 0.1;
}

// The older curve (hamulce.cpp's "STARA WERSJA").
double TBrakeCyl::P() {
    static double const VS = 0.005;
    static double const pS = 0.05;
    static double const VD = 1.10;
    static double const cD = 1;
    static double const pD = VD - cD;

    double VtoC = Cap > 0.0 ? Vol / Cap : 0.0;
    if (VtoC < VS)
        return VtoC * pS / VS; // objetosc szkodliwa
    else if (VtoC > VD)
        return VtoC - cD; // caly silownik;
    else
        return pS + (VtoC - VS) / (VD - VS) * (pD - pS); // wysuwanie tloka
}

//---HAMULEC---

TBrake::TBrake(double i_mbp, double i_bcr, double i_bcd, double i_brc, int i_bcn, int i_BD, int i_mat, int i_ba, int i_nbpa)
    : BCN(i_bcn), BCM(1), BCA(i_bcn * i_bcr * i_bcr * M_PI), BrakeDelays(i_BD), MaxBP(i_mbp), BA(i_ba), NBpA(i_nbpa),
      SizeBR(i_brc * 0.0128) {
    BrakeDelayFlag = bdelay_P;
    SizeBC = i_bcn * i_bcr * i_bcr * i_bcd * 210.88 * MaxBP / 4.2;

    BrakeCyl = std::make_shared<TBrakeCyl>();
    BrakeRes = std::make_shared<TReservoir>();
    ValveRes = std::make_shared<TReservoir>();

    // tworzenie zbiornikow
    BrakeCyl->CreateCap(i_bcd * BCA * 1000);
    BrakeRes->CreateCap(i_brc);
    ValveRes->CreateCap(0.25);

    // materialy cierne
    i_mat = i_mat & (255 - bp_MHS);
    switch (i_mat) {
    case bp_P10Bg:
        FM = std::make_shared<TP10Bg>();
        break;
    case bp_P10Bgu:
        FM = std::make_shared<TP10Bgu>();
        break;
    case bp_FR513:
        FM = std::make_shared<TFR513>();
        break;
    case bp_FR510:
        FM = std::make_shared<TFR510>();
        break;
    case bp_Cosid:
        FM = std::make_shared<TCosid>();
        break;
    case bp_P10yBg:
        FM = std::make_shared<TP10yBg>();
        break;
    case bp_P10yBgu:
        FM = std::make_shared<TP10yBgu>();
        break;
    case bp_D1:
        FM = std::make_shared<TDisk1>();
        break;
    case bp_D2:
        FM = std::make_shared<TDisk2>();
        break;
    default: // domyslnie
        FM = std::make_shared<TP10>();
    }
}

void TBrake::Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) {
    BrakeDelayFlag = BDF;
}

double TBrake::GetFC(double const Vel, double const N) {
    return FM->GetFC(N, Vel);
}

double TBrake::GetBCP() const {
    return BrakeCyl->P();
}

double TBrake::GetEDBCP() {
    return 0;
}

double TBrake::GetBRP() const {
    return BrakeRes->P();
}

double TBrake::GetVRP() const {
    return ValveRes->P();
}

double TBrake::GetCRP() {
    return GetBRP();
}

double TBrake::GetPF(double const PP, double const dt, double const Vel) {
    ValveRes->Act();
    BrakeCyl->Act();
    BrakeRes->Act();
    return 0;
}

double TBrake::GetHPFlow(double const HP, double const dt) {
    return 0;
}

double TBrake::GetBCF() const {
    return BCA * 100 * BrakeCyl->P();
}

bool TBrake::SetBDF(int const nBDF) {
    if ((nBDF & BrakeDelays) == nBDF && nBDF != BrakeDelayFlag) {
        BrakeDelayFlag = nBDF;
        return true;
    } else
        return false;
}

void TBrake::Releaser(int const state) {
    BrakeStatus = (BrakeStatus & ~b_rls) | (state * b_rls);
}

bool TBrake::Releaser() const {
    return (BrakeStatus & b_rls) == b_rls;
}

void TBrake::SetEPS(double const nEPS) {}

void TBrake::ASB(int const state) {
    BrakeStatus = (BrakeStatus & ~b_asb) | (state / 2 * b_asb);
    BrakeStatus = (BrakeStatus & ~b_asb_unbrake) | (state % 2 * b_asb_unbrake);
}

int TBrake::GetStatus() const {
    return BrakeStatus;
}

int TBrake::GetSoundFlag() {
    int result = SoundFlag;
    SoundFlag = 0;
    return result;
}

void TBrake::SetASBP(double const Press) {
    ASBP = Press;
}

void TBrake::ForceEmptiness() {
    ValveRes->CreatePress(0);
    BrakeRes->CreatePress(0);

    ValveRes->Act();
    BrakeRes->Act();
}

void TBrake::ForceLeak(double const Amount) {
    BrakeRes->Flow(-Amount * BrakeRes->P());
    ValveRes->Flow(-Amount * ValveRes->P() * 0.01);

    BrakeRes->Act();
    ValveRes->Act();
}

//---WESTINGHOUSE---

void TWest::Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) {
    TBrake::Init(PP, HPP, LPP, BP, BDF);
    ValveRes->CreatePress(PP);
    BrakeCyl->CreatePress(BP);
    BrakeRes->CreatePress(PP / 2 + HPP / 2);
}

double TWest::GetPF(double const PP, double const dt, double const Vel) {
    double dv;
    double dV1;
    double VVP;
    double BVP;
    double CVP;
    double BCP;
    double temp;

    BVP = BrakeRes->P();
    VVP = ValveRes->P();
    CVP = BrakeCyl->P();
    BCP = BrakeCyl->P();

    if ((BrakeStatus & b_hld) == b_hld) {
        if (VVP + 0.03 < BVP)
            BrakeStatus |= b_on;
        else if (VVP > BVP + 0.1)
            BrakeStatus &= ~(b_on | b_hld);
        else if (VVP > BVP)
            BrakeStatus &= ~b_on;
    } else if (VVP + 0.25 < BVP)
        BrakeStatus |= b_on | b_hld;

    if ((BrakeStatus & b_hld) == b_off && !DCV)
        dv = PF(0, CVP, 0.0068 * SizeBC) * dt;
    else
        dv = 0;
    BrakeCyl->Flow(-dv);

    if (BCP > LBP + 0.01 && DCV)
        dv = PF(0, CVP, 0.1 * SizeBC) * dt;
    else
        dv = 0;
    BrakeCyl->Flow(-dv);

    if ((BrakeStatus & b_rls) == b_rls) // odluzniacz
        dv = PF(0, CVP, 0.1 * SizeBC) * dt;
    else
        dv = 0;
    BrakeCyl->Flow(-dv);

    // hamulec EP
    temp = BVP * int(EPS > 0);
    dv = PF(temp, LBP, 0.0015) * dt * EPS * EPS * int(LBP * EPS < MaxBP * LoadC);
    LBP = LBP - dv;

    // przeplyw ZP <-> silowniki
    if ((BrakeStatus & b_on) == b_on && (TareBP < 0.1 || BCP < MaxBP * LoadC)) {
        if (BVP > LBP) {
            DCV = false;
            dv = PF(BVP, CVP, 0.017 * SizeBC) * dt;
        } else
            dv = 0;
    } else
        dv = 0;
    BrakeRes->Flow(dv);
    BrakeCyl->Flow(-dv);
    if (DCV)
        dVP = PF(LBP, BCP, 0.01 * SizeBC) * dt;
    else
        dVP = 0;
    BrakeCyl->Flow(-dVP);
    if (dVP > 0)
        dVP = 0;
    // przeplyw ZP <-> rozdzielacz
    if ((BrakeStatus & b_hld) == b_off)
        dv = PF(BVP, VVP, 0.0011 * SizeBR) * dt;
    else
        dv = 0;
    BrakeRes->Flow(dv);
    dV1 = dv * 0.95;
    ValveRes->Flow(-0.05 * dv);
    // przeplyw PG <-> rozdzielacz
    dv = PF(PP, VVP, 0.01 * SizeBR) * dt;
    ValveRes->Flow(-dv);

    ValveRes->Act();
    BrakeCyl->Act();
    BrakeRes->Act();
    return dv - dV1;
}

double TWest::GetHPFlow(double const HP, double const dt) {
    return dVP;
}

void TWest::SetLBP(double const P) {
    LBP = P;
    if (P > BrakeCyl->P())
        DCV = true;
}

void TWest::SetEPS(double const nEPS) {
    if (nEPS > 0)
        DCV = true;
    else if (nEPS == 0 && EPS != 0) {
        if (LBP > 0.4)
            LBP = BrakeCyl->P();
        if (LBP < 0.15)
            LBP = 0;
    }
    EPS = nEPS;
}

void TWest::PLC(double const mass) {
    LoadC = 1 + int(mass < LoadM) * ((TareBP + (MaxBP - TareBP) * (mass - TareM) / (LoadM - TareM)) / MaxBP - 1);
}

void TWest::SetLP(double const TM, double const LM, double const TBP) {
    TareM = TM;
    LoadM = LM;
    TareBP = TBP;
}

//---OERLIKON EST4---

void TESt::CheckReleaser(double const dt) {
    double VVP = std::min(ValveRes->P(), BrakeRes->P() + 0.05);
    double CVP = CntrlRes->P() - 0.0;

    // odluzniacz
    if ((BrakeStatus & b_rls) == b_rls) {
        if (CVP - VVP < 0)
            BrakeStatus &= ~b_rls;
        else {
            CntrlRes->Flow(+PF(CVP, 0, 0.1) * dt);
        }
    }
}

void TESt::CheckState(double const BCP, double & /*dV1*/) {
    double const VVP{ValveRes->P()};
    double const CVP{CntrlRes->P()};

    // sprawdzanie stanu
    if (BCP > 0.25) {
        if ((BrakeStatus & b_hld) == b_hld) {
            if (VVP + 0.003 + BCP / BVM < CVP) {
                // hamowanie stopniowe
                BrakeStatus |= b_on;
            } else {
                if (VVP + BCP / BVM > CVP) {
                    // zatrzymanie napelaniania
                    BrakeStatus &= ~b_on;
                }
                if (VVP - 0.003 + (BCP - 0.1) / BVM > CVP) {
                    // luzowanie
                    BrakeStatus &= ~(b_on | b_hld);
                }
            }
        } else {
            if (VVP + BCP / BVM < CVP && (CVP - VVP) * BVM > 0.25) {
                // zatrzymanie luzowanie
                BrakeStatus |= b_hld;
            }
        }
    } else {
        if (VVP + 0.1 < CVP) {
            // poczatek hamowania
            if ((BrakeStatus & b_hld) == 0) {
                // przyspieszacz
                ValveRes->CreatePress(0.02 * VVP);
                SoundFlag |= sf_Acc;
                ValveRes->Act();
            }
            BrakeStatus |= b_on | b_hld;
        }
    }

    if ((BrakeStatus & b_hld) == 0) {
        SoundFlag |= sf_CylU;
    }
}

double TESt::CVs(double const BP) const {
    double VVP;
    double BVP;
    double CVP;

    BVP = BrakeRes->P();
    CVP = CntrlRes->P();
    VVP = ValveRes->P();

    // przeplyw ZS <-> PG
    if (VVP < CVP - 0.12 || BVP < CVP - 0.3 || BP > 0.4)
        return 0;
    else if (VVP > CVP + 0.4) {
        if (BVP > CVP + 0.2)
            return 0.23;
        else
            return 0.05;
    } else if (BVP > CVP - 0.1)
        return 1;
    else
        return 0.3;
}

double TESt::BVs(double const BCP) const {
    double VVP;
    double BVP;
    double CVP;

    BVP = BrakeRes->P();
    CVP = CntrlRes->P();
    VVP = ValveRes->P();

    // przeplyw ZP <-> rozdzielacz
    if (BVP < CVP - 0.3)
        return 0.6;
    else if (BCP < 0.5) {
        if (VVP > CVP + 0.4)
            return 0.1;
        else
            return 0.3;
    } else
        return 0;
}

double TESt::GetPF(double const PP, double const dt, double const Vel) {
    double dv;
    double dV1;
    double temp;
    double VVP;
    double BVP;
    double BCP;
    double CVP;

    BVP = BrakeRes->P();
    BCP = BrakeCyl->P();

    dV1 = 0;

    // sprawdzanie stanu
    CheckState(BCP, dV1);
    CheckReleaser(dt);

    CVP = CntrlRes->P();
    VVP = ValveRes->P();
    // przeplyw ZS <-> PG
    temp = CVs(BCP);
    dv = PF(CVP, VVP, 0.0015 * temp) * dt;
    CntrlRes->Flow(+dv);
    ValveRes->Flow(-0.04 * dv);
    dV1 = dV1 - 0.96 * dv;

    // luzowanie
    if ((BrakeStatus & b_hld) == b_off)
        dv = PF(0, BCP, 0.0058 * SizeBC) * dt;
    else
        dv = 0;
    BrakeCyl->Flow(-dv);

    // przeplyw ZP <-> silowniki
    if ((BrakeStatus & b_on) == b_on)
        dv = PF(BVP, BCP, 0.016 * SizeBC) * dt;
    else
        dv = 0;
    BrakeRes->Flow(dv);
    BrakeCyl->Flow(-dv);

    // przeplyw ZP <-> rozdzielacz
    temp = BVs(BCP);
    if (VVP - 0.05 > BVP)
        dv = PF(BVP, VVP, 0.02 * SizeBR * temp / 1.87) * dt;
    else
        dv = 0;
    BrakeRes->Flow(dv);
    dV1 = dV1 + dv * 0.96;
    ValveRes->Flow(-0.04 * dv);
    // przeplyw PG <-> rozdzielacz
    dv = PF(PP, VVP, 0.01) * dt;
    ValveRes->Flow(-dv);

    ValveRes->Act();
    BrakeCyl->Act();
    BrakeRes->Act();
    CntrlRes->Act();
    return dv - dV1;
}

void TESt::Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) {
    TBrake::Init(PP, HPP, LPP, BP, BDF);
    ValveRes->CreatePress(PP);
    BrakeCyl->CreatePress(BP);
    BrakeRes->CreatePress(PP);
    CntrlRes->CreateCap(15);
    CntrlRes->CreatePress(HPP);
    BrakeStatus = b_off;

    BVM = 1.0 / (HPP - LPP) * MaxBP;

    BrakeDelayFlag = BDF;
}

void TESt::EStParams(double const /*i_crc*/) const {}

double TESt::GetCRP() {
    return CntrlRes->P();
}

void TESt::ForceEmptiness() {
    ValveRes->CreatePress(0);
    BrakeRes->CreatePress(0);
    CntrlRes->CreatePress(0);

    ValveRes->Act();
    BrakeRes->Act();
    CntrlRes->Act();
}

//---EP2---

void TEStEP2::Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) {
    TLSt::Init(PP, HPP, LPP, BP, BDF);
    ImplsRes->CreateCap(1);
    ImplsRes->CreatePress(BP);

    BrakeRes->CreatePress(PP);

    BrakeDelayFlag = bdelay_P;
    BrakeDelays = bdelay_P;
}

double TEStEP2::GetPF(double const PP, double const dt, double const Vel) {
    double result;
    double dv;
    double dV1;
    double temp;
    double VVP;
    double BVP;
    double BCP;
    double CVP;

    BVP = BrakeRes->P();
    VVP = ValveRes->P();
    BCP = ImplsRes->P();
    CVP = CntrlRes->P(); // 110115 - konsultacje warszawa1

    dV1 = 0;

    // odluzniacz
    CheckReleaser(dt);

    // sprawdzanie stanu
    if ((BrakeStatus & b_hld) == b_hld && BCP > 0.25) {
        if (VVP + 0.003 + BCP / BVM < CVP - 0.12)
            BrakeStatus |= b_on; // hamowanie stopniowe;
        else if (VVP - 0.003 + BCP / BVM > CVP - 0.12)
            BrakeStatus &= ~(b_on | b_hld); // luzowanie;
        else if (VVP + BCP / BVM > CVP - 0.12)
            BrakeStatus &= ~b_on; // zatrzymanie napelaniania;
    } else if (VVP + 0.10 < CVP - 0.12 && BCP < 0.25) // poczatek hamowania
    {
        BrakeStatus |= b_on | b_hld;
    } else if (VVP + BCP / BVM < CVP - 0.12 && BCP > 0.25) // zatrzymanie luzowanie
        BrakeStatus |= b_hld;

    if ((BrakeStatus & b_hld) == 0) {
        SoundFlag |= sf_CylU;
    }

    // przeplyw ZS <-> PG
    if (BVP < CVP - 0.2 || BrakeStatus != b_off || BCP > 0.25)
        temp = 0;
    else if (VVP > CVP + 0.4)
        temp = 0.1;
    else
        temp = 0.5;

    dv = PF(CVP, VVP, 0.0015 * temp / 1.8) * dt;
    CntrlRes->Flow(+dv);
    ValveRes->Flow(-0.04 * dv);
    dV1 = dV1 - 0.96 * dv;

    // hamulec EP
    EPCalc(dt);

    // luzowanie KI
    if ((BrakeStatus & b_hld) == b_off)
        dv = PF(0, BCP, 0.00083) * dt;
    else
        dv = 0;
    ImplsRes->Flow(-dv);
    // przeplyw ZP <-> KI
    if ((BrakeStatus & b_on) == b_on && BCP < MaxBP * LoadC)
        dv = PF(BVP, BCP, 0.0006) * dt;
    else
        dv = 0;
    BrakeRes->Flow(dv);
    ImplsRes->Flow(-dv);
    // przeplyw PG <-> rozdzielacz
    dv = PF(PP, VVP, 0.01 * SizeBR) * dt;
    ValveRes->Flow(-dv);

    result = dv - dV1;

    temp = std::max(BCP, LBP);

    if (ImplsRes->P() > LBP + 0.01)
        LBP = 0;

    // luzowanie CH
    if (BrakeCyl->P() > temp + 0.005 || std::max(ImplsRes->P(), 8 * LBP) < 0.05)
        dv = PF(0, BrakeCyl->P(), 0.25 * SizeBC * (0.01 + (BrakeCyl->P() - temp))) * dt;
    else
        dv = 0;
    BrakeCyl->Flow(-dv);
    // przeplyw ZP <-> CH
    if (BrakeCyl->P() < temp - 0.005 && std::max(ImplsRes->P(), 8 * LBP) > 0.10 && std::max(BCP, LBP) < MaxBP * LoadC)
        dv = PF(BVP, BrakeCyl->P(), 0.35 * SizeBC * (0.01 - (BrakeCyl->P() - temp))) * dt;
    else
        dv = 0;
    BrakeRes->Flow(dv);
    BrakeCyl->Flow(-dv);

    ImplsRes->Act();
    ValveRes->Act();
    BrakeCyl->Act();
    BrakeRes->Act();
    CntrlRes->Act();
    return result;
}

void TEStEP2::PLC(double const mass) {
    LoadC = 1 + int(mass < LoadM) * ((TareBP + (MaxBP - TareBP) * (mass - TareM) / (LoadM - TareM)) / MaxBP - 1);
}

void TEStEP2::SetEPS(double const nEPS) {
    EPS = nEPS;
    if (EPS > 0 && LBP + 0.01 < BrakeCyl->P())
        LBP = BrakeCyl->P();
}

void TEStEP2::SetLP(double const TM, double const LM, double const TBP) {
    TareM = TM;
    LoadM = LM;
    TareBP = TBP;
}

void TEStEP2::EPCalc(double dt) {
    double temp = BrakeRes->P() * int(EPS > 0);
    double dv = PF(temp, LBP, 0.00053 + 0.00060 * int(EPS < 0)) * dt * EPS * EPS * int(LBP * EPS < MaxBP * LoadC);
    LBP = LBP - dv;
}

void TEStEP1::EPCalc(double dt) {
    double temp = EPS - std::floor(EPS);                         // część ułamkowa jest hamulcem EP
    double LBPLim = std::min(MaxBP * LoadC * temp, BrakeRes->P()); // do czego dążymy
    double S = 10 * std::clamp(LBPLim - LBP, -0.1, 0.1);         // przymykanie zaworku
    double dv = PF(S > 0 ? BrakeRes->P() : 0, LBP, std::abs(S) * (0.00053 + 0.00060 * int(S < 0))) * dt; // przepływ
    LBP = LBP - dv;
}

void TEStEP1::SetEPS(double const nEPS) {
    EPS = nEPS;
}

//---EST4-RAPID---

double TESt4R::GetPF(double const PP, double const dt, double const Vel) {
    double result;
    double dv;
    double dV1;
    double temp;
    double VVP;
    double BVP;
    double BCP;
    double CVP;

    BVP = BrakeRes->P();
    BCP = ImplsRes->P();

    dV1 = 0;

    // sprawdzanie stanu
    CheckState(BCP, dV1);
    CheckReleaser(dt);

    CVP = CntrlRes->P();
    VVP = ValveRes->P();
    // przeplyw ZS <-> PG
    temp = CVs(BCP);
    dv = PF(CVP, VVP, 0.0015 * temp / 1.8) * dt;
    CntrlRes->Flow(+dv);
    ValveRes->Flow(-0.04 * dv);
    dV1 = dV1 - 0.96 * dv;

    // luzowanie KI
    if ((BrakeStatus & b_hld) == b_off)
        dv = PF(0, BCP, 0.00037 * 1.14 * 15 / 19) * dt;
    else
        dv = 0;
    ImplsRes->Flow(-dv);
    // przeplyw ZP <-> KI
    if ((BrakeStatus & b_on) == b_on)
        dv = PF(BVP, BCP, 0.0014) * dt;
    else
        dv = 0;
    ImplsRes->Flow(-dv);
    // przeplyw ZP <-> rozdzielacz
    temp = BVs(BCP);
    if (BVP < VVP - 0.05)
        dv = PF(BVP, VVP, 0.02 * SizeBR * temp / 1.87) * dt;
    else
        dv = 0;
    BrakeRes->Flow(dv);
    dV1 = dV1 + dv * 0.96;
    ValveRes->Flow(-0.04 * dv);
    // przeplyw PG <-> rozdzielacz
    dv = PF(PP, VVP, 0.01 * SizeBR) * dt;
    ValveRes->Flow(-dv);

    result = dv - dV1;

    RapidStatus = BrakeDelayFlag == bdelay_R && ((Vel > 55 && RapidStatus == true) || Vel > 70);

    RapidTemp = RapidTemp + (0.9 * int(RapidStatus) - RapidTemp) * dt / 2;
    temp = 1.9 - RapidTemp;
    if ((BrakeStatus & b_asb) == b_asb)
        temp = 1000;
    // luzowanie CH
    if (BrakeCyl->P() * temp > ImplsRes->P() + 0.005 || ImplsRes->P() < 0.25) {
        if ((BrakeStatus & b_asb) == b_asb)
            dv = PFVd(BrakeCyl->P(), 0, 0.115 * SizeBC * 4, ImplsRes->P() / temp) * dt;
        else
            dv = PFVd(BrakeCyl->P(), 0, 0.115 * SizeBC, ImplsRes->P() / temp) * dt;
    } else
        dv = 0;
    BrakeCyl->Flow(-dv);
    // przeplyw ZP <-> CH
    if (BrakeCyl->P() * temp < ImplsRes->P() - 0.005 && ImplsRes->P() > 0.3)
        dv = PFVa(BVP, BrakeCyl->P(), 0.60 * SizeBC, ImplsRes->P() / temp) * dt;
    else
        dv = 0;
    BrakeRes->Flow(-dv);
    BrakeCyl->Flow(+dv);

    ImplsRes->Act();
    ValveRes->Act();
    BrakeCyl->Act();
    BrakeRes->Act();
    CntrlRes->Act();
    return result;
}

void TESt4R::Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) {
    TESt::Init(PP, HPP, LPP, BP, BDF);
    ImplsRes->CreateCap(1);
    ImplsRes->CreatePress(BP);

    BrakeDelayFlag = bdelay_R;
}

//---LSt---

double TLSt::GetPF(double const PP, double const dt, double const Vel) {
    double result;

    double const BVP{BrakeRes->P()};
    double const VVP{ValveRes->P()};
    double const BCP{ImplsRes->P()};
    double const CVP{CntrlRes->P()};

    double dV{0.0};
    double dV1{0.0};

    // sprawdzanie stanu
    if ((BrakeStatus & b_hld) == b_hld && BCP > 0.25) {
        if (VVP + 0.003 + BCP / BVM < CVP) {
            // hamowanie stopniowe
            BrakeStatus |= b_on;
        } else if (VVP - 0.003 + (BCP - 0.1) / BVM > CVP) {
            // luzowanie
            BrakeStatus &= ~(b_on | b_hld);
        } else if (VVP + BCP / BVM > CVP) {
            // zatrzymanie napelaniania
            BrakeStatus &= ~b_on;
        }
    } else if (VVP + 0.10 < CVP && BCP < 0.25) {
        // poczatek hamowania
        if ((BrakeStatus & b_hld) == b_off) {
            SoundFlag |= sf_Acc;
        }
        BrakeStatus |= b_on | b_hld;
    } else if (VVP + (BCP - 0.1) / BVM < CVP && (CVP - VVP) * BVM > 0.25 && BCP > 0.25) {
        // zatrzymanie luzowanie
        BrakeStatus |= b_hld;
    }
    if ((BrakeStatus & b_hld) == 0) {
        SoundFlag |= sf_CylU;
    }
    if (bool is_releasing = (BrakeStatus & b_rls) || (UniversalFlag & ub_Release); is_releasing) {
        if (CVP < 0.0) {
            BrakeStatus &= ~b_rls;
        } else { // 008
            dV = PF1(CVP, BCP, 0.024) * dt;
            CntrlRes->Flow(dV);
        }
    }

    // przeplyw ZS <-> PG
    double temp;
    if ((CVP - BCP) * BVM > 0.5)
        temp = 0.0;
    else if (VVP > CVP + 0.4)
        temp = 0.5;
    else
        temp = 0.5;

    dV = PF1(CVP, VVP, 0.0015 * temp / 1.8 / 2) * dt;
    CntrlRes->Flow(+dV);
    ValveRes->Flow(-0.04 * dV);
    dV1 = dV1 - 0.96 * dV;

    // luzowanie KI  {G}
    /*P*/
    if (VVP > BCP) {
        dV = PF(VVP, BCP, 0.00043 * (1.5 - (true == ((CVP - BCP) * BVM > 1.0 && BrakeDelayFlag == bdelay_G) ? 1.0 : 0.0)), 0.1) * dt;
    } else if (CVP - BCP < 1.5) {
        dV = PF(VVP, BCP, 0.001472 * (1.36 - (true == ((CVP - BCP) * BVM > 1.0 && BrakeDelayFlag == bdelay_G) ? 1.0 : 0.0)), 0.1) * dt;
    } else {
        dV = 0;
    }

    ImplsRes->Flow(-dV);
    ValveRes->Flow(+dV);
    // przeplyw PG <-> rozdzielacz
    dV = PF(PP, VVP, 0.01, 0.1) * dt;
    ValveRes->Flow(-dV);

    result = dV - dV1;

    // cisnienie PP
    RapidTemp = RapidTemp + (RM * int(Vel > 55 && BrakeDelayFlag == bdelay_R) - RapidTemp) * dt / 2;
    temp = 1 - RapidTemp;
    if (EDFlag > 0.2)
        temp = 10000;
    double tempasb = 0;
    if ((UniversalFlag & ub_AntiSlipBrake) > 0 || (BrakeStatus & b_asb_unbrake) == b_asb_unbrake)
        tempasb = ASBP;
    // powtarzacz — podwojny zawor zwrotny
    temp = std::max(((CVP - BCP) * BVM + tempasb) / temp, LBP);
    // luzowanie CH
    if (BrakeCyl->P() > temp + 0.005 || temp < 0.28)
        dV = PFVd(BrakeCyl->P(), 0, 0.005 * 7 * SizeBC, temp) * dt;
    else
        dV = 0;
    BrakeCyl->Flow(-dV);
    // przeplyw ZP <-> CH
    if (BrakeCyl->P() < temp - 0.005 && temp > 0.29)
        dV = -PFVa(BVP, BrakeCyl->P(), 0.002 * 7 * SizeBC * 2, temp) * dt;
    else
        dV = 0;
    BrakeRes->Flow(dV);
    BrakeCyl->Flow(-dV);

    ImplsRes->Act();
    ValveRes->Act();
    BrakeCyl->Act();
    BrakeRes->Act();
    CntrlRes->Act();
    return result;
}

void TLSt::Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) {
    TESt4R::Init(PP, HPP, LPP, BP, BDF);
    ValveRes->CreateCap(1);
    ImplsRes->CreateCap(8);
    ImplsRes->CreatePress(PP);
    BrakeRes->CreatePress(8);
    ValveRes->CreatePress(PP);

    EDFlag = 0;

    BrakeDelayFlag = BDF;
}

void TLSt::SetLBP(double const P) {
    LBP = P;
}

double TLSt::GetEDBCP() {
    double CVP;
    double BCP;

    CVP = CntrlRes->P();
    BCP = ImplsRes->P();
    return (CVP - BCP) * BVM;
}

void TLSt::SetED(double const EDstate) {
    EDFlag = EDstate;
}

void TLSt::SetRM(double const RMR) {
    RM = 1 - RMR;
}

double TLSt::GetHPFlow(double const HP, double const dt) {
    double dv;

    dv = std::min(PF(HP, BrakeRes->P(), 0.01 * dt), 0.0);
    BrakeRes->Flow(-dv);
    return dv;
}

//---EStED---

double TEStED::GetPF(double const PP, double const dt, double const Vel) {
    double dv;
    double dV1;
    double temp;
    double VVP;
    double BVP;
    double BCP;
    double CVP;
    double MPP;
    double nastG;

    BVP = BrakeRes->P();
    VVP = ValveRes->P();
    BCP = ImplsRes->P();
    CVP = CntrlRes->P() - 0.0;
    MPP = Miedzypoj->P();
    dV1 = 0;

    nastG = BrakeDelayFlag & bdelay_G;

    // sprawdzanie stanu
    if (BCP < 0.25 && VVP + 0.08 > CVP)
        Przys_blok = false;

    // sprawdzanie stanu
    if (VVP + 0.002 + BCP / BVM < CVP - 0.05 && Przys_blok)
        BrakeStatus |= b_on | b_hld; // hamowanie stopniowe;
    else if (VVP - 0.002 + (BCP - 0.1) / BVM > CVP - 0.05)
        BrakeStatus &= ~(b_on | b_hld); // luzowanie;
    else if (VVP + BCP / BVM > CVP - 0.05)
        BrakeStatus &= ~b_on; // zatrzymanie napelaniania;
    else if (VVP + (BCP - 0.1) / BVM < CVP - 0.05 && BCP > 0.25) // zatrzymanie luzowania
        BrakeStatus |= b_hld;

    // poczatek hamowania
    if (VVP + 0.10 < CVP && BCP < 0.25 && !Przys_blok) {
        ValveRes->CreatePress(0.75 * VVP);
        SoundFlag |= sf_Acc;
        ValveRes->Act();
        Przys_blok = true;
    }

    if (BCP > 0.5)
        Zamykajacy = true;
    else if (VVP - 0.6 < MPP)
        Zamykajacy = false;

    if ((BrakeStatus & b_rls) == b_rls) {
        dv = PF(CVP, BCP, 0.024) * dt;
        CntrlRes->Flow(+dv);
    }

    // luzowanie
    if ((BrakeStatus & b_hld) == b_off)
        dv = PF(0, BCP, Nozzles[3] * nastG + (1 - nastG) * Nozzles[1]) * dt;
    else
        dv = 0;
    ImplsRes->Flow(-dv);
    if ((BrakeStatus & b_on) == b_on && BCP < MaxBP)
        dv = PF(BVP, BCP, Nozzles[2] * (nastG + 2 * int(BCP < 0.8)) + Nozzles[0] * (1 - nastG)) * dt;
    else
        dv = 0;
    ImplsRes->Flow(-dv);
    BrakeRes->Flow(dv);

    // przeplyw testowy miedzypojemnosci
    if (MPP < CVP - 0.3)
        temp = Nozzles[4];
    else if (BCP < 0.5) {
        if (Zamykajacy)
            temp = Nozzles[8]; // 1.25;
        else
            temp = Nozzles[7];
    } else
        temp = 0;
    dv = PF(MPP, VVP, temp);

    if (MPP < CVP - 0.17)
        temp = 0;
    else if (MPP > CVP - 0.08)
        temp = Nozzles[5];
    else
        temp = Nozzles[6];
    dv = dv + PF(MPP, CVP, temp);

    if (MPP - 0.05 > BVP)
        dv = dv + PF(MPP - 0.05, BVP, Nozzles[10] * nastG + (1 - nastG) * Nozzles[9]);
    if (MPP > VVP)
        dv = dv + PF(MPP, VVP, 0.02);
    Miedzypoj->Flow(dv * dt * 0.15);

    RapidTemp = RapidTemp + (RM * int(Vel > RV && BrakeDelayFlag == bdelay_R) - RapidTemp) * dt / 2;
    temp = std::max(1 - RapidTemp, 0.001);

    // powtarzacz — podwojny zawor zwrotny
    temp = std::max(LoadC * BCP / temp * std::clamp(1 - EDFlag, 0., 1.), LBP);

    if ((UniversalFlag & ub_AntiSlipBrake) > 0)
        temp = std::max(temp, ASBP);

    double speed = 1;
    if (ASBP < 0.1 && (BrakeStatus & b_asb_unbrake) == b_asb_unbrake) {
        temp = 0;
        speed = 3;
    }

    if (BrakeCyl->P() > temp)
        dv = -PFVd(BrakeCyl->P(), 0, 0.05 * SizeBC * speed, temp) * dt;
    else if (BrakeCyl->P() < temp && (BrakeStatus & b_asb) == 0)
        dv = PFVa(BVP, BrakeCyl->P(), 0.05 * SizeBC, temp) * dt;
    else
        dv = 0;

    BrakeCyl->Flow(dv);
    if (dv > 0)
        BrakeRes->Flow(-dv);

    // przeplyw ZS <-> PG
    if (MPP < CVP - 0.17)
        temp = 0;
    else if (MPP > CVP - 0.08)
        temp = Nozzles[5];
    else
        temp = Nozzles[6];
    dv = PF(CVP, MPP, temp) * dt;
    CntrlRes->Flow(+dv);
    ValveRes->Flow(-0.02 * dv);
    dV1 = dV1 + 0.98 * dv;

    // przeplyw ZP <-> MPJ
    if (MPP - 0.05 > BVP)
        dv = PF(BVP, MPP - 0.05, Nozzles[10] * nastG + (1 - nastG) * Nozzles[9]) * dt;
    else
        dv = 0;
    BrakeRes->Flow(dv);
    dV1 = dV1 + dv * 0.98;
    ValveRes->Flow(-0.02 * dv);
    // przeplyw PG <-> rozdzielacz
    dv = PF(PP, VVP, 0.005) * dt; // 0.01
    ValveRes->Flow(-dv);

    ValveRes->Act();
    BrakeCyl->Act();
    BrakeRes->Act();
    CntrlRes->Act();
    Miedzypoj->Act();
    ImplsRes->Act();
    return dv - dV1;
}

void TEStED::Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) {
    TLSt::Init(PP, HPP, LPP, BP, BDF);

    ValveRes->CreatePress(PP);
    BrakeCyl->CreatePress(BP);

    BrakeStatus = BP > 1.0 ? 1 : 0;
    Miedzypoj->CreateCap(5);
    Miedzypoj->CreatePress(PP);

    ImplsRes->CreateCap(1);
    ImplsRes->CreatePress(BP);

    BVM = 1.0 / (HPP - 0.05 - LPP) * MaxBP;

    BrakeDelayFlag = BDF;
    Zamykajacy = false;
    EDFlag = 0;

    Nozzles[0] = 1.250 / 1.7;
    Nozzles[1] = 0.907;
    Nozzles[2] = 0.510 / 1.7;
    Nozzles[3] = 0.524 / 1.17;
    Nozzles[4] = 7.4;
    Nozzles[7] = 5.3;
    Nozzles[8] = 2.5;
    Nozzles[9] = 7.28;
    Nozzles[10] = 2.96;
    Nozzles[5] = 1.1;
    Nozzles[6] = 0.9;

    for (int i = 0; i < 11; ++i)
        Nozzles[i] = Nozzles[i] * Nozzles[i] * 3.14159 / 4000;
}

double TEStED::GetEDBCP() {
    return ImplsRes->P() * LoadC;
}

void TEStED::PLC(double const mass) {
    LoadC = 1 + int(mass < LoadM) * ((TareBP + (MaxBP - TareBP) * (mass - TareM) / (LoadM - TareM)) / MaxBP - 1);
}

void TEStED::SetLP(double const TM, double const LM, double const TBP) {
    TareM = TM;
    LoadM = LM;
    TareBP = TBP;
}

//--- KNORR KE ---

void TKE::CheckReleaser(double const dt) {
    double VVP;
    double CVP;

    VVP = ValveRes->P();
    CVP = CntrlRes->P();

    // odluzniacz
    if (true == ((BrakeStatus & b_rls) == b_rls)) {
        if (CVP - VVP < 0)
            BrakeStatus &= ~b_rls;
        else
            CntrlRes->Flow(+PF(CVP, 0, 0.1) * dt);
    }
}

void TKE::CheckState(double const BCP, double & /*dV1*/) {
    double VVP;
    double CVP;

    VVP = ValveRes->P();
    CVP = CntrlRes->P();

    // sprawdzanie stanu
    if (BCP > 0.1) {
        if ((BrakeStatus & b_hld) == b_hld) {
            if (VVP + 0.003 + BCP / BVM < CVP) {
                // hamowanie stopniowe;
                BrakeStatus |= b_on;
            } else {
                if (VVP + BCP / BVM > CVP) {
                    // zatrzymanie napelaniania;
                    BrakeStatus &= ~b_on;
                }
                if (VVP - 0.003 + BCP / BVM > CVP) {
                    // luzowanie;
                    BrakeStatus &= ~(b_on | b_hld);
                }
            }
        } else {
            if (VVP + BCP / BVM < CVP && (CVP - VVP) * BVM > 0.25) {
                // zatrzymanie luzowanie
                BrakeStatus |= b_hld;
            }
        }
    } else {
        if (VVP + 0.1 < CVP) {
            // poczatek hamowania
            if ((BrakeStatus & b_hld) == 0) {
                // przyspieszacz
                ValveRes->CreatePress(0.8 * VVP);
                SoundFlag |= sf_Acc;
                ValveRes->Act();
            }
            BrakeStatus |= b_on | b_hld;
        }
    }

    if ((BrakeStatus & b_hld) == 0) {
        SoundFlag |= sf_CylU;
    }
}

double TKE::CVs(double const BP) const {
    double VVP;
    double CVP;

    CVP = CntrlRes->P();
    VVP = ValveRes->P();

    // przeplyw ZS <-> PG
    if (BP > 0.2)
        return 0;
    else if (VVP > CVP + 0.4)
        return 0.05;
    else
        return 0.23;
}

double TKE::BVs(double const /*BCP*/) const {
    double VVP;
    double BVP;
    double CVP;

    BVP = BrakeRes->P();
    CVP = CntrlRes->P();
    VVP = ValveRes->P();

    // przeplyw ZP <-> rozdzielacz
    if (BVP > VVP)
        return 0;
    else if (BVP < CVP - 0.3)
        return 0.6;
    else
        return 0.13;
}

double TKE::GetPF(double const PP, double const dt, double const Vel) {
    double dv;
    double dV1;
    double temp;
    double VVP;
    double BVP;
    double BCP;
    double IMP;
    double CVP;

    BVP = BrakeRes->P();
    VVP = ValveRes->P();
    BCP = BrakeCyl->P();
    IMP = ImplsRes->P();
    CVP = CntrlRes->P();

    dV1 = 0;

    // sprawdzanie stanu
    CheckState(IMP, dV1);
    CheckReleaser(dt);

    // przeplyw ZS <-> PG
    temp = CVs(IMP);
    dv = PF(CVP, VVP, 0.0015 * temp) * dt;
    CntrlRes->Flow(+dv);
    ValveRes->Flow(-0.04 * dv);
    dV1 = dV1 - 0.96 * dv;

    // luzowanie
    if ((BrakeStatus & b_hld) == b_off) {
        if ((BrakeDelayFlag & bdelay_G) == 0)
            temp = 0.283 + 0.139;
        else
            temp = 0.139;
        dv = PF(0, IMP, 0.001 * temp) * dt;
    } else
        dv = 0;
    ImplsRes->Flow(-dv);

    // przeplyw ZP <-> silowniki
    if ((BrakeStatus & b_on) == b_on && IMP < MaxBP) {
        temp = 0.113;
        if ((BrakeDelayFlag & bdelay_G) == 0)
            temp = temp + 0.636;
        if (BCP < 0.5)
            temp = temp + 0.785;
        dv = PF(BVP, IMP, 0.001 * temp) * dt;
    } else
        dv = 0;
    BrakeRes->Flow(dv);
    ImplsRes->Flow(-dv);

    // rapid
    auto const &frictionmaterial{*FM};
    if (auto const &frictionmaterialtype{typeid(frictionmaterial)};
        !(frictionmaterialtype == typeid(TDisk1) || frictionmaterialtype == typeid(TDisk2))) // jesli zeliwo to schodz
        RapidStatus = (BrakeDelayFlag & bdelay_R) == bdelay_R && (RV < 0 || (Vel > RV && RapidStatus) || Vel > RV + 20);
    else // jesli tarczowki, to zostan
        RapidStatus = (BrakeDelayFlag & bdelay_R) == bdelay_R;

    if (RM * RM > 0.001) { // jesli jest rapid
        if (RM > 0) // jesli dodatni (naddatek);
            temp = 1 - RM * int(RapidStatus);
        else
            temp = 1 - RM * (1 - int(RapidStatus));
    } else
        temp = 1;
    temp = temp / LoadC;
    // luzowanie CH
    IMP = std::max(IMP / temp, std::max(LBP, ASBP * int((BrakeStatus & b_asb) == b_asb)));
    if (ASBP < 0.1 && (BrakeStatus & b_asb) == b_asb)
        IMP = 0;
    // luzowanie CH
    if (BCP > IMP + 0.005 || std::max(ImplsRes->P(), 8 * LBP) < 0.25)
        dv = PFVd(BCP, 0, 0.05, IMP) * dt;
    else
        dv = 0;
    BrakeCyl->Flow(-dv);
    if (BCP < IMP - 0.005 && std::max(ImplsRes->P(), 8 * LBP) > 0.3)
        dv = PFVa(BVP, BCP, 0.05, IMP) * dt;
    else
        dv = 0;
    BrakeRes->Flow(-dv);
    BrakeCyl->Flow(+dv);

    // przeplyw ZP <-> rozdzielacz
    temp = BVs(IMP);
    if (IMP < 0.25 || VVP + 0.05 > BVP)
        dv = PF(BVP, VVP, 0.02 * SizeBR * temp / 1.87) * dt;
    else
        dv = 0;
    BrakeRes->Flow(dv);
    dV1 = dV1 + dv * 0.96;
    ValveRes->Flow(-0.04 * dv);
    // przeplyw PG <-> rozdzielacz
    dv = PF(PP, VVP, 0.01) * dt;
    ValveRes->Flow(-dv);

    ValveRes->Act();
    BrakeCyl->Act();
    BrakeRes->Act();
    CntrlRes->Act();
    ImplsRes->Act();
    return dv - dV1;
}

void TKE::Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) {
    TBrake::Init(PP, HPP, LPP, BP, BDF);
    ValveRes->CreatePress(PP);
    BrakeCyl->CreatePress(BP);
    BrakeRes->CreatePress(PP);

    CntrlRes->CreateCap(5);
    CntrlRes->CreatePress(HPP);

    ImplsRes->CreateCap(1);
    ImplsRes->CreatePress(BP);

    BrakeStatus = b_off;

    BVM = 1.0 / (HPP - LPP) * MaxBP;

    BrakeDelayFlag = BDF;
}

double TKE::GetCRP() {
    return CntrlRes->P();
}

double TKE::GetHPFlow(double const HP, double const dt) {
    double dv;

    dv = PF(HP, BrakeRes->P(), 0.01) * dt;
    dv = std::min(0., dv);
    BrakeRes->Flow(-dv);
    return dv;
}

void TKE::PLC(double const mass) {
    LoadC = 1 + int(mass < LoadM) * ((TareBP + (MaxBP - TareBP) * (mass - TareM) / (LoadM - TareM)) / MaxBP - 1);
}

void TKE::SetLP(double const TM, double const LM, double const TBP) {
    TareM = TM;
    LoadM = LM;
    TareBP = TBP;
}

void TKE::SetRM(double const RMR) {
    RM = 1.0 - RMR;
}

void TKE::SetLBP(double const P) {
    LBP = P;
}

void TKE::ForceEmptiness() {
    ValveRes->CreatePress(0);
    BrakeRes->CreatePress(0);
    CntrlRes->CreatePress(0);
    ImplsRes->CreatePress(0);
    Brak2Res->CreatePress(0);

    ValveRes->Act();
    BrakeRes->Act();
    CntrlRes->Act();
    ImplsRes->Act();
    Brak2Res->Act();
}

// ---- Oerlikon_ESt.cpp ----

double d2A(double const d) {
    return d * d * 0.7854 / 1000.0;
}

// ------ RURA ------

double TRura::P(void) {
    return Next->P();
}

void TRura::Update(double dt) {
    Next->Flow(dVol);
    dVol = 0;
}

// ------ PRZECIWPOSLIG ------

void TPrzeciwposlizg::SetPoslizg(bool flag) {
    Poslizg = flag;
}

void TPrzeciwposlizg::Update(double dt) {
    if (true == Poslizg) {
        BrakeRes->Flow(dVol);
        Next->Flow(PF(Next->P(), 0, d2A(10)) * dt);
    } else
        Next->Flow(dVol);
    dVol = 0;
}

// ------ PRZEKLADNIK ------

void TPrzekladnik::Update(double dt) {
    double dV;

    double BCP{Next->P()};
    double BVP{BrakeRes->P()};

    if (BCP > P())
        dV = -PFVd(BCP, 0, d2A(10), P()) * dt;
    else {
        if (BCP < P())
            dV = PFVa(BVP, BCP, d2A(10), P()) * dt;
        else
            dV = 0;
    }

    Next->Flow(dV);
    if (dV > 0)
        BrakeRes->Flow(-dV);
}

// ------ PRZEKLADNIK RAPID ------

void TRapid::SetRapidParams(double mult, double size) {
    RapidMult = mult;
    RapidStatus = false;
    if (size > 0.1) // dopasowywanie srednicy przekladnika
    {
        DN = d2A(size * 0.4);
        DL = d2A(size * 0.4);
    } else {
        DN = d2A(5.0);
        DL = d2A(5.0);
    }
}

void TRapid::SetRapidStatus(bool rs) {
    RapidStatus = rs;
}

void TRapid::Update(double dt) {
    double BVP{BrakeRes->P()};
    double BCP{Next->P()};
    double ActMult;
    double dV;

    if (true == RapidStatus) {
        ActMult = RapidMult;
    } else {
        ActMult = 1.0;
    }

    if (BCP * RapidMult > P() * ActMult)
        dV = -PFVd(BCP, 0, DL, P() * ActMult / RapidMult) * dt;
    else if (BCP * RapidMult < P() * ActMult)
        dV = PFVa(BVP, BCP, DN, P() * ActMult / RapidMult) * dt;
    else
        dV = 0.0;

    Next->Flow(dV);
    if (dV > 0.0)
        BrakeRes->Flow(-dV);
}

// ------ PRZEKŁADNIK CIĄGŁY ------

void TPrzekCiagly::SetMult(double m) {
    Mult = m;
}

void TPrzekCiagly::Update(double dt) {
    double const BVP{BrakeRes->P()};
    double const BCP{Next->P()};
    double dV;

    if (BCP > P() * Mult)
        dV = -PFVd(BCP, 0, d2A(8.0), P() * Mult) * dt;
    else if (BCP < P() * Mult)
        dV = PFVa(BVP, BCP, d2A(8.0), P() * Mult) * dt;
    else
        dV = 0.0;

    Next->Flow(dV);
    if (dV > 0.0)
        BrakeRes->Flow(-dV);
}

// ------ PZZ ------

void TPrzek_PZZ::SetLBP(double P) {
    LBP = P;
}

void TPrzek_PZZ::Update(double dt) {
    double const BVP{BrakeRes->P()};
    double const BCP{Next->P()};
    double const Pgr{std::max(LBP, P())};
    double dV;

    if (BCP > Pgr)
        dV = -PFVd(BCP, 0, d2A(8.0), Pgr) * dt;
    else if (BCP < Pgr)
        dV = PFVa(BVP, BCP, d2A(8.0), Pgr) * dt;
    else
        dV = 0.0;

    Next->Flow(dV);
    if (dV > 0.0)
        BrakeRes->Flow(-dV);
}

// ------ ED ------

void TPrzekED::SetP(double P) {
    MaxP = P;
}

void TPrzekED::Update(double dt) {
    if (Next->P() > MaxP) {
        BrakeRes->Flow(dVol);
        Next->Flow(PFVd(Next->P(), 0, d2A(10.0) * dt, MaxP));
    } else
        Next->Flow(dVol);
    dVol = 0.0;
}

// ------ OERLIKON EST NA BOGATO ------

double TNESt3::GetPF(double const PP, double const dt, double const Vel) {
    double const BVP{BrakeRes->P()};
    double const VVP{ValveRes->P()};
    double const BCP{Przekladniki[1]->P()};
    double const CVP{CntrlRes->P()};
    double const MPP{Miedzypoj->P()};
    double dV{0.0};
    double dV1{0.0};

    auto const nastG = static_cast<double>(BrakeDelayFlag & bdelay_G);

    // sprawdzanie stanu
    CheckState(BCP, dV1);
    CheckReleaser(dt);

    // luzowanie
    if ((BrakeStatus & b_hld) == b_off)
        dV = PF(0.0, BCP, Nozzles[dTO] * nastG + (1.0 - nastG) * Nozzles[dOO]) * dt
                * (0.1 + 4.9 * std::min(0.2, BCP - ((CVP - 0.05 - VVP) * BVM + 0.1)));
    else
        dV = 0.0;
    Przekladniki[1]->Flow(-dV);
    if ((BrakeStatus & b_on) == b_on && Przekladniki[1]->P() * HBG300 < MaxBP)
        dV = PF(BVP, BCP, Nozzles[dTN] * (nastG + 2.0 * (BCP < Podskok ? 1.0 : 0.0)) + Nozzles[dON] * (1.0 - nastG)) * dt
                * (0.1 + 4.9 * std::min(0.2, (CVP - 0.05 - VVP) * BVM - BCP));
    else
        dV = 0;
    Przekladniki[1]->Flow(-dV);
    BrakeRes->Flow(dV);

    for (int i = 1; i < 4; ++i)
        Przekladniki[i]->Update(dt);

    if (PrzekRapid != nullptr) {
        RapidStatus = (BrakeDelayFlag & bdelay_R) == bdelay_R
                && (std::abs(Vel) > 70.0 || (std::abs(Vel) > 50.0 && RapidStatus) || RapidStaly);
        PrzekRapid->SetRapidStatus(RapidStatus);
    }
    if (PrzekPoslizg != nullptr)
        PrzekPoslizg->SetPoslizg((BrakeStatus & b_asb) == b_asb);
    if (PrzekED != nullptr) {
        if (Vel < -15.0)
            PrzekED->SetP(0.0);
        else
            PrzekED->SetP(MaxBP * 3.0);
    }
    if (PrzekCiagly != nullptr)
        PrzekCiagly->SetMult(LoadC);
    if (PrzekPZZ != nullptr)
        PrzekPZZ->SetLBP(LBP);

    // przeplyw testowy miedzypojemnosci
    dV = PF(MPP, VVP, BVs(BCP)) + PF(MPP, CVP, CVs(BCP));
    if (MPP - 0.05 > BVP)
        dV += PF(MPP - 0.05, BVP, Nozzles[dPT] * nastG + (1.0 - nastG) * Nozzles[dPO]);
    if (MPP > VVP)
        dV += PF(MPP, VVP, d2A(5.0));
    Miedzypoj->Flow(dV * dt * 0.15);

    // przeplyw ZS <-> PG
    double const temp = CVs(BCP);
    dV = PF(CVP, MPP, temp) * dt;
    CntrlRes->Flow(dV);
    ValveRes->Flow(-0.02 * dV);
    dV1 += 0.98 * dV;

    // przeplyw ZP <-> MPJ
    if (MPP - 0.05 > BVP)
        dV = PF(BVP, MPP - 0.05, Nozzles[dPT] * nastG + (1.0 - nastG) * Nozzles[dPO]) * dt;
    else
        dV = 0.0;
    BrakeRes->Flow(dV);
    dV1 += dV * 0.98;
    ValveRes->Flow(-0.02 * dV);
    // przeplyw PG <-> rozdzielacz
    dV = PF(PP, VVP, 0.005) * dt; // 0.01
    ValveRes->Flow(-dV);

    ValveRes->Act();
    BrakeCyl->Act();
    BrakeRes->Act();
    CntrlRes->Act();
    Miedzypoj->Act();
    Przekladniki[1]->Act();
    Przekladniki[2]->Act();
    Przekladniki[3]->Act();
    return dV - dV1;
}

void TNESt3::EStParams(double const /*i_crc*/) const {}

void TNESt3::Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) {
    TBrake::Init(PP, HPP, LPP, BP, BDF);
    ValveRes->CreatePress(PP);
    BrakeCyl->CreatePress(BP);
    BrakeRes->CreatePress(PP);
    CntrlRes = std::make_shared<TReservoir>();
    CntrlRes->CreateCap(15.0);
    CntrlRes->CreatePress(HPP);
    BrakeStatus = BP > 1.0 ? 1 : 0;
    Miedzypoj = std::make_shared<TReservoir>();
    Miedzypoj->CreateCap(5.0);
    Miedzypoj->CreatePress(PP);

    BVM = 1.0 / (HPP - 0.05 - LPP) * MaxBP;

    BrakeDelayFlag = BDF;

    Zamykajacy = false;

    auto const &frictionmaterial{*FM};
    auto const &frictionmaterialtype{typeid(frictionmaterial)};
    if (frictionmaterialtype == typeid(TDisk1) || frictionmaterialtype == typeid(TDisk2)) // jesli zeliwo to schodz
        RapidStaly = true;
    else
        RapidStaly = false;
}

double TNESt3::GetCRP() {
    return CntrlRes->P();
}

void TNESt3::CheckState(double const BCP, double & /*dV1*/) {
    double const VVP{ValveRes->P()};
    double const CVP{CntrlRes->P()};
    double const MPP{Miedzypoj->P()};

    if (BCP < 0.25 && VVP + 0.08 > CVP)
        Przys_blok = false;

    // sprawdzanie stanu
    if (VVP + 0.01 + BCP / BVM < CVP - 0.05 && Przys_blok)
        BrakeStatus |= b_on | b_hld; // hamowanie stopniowe;
    else if (VVP - 0.01 + (BCP - 0.1) / BVM > CVP - 0.05)
        BrakeStatus &= ~(b_on | b_hld); // luzowanie;
    else if (VVP + BCP / BVM > CVP - 0.05)
        BrakeStatus &= ~b_on; // zatrzymanie napelaniania;
    else if (VVP + (BCP - 0.1) / BVM < CVP - 0.05 && BCP > 0.25) // zatrzymanie luzowania
        BrakeStatus |= b_hld;

    if ((BrakeStatus & b_hld) == 0)
        SoundFlag |= sf_CylU;

    // poczatek hamowania
    if (VVP + 0.10 < CVP && BCP < 0.25 && false == Przys_blok) {
        ValveRes->CreatePress(0.1 * VVP);
        SoundFlag |= sf_Acc;
        ValveRes->Act();
        Przys_blok = true;
    }

    if (BCP > 0.5)
        Zamykajacy = true;
    else if (VVP - 0.6 < MPP)
        Zamykajacy = false;
}

void TNESt3::CheckReleaser(double const dt) {
    double const VVP{ValveRes->P()};
    double const CVP{CntrlRes->P()};

    // odluzniacz automatyczny
    if ((BrakeStatus & b_rls) == b_rls) {
        CntrlRes->Flow(PF(CVP, 0, 0.02) * dt);
        if (CVP < VVP + 0.3 || false == autom)
            BrakeStatus &= ~b_rls;
    }
}

double TNESt3::CVs(double const /*BP*/) const {
    double const CVP{CntrlRes->P()};
    double const MPP{Miedzypoj->P()};

    // przeplyw ZS <-> PG
    if (MPP < CVP - 0.17)
        return 0.0;
    else if (MPP > CVP - 0.08)
        return Nozzles[dSd];
    else
        return Nozzles[dSm];
}

double TNESt3::BVs(double const BCP) const {
    double const CVP{CntrlRes->P()};
    double const MPP{Miedzypoj->P()};

    // przeplyw ZP <-> rozdzielacz
    if (MPP < CVP - 0.3)
        return Nozzles[dP];
    else if (BCP < 0.5) {
        if (true == Zamykajacy)
            return Nozzles[dPm]; // 1.25
        else
            return Nozzles[dPd];
    } else
        return 0.0;
}

void TNESt3::PLC(double const mass) {
    LoadC = 1.0 + (mass < LoadM ? (TareBP + (MaxBP - TareBP) * (mass - TareM) / (LoadM - TareM)) / MaxBP - 1.0 : 0.0);
}

void TNESt3::ForceEmptiness() {
    ValveRes->CreatePress(0);
    BrakeRes->CreatePress(0);
    Miedzypoj->CreatePress(0);
    CntrlRes->CreatePress(0);

    ValveRes->Act();
    BrakeRes->Act();
    Miedzypoj->Act();
    CntrlRes->Act();
}

void TNESt3::SetLP(double const TM, double const LM, double const TBP) {
    TareM = TM;
    LoadM = LM;
    TareBP = TBP;
}

void TNESt3::SetLBP(double const P) {
    LBP = P;
}

void TNESt3::SetSize(int const size, std::string const &params) {
    static double const dNO1l = 1.250;
    static double const dNT1l = 0.510;
    static double const dOO1l = 0.907;
    static double const dOT1l = 0.524;

    PrzekRapid.reset();
    PrzekPoslizg.reset();
    PrzekED.reset();
    PrzekCiagly.reset();
    PrzekPZZ.reset();

    if (contains(params, "ESt3")) {
        Podskok = 0.7;
        Przekladniki[1] = std::make_shared<TRura>();
        Przekladniki[3] = std::make_shared<TRura>();
    } else {
        Podskok = -1.0;
        PrzekRapid = std::make_shared<TRapid>();
        Przekladniki[1] = PrzekRapid;

        if (contains(params, "-ED")) {
            PrzekRapid->SetRapidParams(2, 18);
            PrzekED = std::make_shared<TPrzekED>();
            Przekladniki[3] = PrzekED;
        } else {
            if (contains(params, "-s216"))
                PrzekRapid->SetRapidParams(2, 16);
            else
                PrzekRapid->SetRapidParams(2, 0);
            PrzekPoslizg = std::make_shared<TPrzeciwposlizg>();
            Przekladniki[3] = PrzekPoslizg;
        }
    }

    if (contains(params, "AL2")) {
        PrzekCiagly = std::make_shared<TPrzekCiagly>();
        Przekladniki[2] = PrzekCiagly;
    } else if (contains(params, "PZZ")) {
        PrzekPZZ = std::make_shared<TPrzek_PZZ>();
        Przekladniki[2] = PrzekPZZ;
    } else
        Przekladniki[2] = std::make_shared<TRura>();

    if (contains(params, "3d") || contains(params, "4d")) {
        autom = false;
    } else
        autom = true;
    if (contains(params, "HBG300"))
        HBG300 = 1.0;
    else
        HBG300 = 0.0;

    switch (size) {
    case 16: { // dON,dOO,dTN,dTO,dP,dS
        Nozzles[dON] = 5.0;
        Nozzles[dOO] = 3.4;
        Nozzles[dTN] = 2.0;
        Nozzles[dTO] = 1.75;
        Nozzles[dP] = 3.8;
        Nozzles[dPd] = 2.70;
        Nozzles[dPm] = 1.25;
        Nozzles[dPO] = Nozzles[dON];
        Nozzles[dPT] = Nozzles[dTN];
        break;
    }
    case 14: {
        Nozzles[dON] = 4.3;
        Nozzles[dOO] = 2.85;
        Nozzles[dTN] = 1.83;
        Nozzles[dTO] = 1.57;
        Nozzles[dP] = 3.4;
        Nozzles[dPd] = 2.20;
        Nozzles[dPm] = 1.10;
        Nozzles[dPO] = Nozzles[dON];
        Nozzles[dPT] = Nozzles[dTN];
        break;
    }
    case 12: {
        Nozzles[dON] = 3.7;
        Nozzles[dOO] = 2.50;
        Nozzles[dTN] = 1.65;
        Nozzles[dTO] = 1.39;
        Nozzles[dP] = 2.65;
        Nozzles[dPd] = 1.80;
        Nozzles[dPm] = 0.85;
        Nozzles[dPO] = Nozzles[dON];
        Nozzles[dPT] = Nozzles[dTN];
        break;
    }
    case 10: {
        Nozzles[dON] = 3.1;
        Nozzles[dOO] = 2.0;
        Nozzles[dTN] = 1.35;
        Nozzles[dTO] = 1.13;
        Nozzles[dP] = 1.6;
        Nozzles[dPd] = 1.55;
        Nozzles[dPm] = 0.7;
        Nozzles[dPO] = Nozzles[dON];
        Nozzles[dPT] = Nozzles[dTN];
        break;
    }
    case 200: {
        Nozzles[dON] = dNO1l;
        Nozzles[dOO] = dOO1l / 1.15;
        Nozzles[dTN] = dNT1l;
        Nozzles[dTO] = dOT1l;
        Nozzles[dP] = 7.4;
        Nozzles[dPd] = 5.3;
        Nozzles[dPm] = 2.5;
        Nozzles[dPO] = 7.28;
        Nozzles[dPT] = 2.96;
        break;
    }
    case 375: {
        Nozzles[dON] = dNO1l;
        Nozzles[dOO] = dOO1l / 1.15;
        Nozzles[dTN] = dNT1l;
        Nozzles[dTO] = dOT1l;
        Nozzles[dP] = 13.0;
        Nozzles[dPd] = 9.6;
        Nozzles[dPm] = 4.4;
        Nozzles[dPO] = 9.92;
        Nozzles[dPT] = 3.99;
        break;
    }
    case 150: {
        Nozzles[dON] = dNO1l;
        Nozzles[dOO] = dOO1l;
        Nozzles[dTN] = dNT1l;
        Nozzles[dTO] = dOT1l;
        Nozzles[dP] = 5.8;
        Nozzles[dPd] = 4.1;
        Nozzles[dPm] = 1.9;
        Nozzles[dPO] = 6.33;
        Nozzles[dPT] = 2.58;
        break;
    }
    case 100: {
        Nozzles[dON] = dNO1l;
        Nozzles[dOO] = dOO1l;
        Nozzles[dTN] = dNT1l;
        Nozzles[dTO] = dOT1l;
        Nozzles[dP] = 4.2;
        Nozzles[dPd] = 2.9;
        Nozzles[dPm] = 1.4;
        Nozzles[dPO] = 5.19;
        Nozzles[dPT] = 2.14;
        break;
    }
    default: {
        Nozzles[dON] = 0;
        Nozzles[dOO] = 0;
        Nozzles[dTN] = 0;
        Nozzles[dTO] = 0;
        Nozzles[dP] = 0;
        Nozzles[dPd] = 0;
        Nozzles[dPm] = 0;
    }
    }

    Nozzles[dSd] = 1.1;
    Nozzles[dSm] = 0.9;

    // przeliczanie z mm^2 na l/m
    for (auto &nozzle : Nozzles)
        nozzle = d2A(nozzle);
    for (int i = 1; i < 4; ++i) {
        Przekladniki[i]->BrakeRes = BrakeRes;
        Przekladniki[i]->CreateCap(i);
        Przekladniki[i]->CreatePress(BrakeCyl->P());
        if (i < 3)
            Przekladniki[i]->Next = Przekladniki[i + 1];
        else
            Przekladniki[i]->Next = BrakeCyl;
    }
}

// ---- hamulce.cpp: the driver's handles (task driving 10, step 5) ----

double const TFV4aM::pos_table[11] = {-2, 6, -1, 0, -2, 1, 4, 6, 0, 0, 0};
double const TMHZ_EN57::pos_table[11] = {-1, 10, -1, 0, 0, 2, 9, 10, 0, 0, 0};
double const TMHZ_K5P::pos_table[11] = {-1, 3, -1, 0, 1, 1, 2, 3, 0, 0, 0};
double const TMHZ_6P::pos_table[11] = {-1, 4, -1, 0, 2, 2, 3, 4, 0, 0, 0};
double const TM394::pos_table[11] = {-1, 5, -1, 0, 1, 2, 4, 5, 0, 0, 0};
double const TH14K1::BPT_K[6][2] = {{10, 0}, {4, 1}, {0, 1}, {4, 0}, {4, -1}, {15, -1}};
double const TH14K1::pos_table[11] = {-1, 4, -1, 0, 1, 2, 3, 4, 0, 0, 0};
double const TSt113::BPT_K[6][2] = {{10, 0}, {4, 1}, {0, 1}, {4, 0}, {4, -1}, {15, -1}};
double const TSt113::BEP_K[7] = {0, -1, 1, 0, 0, 0, 0};
double const TSt113::pos_table[11] = {-1, 5, -1, 0, 2, 3, 4, 5, 0, 0, 1};
double const TFVel6::pos_table[11] = {-1, 6, -1, 0, 6, 4, 4.7, 5, -1, 0, 1};
double const TFVE408::pos_table[11] = {0, 10, 0, 0, 10, 7, 8, 9, 0, 1, 5};

double PR(double P1, double P2) {
    double PH = std::max(P1, P2) + 0.1;
    double PL = P1 + P2 - PH + 0.2;
    return (P2 - P1) / (1.13 * PH - PL);
}

bool is_EQ(double pos, double i_pos) {
    return pos <= i_pos + 0.5 && pos > i_pos - 0.5;
}

//---KRANY---

double TDriverHandle::GetPF(double const i_bcp, double PP, double HP, double dt, double ep) {
    return 0;
}

void TDriverHandle::Init(double Press) {
    Time = false;
    TimeEP = false;
}

void TDriverHandle::SetReductor(double nAdj) {}

double TDriverHandle::GetCP() {
    return 0;
}

double TDriverHandle::GetEP() {
    return 0;
}

double TDriverHandle::GetRP() {
    return 0;
}

double TDriverHandle::GetSound(int i) {
    return 0;
}

double TDriverHandle::GetPos(int i) {
    return 0;
}

double TDriverHandle::GetEP(double pos) {
    return 0;
}

void TDriverHandle::OvrldButton(bool Active) {
    ManualOvrldActive = Active;
}

void TDriverHandle::SetUniversalFlag(int flag) {
    UniversalFlag = flag;
}

//---FV4a/M---

double TFV4aM::GetPF(double i_bcp, double PP, double HP, double dt, double ep) {
    int const LBDelay{100};
    double const xpM{0.3}; // mnoznik membrany komory pod

    for (auto &soundvalue : Sounds)
        soundvalue = 0;

    // na wszelki wypadek, zeby nie wyszlo poza zakres
    i_bcp = std::clamp(i_bcp, -1.999, 5.999);

    double DP{0.0};
    if (TP > 0.0) {
        // jesli czasowy jest niepusty
        DP = 0.045; // 2.5 w 55 sekund (5,35->5,15 w PG)
        TP -= DP * dt;
        Sounds[s_fv4a_t] = DP;
    } else {
        TP = 0.0;
    }

    if (XP > 0) {
        // jesli komora pod niepusta jest niepusty
        DP = 2.5;
        Sounds[s_fv4a_x] = DP * XP;
        XP -= dt * DP * 2.0; // od cisnienia 5 do 0 w 10 sekund ((5-0)*dt/10)
    } else {
        XP = 0.0;
    }

    double pom;
    if (EQ(i_bcp, -1.0))
        pom = std::min(HP, 5.4 + RedAdj);
    else
        pom = std::min(CP, HP);

    if (pom > RP + 0.25)
        Fala = true;
    if (Fala) {
        if (pom > RP + 0.3)
            XP = XP - 20.0 * PR(pom, XP) * dt;
        else
            Fala = false;
    }

    double LimPP = std::min(LPP_RP(i_bcp) + TP * 0.08 + RedAdj, HP); // pozycja + czasowy lub zasilanie

    // zbiornik sterujacy
    if (LimPP > CP)
        CP += 5.0 * 60.0 * std::min(std::abs(LimPP - CP), 0.05) * PR(CP, LimPP) * dt; // podwyzszanie szybkie
    else
        CP += 13 * std::min(std::abs(LimPP - CP), 0.05) * PR(CP, LimPP) * dt;

    LimPP = pom; // cp
    double const dpPipe = std::min(HP, LimPP + XP * xpM);

    double const ActFlowSpeed = BPT[std::lround(i_bcp) + 2][0];

    double dpMainValve = dpPipe > PP ? -PFVa(HP, PP, ActFlowSpeed / LBDelay, dpPipe, 0.4)
                                     : PFVd(PP, 0, ActFlowSpeed / LBDelay, dpPipe, 0.4);

    if (EQ(i_bcp, -1)) {
        if (TP < 5)
            TP += dt;
        if (TP < 1)
            TP -= 0.5 * dt;
    }

    if (EQ(i_bcp, 0) && TP > 2)
        dpMainValve *= 1.5;

    ep = dpPipe;
    if (EQ(i_bcp, 0) || RP > ep)
        RP += PF(RP, ep, 0.0007) * dt; // powolne wzrastanie, ale szybsze na jezdzie;
    else
        RP += PF(RP, ep, 0.000093 / 2 * 2) * dt; // powolne wzrastanie i to bardzo
    // jednak trzeba wydluzyc, bo obecnie zle dziala
    if (RP < ep && RP < BPT[std::lround(i_bcpno) + 2][1])
        RP += PF(RP, CP, 0.005) * dt;

    if (EQ(i_bcp, i_bcpno) || EQ(i_bcp, -2)) {
        DP = PF(0.0, PP, ActFlowSpeed / LBDelay);
        dpMainValve = DP;
        Sounds[s_fv4a_e] = DP;
        Sounds[s_fv4a_u] = 0.0;
        Sounds[s_fv4a_b] = 0.0;
        Sounds[s_fv4a_x] = 0.0;
    } else {
        if (dpMainValve > 0.0)
            Sounds[s_fv4a_b] = dpMainValve;
        else
            Sounds[s_fv4a_u] = -dpMainValve;
    }

    return dpMainValve * dt;
}

void TFV4aM::Init(double Press) {
    CP = Press;
    RP = Press;
}

void TFV4aM::SetReductor(double nAdj) {
    RedAdj = nAdj;
}

double TFV4aM::GetSound(int i) {
    if (i > 4)
        return 0;
    else
        return Sounds[i];
}

double TFV4aM::GetPos(int i) {
    return pos_table[i];
}

double TFV4aM::GetCP() {
    return TP;
}

double TFV4aM::GetRP() {
    return 5.0 + TP * 0.08 + RedAdj;
}

double TFV4aM::LPP_RP(double pos) const // cisnienie z zaokraglonej pozycji;
{
    auto const i_pos = static_cast<int>(2 + std::floor(pos)); // zaokraglone w dol
    return BPT[i_pos][1] + (BPT[i_pos + 1][1] - BPT[i_pos][1]) * (pos + 2 - i_pos); // interpolacja liniowa
}

bool TFV4aM::EQ(double pos, double i_pos) const {
    return pos <= i_pos + 0.5 && pos > i_pos - 0.5;
}

void TFV4aM::Exchange(double *values, bool store) {
    double *members[] = {&CP, &TP, &RP, &XP, &RedAdj};
    for (int i = 0; i < 5; ++i)
        store ? void(values[i] = *members[i]) : void(*members[i] = values[i]);
    store ? void(values[5] = Fala ? 1.0 : 0.0) : void(Fala = values[5] != 0.0);
}

//---MHZ_EN57--- manipulator hamulca zespolonego do EN57

double TMHZ_EN57::GetPF(double i_bcp, double PP, double HP, double dt, double ep) {
    static int const LBDelay = 100;

    double LimPP;
    double dpPipe;
    double dpMainValve;
    double ActFlowSpeed;
    double DP;
    double pom;

    for (auto &soundvalue : Sounds)
        soundvalue = 0;

    i_bcp = std::clamp(i_bcp, -0.999, 9.999); // na wszelki wypadek, zeby nie wyszlo poza zakres

    if (TP > 0 && CP > 4.9) {
        DP = OverloadPressureDecrease;
        if (EQ(i_bcp, 0))
            TP = TP - DP * dt;
        Sounds[s_fv4a_t] = DP;
    } else {
        TP = 0;
    }

    LimPP = std::min(LPP_RP(i_bcp) + TP * 0.08 + RedAdj, HP); // pozycja + czasowy lub zasilanie
    ActFlowSpeed = 4;

    double uop = UnbrakeOverPressure; // unbrake over pressure in actual state
    ManualOvrldActive = UniversalFlag & ub_HighPressure; // button is pressed
    if (ManualOvrld && !ManualOvrldActive) // no overpressure for not pressed button if it does not exists
        uop = 0;

    if (EQ(i_bcp, -1) && uop > 0)
        pom = std::min(HP, 5.4 + RedAdj + uop);
    else
        pom = std::min(CP, HP);

    if (LimPP > CP) // podwyzszanie szybkie
        CP = CP + 60 * std::min(std::abs(LimPP - CP), 0.05) * PR(CP, LimPP) * dt; // zbiornik sterujacy;
    else
        CP = CP + 13 * std::min(std::abs(LimPP - CP), 0.05) * PR(CP, LimPP) * dt; // zbiornik sterujacy

    LimPP = pom; // cp
    dpPipe = std::min(HP, LimPP);

    if (dpPipe > PP)
        dpMainValve = -PFVa(HP, PP, ActFlowSpeed / LBDelay, dpPipe, 0.4);
    else
        dpMainValve = PFVd(PP, 0, ActFlowSpeed / LBDelay, dpPipe, 0.4);

    if ((EQ(i_bcp, -1) && AutoOvrld) || (i_bcp < 0.5 && (UniversalFlag & ub_Overload))) {
        if (TP < 5)
            TP = TP + dt; // 5/10
        if (TP < OverloadMaxPressure)
            TP = TP - 0.5 * dt; // 5/10
    }

    if (EQ(i_bcp, 10) || EQ(i_bcp, -2)) {
        DP = PF(0, PP, 2 * ActFlowSpeed / LBDelay);
        dpMainValve = DP;
        Sounds[s_fv4a_e] = DP;
        Sounds[s_fv4a_u] = 0;
        Sounds[s_fv4a_b] = 0;
        Sounds[s_fv4a_x] = 0;
    } else {
        if (dpMainValve > 0)
            Sounds[s_fv4a_b] = dpMainValve;
        else
            Sounds[s_fv4a_u] = -dpMainValve;
    }

    if (i_bcp < 1.5)
        RP = std::max(0., 0.125 * i_bcp);
    else
        RP = std::min(1., 0.125 * i_bcp - 0.125);

    return dpMainValve * dt;
}

void TMHZ_EN57::Init(double Press) {
    CP = Press;
}

void TMHZ_EN57::SetReductor(double nAdj) {
    RedAdj = nAdj;
}

double TMHZ_EN57::GetSound(int i) {
    if (i > 4)
        return 0;
    else
        return Sounds[i];
}

double TMHZ_EN57::GetPos(int i) {
    return pos_table[i];
}

double TMHZ_EN57::GetCP() {
    return RP;
}

double TMHZ_EN57::GetRP() {
    return 5.0 + RedAdj;
}

double TMHZ_EN57::GetEP(double pos) {
    if (pos < 9.5)
        return std::clamp(0.125 * pos, 0., 1.);
    else
        return 0;
}

double TMHZ_EN57::LPP_RP(double pos) const // cisnienie z zaokraglonej pozycji;
{
    if (pos > 8.5)
        return 5.0 - 0.15 * pos - 0.35;
    else if (pos > 0.5)
        return 5.0 - 0.15 * pos - 0.1;
    else
        return 5.0;
}

void TMHZ_EN57::SetParams(bool AO, bool MO, double OverP, double, double OMP, double OPD) {
    AutoOvrld = AO;
    ManualOvrld = MO;
    UnbrakeOverPressure = std::max(0.0, OverP);
    Fala = OverP > 0.01;
    OverloadMaxPressure = OMP;
    OverloadPressureDecrease = OPD;
}

bool TMHZ_EN57::EQ(double pos, double i_pos) const {
    return pos <= i_pos + 0.5 && pos > i_pos - 0.5;
}

void TMHZ_EN57::Exchange(double *values, bool store) {
    double *members[] = {&CP, &TP, &RP, &RedAdj};
    for (int i = 0; i < 4; ++i)
        store ? void(values[i] = *members[i]) : void(*members[i] = values[i]);
}

//---MHZ_K5P--- manipulator hamulca zespolonego Knorr 5-ciopozycyjny

double TMHZ_K5P::GetPF(double i_bcp, double PP, double HP, double dt, double ep) {
    static int const LBDelay = 100;

    double LimCP;
    double dpPipe;
    double dpMainValve;
    double ActFlowSpeed;
    double DP;

    for (auto &soundvalue : Sounds)
        soundvalue = 0;

    i_bcp = std::clamp(i_bcp, -0.999, 2.999); // na wszelki wypadek, zeby nie wyszlo poza zakres

    if (TP > 0 && CP > 4.9) {
        DP = OverloadPressureDecrease;
        TP = TP - DP * dt;
        Sounds[s_fv4a_t] = DP;
    }

    if (EQ(i_bcp, 1)) // odcięcie - nie rób nic
        LimCP = CP;
    else if (i_bcp > 1) // hamowanie
        LimCP = 3.4;
    else // luzowanie
        LimCP = 5.0;
    LimCP = std::min(LimCP, HP); // pozycja + czasowy lub zasilanie
    ActFlowSpeed = 4;

    CP = CP + 9 * std::min(std::abs(LimCP - CP), 0.05) * PR(CP, LimCP) * dt; // zbiornik sterujacy

    double uop = UnbrakeOverPressure; // unbrake over pressure in actual state
    ManualOvrldActive = UniversalFlag & ub_HighPressure; // button is pressed
    if (ManualOvrld && !ManualOvrldActive) // no overpressure for not pressed button if it does not exists
        uop = 0;

    dpPipe = std::min(HP, CP + TP + RedAdj);

    if (EQ(i_bcp, -1)) {
        if (Fala) {
            dpPipe = 5.0 + TP + RedAdj + uop;
            ActFlowSpeed = 12;
        } else {
            ActFlowSpeed *= FillingStrokeFactor;
        }
    }

    if (dpPipe > PP)
        dpMainValve = -PFVa(HP, PP, ActFlowSpeed / LBDelay, dpPipe, 0.4);
    else
        dpMainValve = PFVd(PP, 0, ActFlowSpeed / LBDelay, dpPipe, 0.4);

    if (((EQ(i_bcp, -1) && AutoOvrld) || (i_bcp < 0.5 && (UniversalFlag & ub_Overload))) && TP < OverloadMaxPressure)
        TP = TP + 0.03 * dt;

    if (EQ(i_bcp, 3)) {
        DP = PF(0, PP, 2 * ActFlowSpeed / LBDelay);
        dpMainValve = DP;
        Sounds[s_fv4a_e] = DP;
        Sounds[s_fv4a_u] = 0;
        Sounds[s_fv4a_b] = 0;
        Sounds[s_fv4a_x] = 0;
    } else {
        if (dpMainValve > 0)
            Sounds[s_fv4a_b] = dpMainValve;
        else
            Sounds[s_fv4a_u] = -dpMainValve;
    }

    return dpMainValve * dt;
}

void TMHZ_K5P::Init(double Press) {
    CP = Press;
    Time = true;
    TimeEP = true;
}

void TMHZ_K5P::SetReductor(double nAdj) {
    RedAdj = nAdj;
}

double TMHZ_K5P::GetSound(int i) {
    if (i > 4)
        return 0;
    else
        return Sounds[i];
}

double TMHZ_K5P::GetPos(int i) {
    return pos_table[i];
}

double TMHZ_K5P::GetCP() {
    return CP;
}

double TMHZ_K5P::GetRP() {
    return 5.0 + TP + RedAdj;
}

void TMHZ_K5P::SetParams(bool AO, bool MO, double OverP, double FSF, double OMP, double OPD) {
    AutoOvrld = AO;
    ManualOvrld = MO;
    UnbrakeOverPressure = std::max(0.0, OverP);
    Fala = OverP > 0.01;
    OverloadMaxPressure = OMP;
    OverloadPressureDecrease = OPD;
    FillingStrokeFactor = 1 + FSF;
}

bool TMHZ_K5P::EQ(double pos, double i_pos) const {
    return pos <= i_pos + 0.5 && pos > i_pos - 0.5;
}

void TMHZ_K5P::Exchange(double *values, bool store) {
    double *members[] = {&CP, &TP, &RedAdj};
    for (int i = 0; i < 3; ++i)
        store ? void(values[i] = *members[i]) : void(*members[i] = values[i]);
}

//---MHZ_6P--- manipulator hamulca zespolonego 6-ciopozycyjny

double TMHZ_6P::GetPF(double i_bcp, double PP, double HP, double dt, double ep) {
    static int const LBDelay = 100;

    double LimCP;
    double dpPipe;
    double dpMainValve;
    double ActFlowSpeed;
    double DP;

    for (auto &soundvalue : Sounds)
        soundvalue = 0;

    i_bcp = std::clamp(i_bcp, -0.999, 3.999); // na wszelki wypadek, zeby nie wyszlo poza zakres

    if (TP > 0 && CP > 4.9) {
        DP = OverloadPressureDecrease;
        TP = TP - DP * dt;
        Sounds[s_fv4a_t] = DP;
    }

    if (EQ(i_bcp, 2)) // odcięcie - nie rób nic
        LimCP = CP;
    else if (i_bcp > 2.5) // hamowanie
        LimCP = 3.4;
    else // luzowanie
        LimCP = 5.0;
    LimCP = std::min(LimCP, HP); // pozycja + czasowy lub zasilanie
    ActFlowSpeed = 4;

    CP = CP + 9 * std::min(std::abs(LimCP - CP), 0.05) * PR(CP, LimCP) * dt; // zbiornik sterujacy

    dpPipe = std::min(HP, CP + TP + RedAdj);

    double uop = UnbrakeOverPressure; // unbrake over pressure in actual state
    ManualOvrldActive = UniversalFlag & ub_HighPressure; // button is pressed
    if (ManualOvrld && !ManualOvrldActive) // no overpressure for not pressed button if it does not exists
        uop = 0;

    if (EQ(i_bcp, -1)) {
        if (Fala) {
            dpPipe = 5.0 + TP + RedAdj + uop;
            ActFlowSpeed = 12;
        } else {
            ActFlowSpeed *= FillingStrokeFactor;
        }
    }

    if (dpPipe > PP)
        dpMainValve = -PFVa(HP, PP, ActFlowSpeed / LBDelay, dpPipe, 0.4);
    else
        dpMainValve = PFVd(PP, 0, ActFlowSpeed / LBDelay, dpPipe, 0.4);

    if (((EQ(i_bcp, -1) && AutoOvrld) || (i_bcp < 0.5 && (UniversalFlag & ub_Overload))) && TP < OverloadMaxPressure)
        TP = TP + 0.03 * dt;

    if (EQ(i_bcp, 4)) {
        DP = PF(0, PP, 2 * ActFlowSpeed / LBDelay);
        dpMainValve = DP;
        Sounds[s_fv4a_e] = DP;
        Sounds[s_fv4a_u] = 0;
        Sounds[s_fv4a_b] = 0;
        Sounds[s_fv4a_x] = 0;
    } else {
        if (dpMainValve > 0)
            Sounds[s_fv4a_b] = dpMainValve;
        else
            Sounds[s_fv4a_u] = -dpMainValve;
    }

    return dpMainValve * dt;
}

void TMHZ_6P::Init(double Press) {
    CP = Press;
    Time = true;
    TimeEP = true;
}

void TMHZ_6P::SetReductor(double nAdj) {
    RedAdj = nAdj;
}

double TMHZ_6P::GetSound(int i) {
    if (i > 4)
        return 0;
    else
        return Sounds[i];
}

double TMHZ_6P::GetPos(int i) {
    return pos_table[i];
}

double TMHZ_6P::GetCP() {
    return CP;
}

double TMHZ_6P::GetRP() {
    return 5.0 + TP + RedAdj;
}

void TMHZ_6P::SetParams(bool AO, bool MO, double OverP, double FSF, double OMP, double OPD) {
    AutoOvrld = AO;
    ManualOvrld = MO;
    UnbrakeOverPressure = std::max(0.0, OverP);
    Fala = OverP > 0.01;
    OverloadMaxPressure = OMP;
    OverloadPressureDecrease = OPD;
    FillingStrokeFactor = 1 + FSF;
}

bool TMHZ_6P::EQ(double pos, double i_pos) const {
    return pos <= i_pos + 0.5 && pos > i_pos - 0.5;
}

void TMHZ_6P::Exchange(double *values, bool store) {
    double *members[] = {&CP, &TP, &RedAdj};
    for (int i = 0; i < 3; ++i)
        store ? void(values[i] = *members[i]) : void(*members[i] = values[i]);
}

//---M394--- Matrosow

double TM394::GetPF(double i_bcp, double PP, double HP, double dt, double ep) {
    static int const LBDelay = 65;

    double LimPP;
    double dpPipe;
    double dpMainValve;
    double ActFlowSpeed;
    int BCP;

    BCP = static_cast<int>(std::lround(i_bcp));
    if (BCP < -1)
        BCP = 1;

    LimPP = std::min(BPT_394[BCP + 1][1], HP);
    ActFlowSpeed = BPT_394[BCP + 1][0];
    if (BCP == 1 || BCP == i_bcpno)
        LimPP = PP;
    if (BCP == 0)
        LimPP = LimPP + RedAdj;
    if (BCP != 2) {
        if (CP < LimPP)
            CP = CP + 4 * std::min(std::abs(LimPP - CP), 0.05) * PR(CP, LimPP) * dt; // zbiornik sterujacy
        else if (BCP == 0)
            CP = CP - 0.2 * dt / 100;
        else
            CP = CP + 4 * (1 + int(BCP != 3) + int(BCP > 4)) * std::min(std::abs(LimPP - CP), 0.05) * PR(CP, LimPP) * dt; // zbiornik sterujacy
    }

    LimPP = CP;
    dpPipe = std::min(HP, LimPP);

    dpMainValve = PF(dpPipe, PP, ActFlowSpeed / LBDelay) * dt;

    if (BCP == -1)
        dpMainValve = PF(HP, PP, ActFlowSpeed / LBDelay) * dt;

    if (BCP == i_bcpno)
        dpMainValve = PF(0, PP, ActFlowSpeed / LBDelay) * dt;

    return dpMainValve;
}

void TM394::Init(double Press) {
    CP = Press;
    Time = true;
}

void TM394::SetReductor(double nAdj) {
    RedAdj = nAdj;
}

double TM394::GetCP() {
    return CP;
}

double TM394::GetRP() {
    return std::max(5.0, CP) + RedAdj;
}

double TM394::GetPos(int i) {
    return pos_table[i];
}

void TM394::Exchange(double *values, bool store) {
    double *members[] = {&CP, &RedAdj};
    for (int i = 0; i < 2; ++i)
        store ? void(values[i] = *members[i]) : void(*members[i] = values[i]);
}

//---H14K1-- Knorr

double TH14K1::GetPF(double i_bcp, double PP, double HP, double dt, double ep) {
    int const LBDelay = 100; // szybkosc + zasilanie sterujacego
    double const NomPress = 5.0;

    auto BCP = static_cast<int>(std::lround(i_bcp));
    if (i_bcp < -1)
        BCP = 1;

    double LimPP = BPT_K[BCP + 1][1];
    if (LimPP < 0.0)
        LimPP = 0.5 * PP;
    else if (LimPP > 0.0)
        LimPP = PP;
    else
        LimPP = CP;
    double ActFlowSpeed = BPT_K[BCP + 1][0];

    CP = CP + 6 * std::min(std::abs(LimPP - CP), 0.05) * PR(CP, LimPP) * dt; // zbiornik sterujacy

    double dpMainValve = 0.0;

    if (BCP == -1)
        dpMainValve = PF(HP, PP, ActFlowSpeed / LBDelay) * dt;
    if (BCP == 0)
        dpMainValve = -PFVa(HP, PP, ActFlowSpeed / LBDelay, NomPress + RedAdj) * dt;
    if (BCP > 1 && PP > CP)
        dpMainValve = PFVd(PP, 0, ActFlowSpeed / LBDelay, CP) * dt;
    if (BCP == i_bcpno)
        dpMainValve = PF(0, PP, ActFlowSpeed / LBDelay) * dt;

    return dpMainValve;
}

void TH14K1::Init(double Press) {
    CP = Press;
    Time = true;
    TimeEP = true;
}

void TH14K1::SetReductor(double nAdj) {
    RedAdj = nAdj;
}

double TH14K1::GetCP() {
    return CP;
}

double TH14K1::GetRP() {
    return 5.0 + RedAdj;
}

double TH14K1::GetPos(int i) {
    return pos_table[i];
}

void TH14K1::Exchange(double *values, bool store) {
    double *members[] = {&CP, &RedAdj};
    for (int i = 0; i < 2; ++i)
        store ? void(values[i] = *members[i]) : void(*members[i] = values[i]);
}

//---St113-- Knorr EP

double TSt113::GetPF(double i_bcp, double PP, double HP, double dt, double ep) {
    static int const LBDelay = 100; // szybkosc + zasilanie sterujacego
    static double const NomPress = 5.0;

    double LimPP;
    double dpMainValve;
    double ActFlowSpeed;
    int BCP;

    CP = PP;

    BCP = static_cast<int>(std::lround(i_bcp));

    EPS = BEP_K[BCP + 1];

    if (BCP > 0)
        BCP = BCP - 1;

    if (BCP < -1)
        BCP = 1;
    LimPP = BPT_K[BCP + 1][1];
    if (LimPP < 0)
        LimPP = 0.5 * PP;
    else if (LimPP > 0)
        LimPP = PP;
    else
        LimPP = CP;
    ActFlowSpeed = BPT_K[BCP + 1][0];

    CP = CP + 6 * std::min(std::abs(LimPP - CP), 0.05) * PR(CP, LimPP) * dt; // zbiornik sterujacy

    dpMainValve = 0;

    if (BCP == -1)
        dpMainValve = PF(HP, PP, ActFlowSpeed / LBDelay) * dt;
    if (BCP == 0)
        dpMainValve = -PFVa(HP, PP, ActFlowSpeed / LBDelay, NomPress + RedAdj) * dt;
    if (BCP > 1 && PP > CP)
        dpMainValve = PFVd(PP, 0, ActFlowSpeed / LBDelay, CP) * dt;
    if (BCP == i_bcpno)
        dpMainValve = PF(0, PP, ActFlowSpeed / LBDelay) * dt;

    return dpMainValve;
}

double TSt113::GetCP() {
    return CP;
}

double TSt113::GetRP() {
    return 5.0 + RedAdj;
}

double TSt113::GetEP() {
    return EPS;
}

double TSt113::GetPos(int i) {
    return pos_table[i];
}

void TSt113::Init(double Press) {
    Time = true;
    TimeEP = true;
}

void TSt113::Exchange(double *values, bool store) {
    double *members[] = {&CP, &EPS, &RedAdj};
    for (int i = 0; i < 3; ++i)
        store ? void(values[i] = *members[i]) : void(*members[i] = values[i]);
}

//---FD1---

double TFD1::GetPF(double i_bcp, double PP, double HP, double dt, double ep) {
    double DP;
    double temp;

    temp = std::min(i_bcp * MaxBP, HP); // 0011
    DP = 10.0 * std::min(std::abs(temp - BP), 0.1) * PF(temp, BP, 0.0006 * (temp > BP ? 3.0 : 2.0)) * dt * Speed;
    BP = BP - DP;
    return -DP;
}

void TFD1::Init(double Press) {
    MaxBP = Press;
    Speed = 1.0;
}

double TFD1::GetCP() {
    return BP;
}

void TFD1::SetSpeed(double nSpeed) {
    Speed = nSpeed;
}

void TFD1::Exchange(double *values, bool store) {
    store ? void(values[0] = BP) : void(BP = values[0]);
}

//---KNORR---

double TH1405::GetPF(double i_bcp, double PP, double HP, double dt, double ep) {
    double DP;
    double temp;
    double A;

    PP = std::min(PP, MaxBP);
    if (i_bcp > 0.5) {
        temp = std::min(MaxBP, HP);
        A = 2 * (i_bcp - 0.5) * 0.0011;
        BP = std::max(BP, PP);
    } else {
        temp = 0;
        A = 0.2 * (0.5 - i_bcp) * 0.0033;
        BP = std::min(BP, PP);
    }
    DP = PF(temp, BP, A) * dt;
    BP = BP - DP;
    return -DP;
}

void TH1405::Init(double Press) {
    MaxBP = Press;
    Time = true;
}

double TH1405::GetCP() {
    return BP;
}

void TH1405::Exchange(double *values, bool store) {
    store ? void(values[0] = BP) : void(BP = values[0]);
}

//---FVel6---

double TFVel6::GetPF(double i_bcp, double PP, double HP, double dt, double ep) {
    static int const LBDelay = 100;

    double LimPP;
    double dpMainValve;
    double ActFlowSpeed;

    CP = PP;

    LimPP = std::min(5. * int(i_bcp < 3.5), HP);
    if (i_bcp >= 3.5 && (i_bcp < 4.3 || i_bcp > 5.5))
        ActFlowSpeed = 0;
    else if (i_bcp > 4.3 && i_bcp < 4.8)
        ActFlowSpeed = 4 * (i_bcp - 4.3); // konsultacje wawa1 - bylo 8;
    else if (i_bcp < 4)
        ActFlowSpeed = 2;
    else
        ActFlowSpeed = 4;
    dpMainValve = PF(LimPP, PP, ActFlowSpeed / LBDelay) * dt;

    Sounds[s_fv4a_e] = 0;
    Sounds[s_fv4a_u] = 0;
    Sounds[s_fv4a_b] = 0;
    if (i_bcp < 3.5)
        Sounds[s_fv4a_u] = -dpMainValve;
    else if (i_bcp < 4.8)
        Sounds[s_fv4a_b] = dpMainValve;
    else if (i_bcp < 5.5)
        Sounds[s_fv4a_e] = dpMainValve;

    if (i_bcp < -0.5)
        EPS = -1;
    else if (i_bcp > 0.5 && i_bcp < 4.7)
        EPS = 1;
    else
        EPS = 0;
    return dpMainValve;
}

double TFVel6::GetCP() {
    return CP;
}

double TFVel6::GetRP() {
    return 5.0;
}

double TFVel6::GetEP() {
    return EPS;
}

double TFVel6::GetPos(int i) {
    return pos_table[i];
}

double TFVel6::GetSound(int i) {
    if (i > 2)
        return 0;
    else
        return Sounds[i];
}

void TFVel6::Init(double Press) {
    Time = true;
    TimeEP = true;
}

void TFVel6::Exchange(double *values, bool store) {
    double *members[] = {&CP, &EPS};
    for (int i = 0; i < 2; ++i)
        store ? void(values[i] = *members[i]) : void(*members[i] = values[i]);
}

//---FVE408---

double TFVE408::GetPF(double i_bcp, double PP, double HP, double dt, double ep) {
    static int const LBDelay = 100;

    double LimPP;
    double dpMainValve;
    double ActFlowSpeed;

    CP = PP;

    LimPP = std::min(5. * int(i_bcp < 6.5), HP);
    if (i_bcp >= 6.5 && (i_bcp < 7.5 || i_bcp > 9.5))
        ActFlowSpeed = 0;
    else if (i_bcp > 7.5 && i_bcp < 8.5)
        ActFlowSpeed = 2; // konsultacje wawa1 - bylo 8;
    else if (i_bcp < 6.5)
        ActFlowSpeed = 2;
    else
        ActFlowSpeed = 4;
    dpMainValve = PF(LimPP, PP, ActFlowSpeed / LBDelay) * dt;

    Sounds[s_fv4a_e] = 0;
    Sounds[s_fv4a_u] = 0;
    Sounds[s_fv4a_b] = 0;
    if (i_bcp < 6.5)
        Sounds[s_fv4a_u] = -dpMainValve;
    else if (i_bcp < 8.5)
        Sounds[s_fv4a_b] = dpMainValve;
    else if (i_bcp < 9.5)
        Sounds[s_fv4a_e] = dpMainValve;

    if (is_EQ(i_bcp, 1))
        EPS = 1.15;
    else if (is_EQ(i_bcp, 2))
        EPS = 1.40;
    else if (is_EQ(i_bcp, 3))
        EPS = 2.64;
    else if (is_EQ(i_bcp, 4))
        EPS = 3.84;
    else if (is_EQ(i_bcp, 5))
        EPS = 3.99;
    else
        EPS = 0;

    return dpMainValve;
}

double TFVE408::GetCP() {
    return CP;
}

double TFVE408::GetEP() {
    return EPS;
}

double TFVE408::GetRP() {
    return 5.0;
}

double TFVE408::GetPos(int i) {
    return pos_table[i];
}

double TFVE408::GetSound(int i) {
    if (i > 2)
        return 0;
    else
        return Sounds[i];
}

void TFVE408::Init(double Press) {
    Time = true;
    TimeEP = false;
}

void TFVE408::Exchange(double *values, bool store) {
    double *members[] = {&CP, &EPS};
    for (int i = 0; i < 2; ++i)
        store ? void(values[i] = *members[i]) : void(*members[i] = values[i]);
}

} // namespace MaszynaHamulce
