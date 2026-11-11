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
#include <tsre/maszyna/MaszynaLamps.h>
#include <tsre/maszyna/MaszynaSmoke.h>
#include <tsre/maszyna/E3dFile.h>
#include <tsre/trains/CarLights.h>
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
    QString text(const char *name) const { return Sim::Message{QString(), out.values.value(name, QString())}.text(); }
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

    // ET22's main switch (task maszyna 06): MaSzyna's push buttons, closing
    // only with the shunt lever at 0; without a line it trips (MinV).
    {
        LocoRun run(scripts, et22);
        run.step();
        const bool hot = run.until([&] { return run.text("circuitBreaker") == "closed"; }, 10.0);
        run.command("circuitBreaker", "open");
        run.step(10);
        run.command("circuitBreaker", "released");
        const bool opened = run.text("circuitBreaker") == "open";
        run.command("shuntField", 1.0 / 6.0);
        run.step(10);
        run.command("circuitBreaker", "close");
        run.step(10);
        run.command("circuitBreaker", "released");
        const bool refused = run.text("circuitBreaker") == "open";
        run.command("shuntField", 0.0);
        run.step(10);
        run.command("circuitBreaker", "close");
        run.step(10);
        run.command("circuitBreaker", "released");
        const bool closed = run.text("circuitBreaker") == "closed";
        for (const char *pantograph : {"pantograph1", "pantograph2", "pantograph3", "pantograph4"})
            run.command(pantograph, "down");
        run.step(10);
        const bool tripped = run.text("circuitBreaker") == "open";
        test.check(hot && opened && refused && closed && tripped && run.problem.isEmpty(),
                   QString("ET22's main switch: closed at a hot start %1, opened by its button %2, not closed with "
                           "the shunt lever on %3, closed with it at 0 %4, tripped with its pantographs down %5 %6")
                           .arg(hot).arg(opened).arg(refused).arg(closed).arg(tripped).arg(run.problem));
    }

    // The line voltage (the user, 2026-11-11): the route's where the loco runs
    // on it (half its MaxVoltage to MaxVoltage + 200 V: 1800 to 3800 V for
    // ET22's 3600), MaSzyna's own 0.95 × MaxVoltage (3420 V) without one or
    // outside it (PRUDNIK's 16000).
    {
        const auto voltage = [&](double route) {
            LocoRun run(scripts, et22);
            run.in.lineVoltage = route;
            run.step(200);
            return run.number("lineVoltage");
        };
        const double reduced = voltage(2600.0), none = voltage(0.0), wrong = voltage(16000.0);
        test.check(std::abs(reduced - 2600.0) < 30.0 && std::abs(none - 3420.0) < 30.0 && std::abs(wrong - 3420.0) < 30.0,
                   QString("ET22's line voltage: the route's 2600 V taken (%1), none %2, 16000 V outside its range %3")
                           .arg(reduced, 0, 'f', 0).arg(none, 0, 'f', 0).arg(wrong, 0, 'f', 0));
    }

    // MaSzyna's vigilance (maszyna.security, task maszyna 06): ET22 at 50 km/h
    // alarms after its AwareDelay (60 s), sounds after SoundSignalDelay (5 s)
    // more and brakes after EmergencyBrakeDelay (6 s) more, through the
    // emergency brake; the alerter key acknowledges it and lets the brake go.
    // No cab signal at a hot start.
    {
        LocoRun run(scripts, et22);
        run.in.speed = 50.0 / 3.6;
        run.step();
        const bool noSignal = !run.flag("securityCabSignal");
        run.step(5500);
        const bool quiet = !run.flag("securityAlarm");
        run.step(700);
        const bool alarm = run.flag("securityAware") && !run.flag("securityBuzzer");
        run.command("alerter", "on");
        run.step();
        run.command("alerter", "off");
        run.step();
        const bool reset = !run.flag("securityAlarm");
        run.step(6600);
        const bool buzzer = run.flag("securityBuzzer") && !run.flag("securityBraking");
        run.step(700);
        const bool braking = run.flag("securityBraking") && run.flag("emergency");
        run.command("alerter", "on");
        run.step();
        run.command("alerter", "off");
        run.step(10);
        const bool released = !run.flag("securityBraking") && !run.flag("emergency");
        test.check(noSignal && quiet && alarm && reset && buzzer && braking && released && run.problem.isEmpty(),
                   QString("ET22's vigilance: no cab signal at a hot start %1, quiet at 55 s %2, alarm at 62 s %3, "
                           "acknowledged %4, buzzer 5 s after the next alarm %5, braking 6 s after %6, let go when "
                           "acknowledged %7 %8")
                           .arg(noSignal).arg(quiet).arg(alarm).arg(reset).arg(buzzer).arg(braking).arg(released)
                           .arg(run.problem));
    }

    // MaSzyna's pressure switch (task maszyna 06): ET22's contactors open with
    // 2.5 bar in its cylinders and stay open until the controller goes back
    // to 0; ST44 (diesel-electric) pulls nothing with the pipe at 3.5 bar.
    {
        LocoRun run(scripts, et22);
        run.in.speed = 20.0 / 3.6;
        run.step();
        run.until([&] { return run.flag("mainPower"); }, 10.0);
        run.command("direction", 1.0);
        run.command("throttle", 6.0 / 48.0);
        run.until([&] { return run.number("controllerPosition") >= 6; }, 10.0);
        run.step(50);
        const bool pulling = run.number("force") > 0.0;
        run.in.brakeCylinder = 250000.0;
        run.step(50);
        const bool cut = run.number("force") <= 0.0;
        run.in.brakeCylinder = 0.0;
        run.step(100);
        const bool open = run.number("force") <= 0.0;
        run.command("throttle", 0.0);
        run.step(50);
        run.command("throttle", 1.0 / 48.0);
        const bool again = run.until([&] { return run.number("force") > 0.0; }, 5.0);
        bool dieselCut = true;
        if (QFileInfo(pack + "/st44_v2").isDir()) {
            LocoRun diesel(scripts, MaszynaVehicles::definition(pack + "/st44_v2", "st44-1030#st44-1030.mmd"));
            diesel.in.speed = 20.0 / 3.6;
            diesel.step(100);
            diesel.command("direction", 1.0);
            diesel.command("throttle", 0.5);
            const bool dieselPulls = diesel.until([&] { return diesel.number("force") > 1000.0; }, 30.0);
            diesel.in.brakePipe = 350000.0;
            diesel.step(200);
            dieselCut = dieselPulls && diesel.number("force") <= 0.0 && diesel.problem.isEmpty();
        }
        test.check(pulling && cut && open && again && dieselCut && run.problem.isEmpty(),
                   QString("the pressure switch: ET22 pulling %1, cut at 2.5 bar %2, still open after %3, closing again "
                           "from 0 %4; ST44 cut at 3.5 bar in the pipe %5 %6")
                           .arg(pulling).arg(cut).arg(open).arg(again).arg(dieselCut).arg(run.problem));
    }

    // ET22's force envelope against MaSzyna's steady state at 3420 V, the
    // voltage MaSzyna gives its pantographs without live wires (0.95 ×
    // MaxVoltage 3600; task maszyna 06), here a route giving no voltage (the
    // review: the most force with the motors' current at ImaxLo, 600 A, or
    // under): each notch in turn at a fixed speed, then the field steps.
    {
        const double kmh[] = {0, 40, 60, 80, 100, 120};
        const double expected[] = {375, 364, 303, 234, 163, 107};
        QStringList got;
        bool close = true;
        for (int k = 0; k < 6; ++k) {
            LocoRun run(scripts, et22);
            run.in.lineVoltage = 0.0;
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
        test.check(close, "ET22's force envelope at 3420 V within 5 % of MaSzyna's (375, 364, 303, 234, 163, 107 kN): "
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
        test.check(run.model && run.has("controllerDemand") && std::abs(start - f0) < 0.01 * f0
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
        const auto blowers = [&] {
            return std::max(run.number("sound.motorblower:front"), run.number("sound.motorblower:rear"));
        };
        const bool battery = run.number("sound.battery") > 0.0, standing = blowers() <= 0.0;
        run.command("direction", 1.0);
        run.in.speed = 100.0 / 3.6;
        run.command("throttle", 0.3);
        run.step(1000);
        const double partGain = run.number("sound.tractionacmotor");
        const bool blowing = standing && blowers() > 0.0;
        run.command("throttle", 1.0);
        run.step(1000);
        const double fullGain = run.number("sound.tractionacmotor");
        const double frequency = run.number("sound.tractionacmotor:pitch");
        run.command("throttle", 0.0);
        run.step(1000);
        const bool coasting = run.number("sound.tractionacmotor") <= 0.0;
        test.check(battery && blowing && partGain > 0.0 && fullGain > partGain && lowest <= highest
                           && frequency >= lowest && frequency <= highest && coasting && run.problem.isEmpty(),
                   QString("EU44's sounds: battery %1, blowers off standing and on pulling %2; its AC motors at 100 km/h pulling 30 % (gain %3) and "
                           "all (%4) at %5 (ffList %6 to %7), none coasting %8 %9")
                           .arg(battery).arg(blowing).arg(partGain, 0, 'f', 2).arg(fullGain, 0, 'f', 2)
                           .arg(frequency, 0, 'f', 2).arg(lowest).arg(highest).arg(coasting).arg(run.problem));
    }

    // MaSzyna's induction controllers (maszyna.eimc, task maszyna 06).
    // E186 (type 1, B+ B B- 0 T- T T+ TMax): at 0 at a hot start; T+ adds
    // 0.14 a second, T holds, T- takes it down, B+ brakes.
    if (QFileInfo(pack + "/e186_v2").isDir()) {
        LocoRun run(scripts, MaszynaVehicles::definition(pack + "/e186_v2", "eu47.mmd"));
        run.step();
        // Its main power after MainInitTime (10 s).
        run.until([&] { return run.flag("mainPower"); }, 15.0);
        const bool neutral = run.number("controllerPosition") == 3.0 && std::abs(run.number("controllerDemand")) < 1e-9;
        run.command("direction", 1.0);
        run.in.speed = 20.0 / 3.6;
        run.command("throttle", 6.0 / 7.0);
        run.step(300);
        const double rising = run.number("controllerDemand");
        run.command("throttle", 5.0 / 7.0);
        run.step(300);
        const double held = run.number("controllerDemand");
        const bool pulling = run.number("force") > 0.0;
        run.command("throttle", 4.0 / 7.0);
        run.step(1000);
        const double down = run.number("controllerDemand");
        run.command("throttle", 0.0);
        run.step(300);
        const double braking = run.number("controllerDemand");
        test.check(neutral && std::abs(rising - 0.42) < 0.02 && std::abs(held - rising) < 0.005 && pulling
                           && std::abs(down) < 1e-9 && braking < -0.3 && run.problem.isEmpty(),
                   QString("E186's controller: at 0 at a hot start %1; T+ 3 s %2, T holds %3 (pulling %4), T- to %5, "
                           "B+ 3 s %6 %7")
                           .arg(neutral).arg(rising, 0, 'f', 3).arg(held, 0, 'f', 3).arg(pulling).arg(down, 0, 'f', 3)
                           .arg(braking, 0, 'f', 3).arg(run.problem));
    }
    // Elf (type 2: 0-1 brake, 2 down, 3 holds, 4 up): a hundredth at once on
    // a move, then SCDelay (0.2) a second after IniCDelay (1 s).
    if (QFileInfo(pack + "/elf_v1").isDir()) {
        LocoRun run(scripts, MaszynaVehicles::definition(pack + "/elf_v1", "21we-a.mmd"));
        run.step(100);
        const bool neutral = run.number("controllerPosition") == 3.0 && std::abs(run.number("controllerDemand")) < 1e-9;
        run.command("direction", 1.0);
        run.in.speed = 20.0 / 3.6;
        run.command("throttle", 1.0);
        run.step(2);
        const double moved = run.number("controllerDemand");
        run.step(298);
        const double rising = run.number("controllerDemand");
        run.command("throttle", 3.0 / 4.0);
        run.step(300);
        const double held = run.number("controllerDemand");
        run.command("throttle", 2.0 / 4.0);
        run.step(300);
        const double lower = run.number("controllerDemand");
        run.command("throttle", 0.0);
        run.step(1500);
        const double braking = run.number("controllerDemand");
        test.check(neutral && std::abs(moved - 0.01) < 1e-6 && std::abs(rising - 0.41) < 0.02
                           && std::abs(held - rising) < 0.012 && lower < held && braking < 0.0 && run.problem.isEmpty(),
                   QString("Elf's controller: at 3 at a hot start %1; up: %2 at once, %3 after 3 s; 3 holds %4; 2 lowers "
                           "to %5; 0 brakes %6 %7")
                           .arg(neutral).arg(moved, 0, 'f', 3).arg(rising, 0, 'f', 3).arg(held, 0, 'f', 3)
                           .arg(lower, 0, 'f', 3).arg(braking, 0, 'f', 3).arg(run.problem));
    }

    // MaSzyna's mechanical and hydraulic diesels (maszyna.mechanical on the
    // native maszyna.diesel, task maszyna 06), alone, their speed from
    // their force and mass (no resistance), stepped at 0.01 s.
    const auto driveFor = [](LocoRun &run, double mass, double seconds) {
        double most = 0.0;
        for (int i = 0; i < int(seconds / run.in.dt + 0.5); ++i) {
            run.step();
            run.in.speed = std::max(0.0, run.in.speed + run.number("force") / mass * run.in.dt);
            most = std::max(most, run.number("force"));
        }
        return most;
    };
    // SN61 (a mechanical gearbox, five gears shifting on their own; DList:
    // position 0 no fuel, 1 idle, 3 to 8 the clutch in): idling at a hot
    // start at position 1; full, its first gear's force at most MaSzyna's
    // Mmax × ratio × gear × 2 / D (3630 N·m × 83/39 × 5.056 × 2 / 0.94 =
    // 83 kN), shifting up as it gains speed; back at idle the clutch out; at
    // 0 the engine stops (MaSzyna's), and the cab's start button does not
    // start it there, but at idle.
    if (QFileInfo(pack + "/sn61_v2").isDir()) {
        const auto sn61 = MaszynaVehicles::definition(pack + "/sn61_v2", "sn61.mmd");
        const double mass = sn61->maszyna->physics().number(u"Param", u"M");
        LocoRun run(scripts, sn61);
        run.step(200);
        const double idleRpm = run.number("engineRpm");
        const bool idle = run.number("controllerPosition") == 1.0 && run.text("engineState") == "running";
        run.command("direction", 1.0);
        run.command("throttle", 1.0);
        const double most = driveFor(run, mass, 20.0);
        const double gear20 = run.number("gear"), v20 = run.in.speed * 3.6;
        driveFor(run, mass, 40.0);
        const double gear60 = run.number("gear"), v60 = run.in.speed * 3.6;
        run.command("throttle", 1.0 / 8.0);
        driveFor(run, mass, 5.0);
        const double idleForce = run.number("force"), clutch = run.number("clutch");
        run.in.speed = 0.0;
        run.command("throttle", 0.0);
        const bool stalled = run.until([&] { return run.text("engineState") == "stopped"; }, 30.0);
        run.command("circuitBreaker", "close");
        run.step(1500);
        const bool refused = run.text("engineState") == "stopped";
        run.command("throttle", 1.0 / 8.0);
        run.command("circuitBreaker", "close");
        const bool started = run.until([&] { return run.text("engineState") == "running"; }, 30.0);
        test.check(idle && std::abs(idleRpm - 660.0) < 66.0 && most > 0.5 * 83000.0 && most < 1.02 * 83096.0
                           && gear20 >= 1 && gear60 > gear20 && v60 > v20 && std::abs(idleForce) < 1.0 && clutch < 0.1
                           && stalled && refused && started && run.problem.isEmpty(),
                   QString("SN61: idling at %1 rpm at position 1 %2; full: %3 kN at most, gear %4 at %5 km/h after "
                           "20 s, gear %6 at %7 km/h after 60 s; at idle %8 N, clutch %9; at 0 stopped %10, no start "
                           "there %11, started at idle %12 %13")
                           .arg(idleRpm, 0, 'f', 0).arg(idle).arg(most / 1000.0, 0, 'f', 1).arg(gear20)
                           .arg(v20, 0, 'f', 1).arg(gear60).arg(v60, 0, 'f', 1).arg(idleForce, 0, 'f', 1)
                           .arg(clutch, 0, 'f', 2).arg(stalled).arg(refused).arg(started).arg(run.problem));
    }
    // SA134 (hydraulic: a torque converter locking up from 65 km/h, a
    // retarder; a digital controller of type 3, UCList, its train brake
    // integrated): started at position 2 (no power), its released brake
    // handle takes the controller on to 4, the first released one (as
    // MaSzyna's ChangeCab and CheckEIMIC do); at 5 the demand up at 0.15 a
    // second, the converter filling, pulling away; at 2 the retarder braking
    // at speed; at 0 the train brake handle at UCList's mode 3.
    if (QFileInfo(pack + "/sa134_v1").isDir()) {
        const auto sa134 = MaszynaVehicles::definition(pack + "/sa134_v1", "214m.mmd");
        const double mass = sa134->maszyna->physics().number(u"Param", u"M");
        LocoRun run(scripts, sa134);
        run.step(200);
        const double idleRpm = run.number("engineRpm");
        const bool neutral = run.number("controllerPosition") == 4.0 && std::abs(run.number("controllerDemand")) < 1e-9
                             && run.text("engineState") == "running";
        const double brakeBefore = run.number("trainBrake");
        run.command("direction", 1.0);
        run.command("throttle", 1.0);
        run.step(200);
        const double demand2 = run.number("controllerDemand");
        const double most = driveFor(run, mass, 30.0);
        const double v30 = run.in.speed * 3.6, converter = run.number("converter");
        driveFor(run, mass, 90.0);
        const double v120 = run.in.speed * 3.6, lockup = run.number("lockup");
        run.command("throttle", 2.0 / 5.0);
        driveFor(run, mass, 10.0);
        const double retarder = run.number("retarder"), braking = run.number("force");
        run.command("throttle", 0.0);
        run.step(50);
        const double brakeAfter = run.number("trainBrake");
        // The demand after 2 s: a hundredth at once, 0.15 a second after
        // IniCDelay (1 s): 0.16.
        test.check(neutral && std::abs(demand2 - 0.16) < 0.02 && most > 20000.0 && v30 > 10.0 && converter > 0.9
                           && v120 > v30 && retarder > 0.0 && braking < 0.0 && brakeAfter > brakeBefore
                           && run.problem.isEmpty(),
                   QString("SA134: at 4 after a hot start, idling at %1 rpm %2; at 5 the demand %3 after 2 s, %4 kN at "
                           "most, %5 km/h after 30 s (converter %6), %7 km/h after 2 min (lockup %8); at 2 the "
                           "retarder %9, %10 kN; at 0 the train brake %11 from %12 %13")
                           .arg(idleRpm, 0, 'f', 0).arg(neutral).arg(demand2, 0, 'f', 2).arg(most / 1000.0, 0, 'f', 1)
                           .arg(v30, 0, 'f', 1).arg(converter, 0, 'f', 2).arg(v120, 0, 'f', 1).arg(lockup, 0, 'f', 2)
                           .arg(retarder, 0, 'f', 2).arg(braking / 1000.0, 0, 'f', 1).arg(brakeAfter, 0, 'f', 2)
                           .arg(brakeBefore, 0, 'f', 2).arg(run.problem));
    }

    // The Elf's doors (task maszyna 06): open, its motors pull nothing; the cab
    // car tells the unit (maszynaDoors on the line).
    if (QFileInfo(pack + "/elf_v1").isDir()) {
        LocoRun run(scripts, MaszynaVehicles::definition(pack + "/elf_v1", "21we-a.mmd"));
        run.step();
        run.until([&] { return run.flag("mainPower"); }, 15.0);
        run.command("direction", 1.0);
        run.in.speed = 5.0 / 3.6;
        run.command("throttle", 1.0);
        const bool pulling = run.until([&] { return run.number("force") > 5000.0; }, 20.0);
        run.command("doorsLeft", true);
        run.step(300);
        const bool stopped = run.number("force") <= 0.0;
        run.command("doorsLeft", false);
        const bool again = run.until([&] { return run.number("force") > 5000.0; }, 10.0);
        Sim::LocoScripts unitScripts(locoFolder);
        LocoRun other(unitScripts, MaszynaVehicles::definition(pack + "/elf_v1", "21we-c.mmd"));
        other.in.cab = false;
        other.step();
        other.until([&] { return other.flag("mainPower"); }, 15.0);
        other.command("direction", 1.0);
        other.in.speed = 5.0 / 3.6;
        other.command("throttle", 1.0);
        other.until([&] { return other.number("force") > 5000.0; }, 20.0);
        other.model->fromLine({"maszynaDoors", true});
        other.step(300);
        const bool unit = other.number("force") <= 0.0;
        test.check(pulling && stopped && again && unit && run.problem.isEmpty() && other.problem.isEmpty(),
                   QString("Elf's doors: pulling %1, nothing with a door open %2, pulling again closed %3; another car "
                           "told by the line %4 %5")
                           .arg(pulling).arg(stopped).arg(again).arg(unit).arg(run.problem + other.problem));
    }

    // MaSzyna's speed control (maszyna.speedctrl, task maszyna 06), alone as
    // above. Elf (SCPN 1, type 2, OverrideManual): at 4 to 60 km/h, switched
    // on by tempomat_sw it holds the speed it had less Offset (0.5) for a
    // minute; speedinc_bt sets the next VelStep (65), speedbutton3
    // SpeedButtons' 60, braking electrically; off, it pulls on.
    const auto driveTo = [&](LocoRun &run, double mass, double kmh, double limit) {
        for (double t = 0.0; t < limit && run.in.speed < kmh / 3.6; t += run.in.dt)
            driveFor(run, mass, run.in.dt);
    };
    const auto press = [](LocoRun &run, const QString &button) {
        run.command(button, "press");
        run.step();
        run.command(button, "release");
        run.step();
    };
    if (QFileInfo(pack + "/elf_v1").isDir()) {
        const auto elf = MaszynaVehicles::definition(pack + "/elf_v1", "21we-a.mmd");
        const double mass = elf->maszyna->physics().number(u"Param", u"M");
        LocoRun run(scripts, elf);
        run.step();
        run.until([&] { return run.flag("mainPower"); }, 15.0);
        run.command("direction", 1.0);
        run.command("throttle", 1.0);
        driveTo(run, mass, 60.0, 120.0);
        const double reached = run.in.speed * 3.6;
        press(run, "tempomat_sw");
        const bool on = run.flag("speedControl");
        const double set = run.number("speedControlSpeed") * 3.6;
        driveFor(run, mass, 60.0);
        const double held = run.in.speed * 3.6;
        press(run, "speedinc_bt");
        const double raisedSet = run.number("speedControlSpeed") * 3.6;
        driveFor(run, mass, 60.0);
        const double raised = run.in.speed * 3.6;
        press(run, "speedbutton3");
        driveFor(run, mass, 60.0);
        const double lowered = run.in.speed * 3.6;
        press(run, "tempomat_sw");
        const bool off = !run.flag("speedControl");
        driveFor(run, mass, 20.0);
        const double free = run.in.speed * 3.6;
        test.check(on && std::abs(set - reached) < 0.5 && std::abs(held - (set - 0.5)) < 1.0
                           && std::abs(raisedSet - 65.0) < 1e-9 && std::abs(raised - 64.5) < 1.0
                           && std::abs(lowered - 59.5) < 1.0 && off && free > lowered + 2.0 && run.problem.isEmpty(),
                   QString("Elf's speed control: on at %1 km/h %2, set %3, %4 km/h a minute on; speedinc_bt %5, %6 "
                           "km/h; speedbutton3 %7 km/h; off %8, %9 km/h after 20 s %10")
                           .arg(reached, 0, 'f', 1).arg(on).arg(set, 0, 'f', 1).arg(held, 0, 'f', 1)
                           .arg(raisedSet, 0, 'f', 0).arg(raised, 0, 'f', 1).arg(lowered, 0, 'f', 1).arg(off)
                           .arg(free, 0, 'f', 1).arg(run.problem));
    }
    // EU47 (SCPN 4, SpeedCtrlType Time: 0 off, 1 down, 2 sets, 3 up, 4 Vmax;
    // 10 km/h each SpeedCtrlDelay, 0.5 s, the first at once): 3 for 1.2 s,
    // not on yet; 2 sets 30 km/h; 1 for 0.3 s and 2, 20; 4 Vmax (160); 0
    // off.
    if (QFileInfo(pack + "/e186_v2").isDir()) {
        LocoRun run(scripts, MaszynaVehicles::definition(pack + "/e186_v2", "eu47.mmd"));
        run.step();
        const auto select = [&](int position, int steps) {
            run.command("shuntField", position / 4.0);
            run.step(steps);
            return run.number("speedControlSpeed") * 3.6;
        };
        select(3, 120);
        const bool waiting = !run.flag("speedControl");
        const double up = select(2, 2);
        const bool on = run.flag("speedControl");
        select(1, 30);
        const double down = select(2, 2);
        const double top = select(4, 2);
        select(0, 2);
        const bool off = !run.flag("speedControl");
        test.check(waiting && on && std::abs(up - 30.0) < 1e-9 && std::abs(down - 20.0) < 1e-9
                           && std::abs(top - 160.0) < 1e-6 && off && run.problem.isEmpty(),
                   QString("EU47's speed selector: 3 not on yet %1; 2 %2 km/h, on %3; down %4; 4 %5; 0 off %6 %7")
                           .arg(waiting).arg(up, 0, 'f', 1).arg(on).arg(down, 0, 'f', 1).arg(top, 0, 'f', 1).arg(off)
                           .arg(run.problem));
    }
    // Vectron (SCPN 32, type 0: the position's share of Vmax, 160 km/h, once
    // held SpeedCtrlDelay, 0.5 s): 16 sets 80 km/h after half a second, not
    // before; the controller full, it pulls at 70 km/h and brakes
    // electrically at 85.
    if (QFileInfo(pack + "/vectron_v1").isDir()) {
        LocoRun run(scripts, MaszynaVehicles::definition(pack + "/vectron_v1", "wariant_11n.mmd"));
        run.step();
        run.until([&] { return run.flag("mainPower"); }, 15.0);
        run.command("direction", 1.0);
        run.command("throttle", 1.0);
        run.command("shuntField", 16.0 / 32.0);
        run.step(30);
        const bool waiting = !run.flag("speedControl");
        run.step(30);
        const bool on = run.flag("speedControl");
        const double set = run.number("speedControlSpeed") * 3.6;
        run.in.speed = 70.0 / 3.6;
        run.step(300);
        const double below = run.number("force");
        run.in.speed = 85.0 / 3.6;
        run.step(300);
        const double above = run.number("force");
        test.check(waiting && on && std::abs(set - 80.0) < 1e-9 && below > 0.0 && above < 0.0
                           && run.problem.isEmpty(),
                   QString("Vectron's speed lever: not on before 0.5 s %1, on %2 at %3 km/h; at 70 km/h %4 kN, at 85 "
                           "%5 kN %6")
                           .arg(waiting).arg(on).arg(set, 0, 'f', 1).arg(below / 1000.0, 0, 'f', 1)
                           .arg(above / 1000.0, 0, 'f', 1).arg(run.problem));
    }
    // SA133 (21xm_v1's 214m: hydraulic, type 3, SCPN 2; OverrideManual): at
    // 5 to 50 km/h, tempomat_sw on holds the speed it had, rounded, less
    // Offset for a minute; off again.
    if (QFileInfo(pack + "/21xm_v1").isDir()) {
        const auto sa133 = MaszynaVehicles::definition(pack + "/21xm_v1", "214m.mmd");
        const double mass = sa133->maszyna->physics().number(u"Param", u"M");
        LocoRun run(scripts, sa133);
        run.step(200);
        run.command("direction", 1.0);
        run.command("throttle", 1.0);
        driveTo(run, mass, 50.0, 180.0);
        const double reached = run.in.speed * 3.6;
        press(run, "tempomat_sw");
        const bool on = run.flag("speedControl");
        const double set = run.number("speedControlSpeed") * 3.6;
        double lowest = 1e9, highest = 0.0;
        for (int i = 0; i < 60; ++i) {
            driveFor(run, mass, 1.0);
            if (i >= 30) {
                lowest = std::min(lowest, run.in.speed * 3.6);
                highest = std::max(highest, run.in.speed * 3.6);
            }
        }
        press(run, "tempomat_sw");
        const bool off = !run.flag("speedControl");
        test.check(on && std::abs(set - std::floor(reached + 0.5)) < 1e-9 && lowest > set - 2.5
                           && highest < set + 1.5 && off && run.problem.isEmpty(),
                   QString("SA133's speed control: on at %1 km/h %2, set %3; from 30 s to a minute %4 to %5 km/h; off "
                           "%6 %7")
                           .arg(reached, 0, 'f', 1).arg(on).arg(set, 0, 'f', 0).arg(lowest, 0, 'f', 1)
                           .arg(highest, 0, 'f', 1).arg(off).arg(run.problem));
    }

    // ET42's dynamic brake (maszyna.dcmotors, DynamicBrake Switch): the
    // second controller below 0 with the main one at 0 switches it on (-1);
    // the main controller then brakes on the starting resistors, harder as
    // it goes up, held at 20; over 300 A the overload relay trips (at 60
    // km/h, position 20); the second controller does not leave -1 with the
    // main one up, and switches it off from 0. The other section follows the
    // line.
    if (QFileInfo(pack + "/et42_v2").isDir()) {
        LocoRun run(scripts, MaszynaVehicles::definition(pack + "/et42_v2", "112e-a.mmd"));
        run.step();
        run.until([&] { return run.flag("mainPower"); }, 15.0);
        run.command("direction", 1.0);
        run.in.speed = 60.0 / 3.6;
        run.command("shuntField", "-");
        run.step();
        const bool on = run.flag("dynamicBrakeEngaged") && run.number("shuntFieldPosition") == -1.0;
        const auto at = [&](int position) {
            run.command("throttle", position / 38.0);
            run.step(300);
            return run.number("force");
        };
        const double low = at(3);
        const double lowCurrent = run.number("motorCurrent");
        const double high = at(6);
        const double highCurrent = run.number("motorCurrent");
        at(30);
        const QString notch = run.text("throttleNotch");
        const bool tripped = run.flag("overload") && run.number("force") == 0.0;
        run.command("shuntField", "+");
        run.step();
        const bool stays = run.flag("dynamicBrakeEngaged");
        at(0);
        run.command("overloadReset", true);
        run.command("shuntField", "+");
        run.step();
        const bool off = !run.flag("dynamicBrakeEngaged") && run.number("shuntFieldPosition") == 0.0
                         && !run.flag("overload");
        const double pulling = at(5);
        Sim::LocoScripts unitScripts(locoFolder);
        LocoRun other(unitScripts, MaszynaVehicles::definition(pack + "/et42_v2", "112e-b.mmd"));
        other.in.cab = false;
        other.step();
        other.model->fromLine({"maszynaDynamicBrake", true});
        other.step();
        const bool told = other.flag("dynamicBrakeEngaged");
        test.check(on && low < 0.0 && high < low && lowCurrent < 0.0 && highCurrent > -300.0 && notch == "20 / 38"
                           && tripped && stays && off && pulling > 0.0 && told && run.problem.isEmpty()
                           && other.problem.isEmpty(),
                   QString("ET42's dynamic brake: on at -1 %1; at 3 %2 kN (%3 A), at 6 %4 kN (%5 A); 30 held at %6, "
                           "the overload relay out %7; stays with the controller up %8; off from 0 %9; then pulling "
                           "%10 kN; the other section told %11 %12")
                           .arg(on).arg(low / 1000.0, 0, 'f', 1).arg(lowCurrent, 0, 'f', 0).arg(high / 1000.0, 0, 'f', 1)
                           .arg(highCurrent, 0, 'f', 0).arg(notch).arg(tripped).arg(stays).arg(off)
                           .arg(pulling / 1000.0, 0, 'f', 1).arg(told).arg(run.problem + other.problem));
    }

    // MaSzyna's lamps (task maszyna 07, MaszynaLamps): ET22's model's lamp
    // parts, end 1 its front (+z) and the right lamp on the right (MSTS x);
    // lit by TSRE's train rules (the front's head lamps on the first car
    // with the headlights on, the rear's markers on the last; reversed the
    // other way; none in the middle of a train) or the cab's say; a beam out
    // of each of the three head lamps (CarLights).
    if (QFileInfo(pack + "/et22_v3").isDir()) {
        const auto et22 = MaszynaVehicles::definition(pack + "/et22_v3", "201e2.mmd");
        QString error;
        const std::shared_ptr<const E3dModel> model = E3dFile::read(et22->shapePath, &error);
        const int rgb[3] = {-1, -1, -1};
        const MaszynaLamps::Lamps lamps = model ? MaszynaLamps::find(*model, rgb, 0, 0, 0, 0) : MaszynaLamps::Lamps();
        const auto at = [&](const char *name) -> const MaszynaLamps::Lamp * {
            for (const MaszynaLamps::Lamp &l : lamps.lamps)
                if (l.name == QLatin1String(name))
                    return &l;
            return nullptr;
        };
        const auto *upper = at("headlamp11"), *right = at("headlamp12"), *left = at("headlamp13"), *rear = at("headlamp21");
        QStringList found;
        for (const MaszynaLamps::Lamp &l : lamps.lamps)
            found << QString("%1 (%2 %3 %4)").arg(l.name).arg(l.position[0], 0, 'f', 2).arg(l.position[1], 0, 'f', 2)
                             .arg(l.position[2], 0, 'f', 2);
        const bool placed = upper && right && left && rear && upper->position[2] > 0.0f && rear->position[2] < 0.0f
                            && right->position[0] > left->position[0] && upper->position[1] > right->position[1];
        const auto count = [&](quint64 set, MaszynaLamps::Lamp::Kind kind, int end) {
            int n = 0;
            for (int i = 0; i < lamps.lamps.size(); ++i)
                if (MaszynaLamps::isLit(set, i) && lamps.lamps[i].kind == kind && lamps.lamps[i].end == end)
                    ++n;
            return n;
        };
        EngLights::Train alone;
        const MaszynaLamps::Cab none;
        const quint64 ahead = MaszynaLamps::lit(lamps, alone, none);
        EngLights::Train turned = alone;
        turned.reversed = true;
        const quint64 back = MaszynaLamps::lit(lamps, turned, none);
        EngLights::Train middle = alone;
        middle.first = middle.last = false;
        const quint64 inside = MaszynaLamps::lit(lamps, middle, none);
        EngLights::Train dark = alone;
        dark.headlight = EngLights::Train::HeadlightOff;
        const quint64 off = MaszynaLamps::lit(lamps, dark, none);
        MaszynaLamps::Cab cab;
        cab.controlled = MaszynaLamps::HeadLeft | MaszynaLamps::HeadUpper | MaszynaLamps::HeadRight;
        cab.ends[0] = MaszynaLamps::HeadLeft;
        const quint64 own = MaszynaLamps::lit(lamps, alone, cab);
        const bool rules = count(ahead, MaszynaLamps::Lamp::Head, 1) == 3 && count(ahead, MaszynaLamps::Lamp::Head, 2) == 0
                           && count(ahead, MaszynaLamps::Lamp::Marker, 2) > 0 && count(ahead, MaszynaLamps::Lamp::Marker, 1) == 0
                           && count(back, MaszynaLamps::Lamp::Head, 2) == 3 && count(back, MaszynaLamps::Lamp::Marker, 1) > 0
                           && inside == 0 && count(off, MaszynaLamps::Lamp::Head, 1) == 0
                           && count(own, MaszynaLamps::Lamp::Head, 1) == 1 && count(own, MaszynaLamps::Lamp::Marker, 2) > 0;
        const std::shared_ptr<EngLights> lights = MaszynaLamps::lights(lamps, ahead, false);
        const size_t beams = lights ? CarLights::beams(*lights, alone, et22->size[2] * 0.5f).size() : 0;
        test.check(placed && rules && beams == 3,
                   QString("ET22's lamps: %1; placed %2; the train's rules and the cab's %3; %4 beams")
                           .arg(found.join(", ")).arg(placed).arg(rules).arg(beams));
    }
    // maszyna.lights: the headlight key lights the cab's end's three head
    // lamps (bright), a switch its own lamp, the dimming its switch; only the
    // driven cab's loco says.
    if (QFileInfo(pack + "/et22_v3").isDir()) {
        LocoRun run(scripts, MaszynaVehicles::definition(pack + "/et22_v3", "201e2.mmd"));
        run.step();
        run.command("headlight", 2.0);
        run.step();
        const int key = int(run.number("lamps1"));
        run.command("leftlight_sw", false);
        run.command("dimheadlights_sw", true);
        run.command("leftend_sw", true);
        run.step();
        const int switched = int(run.number("lamps1"));
        const int controlled = int(run.number("lampsControlled"));
        const bool dim = run.flag("lampsDim") && !run.flag("leftlight_sw") && run.flag("upperlight_sw");
        // The key and a switch before one step: the key's set first, then
        // the switch (the capture harness sends both at once).
        LocoRun same(scripts, MaszynaVehicles::definition(pack + "/et22_v3", "201e2.mmd"));
        same.step();
        same.command("headlight", 2.0);
        same.command("leftlight_sw", false);
        same.step();
        const int together = int(same.number("lamps1"));
        run.in.cab = false;
        run.step();
        const bool quiet = run.number("lampsControlled") == 0.0;
        test.check(key == (1 | 4 | 16) && switched == (4 | 16 | 2) && controlled == (1 | 4 | 16 | 2 | 32) && dim && quiet
                           && together == (4 | 16) && run.problem.isEmpty() && same.problem.isEmpty(),
                   QString("maszyna.lights: the key %1, switched %2 (controlled %3), dimmed and shown %4, quiet off "
                           "the cab %5; the key and a switch in one step %6 %7")
                           .arg(key).arg(switched).arg(controlled).arg(dim).arg(quiet).arg(together)
                           .arg(run.problem + same.problem));
    }

    // MaSzyna's smoke (task maszyna 07, MaszynaSmoke): ST44's model's smoke
    // sources up on its roof, their preset st44's (bigger, sootier and darker
    // than the default); an unknown template takes the default; the engine's
    // exhaust scaled by the preset; the script publishes the engine's
    // exhaust (fx.exhaust.*) running, its definition naming the kind.
    if (QFileInfo(pack + "/st44_v2").isDir()) {
        const auto st44 = MaszynaVehicles::definition(pack + "/st44_v2", "st44-1030#st44-1030.mmd");
        QString error;
        const std::shared_ptr<const E3dModel> model = E3dFile::read(st44->shapePath, &error);
        const std::vector<MaszynaSmoke::Source> sources = model ? MaszynaSmoke::find(*model) : std::vector<MaszynaSmoke::Source>();
        QStringList found;
        bool roof = !sources.empty();
        for (const MaszynaSmoke::Source &source : sources) {
            found << QString("%1 (%2 %3 %4)").arg(QLatin1String(source.preset->name)).arg(source.position[0], 0, 'f', 2)
                             .arg(source.position[1], 0, 'f', 2).arg(source.position[2], 0, 'f', 2);
            roof = roof && source.position[1] > 3.5f && QLatin1String(source.preset->name) == QLatin1String("st44");
        }
        const MaszynaSmoke::Preset &heavy = MaszynaSmoke::preset(u"SmokeSource_ST44");
        const bool presets = QLatin1String(heavy.name) == QLatin1String("st44")
                             && QLatin1String(MaszynaSmoke::preset(u"smokesource_nothing").name) == QLatin1String("default")
                             && heavy.output.flow > MaszynaSmoke::defaults().output.flow && heavy.diameter > 0.3f
                             && heavy.output.soot > MaszynaSmoke::defaults().output.soot;
        ExhaustOutput engine{1.0f, 10.0f, 300.0f, 0.1f, 0.4f, 0.0f, 0u};
        const ExhaustOutput adapted = MaszynaSmoke::adapt(engine, heavy);
        const bool scaled = std::abs(adapted.flow - 2.186f) < 0.01f && std::abs(adapted.speed - 9.51f) < 0.01f
                            && adapted.color == heavy.output.color && adapted.temperature == 300.0f;
        LocoRun run(scripts, st44);
        run.step(300);
        const double flow = run.number("fx.exhaust.flow");
        const bool kinds = st44->exhaustKinds == QStringList{QStringLiteral("exhaust")};
        test.check(roof && presets && scaled && kinds && flow > 0.0 && run.problem.isEmpty(),
                   QString("ST44's smoke: %1; on the roof with st44's preset %2; presets %3; adapted %4 m³/s %5 m/s %6; "
                           "the kind named %7, idling %8 m³/s %9")
                           .arg(found.join(", ")).arg(roof).arg(presets).arg(adapted.flow, 0, 'f', 3)
                           .arg(adapted.speed, 0, 'f', 2).arg(scaled).arg(kinds).arg(flow, 0, 'f', 3)
                           .arg(run.problem));
    }

    // MaSzyna's pumps, blowers and diesel start (maszyna.auxiliaries).
    // ST44: its fuel pump switched off stops its engine; the keyboard's
    // engine command switches it on again and starts it, the oil pump
    // priming first.
    if (QFileInfo(pack + "/st44_v2").isDir()) {
        LocoRun run(scripts, MaszynaVehicles::definition(pack + "/st44_v2", "st44-1030#st44-1030.mmd"));
        run.step(100);
        const bool hot = run.flag("fuelpump_sw") && run.text("engineState") == "running";
        run.command("fuelpump_sw", false);
        const bool starved = run.until([&] { return run.text("engineState") == "stopped"; }, 120.0);
        run.command("engine", "on");
        run.step();
        const bool switched = run.flag("fuelpump_sw");
        const bool primed = run.until([&] { return run.number("sound.oilpump") > 0.0; }, 5.0);
        const bool waited = run.text("engineState") == "stopped";
        const bool started = run.until([&] { return run.text("engineState") == "running"; }, 60.0);
        test.check(hot && starved && switched && primed && waited && started && run.problem.isEmpty(),
                   QString("ST44's fuel pump: on at a hot start %1, switched off the engine stops %2; the engine "
                           "command switches it on %3 and primes the oil %4 before cranking %5, then runs %6 %7")
                           .arg(hot).arg(starved).arg(switched).arg(primed).arg(waited).arg(started).arg(run.problem));
    }
    // SU46: its manual pumps and blowers on push buttons (type: return).
    // Hot started, both blowers on; the rear's switch off, the front's
    // alone; all off, none. Its fuel pump released runs until the engine
    // stops; then its start button does nothing; the fuel pump and the oil
    // pump pressed, a start before the oil's pressure is called off (a
    // manual oil pump), one after it starts the engine.
    if (QFileInfo(pack + "/su46_v2").isDir()) {
        LocoRun run(scripts, MaszynaVehicles::definition(pack + "/su46_v2", "303d.mmd"));
        run.step(300);
        const auto blower = [&](const char *place) { return run.number(qPrintable(QString("sound.motorblower:") + place)); };
        const bool both = blower("front") > 0.0 && blower("rear") > 0.0;
        run.command("motorblowersrear_sw", false);
        run.step(1000);
        const bool frontOnly = blower("front") > 0.0 && blower("rear") <= 0.0;
        run.command("motorblowersalloff_sw", QString("press"));
        run.step(1000);
        const bool none = blower("front") <= 0.0 && blower("rear") <= 0.0;
        run.command("motorblowersalloff_sw", QString("release"));
        run.command("fuelpump_sw", QString("press"));
        run.command("fuelpump_sw", QString("release"));
        run.step(10);
        const bool fuelOn = run.number("sound.fuelpump") > 0.0;
        run.command("engine", "off");
        const bool stopped = run.until([&] { return run.text("engineState") == "stopped"; }, 120.0);
        run.step(10);
        const bool fuelOff = run.number("sound.fuelpump") <= 0.0;
        run.command("circuitBreaker", "close");
        run.step(3000);
        const bool noFuel = run.text("engineState") == "stopped";
        run.command("fuelpump_sw", QString("press"));
        run.command("fuelpump_sw", QString("release"));
        run.command("oilpump_sw", QString("press"));
        run.command("oilpump_sw", QString("release"));
        run.command("circuitBreaker", "close");
        run.step(300);
        const bool early = run.text("engineState") == "stopped" && run.number("sound.oilpump") > 0.0;
        run.command("circuitBreaker", "close");
        const bool started = run.until([&] { return run.text("engineState") == "running"; }, 60.0);
        test.check(both && frontOnly && none && fuelOn && stopped && fuelOff && noFuel && early && started
                           && run.problem.isEmpty(),
                   QString("SU46's blowers: both on at a hot start %1, the rear switched off %2, all off %3; its fuel "
                           "pump released runs %4 until the engine stops %5 %6; no start without it %7, none before "
                           "the oil's pressure %8, one after %9 %10")
                           .arg(both).arg(frontOnly).arg(none).arg(fuelOn).arg(stopped).arg(fuelOff).arg(noFuel)
                           .arg(early).arg(started).arg(run.problem));
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

    // Each kind's script's cost a step (task maszyna 06: what goes native),
    // pulling at half throttle at 36 km/h, in the cab (its sounds too).
    // Quote a Release build's.
    const std::pair<const char *, const char *> timed[] = {{"et22_v3", "201e#201e-037.mmd"}, {"eu44_v1", "es64u4.mmd"},
                                                           {"st44_v2", "st44-1030#st44-1030.mmd"}, {"sm42_v1", "6d1.mmd"},
                                                           {"sn61_v2", "sn61.mmd"}, {"sa134_v1", "214m.mmd"}};
    for (const auto &[folder, name] : timed) {
        if (!QFileInfo(pack + "/" + folder).isDir())
            continue;
        LocoRun run(scripts, MaszynaVehicles::definition(pack + "/" + folder, name));
        run.step(300);
        run.command("direction", 1.0);
        run.command("throttle", 0.5);
        run.in.speed = 10.0;
        run.step(300);
        QElapsedTimer timer;
        timer.start();
        run.step(20000);
        const double us = timer.nsecsElapsed() / 1000.0 / 20000.0;
        test.check(us < 1000.0 && run.problem.isEmpty(),
                   QString("%1 (%2): %3 µs a step %4").arg(folder, name).arg(us, 0, 'f', 1).arg(run.problem));
    }
    return test.finish();
}
