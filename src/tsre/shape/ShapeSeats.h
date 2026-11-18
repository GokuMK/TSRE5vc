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
#include <utility>
#include <vector>

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
// y, from z0 to z1. A car may have several (task humans 12): pieces of
// their own x and floor height, joined where they meet (joins()).
struct Aisle {
    float x = 0.0f;
    float y = 0.0f;
    float z0 = 0.0f;
    float z1 = 0.0f;
    // Its number (as a seat's: its line's label, a glTF's place in the
    // list from 1); 0 for none.
    int number = 0;
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

// Where two aisles meet (task humans 12; found, never written: the user,
// 2026-11-17): people leave aisle `a` at za and walk straight onto aisle
// `b` at zb. Overlapping aisles: both at the overlap's middle (crossing
// the car there); aisles a little apart: each at its end facing the other
// (the leg over the gap, rising or falling at a level change: the stairs).
struct Join {
    int a = -1;
    int b = -1;
    float za = 0.0f;
    float zb = 0.0f;
};

// Two aisles on one floor join where their ranges overlap or leave at most
// this between them (m).
constexpr float JoinGap = 0.3f;
// Floors this far apart in height are one (m); more is a level change.
constexpr float LevelStep = 0.08f;
// A level change of more is not a join (a deck above, m).
constexpr float JoinRise = 1.2f;
// A level change's stairs: their run along the car may be up to this
// times their rise, beyond JoinGap.
constexpr float StairsRun = 2.0f;

struct Seats {
    QVector<Seat> seats;
    QVector<Aisle> aisles;
    QVector<Passage> passages;
    QVector<Door> doors;
    // Found by connect(): the aisles' joins and each seat's aisle (an index
    // into aisles, -1 for none).
    std::vector<Join> joins;
    std::vector<int> seatAisles;
    // No seats people can reach (none, or no aisle).
    bool empty() const { return seats.isEmpty() || aisles.isEmpty(); }
    // Nothing to use: no seats, no doors.
    bool none() const { return empty() && doors.isEmpty(); }
};

// The joins of a car's aisles: every pair on one floor whose ranges overlap
// or nearly meet (JoinGap), every pair at a level change of up to JoinRise
// whose ranges overlap or leave room for its stairs.
std::vector<Join> joins(const QVector<Aisle> &aisles);
// The aisle a place belongs to: among those whose range holds its z (with
// `reach` to spare), the one whose floor is nearest `floor` in height,
// then the nearest in x; else the nearest along the car. -1 without
// aisles.
int aisleFor(const QVector<Aisle> &aisles, float x, float floor, float z, float reach = 0.5f);
// A seat's aisle (its floor about a cushion's height below it).
int aisleOfSeat(const QVector<Aisle> &aisles, const Seat &seat);
// The joins and each seat's aisle found (after reading, merging, drawn()).
void connect(Seats &seats);

// A way along aisles: from a place on aisle `from` (at z fromZ) to one on
// `to` (at toZ), over their joins, the shortest. Its points (x y z each,
// on the aisles' lines, the first and last included) and its length (m,
// along the car and across it); false where the aisles do not connect.
struct Way {
    std::vector<float> points;
    float length = 0.0f;
};
bool route(const QVector<Aisle> &aisles, const std::vector<Join> &joins, int from, float fromZ, int to, float toZ,
           Way *way = nullptr);
// The first `length` metres of a way (all of it when shorter).
Way wayStart(const Way &way, float length);
// A way without its last `length` metres (its first point when shorter).
Way wayShort(const Way &way, float length);
// The free seats (`taken` -1, all when null) a place on an aisle reaches,
// the nearest first by the way's length: along the aisles to the seat's
// row on its own aisle, then across to it. Seats on aisles the place's
// does not connect to are left out. Needs connect().
std::vector<std::pair<float, int>> seatsByWay(const Seats &seats, int fromAisle, float fromZ,
                                              const std::vector<int> *taken = nullptr);

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
