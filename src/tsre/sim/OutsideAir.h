#pragma once
#include <QDate>
#include <memory>

class Weather;

namespace Sim {

// The outside air over a run (task driving 06): its temperature from the
// route's weather where it has one, else an estimate of the climate at the
// route's latitude, by the date and the time of day. Set at the start, so the
// core reads it at its own time (fast runs too).
struct OutsideAir {
    // The route's weather (its keyframes only), null without one, and its
    // hours at the run's first midnight (Weather::editorHours).
    std::shared_ptr<const Weather> weather;
    double weatherMidnight = 0.0;
    // The climate's place and the run's first day: degrees north (- south).
    double latitude = 50.0;
    QDate date;
    // An MSTS activity's snow: the climate's air at most 0 °C (as Open
    // Rails has it).
    bool snow = false;

    // °C at `seconds` after the first day's midnight, `height` m above the
    // sea: the weather's as it is (the same everywhere); the climate's 6.5 °C
    // colder a km up (the standard atmosphere).
    double at(double seconds, double height) const;
};

// The climate's estimate at sea level (°C): the mean for the latitude and the
// day of the year (1 to 366; coldest about 20 January in the north, July in
// the south), 4 °C warmer at 15:00 and colder at 03:00 (as the weather
// generator has its day). Land, every 10° of latitude; the hemispheres alike.
double ClimateTemperature(double latitude, double dayOfYear, double hours);

// The date a run starts on: `date` where it falls in an MSTS activity's
// season (0 spring, 1 summer, 2 autumn, 3 winter; meteorological, the south's
// half a year on), else that season's middle in the same year (15 April, July,
// October, January in the north). Without a season (-1), `date`.
QDate SeasonDate(const QDate &date, int season, double latitude);

}
