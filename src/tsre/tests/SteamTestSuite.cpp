#include <tsre/tests/SteamTestSuite.h>
#include "TokenTestSupport.h"
#include <QFile>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <cmath>
#include <tsre/sim/steam/SteamConfig.h>
#include <tsre/sim/steam/SteamTables.h>
#include <tsre/trains/VehicleDefinition.h>

namespace {

bool near(double a, double b, double relative = 1e-9) {
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
    test.check(near(temperature(0.0), 212.0) && near(temperature(5.3), 227.96) && near(temperature(2.65), half)
                       && near(temperature(-1.0), below) && near(pressure(212.0), 0.0) && pressure.x().size() == 20,
               QString("steam tables: %1 °F at 0 psi (212), %2 half way to 5.3 psi (%3), %4 beyond (%5); "
                       "back to %6 psi")
                       .arg(temperature(0.0)).arg(temperature(2.65)).arg(half).arg(temperature(-1.0)).arg(below)
                       .arg(pressure(212.0)));
    // A 2D table: a row's own value at its x (50 and 65 °F), half way
    // between two rows their mean.
    const Table2D &maxima = table2D("LiveSteamInjectorWaterDeliveryMaximaLbsPerPSIPerF");
    const Table &first = table("LiveSteamInjectorWaterDeliveryMaximumLBperPSIat50F");
    const Table &second = table("LiveSteamInjectorWaterDeliveryMaximumLBperPSIat65F");
    test.check(!maxima.empty() && !first.empty() && near(maxima(50.0, 150.0), first(150.0))
                       && near(maxima(65.0, 150.0), second(150.0)) && near(maxima(57.5, 150.0), (first(150.0) + second(150.0)) / 2.0)
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

// What Open Rails derives at load for MSTS's Royal Scot and 380, against the
// load spec's §5 (its numbers from a model of Open Rails' Initialize, to
// four figures).
void checkConfig(TokenTest::Suite &test) {
    using namespace Sim::Steam;
    QTemporaryDir files;
    writeUtf16(files.path() + "/scot.eng",
               steamFile("Scot", "\tMass ( 86t )\n\tSize ( 2.8m 4m 12.7m )\n\tBrakeSystemType ( \"Vacuum_single_pipe\" )",
                         "\tWheelRadius ( 1.03m )\n\tNumWheels ( 1 )\n\tNumCylinders ( 3 )\n\tCylinderDiameter ( 18in )\n"
                         "\tCylinderStroke ( 26in )\n\tMaxBoilerPressure ( 250 )\n\tBoilerVolume ( \"160*(ft^3)\" )\n"
                         "\tBoilerLength ( 6.2m )\n\tSuperHeater ( 1.2 )\n\tInjectorTypes ( 0 1 )\n"
                         "\tMaxTenderCoalMass ( 11200lb )\n\tMaxTenderWaterMass ( 40000lb )\n\tShovelCoalMass ( 1lb )\n"
                         "\tVacuumBrakesSmallEjectorUsageRate ( 500 )\n\tVacuumBrakesLargeEjectorUsageRate ( 2400 )"));
    writeUtf16(files.path() + "/380.eng",
               steamFile("K380", "\tMass ( 81t )\n\tSize ( 2.8m 4m 11.75m )",
                         "\tWheelRadius ( 0.705m )\n\tNumWheels ( 2 )\n\tNumCylinders ( 4 )\n\tCylinderDiameter ( 19in )\n"
                         "\tCylinderStroke ( 26in )\n\tMaxBoilerPressure ( 228 )\n\tBoilerVolume ( \"170*(ft^3)\" )\n"
                         "\tBoilerLength ( 6.0m )\n\tSuperHeater ( 1.15 )\n\tInjectorTypes ( 0 0 )\n"
                         "\tSteamFiremanMaxPossibleFiringRate ( 7000 )\n\tSteamGaugeGlassHeight ( 11 )\n"
                         "\tMaxTenderCoalMass ( 18000lb )\n\tMaxTenderWaterMass ( 50000lb )\n\tMaxSteamHeatingPressure ( 80 )"));
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
        const auto definition = VehicleDefinition::read(files.path(), file, files.path(), false);
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

}

int TsreTests::runSteamSuite(bool verbose) {
    TokenTest::Suite test{"[tests:steam]", verbose};
    checkTables(test);
    checkConfig(test);
    return test.finish();
}
