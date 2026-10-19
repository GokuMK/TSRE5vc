/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef IMPOSTOR_H
#define IMPOSTOR_H

#include <QString>
#include <QVector>

// A tree's impostor (task world vegetation-objects, tree-gen's STF draft,
// version 1): views of the whole tree captured from directions around it, in
// one atlas, drawn far away instead of its meshes. Beside the shape:
// "tree.gltf" has "tree.impostor".
struct Impostor {
    // A capture: the direction from the tree to the camera (unit, tree
    // space), its cell in the atlas (u min, v min, u max, v max; v down) and
    // the extent of the view (x min, x max, y min, y max) in the plane of its
    // right and up axes, from the pivot.
    struct Frame {
        float direction[3] = {0.0f, 0.0f, 1.0f};
        float uv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        float extent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        // The capture's right and up axes: R = normalize(up x D), U = D x R.
        void axes(float right[3], float up[3]) const;
    };
    // Texture file names, resolved as the route's textures.
    QString colorTexture, normalTexture, backlightTexture;
    float backlightStrength = 0.0f;
    float alphaCutoff = 0.5f;
    float roughness = 0.9f;
    float specular = 0.35f;
    // LodBinding: after the last mesh level (Append) or instead of it
    // (ReplaceLast).
    bool replaceLast = false;
    float pivot[3] = {0.0f, 0.0f, 0.0f};
    float bounds[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    QVector<Frame> frames;

    // The impostor beside a shape: its stem and ".impostor" (lowercase, as
    // TSRE's file name rules have names it builds).
    static QString besideShape(const QString &shapePath);
    // Reads a version 1 file; false with a message when it is not one or
    // its values are invalid.
    static bool read(const QString &path, Impostor &out, QString &error);
    // The frame captured nearest to this direction (from the tree to the
    // camera, tree space); the first of equals.
    int frameFor(const float direction[3]) const;
};

#endif
