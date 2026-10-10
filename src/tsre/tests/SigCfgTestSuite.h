#pragma once
#include <QString>

namespace TsreTests {
// The sigcfg.dat reader on a fixture; with a path, also on that file.
int runSigCfgSuite(const QString &configurationPath, bool verbose);
}
