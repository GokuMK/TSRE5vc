/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#include "Cast.h"

#include <tsre/characters/Character.h>

namespace Characters {

Cast &Cast::instance() {
    static Cast cast;
    return cast;
}

Character *Cast::add(unsigned int run) {
    if (run != ofRun)
        clear();
    ofRun = run;
    characters.push_back(std::make_unique<Character>());
    return characters.back().get();
}

void Cast::setPlayer(Character *character) {
    if (playing == character)
        return;
    if (playing != nullptr)
        playing->setControlled(false);
    playing = character;
    if (playing != nullptr)
        playing->setControlled(true);
}

void Cast::update(float seconds, float yaw, float pitch, const int *cameraTile, const float *cameraAt) {
    for (const std::unique_ptr<Character> &character : characters)
        character->update(seconds, yaw, pitch, cameraTile, cameraAt);
}

void Cast::push(RenderQueue &queue) {
    for (const std::unique_ptr<Character> &character : characters)
        character->push(queue);
}

void Cast::clear() {
    playing = nullptr;
    characters.clear();
}

}
