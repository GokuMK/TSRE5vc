/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RHITEXTURES_H
#define RHITEXTURES_H

#include <QByteArray>
#include <QVector>

class QRhiResourceUpdateBatch;
class QRhiTexture;

// Textures of the QRhi renderer, by handle. TexLib stores a handle where the
// OpenGL renderer has a texture name, so producers pass it on unchanged.
namespace RhiTextures {

// A mipmapped RGBA8 texture from RGBA levels (level 0 first). Missing levels
// are generated from level 0. The upload goes with the next frame's resource
// updates. Returns 0 when there is no QRhi.
unsigned int create(int width, int height, const QVector<QByteArray> &levels);
// How a texture is sampled: with its mipmaps (an OpenGL texture uploaded
// without them has none), and clamped at the edges (baked terrain).
void setSampling(unsigned int handle, bool mipmaps, bool clamp);
bool sampledWithMipmaps(unsigned int handle);
bool clampedToEdge(unsigned int handle);
// The texture behind a handle; null for 0 or a released handle.
QRhiTexture *texture(unsigned int handle);
void release(unsigned int handle);
// Uploads recorded since the last call, for the renderer to submit before
// its first pass; null when there are none.
QRhiResourceUpdateBatch *takeUpdates();
// The batch new uploads go to (created when needed).
QRhiResourceUpdateBatch *updates();

}

#endif
