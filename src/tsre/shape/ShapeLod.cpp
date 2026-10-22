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
#include <settings/SettingsAccess.h>
#include <settings/SettingsManager.h>
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

namespace {

const char *const LodBiasKey = "core.rendering.lodBias";
int bias = 0;
bool biasRead = false;

int readBias() {
    return std::clamp(Settings::integer(LodBiasKey), -100, 100);
}

}

int lodBias() {
    // Read once, then as the profile changes: never per object.
    if (!biasRead) {
        biasRead = true;
        bias = readBias();
        QObject::connect(&SettingsManager::instance(), &SettingsManager::runtimeSettingsChanged,
                         [](const QStringList &keys) {
            if (keys.contains(QString::fromLatin1(LodBiasKey)))
                bias = readBias();
        });
    }
    return bias;
}

int forcedLevel() {
    static const int forced = [] {
        bool ok = false;
        const int value = qEnvironmentVariable("TSRE_SHAPE_LEVEL").toInt(&ok);
        return ok && value >= 0 ? value : -1;
    }();
    return forced;
}

namespace {
// The level at this place with the detail scaled (1 for the view's).
int levelAt(const ComplexShape &shape, const Place &place, float scale) {
    const int count = shape.distanceLevelCount();
    if (count <= 0)
        return 0;
    if (forcedLevel() >= 0)
        return std::min(forcedLevel(), count - 1);
    // The LOD bias's ends: always the most detailed level, or the least.
    if (lodBias() >= 100)
        return 0;
    if (lodBias() <= -100)
        return count - 1;
    const float detail = (1.0f + lodBias() / 100.0f) * scale;
    if (shape.distanceLevelCoverage(0) > 0.0f) {
        const float covered = coverage(shape, place, Game::cameraFov) * detail;
        for (int i = 0; i < count - 1; ++i)
            if (covered > shape.distanceLevelCoverage(i))
                return i;
        return mode() == Mode::Cull && covered <= shape.distanceLevelCoverage(count - 1) ? -1 : count - 1;
    }
    // The shape's size is its bounding box's diagonal.
    const float radius = shape.getSize() * 0.5f;
    for (int i = 0; i < count; ++i)
        if (place.distance < shape.distanceLevelDistance(i) * detail + radius)
            return i;
    return mode() == Mode::Cull ? -1 : count - 1;
}
}

int level(const ComplexShape &shape, const Place &place) {
    return levelAt(shape, place, 1.0f);
}

int shadowLevel(const ComplexShape &shape, const Place &place, int viewLevel) {
    const int floor = shape.shadowFloorLevel();
    // The floor itself, an impostor past it, or levels compared on purpose
    // (TSRE_SHAPE_LEVEL): the shadow is the view's.
    if (floor < 0 || viewLevel < 0 || viewLevel >= floor || forcedLevel() >= 0)
        return -1;
    int farther = levelAt(shape, place, 1.0f / ShadowDistanceScale);
    if (farther < 0)
        farther = floor;
    const int chosen = std::clamp(farther, viewLevel, floor);
    return chosen == viewLevel ? -1 : chosen;
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
    shape.setShadowDistanceLevel(stateId, shadowLevel(shape, place, chosen));
    return true;
}

}
