#include <tsre/sim/OutsideAir.h>
#include <tsre/environment/Weather.h>
#include <algorithm>
#include <cmath>

namespace Sim {

namespace {

// Land climate by latitude at sea level, every 10° from the equator to the
// pole (°C): the year's mean and its swing from it to the warmest and the
// coldest month's. Rounded from places on each line, brought to sea level
// (Warsaw at 52°, 106 m: 9.2 and 10.6; New York at 41°: 13.2 and 12.3;
// Helsinki and St Petersburg at 60°: 6 and 11 to 13; Cairo at 30°: 22 and 8).
constexpr double Mean[10] = {26.0, 26.0, 24.0, 20.0, 14.0, 9.5, 5.0, -4.0, -14.0, -20.0};
constexpr double Swing[10] = {1.0, 2.0, 5.0, 8.0, 11.0, 10.5, 11.5, 13.0, 15.0, 15.0};
// The coldest day in the north (20 January); the daily swing.
constexpr double ColdestDay = 20.0;
constexpr double DaySwing = 4.0;
// °C a metre up.
constexpr double Lapse = 0.0065;
constexpr double Year = 365.25;
constexpr double Pi = 3.14159265358979323846;

double byLatitude(const double (&table)[10], double latitude) {
    const double x = std::clamp(std::fabs(latitude) / 10.0, 0.0, 9.0);
    const int i = std::min(int(x), 8);
    return table[i] + (table[i + 1] - table[i]) * (x - i);
}

// The meteorological season of a month in the north (0 spring: March to May;
// 3 winter: December to February).
int seasonOfMonth(int month) {
    return month == 12 || month <= 2 ? 3 : (month - 3) / 3;
}

}

double ClimateTemperature(double latitude, double dayOfYear, double hours) {
    const double coldest = latitude >= 0.0 ? ColdestDay : ColdestDay + Year / 2.0;
    const double season = std::cos(2.0 * Pi * (dayOfYear - coldest) / Year);
    const double day = std::cos(2.0 * Pi * (hours - 15.0) / 24.0);
    return byLatitude(Mean, latitude) - byLatitude(Swing, latitude) * season + DaySwing * day;
}

QDate SeasonDate(const QDate &date, int season, double latitude) {
    if (!date.isValid() || season < 0 || season > 3)
        return date;
    // The south's seasons are the north's half a year on.
    const int north = latitude >= 0.0 ? season : (season + 2) % 4;
    if (seasonOfMonth(date.month()) == north)
        return date;
    static constexpr int Middle[4] = {4, 7, 10, 1};
    return QDate(date.year(), Middle[north], 15);
}

double OutsideAir::at(double seconds, double height) const {
    if (weather != nullptr && !weather->keyframes.isEmpty())
        return weather->air(weatherMidnight + seconds / 3600.0).temperature;
    const double days = std::floor(seconds / 86400.0);
    const double hours = (seconds - days * 86400.0) / 3600.0;
    const QDate first = date.isValid() ? date : QDate(2026, 6, 21);
    const double dayOfYear = first.dayOfYear() + days;
    double air = ClimateTemperature(latitude, dayOfYear, hours) - Lapse * height;
    if (snow)
        air = std::min(air, 0.0);
    return air;
}

}
