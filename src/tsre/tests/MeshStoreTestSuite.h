#ifndef TSRE_MESH_STORE_TEST_SUITE_H
#define TSRE_MESH_STORE_TEST_SUITE_H

namespace TsreTests {
int runMeshStoreSuite(bool verbose);
// Uploads of edited terrain through the QRhi renderer's mesh store: a paged
// page rewritten patch by patch, and a legacy tile replaced whole, each
// frame (core.rendering.rhiApi selects the API).
int runMeshUploadRhiBenchmark(bool verbose);
}

#endif
