/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef FRAMELOG_H
#define FRAMELOG_H

#include <QtGlobal>
#include <atomic>

// A log of a real session, one CSV line a frame of the route editor's 3D
// view (core.system.frameLog, or TSRE_FRAME_LOG=<file>): where the frame's
// time went (waiting for the GPU, the paint's sections, the submit), the GPU's
// time, what it made and loaded, and where the camera was. Written to
// frames-YYYYMMDD-HHMMSS.csv beside log.txt, a file each session. Off, every
// call returns at its first check.
namespace FrameLog {

// Whether the log is on (read once, on the first frame).
bool enabled();
// For code on any thread: true once the log is open.
extern std::atomic<bool> open;
inline bool active() { return open.load(std::memory_order_relaxed); }

// The view surface's frame: its start, beginFrame returned (the wait for a
// free frame: the GPU behind), the paint done, endFrame done (the line goes).
void frameStart();
void frameBegun();
void framePainted();
void frameEnd();

// The paint's sections: the time since the previous mark (or the paint's
// start) goes to the section named.
enum Section {
    WholeView,    // loading everything at once (first view, camera jump)
    Gather,       // terrain, world, overlays, water, sky gathered
    Shadows,      // shadow maps
    Enclosure,    // sky enclosure maps
    Environment,  // environment map faces
    Scene,        // the view's passes (lights, reflection, terrain to water)
    Overlay,      // compass, HUD
    Finish,       // renderFrame: the remaining passes, post-processing, flush
    SectionCount
};
void paintStart();
void mark(Section section);

// Counts of the frame, from the code doing it.
enum Counter {
    TexturesMade, TextureMs, MeshMs, UploadsDeferred,
    WorldTiles, WorldTileMs, TerrainTiles, TerrainTileMs, ShapesAdopted,
    CounterCount
};
void add(Counter counter, double value);
// Log lines written (any thread) and the main thread's time writing them.
void countLogLine(qint64 mainThreadNs);
void setGpuMs(double ms);
// The camera (rotations in radians): the line's camera column is it as
// core.startup.camera takes it, to start TSRE at that frame's view.
void setCamera(int tileX, int tileZ, const float *pos, float rotX, float rotY);

}

#endif
