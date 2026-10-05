#pragma once

#include <QString>

namespace TsreTests {
// Captures the production serializers and complete save path into a fresh directory.
int runTdbRoundTripSuite(const QString &outputDirectory);
}
