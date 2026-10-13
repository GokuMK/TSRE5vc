#pragma once
#include <QString>
namespace TsreTests {
// Activity, service, traffic and consist files read and written by TSRE:
// the written file must hold what the original held. Files written for the
// test (Open Rails additions, comments, unknown blocks at each level), then
// every ACT, SRV, TRF and CON file under contentRoot when one is given.
int runActivityRoundTripSuite(const QString &contentRoot, bool verbose);
}
