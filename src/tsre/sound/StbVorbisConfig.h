/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef STBVORBISCONFIG_H
#define STBVORBISCONFIG_H

// stb_vorbis (thirdparty/stb_vorbis) with the options TSRE builds it with:
// every file that uses it goes through here, so the declarations match the
// one implementation (StbVorbis.cpp). TSRE reads the file itself and
// decodes it from memory: no stdio, no push-data API.
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#ifndef TSRE_STB_VORBIS_IMPLEMENTATION
#define STB_VORBIS_HEADER_ONLY
#endif
#include <stb_vorbis/stb_vorbis.c>

#endif
