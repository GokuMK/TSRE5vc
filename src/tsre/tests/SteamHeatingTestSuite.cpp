#include <tsre/tests/SteamHeatingTestSuite.h>

#include <QDate>
#include <QDateTime>
#include <cmath>
#include <memory>
#include <QElapsedTimer>
#include <QFile>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <tsre/environment/Weather.h>
#include <tsre/sim/OutsideAir.h>
#include <tsre/sim/SteamHeating.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/trains/VehicleDefinition.h>

namespace {

bool about(double a, double b, double within) {
    return std::fabs(a - b) <= within;
}

// The climate's estimate: the equator warm all year; at Warsaw's latitude
// (52°) a winter below 0 °C and a summer about 18 °C, as its climate has
// them (-1.8 and 19.4 at 106 m); the south's seasons half a year on; the day
// 8 °C from 03:00 to 15:00; a km up 6.5 °C colder; snow at 0 °C at most.
void checkClimate(TokenTest::Suite &test) {
    using Sim::ClimateTemperature;
    // The day's mean: at 09:00 and 21:00 the daily swing is nothing.
    const auto mean = [](double latitude, double day) { return ClimateTemperature(latitude, day, 9.0); };
    const double equatorJanuary = mean(0.0, 15.0), equatorJuly = mean(0.0, 196.0);
    const double warsawJanuary = mean(52.2, 15.0), warsawJuly = mean(52.2, 196.0);
    const double southJuly = mean(-52.2, 196.0 + 1.0), southJanuary = mean(-52.2, 15.0 + 2.0);
    const double day = ClimateTemperature(52.2, 15.0, 15.0) - ClimateTemperature(52.2, 15.0, 3.0);
    test.check(about(equatorJanuary, 26.0, 1.0) && about(equatorJuly, 26.0, 1.0) && warsawJanuary < 0.0
                       && warsawJanuary > -5.0 && warsawJuly > 16.0 && warsawJuly < 21.0
                       && about(southJuly, warsawJanuary, 0.2) && about(southJanuary, warsawJuly, 0.2)
                       && about(day, 8.0, 1e-9),
               QString("the climate's estimate: the equator %1 and %2 °C; 52° N mid-January %3, mid-July %4; 52° S "
                       "mid-July %5, mid-January %6; 15:00 %7 °C warmer than 03:00")
                       .arg(equatorJanuary, 0, 'f', 1).arg(equatorJuly, 0, 'f', 1).arg(warsawJanuary, 0, 'f', 1)
                       .arg(warsawJuly, 0, 'f', 1).arg(southJuly, 0, 'f', 1).arg(southJanuary, 0, 'f', 1)
                       .arg(day, 0, 'f', 1));

    // A run's air: the climate at its first day and time, a day later the
    // next day's; up 1,600 m (Marias Pass, USA1) 10.4 °C colder; snow.
    Sim::OutsideAir air;
    air.latitude = 48.3;
    air.date = QDate(2026, 1, 15);
    const double low = air.at(9.0 * 3600.0, 0.0), pass = air.at(9.0 * 3600.0, 1600.0);
    const double nextDay = air.at(86400.0 + 9.0 * 3600.0, 0.0);
    air.date = QDate(2026, 7, 15);
    const double summer = air.at(9.0 * 3600.0, 0.0);
    air.snow = true;
    const double snowing = air.at(9.0 * 3600.0, 0.0);
    test.check(about(low - pass, 10.4, 1e-9) && about(low, mean(48.3, 15.0), 1e-9)
                       && about(nextDay, mean(48.3, 16.0), 1e-9) && summer > 15.0 && snowing == 0.0
                       && pass < -8.0 && pass > -14.0,
               QString("a run's air at 48.3° N on 15 January 09:00: %1 °C at sea level, %2 at 1,600 m, the next day "
                       "%3; in July %4, snowing %5")
                       .arg(low, 0, 'f', 1).arg(pass, 0, 'f', 1).arg(nextDay, 0, 'f', 2).arg(summer, 0, 'f', 1)
                       .arg(snowing, 0, 'f', 1));
}

// The route's weather where it has one: its keyframes' air, at the run's
// hours (a relative weather from midnight, looping; an absolute one from its
// start), the same at any height.
void checkWeather(TokenTest::Suite &test) {
    auto relative = std::make_shared<Weather>();
    WeatherKeyframe k;
    k.time = 0.0f;
    k.air.temperature = 10.0f;
    relative->keyframes.push_back(k);
    k.time = 12.0f;
    k.air.temperature = 20.0f;
    relative->keyframes.push_back(k);
    Sim::OutsideAir air;
    air.date = QDate(2026, 1, 15);
    air.weather = relative;
    air.weatherMidnight = relative->editorHours(air.date, 0.0);
    const double six = air.at(6.0 * 3600.0, 1000.0), looped = air.at(30.0 * 3600.0, 0.0);

    auto absolute = std::make_shared<Weather>();
    absolute->start = QDateTime(QDate(2026, 1, 14), QTime(12, 0), Qt::UTC);
    k.time = 0.0f;
    k.air.temperature = -5.0f;
    absolute->keyframes.push_back(k);
    k.time = 24.0f;
    k.air.temperature = 5.0f;
    absolute->keyframes.push_back(k);
    air.weather = absolute;
    air.weatherMidnight = absolute->editorHours(air.date, 0.0);
    air.snow = true;
    const double midnight = air.at(0.0, 0.0), noon = air.at(12.0 * 3600.0, 0.0);
    test.check(six == 15.0 && looped == 15.0 && about(midnight, 0.0, 1e-6) && about(noon, 5.0, 1e-6),
               QString("the route's weather: relative %1 °C at 06:00 (15) 1 km up, %2 a day later (15); absolute from "
                       "noon the day before: %3 at midnight (0), %4 at noon (5), its own air in snow")
                       .arg(six).arg(looped).arg(midnight).arg(noon));
}

// The run's first day: in an MSTS activity's season, or moved to its middle
// in the same year (the south's seasons half a year on).
void checkSeason(TokenTest::Suite &test) {
    using Sim::SeasonDate;
    const QDate june(2026, 6, 21), january(2026, 1, 10);
    const QDate winter = SeasonDate(june, 3, 50.0), southWinter = SeasonDate(june, 3, -35.0);
    const QDate stays = SeasonDate(january, 3, 50.0), summer = SeasonDate(january, 1, 50.0);
    const QDate spring = SeasonDate(january, 0, 50.0), autumn = SeasonDate(june, 2, 50.0);
    const QDate southSummer = SeasonDate(june, 1, -35.0), none = SeasonDate(june, -1, 50.0);
    test.check(winter == QDate(2026, 1, 15) && southWinter == june && stays == january
                       && summer == QDate(2026, 7, 15) && spring == QDate(2026, 4, 15)
                       && autumn == QDate(2026, 10, 15) && southSummer == QDate(2026, 1, 15) && none == june,
               QString("the activity's season: 21 June in winter %1 (north), %2 (south); 10 January in winter %3, "
                       "summer %4, spring %5; 21 June in autumn %6; south summer %7; no season %8")
                       .arg(winter.toString(Qt::ISODate), southWinter.toString(Qt::ISODate),
                            stays.toString(Qt::ISODate), summer.toString(Qt::ISODate), spring.toString(Qt::ISODate),
                            autumn.toString(Qt::ISODate), southSummer.toString(Qt::ISODate),
                            none.toString(Qt::ISODate)));
}

bool writeUtf16(const QString &path, const QString &text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QStringEncoder encoder(QStringEncoder::Utf16LE);
    return file.write("\xff\xfe" + QByteArray(encoder(text))) > 0;
}

// A wagon file: its type, size and lines of its own.
QString wagonFile(const QString &type, const QString &size, const QString &extra = QString()) {
    return QString("SIMISA@@@@@@@@@@JINX0D0t______\n\nWagon ( Test\n\tType ( %1 )\n\tSize ( %2 )\n\tMass ( 40t )\n%3)\n")
            .arg(type, size, extra);
}

void writeFixtures(const QString &folder) {
    writeUtf16(folder + "/coach.wag", wagonFile("Carriage", "2.9m 4m 24.5m"));
    writeUtf16(folder + "/warm.wag", wagonFile("Carriage", "2.9m 4m 24.5m",
                                               "\tORTSHeatingCompartmentTemperatureSet ( 22degC )\n"
                                               "\tORTSHeatingWindowDeratingFactor ( 0.2 )\n"
                                               "\tEffects ( SpecialEffects ( HeatingHoseFX ( 0 0.5 12 0 0 1 0.05 ) ) )\n"));
    writeUtf16(folder + "/freight.wag", wagonFile("Freight", "3m 4m 15m"));
    writeUtf16(folder + "/crew.wag", wagonFile("Freight", "3m 4m 15m", "\tORTSHeatingCompartmentTemperatureSet ( 20 )\n"));
    writeUtf16(folder + "/loco.eng",
               wagonFile("Engine", "3m 4.5m 20m") + "Engine ( Test\n\tType ( Diesel )\n\tMaxSteamHeatingPressure ( 58 )\n)\n");
}

std::shared_ptr<const VehicleDefinition> definition(const QString &folder, const QString &name) {
    return VehicleDefinition::read(folder, name, folder, false);
}

Sim::HeatingSpec spec(const QString &folder, const QString &name) {
    return Sim::ReadHeating(*definition(folder, name));
}

// Steam's properties (Open Rails' tables) and each vehicle's part from its
// file: a coach heated, its compartment and radiators from its size; a
// freight wagon without the pipe, one setting a temperature heated; a loco
// fitted at 58 psi.
void checkSpecs(TokenTest::Suite &test, const QString &folder) {
    using namespace Sim;
    const double t0 = SaturationTemperature(0.0), t1 = SaturationTemperature(1e5);
    const double latent0 = LatentHeat(0.0), latent4 = LatentHeat(4e5), flash = WaterHeatOver100(2e5) / latent0;
    test.check(about(t0, 100.0, 0.1) && about(t1, 118.9, 0.3) && about(latent0, 2257e3, 5e3)
                       && about(latent4, 2108e3, 10e3) && about(flash, 0.066, 0.005),
               QString("steam: %1 °C at 0 bar, %2 at 1 bar (120.2 in the steam tables, Open Rails' table "
                       "linear between its points); condensing %3 kJ/kg at 0 bar, %4 at 4 bar; %5 % of 2 bar's "
                       "water flashes")
                       .arg(t0, 0, 'f', 1).arg(t1, 0, 'f', 1).arg(latent0 / 1e3, 0, 'f', 0).arg(latent4 / 1e3, 0, 'f', 0)
                       .arg(flash * 100.0, 0, 'f', 1));
    const HeatingSpec coach = spec(folder, "coach.wag"), warm = spec(folder, "warm.wag");
    const HeatingSpec freight = spec(folder, "freight.wag"), crew = spec(folder, "crew.wag");
    const HeatingSpec loco = spec(folder, "loco.eng");
    const double ua = BodyLoss(coach, 25.0) + AirChangeLoss(coach, 25.0, false);
    const double still = BodyLoss(coach, 0.0) + AirChangeLoss(coach, 0.0, false);
    test.check(coach.pipe && coach.heated && about(coach.volume, 23.3 * 2.7 * 2.4, 0.01) && coach.windows == 0.275
                       && coach.setTemperature == 20.0 && coach.radiators > 300.0 && coach.radiators < 600.0
                       && warm.setTemperature == 22.0 && warm.windows == 0.2 && warm.hoseEmitter
                       && !freight.pipe && !freight.heated && crew.pipe && crew.heated && loco.pipe && !loco.heated
                       && about(loco.maxPressure, 58.0 * 6894.757, 1.0) && ua > 2.0 * still,
               QString("a coach: %1 m³, its body and air %2 W/K standing, %3 at 25 m/s, radiators %4 W/K; a freight "
                       "wagon no pipe, one setting a temperature heated; the loco fitted at %5 bar")
                       .arg(coach.volume, 0, 'f', 0).arg(still, 0, 'f', 0).arg(ua, 0, 'f', 0)
                       .arg(coach.radiators, 0, 'f', 0).arg(loco.maxPressure / 1e5, 0, 'f', 2));
}

// Steps a heating for `seconds` at a supply on the vehicle `feeding`.
void run(Sim::SteamHeating &h, double seconds, int feeding, double supply, double air, double speed,
         bool doors = false) {
    std::vector<double> supplies(h.vehicles.size(), 0.0);
    if (feeding >= 0)
        supplies[size_t(feeding)] = supply;
    for (double t = 0.0; t < seconds - 1e-9; t += Sim::SteamHeating::Step)
        h.step(Sim::SteamHeating::Step, supplies, air, speed, doors);
}

// A loco and ten coaches at -10 °C: in steady running at 25 m/s on 4 bar,
// started warm, the steam a coach takes against published figures (100 to
// 150 lb/h a car in cold weather: 45 to 70 kg/h), the pressure falling along
// the train; started cold and standing, the steam reaching the last coach
// and the coaches warming over the next hour; without steam, cooling.
void checkTrain(TokenTest::Suite &test, const QString &folder) {
    using namespace Sim;
    const HeatingSpec loco = spec(folder, "loco.eng"), coach = spec(folder, "coach.wag");
    std::vector<HeatingSpec> train{loco};
    for (int i = 0; i < 10; ++i)
        train.push_back(coach);
    const double air = -10.0;
    {
        SteamHeating h;
        h.setUp(train, air, true, 1);
        QElapsedTimer timer;
        timer.start();
        run(h, 7200.0, 0, 4e5, air, 25.0);
        const double us = timer.nsecsElapsed() / 1000.0 / 72000.0;
        const double total = h.vehicles[0].supplied * 3600.0;
        double radiators = 0.0, leaks = 0.0;
        for (const auto &v : h.vehicles) {
            radiators += v.radiatorHeat / LatentHeat(v.pressure) * 3600.0;
            leaks += v.hoseLeak * 3600.0;
        }
        const double first = h.vehicles[1].air, lastAir = h.vehicles[10].air;
        const double second = h.vehicles[1].pressure, end = h.vehicles[10].pressure;
        const double perCoach = radiators / 10.0;
        test.check(h.source == 0 && perCoach > 40.0 && perCoach < 80.0 && total > radiators && end < second
                       && end > 0.05e5 && second > 3e5 && std::fabs(first - 20.0) < 1.5 && lastAir > 15.0,
                   QString("a loco and 10 coaches at -10 °C, 25 m/s, 4 bar, warm, 2 h: %1 kg/h in all, the radiators "
                           "%2 a coach, leaks %3; %4 bar at the first coach, %5 at the last; their air %6 and %7 °C; "
                           "%8 µs a step")
                           .arg(total, 0, 'f', 0).arg(perCoach, 0, 'f', 1).arg(leaks, 0, 'f', 1)
                           .arg(second / 1e5, 0, 'f', 2).arg(end / 1e5, 0, 'f', 2).arg(first, 0, 'f', 1)
                           .arg(lastAir, 0, 'f', 1).arg(us, 0, 'f', 1));
    }
    {
        SteamHeating h;
        h.setUp(train, air, false, 1);
        double reached = -1.0, firstWarm = -1.0, lastWarm = -1.0;
        for (int s = 1; s <= 3 * 3600; ++s) {
            run(h, 1.0, 0, 4e5, air, 0.0);
            if (reached < 0.0 && h.vehicles[10].pressure > 0.2e5)
                reached = s;
            if (firstWarm < 0.0 && h.vehicles[1].air >= 18.0)
                firstWarm = s;
            if (lastWarm < 0.0 && h.vehicles[10].air >= 18.0)
                lastWarm = s;
        }
        test.check(reached > 300.0 && reached < 3600.0 && firstWarm > 600.0 && firstWarm < 3600.0
                           && lastWarm > reached && lastWarm < 3.0 * 3600.0,
                   QString("cold at -10 °C, standing, 4 bar on: steam at the last coach after %1 min; the first coach "
                           "at 18 °C after %2 min, the last after %3")
                           .arg(reached / 60.0, 0, 'f', 1).arg(firstWarm / 60.0, 0, 'f', 0)
                           .arg(lastWarm / 60.0, 0, 'f', 0));
    }
    {
        SteamHeating standing, moving;
        standing.setUp(train, air, true, 1);
        moving.setUp(train, air, true, 1);
        run(standing, 3600.0, -1, 0.0, air, 0.0);
        run(moving, 3600.0, -1, 0.0, air, 25.0);
        const double still = standing.vehicles[5].air, fast = moving.vehicles[5].air;
        test.check(still < 15.0 && still > -2.0 && fast < still && standing.vehicles[5].pressure == 0.0,
                   QString("no steam at -10 °C for an hour: a coach from 20 °C to %1 standing, %2 at 25 m/s")
                           .arg(still, 0, 'f', 1).arg(fast, 0, 'f', 1));
    }
}

// The pipe ends at a freight wagon; a loco in the middle feeds both ways;
// the highest pressure feeds; the leaks and the traps' steam show.
void checkPipe(TokenTest::Suite &test, const QString &folder) {
    using namespace Sim;
    const HeatingSpec loco = spec(folder, "loco.eng"), coach = spec(folder, "coach.wag");
    const HeatingSpec freight = spec(folder, "freight.wag"), warm = spec(folder, "warm.wag");
    SteamHeating broken;
    broken.setUp({loco, coach, freight, coach}, 0.0, true, 2);
    run(broken, 60.0, 0, 4e5, 0.0, 10.0);
    SteamHeating middle;
    middle.setUp({warm, coach, loco, coach, warm}, 0.0, true, 3);
    std::vector<double> supplies{0.0, 0.0, 4e5, 0.0, 0.0};
    for (int i = 0; i < 600; ++i)
        middle.step(SteamHeating::Step, supplies, 0.0, 10.0, false);
    const bool both = middle.source == 2 && middle.first == 0 && middle.last == 4 && middle.vehicles[0].pressure > 1e5
            && middle.vehicles[4].pressure > 1e5;
    const double hose = middle.vehicles[0].hoseLeak * 3600.0, trap = middle.vehicles[0].radiatorTrap * 3600.0;
    supplies[0] = 5e5;
    for (int i = 0; i < 100; ++i)
        middle.step(SteamHeating::Step, supplies, 0.0, 10.0, false);
    test.check(broken.vehicles[1].pressure > 1e5 && broken.vehicles[2].pressure == 0.0
                       && broken.vehicles[3].pressure == 0.0 && broken.last == 1 && both && hose > 0.5 && hose < 10.0
                       && trap > 0.0 && middle.source == 0,
               QString("a freight wagon ends the pipe (%1 bar behind it); a loco in the middle feeds %2 and %3 bar "
                       "both ways; the end coach's couplings leak %4 kg/h, its radiators' traps flash %5 kg/h; a "
                       "higher pressure at the end feeds instead")
                       .arg(broken.vehicles[3].pressure / 1e5).arg(middle.vehicles[0].pressure / 1e5, 0, 'f', 2)
                       .arg(middle.vehicles[4].pressure / 1e5, 0, 'f', 2).arg(hose, 0, 'f', 1).arg(trap, 0, 'f', 2));
}

}

int TsreTests::runSteamHeatingSuite(bool verbose) {
    TokenTest::Suite test{"[tests:heating]", verbose};
    checkClimate(test);
    checkWeather(test);
    checkSeason(test);
    QTemporaryDir files;
    writeFixtures(files.path());
    checkSpecs(test, files.path());
    checkTrain(test, files.path());
    checkPipe(test, files.path());
    return test.finish();
}
