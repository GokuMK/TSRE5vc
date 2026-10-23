/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef CARANIMATION_H
#define CARANIMATION_H

#include <QString>
#include <vector>

class ComplexShape;

// A car's moving parts (task shapes 12), found by Open Rails' rules
// (MSTSWagonViewer): wheels turned by the distance it goes, bogies turned
// to the track, running gear (steam rods, keyed wheels) played by its
// driving wheels' turn, and two-state parts (doors, pantographs, mirrors,
// windows) moving at one key frame a second between frame 0 (closed, down)
// and their last key frame (open, up). Its shape's parts in a shape state
// of the car's own; only parts away from their rest pose are posed, so a
// standing car on straight track draws as before.
class CarAnimation {
public:
    // What its parts are, from its loaded shape. `engine`: a locomotive
    // (its wheels at the driving wheels' radius, running gear a revolution
    // to its last key frame); `steam`: its non-driven wheels at
    // `wheelRadius`. Radii in metres (0: Open Rails' defaults).
    CarAnimation(const ComplexShape &shape, bool engine, bool steam, float wheelRadius, float driverWheelRadius);

    // The shape z (metres, + toward the shape's front) of the points that
    // carry the body (bogie pivots, axles directly under it), at least two
    // or none (it rests on its ends then).
    const std::vector<float> &carriers() const { return carrierZ; }
    // Its bogies: the shape part and its pivot (shape z).
    struct Bogie {
        int part = -1;
        float z = 0.0f;
    };
    const std::vector<Bogie> &bogies() const { return bogieParts; }

    // What it shows, set by its consist (the simulation) or a viewer.
    enum Group { DoorLeft, DoorRight, Pantograph1, Pantograph2, Pantograph3, Pantograph4, Mirrors, Wipers,
                 Windows, GroupCount };
    // `now`: there at once rather than a key frame a second.
    void set(Group group, bool on, bool now = false);
    // The simulator's pantograph n (1 or 2) up or down: for an unflipped car
    // with both, the shape's pantographs 1 and 2 swapped (Open Rails'
    // MSTSWagonViewer).
    void setPantograph(int n, bool up, bool flipped);
    bool isSet(Group group) const { return wanted[group]; }
    bool has(Group group) const;

    // On by `metres` in its own frame (+ toward its shape's front), `dt`
    // seconds after the last update: wheels and running gear turned, the
    // two-state parts moved toward what is set (at once the first time).
    void update(float metres, float dt);
    // Its pose into the shape state: `bogieTurns` the transform of each
    // bogie relative to its parent (16 floats each, as bogies(); NaN as the
    // last leaves that bogie at rest), null to leave them all at rest.
    // Nothing done while its pose is what was put there last (`state` the
    // same).
    void pose(ComplexShape &shape, unsigned int state, const std::vector<float> *bogieTurns);

    // Running gear, wheels and two-state groups found (tests, the viewer).
    int wheelCount() const { return int(wheels.size()); }
    int runningGearCount() const { return int(gear.parts.size()); }
    int groupParts(Group group) const { return int(groups[group].parts.size()); }
    // One line of what it found (TSRE_ANIMATION_LOG).
    QString summary() const;

private:
    struct Wheel {
        int part = -1;
        float radius = 0.5f;
    };
    // Parts played at one frame: a two-state group or the running gear.
    struct Played {
        std::vector<int> parts;
        float lastFrame = 0.0f;
        float frame = 0.0f;
        // Frame 0 is not its rest pose: posed even there.
        bool restDiffers = false;
    };
    std::vector<Wheel> wheels;
    std::vector<Bogie> bogieParts;
    std::vector<float> carrierZ;
    Played gear;
    Played groups[GroupCount];
    bool wanted[GroupCount] = {};
    bool started = false;
    bool engine = false;
    // Running gear: frames per revolution of the driving wheels, their radius.
    float gearFramesPerTurn = 0.0f;
    // Wipers: frames a second while on.
    float wiperRate = 1.0f;
    float driverRadius = 0.762f;
    // Metres gone so far (wheels' turn).
    double travelled = 0.0;
    // What was last put into a shape state (pose), to leave it as it is.
    std::vector<float> posedSignature, signature;
    unsigned int posedState = ~0U;
};

#endif
