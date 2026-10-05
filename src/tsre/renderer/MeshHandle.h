/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MESHHANDLE_H
#define MESHHANDLE_H

#include <QtGlobal>

// A renderer-owned mesh (see Mesh.h). Producers keep only this handle: they
// create and update meshes from CPU data on any thread, and the renderer
// uploads them on the GL thread before drawing. The mesh lives until
// Meshes::release().
struct MeshHandle {
    quint32 index = 0;
    quint32 generation = 0;
    bool valid() const { return generation != 0; }
    bool operator==(const MeshHandle &other) const {
        return index == other.index && generation == other.generation;
    }
};

#endif // MESHHANDLE_H
