/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TERRAINEDITTOOLS_H
#define TERRAINEDITTOOLS_H

#include <memory>
#include <vector>
#include "EditorTool.h"

// Tools that edit the terrain: height and texture brushes, procedural
// materials, water, gaps, drawing and tile textures.
namespace TerrainEditTools {
std::vector<std::unique_ptr<EditorTool>> create();
}

#endif
