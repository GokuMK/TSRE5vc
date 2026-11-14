/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tests/MaszynaBrakesTestSuite.h>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <tsre/Game.h>
#include <tsre/maszyna/MaszynaBrake.h>
#include <tsre/maszyna/MaszynaHamulce.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/sim/AirBrake.h>
#include <tsre/sim/LocoScript.h>
#include <tsre/sim/VehicleModel.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/trains/VehicleDefinition.h>

namespace {

constexpr double Bar = 1e5;
constexpr double Kmh = 1.0 / 3.6;

// A vehicle of MaSzyna's pack: its folder under dynamic/ and its type.
std::shared_ptr<const VehicleDefinition> vehicle(const QString &folder, const QString &type) {
    const QString dir = QDir::cleanPath(Game::root) + "/dynamic/" + folder;
    return MaszynaVehicles::definition(dir, MaszynaVehicles::firstRow(dir, type));
}

// A brake on a test stand: the pipe held where the stand puts it, the main
// reservoir pipe at 8 bar, the vehicle at a speed; `seconds` of 0.01 s
// steps, `each` told the time and the brake after each.
struct Stand {
    Sim::VehicleBrake &brake;
    double t = 0.0;
    double pipe = 5.0 * Bar, main = 8.0 * Bar, speed = 0.0, ep = -1.0, local = 0.0, bailOff = 0.0;
    void run(double seconds, double held, const std::function<void(double)> &each = {}) {
        const int steps = int(std::lround(seconds / 0.01));
        for (int i = 0; i < steps; ++i) {
            pipe = held;
            main = 8.0 * Bar;
            brake.step(0.01, speed, pipe, ep, brake.twinPipe() ? &main : nullptr);
            brake.independent(0.01, local, bailOff);
            t += 0.01;
            if (each)
                each(t);
        }
    }
};

// The time from `from` until the cylinder first reaches `share` of where it
// is after `seconds` (the stand run twice from the same start).
double fillTime(const std::shared_ptr<const VehicleDefinition> &def, double held, double speed, double seconds,
                double share, double *level) {
    std::unique_ptr<Sim::VehicleBrake> a = Sim::MakeVehicleBrake(*def);
    a->charge(5.0 * Bar);
    Stand sa{*a};
    sa.speed = speed;
    sa.run(seconds, held);
    *level = a->cylinder();
    std::unique_ptr<Sim::VehicleBrake> b = Sim::MakeVehicleBrake(*def);
    b->charge(5.0 * Bar);
    Stand sb{*b};
    sb.speed = speed;
    double reached = -1.0;
    sb.run(seconds, held, [&](double t) {
        if (reached < 0.0 && b->cylinder() >= share * *level)
            reached = t;
    });
    return reached;
}

// A loco's script on a test rig playing the core's part (task driving 10
// step 6): its length of the brake pipe and of the main reservoir pipe, its
// own brake (MakeVehicleBrake) with the ED channel (edBrakePressure in,
// edBrake to SetED), the speed set by the test.
struct Rig {
    std::unique_ptr<Sim::VehicleModel> model;
    std::unique_ptr<Sim::VehicleBrake> brake;
    Sim::VehicleInputs in;
    Sim::VehicleOutputs out;
    double main = 0.0;
    bool edChannel = true;
    QString problem;
    Rig(Sim::LocoScripts &scripts, const std::shared_ptr<const VehicleDefinition> &def)
        : model(scripts.make(def)), brake(Sim::MakeVehicleBrake(*def)) {
        in.dt = 0.01;
        in.cab = true;
        in.lineVoltage = 3000.0;
        in.brakePipe = 5.0 * 1e5;
        in.brakePipeVolume = brake->pipeVolume();
        main = 8.0 * 1e5;
        brake->charge(in.brakePipe);
    }
    void step(double seconds) {
        for (int i = 0; i < int(std::lround(seconds / 0.01)); ++i) {
            in.brakeCylinder = brake->cylinder();
            in.mainPipe = brake->twinPipe() ? main : 0.0;
            auto *m = dynamic_cast<MaszynaBrake *>(brake.get());
            in.edBrakePressure = edChannel && m ? m->edPressure() : -1.0;
            out.events.clear();
            out.line.clear();
            out.brakePipeAir = out.mainPipeAir = 0.0;
            model->step(in, out);
            for (const Sim::Message &e : out.events)
                if (e.name == "scriptError" && problem.isEmpty())
                    problem = e.text();
            in.brakePipe = std::max(0.0, in.brakePipe + out.brakePipeAir / in.brakePipeVolume);
            if (brake->twinPipe())
                main = std::max(0.0, main + out.mainPipeAir / in.brakePipeVolume);
            if (m && edChannel)
                m->setED(out.edBrake);
            double *mainPipe = brake->twinPipe() ? &main : nullptr;
            brake->step(0.01, std::fabs(in.speed), in.brakePipe, -1.0, mainPipe);
            brake->independent(0.01, out.independentBrake, out.bailOff);
        }
    }
    double value(const char *name) const { return Sim::Message{QString(), out.values.value(name, 0.0)}.number(); }
    bool flag(const char *name) const { return out.values.value(name, false) == Sim::MessageValue(true); }
    bool until(const std::function<bool()> &done, double limit) {
        for (double t = 0.0; t < limit; t += 0.1) {
            step(0.1);
            if (done())
                return true;
        }
        return false;
    }
};

QString bar(double pa) {
    return QString::number(pa / Bar, 'f', 2) + " bar";
}

}

int TsreTests::runMaszynaBrakesSuite(bool verbose) {
    TokenTest::Suite test{"[tests:maszyna-brakes]", verbose};

    // ---- The pipe's MaSzyna law (no pack needed). ----
    {
        // Six lengths (ET22's 15.6 l and five 111A's 12.7 l), a 1.5 bar
        // drop at the head, MaSzyna's law at every joint.
        Sim::BrakePipe pipe;
        pipe.volume = {0.015634, 0.012675, 0.012675, 0.012675, 0.012675, 0.012675};
        pipe.pressure = {3.5 * Bar, 5.0 * Bar, 5.0 * Bar, 5.0 * Bar, 5.0 * Bar, 5.0 * Bar};
        pipe.laws.assign(5, Sim::VehicleBrake::PipeLaw::Maszyna);
        const auto air = [&pipe]() {
            double sum = 0.0;
            for (size_t i = 0; i < pipe.volume.size(); ++i)
                sum += pipe.volume[i] * pipe.pressure[i];
            return sum;
        };
        const double before = air();
        for (int i = 0; i < 200; ++i)
            pipe.step(0.01);
        const double spread = *std::max_element(pipe.pressure.begin(), pipe.pressure.end())
                - *std::min_element(pipe.pressure.begin(), pipe.pressure.end());
        test.check(std::fabs(air() - before) < 1e-9 * before, "MaSzyna's law conserves the air: "
                   + QString::number(air() - before, 'g', 3) + " Pa·m³ of " + QString::number(before, 'g', 6));
        test.check(spread < 0.001 * Bar, "and evens a six-car pipe out within 2 s: spread " + bar(spread));
        // Mixed joints, and cut ones, conserve too.
        Sim::BrakePipe mixed = pipe;
        mixed.pressure = {0.0, 5.0 * Bar, 2.0 * Bar, 5.0 * Bar, 1.0 * Bar, 4.0 * Bar};
        mixed.laws = {Sim::VehicleBrake::PipeLaw::Maszyna, Sim::VehicleBrake::PipeLaw::OpenRails,
                      Sim::VehicleBrake::PipeLaw::Maszyna, Sim::VehicleBrake::PipeLaw::OpenRails,
                      Sim::VehicleBrake::PipeLaw::Maszyna};
        mixed.joined = {1, 1, 0, 1, 1};
        double mixedBefore = 0.0, mixedAfter = 0.0;
        for (size_t i = 0; i < 6; ++i)
            mixedBefore += mixed.volume[i] * mixed.pressure[i];
        for (int i = 0; i < 1000; ++i)
            mixed.step(0.1);
        for (size_t i = 0; i < 6; ++i)
            mixedAfter += mixed.volume[i] * mixed.pressure[i];
        test.check(std::fabs(mixedAfter - mixedBefore) < 1e-9 * mixedBefore
                           && std::fabs(mixed.pressure[0] - mixed.pressure[2]) < 1.0
                           && std::fabs(mixed.pressure[3] - mixed.pressure[5]) < 1.0
                           && std::fabs(mixed.pressure[2] - mixed.pressure[3]) > 0.1 * Bar,
                   "mixed laws and a cut joint at 0.1 s steps: air conserved, each side even, the cut kept");
        // Its linearisation against MaSzyna's explicit flow (GetDVc: half of
        // PF(p, p', S) from each side) over a short step: the same air.
        for (const double drop : {0.02, 0.3, 1.5, 4.0}) {
            Sim::BrakePipe two;
            two.volume = {0.015, 0.012};
            two.pressure = {(5.0 - drop) * Bar, 5.0 * Bar};
            two.laws = {Sim::VehicleBrake::PipeLaw::Maszyna};
            two.nozzle = {0.58, 0.29};
            const double dt = 1e-5;
            two.step(dt);
            const double moved = (two.pressure[0] - (5.0 - drop) * Bar) * two.volume[0];
            const double maszyna = dt * 0.5 * (MaszynaHamulce::PF(5.0 - drop, 5.0, 0.58)
                                               + MaszynaHamulce::PF(5.0 - drop, 5.0, 0.29)) * 100.0;
            test.check(std::fabs(moved - maszyna) < 0.01 * std::fabs(maszyna),
                       QString("a %1 bar difference: %2 Pa·m³ a step, MaSzyna's explicit %3").arg(drop)
                               .arg(moved, 0, 'g', 5).arg(maszyna, 0, 'g', 5));
        }
        // Open Rails' joints as before: an OR-only pipe is untouched by laws
        // left empty or all OpenRails.
        Sim::BrakePipe a = pipe, b = pipe;
        a.pressure = b.pressure = {3.5 * Bar, 5.0 * Bar, 5.0 * Bar, 5.0 * Bar, 5.0 * Bar, 5.0 * Bar};
        a.laws.clear();
        b.laws.assign(5, Sim::VehicleBrake::PipeLaw::OpenRails);
        for (int i = 0; i < 50; ++i) {
            a.step(0.01);
            b.step(0.01);
        }
        test.check(a.pressure == b.pressure, "Open Rails' joints: laws empty or OpenRails the same pipe");
    }

    // ---- The driver's handles (step 5), MaSzyna's units: on a 15 l pipe
    // fed from 8 bar. ----
    {
        using namespace MaszynaHamulce;
        const auto onPipe = [](TDriverHandle &h, double &pipe, double position, double seconds) {
            for (int i = 0; i < int(std::lround(seconds / 0.01)); ++i)
                pipe = std::max(0.0, pipe - h.GetPF(position, pipe, 8.0, 0.01, pipe) / 15.0);
        };
        TFV4aM fv4a;
        fv4a.Init(5.0);
        double pipe = 5.0;
        onPipe(fv4a, pipe, 0, 20);
        const double running = pipe;
        onPipe(fv4a, pipe, 3, 30);
        const double three = pipe;
        onPipe(fv4a, pipe, 5, 30);
        const double five = pipe;
        test.check(std::fabs(running - 5.0) < 0.01 && std::fabs(three - 3.8) < 0.01 && std::fabs(five - 2.8) < 0.01,
                   QString("FV4a/M: running %1, position 3 %2, 5 %3 bar (BPT 5.0, 3.8, 2.8)").arg(running, 0, 'f', 3)
                           .arg(three, 0, 'f', 3).arg(five, 0, 'f', 3));
        // The filling position, then running: the pipe over 5 bar from the
        // time chamber, sinking back to 5 as it empties.
        onPipe(fv4a, pipe, -1, 4);
        double peak = pipe, back = -1.0;
        for (int i = 0; i < 12000 && back < 0.0; ++i) {
            onPipe(fv4a, pipe, 0, 0.01);
            peak = std::max(peak, pipe);
            if (i > 100 && pipe < 5.02)
                back = i * 0.01;
        }
        test.check(peak > 5.05 && back > 0.0, QString("FV4a/M filling 4 s then running: the pipe up to %1 bar, back "
                                                      "under 5.02 bar %2 s later").arg(peak, 0, 'f', 3).arg(back, 0, 'f', 1));
        onPipe(fv4a, pipe, 6, 10);
        test.check(pipe < 0.1, QString("FV4a/M emergency: %1 bar after 10 s").arg(pipe, 0, 'f', 3));
        TFD1 fd1;
        fd1.Init(3.8);
        double cylinder = 0.0;
        onPipe(fd1, cylinder, 0.5, 15);
        test.check(std::fabs(fd1.GetCP() - 1.9) < 0.02, QString("FD1 at half: %1 bar of 3.8").arg(fd1.GetCP(), 0, 'f', 3));
        // A handle's state kept as numbers (Exchange) steps as one kept.
        const auto roundTrip = [&](auto make, const char *name, std::initializer_list<double> positions) {
            auto kept = make();
            auto rebuilt = make();
            double state[TDriverHandle::StateCount] = {};
            rebuilt->Exchange(state, true);
            double a = 5.0, b = 5.0;
            bool same = true;
            int i = 0;
            for (const double position : positions)
                for (int k = 0; k < 1000; ++k, ++i) {
                    a = std::max(0.0, a - kept->GetPF(position, a, 8.0, 0.01, a) / 15.0);
                    auto fresh = make();
                    fresh->Exchange(state, false);
                    b = std::max(0.0, b - fresh->GetPF(position, b, 8.0, 0.01, b) / 15.0);
                    fresh->Exchange(state, true);
                    same = same && a == b && kept->GetCP() == fresh->GetCP();
                }
            test.check(same, QString("%1's state as numbers between steps: the same pipe (%2 bar)").arg(name).arg(b, 0, 'f', 3));
        };
        roundTrip([] { auto h = std::make_unique<TFV4aM>(); h->Init(5.0); return h; }, "FV4a/M", {0, -1, 0, 3, 6, 0});
        roundTrip([] { auto h = std::make_unique<TMHZ_EN57>(); h->Init(5.0); return h; }, "MHZ_EN57", {0, 2, 9, 10, 0});
        roundTrip([] { auto h = std::make_unique<TFVel6>(); h->Init(5.0); return h; }, "FVel6", {0, 2, 4.5, 5, 0});
        roundTrip([] { auto h = std::make_unique<TMHZ_K5P>(); h->Init(5.0); return h; }, "MHZ_K5P", {0, 2, 1, 3, -1, 0});
        roundTrip([] { auto h = std::make_unique<TFD1>(); h->Init(3.8); return h; }, "FD1", {0.3, 1.0, 0.0});
    }

    // ---- ENG/WAG: Open Rails' after, as before. ----
    {
        std::unique_ptr<Sim::VehicleBrake> eng = Sim::MakeVehicleBrake(*VehicleDefinition::empty());
        test.check(dynamic_cast<Sim::AirBrake *>(eng.get()) != nullptr
                           && eng->pipeLaw() == Sim::VehicleBrake::PipeLaw::OpenRails,
                   "a vehicle without a .fiz: AirBrake, Open Rails' pipe law");
    }

    const QString root = QDir::cleanPath(Game::root) + "/dynamic";
    if (!QFileInfo(root + "/pkp/et22_v3/201e.fiz").isFile()) {
        qInfo() << "[tests:maszyna-brakes] SKIP the pack's vehicles: no MaSzyna pack (dynamic/pkp/et22_v3) in the game root"
                << Game::root;
        return test.finish();
    }

    // ---- The factory. ----
    struct Sample {
        const char *folder, *type;
        int valve; // MaszynaBrake::Valve, -1 AirBrake
        bool twin, ep;
    };
    using V = MaszynaBrake::Valve;
    const Sample samples[] = {
        {"pkp/et22_v3", "201e", int(V::LSt), true, false},
        {"pkp/111arow_v1", "111arow_i", int(V::NESt3), true, false},
        {"pkp/21xm_v1", "214m", int(V::EStED), true, false},
        {"pkp/ed72_v1", "5bs-ra", int(V::EP2), true, true},
        {"pkp/ew58_v1", "ew58-sa", int(V::EP1), true, true},
        {"db/rlmmp_v1", "rlmmp", int(V::KE), false, false},
        {"pkp/ep01_v1", "el101", int(V::West), false, false},
        {"pkp/et21_v2", "3e2", int(V::West), false, false},
        {"pkp/dl2_v2", "dl2", -1, false, false},
    };
    std::shared_ptr<const VehicleDefinition> defs[std::size(samples)];
    for (size_t i = 0; i < std::size(samples); ++i) {
        const Sample &s = samples[i];
        defs[i] = vehicle(s.folder, s.type);
        if (defs[i] == nullptr || defs[i]->maszyna == nullptr) {
            test.check(false, QString("%1/%2 read").arg(s.folder, s.type));
            continue;
        }
        std::unique_ptr<Sim::VehicleBrake> brake = Sim::MakeVehicleBrake(*defs[i]);
        const auto *m = dynamic_cast<const MaszynaBrake *>(brake.get());
        if (s.valve < 0)
            test.check(m == nullptr && dynamic_cast<const Sim::AirBrake *>(brake.get()) != nullptr,
                       QString("%1 (no valve): AirBrake").arg(s.type));
        else
            test.check(m != nullptr && int(m->valve()) == s.valve && m->twinPipe() == s.twin && m->ep() == s.ep
                               && m->pipeLaw() == Sim::VehicleBrake::PipeLaw::Maszyna,
                       QString("%1: MaSzyna's valve %2, main hose %3, EP %4").arg(s.type).arg(s.valve).arg(s.twin)
                               .arg(s.ep));
    }
    const auto &et22 = defs[0], &coach = defs[1], &ed = defs[2], &ep2 = defs[3], &ep1 = defs[4], &ke = defs[5],
               &west = defs[7];
    for (const auto &d : defs)
        if (d == nullptr)
            return test.finish();
    {
        std::unique_ptr<Sim::VehicleBrake> b = Sim::MakeVehicleBrake(*et22);
        const auto *m = static_cast<const MaszynaBrake *>(b.get());
        // ET22: L 19.24 m, BrakeDelays GPR and PWR 3120 kW: Spg 0.792.
        test.check(std::fabs(b->pipeVolume() - (19.24 + 0.5) * 0.792 / 1000.0) < 1e-12
                           && std::fabs(m->pipeNozzle() - 0.792 / (1.0 + 0.015 / 0.792 * 19.24)) < 1e-12,
                   "ET22's pipe: (max(L, 14) + 0.5)·Spg litres, Spg 0.792 (G, powered); its orifice "
                           + QString::number(m->pipeNozzle(), 'f', 4));
        std::unique_ptr<Sim::VehicleBrake> c = Sim::MakeVehicleBrake(*coach);
        test.check(std::fabs(c->pipeVolume() - (24.5 + 0.5) * 0.507 / 1000.0) < 1e-12,
                   "111A's pipe: Spg 0.507 (R only)");
    }

    // ---- Each distributor on a test stand. ----
    struct Bench {
        const char *name;
        std::shared_ptr<const VehicleDefinition> def;
        double maxBP;     // bar
        double speed;     // m/s
        double low, high; // a 1.5 bar reduction's cylinder, shares of MaxBP
    };
    const Bench benches[] = {
        {"LSt (ET22)", et22, 4.2, 100 * Kmh, 0.9, 1.05},
        {"NESt3 ESt4 R (111A)", coach, 4.0, 100 * Kmh, 0.9, 1.05},
        {"EStED R (214M)", ed, 2.9, 100 * Kmh, 0.9, 1.6},
        {"EP2 (5Bs ra)", ep2, 4.0, 0.0, 0.5, 1.05},
        {"EP1 (EW58 sa)", ep1, 3.8, 0.0, 0.5, 1.05},
        {"KE (Rlmmp)", ke, 3.8, 0.0, 0.5, 1.05},
        {"West W (ET21)", west, 4.1, 0.0, 0.5, 1.05},
    };
    for (const Bench &bench : benches) {
        std::unique_ptr<Sim::VehicleBrake> b = Sim::MakeVehicleBrake(*bench.def);
        b->charge(5.0 * Bar);
        Stand s{*b};
        s.speed = bench.speed;
        s.run(10.0, 5.0 * Bar);
        const double released = b->cylinder();
        double level = 0.0;
        const double fill = fillTime(bench.def, 3.5 * Bar, bench.speed, 60.0, 0.95, &level);
        s.run(60.0, 3.5 * Bar);
        const double service = b->cylinder();
        s.run(90.0, 5.0 * Bar);
        const double after = b->cylinder();
        s.run(60.0, 0.0);
        const double emergency = b->cylinder();
        s.run(120.0, 5.0 * Bar);
        const double again = b->cylinder();
        test.check(released < 0.05 * Bar, QString("%1 charged at 5 bar: released (%2)").arg(bench.name, bar(released)));
        test.check(service > bench.low * bench.maxBP * Bar && service < bench.high * bench.maxBP * Bar && fill > 0.0,
                   QString("%1, 1.5 bar reduction: %2 (MaxBP %3 bar), 95 % in %4 s")
                           .arg(bench.name, bar(service)).arg(bench.maxBP).arg(fill, 0, 'f', 2));
        test.check(after < 0.1 * Bar, QString("%1 released again: %2").arg(bench.name, bar(after)));
        test.check(emergency >= 0.95 * service, QString("%1 emergency: %2").arg(bench.name, bar(emergency)));
        test.check(again < 0.1 * Bar, QString("%1 released after the emergency: %2").arg(bench.name, bar(again)));
        QStringList values;
        b->values([&values](const char *name, double v) { values << QString("%1 %2").arg(name, bar(v)); });
        test.check(values.size() >= 2, QString("%1's values: %2").arg(bench.name, values.join(", ")));
    }

    // ---- The R relay by speed (111A's ESt4 R: TRapid, 2:1 below 50 km/h). ----
    {
        double fast = 0.0, slow = 0.0;
        fillTime(coach, 3.5 * Bar, 100 * Kmh, 40.0, 0.95, &fast);
        fillTime(coach, 3.5 * Bar, 30 * Kmh, 40.0, 0.95, &slow);
        test.check(fast > 0.0 && std::fabs(slow / fast - 0.5) < 0.05,
                   "111A's R relay: " + bar(fast) + " at 100 km/h, " + bar(slow) + " at 30 km/h");
        // Slowing through 50 km/h with the brake on: the relay lets half out.
        std::unique_ptr<Sim::VehicleBrake> b = Sim::MakeVehicleBrake(*coach);
        b->charge(5.0 * Bar);
        Stand s{*b};
        s.speed = 80 * Kmh;
        s.run(20.0, 3.5 * Bar);
        const double high = b->cylinder();
        s.speed = 45 * Kmh;
        double changed = -1.0;
        const double from = s.t;
        s.run(20.0, 3.5 * Bar, [&](double t) {
            if (changed < 0.0 && b->cylinder() < 0.55 * high)
                changed = t - from;
        });
        test.check(changed > 0.0 && b->cylinder() < 0.55 * high,
                   "below 50 km/h: " + bar(high) + " to " + bar(b->cylinder()) + " in "
                           + QString::number(changed, 'f', 2) + " s");
    }

    // ---- The local brake (SetLBP) and bail off (the releaser). ----
    for (const auto &[name, def] : {std::pair{"LSt (ET22)", et22}, std::pair{"West W (ET21)", west}}) {
        std::unique_ptr<Sim::VehicleBrake> b = Sim::MakeVehicleBrake(*def);
        b->charge(5.0 * Bar);
        Stand s{*b};
        s.local = 2.0 * Bar;
        s.run(30.0, 5.0 * Bar);
        const double local = b->cylinder();
        s.local = 0.0;
        s.run(30.0, 5.0 * Bar);
        test.check(std::fabs(local - 2.0 * Bar) < 0.15 * Bar && b->cylinder() < 0.1 * Bar,
                   QString("%1's local brake at 2 bar: %2, then %3").arg(name, bar(local), bar(b->cylinder())));
    }
    {
        std::unique_ptr<Sim::VehicleBrake> b = Sim::MakeVehicleBrake(*coach);
        b->charge(5.0 * Bar);
        Stand s{*b};
        s.speed = 100 * Kmh;
        s.run(30.0, 3.5 * Bar);
        const double applied = b->cylinder();
        s.bailOff = 1.0;
        s.run(5.0, 3.5 * Bar);
        s.bailOff = 0.0;
        s.run(60.0, 3.5 * Bar);
        // TNESt3's automatic releaser vents the control reservoir to 0.3 bar
        // above the valve's chamber, then lets go: a little is left.
        test.check(applied > 2.0 * Bar && b->cylinder() < 0.3 * applied,
                   "111A's releaser held (bail off) at 3.5 bar: " + bar(applied) + " to " + bar(b->cylinder()));
    }

    // ---- EP: EP2 by TSRE's wire, EP1's demand. ----
    {
        std::unique_ptr<Sim::VehicleBrake> b = Sim::MakeVehicleBrake(*ep2);
        b->charge(5.0 * Bar);
        Stand s{*b};
        s.ep = 1.0;
        s.run(20.0, 5.0 * Bar);
        const double on = b->cylinder();
        s.ep = 0.0;
        s.run(30.0, 5.0 * Bar);
        test.check(on > 2.0 * Bar && b->cylinder() < 0.1 * Bar,
                   "EP2 on the EP wire alone (the pipe at 5 bar): " + bar(on) + ", released " + bar(b->cylinder()));
        std::unique_ptr<Sim::VehicleBrake> c = Sim::MakeVehicleBrake(*ep1);
        c->charge(5.0 * Bar);
        Stand t{*c};
        t.ep = 0.5;
        t.run(30.0, 5.0 * Bar);
        const double half = c->cylinder();
        t.ep = 0.9;
        t.run(30.0, 5.0 * Bar);
        test.check(half > 0.5 * Bar && c->cylinder() > half * 1.4,
                   "EP1 proportional: " + bar(half) + " at 0.5, " + bar(c->cylinder()) + " at 0.9");
    }

    // ---- The force: MaSzyna's BrakeForce from the .fiz (ET22). ----
    {
        std::unique_ptr<Sim::VehicleBrake> b = Sim::MakeVehicleBrake(*et22);
        b->charge(5.0 * Bar);
        Stand s{*b};
        s.run(60.0, 3.5 * Bar);
        const double p = b->cylinder() / Bar;
        // ET22: BCN 8, BCR 0.1143, BCM 7.75, BCS 1 kN, BSA 0, BRE 0.75,
        // Co'Co' (6 axles), NBpA 4, P10-Bgu.
        const double Ntotal = ((p * 100 * M_PI * 0.1143 * 0.1143 - 1.0) * 7.75 - 0.0) * 0.75;
        const double K = Ntotal * 8.0 / (6 * 4);
        bool ok = true;
        QString shown;
        for (const double kmh : {0.0, 20.0, 50.0, 100.0}) {
            const double u = std::exp(-0.017 * K) * (0.18 - 0.09 * std::exp(-kmh / 25.7)) + 0.381 * std::exp(-kmh / 25.7)
                    - 0.022;
            const double expected = u * K * 1000.0 * 6 * 4;
            const double got = b->force(kmh * Kmh);
            ok = ok && std::fabs(got - expected) < 1e-6 * expected;
            shown += QString(" %1 km/h %2 kN").arg(kmh).arg(got / 1000.0, 0, 'f', 1);
        }
        test.check(ok, "ET22's force at " + bar(b->cylinder()) + ", MaSzyna's BrakeForce:" + shown);
    }

    // ---- The ET22's script on MaSzyna's handles (maszyna.handle): FV4a,
    // FD1. Its pipe a length of 15.6 l the test keeps. ----
    {
        const QString folder = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/scripts/loco");
        Sim::LocoScripts scripts(folder);
        std::unique_ptr<Sim::VehicleModel> model = scripts.make(et22);
        Sim::VehicleInputs in;
        Sim::VehicleOutputs out;
        in.dt = 0.01;
        in.cab = true;
        in.lineVoltage = 3000.0;
        in.brakePipe = 5.0 * Bar;
        in.brakePipeVolume = (19.24 + 0.5) * 0.792 / 1000.0;
        in.mainPipe = 8.5 * Bar;
        QString problem;
        double peak = 0.0;
        const auto run = [&](double seconds) {
            for (int i = 0; i < int(std::lround(seconds / 0.01)); ++i) {
                out.events.clear();
                out.brakePipeAir = 0.0;
                model->step(in, out);
                in.brakePipe = std::max(0.0, in.brakePipe + out.brakePipeAir / in.brakePipeVolume);
                in.brakeCylinder = out.independentBrake;
                peak = std::max(peak, in.brakePipe);
                for (const Sim::Message &e : out.events)
                    if (e.name == "scriptError" && problem.isEmpty())
                        problem = e.text();
            }
        };
        const auto value = [&](const char *name) {
            return Sim::Message{QString(), out.values.value(name, -1.0)}.number();
        };
        test.check(model != nullptr, "the ET22 gets its script");
        if (model) {
            run(1.0);
            model->command({"trainBrake", 0.05});
            run(10.0);
            const double running = in.brakePipe;
            // Position 3: its notch's value, (HiPP - the row's 3.7 bar) over
            // the rows' full service (5 - 2.8) / 0.9.
            model->command({"trainBrake", 1.3 / (2.2 / 0.9) + 1e-6});
            run(30.0);
            const double three = in.brakePipe, control = value("equalisingReservoir");
            test.check(std::fabs(running - 5.0 * Bar) < 0.01 * Bar && std::fabs(three - 3.8 * Bar) < 0.02 * Bar
                               && std::fabs(control - 3.8 * Bar) < 0.02 * Bar && out.values.contains("brakeHandleGauge")
                               && problem.isEmpty(),
                       "ET22's FV4a: running " + bar(running) + ", position 3 " + bar(three) + " (MaSzyna's 3.8, the "
                               ".fiz row 3.7), its control chamber " + bar(control) + " " + problem);
            // Filling, then running: no overcharge left.
            model->command({"trainBrake", 0.0});
            peak = 0.0;
            run(4.0);
            model->command({"trainBrake", 0.05});
            run(80.0);
            test.check(peak > 5.05 * Bar && in.brakePipe < 5.02 * Bar,
                       "ET22's FV4a filling 4 s, then running: up to " + bar(peak) + ", " + bar(in.brakePipe)
                               + " 80 s later");
            model->command({"trainBrake", 1.0});
            run(10.0);
            test.check(in.brakePipe < 0.2 * Bar, "ET22's FV4a emergency: " + bar(in.brakePipe) + " after 10 s");
            model->command({"trainBrake", 0.05});
            model->command({"engineBrake", 0.5});
            run(15.0);
            test.check(std::fabs(out.independentBrake - 2.1 * Bar) < 0.05 * Bar,
                       "ET22's FD1 at half: the engine brake's line " + bar(out.independentBrake) + " (MaxBP 4.2)");
        }
    }

    // ---- Step 6: ED and air, the speed control's brake intervention and
    // Parking, on the locos' scripts with their own brakes. ----
    {
        const QString folder = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/scripts/loco");
        Sim::LocoScripts scripts(folder);
        const auto kn = [](double n) { return QString::number(n / 1000.0, 'f', 1) + " kN"; };
        // EP09: its automatic dynamic brake from the train brake (FV4a's
        // position 3), the air held off while it brakes, back as it fades.
        if (const auto ep09 = vehicle("pkp/ep09_v1", "104e_1"); ep09 && ep09->maszyna) {
            for (const bool channel : {true, false}) {
                Rig r(scripts, ep09);
                r.edChannel = channel;
                const bool powered = r.until([&] { return r.flag("mainPower"); }, 20.0);
                r.model->command({"direction", 1.0});
                r.model->command({"trainBrake", 0.05});
                r.in.speed = 80 / 3.6;
                r.step(5.0);
                r.model->command({"trainBrake", 0.6});
                r.step(15.0);
                const double cylinderFast = r.brake->cylinder(), force = r.value("dynamicBrakeForce"),
                             share = r.value("edShare");
                r.in.speed = 3 / 3.6;
                r.step(20.0);
                const double cylinderSlow = r.brake->cylinder(), forceSlow = r.value("dynamicBrakeForce");
                if (channel)
                    test.check(powered && force > 50e3 && share > 0.5 && cylinderFast < 0.6 * Bar
                                       && cylinderSlow > 1.5 * Bar && forceSlow < force / 4 && r.problem.isEmpty(),
                               "EP09's automatic ED at 80 km/h: " + kn(force) + ", its share " + QString::number(share, 'f', 2)
                                       + ", the cylinders " + bar(cylinderFast) + "; at 3 km/h " + kn(forceSlow)
                                       + ", the air back to " + bar(cylinderSlow) + " " + r.problem);
                else
                    test.check(force == 0.0 && cylinderFast > 1.5 * Bar,
                               "EP09 without the host's GetEDBCP: no ED, the air alone " + bar(cylinderFast));
            }
        }
        // EW58 (EZT, FVE408, TUHEX stages): the handle's EP stages switch its
        // automatic dynamic brake on (TUHEX_StageActual), position 3 the
        // second stage (EpForce 2.64). Its sa car has neither battery nor
        // converter: the EP valve needs 24 V and the ED 110 V, which its
        // unit's d car puts on the train's lines (step 8b), given here.
        if (const auto ew58 = vehicle("pkp/ew58_v1", "ew58-sa"); ew58 && ew58->maszyna) {
            Rig r(scripts, ew58);
            r.in.train24V = 110.0;
            r.in.train110V = 110.0;
            const bool powered = r.until([&] { return r.flag("mainPower"); }, 30.0);
            r.model->command({"direction", 1.0});
            r.model->command({"trainBrake", 0.05});
            r.in.speed = 70 / 3.6;
            r.step(3.0);
            r.model->command({"trainBrake", 0.39});
            r.step(10.0);
            const double force = r.value("dynamicBrakeForce"), current = r.value("motorCurrent");
            r.model->command({"trainBrake", 0.05});
            r.step(10.0);
            const double after = r.value("dynamicBrakeForce");
            test.check(force > 20e3 && after == 0.0 && r.problem.isEmpty(),
                       QString("EW58's ED from FVE408's EP stage 2 at 70 km/h%1: ").arg(powered ? "" : " (no main power)")
                               + kn(force) + ", " + QString::number(current, 'f', 0) + " A; running: " + kn(after) + " "
                               + r.problem);
        }
        // EU47 (induction, abed 1): the train brake's ED, the air let off by
        // its share; fading under Vh1.
        if (const auto eu47 = vehicle("pkp/e186_v2", "eu47"); eu47 && eu47->maszyna) {
            Rig r(scripts, eu47);
            const bool powered = r.until([&] { return r.flag("mainPower"); }, 30.0);
            r.model->command({"direction", 1.0});
            r.model->command({"trainBrake", 0.05});
            r.in.speed = 80 / 3.6;
            r.step(5.0);
            r.model->command({"trainBrake", 0.6});
            r.step(20.0);
            const double cylinderFast = r.brake->cylinder(), force = r.value("dynamicBrakeForce"), share = r.value("edShare");
            r.in.speed = 4 / 3.6;
            r.step(20.0);
            const double cylinderSlow = r.brake->cylinder(), shareSlow = r.value("edShare");
            test.check(powered && force > 30e3 && share > 0.5 && cylinderFast < 0.5 * cylinderSlow && cylinderSlow > 1.5 * Bar
                               && shareSlow < 0.1 && r.problem.isEmpty(),
                       "EU47's train brake ED at 80 km/h: " + kn(force) + ", the air's share off "
                               + QString::number(share, 'f', 2) + ", its cylinders " + bar(cylinderFast)
                               + "; at 4 km/h (under Vh1) share " + QString::number(shareSlow, 'f', 2) + ", the air "
                               + bar(cylinderSlow) + " " + r.problem);
        }
        // SM42 (214M, K5P, BrakeIntervention, SBD 0.47): the speed control
        // braking past its speed under 30 km/h, unbraking below it; Parking
        // at a stand.
        if (const auto sm42 = vehicle("pkp/21xm_v1", "214m"); sm42 && sm42->maszyna) {
            Rig r(scripts, sm42);
            const bool started = r.until([&] { return r.value("engineRpm") > 100; }, 60.0);
            r.model->command({"direction", 1.0});
            r.model->command({"trainBrake", 0.05});
            r.model->command({"throttle", 1.0});
            r.in.speed = 20 / 3.6;
            r.step(3.0);
            r.model->command({"shuntField", QString("+")});
            r.step(3.0);
            const bool active = r.flag("speedControl");
            r.in.speed = 28 / 3.6;
            double lowest = r.in.brakePipe;
            for (int i = 0; i < 300; ++i) {
                r.step(0.1);
                lowest = std::min(lowest, r.in.brakePipe);
            }
            r.in.speed = 12 / 3.6;
            r.step(40.0);
            const double recharged = r.in.brakePipe;
            test.check(started && lowest < 4.2 * Bar && recharged > 4.9 * Bar && r.problem.isEmpty(),
                       "214M's speed control set at 20 km/h (" + QString(active ? "on" : "?") + "), at 28 km/h its brake "
                               "intervention: the pipe down to " + bar(lowest) + "; at 12 km/h back to " + bar(recharged)
                               + " " + r.problem);
            r.model->command({"throttle", 0.0});
            r.in.speed = 0.0;
            r.step(20.0);
            test.check(std::fabs(r.out.independentBrake - 0.47 * 2.9 * Bar) < 0.05 * Bar,
                       "214M at a stand, the speed control on: Parking's local brake " + bar(r.out.independentBrake)
                               + " (SBD 0.47 of 2.9 bar)");
        }
    }

    // ---- Steps longer than MaSzyna's (a heard train's 0.1 s): sub-steps. ----
    {
        std::unique_ptr<Sim::VehicleBrake> a = Sim::MakeVehicleBrake(*coach);
        std::unique_ptr<Sim::VehicleBrake> b = Sim::MakeVehicleBrake(*coach);
        a->charge(5.0 * Bar);
        b->charge(5.0 * Bar);
        double pa = 5.0 * Bar, pb = 5.0 * Bar, ma = 8 * Bar, mb = 8 * Bar;
        for (int i = 0; i < 300; ++i) {
            pa = pb = 3.5 * Bar;
            ma = mb = 8 * Bar;
            a->step(0.1, 30.0, pa, -1.0, &ma);
            for (int k = 0; k < 10; ++k) {
                pb = 3.5 * Bar;
                b->step(0.01, 30.0, pb, -1.0, &mb);
            }
        }
        test.check(std::fabs(a->cylinder() - b->cylinder()) < 0.02 * Bar,
                   "0.1 s steps as ten of 0.01 s: " + bar(a->cylinder()) + " and " + bar(b->cylinder()));
    }
    return test.finish();
}
