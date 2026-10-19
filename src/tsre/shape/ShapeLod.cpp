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
#include <tsre/Game.h>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

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

float coverage(const ComplexShape &shape, const Place &place, float fieldOfView) {
    // Bounds: max x, min x, max y, min y, max z, min z.
    const float *b = shape.getBound();
    const float extent = std::max({b[0] - b[1], b[2] - b[3], b[4] - b[5]});
    // To the middle of the bounds; objects stand upright.
    const float height = place.height + 0.5f * (b[2] + b[3]);
    const float distance = std::sqrt(place.distance * place.distance + height * height);
    const float view = 2.0f * distance * std::tan(fieldOfView * float(M_PI) / 360.0f);
    return view > 1e-6f ? extent / view : 1e6f;
}

float coverageScale() {
    static const float scale = [] {
        bool ok = false;
        const float value = qEnvironmentVariable("TSRE_SHAPE_COVERAGE_SCALE").toFloat(&ok);
        return ok && value > 0.0f ? value : 1.0f;
    }();
    return scale;
}

int level(const ComplexShape &shape, const Place &place) {
    const int count = shape.distanceLevelCount();
    if (count <= 0)
        return 0;
    if (shape.distanceLevelCoverage(0) > 0.0f) {
        const float covered = coverage(shape, place, Game::cameraFov) / coverageScale();
        for (int i = 0; i < count - 1; ++i)
            if (covered > shape.distanceLevelCoverage(i))
                return i;
        return mode() == Mode::Cull && covered <= shape.distanceLevelCoverage(count - 1) ? -1 : count - 1;
    }
    // The shape's size is its bounding box's diagonal.
    const float radius = shape.getSize() * 0.5f;
    for (int i = 0; i < count; ++i)
        if (place.distance < shape.distanceLevelDistance(i) + radius)
            return i;
    return mode() == Mode::Cull ? -1 : count - 1;
}

bool apply(ComplexShape &shape, unsigned int stateId, const Place &place) {
    if (place.transform != nullptr) {
        // Into the shape's space: the transform's rotation, transposed.
        const float *m = place.transform;
        const float *t = place.toCamera;
        float local[3];
        for (int i = 0; i < 3; ++i)
            local[i] = m[i * 4] * t[0] + m[i * 4 + 1] * t[1] + m[i * 4 + 2] * t[2];
        const float length = std::sqrt(local[0] * local[0] + local[1] * local[1] + local[2] * local[2]);
        if (length > 1e-6f) {
            for (float &c : local)
                c /= length;
            shape.setCameraDirection(stateId, local);
        }
    }
    if (mode() == Mode::Off)
        return true;
    const int chosen = level(shape, place);
    if (chosen < 0)
        return false;
    shape.setCurrentDistanceLevel(stateId, chosen);
    return true;
}

}
