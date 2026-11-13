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

// A car's sitting places (task humans 09) and its doors (task humans 11):
// its .sd's ESD_TSRESeats block (an S file's, or the .sd beside a MaSzyna
// model, there its interior's), a glTF scene's extras.TSRE_seats. Only
// these: seats are never guessed from a model at load (the user:
// mismatches); a car without them keeps task 06's boarding. Doors written
// here are taken as they are (checked on the finder's plan): no door parts
// read at load; a car without them has its door parts read (CarDoors).
//
//   ESD_TSRESeats (
//       Aisle ( 0 1.16 -10.6 10.6 )
//       Passage ( 0 1.16 -5.4 0.7 )
//       Seat 1 ( 0.64 1.54 -9.77 180 )
//       Seat 2 ( 0.19 1.54 -9.77 180 )
//       Door 1 ( -1.36 1.14 -4.52 1.3 )
//   )
//
// In the model's space as written (an S file's: its file's, as its lights';
// mirrored in x where the car is drawn). A seat's label is its number (the
// finder's plan shows them: a wrong seat is taken out by its line; the
// user, 2026-11-13); a seat without one is still read.
namespace ShapeSeats {

struct Seat {
    // The cushion's top at the middle of the place (m).
    float position[3] = {0.0f, 0.0f, 0.0f};
    // The way the sitter faces (degrees about y; 0 faces +z, 90 +x).
    float heading = 0.0f;
    // Its number (its line's label; a glTF's: its place in the list, from
    // 1); 0 for none.
    int number = 0;
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

// A door people get in and out by (m): its outer face across at x, which
// gives its side (where the car is drawn, +x its left: an S file's DOOR_D..F,
// at -x as written; a MaSzyna model's odd doors), the floor at its threshold
// y, its middle along the car z, its width.
struct Door {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float width = 1.0f;
    // Its number (as a seat's); 0 for none.
    int number = 0;
};

struct Seats {
    QVector<Seat> seats;
    QVector<Aisle> aisles;
    QVector<Passage> passages;
    QVector<Door> doors;
    // No seats people can reach (none, or no aisle).
    bool empty() const { return seats.isEmpty() || aisles.isEmpty(); }
    // Nothing to use: no seats, no doors.
    bool none() const { return empty() && doors.isEmpty(); }
};

// The block of a parsed .sd's shape node; what was left out into `problems`
// (translated).
Seats read(const SFileDetail::Node &shape, QStringList *problems = nullptr);
// A glTF scene's extras.TSRE_seats: {"aisles": [[x, y, z0, z1]], "passages":
// [[x, y, z, width]], "seats": [[x, y, z, heading]], "doors": [[x, y, z,
// width]]}.
Seats fromExtras(const QJsonObject &extras, QStringList *problems = nullptr);
// A model's seats and doors: the .sd beside it (.s, .e3d), a glTF's default
// scene's extras; false without either. Any thread.
bool readFor(const QString &modelPath, Seats &out, QStringList *problems = nullptr);
// Where the car is drawn: an S file's mirrored in x (its root matrix),
// another model's as written.
Seats drawn(const Seats &seats, const QString &modelPath);

}

#endif // SHAPESEATS_H
