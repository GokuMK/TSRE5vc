/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tests/MaszynaFizTestSuite.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/maszyna/MaszynaFiz.h>
#include <tsre/maszyna/MaszynaText.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/sim/LocoScript.h>
#include <tsre/sim/VehicleModel.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrainRoute.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/PathTrack.h>
#include <QScopedValueRollback>
#include <tsre/trains/VehicleDefinition.h>
#include <functional>
#include <memory>
#include <tsre/Game.h>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QMap>
#include <QTemporaryDir>
#include <cmath>

namespace {

void writeFile(const QString &path, const QByteArray &bytes) {
    QFile out(path);
    out.open(QIODevice::WriteOnly);
    out.write(bytes);
}

bool near(double a, double b) {
    return std::abs(a - b) <= 1e-9 * std::max(1.0, std::abs(b));
}

}

int TsreTests::runFizFileSuite(bool verbose) {
    TokenTest::Suite test{"[tests:fiz-file]", verbose};
    QTemporaryDir dir;
    writeFile(dir.path() + "/a.fiz",
              "Author chk: someone\n"
              "# a line with a hash is dropped: Param. M=1\n"
              "Param. Category=train M=120000 Mred=18000 Vmax=125 PWR=3120 Type=181\n"
              "Dimensions: L=19.24 L=20 H=4.055 W=3.005 Cx=0.3 // the drag\n"
              "Wheels: D=1.25 Axle=Co'Co' Tw=1.435\n"
              "Brake: BrakeValve=LSt MaxBP=4.2 HiPP=5 Vv=1 MinCP=7.5 MaxCP=8.5 CompressorSpeed=0.04 BVV=125\n"
              "Cntrl. BrakeSystem=Pneumatic BCPN=6 BrakeHandle=FV4a MCPN=43\n"
              "-1 0.6 -1 12 Pneumatic\n"
              " 0 0.5\t-1\t9  Pneumatic\n"
              "Engine: EngineType=ElectricSeriesMotor Trans=18:79 WindingRes=0.14 nmax=2390\n"
              "include power.inc 3600 end\n"
              "include missing.inc end\n"
              "Circuit: ImaxLo=600 ImaxHi=750\n"
              "RList: Size=3 RVent=Automatic\n"
              "0 0 0 0 0\n"
              "1 25.88 1 6 0\n"
              "Security: AwareSystem=Active,CabSignal AwareDelay=60\n"
              "2 15.05 1 6 0\n"
              "END-RL\n"
              "3 4 4 4 4\n"
              "Param. Vmax=100\n"
              "param. M=1\n"
              "Load: MaxLoad=52 LoadQ=pieces\tHidden=1\n"
              "WWList: Size=2\n"
              "0 0 0 0\n"
              "1 800 400 1200\n"
              "END-WWL\n"
              "DList: Mmax=1000 nMmax=20 nmax=30 NomFuelConsRate=200\n"
              "END-DL\n");
    writeFile(dir.path() + "/power.inc", "Power: EnginePower=CurrentCollector MaxVoltage=(p1) MinPress=3.5\n");

    const MaszynaFiz fiz = MaszynaFiz::read(dir.path(), "a.fiz");
    test.check(!fiz.sections.isEmpty(), "read");
    test.check(near(fiz.number(u"Param", u"M"), 120000.0) && near(fiz.number(u"Param", u"Mred"), 18000.0),
               "kg as written; a line with a hash dropped; a header in another case is no section");
    test.check(near(fiz.number(u"Param", u"Vmax"), 100.0 / 3.6), "km/h to m/s; a repeated section's key overrides");
    test.check(near(fiz.number(u"Param", u"PWR"), 3120000.0), "kW to W");
    const MaszynaFiz::Value *type = fiz.section(u"Param") ? fiz.section(u"Param")->value(u"Type") : nullptr;
    test.check(type != nullptr && !type->isNumber && type->text == "181" && fiz.text(u"Param", u"Category") == "train",
               "text keys stay text, even reading as a number (Type=181)");
    test.check(near(fiz.number(u"Dimensions", u"L"), 19.24) && near(fiz.number(u"Dimensions", u"Cx"), 0.3),
               "a key's first in a line counts; a // comment ends the line");
    test.check(fiz.text(u"Wheels", u"Axle") == "Co'Co'", "a value that is no number is text");
    test.check(near(fiz.number(u"Brake", u"MaxBP"), 420000.0) && near(fiz.number(u"Brake", u"HiPP"), 500000.0)
                       && near(fiz.number(u"Brake", u"MinCP"), 750000.0) && near(fiz.number(u"Brake", u"Vv"), 1.0)
                       && near(fiz.number(u"Brake", u"BVV"), 0.125) && near(fiz.number(u"Brake", u"CompressorSpeed"), 4000.0),
               "bar to Pa, litres to m³, the compressor's bar·m³/s to Pa·m³/s");
    const MaszynaFiz::Section *cntrl = fiz.section(u"Cntrl");
    test.check(cntrl != nullptr && cntrl->rows.size() == 2 && cntrl->rows[0].size() == 5
                       && near(cntrl->rows[0][0].number, -1.0) && near(cntrl->rows[0][1].number, 600000.0)
                       && near(cntrl->rows[0][2].number, -1000000.0) && cntrl->rows[0][4].text == "Pneumatic"
                       && near(cntrl->rows[1][3].number, 9.0),
               "Cntrl.'s rows to the next section, their pressures from MPa (0.6 is 6 bar), tabs between values");
    test.check(fiz.text(u"Engine", u"EngineType") == "ElectricSeriesMotor" && fiz.text(u"Engine", u"Trans") == "18:79"
                       && near(fiz.number(u"Engine", u"nmax"), 2390.0),
               "the engine's kind and gear as text, rpm as written");
    test.check(near(fiz.number(u"Power", u"MaxVoltage"), 3600.0) && near(fiz.number(u"Power", u"MinPress"), 350000.0),
               "an include with its (p1)");
    test.check(fiz.files.size() == 2 && fiz.problems.size() == 1, "the files read, the include not found");
    const MaszynaFiz::Section *rlist = fiz.section(u"RList");
    test.check(rlist != nullptr && rlist->rows.size() == 3 && near(rlist->rows[2][1].number, 15.05)
                       && near(rlist->number(u"Size"), 3.0) && rlist->text(u"RVent") == "Automatic",
               "a table to its END marker, past other headers; nothing after it");
    test.check(fiz.text(u"Security", u"AwareSystem") == "Active,CabSignal", "a value with commas");
    const MaszynaFiz::Section *load = fiz.section(u"Load");
    test.check(load != nullptr && near(load->number(u"MaxLoad"), 52.0) && load->value(u"Hidden") == nullptr,
               "a key after a tab is not one (MaSzyna looks for \" Key=\")");
    const MaszynaFiz::Section *ww = fiz.section(u"WWList");
    test.check(ww != nullptr && ww->rows.size() == 2 && near(ww->rows[1][1].number, 800000.0)
                       && near(ww->rows[1][2].number, 400.0) && near(ww->rows[1][3].number, 1200.0),
               "the generator table's kW to W");
    test.check(near(fiz.number(u"DList", u"nmax"), 1800.0) && near(fiz.number(u"DList", u"nMmax"), 1200.0)
                       && near(fiz.number(u"DList", u"NomFuelConsRate"), 200.0 * 0.001 / 3600000.0),
               "DList's rev/s to rpm, g/kWh to kg/J");

    const MaszynaFiz none = MaszynaFiz::read(dir.path(), "none.fiz");
    test.check(none.sections.isEmpty() && !none.problems.isEmpty(), "a missing file: nothing, a problem");
    return test.finish();
}

int TsreTests::runFizCorpusSuite(bool verbose) {
    TokenTest::Suite test{"[tests:fiz-corpus]", verbose};
    const QString root = QDir::cleanPath(Game::root) + "/dynamic";
    if (!QFileInfo(root).isDir()) {
        qInfo() << "[tests:fiz-corpus] SKIP: no dynamic/ in the game root" << Game::root;
        return 0;
    }
    QElapsedTimer timer;
    timer.start();
    int files = 0, withProblems = 0, powered = 0;
    QMap<QString, int> kinds, poweredKinds;
    QStringList problems;
    QDirIterator it(root, {"*.fiz"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        const MaszynaFiz fiz = MaszynaFiz::read(info.path(), info.fileName());
        ++files;
        if (!fiz.problems.isEmpty()) {
            ++withProblems;
            if (problems.size() < 5)
                problems << info.filePath() + ": " + fiz.problems.join(", ");
        }
        const QString kind = fiz.text(u"Engine", u"EngineType");
        ++kinds[kind.isEmpty() ? QStringLiteral("(none)") : kind];
        if (fiz.text(u"Param", u"Category") == "train" && fiz.number(u"Param", u"PWR") > 2000.0) {
            ++powered;
            ++poweredKinds[kind.isEmpty() ? QStringLiteral("(none)") : kind];
        }
    }
    const qint64 ms = timer.elapsed();
    QStringList summary, poweredSummary;
    for (auto i = kinds.cbegin(); i != kinds.cend(); ++i)
        summary << QString("%1 %2").arg(i.key()).arg(i.value());
    for (auto i = poweredKinds.cbegin(); i != poweredKinds.cend(); ++i)
        poweredSummary << QString("%1 %2").arg(i.key()).arg(i.value());
    qInfo().noquote() << "[tests:fiz-corpus]" << files << "files in" << ms << "ms;" << summary.join(", ");
    qInfo().noquote() << "[tests:fiz-corpus] powered (train, PWR above 2 kW):" << powered << ";"
                      << poweredSummary.join(", ");
    test.check(withProblems == 0, QString("no include missing (%1: %2)").arg(withProblems).arg(problems.join("; ")));
    // The review's counts (reports/fiz-physics-format.md, 2026-11-10).
    test.check(files == 1466, QString("the review's 1466 files (%1)").arg(files));
    test.check(kinds.value("ElectricSeriesMotor") == 285 && kinds.value("ElectricInductionMotor") == 188
                       && kinds.value("DumbDE") == 93 && kinds.value("DieselElectric") == 67
                       && kinds.value("DieselEngine") == 52 && kinds.value("Dumb") == 29
                       && kinds.value("WheelsDriven") == 16 && kinds.value("(none)") == 736,
               "the review's engine kinds: " + summary.join(", "));
    test.check(powered == 621 && poweredKinds.value("ElectricSeriesMotor") == 282
                       && poweredKinds.value("ElectricInductionMotor") == 141
                       && poweredKinds.value("DumbDE") + poweredKinds.value("DieselElectric") == 159
                       && poweredKinds.value("DieselEngine") == 32,
               "the review's powered vehicles: " + poweredSummary.join(", "));
    return test.finish();
}

namespace {

// A MaSzyna loco on its script, alone: hot started, stepped at 0.01 s; the
// sounds it sent by name (send("sound", "ignition")) kept.
struct LocoRun {
    std::unique_ptr<Sim::VehicleModel> model;
    Sim::VehicleInputs in;
    Sim::VehicleOutputs out;
    QString problem;
    QStringList sounds;
    double t = 0.0;

    LocoRun(Sim::LocoScripts &scripts, const std::shared_ptr<const VehicleDefinition> &definition)
        : model(scripts.make(definition)) {
        in.dt = 0.01;
        in.cab = true;
        in.lineVoltage = 3000.0;
        in.brakePipe = 500000.0;
        in.brakePipeVolume = 0.0169;
    }
    void step(int steps = 1) {
        for (int i = 0; i < steps; ++i) {
            t += in.dt;
            if (!model)
                continue;
            out.events.clear();
            model->step(in, out);
            for (const Sim::Message &e : out.events) {
                if (e.name == "scriptError" && problem.isEmpty())
                    problem = e.text();
                if (e.name == "sound")
                    sounds << e.text();
            }
        }
    }
    void command(const QString &name, const Sim::MessageValue &value) {
        if (model)
            model->command({name, value});
    }
    double number(const char *name) const { return Sim::Message{QString(), out.values.value(name, 0.0)}.number(); }
    bool flag(const char *name) const { return out.values.value(name, false) == Sim::MessageValue(true); }
    bool has(const char *name) const { return out.values.contains(name); }
    // Stepped until `done`, at most `limit` s; whether it came.
    bool until(const std::function<bool()> &done, double limit) {
        const double start = t;
        while (t - start < limit) {
            step();
            if (done())
                return true;
        }
        return false;
    }
};

}

int TsreTests::runMaszynaScriptsSuite(bool verbose) {
    TokenTest::Suite test{"[tests:maszyna-scripts]", verbose};
    const QString pack = QDir::cleanPath(Game::root) + "/dynamic/pkp";
    if (!QFileInfo(pack + "/et22_v3").isDir()) {
        qInfo() << "[tests:maszyna-scripts] SKIP: no dynamic/pkp/et22_v3 in the game root" << Game::root;
        return 0;
    }
    const QString locoFolder = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/scripts/loco");
    Sim::LocoScripts scripts(locoFolder);

    // ET22: the resistor script, its contactors closing on the first notch.
    const auto et22 = MaszynaVehicles::definition(pack + "/et22_v3", "201e#201e-037.mmd");
    {
        LocoRun run(scripts, et22);
        test.check(run.model != nullptr, "ET22 gets a script");
        if (!run.model)
            return test.finish();
        run.step();
        test.check(run.has("controllerPosition"), "ET22 runs electric-resistor-mmd.lua (MaSzyna's series motors)");
        const bool powered = run.until([&] { return run.flag("mainPower"); }, 10.0);
        run.command("direction", 1.0);
        run.command("trainBrake", 0.05);
        run.command("throttle", "+");
        const bool closed = run.until([&] { return run.number("controllerPosition") >= 1; }, 2.0);
        run.step(50);
        test.check(powered && closed && run.number("motorCurrent") > 0.0 && run.number("force") > 0.0 && run.problem.isEmpty(),
                   QString("ET22 from standstill: power, the contactors on notch 1, %1 A, %2 kN (%3)")
                           .arg(run.number("motorCurrent"), 0, 'f', 0).arg(run.number("force") / 1000.0, 0, 'f', 1)
                           .arg(run.problem));
    }

    // ET22's controller set straight to 6 from 0 (a key, the panel): the
    // contactors close as if it had passed position 1, the relay steps up
    // (at a standstill past about 9 the motors' current trips the overload
    // relay, as MaSzyna's).
    {
        LocoRun run(scripts, et22);
        run.step();
        run.until([&] { return run.flag("mainPower"); }, 10.0);
        run.command("direction", 1.0);
        run.command("throttle", 6.0 / 48.0);
        const bool stepped = run.until([&] { return run.number("controllerPosition") >= 6; }, 10.0);
        test.check(stepped && run.number("motorCurrent") > 0.0 && !run.flag("overload") && run.problem.isEmpty(),
                   QString("ET22's controller set to 6 at once: the relay at %1, %2 A")
                           .arg(run.number("controllerPosition")).arg(run.number("motorCurrent"), 0, 'f', 0));
    }

    // ET22's force envelope at 3000 V against MaSzyna's steady state (the
    // review: the most force with the motors' current at ImaxLo, 600 A,
    // or under): each notch in turn at a fixed speed, then the field steps.
    {
        const double kmh[] = {0, 40, 60, 80, 100, 120};
        const double expected[] = {352, 383, 268, 200, 119, 79};
        QStringList got;
        bool close = true;
        for (int k = 0; k < 6; ++k) {
            LocoRun run(scripts, et22);
            run.in.speed = kmh[k] / 3.6;
            run.step();
            run.until([&] { return run.flag("mainPower"); }, 10.0);
            run.command("direction", 1.0);
            double best = 0.0;
            const auto settle = [&](const char *value, int target) {
                return run.until([&] { return run.number(value) >= target || run.flag("overload"); }, 5.0);
            };
            for (int p = 1; p <= 48 && !run.flag("overload"); ++p) {
                run.command("throttle", p / 48.0);
                if (!settle("controllerPosition", p))
                    break;
                run.step(30);
                if (!run.flag("overload") && std::abs(run.number("motorCurrent")) <= 600.0)
                    best = std::max(best, run.number("force"));
            }
            for (int f = 1; f <= 6 && !run.flag("overload") && run.number("controllerPosition") >= 48; ++f) {
                run.command("shuntField", f / 6.0);
                if (!settle("shuntFieldPosition", f))
                    break;
                run.step(30);
                if (!run.flag("overload") && std::abs(run.number("motorCurrent")) <= 600.0)
                    best = std::max(best, run.number("force"));
            }
            got << QString("%1 km/h %2 kN").arg(kmh[k]).arg(best / 1000.0, 0, 'f', 0);
            close = close && std::abs(best / 1000.0 - expected[k]) <= 0.05 * expected[k] && run.problem.isEmpty();
        }
        test.check(close, "ET22's force envelope at 3000 V within 5 % of MaSzyna's (352, 383, 268, 200, 119, 79 kN): "
                                  + got.join(", "));
    }

    // EU44: the induction script, F0 at a start, Pmax / v at speed.
    if (QFileInfo(pack + "/eu44_v1").isDir()) {
        const auto eu44 = MaszynaVehicles::definition(pack + "/eu44_v1", "es64u4.mmd");
        const MaszynaFiz &fiz = eu44->maszyna->physics();
        const double f0 = fiz.number(u"Engine", u"F0"), pmax = fiz.number(u"Engine", u"Pmax");
        LocoRun run(scripts, eu44);
        run.step();
        run.until([&] { return run.flag("mainPower"); }, 10.0);
        run.command("direction", 1.0);
        run.command("throttle", 1.0);
        run.step(500);
        const double start = run.number("force");
        run.in.speed = 200.0 / 3.6;
        run.step(500);
        const double fast = run.number("force");
        test.check(run.model && !run.has("controllerPosition") && std::abs(start - f0) < 0.01 * f0
                           && std::abs(fast - pmax / (200.0 / 3.6)) < 0.01 * fast && run.problem.isEmpty(),
                   QString("EU44 on electric-induction-mmd.lua: %1 kN at a start (F0 %2), %3 kN at 200 km/h (Pmax/v %4) %5")
                           .arg(start / 1000.0, 0, 'f', 1).arg(f0 / 1000.0, 0, 'f', 1).arg(fast / 1000.0, 0, 'f', 1)
                           .arg(pmax / (200.0 / 3.6) / 1000.0, 0, 'f', 1).arg(run.problem));
    }

    // An EN57 unit's motor car (6bs): on notch 2 (of 3) its relay steps
    // through the series rows on its own (SCDelay 0.975 s) as it gathers
    // speed (0.5 m/s² here), the overload relay untouched.
    if (QFileInfo(pack + "/en57_v1").isDir()) {
        const auto en57 = MaszynaVehicles::definition(pack + "/en57_v1", "6bs_840.mmd");
        LocoRun run(scripts, en57);
        run.step();
        run.until([&] { return run.flag("mainPower"); }, 10.0);
        run.command("direction", 1.0);
        run.command("throttle", 2.0 / 3.0);
        double most = 0.0, force = 0.0;
        for (int i = 0; i < 1500; ++i) {
            run.in.speed = std::min(run.in.speed + 0.5 * run.in.dt, 20.0);
            run.step();
            most = std::max(most, run.number("controllerPosition"));
            force = std::max(force, run.number("force"));
        }
        test.check(run.model && most >= 9 && !run.flag("overload") && force > 0.0 && run.problem.isEmpty(),
                   QString("EN57 on notch 2: its relay up to row %1 of the series (9), overload %2, at most %3 kN %4")
                           .arg(most).arg(run.flag("overload")).arg(force / 1000.0, 0, 'f', 1).arg(run.problem));
    }
    // SM42 (DumbDE, a diesel-electric): its top notch gives Ftmax at a start,
    // the generator's power over (v + Vadd) at speed (MaSzyna's
    // DieselElectric: 540 kW, Vadd 5.5 km/h).
    if (QFileInfo(pack + "/sm42_v1").isDir()) {
        const auto sm42 = MaszynaVehicles::definition(pack + "/sm42_v1", "6d1.mmd");
        LocoRun run(scripts, sm42);
        run.step();
        run.until([&] { return run.flag("mainPower"); }, 20.0);
        run.command("direction", 1.0);
        run.command("throttle", 1.0);
        run.step(3000);
        const double start = run.number("force");
        run.in.speed = 40.0 / 3.6;
        run.step(1000);
        const double moving = run.number("force");
        const double hyperbola = 540000.0 / (40.0 / 3.6 + 5.5 / 3.6);
        test.check(run.model && std::abs(start - 228000.0) < 0.02 * 228000.0 && std::abs(moving - hyperbola) < 0.03 * hyperbola
                           && run.problem.isEmpty(),
                   QString("SM42 on diesel-mmd.lua: %1 kN at a start (Ftmax 228), %2 kN at 40 km/h (%3) %4")
                           .arg(start / 1000.0, 0, 'f', 1).arg(moving / 1000.0, 0, 'f', 1).arg(hyperbola / 1000.0, 0, 'f', 1)
                           .arg(run.problem));
    }

    // MaSzyna's loco sounds on the diesel and induction scripts (task
    // maszyna 05), as maszyna.sounds holds them (sound.<key> its gain,
    // sound.<key>:pitch its pitch) and sends them.
    // ST44 (DieselElectric): its engine at idle, higher on the top notch, its
    // motors moving; stopped and started again, its oil pump priming while
    // it cranks, then its ignition.
    if (QFileInfo(pack + "/st44_v2").isDir()) {
        const auto st44 = MaszynaVehicles::definition(pack + "/st44_v2", "st44-1030#st44-1030.mmd");
        const MaszynaSound::Sounds &sounds = st44->maszyna->sounds();
        LocoRun run(scripts, st44);
        run.step(100);
        const double idleGain = run.number("sound.engine"), idlePitch = run.number("sound.engine:pitch");
        run.command("direction", 1.0);
        run.command("throttle", 1.0);
        run.step(3000);
        const double fullGain = run.number("sound.engine"), fullPitch = run.number("sound.engine:pitch");
        run.in.speed = 10.0;
        run.step(200);
        const double motors = run.number("sound.tractionmotor");
        run.command("throttle", 0.0);
        run.in.speed = 0.0;
        run.step(100);
        run.command("engine", "off");
        const bool stopped = run.until([&] { return run.number("engineRpm") <= 0.0; }, 120.0);
        run.step(100);
        const bool silent = run.number("sound.engine") <= 0.0;
        run.sounds.clear();
        run.command("engine", "on");
        const bool primed = run.until([&] { return run.number("sound.oilpump") > 0.0; }, 10.0);
        const bool fired = run.until([&] { return run.sounds.contains("ignition"); }, 60.0);
        run.step(10);
        const bool primedOnly = run.number("sound.oilpump") <= 0.0;
        test.check(sounds.find(u"engine") && sounds.find(u"oilpump") && sounds.find(u"ignition")
                           && idleGain > 0.05 && fullGain > idleGain && fullPitch > idlePitch && motors > 0.05 && stopped
                           && silent && primed && fired && primedOnly && run.problem.isEmpty(),
                   QString("ST44's sounds: its engine at idle (gain %1, pitch %2) and on the top notch (%3, %4), its "
                           "motors at 36 km/h (%5); stopped %6 and silent %7, started again: its oil pump %8 before "
                           "its ignition %9 %10")
                           .arg(idleGain, 0, 'f', 2).arg(idlePitch, 0, 'f', 2).arg(fullGain, 0, 'f', 2)
                           .arg(fullPitch, 0, 'f', 2).arg(motors, 0, 'f', 2).arg(stopped).arg(silent)
                           .arg(primed && primedOnly).arg(fired).arg(run.problem));
    }
    // SM42 (DumbDE): its turbo from TurboPos (5) on, slewing in.
    if (QFileInfo(pack + "/sm42_v1").isDir()) {
        const auto sm42 = MaszynaVehicles::definition(pack + "/sm42_v1", "6d1.mmd");
        LocoRun run(scripts, sm42);
        run.step(100);
        const bool quietBelow = run.number("sound.turbo") <= 0.0;
        run.command("direction", 1.0);
        run.command("throttle", 1.0);
        const bool turbo = run.until([&] { return run.number("sound.turbo") > 0.05; }, 30.0);
        test.check(quietBelow && turbo && run.problem.isEmpty(),
                   QString("SM42's turbo: none at idle %1, heard on the top notch %2 (gain %3, pitch %4) %5")
                           .arg(quietBelow).arg(turbo).arg(run.number("sound.turbo"), 0, 'f', 2)
                           .arg(run.number("sound.turbo:pitch"), 0, 'f', 2).arg(run.problem));
    }
    // EU44 (ElectricInductionMotor): its battery on; its blowers off
    // standing (MotorBlowersStartVelocity 1 km/h), on pulling (the
    // converter's supply, MotorBlowersSpeed -10: their own 10 rpm); at 100
    // km/h its AC motors at the inverter's frequency from its ffList (within
    // the table's), louder with more of the force; coasting, none.
    if (QFileInfo(pack + "/eu44_v1").isDir()) {
        const auto eu44 = MaszynaVehicles::definition(pack + "/eu44_v1", "es64u4.mmd");
        const MaszynaFiz &fiz = eu44->maszyna->physics();
        double lowest = 1e9, highest = -1e9;
        for (const auto &section : fiz.sections)
            if (section.name == QLatin1String("ffList"))
                for (const auto &row : section.rows)
                    if (row.size() > 1) {
                        lowest = std::min(lowest, row[1].number);
                        highest = std::max(highest, row[1].number);
                    }
        LocoRun run(scripts, eu44);
        run.step();
        run.until([&] { return run.flag("mainPower"); }, 10.0);
        // Its converter after ConverterStartDelay (20.2 s).
        run.step(2500);
        const bool battery = run.number("sound.battery") > 0.0, standing = run.number("sound.motorblower") <= 0.0;
        run.command("direction", 1.0);
        run.in.speed = 100.0 / 3.6;
        run.command("throttle", 0.3);
        run.step(1000);
        const double partGain = run.number("sound.tractionacmotor");
        const bool blowers = standing && run.number("sound.motorblower") > 0.0;
        run.command("throttle", 1.0);
        run.step(1000);
        const double fullGain = run.number("sound.tractionacmotor");
        const double frequency = run.number("sound.tractionacmotor:pitch");
        run.command("throttle", 0.0);
        run.step(1000);
        const bool coasting = run.number("sound.tractionacmotor") <= 0.0;
        test.check(battery && blowers && partGain > 0.0 && fullGain > partGain && lowest <= highest
                           && frequency >= lowest && frequency <= highest && coasting && run.problem.isEmpty(),
                   QString("EU44's sounds: battery %1, blowers off standing and on pulling %2; its AC motors at 100 km/h pulling 30 % (gain %3) and "
                           "all (%4) at %5 (ffList %6 to %7), none coasting %8 %9")
                           .arg(battery).arg(blowers).arg(partGain, 0, 'f', 2).arg(fullGain, 0, 'f', 2)
                           .arg(frequency, 0, 'f', 2).arg(lowest).arg(highest).arg(coasting).arg(run.problem));
    }

    // The simulation's keys from the .fiz: ET22's brake force (its blocks'
    // force at the cylinders' most, MaSzyna's friction at 50 km/h), its
    // cylinders' most, the train brake's pressures.
    {
        const double brakeForce = et22->number("wagon(maxbrakeforce", 0, 0.0);
        test.check(brakeForce > 80000.0 && brakeForce < 120000.0
                           && std::abs(et22->number("wagon(brakecylinderpressureformaxbrakebrakeforce", 0, 0.0) - 420000.0) < 1.0
                           && std::abs(et22->number("engine(trainbrakescontrollermaxsystempressure", 0, 0.0) - 500000.0) < 1.0
                           && std::abs(et22->number("engine(trainbrakescontrollerfullservicepressuredrop", 0, 0.0) - 150000.0) < 1.0
                           && et22->number("wagon(somethingelse", 0, 7.0) == 7.0,
                   QString("ET22's air brake from its .fiz: %1 kN, 4.2 bar in its cylinders, 5 bar less 1.5 at full service")
                           .arg(brakeForce / 1000.0, 0, 'f', 1));
    }
    // A MaSzyna train in the simulation, on a straight 2 km track: ET22 and
    // two 441W wagons. Driven: the ET22 is the cab, its notches take it
    // away, its air brake stops it. Heard as an AI train: its shadow runs
    // the same script.
    if (QFileInfo(pack + "/441w_v1").isDir()) {
        Sim::Vehicle loco, wagon;
        loco.definition = et22;
        wagon.definition = MaszynaVehicles::definition(pack + "/441w_v1", "441w_m.mmd");
        QScopedValueRollback<TDB *> restoreTrack(Game::trackDB);
        TestTrack::Layout straight;
        straight.end(1, 10);
        straight.track(10, 1, 2, 200);
        straight.end(2, 10);
        TDB *line = &straight.db;
        Game::trackDB = line;
        const PathTrack::Result path = PathTrack::startAt(line, {10, 100.0f});
        const Sim::TrainRoute route = Sim::BuildRoute(line, path.nodes, PathTrack::legs(line, path.nodes), 31.0);
        const auto geometry = std::make_shared<const Sim::TrackGeometry>(line);
        const int t0 = 8 * 3600;
        const auto inputOf = [&](int driven) {
            Sim::TrainPlan p;
            p.vehicles = {loco, wagon, wagon};
            for (const Sim::Vehicle &v : p.vehicles)
                p.length += float(v.length());
            p.maxSpeed = 20.0f;
            Sim::SetRates(p);
            p.performance = Sim::PerformanceOf(p.vehicles);
            p.driving = Sim::DrivingOf(p.vehicles);
            p.startTime = t0;
            p.route = route;
            p.startFront = route.pathStart + p.length + 1.0;
            auto made = std::make_shared<Sim::SimulationInput>();
            made->startTime = t0;
            made->geometry = geometry;
            made->trains.push_back(p);
            made->step = driven >= 0 ? 0.01 : 0.1;
            made->driven = driven;
            made->lineVoltage = 3000.0;
            made->locoScripts = locoFolder;
            return std::shared_ptr<const Sim::SimulationInput>(made);
        };
        const auto value = [](Sim::SimulationCore &core, int vehicle, const char *name) {
            const Sim::VehicleOutputs *out = core.outputsOf(0, vehicle);
            return out ? Sim::Message{QString(), out->values.value(name, 0.0)}.number() : -1.0;
        };
        {
            Sim::SimulationCore core(inputOf(0));
            core.step(0.01);
            core.command(0, -1, {"direction", 1.0});
            core.command(0, -1, {"trainBrake", 0.05});
            core.command(0, -1, {"engineBrake", 0.0});
            core.step(10.0);
            const bool cab = core.outputsOf(0, -1) != nullptr && core.outputsOf(0, -1)->values.contains("controllerPosition");
            for (int notch = 1; notch <= 12; ++notch) {
                core.command(0, -1, {"throttle", notch / 48.0});
                core.step(2.0);
            }
            core.step(30.0);
            const double speed = core.trains()[0].speed;
            const double position = value(core, -1, "controllerPosition");
            core.command(0, -1, {"throttle", 0.0});
            core.command(0, -1, {"trainBrake", 0.9});
            core.step(60.0);
            const double stopped = core.trains()[0].speed;
            const double mainReservoir = value(core, -1, "mainReservoir");
            test.check(cab && position == 12 && speed > 3.0 && stopped < 0.01 && mainReservoir > 700000.0
                               && !value(core, -1, "overload"),
                       QString("driven: the ET22 its cab, on notch %1 at %2 m/s; its train brake's last service "
                               "position stops it (%3 m/s), its main reservoir at %4 bar")
                               .arg(position).arg(speed, 0, 'f', 1).arg(stopped, 0, 'f', 2)
                               .arg(mainReservoir / 100000.0, 0, 'f', 1));
        }
        // An EN57 unit, ra + s + rb: the control car ra its cab, with no force
        // of its own; its notches go to the motor car on the control line.
        if (QFileInfo(pack + "/en57_v1").isDir()) {
            Sim::Vehicle ra, s, rb;
            ra.definition = MaszynaVehicles::definition(pack + "/en57_v1", "6ba_840.mmd");
            s.definition = MaszynaVehicles::definition(pack + "/en57_v1", "6bs_840.mmd");
            rb.definition = MaszynaVehicles::definition(pack + "/en57_v1", "6bb_840.mmd");
            const auto unit = [&]() {
                auto made = std::const_pointer_cast<Sim::SimulationInput>(inputOf(0));
                Sim::TrainPlan &p = made->trains[0];
                p.vehicles = {ra, s, rb};
                p.length = 0.0f;
                for (const Sim::Vehicle &v : p.vehicles)
                    p.length += float(v.length());
                p.performance = Sim::PerformanceOf(p.vehicles);
                p.driving = Sim::DrivingOf(p.vehicles);
                p.startFront = route.pathStart + p.length + 1.0;
                return std::shared_ptr<const Sim::SimulationInput>(made);
            };
            Sim::SimulationCore core(unit());
            core.step(0.01);
            core.command(0, -1, {"direction", 1.0});
            core.command(0, -1, {"trainBrake", 0.05});
            core.step(10.0);
            core.command(0, -1, {"throttle", 2.0 / 3.0});
            core.step(40.0);
            const double speed = core.trains()[0].speed;
            const double cabForce = value(core, 0, "force"), motorForce = value(core, 1, "force");
            core.command(0, -1, {"throttle", 0.0});
            core.command(0, -1, {"trainBrake", 0.9});
            core.step(90.0);
            const double stopped = core.trains()[0].speed;
            test.check(ra.definition->wagonTypeId == 4 && rb.definition->wagonTypeId == 4 && core.cabOf(0) == 0
                               && cabForce == 0.0 && speed > 3.0 && stopped < 0.01,
                       QString("EN57 ra+s+rb: ra a control car and the cab (cab %1), no force of its own (%2 N); notch 2 "
                               "drives the motor car (%3 m/s); its brake stops the unit (%4 m/s)")
                               .arg(core.cabOf(0)).arg(cabForce).arg(speed, 0, 'f', 1).arg(stopped, 0, 'f', 2));
            (void)motorForce;
        }
        {
            Sim::SimulationCore core(inputOf(-1));
            core.hear({0});
            double fastest = 0.0;
            bool shadowed = false, scripted = false;
            for (int i = 0; i < 1200; ++i) {
                core.step(0.1);
                fastest = std::max(fastest, double(core.trains()[0].speed));
                shadowed = shadowed || core.shadowed() == std::vector<int>{0};
                const Sim::VehicleOutputs *out = core.outputsOf(0, 0);
                scripted = scripted || (out != nullptr && out->values.contains("controllerPosition"));
            }
            test.check(shadowed && scripted && fastest > 1.0,
                       QString("heard as an AI train: its shadow runs the resistor script (%1), the AI moves it (%2 m/s)")
                               .arg(scripted).arg(fastest, 0, 'f', 1));
        }
    }
    // Every engine in the pack on its script: started, notch 1, 10 s
    // stepped without a script error (the ones whose kind has no script run
    // the C++ model).
    {
        QMap<QString, int> scripted;
        int engines = 0, standIns = 0, failed = 0;
        QStringList problems;
        QDirIterator it(QDir::cleanPath(Game::root) + "/dynamic", {"*.fiz"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo info(it.next());
            if (!QFileInfo::exists(info.path() + "/" + info.completeBaseName() + ".mmd"))
                continue;
            const auto definition = MaszynaVehicles::definition(info.path(), info.completeBaseName() + ".mmd");
            if (definition == nullptr || definition->wagonTypeId < 4)
                continue;
            ++engines;
            LocoRun run(scripts, definition);
            if (!run.model) {
                ++standIns;
                continue;
            }
            run.step();
            const QString kind = run.has("controllerPosition") ? "resistor"
                    : run.has("engineRpm") ? "diesel" : "induction";
            ++scripted[kind];
            run.until([&] { return run.flag("mainPower"); }, 20.0);
            run.command("direction", 1.0);
            run.command("throttle", "+");
            run.step(1000);
            if (!run.problem.isEmpty()) {
                ++failed;
                if (problems.size() < 4)
                    problems << info.fileName() + ": " + run.problem;
            }
        }
        QStringList counts;
        for (auto c = scripted.cbegin(); c != scripted.cend(); ++c)
            counts << QString("%1 %2").arg(c.key()).arg(c.value());
        test.check(engines > 600 && failed == 0,
                   QString("the pack's %1 engines: %2, %3 on the C++ model, %4 script errors %5")
                           .arg(engines).arg(counts.join(", ")).arg(standIns).arg(failed).arg(problems.join("; ")));
    }
    return test.finish();
}
