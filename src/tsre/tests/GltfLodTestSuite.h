#pragma once

namespace TsreTests {
// Levels of detail of glTF shapes (task shapes 10): MSFT_lod groups in
// glTF and GLB files, the packets of each level, invalid references, and
// the level chosen by screen coverage (ShapeLod).
int runGltfLodSuite(bool verbose);
}
