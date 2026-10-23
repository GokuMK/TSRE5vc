/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "CarAnimation.h"
#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/shape/ComplexShape.h>

namespace {

// Open Rails' default radii (MSTSWagon, TrainCar): 18 in, 30 in.
const float WagonWheelRadius = 0.4572f;
const float DriverWheelRadius = 0.762f;

bool sameMatrix(const float *a, const float *b) {
    for (int i = 0; i < 16; ++i)
        if (std::fabs(a[i] - b[i]) > 1e-4f)
            return false;
    return true;
}

}

CarAnimation::CarAnimation(const ComplexShape &shape, bool isEngine, bool steam, float wheelRadius,
                           float driverWheelRadius)
        : engine(isEngine) {
    const int count = shape.partCount();
    const float wagonRadius = wheelRadius > 0.0f ? wheelRadius : WagonWheelRadius;
    driverRadius = driverWheelRadius > 0.0f ? driverWheelRadius : DriverWheelRadius;
    // Diesel and electric locomotives turn every wheel at the driving
    // wheels' radius; a steam locomotive's turned wheels are its non-driven
    // ones (Open Rails' UpdateAxles).
    const float turnedRadius = engine && !steam ? driverRadius : wagonRadius;

    std::vector<std::vector<int>> children(count);
    std::vector<int> parent(count, -1);
    for (int i = 1; i < count; ++i) {
        parent[i] = shape.partParent(i);
        if (parent[i] >= 0 && parent[i] < count && parent[i] != i)
            children[parent[i]].push_back(i);
    }
    // Each part's rest transform within the shape (without its root, whose
    // translation Open Rails takes as zero).
    std::vector<std::array<float, 16>> absolute(count);
    std::vector<char> known(count, 0);
    std::function<const float *(int, int)> rest = [&](int part, int depth) -> const float * {
        if (!known[part]) {
            float local[16];
            shape.partRest(part, local);
            const int up = parent[part];
            if (up > 0 && up < count && depth < count) {
                float m[16];
                std::copy(rest(up, depth + 1), rest(up, depth + 1) + 16, m);
                Mat4::multiply(absolute[part].data(), m, local);
            } else {
                std::copy(local, local + 16, absolute[part].begin());
            }
            known[part] = 1;
        }
        return absolute[part].data();
    };
    std::function<void(int, std::vector<int> &)> subtree = [&](int part, std::vector<int> &out) {
        out.push_back(part);
        for (int c : children[part])
            subtree(c, out);
    };
    std::vector<QString> names(count);
    bool anyPanto1 = false;
    for (int i = 0; i < count; ++i) {
        names[i] = shape.partName(i).toUpper();
        if (names[i].startsWith("PANTO") && names[i].contains('1'))
            anyPanto1 = true;
    }
    static const QRegularExpression pantographName("^PANTOGRAPH(BOTTOM|MIDDLE|TOP)([12])[AB]?$");
    auto pantograph = [&](const QString &name) {
        const QRegularExpressionMatch m = pantographName.match(name);
        if (m.hasMatch())
            return m.captured(2).toInt();
        for (int n = 1; n <= 4; ++n)
            if (name.contains(QChar('0' + n)))
                return n;
        return anyPanto1 ? 1 : 2;
    };

    // Axles directly under the body and under each bogie (by its part).
    std::vector<float> bodyAxles;
    std::vector<std::vector<float>> bogieAxles;
    std::function<void(int, int)> visit = [&](int part, int bogie) {
        const QString &name = names[part];
        const float *at = rest(part, 0);
        const bool keyed = shape.partLastFrame(part) >= 0.0f;
        auto group = [&](Played &played) { subtree(part, played.parts); };
        if (name.startsWith("WHEELS") && name.size() >= 7 && name.size() <= 9 && at[13] < 3.0f) {
            if (name.size() == 7 && keyed) {
                group(gear);
                return;
            }
            wheels.push_back({part, turnedRadius});
            // Under a bogie, its axle; else the body's (Open Rails takes a
            // wheel directly under the root as the body's).
            if (bogie >= 0)
                bogieAxles[size_t(bogie)].push_back(at[14]);
            else
                bodyAxles.push_back(at[14]);
            return;
        }
        if (name.startsWith("BOGIE") && name.size() <= 6) {
            bogieParts.push_back({part, at[14]});
            bogieAxles.emplace_back();
            const int index = int(bogieParts.size()) - 1;
            for (int c : children[part])
                visit(c, index);
            return;
        }
        if (name.startsWith("WIPER")) {
            group(groups[Wipers]);
            return;
        }
        if (name.startsWith("DOOR")) {
            if (name.startsWith("DOOR_D") || name.startsWith("DOOR_E") || name.startsWith("DOOR_F"))
                group(groups[DoorLeft]);
            else if (name.startsWith("DOOR_A") || name.startsWith("DOOR_B") || name.startsWith("DOOR_C"))
                group(groups[DoorRight]);
            return;
        }
        if (name.startsWith("PANTOGRAPH")) {
            group(groups[Pantograph1 + pantograph(name) - 1]);
            return;
        }
        if (name.startsWith("MIRROR")) {
            group(groups[Mirrors]);
            return;
        }
        if (name.startsWith("LEFTWINDOWFRONT") || name.startsWith("RIGHTWINDOWFRONT")
                || name.startsWith("LEFTWINDOWREAR") || name.startsWith("RIGHTWINDOWREAR")) {
            group(groups[Windows]);
            return;
        }
        if (name.startsWith("UNLOADINGPARTS") || name.startsWith("ORTSBELL") || name.startsWith("ORTSITEM")
                || name.startsWith("ORTSBRAKECYLINDER") || name.startsWith("ORTSHANDBRAKE")
                || name.startsWith("ORTSBRAKERIGGING"))
            return;
        if (name.startsWith("PANTO")) {
            group(groups[Pantograph1 + pantograph(name) - 1]);
            return;
        }
        // Keyed and claimed by no rule: running gear.
        if (keyed) {
            group(gear);
            return;
        }
        for (int c : children[part])
            visit(c, bogie);
    };
    if (count > 0)
        for (int c : children[0])
            visit(c, -1);
    for (int i = 1; i < count; ++i)
        if (parent[i] < 0)
            visit(i, -1);

    // What carries the body: its axles and the bogies' pivots; one bogie
    // and no body axles: that bogie's axles (it stays straight).
    carrierZ = bodyAxles;
    for (const Bogie &b : bogieParts)
        carrierZ.push_back(b.z);
    if (bogieParts.size() == 1 && bodyAxles.empty() && bogieAxles[0].size() >= 2) {
        carrierZ = bogieAxles[0];
        bogieParts.clear();
    }
    std::sort(carrierZ.begin(), carrierZ.end());
    carrierZ.erase(std::unique(carrierZ.begin(), carrierZ.end(), [](float a, float b) { return std::fabs(a - b) < 0.05f; }),
                   carrierZ.end());
    if (carrierZ.size() < 2)
        carrierZ.clear();

    // Each group's last key frame (over its parts), and whether frame 0 is
    // its rest pose.
    auto finish = [&](Played &played) {
        for (int part : played.parts) {
            played.lastFrame = std::max(played.lastFrame, shape.partLastFrame(part));
            float at[16], still[16];
            if (shape.partAtFrame(part, 0.0f, at) && shape.partRest(part, still) && !sameMatrix(at, still))
                played.restDiffers = true;
        }
    };
    finish(gear);
    for (Played &g : groups)
        finish(g);
    // Running gear: a revolution its last key frame on a locomotive, on a
    // wagon the shape's frame rate * 8 / 30 frames (Open Rails).
    gearFramesPerTurn = engine ? gear.lastFrame : shape.animationRate() * 8.0f / 30.0f;
    wiperRate = shape.animationRate() * 1.5f / 30.0f;
}

QString CarAnimation::summary() const {
    static const char *const names[GroupCount] = {"doors left", "doors right", "pantograph 1", "pantograph 2",
                                                  "pantograph 3", "pantograph 4", "mirrors", "wipers", "windows"};
    QString line = QString("wheels %1, bogies %2, carriers %3, running gear %4 (to frame %5)")
            .arg(wheels.size()).arg(bogieParts.size()).arg(carrierZ.size()).arg(gear.parts.size()).arg(gear.lastFrame);
    for (int g = 0; g < GroupCount; ++g)
        if (!groups[g].parts.empty())
            line += QString(", %1 %2 (to %3)%4").arg(names[g]).arg(groups[g].parts.size()).arg(groups[g].lastFrame)
                    .arg(wanted[g] ? " set" : "");
    return line;
}

bool CarAnimation::has(Group group) const {
    return !groups[group].parts.empty() && groups[group].lastFrame > 0.0f;
}

void CarAnimation::set(Group group, bool on, bool now) {
    wanted[group] = on;
    if (now && group != Wipers)
        groups[group].frame = on ? groups[group].lastFrame : 0.0f;
}

void CarAnimation::setPantograph(int n, bool up, bool flipped) {
    if (n < 1 || n > 4)
        return;
    const bool swap = !flipped && has(Pantograph1) && has(Pantograph2);
    if (swap && n <= 2)
        n = 3 - n;
    set(Group(Pantograph1 + n - 1), up);
}

void CarAnimation::update(float metres, float dt) {
    travelled += metres;
    if (gear.lastFrame > 0.0f && gearFramesPerTurn > 0.0f) {
        const double turns = travelled / (2.0 * M_PI * driverRadius);
        gear.frame = float(std::fmod(turns * gearFramesPerTurn, double(gear.lastFrame)));
        if (gear.frame < 0.0f)
            gear.frame += gear.lastFrame;
        // Its last key frame is its first again (Open Rails wraps there).
        if (gear.frame < 1e-4f || gear.frame > gear.lastFrame - 1e-4f)
            gear.frame = 0.0f;
    }
    for (int g = 0; g < GroupCount; ++g) {
        Played &played = groups[g];
        if (played.lastFrame <= 0.0f)
            continue;
        if (g == Wipers) {
            // Looping while on (Open Rails: the shape's frame rate * 1.5 / 30
            // frames a second).
            played.frame = wanted[g] ? std::fmod(played.frame + std::max(dt, 0.0f) * wiperRate, played.lastFrame)
                                     : 0.0f;
            continue;
        }
        const float target = wanted[g] ? played.lastFrame : 0.0f;
        if (!started) {
            played.frame = target;
            continue;
        }
        const float step = std::max(dt, 0.0f);
        played.frame = played.frame < target ? std::min(target, played.frame + step)
                                             : std::max(target, played.frame - step);
    }
    started = true;
}

void CarAnimation::pose(ComplexShape &shape, unsigned int state, const std::vector<float> *bogieTurns) {
    // What decides the pose: unchanged, the state keeps it.
    signature.clear();
    signature.push_back(float(std::fmod(travelled, 1000.0)));
    signature.push_back(float(travelled / 1000.0));
    signature.push_back(gear.frame);
    for (const Played &g : groups)
        signature.push_back(g.frame);
    if (bogieTurns != nullptr)
        signature.insert(signature.end(), bogieTurns->begin(), bogieTurns->end());
    auto same = [](const std::vector<float> &a, const std::vector<float> &b) {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!(a[i] == b[i] || (std::isnan(a[i]) && std::isnan(b[i]))))
                return false;
        return true;
    };
    if (state == posedState && same(signature, posedSignature))
        return;
    posedState = state;
    std::swap(posedSignature, signature);
    // Every part it moves set, at rest where it rests (the shape counts a
    // change only where one is).
    for (const Wheel &wheel : wheels) {
        const double angle = std::remainder(travelled / wheel.radius, 2.0 * M_PI);
        if (std::fabs(angle) < 1e-6) {
            shape.setPartTransform(state, wheel.part, nullptr);
            continue;
        }
        // About its own x: + rolls its top toward the shape's front.
        float local[16], turn[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        const float c = float(std::cos(angle)), s = float(std::sin(angle));
        turn[5] = c;
        turn[6] = s;
        turn[9] = -s;
        turn[10] = c;
        if (!shape.partRest(wheel.part, local))
            continue;
        float out[16];
        Mat4::multiply(out, local, turn);
        shape.setPartTransform(state, wheel.part, out);
    }
    auto play = [&](const Played &played) {
        const bool resting = played.frame == 0.0f && !played.restDiffers;
        for (int part : played.parts)
            if (resting)
                shape.setPartTransform(state, part, nullptr);
            else
                shape.setPartFrame(state, part, played.frame);
    };
    play(gear);
    for (const Played &g : groups)
        play(g);
    // A turn whose last element is NaN leaves its bogie at rest.
    for (size_t b = 0; b < bogieParts.size(); ++b) {
        const bool turned = bogieTurns != nullptr && bogieTurns->size() >= bogieParts.size() * 16
                && !std::isnan((*bogieTurns)[b * 16 + 15]);
        shape.setPartTransform(state, bogieParts[b].part, turned ? bogieTurns->data() + b * 16 : nullptr);
    }
}
