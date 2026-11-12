/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef HUMANS_IMPOSTORBAKE_H
#define HUMANS_IMPOSTORBAKE_H

#include <QByteArray>
#include <QImage>
#include <QString>
#include <vector>
#include <tsre/shape/GltfShape.h>

class SkeletalAnimation;

namespace Humans {

// A human's impostor (task humans 03), baked on the CPU from its kept
// triangles (GltfShape::SkinnedSource) on any thread: the human posed and
// seen from `directions` ways around it, level, without perspective, each
// view in a cell of two atlases:
// - colour: the base colour times its factor (the human's ambient occlusion
//   is in its texture), alpha the share of the cell's pixel covered;
// - normals: the surface's normal (with its normal map) in the view's
//   axes, x right, y up, z towards the viewer, as 0.5 + n / 2; alpha 1.
// Direction k looks from angle 2 pi k / directions about +y (from +z
// towards +x) in the shape's space; its right is up x towards-viewer.
// Every cell shows the same rectangle of its view plane, so one quad per
// direction fits every pose: across from -halfWidth to halfWidth, up from
// bottom to top (shape units). Uncovered pixels take their nearest covered
// neighbour's colour and normal (alpha 0), so mipmaps do not darken edges.
//
// Compressed (the default), the atlases come with their mipmaps as blocks:
// colour DXT5, each cell's alpha scaled at every level so as many of its
// texels pass the cutoff as at full size (a far person keeps its outline),
// normals DXT1. About 3 MB with mipmaps for 16 directions and 12 poses at
// 64 x 128, against 17 MB uncompressed.
struct ImpostorBake {
    struct Pose {
        // A clip and a time in it (seconds); clip -1: the rest pose.
        int clip = -1;
        float time = 0.0f;
    };
    struct Request {
        int directions = 16;
        int cellWidth = 64;
        int cellHeight = 128;
        // Samples per pixel along each axis.
        int supersample = 2;
        std::vector<Pose> poses;
        bool compress = true;
        float alphaCutoff = 0.5f;
    };
    struct Result {
        bool ok = false;
        QString error;
        int directions = 0;
        int poses = 0;
        int columns = 0;
        int rows = 0;
        int cellWidth = 0;
        int cellHeight = 0;
        float halfWidth = 0.0f;
        float bottom = 0.0f;
        float top = 0.0f;
        // RGBA8888, straight alpha.
        QImage color;
        QImage normal;
        // Compressed: level 0 and its mipmaps down to 1 x 1, colour DXT5,
        // normals DXT1; each level half the one above (rounded down).
        std::vector<QByteArray> colorLevels, normalLevels;
        double milliseconds = 0.0;
        // Cell pose * directions + direction: its texture coordinates (u
        // min, v min, u max, v max; v down, as glTF's).
        void cellRect(int cell, float uv[4]) const;
    };
    static Result bake(const GltfShape::SkinnedSource &source, const SkeletalAnimation &rig, const Request &request);
};

}

#endif
