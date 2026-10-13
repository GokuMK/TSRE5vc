/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef PATHTOOLS_H
#define PATHTOOLS_H

#include <memory>
#include <vector>
#include "EditorTool.h"

// The path tool (task editor 06): edits the activity path chosen in the
// activity tools, in 3D and on the map, through its context menu.
namespace PathTools {
inline const char *const TrackPathToolId = "trackPathTool";
std::vector<std::unique_ptr<EditorTool>> create();
}

#endif
