/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef CLOUDNOISE_H
#define CLOUDNOISE_H

#include <QtGlobal>
#include <vector>

// The 3D noise the volumetric clouds are carved from (task renderer 28),
// tileable in all three directions, after Schneider ("The Real-time
// Volumetric Cloudscapes of Horizon Zero Dawn", 2015) and Hillaire's
// TileableVolumeNoise:
// - shape, size^3 RGBA8: R Perlin-Worley (Perlin octaves remapped by
//   Worley octaves), G, B, A Worley octaves at 2, 4 and 8 times the
//   frequency: the clouds' billows;
// - detail, size^3 RGBA8: R, G, B Worley octaves at three frequencies: the
//   wisps that erode the edges.
// Rows run along x, then y, then slices along z.
namespace CloudNoise {

std::vector<quint8> shape(int size = 128);
std::vector<quint8> detail(int size = 32);

// Tileable Worley noise: 1 - the distance to the nearest of one random point
// per cell (cells per period), clamped; p in periods.
float worley(float x, float y, float z, int cells);
// Tileable gradient noise in fractal octaves, 0-1; frequency cells per
// period, each octave twice as fine.
float perlin(float x, float y, float z, int frequency, int octaves);

}

#endif
