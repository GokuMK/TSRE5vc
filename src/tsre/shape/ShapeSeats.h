/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SHAPESEATS_H
#define SHAPESEATS_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace SFileDetail {
struct Node;
}

// A car's sitting places (task humans 09): its .sd's ESD_TSRESeats block
// (an S file's, or the .sd beside a MaSzyna model, there its interior's),
// a glTF scene's extras.TSRE_seats. Only these: seats are never guessed
// from a model at load (the user: mismatches); a car without them keeps
// task 06's boarding.
//
//   ESD_TSRESeats (
//       Aisle ( 0 1.16 -10.6 10.6 )
//       Passage ( 0 1.16 -5.4 0.7 )
//       Seat ( 0.64 1.54 -9.77 180 )
//   )
//
// In the model's space as written (an S file's: its file's, as its lights';
// mirrored in x where the car is drawn).
namespace ShapeSeats {

struct Seat {
    // The cushion's top at the middle of the place (m).
    float position[3] = {0.0f, 0.0f, 0.0f};
    // The way the sitter faces (degrees about y; 0 faces +z, 90 +x).
    float heading = 0.0f;
};

// A straight way along the car people walk (m): across at x, the floor at
// y, from z0 to z1.
struct Aisle {
    float x = 0.0f;
    float y = 0.0f;
    float z0 = 0.0f;
    float z1 = 0.0f;
};

// A closed door on an aisle people walk through (a partition's): where, and
// its width (m).
struct Passage {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float width = 0.7f;
};

struct Seats {
    QVector<Seat> seats;
    QVector<Aisle> aisles;
    QVector<Passage> passages;
    bool empty() const { return seats.isEmpty() || aisles.isEmpty(); }
};

// The block of a parsed .sd's shape node; what was left out into `problems`
// (translated).
Seats read(const SFileDetail::Node &shape, QStringList *problems = nullptr);
// A glTF scene's extras.TSRE_seats: {"aisles": [[x, y, z0, z1]], "passages":
// [[x, y, z, width]], "seats": [[x, y, z, heading]]}.
Seats fromExtras(const QJsonObject &extras, QStringList *problems = nullptr);
// A model's seats: the .sd beside it (.s, .e3d), a glTF's default scene's
// extras; false without them. Any thread.
bool readFor(const QString &modelPath, Seats &out, QStringList *problems = nullptr);
// Where the car is drawn: an S file's mirrored in x (its root matrix),
// another model's as written.
Seats drawn(const Seats &seats, const QString &modelPath);

}

#endif // SHAPESEATS_H
