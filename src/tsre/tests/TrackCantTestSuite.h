#pragma once

namespace TsreTests {
// Task tracks 16: superelevation as Open Rails computes it.
int runTrackCantSuite(bool verbose);
// Its cost on a real route (--game-root, --route); not in the default run.
int runTrackCantBenchmark(bool verbose);
}
