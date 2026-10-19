/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/ShapeLod.h>
#include <tsre/shape/ComplexShape.h>
#include <QtGlobal>

namespace ShapeLod {

Mode mode() {
    static const Mode chosen = [] {
        if (!qEnvironmentVariableIsSet("TSRE_SHAPE_LOD"))
            return Mode::Levels;
        switch (qEnvironmentVariableIntValue("TSRE_SHAPE_LOD")) {
        case 0: return Mode::Off;
        case 2: return Mode::Cull;
        default: return Mode::Levels;
        }
    }();
    return chosen;
}

int level(const ComplexShape &shape, float distance) {
    const int count = shape.distanceLevelCount();
    if (count <= 0)
        return 0;
    // The shape's size is its bounding box's diagonal.
    const float radius = shape.getSize() * 0.5f;
    for (int i = 0; i < count; ++i)
        if (distance < shape.distanceLevelDistance(i) + radius)
            return i;
    return mode() == Mode::Cull ? -1 : count - 1;
}

bool apply(ComplexShape &shape, unsigned int stateId, float distance) {
    if (mode() == Mode::Off)
        return true;
    const int chosen = level(shape, distance);
    if (chosen < 0)
        return false;
    shape.setCurrentDistanceLevel(stateId, chosen);
    return true;
}

}
