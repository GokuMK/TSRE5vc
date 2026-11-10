#pragma once

namespace TsreTests {
// MaSzyna's models (task maszyna 01): E3dFile on small files built here, each
// chunk type, the tree, the levels, and what it refuses.
int runE3dFileSuite(bool verbose);
// Every .e3d under the game root's dynamic/ read: no failures, the review's
// totals for dynamic/pkp, the time.
int runE3dCorpusSuite(bool verbose);
// E3dShape on a small model: packets, levels, pose, what is hidden, repaints.
int runE3dShapeSuite(bool verbose);
// MaSzyna's .mmd, .fiz and textures.txt (task maszyna 02) on small files.
int runMaszynaVehicleSuite(bool verbose);
// Every vehicle under the game root's dynamic/ read: each model found, each
// catalogue line matched to a type, what is not in one report.
int runMaszynaVehicleCorpusSuite(bool verbose);
// A vehicle's and a train's files by main folder (MaSzyna and ENG), their
// archive and the Consist Editor's dialog.
int runVehicleFilesSuite(bool verbose);
}
