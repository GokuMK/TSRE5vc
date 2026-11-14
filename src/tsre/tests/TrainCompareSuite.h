#pragma once

namespace TsreTests {
// The same train driven the same way in TSRE and in another simulator (task
// driving 10, extra/train-compare): a scenario's MaSzyna train on straight
// track, hot started, its commands through its cab's controls as the mouse
// works them, sampled to a CSV of extra/train-compare/README.md's columns.
// TSRE_TRAIN_SCENARIO=<scenario.json>, TSRE_TRAIN_CSV=<out.csv>; the game
// root holds MaSzyna's dynamic/.
int runTrainCompareSuite(bool verbose);
}
