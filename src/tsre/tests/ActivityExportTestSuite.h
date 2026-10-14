#pragma once
#include <QString>
namespace TsreTests {
// Writes variants of an activity of a route (--game-root, --route) as TSRE
// saves them, for Open Rails to load: resaved unchanged, with the Open
// Rails additions, with its own traffic pattern, with waiting points on an
// AI path. The files are written into the route, so the game root must be
// a scratch copy holding a file TSRE_SCRATCH. TSRE_EXPORT_ACTIVITY names
// the activity (lclsrvce by default).
int runActivityExportSuite(bool verbose);
}
