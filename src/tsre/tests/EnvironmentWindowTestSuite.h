#pragma once
#include <QString>

namespace TsreTests {
// The environment window's session values, Reset and Save to profile (on a
// temporary copy of the profile). With a path, also saves an image of the
// window there.
int runEnvironmentWindowSuite(const QString &imagePath, bool verbose);
}
