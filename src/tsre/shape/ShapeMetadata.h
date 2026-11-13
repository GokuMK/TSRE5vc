/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SHAPEMETADATA_H
#define SHAPEMETADATA_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <array>
#include <vector>
#include <tsre/shape/ShapeDisplays.h>
#include <tsre/shape/ShapeEmitters.h>
#include <tsre/shape/ShapeLights.h>
#include <tsre/shape/ShapeLod.h>
#include <tsre/shape/ShapeSeats.h>

namespace SFileDetail {
struct Node;
}

// What a shape's .sd says beyond its materials: the editor's values, its
// boxes, and TSRE's blocks (displays, lights, emitters, how far its last
// level is drawn, when its lights are on). One reader for the S files'
// complex backend and MaSzyna's models (task shapes 18), whose x.sd sits
// beside x.e3d as an S file's beside x.s.
struct ShapeMetadata {
    int detail = -1;                        // ESD_Detail_Level
    int alternative = 0;                    // ESD_Alternative_Texture
    bool snapable = false;                  // ESD_Snapable
    QString soundFileName;                  // ESD_SoundFileName (level crossings' sound)
    // ESD_Bounding_Box, then ESD_Complex's boxes: x0 y0 z0 x1 y1 z1.
    std::vector<std::array<float, 6>> boxes;
    QVector<ShapeDisplays::Display> displays;
    QStringList displayProblems;
    QVector<ShapeLights::Light> lights;
    QStringList lightProblems;
    QVector<ShapeEmitters::Emitter> emitters;
    QStringList emitterProblems;
    ShapeLod::Extension extension = ShapeLod::Extension::Default;
    ShapeLights::Schedule lightSchedule = ShapeLights::Schedule::Default;
    // ESD_TSREWalk ( Floor | Solid | Ground | Ignore ): its role in walk
    // areas (task humans 04), as written; empty: its object's type decides.
    QString walk;
    // ESD_TSRESeats: a car's sitting places, aisles and passages (task
    // humans 09).
    ShapeSeats::Seats seats;
    QStringList seatProblems;

    // The values of a read .sd's root.
    static ShapeMetadata from(const SFileDetail::Node &root);
    // The .sd beside a shape's file (its name with the extension sd), read;
    // false, the defaults kept, when there is none or it cannot be read
    // (`error` says which, empty for none).
    static bool read(const QString &shapePath, ShapeMetadata &out, QString *error = nullptr);
};

#endif // SHAPEMETADATA_H
