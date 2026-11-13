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
struct MaszynaVehicle;

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
    // A MaSzyna vehicle's (task maszyna 02): parts found by the names its
    // .mmd gives, their motion computed, no key frames.
    CarAnimation(const ComplexShape &shape, const MaszynaVehicle &vehicle);

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

    // What it shows, set by its consist (the simulation) or a viewer; its
    // four windows as Open Rails numbers them, on its own sides (task driving
    // 08).
    enum Group { DoorLeft, DoorRight, Pantograph1, Pantograph2, Pantograph3, Pantograph4, Mirrors, Wipers,
                 WindowLeftFront, WindowRightFront, WindowLeftRear, WindowRightRear, GroupCount };
    // `now`: there at once rather than a key frame a second.
    void set(Group group, bool on, bool now = false);
    // The simulator's pantograph n (1 or 2) up or down: for an unflipped car
    // with both, the shape's pantographs 1 and 2 swapped (Open Rails'
    // MSTSWagonViewer).
    void setPantograph(int n, bool up, bool flipped);
    bool isSet(Group group) const { return wanted[group]; }
    bool has(Group group) const;
    // How far a two-state group is on its way, 0 (at rest) to 1 (on), and
    // where its first part is in the shape's space (x right, y up, z
    // toward its front; its rest pose): a window's for its sound (task
    // sound 03). Without parts, 0 or 1 as set, and no place.
    float progress(Group group) const;
    bool place(Group group, float *out) const;

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
    // The shape state's pose was cleared (the Shape Viewer clears its shape's
    // every frame): the next pose sets every part again.
    void forgetPose() { posedState = ~0U; }

    // A two-state part's frame `dt` seconds on (Open Rails' UpdateState): a
    // key frame a second toward its last (`on`) or frame 0.
    static float move(float frame, bool on, float dt, float lastFrame);
    // A wiper's frame `dt` seconds on (Open Rails' AnimatedPart.UpdateLoop):
    // sweeping at `rate` frames a second while on, wrapping at `lastFrame`;
    // off, its sweep finished, then at rest (frame 0). A 3D cab's outside
    // wipers sweep by it too (task driving 08).
    static float sweep(float frame, bool on, float dt, float rate, float lastFrame);

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
        // Its own x is the shape's -x: turned the other way.
        float sign = 1.0f;
    };
    // Parts played at one frame: a two-state group or the running gear.
    struct Played {
        std::vector<int> parts;
        float lastFrame = 0.0f;
        float frame = 0.0f;
        // Frame 0 is not its rest pose: posed even there.
        bool restDiffers = false;
        // Frames a second.
        float rate = 1.0f;
    };
    // A rig's part: rest x motion at its group's progress (0 to 1).
    struct RigPart {
        int part = -1;
        float rest[16];
        float axis[3] = {0.0f, 0.0f, 0.0f};
        float degrees = 0.0f;
        float shift[3] = {0.0f, 0.0f, 0.0f};
        // A plug door: its sliding out, then along.
        float plugOut = 0.0f;
        int pantograph = -1, element = -1;
    };
    struct RigPantograph {
        float lower = 0.0f, upper = 0.0f, horizontal = 0.0f;
        float lowerRest = 0.0f, upperRest = 0.0f, lowerUp = 0.0f;
        float lower2Ratio = 1.0f, upper2Ratio = 1.0f;
    };
    bool rigged = false;
    std::vector<RigPart> rig[GroupCount];
    float groupPlace[GroupCount][3] = {};
    bool groupPlaced[GroupCount] = {};
    std::vector<RigPantograph> rigPantographs;
    // Each bogie's parent's rotation in the shape's space (16 a bogie).
    std::vector<float> bogieParents;
    void addRig(Group group, const RigPart &part, float rate);
    void poseRig(ComplexShape &shape, unsigned int state) const;
    void rigBogie(const ComplexShape &shape, size_t bogie, const float *turn, float *out) const;
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
