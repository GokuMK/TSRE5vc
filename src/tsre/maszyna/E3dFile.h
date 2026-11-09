/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef E3DFILE_H
#define E3DFILE_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

// A MaSzyna model (.e3d, task maszyna 01): one vertex buffer and one index
// buffer for the whole model, and its parts as ranges of them, as the file
// has it. Immutable once read, and shared by everything using the file.
struct E3dModel {
    enum class Kind : unsigned char { Mesh, Pivot, Spot, Other };
    // The passes a part draws in (the file's flags). BySlot: both, cut by
    // alpha, when a texture of the skin slots in its slotMask has an alpha
    // channel, and not at all when none has. None set: as its material's
    // opacity says.
    enum Pass : unsigned char { Opaque = 1, Blended = 2, BySlot = 4 };

    struct Part {
        int parent = -1;            // always lower than this part's index
        int firstChild = -1, nextSibling = -1;
        int name = -1;              // into names, -1 none
        Kind kind = Kind::Other;
        float rest[16];             // relative to the parent, column major
        float world[16];            // at rest, through the parents
        unsigned firstIndex = 0, indexCount = 0; // indices relative to firstVertex
        unsigned firstVertex = 0, vertexCount = 0;
        int material = 0;           // into materialNames; 0 colour; -1..-4 slot
        unsigned char passes = 0;
        unsigned char slotMask = 0; // bit 0 is skin slot 1
        float rangeMin = 0.0f, rangeMax = 0.0f; // metres, min <= d < max
        bool startsHidden = false;  // "_on", "Light_On"
        float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        // The light level below which it glows: 2 always, -1 never.
        float lightThreshold = -1.0f;
        // Sphere around its vertices, in its own space.
        float boundCenter[3] = {0.0f, 0.0f, 0.0f};
        float boundRadius = -1.0f;
    };

    std::vector<Part> parts;        // parents first
    // Handed to the renderer by the first shape drawing the model, which
    // leaves these empty: only the main thread touches them then.
    mutable std::vector<float> vertices; // RenderItem::PBR layout
    mutable QByteArray indices;     // indexBytes each, 2 or 4
    // What the renderer made of them (E3dShape), freed with the model.
    mutable std::shared_ptr<void> drawn;
    int indexBytes = 2;
    QStringList names, materialNames;
    QHash<QString, int> byName;     // lower case, first in MaSzyna's search order
    float bound[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; // max x, min x, max y, min y, max z, min z
    float radius = 0.0f;            // half the box's diagonal
    std::vector<float> levelLimits; // sorted distance limits, metres
    std::vector<std::vector<int>> levelParts; // a level's parts with geometry

    // What the file held, for the corpus suite.
    int fileVertexVariant = -1;     // the digit of VNTn
    int fileIndexBytes = 0;         // the digit of IDXn, 0 without indices
    int fileKinds[4] = {0, 0, 0, 0}; // records of each Kind, reachable or not
    int unreachableParts = 0;

    static constexpr int VertexFloats = 19;
    int vertexCount() const { return int(vertices.size() / VertexFloats); }
};

namespace E3dFile {

// The model in these bytes, or null with the reason in `error`.
std::shared_ptr<E3dModel> parse(const QByteArray &bytes, QString *error);
// The file read once, shared by path and modification time while anything
// holds it; `fresh` goes past what is shared (a reload).
std::shared_ptr<const E3dModel> read(const QString &path, QString *error, bool fresh = false);

}

#endif // E3DFILE_H
