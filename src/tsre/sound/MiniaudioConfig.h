/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MINIAUDIOCONFIG_H
#define MINIAUDIOCONFIG_H

// miniaudio with the options TSRE builds it with: every file that includes
// it goes through here, so the declarations match the one implementation
// (Miniaudio.cpp). TSRE reads its own wave files and plays them through
// its own data sources, so no decoders, encoders or resource manager.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_GENERATION
#include <miniaudio/miniaudio.h>

#endif
