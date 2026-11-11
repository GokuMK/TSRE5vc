/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef CARSOUNDS_H
#define CARSOUNDS_H

#include <QString>

struct SoundValues;
struct CarSoundInput;
class CabControls;

// A car's sounds of a kind its definition makes itself, in place of its
// ENG's .sms (task maszyna 05: MaSzyna's, from its .mmd), fed by Eng every
// frame before SoundSystem's update; Eng knows nothing more of them.
class CarSounds {
public:
    struct Frame {
        // Its middle in the sound system's space (as Eng::updateSounds
        // places its .sms sources), its model's front and up there (unit).
        int tileX = 0;
        int tileZ = 0;
        float position[3] = {0.0f, 0.0f, 0.0f};
        float front[3] = {0.0f, 0.0f, -1.0f};
        float up[3] = {0.0f, 1.0f, 0.0f};
        // What every car's sounds read (speed, brake cylinder, curve...),
        // and its model's input on a simulated train (null without one).
        const SoundValues *values = nullptr;
        const CarSoundInput *model = nullptr;
        // The car (the listener's attachedTo when the camera is with it),
        // and the train it runs in (the listener's train when aboard).
        const void *car = nullptr;
        const void *train = nullptr;
        // It plays its cab's sounds (the driven or leading car).
        bool lead = false;
        float seconds = 0.0f;
    };

    virtual ~CarSounds() = default;
    virtual void set(const Frame &frame) = 0;
    // An event: Open Rails' number (the host's doors), or a name (a
    // script's send("sound", "relay")).
    virtual void event(int trigger) { (void)trigger; }
    virtual void event(const QString &name) { (void)name; }
    // The route's track sounds (TrackSound) play for this car too.
    virtual bool trackSounds() const { return true; }
    // A control of the ridden cab (`controls`, number `index`) shows a new
    // value (its first one too), its part at `place` in the vehicle's model
    // space (null unknown): its click (task maszyna 05).
    virtual void control(const CabControls *controls, int index, double value, const float *place) {
        (void)controls;
        (void)index;
        (void)value;
        (void)place;
    }
};

#endif
