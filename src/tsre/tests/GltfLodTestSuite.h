#pragma once

namespace TsreTests {
// Levels of detail of glTF shapes (task shapes 10): MSFT_lod groups in
// glTF and GLB files, the packets of each level, invalid references, and
// the level chosen by screen coverage (ShapeLod); and the foliage's diffuse
// transmission (KHR_materials_diffuse_transmission, task renderer 36).
int runGltfLodSuite(bool verbose);
}
