/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef HUMANS_CARDOORS_H
#define HUMANS_CARDOORS_H

#include <QString>
#include <memory>
#include <vector>

namespace Humans {

// Where a car's doors are (task humans 06): its shape's door parts by Open
// Rails' names (DOOR_A..DOOR_F), each triangle on the side of the car it is
// on (+x its left, DoorLeft: a part may hold both sides' leaves, EN57-787's;
// task humans 11), or a MaSzyna model's (.e3d) by its .mmd's door prefix
// and number (odd one side, even the other, as CarAnimation rigs them);
// their triangles read again on the CPU at rest (as the walk areas read
// shapes) and split along the car where they leave a gap: one part may hold
// every door of its side. In the shape's space (metres, +z its front).
struct CarDoors {
    // A door part's triangles nearer the car's middle than this are not a
    // door's (a roof's hatch under a DOOR_ name joined EN57-787's door into
    // one piece too wide), and a door whose outer face is nearer it than
    // SideAtLeast is an inner one (a saloon's sliding doors, CD Bdmpee's at
    // 1.10 m; OR_PSK_NEW's real doors all at 1.26 m or more). Metres.
    static constexpr float OffMiddle = 1.0f;
    static constexpr float SideAtLeast = 1.2f;
    struct Door {
        // CarAnimation::DoorLeft or DoorRight: opened with that group.
        int group = 0;
        // Its middle along the car, its width, its outer face across (its
        // side's sign) and its threshold's height: the floor inside, as
        // people take it (the user: the train's floor at the door's height).
        float z = 0.0f;
        float width = 0.0f;
        float x = 0.0f;
        float bottom = 0.0f;
    };
    // The shape was read (a shape without door parts: none).
    bool read = false;
    std::vector<Door> doors;
    // What was found, for the people's log: each door part's name, points
    // and extent along the car.
    QString note;

    // One side's door parts as triangles (x, y, z each point), split into
    // doors where along the car their triangles leave more than `gap`
    // metres between them (a leaf is one triangle's span, its corners lie
    // apart); doors narrower than 0.4 m or wider than 4.5 m are dropped.
    static std::vector<Door> split(const std::vector<float> &points, int group, float gap = 0.35f);
    // An S file's door parts' triangles (both sides') as doors: each
    // triangle to the side it is on (+x DoorLeft), those nearer the middle
    // than OffMiddle left out, each side split, inner doors (SideAtLeast)
    // dropped.
    static std::vector<Door> splitSides(const std::vector<float> &points, float gap = 0.35f);
    // A file's doors read on this thread (carDoors reads them on a worker).
    static std::shared_ptr<const CarDoors> readNow(const QString &path, const QString &texPath,
                                                   const QString &doorPrefix = QString(), int doorCount = 0);
};

// A car shape's doors, read once a file on a worker: null until read
// (asked again later). MSTS shapes and MaSzyna models (`doorPrefix` and
// `doorCount` from its .mmd: animdoorprefix, the animations' door count, 0
// for MaSzyna's 8); other files have none.
std::shared_ptr<const CarDoors> carDoors(const QString &path, const QString &texPath,
                                         const QString &doorPrefix = QString(), int doorCount = 0);
// Every read file forgotten (a route closed).
void forgetCarDoors();

}

#endif
