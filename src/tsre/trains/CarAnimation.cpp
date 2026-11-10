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
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/shape/ComplexShape.h>
#include <QHash>

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
        if (name.startsWith("LEFTWINDOWFRONT")) {
            group(groups[WindowLeftFront]);
            return;
        }
        if (name.startsWith("RIGHTWINDOWFRONT")) {
            group(groups[WindowRightFront]);
            return;
        }
        if (name.startsWith("LEFTWINDOWREAR")) {
            group(groups[WindowLeftRear]);
            return;
        }
        if (name.startsWith("RIGHTWINDOWREAR")) {
            group(groups[WindowRightRear]);
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

// MaSzyna's vehicles (task maszyna 02): what MaSzyna's own loader finds, by
// the .mmd's names, moved as it moves them; the formulas are from the
// behaviour in the review (docs/tasks/maszyna/reports).
namespace {

// The default counts of animated parts when the .mmd gives none.
const int DefaultWheels = 8;
const int DefaultDoors = 8;
const int DefaultPantographs = 2;
// How far a raised pantograph stands above its pivot, with no wire to stop
// it (metres).
const float PantographRaised = 1.5f;
// Open Rails' kind of second to open: MaSzyna's doors and pantographs are
// slower than a key frame.
const float DoorRate = 0.4f;
const float PantographRate = 0.25f;

// The pantograph's upper arm for a lower arm's angle, as its linkage holds it.
float upperAngle(float lower, float upperLength, float lowerLength, float horizontal) {
    const float c = upperLength > 0.0f ? (lowerLength * std::cos(lower) + horizontal) / upperLength : 1.0f;
    return std::acos(std::clamp(c, -1.0f, 1.0f));
}

}

void CarAnimation::addRig(Group group, const RigPart &part, float rate) {
    rig[group].push_back(part);
    groups[group].parts.push_back(part.part);
    groups[group].lastFrame = 1.0f;
    groups[group].rate = rate;
}

CarAnimation::CarAnimation(const ComplexShape &shape, const MaszynaVehicle &vehicle) : engine(false), rigged(true) {
    const int count = shape.partCount();
    QHash<QString, int> byName;
    std::vector<std::vector<int>> children(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const QString name = shape.partName(i).toLower();
        if (!name.isEmpty() && !byName.contains(name))
            byName.insert(name, i);
        const int up = shape.partParent(i);
        if (up >= 0)
            children[size_t(up)].push_back(i);
    }
    const auto find = [&byName](const QString &name) { return byName.value(name.toLower(), -1); };
    const auto prefix = [&vehicle](const char *key) { return vehicle.prefixes.value(QString::fromLatin1(key)); };
    const auto countOf = [&vehicle](int kind, int otherwise) {
        return vehicle.animations[kind] >= 0 ? vehicle.animations[kind] : otherwise;
    };
    const auto rigPart = [&shape](int part) {
        RigPart r;
        r.part = part;
        shape.partRest(part, r.rest);
        return r;
    };

    const float radius = vehicle.wheelDiameter > 0.0f ? vehicle.wheelDiameter * 0.5f : DriverWheelRadius;
    driverRadius = radius;
    const QString wheelPrefix = prefix("animwheelprefix");
    if (!wheelPrefix.isEmpty())
        for (int i = 1; i <= countOf(0, DefaultWheels); ++i) {
            const int part = find(wheelPrefix + QString::number(i));
            if (part < 0)
                continue;
            float world[16];
            shape.partMatrix(0, part, world);
            wheels.push_back({part, radius, world[0] >= 0.0f ? 1.0f : -1.0f});
        }

    const char *const bogieNames[2][2] = {{"bogie1", "boogie01"}, {"bogie2", "boogie02"}};
    for (const auto &names : bogieNames) {
        int part = find(QString::fromLatin1(names[0]));
        if (part < 0)
            part = find(QString::fromLatin1(names[1]));
        if (part < 0)
            continue;
        float world[16], parent[16];
        shape.partMatrix(0, part, world);
        bogieParts.push_back({part, world[14]});
        carrierZ.push_back(world[14]);
        Mat4::identity(parent);
        const int up = shape.partParent(part);
        if (up >= 0)
            shape.partMatrix(0, up, parent);
        parent[12] = parent[13] = parent[14] = 0.0f;
        bogieParents.insert(bogieParents.end(), parent, parent + 16);
    }
    if (carrierZ.empty())
        for (float offset : vehicle.bogieOffsets)
            carrierZ.push_back(offset);
    std::sort(carrierZ.begin(), carrierZ.end());
    if (carrierZ.size() < 2)
        carrierZ.clear();

    float lower2 = 1.0f;
    if (vehicle.pantType.compare(QLatin1String("WBL85"), Qt::CaseInsensitive) == 0)
        lower2 = 1.2f;
    else if (vehicle.pantType.startsWith(QLatin1String("EC160"), Qt::CaseInsensitive))
        lower2 = 0.85f;
    else if (vehicle.pantType.startsWith(QLatin1String("DSA"), Qt::CaseInsensitive))
        lower2 = 1.025f;
    const QString armKeys[5] = {prefix("animpantrd1prefix"), prefix("animpantrd2prefix"), prefix("animpantrg1prefix"),
                                prefix("animpantrg2prefix"), prefix("animpantslprefix")};
    static const float armAxis[5] = {-1.0f, 1.0f, 1.0f, -1.0f, -1.0f};
    for (int i = 1; i <= countOf(5, DefaultPantographs) && i <= 4 && !armKeys[0].isEmpty(); ++i) {
        const int base = find(armKeys[0] + QString::number(i));
        if (base < 0)
            continue;
        RigPantograph pant;
        pant.lower2Ratio = lower2;
        // Its arms' lengths and angles from where its parts stand.
        if (!children[size_t(base)].empty()) {
            float m[16];
            const int joint = children[size_t(base)][0];
            shape.partRest(joint, m);
            pant.horizontal = -std::fabs(m[13]);
            pant.lower = std::hypot(m[13], m[14]);
            pant.lowerRest = std::atan2(std::fabs(m[14]), std::fabs(m[13]));
            if (!children[size_t(joint)].empty()) {
                shape.partRest(children[size_t(joint)][0], m);
                pant.horizontal += std::fabs(m[13]);
                pant.upper = std::hypot(m[13], m[14]);
                pant.upperRest = std::atan2(std::fabs(m[14]), std::fabs(m[13]));
            }
        }
        pant.lowerUp = pant.lowerRest;
        while (pant.lowerUp < float(M_PI_2) && pant.lower > 0.0f && pant.upper > 0.0f) {
            const float height = pant.lower * std::sin(pant.lowerUp)
                    + pant.upper * std::sin(upperAngle(pant.lowerUp, pant.upper, pant.lower, pant.horizontal));
            if (height >= PantographRaised)
                break;
            pant.lowerUp += 0.002f;
        }
        const int index = int(rigPantographs.size());
        rigPantographs.push_back(pant);
        for (int arm = 0; arm < 5; ++arm) {
            const int part = armKeys[arm].isEmpty() ? -1 : find(armKeys[arm] + QString::number(i));
            if (part < 0)
                continue;
            RigPart r = rigPart(part);
            r.axis[0] = armAxis[arm];
            r.pantograph = index;
            r.element = arm;
            addRig(Group(Pantograph1 + i - 1), r, PantographRate);
        }
    }

    const QString doorPrefix = prefix("animdoorprefix");
    const QString method = vehicle.doorOpenMethod.toLower();
    const float doorRange = vehicle.doorShiftLeft > 0.0f ? vehicle.doorShiftLeft : vehicle.doorShiftRight;
    for (int i = 1; i <= countOf(1, DefaultDoors) && !doorPrefix.isEmpty(); ++i) {
        const int part = find(doorPrefix + QString::number(i));
        if (part < 0)
            continue;
        const Group side = (i & 1) == 0 ? DoorRight : DoorLeft;
        RigPart r = rigPart(part);
        if (method == QLatin1String("rotate")) {
            r.axis[0] = 1.0f;
            r.degrees = doorRange;
            addRig(side, r, DoorRate);
        } else if (method == QLatin1String("fold")) {
            r.axis[2] = 1.0f;
            r.degrees = doorRange;
            addRig(side, r, DoorRate);
            if (!children[size_t(part)].empty()) {
                const int wing = children[size_t(part)][0];
                RigPart big = rigPart(wing);
                big.axis[2] = 1.0f;
                big.degrees = -2.0f * doorRange;
                addRig(side, big, DoorRate);
                if (!children[size_t(wing)].empty()) {
                    RigPart foot = rigPart(children[size_t(wing)][0]);
                    foot.axis[1] = 1.0f;
                    foot.degrees = doorRange;
                    addRig(side, foot, DoorRate);
                }
            }
        } else if (method == QLatin1String("plug")) {
            r.plugOut = vehicle.doorPlugShift;
            r.shift[2] = doorRange;
            addRig(side, r, DoorRate);
        } else {
            r.shift[2] = doorRange;
            addRig(side, r, DoorRate);
        }
    }

    const QString stepPrefix = prefix("animstepprefix");
    const bool stepShifts = vehicle.stepOpenMethod.compare(QLatin1String("Shift"), Qt::CaseInsensitive) == 0;
    for (int i = 1; i <= countOf(7, 0) && !stepPrefix.isEmpty() && vehicle.stepShift != 0.0f; ++i) {
        const int part = find(stepPrefix + QString::number(i));
        if (part < 0)
            continue;
        RigPart r = rigPart(part);
        if (stepShifts) {
            r.shift[0] = vehicle.stepShift;
        } else {
            r.axis[1] = 1.0f;
            r.degrees = vehicle.stepShift;
        }
        addRig((i & 1) == 0 ? DoorRight : DoorLeft, r, DoorRate);
    }

    const QString mirrorPrefix = prefix("animmirrorprefix");
    for (int i = 1; i <= countOf(8, 0) && !mirrorPrefix.isEmpty(); ++i) {
        const int part = find(mirrorPrefix + QString::number(i));
        if (part < 0)
            continue;
        RigPart r = rigPart(part);
        r.axis[1] = 1.0f;
        r.degrees = vehicle.mirrorShift > 0.0f ? vehicle.mirrorShift : 90.0f;
        addRig(Mirrors, r, 1.0f);
    }

    const QString wiperPrefix = prefix("animwiperprefix");
    const float wiperAngle = vehicle.wiperAngle > 0.0f ? vehicle.wiperAngle : 45.0f;
    for (int i = 1; i <= countOf(9, 0) && !wiperPrefix.isEmpty(); ++i) {
        const float angle = (i % 2) == 0 ? -wiperAngle : wiperAngle;
        const char *const arms[3] = {"_p1", "_p2", "_p3"};
        for (int arm = 0; arm < 3; ++arm) {
            const int part = find(wiperPrefix + QString::number(i) + QLatin1String(arms[arm]));
            if (part < 0)
                continue;
            RigPart r = rigPart(part);
            r.axis[1] = 1.0f;
            r.degrees = arm == 2 ? -angle : angle;
            addRig(Wipers, r, 1.0f);
        }
    }
    wiperRate = 0.5f;
}

void CarAnimation::poseRig(ComplexShape &shape, unsigned int state) const {
    for (int g = 0; g < GroupCount; ++g) {
        const Played &played = groups[g];
        if (played.frame == 0.0f)
            continue;
        // A wiper goes over and back in its sweep.
        const float t = g == Wipers ? 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * played.frame) : played.frame;
        for (const RigPart &part : rig[g]) {
            float local[16], moved[16];
            std::copy(part.rest, part.rest + 16, local);
            float degrees = part.degrees * t;
            if (part.pantograph >= 0) {
                const RigPantograph &p = rigPantographs[size_t(part.pantograph)];
                const float lower = p.lowerRest + t * (p.lowerUp - p.lowerRest);
                const float a = lower - p.lowerRest;
                const float b = upperAngle(lower, p.upper, p.lower, p.horizontal) - p.upperRest;
                const float c = a + b;
                const float angles[5] = {a, a * p.lower2Ratio, c, c * p.upper2Ratio, b};
                degrees = angles[part.element] * 180.0f / float(M_PI);
            }
            if (part.plugOut > 0.0f) {
                const float along = part.shift[2] * t;
                Mat4::translate(moved, local, std::min(along * 2.0f, part.plugOut), 0.0f, std::max(0.0f, along - part.plugOut * 0.5f));
                std::copy(moved, moved + 16, local);
            } else if (part.shift[0] != 0.0f || part.shift[1] != 0.0f || part.shift[2] != 0.0f) {
                Mat4::translate(moved, local, part.shift[0] * t, part.shift[1] * t, part.shift[2] * t);
                std::copy(moved, moved + 16, local);
            }
            if (degrees != 0.0f) {
                Mat4::rotate(moved, local, degrees * float(M_PI) / 180.0f, part.axis[0], part.axis[1], part.axis[2]);
                std::copy(moved, moved + 16, local);
            }
            shape.setPartTransform(state, part.part, local);
        }
    }
}

// A bogie's turn is worked out for the MSTS shapes' frame, Y up with the root
// mirroring x. Here the parent has a frame of its own: the same turn in the
// shape's drawing space, into the parent's.
void CarAnimation::rigBogie(const ComplexShape &shape, size_t bogie, const float *turn, float *out) const {
    float rest[16];
    shape.partRest(bogieParts[bogie].part, rest);
    const float *parent = bogieParents.data() + bogie * 16;
    float undo[16], turned[16], drawn[16], conjugated[16];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            undo[c * 4 + r] = parent[r * 4 + c];
    std::copy(turn, turn + 16, turned);
    turned[12] = turned[13] = turned[14] = 0.0f;
    // The mirror of x, on both sides.
    turned[1] = -turned[1];
    turned[2] = -turned[2];
    turned[4] = -turned[4];
    turned[8] = -turned[8];
    Mat4::multiply(drawn, undo, turned);
    Mat4::multiply(conjugated, drawn, const_cast<float *>(parent));
    float restRotation[16];
    std::copy(rest, rest + 16, restRotation);
    restRotation[12] = restRotation[13] = restRotation[14] = 0.0f;
    Mat4::multiply(out, conjugated, restRotation);
    out[12] = rest[12];
    out[13] = rest[13];
    out[14] = rest[14];
}

QString CarAnimation::summary() const {
    static const char *const names[GroupCount] = {"doors left", "doors right", "pantograph 1", "pantograph 2",
                                                  "pantograph 3", "pantograph 4", "mirrors", "wipers",
                                                  "left front window", "right front window", "left rear window",
                                                  "right rear window"};
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

float CarAnimation::move(float frame, bool on, float dt, float lastFrame) {
    const float target = on ? lastFrame : 0.0f;
    const float step = std::max(dt, 0.0f);
    return frame < target ? std::min(target, frame + step) : std::max(target, frame - step);
}

float CarAnimation::sweep(float frame, bool on, float dt, float rate, float lastFrame) {
    if (lastFrame <= 0.0f)
        return 0.0f;
    const float next = frame + std::max(dt, 0.0f) * rate;
    if (on || (frame > 0.0f && next < lastFrame))
        return std::fmod(next, lastFrame);
    return 0.0f;
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
            played.frame = sweep(played.frame, wanted[g], dt, wiperRate, played.lastFrame);
            continue;
        }
        const float target = wanted[g] ? played.lastFrame : 0.0f;
        if (!started) {
            played.frame = target;
            continue;
        }
        played.frame = move(played.frame, wanted[g], dt * played.rate, played.lastFrame);
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
        const double angle = std::remainder(travelled / wheel.radius, 2.0 * M_PI) * wheel.sign;
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
    if (rigged)
        poseRig(shape, state);
    // A turn whose last element is NaN leaves its bogie at rest.
    for (size_t b = 0; b < bogieParts.size(); ++b) {
        const bool turned = bogieTurns != nullptr && bogieTurns->size() >= bogieParts.size() * 16
                && !std::isnan((*bogieTurns)[b * 16 + 15]);
        if (rigged && turned) {
            float local[16];
            rigBogie(shape, b, bogieTurns->data() + b * 16, local);
            shape.setPartTransform(state, bogieParts[b].part, local);
            continue;
        }
        shape.setPartTransform(state, bogieParts[b].part, turned ? bogieTurns->data() + b * 16 : nullptr);
    }
}
