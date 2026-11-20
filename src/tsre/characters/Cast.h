/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#ifndef CHARACTERS_CAST_H
#define CHARACTERS_CAST_H

#include <memory>
#include <vector>

class RenderQueue;

namespace Characters {

class Character;

// The run's player characters (task characters 02 step 5), on the main
// thread: made by Walk, kept for the run (Fly leaves one where it stands,
// Walk goes back to it), let go of when the run ends (stopped, reset,
// another started, the route left). One for now; task 04 makes this the
// renderer's half of the simulation's characters (switching between them).
// The player's character is the one the camera rides (walking); none while
// flying.
class Cast {
public:
    static Cast &instance();

    int count() const { return int(characters.size()); }
    Character *at(int index) const { return characters[size_t(index)].get(); }
    // The run they are of (SimulationController::shownRun()).
    unsigned int run() const { return ofRun; }
    // A new character of a run (the others let go of when of another).
    Character *add(unsigned int run);
    // The player's: walking (its keys and the camera's look), or none.
    Character *player() const { return playing; }
    void setPlayer(Character *character);
    // Each frame before the camera: every character (the player's as the
    // camera looks; one left, as it stands), drawn in the camera's tile's
    // space.
    void update(float seconds, float yaw, float pitch, const int *cameraTile, const float *cameraAt);
    void push(RenderQueue &queue);
    // All let go of (their shapes' states: before the shapes go).
    void clear();

private:
    std::vector<std::unique_ptr<Character>> characters;
    Character *playing = nullptr;
    unsigned int ofRun = 0;
};

}

#endif
