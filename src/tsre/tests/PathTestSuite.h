#ifndef TSRE_PATH_TEST_SUITE_H
#define TSRE_PATH_TEST_SUITE_H

#include <QString>

namespace TsreTests {
// Path editing (task editor 06) on small track databases: completion to a
// track end, halfway nodes, other exits, reversals, End Here on and off the
// path, loops, and the file the edits write.
int runPathEditSuite(bool verbose);
// Every path of the route (--game-root, --route): read, written and read
// again, node by node; and rebuilt with the path tool's edits from its
// start, its reversals, its other exits and its end. Writes the files to
// outputDirectory when one is given.
int runPathRoundTripSuite(const QString &outputDirectory, bool verbose);
}

#endif
