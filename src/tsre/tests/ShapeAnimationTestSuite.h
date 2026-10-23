#pragma once
#include <QString>

namespace TsreTests {
// Shape keyframe animation (task shapes 12): the evaluator's rules (Open
// Rails'), per-part poses and reused states; with stock trainset shapes
// (TSRE_ANIMATION_SHAPES or the game root's TRAINS/TRAINSET), each keyed
// part's frame 0 against its rest matrix and both MSTS backends posing
// alike.
int runShapeAnimationSuite(bool verbose);
}
