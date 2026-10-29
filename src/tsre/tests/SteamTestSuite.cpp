#include <tsre/tests/SteamTestSuite.h>
#include "TokenTestSupport.h"
#include <cmath>
#include <tsre/sim/steam/SteamTables.h>

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

}

int TsreTests::runSteamSuite(bool verbose) {
    TokenTest::Suite test{"[tests:steam]", verbose};
    checkTables(test);
    return test.finish();
}
