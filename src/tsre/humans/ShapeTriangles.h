/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef HUMANS_SHAPETRIANGLES_H
#define HUMANS_SHAPETRIANGLES_H

#include <QString>
#include <memory>
#include <vector>

namespace Humans {

// A shape file's triangles for walk areas (task humans 04), read again on
// the CPU (shapes drawn keep none): its most detailed level, parts placed
// at rest, in the shape's space (as drawn with its object's matrix), nine
// floats a triangle. MSTS shapes as the 3D cab's picker reads them
// (SFileComplex), glTF files with GltfShape::readTriangles; MaSzyna's e3d
// models not yet (none). Its own walk role: the .sd's ESD_TSREWalk, a glTF
// scene's extras.TSRE_walk (else its .sd's), as written.
struct ShapeTriangles {
    std::vector<float> triangles;
    QString walk;
    bool read = false;
};

// Read once a file and texture folder, then kept; any thread.
std::shared_ptr<const ShapeTriangles> shapeTriangles(const QString &path, const QString &texPath);
// Forgets every file read (they are read again when asked).
void forgetShapeTriangles();

}

#endif
