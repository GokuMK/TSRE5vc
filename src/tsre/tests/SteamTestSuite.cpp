#include <tsre/tests/SteamTestSuite.h>
#include "TokenTestSupport.h"
#include <QFile>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <cmath>
#include <QDir>
#include <algorithm>
#include <memory>
#include <tsre/Game.h>
#include <tsre/sim/LocoScript.h>
#include <tsre/sim/steam/SteamConfig.h>
#include <tsre/sim/steam/SteamCore.h>
#include <tsre/sim/steam/SteamTables.h>
#include <tsre/trains/VehicleDefinition.h>

namespace {

bool about(double a, double b, double relative = 1e-9) {
    return std::fabs(a - b) <= relative * std::max(1.0, std::fabs(b));
}

// Open Rails' steam tables as SteamTable.cs gives them: its points, linear
// between and beyond them; its 2D tables row by row.
void checkTables(TokenTest::Suite &test) {
    using namespace Sim::Steam;
    const Table &temperature = table("SaturatedSteamHeatPressureToTemperatureInterpolatorPSItoF");
    const Table &pressure = table("TemperatureToPressureInterpolatorFtoPSI");
    // 0 and 5.3 psi gauge: 212 and 227.96 °F; half way linear; beyond 0, on
    // the first segment's line.
    const double half = (212.0 + 227.96) / 2.0;
    const double below = 212.0 - (227.96 - 212.0) / 5.3;
    test.check(about(temperature(0.0), 212.0) && about(temperature(5.3), 227.96) && about(temperature(2.65), half)
                       && about(temperature(-1.0), below) && about(pressure(212.0), 0.0) && pressure.x().size() == 20,
               QString("steam tables: %1 °F at 0 psi (212), %2 half way to 5.3 psi (%3), %4 beyond (%5); "
                       "back to %6 psi")
                       .arg(temperature(0.0)).arg(temperature(2.65)).arg(half).arg(temperature(-1.0)).arg(below)
                       .arg(pressure(212.0)));
    // A 2D table: a row's own value at its x (50 and 65 °F), half way
    // between two rows their mean.
    const Table2D &maxima = table2D("LiveSteamInjectorWaterDeliveryMaximaLbsPerPSIPerF");
    const Table &first = table("LiveSteamInjectorWaterDeliveryMaximumLBperPSIat50F");
    const Table &second = table("LiveSteamInjectorWaterDeliveryMaximumLBperPSIat65F");
    test.check(!maxima.empty() && !first.empty() && about(maxima(50.0, 150.0), first(150.0))
                       && about(maxima(65.0, 150.0), second(150.0)) && about(maxima(57.5, 150.0), (first(150.0) + second(150.0)) / 2.0)
                       && table("NoSuchTable").empty() && table2D("NoSuchTable").empty(),
               QString("steam tables 2D: %1 lb at 50 °F and 150 psi (%2), %3 at 57.5 °F (%4)")
                       .arg(maxima(50.0, 150.0)).arg(first(150.0)).arg(maxima(57.5, 150.0))
                       .arg((first(150.0) + second(150.0)) / 2.0));
}

bool writeUtf16(const QString &path, const QString &text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QStringEncoder encoder(QStringEncoder::Utf16LE);
    return file.write("\xff\xfe" + QByteArray(encoder(text))) > 0;
}

// An MSTS steam ENG with the keys Open Rails reads of MSTS's Royal Scot and
// 380 (the load spec's §5).
QString steamFile(const QString &name, const QString &wagon, const QString &engine) {
    return "SIMISA@@@@@@@@@@JINX0D0t______\n\nWagon ( " + name + "\n\tType ( Engine )\n" + wagon
            + "\n)\nEngine ( " + name + "\n\tType ( Steam )\n" + engine + "\n)\n";
}

// The fixtures: MSTS's Royal Scot and 380 as Open Rails reads them.
void writeFixtures(const QString &folder) {
    writeUtf16(folder + "/scot.eng",
               steamFile("Scot", "\tMass ( 86t )\n\tSize ( 2.8m 4m 12.7m )\n\tBrakeSystemType ( \"Vacuum_single_pipe\" )",
                         "\tWheelRadius ( 1.03m )\n\tNumWheels ( 1 )\n\tNumCylinders ( 3 )\n\tCylinderDiameter ( 18in )\n"
                         "\tCylinderStroke ( 26in )\n\tMaxBoilerPressure ( 250 )\n\tBoilerVolume ( \"160*(ft^3)\" )\n"
                         "\tBoilerLength ( 6.2m )\n\tSuperHeater ( 1.2 )\n\tInjectorTypes ( 0 1 )\n"
                         "\tMaxTenderCoalMass ( 11200lb )\n\tMaxTenderWaterMass ( 40000lb )\n\tShovelCoalMass ( 1lb )\n"
                         "\tVacuumBrakesSmallEjectorUsageRate ( 500 )\n\tVacuumBrakesLargeEjectorUsageRate ( 2400 )"));
    writeUtf16(folder + "/380.eng",
               steamFile("K380", "\tMass ( 81t )\n\tSize ( 2.8m 4m 11.75m )",
                         "\tWheelRadius ( 0.705m )\n\tNumWheels ( 2 )\n\tNumCylinders ( 4 )\n\tCylinderDiameter ( 19in )\n"
                         "\tCylinderStroke ( 26in )\n\tMaxBoilerPressure ( 228 )\n\tBoilerVolume ( \"170*(ft^3)\" )\n"
                         "\tBoilerLength ( 6.0m )\n\tSuperHeater ( 1.15 )\n\tInjectorTypes ( 0 0 )\n"
                         "\tSteamFiremanMaxPossibleFiringRate ( 7000 )\n\tSteamGaugeGlassHeight ( 11 )\n"
                         "\tMaxTenderCoalMass ( 18000lb )\n\tMaxTenderWaterMass ( 50000lb )\n\tMaxSteamHeatingPressure ( 80 )"));
}

std::shared_ptr<const VehicleDefinition> fixture(const QString &folder, const QString &file) {
    return VehicleDefinition::read(folder, file, folder, false);
}

// What Open Rails derives at load for MSTS's Royal Scot and 380, against the
// load spec's §5 (its numbers from a model of Open Rails' Initialize, to
// four figures).
void checkConfig(TokenTest::Suite &test, const QString &folder) {
    using namespace Sim::Steam;
    struct Expected {
        const char *name;
        double scot, k380;
        double (*of)(const Config &);
    };
    const Expected expected[] = {
            {"grate ft²", 54.53, 107.94, [](const Config &c) { return c.grateAreaFt2; }},
            {"ideal fire kg", 778.7, 1541.0, [](const Config &c) { return c.idealFireMassKg; }},
            {"grate burn kg/s", 1.134, 2.244, [](const Config &c) { return c.maxFuelBurnGrateKgps; }},
            {"evaporation ft²", 2726.6, 5397.1, [](const Config &c) { return c.evaporationAreaFt2; }},
            {"Smax lb/h", 40898.0, 80956.0, [](const Config &c) { return c.maxSteamOutputLbps * 3600.0; }},
            {"Tsat K", 480.92, 476.66, [](const Config &c) { return c.saturationTempK; }},
            {"boiler diameter m", 1.592, 1.668, [](const Config &c) { return c.boilerDiameterM; }},
            {"glass m", 0.3048, 0.254, [](const Config &c) { return c.glassLengthM; }},
            {"minimum level", 0.748, 0.746, [](const Config &c) { return c.waterMinLevel; }},
            {"glass top", 0.939, 0.898, [](const Config &c) { return c.waterGlassMaxLevel; }},
            {"start level", 0.844, 0.822, [](const Config &c) { return c.startWaterFraction; }},
            {"grate limit", 138.9, 138.9, [](const Config &c) { return c.grateLimitLbpFt2; }},
            {"firing at Smax lb/h", 8557.0, 16924.0, [](const Config &c) { return c.maxFiringRateLbph; }},
            {"back pressure at Smax psi", 11.30, 22.36,
             [](const Config &c) { return c.backPressure(c.maxSteamOutputLbps * 3600.0); }},
            {"MaxTE lbf", 33108.0, 65536.0, [](const Config &c) { return c.maxTractiveEffortLbf; }},
            {"injector 1 mm", 12.0, 17.0, [](const Config &c) { return c.injectorSizeMm[0]; }},
            {"injector 2 mm", 11.0, 17.0, [](const Config &c) { return c.injectorSizeMm[1]; }},
            {"superheat ratio", 0.6157, 0.4264, [](const Config &c) { return c.superheatRatio; }},
            {"superheat ft²", 681.6, 1349.0, [](const Config &c) { return c.superheatAreaFt2; }},
            {"top speed mph", 55.68, 38.11, [](const Config &c) { return c.maxLocoSpeedMph; }},
            {"steam per IHP-hour lb", 17.8125, 18.325, [](const Config &c) { return c.steamPerIhpHourLb; }},
            {"boiler hp", 2296.0, 4418.0, [](const Config &c) { return c.maxBoilerOutputHp; }},
            {"MaxIHP hp", 3239.6, 4389.0, [](const Config &c) { return c.maxIndicatedHp; }},
            {"start psi", 245.0, 223.0, [](const Config &c) { return c.startPressurePsi; }},
            {"start water K", 479.99, 475.65, [](const Config &c) { return c.startWaterTempK; }},
            {"start flue K", 1041.6, 1035.7, [](const Config &c) { return c.startFlueTempK; }},
            {"boiler lb", 7233.0, 7531.0, [](const Config &c) { return c.startBoilerMassLb; }},
            {"boiler BTU", 2.758e6, 2.809e6, [](const Config &c) { return c.startBoilerHeatBtu; }},
            {"damper factor", 0.4208, 0.8329, [](const Config &c) { return c.damperFactor; }},
            {"blower lb/s/psi", 0.001818, 0.003945, [](const Config &c) { return c.blowerFactorLbpspPsi; }},
            {"fireman kg/s", 1.197, 0.882, [](const Config &c) { return c.firemanMaxFiringRateKgps; }},
            {"safety valves lb/s", 4.62, 9.70, [](const Config &c) { return c.safetyValves.maxDischargeLbps; }},
    };
    for (const char *file : {"scot.eng", "380.eng"}) {
        const auto definition = fixture(folder, file);
        const auto config = Sim::Steam::readConfig(*definition);
        const bool scot = QString(file) == "scot.eng";
        QStringList wrong;
        for (const Expected &e : expected) {
            const double want = scot ? e.scot : e.k380, got = e.of(*config);
            // The spec's numbers to four figures.
            if (std::fabs(got - want) > 0.0015 * std::fabs(want) + 1e-9)
                wrong << QString("%1 %2 (%3)").arg(e.name).arg(got, 0, 'g', 6).arg(want);
        }
        test.check(wrong.isEmpty(), QString("steam config of %1 as Open Rails derives it: %2 values, %3")
                                            .arg(scot ? "the Royal Scot" : "the 380").arg(std::size(expected))
                                            .arg(wrong.isEmpty() ? "all within 0.15 %" : "off: " + wrong.join(", ")));
    }
}


constexpr double Pi = 3.14159265358979323846;
constexpr double LbfN = 1.0 / 0.224808943871;
constexpr double PsiPa = 6894.757293168;

// "name got (wanted)" where `got` is more than `relative` off.
void compare(QStringList &wrong, const char *name, double got, double want, double relative) {
    if (!(std::fabs(got - want) <= relative * std::fabs(want)))
        wrong << QString("%1 %2 (%3)").arg(name).arg(got, 0, 'g', 6).arg(want);
}

// The step spec's worked example (§9.8): the Royal Scot at 60 mph, 245 psi,
// regulator full, cutoff 25 %, steady: its indicator diagram, steam and
// forces as Open Rails gives them.
void checkRoyalScot(TokenTest::Suite &test, const QString &folder) {
    using namespace Sim::Steam;
    const auto config = readConfig(*fixture(folder, "scot.eng"));
    const Config &c = *config;
    const EngineConfig &e = c.engines[0];
    const double revs = 26.8224 / (2.0 * Pi * 1.03);
    const double back = c.backPressure(27400.0);
    const Diagram d = diagram(c, e, 245.0, 1.0, 0.25, revs, back);
    QStringList wrong;
    compare(wrong, "release", d.release, 0.7358, 0.001);
    compare(wrong, "compression", d.compression, 0.2811, 0.001);
    compare(wrong, "admission", d.admission, 0.0046, 0.01);
    compare(wrong, "back pressure psi", back, 5.09, 0.003);
    compare(wrong, "steam chest psi", d.chest, 221.8, 0.001);
    compare(wrong, "a", d.pa, 235.13, 0.001);
    compare(wrong, "b", d.pb, 142.43, 0.001);
    compare(wrong, "c", d.pc, 58.64, 0.001);
    compare(wrong, "d", d.pd, 19.79, 0.001);
    compare(wrong, "f", d.pf, 77.63, 0.001);
    compare(wrong, "MEP psi", d.mep, 75.5, 0.002);
    compare(wrong, "raw steam lb/s", d.steam, 9.05, 0.003);
    double set = 0.0, superheat = 0.0;
    const double factor = superheatFactor(c, 27400.0 / 3600.0, 0.25, revs, set, superheat);
    compare(wrong, "superheat °F", superheat, 203.5, 0.002);
    compare(wrong, "superheat factor", factor, 0.841, 0.002);
    compare(wrong, "cylinder steam lb/h", d.steam * factor * 3600.0, 27400.0, 0.003);
    const double mepForce = e.cylinders / 2.0 * e.diameterIn * e.diameterIn * e.strokeIn / (2.0 * e.wheelRadiusIn) * d.mep;
    compare(wrong, "MEP force lbf", mepForce, 11770.0, 0.002);
    const double degree = Pi / 180.0;
    compare(wrong, "crank 0° lbf", crankForce(c, e, d, 1.0, 0.25, revs, 0.0), 11227.0, 0.002);
    compare(wrong, "crank 30° lbf", crankForce(c, e, d, 1.0, 0.25, revs, 30.0 * degree), 13465.0, 0.002);
    compare(wrong, "crank 60° lbf", crankForce(c, e, d, 1.0, 0.25, revs, 60.0 * degree), 8570.0, 0.002);
    compare(wrong, "crank 90° lbf", crankForce(c, e, d, 1.0, 0.25, revs, 90.0 * degree), 12931.0, 0.002);
    double sum = 0.0, low = 1e9, high = -1e9;
    for (int i = 0; i < 3600; ++i) {
        const double f = crankForce(c, e, d, 1.0, 0.25, revs, i * 0.1 * degree);
        sum += f;
        low = std::min(low, f);
        high = std::max(high, f);
    }
    compare(wrong, "crank mean lbf", sum / 3600.0, 12442.0, 0.002);
    compare(wrong, "crank least lbf", low, 7406.0, 0.003);
    compare(wrong, "crank most lbf", high, 16713.0, 0.003);
    test.check(wrong.isEmpty(), QString("the Royal Scot at 60 mph (step spec §9.8): MEP %1 psi, %2 lb/h of steam, "
                                        "%3 lbf on the MEP, %4 lbf by the crank angles over a turn: %5")
                                        .arg(d.mep, 0, 'f', 1).arg(d.steam * factor * 3600.0, 0, 'f', 0)
                                        .arg(mepForce, 0, 'f', 0).arg(sum / 3600.0, 0, 'f', 0)
                                        .arg(wrong.isEmpty() ? "as the spec's" : "off: " + wrong.join(", ")));
}

// The same point stepped: hot start, held at 60 mph with the regulator full
// at 25 % cutoff, its own boiler and AI fireman; the cylinders' steam and
// the force over a few turns settle at the example's.
void checkRun(TokenTest::Suite &test, const QString &folder) {
    using namespace Sim::Steam;
    const auto config = readConfig(*fixture(folder, "scot.eng"));
    State s;
    Controls k;
    k.regulator = 1.0;
    k.reverser = 0.25;
    Inputs in;
    in.dt = 0.01;
    in.speed = 26.8224;
    Outputs out;
    double force = 0.0;
    int n = 0;
    for (int i = 0; i < 700; ++i) {
        out = Outputs();
        step(*config, s, k, in, out);
        if (i >= 500) {
            force += out.force;
            ++n;
        }
    }
    force /= n;
    QStringList wrong;
    compare(wrong, "boiler psi", out.boilerPressure / PsiPa, 245.0, 0.005);
    compare(wrong, "steam chest psi", out.steamChestPressure / PsiPa, 221.8, 0.005);
    compare(wrong, "back pressure psi", out.backPressure / PsiPa, 5.09, 0.01);
    compare(wrong, "MEP psi", out.meanEffectivePressure / PsiPa, 75.5, 0.01);
    compare(wrong, "cylinder steam lb/h", out.cylinderSteam * 2.20462 * 3600.0, 27400.0, 0.01);
    compare(wrong, "mean force lbf", force / LbfN, 12442.0, 0.015);
    test.check(wrong.isEmpty(), QString("the Royal Scot run 7 s at 60 mph: %1 psi, %2 lb/h of steam, %3 lbf on "
                                        "average over 2 s (%4 kW indicated): %5")
                                        .arg(out.boilerPressure / PsiPa, 0, 'f', 1)
                                        .arg(out.cylinderSteam * 2.20462 * 3600.0, 0, 'f', 0)
                                        .arg(force / LbfN, 0, 'f', 0).arg(out.indicatedPower / 1000.0, 0, 'f', 0)
                                        .arg(wrong.isEmpty() ? "as the example" : "off: " + wrong.join(", ")));
}

// Standing ten minutes, regulator shut: the AI fireman starts both
// injectors at once (the glass at half), keeps the pressure up and the
// fire near its ideal mass.
void checkStanding(TokenTest::Suite &test, const QString &folder) {
    using namespace Sim::Steam;
    const auto config = readConfig(*fixture(folder, "scot.eng"));
    const Config &c = *config;
    State s;
    Controls k;
    Inputs in;
    in.dt = 0.01;
    Outputs out;
    QList<int> firstSounds;
    double lowest = 1e9, highest = 0.0, glassLow = 1e9, glassHigh = -1e9;
    bool finite = true;
    for (int i = 0; i < 60000; ++i) {
        out = Outputs();
        step(c, s, k, in, out);
        if (i == 0)
            firstSounds = QList<int>(out.sounds.begin(), out.sounds.end());
        const double p = out.boilerPressure / PsiPa;
        lowest = std::min(lowest, p);
        highest = std::max(highest, p);
        glassLow = std::min(glassLow, out.waterGlass);
        glassHigh = std::max(glassHigh, out.waterGlass);
        finite = finite && std::isfinite(p) && std::isfinite(s.boilerHeat) && std::isfinite(s.fireMass);
    }
    const double fire = s.fireMass / c.idealFireMassKg;
    test.check(finite && firstSounds.contains(30) && firstSounds.contains(27) && lowest > c.maxBoilerPressurePsi - 10.0
                       && highest <= c.maxBoilerPressurePsi + 1e-9 && fire > 0.9 && fire < 1.1 && glassLow > 0.3
                       && glassHigh < 0.9 && out.force == 0.0,
               QString("the Royal Scot standing 10 min with the AI fireman: injectors started at once (%1), "
                       "%2 to %3 psi (max %4), the glass %5 to %6, the fire at %7 of its ideal mass, %8 kg of coal "
                       "used")
                       .arg([&] {
                           QStringList l;
                           for (int x : firstSounds)
                               l << QString::number(x);
                           return l.join(" ");
                       }())
                       .arg(lowest, 0, 'f', 1).arg(highest, 0, 'f', 1).arg(c.maxBoilerPressurePsi)
                       .arg(glassLow, 0, 'f', 2).arg(glassHigh, 0, 'f', 2).arg(fire, 0, 'f', 2)
                       .arg(c.maxTenderCoalKg - s.tenderCoal, 0, 'f', 1));
}

// The safety valves: with manual firing they lift above the maximum and
// reseat 4 psi below it; the AI fireman caps the pressure instead. The
// boiler's water held at a pressure two steps (a step's valves act on the
// pressure it starts with).
void checkSafetyValves(TokenTest::Suite &test, const QString &folder) {
    using namespace Sim::Steam;
    const auto config = readConfig(*fixture(folder, "scot.eng"));
    const Config &c = *config;
    const Table &saturationF = table("SaturatedSteamHeatPressureToTemperatureInterpolatorPSItoF");
    const auto kelvin = [&](double psi) { return (saturationF(psi) - 32.0) * 100.0 / 180.0 + 273.15; };
    const auto run = [&](bool manual, double psi, State &s) {
        Controls k;
        k.manualFiring = manual;
        Inputs in;
        in.dt = 0.01;
        std::vector<int> sounds;
        double pressure = 0.0;
        for (int i = 0; i < 2; ++i) {
            Outputs out;
            s.waterTemperature = kelvin(psi);
            step(c, s, k, in, out);
            sounds.insert(sounds.end(), out.sounds.begin(), out.sounds.end());
            pressure = out.boilerPressure / PsiPa;
        }
        return std::pair<std::vector<int>, double>(sounds, pressure);
    };
    const auto has = [](const std::vector<int> &sounds, int n) {
        return std::find(sounds.begin(), sounds.end(), n) != sounds.end();
    };
    const double pmax = c.maxBoilerPressurePsi;
    State manual;
    start(c, manual);
    const auto [lift, liftPsi] = run(true, pmax + 3.0, manual);
    const bool open = manual.safety1 != 0.0 && manual.safety2 != 0.0 && manual.safety3 == 0.0;
    const auto [held, heldPsi] = run(true, pmax - 2.0, manual);
    const auto [reseat, reseatPsi] = run(true, pmax - 6.0, manual);
    State ai;
    start(c, ai);
    const auto [capped, cappedPsi] = run(false, pmax + 3.0, ai);
    test.check(has(lift, 42) && open && !has(held, 43) && has(reseat, 43) && manual.safety1 == 0.0
                       && !has(capped, 42) && std::fabs(cappedPsi - pmax) < 1e-9,
               QString("safety valves: manual firing lifts two at %1 psi (sound 42), holds at %2, reseats at %3 "
                       "(43); the AI fireman caps %4 psi at %5")
                       .arg(liftPsi, 0, 'f', 1).arg(heldPsi, 0, 'f', 1).arg(reseatPsi, 0, 'f', 1)
                       .arg(pmax + 3.0, 0, 'f', 1).arg(cappedPsi, 0, 'f', 1));
}

// The loco through its script, steam.lua: the module's values, the
// regulator and reverser, the driver's switches and their sounds.
void checkScript(TokenTest::Suite &test, const QString &folder) {
    const QString locoFolder = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/scripts/loco");
    Sim::LocoScripts scripts(locoFolder);
    const auto definition = fixture(folder, "scot.eng");
    std::unique_ptr<Sim::VehicleModel> model = scripts.make(definition);
    Sim::VehicleInputs in;
    in.dt = 0.01;
    in.cab = true;
    in.brakePipe = 90.0 * PsiPa;
    in.brakePipeVolume = 0.0169;
    Sim::VehicleOutputs out;
    QStringList sounds, events;
    QString problem;
    const auto step = [&](int steps) {
        for (int i = 0; i < steps; ++i) {
            out.events.clear();
            model->step(in, out);
            for (const Sim::Message &e : out.events) {
                if (e.name == "sound")
                    sounds << e.text();
                else if (e.name == "scriptError" && problem.isEmpty())
                    problem = e.text();
                else
                    events << e.name;
            }
        }
    };
    const auto number = [&](const char *name) { return Sim::Message{QString(), out.values.value(name, 0.0)}.number(); };
    if (!model) {
        test.check(false, "steam.lua: no model for a steam engine");
        return;
    }
    step(10);
    const double standing = number("boilerPressure") / PsiPa;
    const bool injectors = sounds.contains("30") && sounds.contains("27");
    model->command({"reverser", 0.25});
    model->command({"regulator", 1.0});
    step(50);
    const double startForce = out.motiveForce;
    const double cutoff = number("cutoff"), regulator = number("regulator");
    sounds.clear();
    model->command({"cylinderCocks", true});
    model->command({"manualFiring", true});
    const double fireBefore = number("fireMass");
    model->command({"shovel", true});
    step(1);
    const double shovelled = number("fireMass") - fireBefore;
    const bool switches = sounds.contains("34") && sounds.contains("137") && sounds.contains("149");
    model->command({"direction", -1.0});
    step(1);
    const double reverse = number("reverser"), direction = number("direction");
    // Saved and restored into another model: the same steps after.
    in.speed = 5.0;
    step(100);
    QByteArray saved;
    std::unique_ptr<Sim::VehicleModel> copy = scripts.make(definition);
    const bool restored = model->save(saved) && copy->restore(saved);
    Sim::VehicleOutputs copyOut;
    copy->step(in, copyOut);
    step(1);
    const bool same = restored && copyOut.motiveForce == out.motiveForce
            && copyOut.values.value("boilerPressure") == out.values.value("boilerPressure")
            && copyOut.values.value("fireMass") == out.values.value("fireMass");
    test.check(problem.isEmpty() && std::fabs(standing - 245.0) < 1.0 && injectors && cutoff == 0.25 && regulator == 1.0
                       && startForce > 50000.0 && switches && shovelled > 0.3 && reverse == -0.25 && direction == -1.0
                       && same,
               QString("steam.lua on the Royal Scot: %1 psi standing, the AI fireman's injectors %2; regulator %3 "
                       "and cutoff %4 start it with %5 kN; cocks and manual firing sound %6; a shovel puts %7 kg "
                       "on the fire; direction -1 sets the reverser to %8 (direction %9); saved (%10 bytes) and "
                       "restored, %11%12")
                       .arg(standing, 0, 'f', 1).arg(injectors ? "started" : "not started").arg(regulator)
                       .arg(cutoff).arg(startForce / 1000.0, 0, 'f', 1).arg(switches ? "as Open Rails" : "wrong")
                       .arg(shovelled, 0, 'f', 2).arg(reverse).arg(direction).arg(saved.size())
                       .arg(same ? "the same step after" : "another step after")
                       .arg(problem.isEmpty() ? QString() : "; error: " + problem));
}
}

int TsreTests::runSteamSuite(bool verbose) {
    TokenTest::Suite test{"[tests:steam]", verbose};
    QTemporaryDir files;
    writeFixtures(files.path());
    checkTables(test);
    checkConfig(test, files.path());
    checkRoyalScot(test, files.path());
    checkRun(test, files.path());
    checkStanding(test, files.path());
    checkSafetyValves(test, files.path());
    checkScript(test, files.path());
    return test.finish();
}
