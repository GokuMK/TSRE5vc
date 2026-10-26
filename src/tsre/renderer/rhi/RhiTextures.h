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
#include <QString>
#include <QVector>
#include <vector>

class QRhiResourceUpdateBatch;
class QRhiTexture;

// Textures of the QRhi renderer, by handle. TexLib stores a handle where the
// OpenGL renderer has a texture name, so producers pass it on unchanged.
namespace RhiTextures {

// A mipmapped RGBA8 texture from RGBA levels (level 0 first). Missing levels
// are generated from level 0. The upload goes with the next frame's resource
// updates. Returns 0 when there is no QRhi. An evicted handle as reuse gets
// the texture back under the same handle (its sampling kept).
unsigned int create(int width, int height, const QVector<QByteArray> &levels,
                    unsigned int reuse = 0);
// Block-compressed formats textures can go to the GPU in unchanged: BC1
// without alpha (DXT1), BC2 (DXT3) and BC3 (DXT5).
enum class Blocks { Bc1, Bc2, Bc3 };
bool supportsBlocks();
// The same for any thread, as the last supportsBlocks() found it: 1 or 0,
// -1 before a QRhi was asked (the renderer asks once it is made).
int blocksSupportKnown();
// A block-compressed texture from its levels (level 0 first): level 0
// alone, without mipmaps, or every level down to 1x1. Returns 0 when there
// is no QRhi or the levels do not make either.
unsigned int createCompressed(int width, int height, Blocks format,
                              const QVector<QByteArray> &levels, unsigned int reuse = 0);
// A texture sampled as stored, without filtering or mipmaps: one byte
// (R8) or four floats (RGBA32F) per texel, rows tightly packed.
unsigned int createData(int width, int height, bool floats, const QByteArray &data);
// Replaces an RGBA8 texture's level 0 with RGBA rows of its size, its
// mipmaps made again from it when it makes its own; false for another size
// or format (nothing done).
bool replaceLevel0(unsigned int handle, int width, int height, const QByteArray &rgba);
// Replaces a rectangle of a texture's level 0 (rows tightly packed).
void updateRegion(unsigned int handle, int x, int y, int width, int height,
                  const QByteArray &data);
// What readBack gives: level 0 as RGBA8 rows, or as the blocks of a
// block-compressed texture; None when it could not read the texture.
enum class Contents { None, Rgba8, Bc1, Bc2, Bc3 };
// Reads a texture's level 0 back to the CPU, rows tightly packed, waiting
// for the GPU: what Texture::setEditable and texture copies need once the
// CPU pixels went with the upload (the OpenGL renderer reads its textures
// back the same way). Uploads not yet submitted go first. None without a
// QRhi, while a frame is being recorded, or when the backend cannot read
// the texture's format.
Contents readBack(unsigned int handle, QByteArray &data);
// A mipmapped RGBA8 2D array of side x side RGBA layers.
unsigned int createArray(int side, const QVector<QByteArray> &layers);
// How a texture is sampled: with its mipmaps (an OpenGL texture uploaded
// without them has none), and clamped at the edges (baked terrain).
void setSampling(unsigned int handle, bool mipmaps, bool clamp);
bool sampledWithMipmaps(unsigned int handle);
bool clampedToEdge(unsigned int handle);
bool sampledNearest(unsigned int handle);
// The texture behind a handle; null for 0, a released or an evicted handle.
// Counts as a use of the handle (lastUsed).
QRhiTexture *texture(unsigned int handle);
// Releases a handle's texture. The texture itself retires: it is deleted
// once the renderer forgot the resource sets made with it (takeRetired), so
// its address cannot come back as another texture's before.
void release(unsigned int handle);
// Releases a handle's texture but keeps the handle for a later create with
// reuse (texture memory residency, docs/tasks/textures/04-texture-memory.md).
void evict(unsigned int handle);
bool evicted(unsigned int handle);
// The clock time (setClock) the handle was last used or created.
qint64 lastUsed(unsigned int handle);
// The time uses are stamped with, in milliseconds, set once a frame.
void setClock(qint64 milliseconds);
// Textures released or evicted since the last call, for the renderer to
// forget, then delete with deleteLater.
std::vector<QRhiTexture *> takeRetired();
// Releases every texture and pending upload, before the QRhi goes away.
void releaseAll();
// Uploads recorded since the last call, for the renderer to submit before
// its first pass; null when there are none.
QRhiResourceUpdateBatch *takeUpdates();
// The batch new uploads go to (created when needed).
QRhiResourceUpdateBatch *updates();
// Live textures and their bytes by format, for TSRE_RHI_TRACE.
QString memorySummary();

}

#endif
