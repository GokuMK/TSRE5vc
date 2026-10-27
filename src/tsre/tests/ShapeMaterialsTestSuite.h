#ifndef TSRE_SHAPE_MATERIALS_TEST_SUITE_H
#define TSRE_SHAPE_MATERIALS_TEST_SUITE_H

namespace TsreTests {
// MSTS shape materials (task shapes 13): PBR factors, tangents, ORM
// packing, the complex backend's material editing and its round trip
// through text and binary files and the .sd, both MSTS backends' PBR
// packets, shape-part selection ids. No GL context.
int runShapeMaterialsSuite(bool verbose);
}

#endif
