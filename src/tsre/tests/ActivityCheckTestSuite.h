#pragma once
#include <QString>
namespace TsreTests {
// Check Activity on activities written for the test: a sound one without
// errors, a broken one with each kind of problem found.
int runActivityCheckSuite(bool verbose);
// Check Activity on every activity of a route (--game-root, --route), with
// its track database: the findings by kind, to judge them.
int runActivityCheckRouteSuite(bool verbose);
}
