/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MAPTOOLS_H
#define MAPTOOLS_H

#include <memory>
#include <vector>
#include "EditorTool.h"

// Tools of the map mode only (task editor 04): mapMeasureTool, Measure Distance
// in the map's context menu (a line dragged on the map and its length; not the 3D
// Ruler object or the ruler tools that place it).
namespace MapTools {
// The id of the Measure Distance tool.
inline const char *const MeasureToolId = "mapMeasureTool";
std::vector<std::unique_ptr<EditorTool>> create();
}

#endif
