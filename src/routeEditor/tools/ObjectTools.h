/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef OBJECTTOOLS_H
#define OBJECTTOOLS_H

#include <memory>
#include <vector>
#include "EditorTool.h"

// Tools that place, select and edit objects: select, place, auto place,
// signal link, flex points, continuous flex track and road, continuous
// ruler.
namespace ObjectTools {
std::vector<std::unique_ptr<EditorTool>> create();
}

#endif
