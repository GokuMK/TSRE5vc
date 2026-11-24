#pragma once

namespace TsreTests {
// Tracks task 17: tsection.dat shapes grouped by geometry, their kinds and
// reference lines (TrackShapeAnalysis), on shapes made here; with
// --game-root and TSRE_TSECTION_REVIEW_CSV (a CSV of
// extra/tsection-review/review.py on that root's GLOBAL/tsection.dat) every
// shape the CSV lists, against review.py's geometry.
int runTrackShapeAnalysisSuite(bool verbose);
// Tracks task 17: the route's trackprofiles.dat filters: reading, the first
// match, kinds, parts, loading with the profile catalog.
int runTrackProfileFiltersSuite(bool verbose);
}
