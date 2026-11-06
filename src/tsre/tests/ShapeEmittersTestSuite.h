#ifndef TSRE_SHAPE_EMITTERS_TEST_SUITE_H
#define TSRE_SHAPE_EMITTERS_TEST_SUITE_H

namespace TsreTests {
// Shape emitters (task shapes 15): the .sd's ESD_TSREEmitter blocks read by
// both MSTS backends, and a placed object's emitters giving out gas into the
// scenery's particles.
int runShapeEmittersSuite(bool verbose);
}

#endif
