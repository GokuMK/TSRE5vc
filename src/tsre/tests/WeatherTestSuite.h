#ifndef TSRE_WEATHER_TEST_SUITE_H
#define TSRE_WEATHER_TEST_SUITE_H

#include <QString>

namespace TsreTests {
// Env files and weather (task environment 01). casesDirectory: optional, a
// directory searched for .env files to round-trip.
int runWeatherSuite(const QString &casesDirectory, bool verbose);
}

#endif
