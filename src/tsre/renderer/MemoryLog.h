/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MEMORYLOG_H
#define MEMORYLOG_H

#include <QString>

class QRhiBuffer;
class QRhiTexture;

// Where memory goes, in log.txt (docs/features/log.md): from the 3D view a
// line at its first frame, then once a minute (TSRE_MEMORY_LOG=<seconds>
// another interval, 0 none). The GPU's own and shared memory as its driver
// tells it, QRhi's allocations, the textures and meshes, the process's and
// the system's memory, what is loaded, and the GPU resources TSRE could not
// create while running.
namespace MemoryLog {

// The process's and the system's memory in MB, -1 where not known.
struct Process {
    double privateMB = -1, workingSetMB = -1, peakWorkingSetMB = -1;
    double systemUsedMB = -1, systemTotalMB = -1, commitMB = -1, commitLimitMB = -1;
};
Process process();

// From the 3D view, after its frame: the line when it is due.
void frame();
// The line's text.
QString line();

// create() for a GPU resource made while TSRE runs (meshes, textures, the
// arenas, the UI's geometry), whose code goes on without it: the first
// failure of each kind (what) to the log with its size, all of them counted
// for the lines. Whether it was created.
bool created(QRhiBuffer *buffer, const char *what);
bool created(QRhiTexture *texture, const char *what);
// The failures so far ("instance arena 2, UI geometry 1"), empty for none.
QString failures();

}

#endif
