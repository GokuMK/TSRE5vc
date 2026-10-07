/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ROUTEDATATOOLS_H
#define ROUTEDATATOOLS_H

#include <memory>
#include <vector>
#include "EditorTool.h"

// Tools for route-scale data: geo data of terrain tiles (map tiles,
// imagery, heights) and activity items (loose consists, speed zones, event
// locations).
namespace RouteDataTools {
std::vector<std::unique_ptr<EditorTool>> create();
}

#endif
