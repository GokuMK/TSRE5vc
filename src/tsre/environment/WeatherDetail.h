/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef WEATHERDETAIL_H
#define WEATHERDETAIL_H

#include <QtGlobal>
#include <tsre/environment/Weather.h>
#include <vector>

// Seeded 2D gradient noise for the weather (the renderer evaluates the same
// functions on the GPU).
namespace WeatherNoise {
// Gradient noise at a point, about -0.7 to 0.7.
double gradient(double x, double y, quint32 seed);
// Octaves of gradient noise, each half as strong and twice as fine,
// normalised to about -1 to 1.
double fractal(double x, double y, int octaves, quint32 seed);
// The share of fractal(…, 4, …) values below value: maps the noise to an
// even distribution, so a threshold covers a known share of the area.
double distribution(double value);
// The 257 quantiles distribution() interpolates (the shaders take them as a
// table).
const std::vector<double> &quantiles();
// fractal() without the octaves an image of texel (in the noise's units)
// cannot show: each fades out between 4 and 2 texels a cell, the rest
// weighted up so the spread stays.
double fractal(double x, double y, int octaves, quint32 seed, double texel);
}

// Where the clouds are within a cell: the noise, drifting with the layer's
// wind, compared with the cell's coverage, so the covered share of an area
// matches its coverage.
namespace WeatherDetail {
// The size (m) of the noise's largest features for a cloud type.
double cloudScale(float type);
// How much of the point is under cloud, 0-1 (soft edges).
double cover(const Weather &weather, double hours, double east, double north, int layer);
// The same at a time computed once (Weather::frame), for many places.
// minimumEdge widens the soft edge (in the noise's even distribution), for
// images coarser than the clouds' edges; texel (m) leaves out the detail
// finer than an image shows, which would only speckle it.
double cover(const Weather &weather, const Weather::Frame &frame, double east, double north, int layer,
             double minimumEdge = 0.0, double texel = 0.0);
// The edge an image of texel metres needs for a cloud type.
double imageEdge(double texel, float type);
// How much of the generated detail an image of texel metres shows, 0-1:
// none where a texel is a third of the clouds' size or more (the coverage
// itself shows), all where it is a tenth or less.
double imageDetail(double texel, float type);
// Rain or snow at a point: the weather's rate varied over tens of km
// (averaging the rate), and for convective clouds falling only under the
// cells (the area's average kept).
// cells: how much the showers keep to the cells (imageDetail; 1 in 3D).
WeatherSurface precipitation(const Weather &weather, const Weather::Frame &frame, double east, double north,
                             double cells = 1.0);
}

#endif
