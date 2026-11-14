/*  This file is part of TSRE5 (task driving 10, step 4).
 *
 *  It is a port of MaSzyna's brake distributors and block friction
 *  (McZapkie/hamulce.h, McZapkie/Oerlikon_ESt.h and McZapkie/friction.h at
 *  66a940d4, https://github.com/eu07/maszyna; (C) 2007-2014 Maciej Cierniak,
 *  youBy), and so is under the Mozilla Public License 2.0, as MaSzyna's code
 *  is: this Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this file,
 *  You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 *  MaSzyna's names, order and units are kept (gauge bar, litres, bar·litre
 *  as an amount of air, km/h, kN), so the two can be compared line by line;
 *  the classes are in a namespace of their own. Only what the MaSzyna pack's
 *  vehicles use: the distributors Mover.cpp builds for them (TWest, TKE,
 *  TNESt3, TLSt, TEStED, TEStEP2, TEStEP1) with their bases, reservoirs,
 *  relays and friction materials (step 4), and the driver's handles it
 *  builds (TFV4aM, TMHZ_EN57, TMHZ_K5P, TMHZ_6P, TM394, TH14K1, TSt113,
 *  TFVel6, TFVE408; the local TFD1, TH1405: step 5). Not TESt3, TESt3AL2,
 *  TCV1, TCV1L_TR, TFV4a, Ttest (no vehicle of the pack has them). No C++
 *  outside the standard library: a test can build it beside MaSzyna's own.
 *  TSRE's one addition: TDriverHandle::Exchange (a handle's state as
 *  numbers, for the scripts that keep it).
 */

#ifndef MASZYNAHAMULCE_H
#define MASZYNAHAMULCE_H

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <string>

namespace MaszynaHamulce {

// ---- friction.h ----

// The friction coefficient of a block (or disc) at N kN on it and Vel km/h.
class TFricMat {
public:
    virtual double GetFC(double N, double Vel);
    virtual ~TFricMat() = default;
};
class TP10Bg : public TFricMat {
public:
    double GetFC(double N, double Vel) override;
};
class TP10Bgu : public TFricMat {
public:
    double GetFC(double N, double Vel) override;
};
class TP10yBg : public TFricMat {
public:
    double GetFC(double N, double Vel) override;
};
class TP10yBgu : public TFricMat {
public:
    double GetFC(double N, double Vel) override;
};
class TP10 : public TFricMat {
public:
    double GetFC(double N, double Vel) override;
};
class TFR513 : public TFricMat {
public:
    double GetFC(double N, double Vel) override;
};
class TFR510 : public TFricMat {
public:
    double GetFC(double N, double Vel) override;
};
class TCosid : public TFricMat {
public:
    double GetFC(double N, double Vel) override;
};
class TDisk1 : public TFricMat {
public:
    double GetFC(double N, double Vel) override;
};
class TDisk2 : public TFricMat {
public:
    double GetFC(double N, double Vel) override;
};

// ---- hamulce.h ----

// Brake delay settings (BrakeDelays, BrakeDelayFlag).
static int const bdelay_G = 1;
static int const bdelay_P = 2;
static int const bdelay_R = 4;
static int const bdelay_M = 8;

// Brake states (BrakeStatus).
static int const b_off = 0;  // luzowanie
static int const b_hld = 1;  // trzymanie
static int const b_on = 2;   // napelnianie
static int const b_rfl = 4;  // uzupelnianie
static int const b_rls = 8;  // odluzniacz
static int const b_ep = 16;  // elektropneumatyczny
static int const b_asb = 32; // przeciwposlizg-wstrzymanie
static int const b_asb_unbrake = 64;
static int const b_dmg = 128; // wylaczony z dzialania

// Friction pairs (BM).
static int const bp_P10 = 0;
static int const bp_P10Bg = 2;
static int const bp_P10Bgu = 1;
static int const bp_LLBg = 4;
static int const bp_LLBgu = 3;
static int const bp_LBg = 6;
static int const bp_LBgu = 5;
static int const bp_KBg = 8;
static int const bp_KBgu = 7;
static int const bp_D1 = 9;
static int const bp_D2 = 10;
static int const bp_FR513 = 11;
static int const bp_Cosid = 12;
static int const bp_PKPBg = 13;
static int const bp_PKPBgu = 14;
static int const bp_MHS = 128; // magnetyczny hamulec szynowy
static int const bp_P10yBg = 15;
static int const bp_P10yBgu = 16;
static int const bp_FR510 = 17;

// Sound flags (SoundFlag).
static int const sf_Acc = 1;
static int const sf_BR = 2;
static int const sf_CylB = 4;
static int const sf_CylU = 8;
static int const sf_rel = 16;
static int const sf_ep = 32;

// The 1" brake pipe's cross-section in l/m.
static double const SpgD = 0.7917;
static double const SpO = 0.5067;

// The universal brake button's actions (UniversalFlag).
enum TUniversalBrake {
    ub_Release = 0x01,
    ub_UnlockPipe = 0x02,
    ub_HighPressure = 0x04,
    ub_Overload = 0x08,
    ub_AntiSlipBrake = 0x10
};

// utilities.h: 1 for 0 too.
inline double Sign(double x) {
    return x >= 0 ? 1.0 : -1.0;
}

// The flow of air (bar·l/s, + into P1's side) from P2 to P1 through an
// orifice S: choked below a pressure ratio of 0.5, eased within DP.
double PF(double const P1, double const P2, double const S, double const DP = 0.25);
// As PF, eased by the pressure ratio.
double PF1(double const P1, double const P2, double const S);
// A filling valve: PH into PL until PL reaches LIM, closing within DP.
double PFVa(double PH, double PL, double const S, double LIM, double const DP = 0.1);
// A venting valve: PH into PL until PH falls to LIM, closing within DP.
double PFVd(double PH, double PL, double const S, double LIM, double const DP = 0.1);

// A reservoir: Cap litres holding Vol bar·l, the step's flows gathered in
// dVol until Act.
class TReservoir {
protected:
    double Cap{1.0};
    double Vol{0.0};
    double dVol{0.0};

public:
    void CreateCap(double Capacity);
    void CreatePress(double Press);
    virtual double pa();
    virtual double P();
    void Flow(double dv);
    void Act();

    TReservoir() = default;
    virtual ~TReservoir() = default;
};

using PReservoir = TReservoir *;

// A brake cylinder: its pressure by the piston's stroke.
class TBrakeCyl : public TReservoir {
public:
    double pa() override;
    double P() override;
    using TReservoir::TReservoir;
};

// A vehicle's brake: its cylinders, auxiliary reservoir (ZP) and the
// distributor's chamber; the distributors below.
class TBrake {
protected:
    std::shared_ptr<TReservoir> BrakeCyl; // silownik
    std::shared_ptr<TReservoir> BrakeRes; // ZP
    std::shared_ptr<TReservoir> ValveRes; // komora wstepna
    int BCN = 0;                          // ilosc silownikow
    double BCM = 0.0;                     // przekladnia hamulcowa
    double BCA = 0.0;                     // laczny przekroj silownikow
    int BrakeDelays = 0;                  // dostepne opoznienia
    int BrakeDelayFlag = 0;               // aktualna nastawa
    std::shared_ptr<TFricMat> FM;         // material cierny
    double MaxBP = 0.0;                   // najwyzsze cisnienie
    int BA = 0;                           // osie hamowane
    int NBpA = 0;                         // klocki na os
    double SizeBR = 0.0;                  // rozmiar^2 ZP (w stosunku do 14")
    double SizeBC = 0.0;                  // rozmiar^2 CH (w stosunku do 14")
    bool DCV = false;                     // podwojny zawor zwrotny
    double ASBP = 0.0;                    // cisnienie hamulca pp
    double RV = 0.0;                      // rapid activation vehicle velocity threshold

    int UniversalFlag = 0;
    int BrakeStatus{b_off};
    int SoundFlag = 0;

public:
    // maksymalne cisnienie, promien, skok roboczy, pojemnosc ZP, ilosc
    // cylindrow, opoznienia hamulca, material klockow, osie hamowane,
    // klocki na os
    TBrake(double i_mbp, double i_bcr, double i_bcd, double i_brc, int i_bcn, int i_BD, int i_mat, int i_ba, int i_nbpa);
    virtual void Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF);

    double GetFC(double const Vel, double const N);
    // The step: the air it takes from (-) or gives to (+) the brake pipe at
    // PP, bar·l, at Vel km/h.
    virtual double GetPF(double const PP, double const dt, double const Vel);
    double GetBCF() const;
    // The air it takes from the main reservoir pipe at HP (-), bar·l.
    virtual double GetHPFlow(double const HP, double const dt);
    double GetBCP() const;
    virtual double GetEDBCP();
    double GetBRP() const;
    double GetVRP() const;
    virtual double GetCRP();
    bool SetBDF(int const nBDF);
    void Releaser(int const state);
    bool Releaser() const;
    virtual void SetEPS(double const nEPS);
    virtual void SetRM(double const RMR) {}
    virtual void SetRV(double const RVR) { RV = RVR; }
    virtual void SetLP(double const TM, double const LM, double const TBP) {}
    virtual void SetLBP(double const P) {}
    virtual void PLC(double const mass) {}
    void ASB(int state);
    int GetStatus() const;
    void SetASBP(double const Press);
    virtual void ForceEmptiness();
    virtual void ForceLeak(double const Amount);
    int GetSoundFlag();
    int GetBrakeStatus() const { return BrakeStatus; }
    void SetBrakeStatus(int const Status) { BrakeStatus = Status; }
    virtual void SetED(double const EDstate) {}
    virtual void SetUniversalFlag(int flag) { UniversalFlag = flag; }
    virtual ~TBrake() = default;
};

// Westinghouse's triple valve (W, K), with an EP brake and load weighing;
// the local brake into the cylinder by its double check valve.
class TWest : public TBrake {
private:
    double LBP = 0.0;    // cisnienie hamulca pomocniczego
    double dVP = 0.0;    // pobor powietrza wysokiego cisnienia
    double EPS = 0.0;    // stan elektropneumatyka
    double TareM = 0.0;  // masa proznego
    double LoadM = 0.0;  // i pelnego
    double TareBP = 0.0; // cisnienie dla proznego
    double LoadC = 0.0;  // wspolczynnik przystawki wazacej

public:
    void Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) override;
    void SetLBP(double const P) override;
    double GetPF(double const PP, double const dt, double const Vel) override;
    double GetHPFlow(double const HP, double const dt) override;
    void PLC(double const mass) override;
    void SetEPS(double const nEPS) override;
    void SetLP(double const TM, double const LM, double const TBP) override;
    using TBrake::TBrake;
};

// Oerlikon's ESt: a control reservoir (ZS) and its main slide valve.
class TESt : public TBrake {
protected:
    std::shared_ptr<TReservoir> CntrlRes; // zbiornik sterujący
    double BVM = 0.0;                     // przelozenie PG-CH

public:
    void Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) override;
    double GetPF(double const PP, double const dt, double const Vel) override;
    void EStParams(double i_crc) const;
    double GetCRP() override;
    void CheckState(double BCP, double &dV1);
    void CheckReleaser(double dt);
    double CVs(double BP) const;
    double BVs(double BCP) const;
    void ForceEmptiness() override;

    TESt(double i_mbp, double i_bcr, double i_bcd, double i_brc, int i_bcn, int i_BD, int i_mat, int i_ba, int i_nbpa)
        : TBrake(i_mbp, i_bcr, i_bcd, i_brc, i_bcn, i_BD, i_mat, i_ba, i_nbpa) {
        CntrlRes = std::make_shared<TReservoir>();
    }
};

// ESt4 with its rapid step and impulse chamber.
class TESt4R : public TESt {
private:
    bool RapidStatus = false;

protected:
    std::shared_ptr<TReservoir> ImplsRes; // komora impulsowa
    double RapidTemp = 0.0;               // aktualne, zmienne przelozenie

public:
    void Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) override;
    double GetPF(double const PP, double const dt, double const Vel) override;

    TESt4R(double i_mbp, double i_bcr, double i_bcd, double i_brc, int i_bcn, int i_BD, int i_mat, int i_ba, int i_nbpa)
        : TESt(i_mbp, i_bcr, i_bcd, i_brc, i_bcn, i_BD, i_mat, i_ba, i_nbpa) {
        ImplsRes = std::make_shared<TReservoir>();
    }
};

// LSt, a loco's: the local brake by its double check valve, the rapid
// ratio, the ED brake releasing it.
class TLSt : public TESt4R {
protected:
    double LBP = 0.0;    // cisnienie hamulca pomocniczego
    double RM = 0.0;     // przelozenie rapida
    double EDFlag = 0.0; // luzowanie hamulca z powodu zalaczonego ED

public:
    void Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) override;
    void SetLBP(double const P) override;
    void SetRM(double const RMR) override;
    double GetPF(double const PP, double const dt, double const Vel) override;
    double GetHPFlow(double const HP, double const dt) override;
    double GetEDBCP() override;
    void SetED(double const EDstate) override;

    using TESt4R::TESt4R;
};

// EStED, EP09's: ESt4 with its own relay, the rapid's control and an
// intermediate chamber.
class TEStED : public TLSt {
private:
    std::shared_ptr<TReservoir> Miedzypoj; // pojemnosc posrednia (urojona)
    double Nozzles[11]{};                  // dysze
    bool Zamykajacy = false;               // pamiec zaworka zamykajacego
    bool Przys_blok = false;               // blokada przyspieszacza
    double TareM = 0.0;
    double LoadM = 0.0;
    double TareBP = 0.0;
    double LoadC = 0.0;

public:
    void Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) override;
    double GetPF(double const PP, double const dt, double const Vel) override;
    double GetEDBCP() override;
    void PLC(double const mass) override;
    void SetLP(double const TM, double const LM, double const TBP) override;

    TEStED(double i_mbp, double i_bcr, double i_bcd, double i_brc, int i_bcn, int i_BD, int i_mat, int i_ba, int i_nbpa)
        : TLSt(i_mbp, i_bcr, i_bcd, i_brc, i_bcn, i_BD, i_mat, i_ba, i_nbpa) {
        Miedzypoj = std::make_shared<TReservoir>();
    }
};

// ESt with the EP2 two-wire EP brake: EPS > 0 applies, < 0 releases.
class TEStEP2 : public TLSt {
protected:
    double TareM = 0.0;
    double LoadM = 0.0;
    double TareBP = 0.0;
    double LoadC = 0.0;
    double EPS = 0.0;

public:
    void Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) override;
    double GetPF(double const PP, double const dt, double const Vel) override;
    void PLC(double const mass) override;
    void SetEPS(double const nEPS) override;
    void SetLP(double const TM, double const LM, double const TBP) override;
    virtual void EPCalc(double dt);

    using TLSt::TLSt;
};

// EP1: the EP brake proportional, EPS's fractional part.
class TEStEP1 : public TEStEP2 {
public:
    void EPCalc(double dt) override;
    void SetEPS(double const nEPS) override;

    using TEStEP2::TEStEP2;
};

// Knorr's KE.
class TKE : public TBrake {
private:
    std::shared_ptr<TReservoir> ImplsRes; // komora impulsowa
    std::shared_ptr<TReservoir> CntrlRes; // zbiornik sterujący
    std::shared_ptr<TReservoir> Brak2Res; // zbiornik pomocniczy 2
    bool RapidStatus = false;
    double BVM = 0.0;
    double TareM = 0.0;
    double LoadM = 0.0;
    double TareBP = 0.0;
    double LoadC = 0.0;
    double RM = 0.0;
    double LBP = 0.0;

public:
    void Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) override;
    void SetRM(double const RMR) override;
    double GetPF(double const PP, double const dt, double const Vel) override;
    double GetHPFlow(double const HP, double const dt) override;
    double GetCRP() override;
    void CheckState(double const BCP, double &dV1);
    void CheckReleaser(double const dt);
    double CVs(double const BP) const;
    double BVs(double const BCP) const;
    void PLC(double const mass) override;
    void SetLP(double const TM, double const LM, double const TBP) override;
    void SetLBP(double const P) override;
    void ForceEmptiness() override;

    TKE(double i_mbp, double i_bcr, double i_bcd, double i_brc, int i_bcn, int i_BD, int i_mat, int i_ba, int i_nbpa)
        : TBrake(i_mbp, i_bcr, i_bcd, i_brc, i_bcn, i_BD, i_mat, i_ba, i_nbpa) {
        ImplsRes = std::make_shared<TReservoir>();
        CntrlRes = std::make_shared<TReservoir>();
        Brak2Res = std::make_shared<TReservoir>();
    }
};

// ---- Oerlikon_ESt.h ----

static int const dMAX = 11; // dysze
static int const dON = 0;   // osobowy napelnianie (+ZP)
static int const dOO = 1;   // osobowy oproznianie
static int const dTN = 2;   // towarowy napelnianie (+ZP)
static int const dTO = 3;   // towarowy oproznianie
static int const dP = 4;    // zbiornik pomocniczy
static int const dSd = 5;   // zbiornik sterujacy
static int const dSm = 6;   // zbiornik sterujacy
static int const dPd = 7;   // duzy przelot zamykajcego
static int const dPm = 8;   // maly przelot zamykajacego
static int const dPO = 9;   // zasilanie pomocniczego O
static int const dPT = 10;  // zasilanie pomocniczego T

// The mm diameter's cross-section in l/m.
double d2A(double const d);

// A relay (przekladnik): its own chamber driving the next one's.
class TPrzekladnik : public TReservoir {
public:
    std::shared_ptr<TReservoir> BrakeRes;
    std::shared_ptr<TReservoir> Next;

    using TReservoir::TReservoir;
    virtual void Update(double dt);
};

// None: a pipe through.
class TRura : public TPrzekladnik {
public:
    using TPrzekladnik::TPrzekladnik;
    double P(void) override;
    void Update(double dt) override;
};

// A pipe filling, venting while the wheels slide.
class TPrzeciwposlizg : public TRura {
private:
    bool Poslizg{false};

public:
    void SetPoslizg(bool flag);
    void Update(double dt) override;
};

// The two-step (rapid) relay.
class TRapid : public TPrzekladnik {
private:
    bool RapidStatus = false;
    double RapidMult = 0.0;
    double DN = 0.0;
    double DL = 0.0;

public:
    void SetRapidParams(double mult, double size);
    void SetRapidStatus(bool rs);
    void Update(double dt) override;
    using TPrzekladnik::TPrzekladnik;
};

// AL2: the relay by the load.
class TPrzekCiagly : public TPrzekladnik {
private:
    double Mult = 0.0;

public:
    void SetMult(double m);
    void Update(double dt) override;
    using TPrzekladnik::TPrzekladnik;
};

// PZZ: a double check valve.
class TPrzek_PZZ : public TPrzekladnik {
private:
    double LBP = 0.0;

public:
    void SetLBP(double P);
    void Update(double dt) override;
    using TPrzekladnik::TPrzekladnik;
};

class TPrzekZalamany : public TPrzekladnik {
public:
    using TPrzekladnik::TPrzekladnik;
};

// -ED: a pipe filling, venting above MaxP.
class TPrzekED : public TRura {
private:
    double MaxP = 0.0;

public:
    void SetP(double P);
    void Update(double dt) override;
    using TRura::TRura;
};

// ESt3, ESt4, ESt3AL2, NESt3 and their kin: Oerlikon's ESt "na bogato",
// relays chosen by its BrakeValve string (SetSize).
class TNESt3 : public TBrake {
private:
    std::shared_ptr<TReservoir> CntrlRes;  // zbiornik sterujacy
    std::shared_ptr<TReservoir> Miedzypoj; // pojemnosc posrednia (urojona)
    std::array<std::shared_ptr<TPrzekladnik>, 4> Przekladniki; // od 1
    std::shared_ptr<TRapid> PrzekRapid;
    std::shared_ptr<TPrzeciwposlizg> PrzekPoslizg;
    std::shared_ptr<TPrzekED> PrzekED;
    std::shared_ptr<TPrzekCiagly> PrzekCiagly;
    std::shared_ptr<TPrzek_PZZ> PrzekPZZ;
    // Zeroed: MaSzyna leaves dPO and dPT unset for a size it does not know.
    double Nozzles[dMAX]{};
    double BVM = 0.0;
    bool Zamykajacy = true;
    bool Przys_blok = true;
    bool RapidStatus = true;
    bool RapidStaly = true;
    double LoadC = 0.0;
    double TareM = 0.0;
    double LoadM = 0.0;
    double TareBP = 0.0;
    double HBG300 = 0.0; // zawor ograniczajacy cisnienie
    double Podskok = 0.0; // podskok preznosci poczatkowej
    bool autom = true;   // odluzniacz samoczynny
    double LBP = 0.0;

public:
    using TBrake::TBrake;
    void Init(double const PP, double const HPP, double const LPP, double const BP, int const BDF) override;
    double GetPF(double const PP, double const dt, double const Vel) override;
    void EStParams(double i_crc) const;
    double GetCRP() override;
    void CheckState(double const BCP, double &dV1);
    void CheckReleaser(double const dt);
    double CVs(double const BP) const;
    double BVs(double const BCP) const;
    void SetSize(int const size, std::string const &params);
    void PLC(double const mass) override;
    void SetLP(double const TM, double const LM, double const TBP) override;
    void ForceEmptiness() override;
    void SetLBP(double const P) override;
};

// ---- hamulce.h: the driver's handles (task driving 10, step 5) ----

// The FV4a's sounds (Sounds[]).
static int const s_fv4a_b = 0; // hamowanie
static int const s_fv4a_u = 1; // luzowanie
static int const s_fv4a_e = 2; // hamowanie nagle
static int const s_fv4a_x = 3; // wyplyw sterujacego fala
static int const s_fv4a_t = 4; // wyplyw z czasowego

// A handle's positions by function (GetPos).
static int const bh_MIN = 0; // minimalna pozycja
static int const bh_MAX = 1; // maksymalna pozycja
static int const bh_FS = 2;  // napelnianie uderzeniowe //jesli nie ma, to jazda
static int const bh_RP = 3;  // jazda
static int const bh_NP = 4;  // odciecie - podwojna trakcja
static int const bh_MB = 5;  // odciecie - utrzymanie stopnia hamowania/pierwszy 1 stopien hamowania
static int const bh_FB = 6;  // pelne
static int const bh_EB = 7;  // nagle
static int const bh_EPR = 8; // ep - luzowanie
static int const bh_EPN = 9; // ep - utrzymanie
static int const bh_EPB = 10; // ep - hamowanie

// The FV4a's positions -2 to 6: { flow speed, pipe pressure }.
static double const BPT[9][2] = {{0, 5.0}, {7, 5.0}, {2.0, 5.0}, {4.5, 4.6}, {4.5, 4.2}, {4.5, 3.8}, {4.5, 3.4}, {4.5, 2.8}, {8, 2.8}};
// Matrosov 394's positions -1 to 5.
static double const BPT_394[7][2] = {{13, 10.0}, {5, 5.0}, {0, -1}, {5, -1}, {5, 0.0}, {5, 0.0}, {18, 0.0}};

// The pressure ratio's flow driver (P2 - P1) / (1.13 PH - PL).
double PR(double P1, double P2);
// A position in the detent i_pos (within ±0.5).
bool is_EQ(double pos, double i_pos);

// A driver's brake handle (kran maszynisty): GetPF the air it takes from
// the brake pipe at PP this step (bar·l, + out of the pipe, - into it from
// the supply at HP), its position i_bcp (MaSzyna's BrakeCtrlPosR).
class TDriverHandle {
protected:
    bool AutoOvrld = false;         // czy jest asymilacja automatyczna na pozycji -1
    bool ManualOvrld = false;       // czy jest asymilacja reczna przyciskiem
    bool ManualOvrldActive = false; // czy jest wcisniety przycisk asymilacji
    int UniversalFlag = 0;          // flaga wcisnietych przyciskow uniwersalnych
    int i_bcpno = 6;

public:
    bool Time = false;
    bool TimeEP = false;
    double Sounds[5]; // wielkosci przeplywow dla dzwiekow

    virtual double GetPF(double i_bcp, double PP, double HP, double dt, double ep);
    virtual void Init(double Press);
    virtual double GetCP();
    virtual double GetEP();
    virtual double GetRP();
    virtual void SetReductor(double nAdj);
    virtual double GetSound(int i);
    virtual double GetPos(int i);
    virtual double GetEP(double pos);
    virtual void SetParams(bool AO, bool MO, double, double, double OMP, double OPD) {}
    virtual void OvrldButton(bool Active);
    virtual void SetUniversalFlag(int flag);
    // TSRE's: its state (what GetPF changes) to and from `values`
    // (StateCount numbers), so a script keeps it between steps.
    static constexpr int StateCount = 6;
    virtual void Exchange(double *values, bool store) {}
    TDriverHandle() { std::fill(std::begin(Sounds), std::end(Sounds), 0.0); }
    virtual ~TDriverHandle() = default;
};

// FV4a/M (BrakeHandle=FV4a): the control chamber, the time chamber (the
// high-pressure release), the reductor and its chamber (the wave).
class TFV4aM : public TDriverHandle {
private:
    double CP = 0.0;     // zbiornik sterujący
    double TP = 0.0;     // zbiornik czasowy
    double RP = 0.0;     // zbiornik redukcyjny
    double XP = 0.0;     // komora powietrzna w reduktorze
    double RedAdj = 0.0; // dostosowanie reduktora cisnienia (krecenie kapturkiem)
    bool Fala = false;
    static double const pos_table[11];
    double LPP_RP(double pos) const;
    bool EQ(double pos, double i_pos) const;

public:
    double GetPF(double i_bcp, double PP, double HP, double dt, double ep) override;
    void Init(double Press) override;
    void SetReductor(double nAdj) override;
    double GetSound(int i) override;
    double GetPos(int i) override;
    double GetCP() override;
    double GetRP() override;
    // TSRE's: the control chamber (GetCP is the time chamber).
    double ControlPressure() const { return CP; }
    void Exchange(double *values, bool store) override;
    using TDriverHandle::TDriverHandle;
};

// MHZ_EN57 (and MHZ_K8P).
class TMHZ_EN57 : public TDriverHandle {
private:
    double CP = 0.0;
    double TP = 0.0;
    double RP = 0.0;
    double RedAdj = 0.0;
    bool Fala = false;
    double UnbrakeOverPressure = 0.0;
    double OverloadMaxPressure = 1.0;
    double OverloadPressureDecrease = 0.045;
    static double const pos_table[11];
    double LPP_RP(double pos) const;
    bool EQ(double pos, double i_pos) const;

public:
    double GetPF(double i_bcp, double PP, double HP, double dt, double ep) override;
    void Init(double Press) override;
    void SetReductor(double nAdj) override;
    double GetSound(int i) override;
    double GetPos(int i) override;
    double GetCP() override;
    double GetRP() override;
    double GetEP(double pos) override;
    void SetParams(bool AO, bool MO, double OverP, double, double OMP, double OPD) override;
    double ControlPressure() const { return CP; }
    void Exchange(double *values, bool store) override;
    using TDriverHandle::TDriverHandle;
};

// MHZ_K5P: Knorr's five positions.
class TMHZ_K5P : public TDriverHandle {
private:
    double CP = 0.0;
    double TP = 0.0;
    double RedAdj = 0.0;
    bool Fala = false;
    double UnbrakeOverPressure = 0.0;
    double OverloadMaxPressure = 1.0;
    double OverloadPressureDecrease = 0.002;
    double FillingStrokeFactor = 1.0;
    static double const pos_table[11];
    bool EQ(double pos, double i_pos) const;

public:
    double GetPF(double i_bcp, double PP, double HP, double dt, double ep) override;
    void Init(double Press) override;
    void SetReductor(double nAdj) override;
    double GetSound(int i) override;
    double GetPos(int i) override;
    double GetCP() override;
    double GetRP() override;
    void SetParams(bool AO, bool MO, double, double, double OMP, double OPD) override;
    void Exchange(double *values, bool store) override;
    using TDriverHandle::TDriverHandle;
};

// MHZ_6P: six positions.
class TMHZ_6P : public TDriverHandle {
private:
    double CP = 0.0;
    double TP = 0.0;
    double RedAdj = 0.0;
    bool Fala = false;
    double UnbrakeOverPressure = 0.0;
    double OverloadMaxPressure = 1.0;
    double OverloadPressureDecrease = 0.002;
    double FillingStrokeFactor = 1.0;
    static double const pos_table[11];
    bool EQ(double pos, double i_pos) const;

public:
    double GetPF(double i_bcp, double PP, double HP, double dt, double ep) override;
    void Init(double Press) override;
    void SetReductor(double nAdj) override;
    double GetSound(int i) override;
    double GetPos(int i) override;
    double GetCP() override;
    double GetRP() override;
    void SetParams(bool AO, bool MO, double, double, double OMP, double OPD) override;
    void Exchange(double *values, bool store) override;
    using TDriverHandle::TDriverHandle;
};

// Matrosov 394.
class TM394 : public TDriverHandle {
private:
    double CP = 0.0;
    double RedAdj = 0.0;
    static double const pos_table[11];

public:
    double GetPF(double i_bcp, double PP, double HP, double dt, double ep) override;
    void Init(double Press) override;
    void SetReductor(double nAdj) override;
    double GetCP() override;
    double GetRP() override;
    double GetPos(int i) override;
    void Exchange(double *values, bool store) override;
    TM394() : TDriverHandle() { i_bcpno = 5; }
};

// H14K1 (BrakeHandle=Knorr).
class TH14K1 : public TDriverHandle {
private:
    static double const BPT_K[4 - -1 + 1][2];
    static double const pos_table[11];

protected:
    double CP = 0.0;
    double RedAdj = 0.0;

public:
    double GetPF(double i_bcp, double PP, double HP, double dt, double ep) override;
    void Init(double Press) override;
    void SetReductor(double nAdj) override;
    double GetCP() override;
    double GetRP() override;
    double GetPos(int i) override;
    void Exchange(double *values, bool store) override;
    TH14K1() : TDriverHandle() { i_bcpno = 4; }
};

// St113: H14K1 with an EP step.
class TSt113 : public TH14K1 {
private:
    double EPS = 0.0;
    static double const BPT_K[4 - -1 + 1][2];
    static double const BEP_K[5 - -1 + 1];
    static double const pos_table[11];
    double CP = 0;

public:
    double GetPF(double i_bcp, double PP, double HP, double dt, double ep) override;
    double GetCP() override;
    double GetRP() override;
    double GetEP() override;
    double GetPos(int i) override;
    void Init(double Press) override;
    void Exchange(double *values, bool store) override;
    using TH14K1::TH14K1;
};

// FD1, a local (independent) brake handle: the cylinder's pressure it
// gives, toward the position's share of MaxBP.
class TFD1 : public TDriverHandle {
private:
    double MaxBP = 0.0;
    double BP = 0.0;

public:
    double Speed = 0.0;
    double GetPF(double i_bcp, double PP, double HP, double dt, double ep) override;
    void Init(double Press) override;
    double GetCP() override;
    void SetSpeed(double nSpeed);
    void Exchange(double *values, bool store) override;
    using TDriverHandle::TDriverHandle;
};

// H1405 (LocBrakeHandle=Knorr).
class TH1405 : public TDriverHandle {
private:
    double MaxBP = 0.0;
    double BP = 0.0;

public:
    double GetPF(double i_bcp, double PP, double HP, double dt, double ep) override;
    void Init(double Press) override;
    double GetCP() override;
    void Exchange(double *values, bool store) override;
    using TDriverHandle::TDriverHandle;
};

// FVel6: EP and pneumatic.
class TFVel6 : public TDriverHandle {
private:
    double EPS = 0.0;
    static double const pos_table[11];
    double CP = 0.0;

public:
    double GetPF(double i_bcp, double PP, double HP, double dt, double ep) override;
    double GetCP() override;
    double GetRP() override;
    double GetEP() override;
    double GetPos(int i) override;
    double GetSound(int i) override;
    void Init(double Press) override;
    void Exchange(double *values, bool store) override;
    using TDriverHandle::TDriverHandle;
};

// FVE408: EP in steps and pneumatic.
class TFVE408 : public TDriverHandle {
private:
    double EPS = 0.0;
    static double const pos_table[11];
    double CP = 0.0;

public:
    double GetPF(double i_bcp, double PP, double HP, double dt, double ep) override;
    double GetCP() override;
    double GetEP() override;
    double GetRP() override;
    double GetPos(int i) override;
    double GetSound(int i) override;
    void Init(double Press) override;
    void Exchange(double *values, bool store) override;
    using TDriverHandle::TDriverHandle;
};

} // namespace MaszynaHamulce

#endif // MASZYNAHAMULCE_H
