/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SHAPEAXES_H
#define SHAPEAXES_H

class QString;

// The axes an .sd's positions are written in (task shapes 20): one space for
// every model format, so an .sd keeps its displays, lights and exhausts in
// place when its S model is upgraded to glTF.
//
//   ESD_TSREAxes ( MSTS )   as S files: left-handed, +x the model's right
//   ESD_TSREAxes ( glTF )   right-handed, +x the model's left: glTF's, and
//                           .e3d's and glTF's as TSRE draws them
//
// Without the line: MSTS, whatever the model. Both have +y up and +z the
// model's front; they differ in x alone. The readers turn an .sd's values
// into its model's own axes (an .s's MSTS, any other model's glTF), mirrored
// in x where the two differ; whatever uses them sees the model's own space,
// as before.
namespace ShapeAxes {

enum class Axes { Msts, Gltf, Unknown };

// An ESD_TSREAxes value (any case); empty: MSTS; neither name: Unknown, read
// as MSTS.
Axes fromName(const QString &name);
// The axes a model file's own space has, by its extension: an .s MSTS,
// every other model (.e3d, .glb, .gltf) glTF.
Axes ofModel(const QString &modelPath);
// Whether values written in `written` axes are mirrored in x to reach
// `model`'s (Unknown counts as MSTS).
bool mirrors(Axes written, Axes model);
// The problem an Unknown value makes, for the blocks it moved (translated).
QString unknown(const QString &name);

// The mirror in x, value by value.
// A point or a direction.
inline void mirrorPoint(float *p) { p[0] = -p[0]; }
// Degrees about x, then y, then z (a display's Rotation, a complex box's).
inline void mirrorRotation(float *degrees) {
    degrees[1] = -degrees[1];
    degrees[2] = -degrees[2];
}
// Degrees about y (a seat's heading).
inline float mirrorHeading(float degrees) { return degrees == 0.0f ? 0.0f : -degrees; }
// A box x0 y0 z0 x1 y1 z1.
inline void mirrorBox(float *box) {
    const float x0 = -box[3], x1 = -box[0];
    box[0] = x0;
    box[3] = x1;
}

}

#endif // SHAPEAXES_H
