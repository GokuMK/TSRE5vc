/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "Crowd.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <limits>

#include <tsre/math3d/GLMatrix.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/ShapeLoader.h>
#include <tsre/shape/SkeletalAnimation.h>

namespace Humans {

namespace {

constexpr unsigned int NoState = std::numeric_limits<unsigned int>::max();

// A repeatable random number in [0, 1) from a person's seed and what it is
// for.
float unitRandom(quint32 seed, quint32 salt) {
    quint32 x = seed * 0x9E3779B9u + salt * 0x85EBCA6Bu + 0x7F4A7C15u;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return float(x >> 8) * (1.0f / 16777216.0f);
}

}

struct Crowd::Kind {
    GltfShape *shape = nullptr;
    QString clipsFile;
    bool prepared = false;
    // Not a human file (no skin), or it failed to load.
    bool failed = false;
    std::vector<int> idles, walks;
    // Each clip's ground speed (m/s).
    std::vector<float> speeds;
    // Where the shape faces (radians about +y from +z): its walk's way.
    float forwardAngle = 0.0f;
    // Its largest extent and the middle of its height (the bounds' at
    // rest), for its coverage.
    float extent = 1.75f;
    float middle = 0.9f;
    // The least coverage of each level (one level 0 without levels).
    std::vector<float> coverage;
    // Each clip's shared phases: states posed once, NoState until used.
    struct Bank {
        float duration = 0.0f;
        std::vector<unsigned int> states;
    };
    std::vector<Bank> banks;
    // Own-pose states not in use, and the rest pose's state.
    std::vector<unsigned int> freeOwn;
    std::vector<unsigned int> ownInUse;
    unsigned int rest = NoState;
};

struct Crowd::Person {
    PersonSpec spec;
    bool chosen = false;
    bool walking = false;
    int clip = -1;
    // Seconds into its clip at time 0, and its playing speed.
    float offset = 0.0f;
    float rate = 1.0f;
    // On its circle: the angle at time 0 and radians a second.
    float startAngle = 0.0f;
    float angularSpeed = 0.0f;
    // As drawn now.
    float position[3] = {0.0f, 0.0f, 0.0f};
    float heading = 0.0f;
    float distance = 0.0f;
    int level = -1;
    int tier = -1;
    unsigned int own = NoState;
    unsigned int draw = NoState;
};

Crowd::Crowd() = default;

Crowd::~Crowd() {
    clear();
}

int Crowd::addKind(GltfShape *shape, const QString &clipsFile) {
    auto kind = std::make_unique<Kind>();
    kind->shape = shape;
    kind->clipsFile = clipsFile;
    kind->failed = shape == nullptr;
    kinds.push_back(std::move(kind));
    return int(kinds.size()) - 1;
}

int Crowd::addPerson(const PersonSpec &spec) {
    if (spec.kind < 0 || spec.kind >= kindCount())
        return -1;
    Person person;
    person.spec = spec;
    people.push_back(person);
    return int(people.size()) - 1;
}

int Crowd::personCount() const {
    return int(people.size());
}

void Crowd::clear() {
    for (std::unique_ptr<Kind> &kind : kinds) {
        if (kind->shape == nullptr)
            continue;
        auto release = [&](unsigned int state) {
            if (state != NoState)
                kind->shape->releaseState(state);
        };
        for (const Kind::Bank &bank : kind->banks)
            for (unsigned int state : bank.states)
                release(state);
        for (unsigned int state : kind->freeOwn)
            release(state);
        for (unsigned int state : kind->ownInUse)
            release(state);
        release(kind->rest);
    }
    people.clear();
    kinds.clear();
    clipFiles.clear();
    figures = Stats();
}

bool Crowd::prepare(Kind &kind) {
    GltfShape *shape = kind.shape;
    if (shape->hasLoadFailed()) {
        kind.failed = true;
        return false;
    }
    if (!shape->isLoaded()) {
        // As a shape drawn: a worker loads it, or this thread when it may.
        if (ShapeLoader::request(shape) == ShapeLoader::Request::LoadHere)
            shape->load();
        if (!shape->isLoaded())
            return false;
    }
    const SkeletalAnimation *rig = shape->skeleton();
    if (rig == nullptr || rig->skinCount() == 0) {
        qWarning() << "Crowd:" << shape->getPathId() << "has no skin, not a human";
        kind.failed = true;
        return false;
    }
    if (!kind.clipsFile.isEmpty()) {
        std::unique_ptr<GltfShape> &file = clipFiles[kind.clipsFile];
        if (!file) {
            const QFileInfo info(kind.clipsFile);
            file = std::make_unique<GltfShape>(kind.clipsFile, info.fileName(), info.path());
            file->load();
            if (!file->isLoaded())
                qWarning() << "Crowd: clips file" << kind.clipsFile << "did not load";
        }
        // Named after the file, as the Shape Viewer names them: a human's
        // own clips keep their names (and a name already there is skipped).
        if (file->isLoaded())
            shape->addClipsFrom(*file, QString(" (%1)").arg(QFileInfo(kind.clipsFile).completeBaseName()));
    }

    const int clips = shape->clipCount();
    kind.speeds.assign(size_t(clips), 0.0f);
    kind.banks.assign(size_t(clips), Kind::Bank());
    bool facing = false;
    for (int clip = 0; clip < clips; ++clip) {
        const SkeletalAnimation::GroundMotion motion = rig->groundMotion(clip, 0);
        kind.speeds[size_t(clip)] = motion.speed;
        if (motion.speed >= WalkSpeed) {
            kind.walks.push_back(clip);
            if (!facing) {
                facing = true;
                kind.forwardAngle = std::atan2(motion.direction[0], motion.direction[2]);
            }
        } else {
            kind.idles.push_back(clip);
        }
        Kind::Bank &bank = kind.banks[size_t(clip)];
        bank.duration = shape->clipDuration(clip);
        const int phases = std::clamp(int(std::lround(bank.duration * tuning.bankRate)), 4, 64);
        bank.states.assign(size_t(phases), NoState);
    }

    // Bounds: max x, min x, max y, min y, max z, min z.
    const float *b = shape->getBound();
    kind.extent = std::max({b[0] - b[1], b[2] - b[3], b[4] - b[5]});
    kind.middle = 0.5f * (b[2] + b[3]);
    kind.coverage.clear();
    for (int level = 0; level < shape->distanceLevelCount(); ++level)
        kind.coverage.push_back(shape->distanceLevelCoverage(level));
    if (kind.coverage.empty())
        kind.coverage.push_back(0.0f);
    kind.rest = shape->newState();
    kind.prepared = true;
    return true;
}

void Crowd::choosePlay(Person &person, const Kind &kind) {
    const quint32 seed = person.spec.seed;
    person.chosen = true;
    person.walking = person.spec.behaviour == Behaviour::Circle && !kind.walks.empty();
    const std::vector<int> &clips = person.walking ? kind.walks : kind.idles;
    if (clips.empty()) {
        person.clip = -1;
        return;
    }
    person.clip = clips[std::min(size_t(unitRandom(seed, 1) * float(clips.size())), clips.size() - 1)];
    person.offset = unitRandom(seed, 2) * kind.banks[size_t(person.clip)].duration;
    person.rate = 1.0f;
    if (person.walking) {
        const float speed = kind.speeds[size_t(person.clip)] * person.spec.scale * person.rate;
        const float radius = std::max(person.spec.radius, 0.1f);
        person.startAngle = unitRandom(seed, 3) * 2.0f * float(M_PI);
        person.angularSpeed = speed / radius * (unitRandom(seed, 4) < 0.5f ? -1.0f : 1.0f);
    }
}

unsigned int Crowd::bankState(Kind &kind, int clip, int phase) {
    Kind::Bank &bank = kind.banks[size_t(clip)];
    unsigned int &state = bank.states[size_t(phase)];
    if (state == NoState) {
        state = kind.shape->newState();
        ComplexShape::ClipPlay play;
        play.clip = clip;
        play.time = bank.duration * float(phase) / float(bank.states.size());
        kind.shape->setClips(state, &play, 1);
        kind.shape->updatePose(state);
        ++figures.bankStates;
    }
    return state;
}

unsigned int Crowd::ownState(Kind &kind) {
    unsigned int state;
    if (!kind.freeOwn.empty()) {
        state = kind.freeOwn.back();
        kind.freeOwn.pop_back();
    } else {
        state = kind.shape->newState();
    }
    kind.ownInUse.push_back(state);
    return state;
}

void Crowd::update(float seconds, const View &view) {
    QElapsedTimer timer;
    timer.start();
    clock += double(seconds);
    const int bankStates = figures.bankStates;
    const double pushMs = figures.pushMs;
    figures = Stats();
    figures.bankStates = bankStates;
    figures.pushMs = pushMs;
    figures.people = personCount();
    for (std::unique_ptr<Kind> &kind : kinds)
        if (!kind->prepared && !kind->failed)
            prepare(*kind);

    const float viewScale = 2.0f * std::tan(view.fieldOfView * float(M_PI) / 360.0f);
    nearCandidates.clear();
    for (size_t i = 0; i < people.size(); ++i) {
        Person &p = people[i];
        Kind &kind = *kinds[size_t(p.spec.kind)];
        p.tier = -1;
        p.level = -1;
        if (!kind.prepared) {
            ++figures.waiting;
            continue;
        }
        if (!p.chosen)
            choosePlay(p, kind);
        // Its place: the walkers along their circles, facing the way.
        std::copy(p.spec.position, p.spec.position + 3, p.position);
        p.heading = p.spec.heading;
        if (p.walking) {
            const float angle = p.startAngle + p.angularSpeed * float(clock);
            const float radius = std::max(p.spec.radius, 0.1f);
            p.position[0] += radius * std::cos(angle);
            p.position[2] += radius * std::sin(angle);
            const float way = p.angularSpeed < 0.0f ? -1.0f : 1.0f;
            p.heading = std::atan2(-std::sin(angle) * way, std::cos(angle) * way) - kind.forwardAngle;
        }
        // Its coverage, as ShapeLod::coverage measures it.
        const float dx = p.position[0] - view.camera[0];
        const float dy = p.position[1] + kind.middle * p.spec.scale - view.camera[1];
        const float dz = p.position[2] - view.camera[2];
        p.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float span = p.distance * viewScale;
        const float coverage = span > 1e-6f ? kind.extent * p.spec.scale / span : 1e6f;
        const int levels = int(kind.coverage.size());
        p.level = levels - 1;
        for (int level = 0; level < levels; ++level)
            if (coverage >= kind.coverage[size_t(level)] * tuning.coverageScale) {
                p.level = level;
                break;
            }
        const bool far = levels > 1 && coverage < kind.coverage.back() * tuning.coverageScale;
        p.tier = far ? 2 : p.level == 0 ? 0 : 1;
        if (p.tier == 0)
            nearCandidates.push_back(int(i));
    }
    // Own poses for the nearest few only.
    if (int(nearCandidates.size()) > tuning.nearCap) {
        const size_t cap = size_t(std::max(tuning.nearCap, 0));
        std::nth_element(nearCandidates.begin(), nearCandidates.begin() + long(cap), nearCandidates.end(),
                         [this](int a, int b) { return people[size_t(a)].distance < people[size_t(b)].distance; });
        for (size_t k = cap; k < nearCandidates.size(); ++k)
            people[size_t(nearCandidates[k])].tier = 1;
    }

    for (Person &p : people) {
        if (p.tier < 0)
            continue;
        Kind &kind = *kinds[size_t(p.spec.kind)];
        if (p.tier != 0 && p.own != NoState) {
            kind.freeOwn.push_back(p.own);
            kind.ownInUse.erase(std::find(kind.ownInUse.begin(), kind.ownInUse.end(), p.own));
            p.own = NoState;
        }
        if (p.tier == 0)
            ++figures.nearTier;
        else if (p.tier == 1)
            ++figures.middleTier;
        else
            ++figures.farTier;
        if (p.clip < 0) {
            p.draw = kind.rest;
            continue;
        }
        const Kind::Bank &bank = kind.banks[size_t(p.clip)];
        const float duration = bank.duration;
        const double t = clock * double(p.rate) + double(p.offset);
        const float phaseTime = duration > 0.0f ? float(t - std::floor(t / double(duration)) * double(duration)) : 0.0f;
        if (p.tier == 0) {
            if (p.own == NoState)
                p.own = ownState(kind);
            ComplexShape::ClipPlay play;
            play.clip = p.clip;
            play.time = phaseTime;
            kind.shape->setClips(p.own, &play, 1);
            kind.shape->updatePose(p.own);
            p.draw = p.own;
            ++figures.ownPoses;
        } else {
            const int phases = int(bank.states.size());
            const int phase = duration > 0.0f ? int(std::lround(phaseTime / duration * float(phases))) % phases : 0;
            p.draw = bankState(kind, p.clip, phase);
        }
    }
    figures.updateMs = double(timer.nsecsElapsed()) / 1e6;
}

void Crowd::push(RenderQueue &queue) {
    QElapsedTimer timer;
    timer.start();
    const bool animated = queue.animatedEnabled();
    queue.setAnimated(true);
    for (const Person &p : people) {
        if (p.tier < 0 || p.draw == NoState)
            continue;
        GltfShape *shape = kinds[size_t(p.spec.kind)]->shape;
        queue.pushTransform();
        float *mv = queue.transform();
        Mat4::translate(mv, mv, p.position[0], p.position[1], p.position[2]);
        Mat4::rotateY(mv, mv, p.heading);
        if (p.spec.scale != 1.0f)
            for (int c = 0; c < 12; ++c)
                mv[c] *= p.spec.scale;
        shape->setCurrentDistanceLevel(p.draw, p.level);
        shape->pushRenderItem(queue, 0, p.draw);
        queue.popTransform();
    }
    queue.setAnimated(animated);
    figures.pushMs = double(timer.nsecsElapsed()) / 1e6;
}

std::vector<int> Crowd::walkClips(int kind) const {
    return kind >= 0 && kind < kindCount() ? kinds[size_t(kind)]->walks : std::vector<int>();
}

std::vector<int> Crowd::idleClips(int kind) const {
    return kind >= 0 && kind < kindCount() ? kinds[size_t(kind)]->idles : std::vector<int>();
}

float Crowd::clipSpeed(int kind, int clip) const {
    if (kind < 0 || kind >= kindCount() || clip < 0 || size_t(clip) >= kinds[size_t(kind)]->speeds.size())
        return 0.0f;
    return kinds[size_t(kind)]->speeds[size_t(clip)];
}

int Crowd::tierOf(int person) const {
    return person >= 0 && person < personCount() ? people[size_t(person)].tier : -1;
}

int Crowd::levelOf(int person) const {
    return person >= 0 && person < personCount() ? people[size_t(person)].level : -1;
}

unsigned int Crowd::stateOf(int person) const {
    return person >= 0 && person < personCount() ? people[size_t(person)].draw : NoState;
}

const float *Crowd::placeOf(int person) const {
    return person >= 0 && person < personCount() ? people[size_t(person)].position : nullptr;
}

}
