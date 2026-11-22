#ifndef TSRE_SHAPE_AXES_TEST_SUITE_H
#define TSRE_SHAPE_AXES_TEST_SUITE_H

namespace TsreTests {
// One axis space for .sd files (task shapes 20): ESD_TSREAxes read, every
// block's positions turned into the model's own axes by every reader (both
// MSTS backends, ShapeMetadata for .e3d, ShapeSeats::readFor), one MSTS .sd
// putting things in one place beside an .s and an .e3d, display squares on
// the root with an MSTS face on every model.
int runShapeAxesSuite(bool verbose);
}

#endif
