/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TEXTUREALPHA_H
#define TEXTUREALPHA_H

#include <QByteArray>

// What a texture's alpha channel holds, found once when it loads (level 0).
// MSTS shapes mark many opaque parts as blended; with this the renderers draw
// such parts with the opaque ones (see RenderItem::drawSurface).
namespace TextureAlpha {

enum Class : unsigned char {
    Unknown = 0,
    // Every texel opaque (alpha 250 or more).
    Opaque,
    // Texels opaque or fully transparent (alpha 5 or less), nothing between.
    Binary,
    // Some texels partly transparent.
    Partial
};

// Of texels with the given number of components (alpha is the fourth).
Class ofPixels(const unsigned char *pixels, qsizetype texels, int components);
// Of DXT blocks in an OpenGL S3TC format (DXT1 without and with alpha, DXT3,
// DXT5); Unknown for other formats.
Class ofBlocks(const QByteArray &blocks, int glFormat);

}

#endif
