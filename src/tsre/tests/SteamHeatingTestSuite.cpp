#include <tsre/tests/SteamHeatingTestSuite.h>

#include <QDate>
#include <QDateTime>
#include <cmath>
#include <memory>
#include <tsre/environment/Weather.h>
#include <tsre/sim/OutsideAir.h>
#include <tsre/tests/TokenTestSupport.h>

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

}

int TsreTests::runSteamHeatingSuite(bool verbose) {
    TokenTest::Suite test{"[tests:heating]", verbose};
    checkClimate(test);
    checkWeather(test);
    checkSeason(test);
    return test.finish();
}
