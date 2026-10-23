#pragma once
#include <QString>

namespace TsreTests {
// Open Rails 3D cabs in ENG files (ORTS3DCab, its alternate viewpoints) on
// fixtures: the cab's shape and viewpoints, the one at each end, the camera's
// place and limits there, the consist riding in it and the riding camera
// keeping to them.
int runCab3DSuite(bool verbose);
}
