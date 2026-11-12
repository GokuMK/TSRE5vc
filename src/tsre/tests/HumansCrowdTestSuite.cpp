/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Crowds of humans (task humans 03), on humans-gen's humans
// (TSRE_HUMANS_SHAPES: its out/route/SHAPES): the clips' ground speeds
// against humans-gen's own measure, tiers by screen coverage, the cap on
// own poses, shared phases sharing packets, walkers keeping their clip's
// speed, and the cost of 500 people a frame (printed; meaningful in a
// Release build).

#include "HumansCrowdTestSuite.h"
#include "TokenTestSupport.h"
#include <tsre/humans/Crowd.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/SkeletalAnimation.h>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {

struct PacketRecorder : RenderQueue {
    QVector<RenderItem *> packets;
    using RenderQueue::submit;
    void submit(RenderItem *item, quint32 = 0, SubmitOrder = SUBMIT_GROUPED) override { packets.push_back(item); }
    void submit(const QVector<RenderItem *> &items, quint32 = 0) override { packets += items; }
    void submitFrameItem(RenderItem *item) override { packets.push_back(item); }
};

std::unique_ptr<GltfShape> loadHuman(const QString &shapes, const QString &name) {
    const QString path = shapes + "/" + name + ".glb";
    auto shape = std::make_unique<GltfShape>(path, QFileInfo(path).fileName(), shapes + "/../TEXTURES");
    shape->load();
    return shape;
}

} // namespace

namespace TsreTests {

int runHumansCrowdSuite(bool verbose) {
    TokenTest::Suite test{"[tests:humans-crowd]", verbose};
    const QString shapes = qEnvironmentVariable("TSRE_HUMANS_SHAPES");
    if (shapes.isEmpty() || !QFileInfo::exists(shapes + "/man.glb")) {
        qInfo() << "[tests:humans-crowd] set TSRE_HUMANS_SHAPES to humans-gen's out/route/SHAPES";
        return 2;
    }
    const QString clipsFile = shapes + "/people_clips.glb";

    // Ground speeds: humans-gen measured the walks from the foot standing
    // (within 2 cm of the ground): man 1.13, woman 1.00, young 0.92 m/s.
    // Idles stand; every human faces the same way.
    struct Expected {
        const char *name;
        float walk;
    };
    float firstDirection[3] = {0.0f, 0.0f, 0.0f};
    for (const Expected &e : {Expected{"man", 1.13f}, Expected{"woman", 1.00f}, Expected{"young", 0.92f}}) {
        std::unique_ptr<GltfShape> human = loadHuman(shapes, e.name);
        const SkeletalAnimation *rig = human->skeleton();
        if (!human->isLoaded() || rig == nullptr) {
            test.check(false, QString("%1 loads with a skeleton").arg(e.name));
            continue;
        }
        bool idlesStand = true, walksGo = true;
        float walk = 0.0f, worstIdle = 0.0f;
        SkeletalAnimation::GroundMotion walkMotion;
        for (int clip = 0; clip < rig->clipCount(); ++clip) {
            const SkeletalAnimation::GroundMotion motion = rig->groundMotion(clip, 0);
            const QString name = rig->clipName(clip);
            if (name.startsWith("idle")) {
                idlesStand = idlesStand && motion.speed < Humans::Crowd::WalkSpeed;
                worstIdle = std::max(worstIdle, motion.speed);
            } else if (name.startsWith("walk")) {
                walksGo = walksGo && motion.speed >= Humans::Crowd::WalkSpeed;
            }
            if (name == "walk") {
                walk = motion.speed;
                walkMotion = motion;
            }
        }
        test.check(std::fabs(walk - e.walk) <= 0.1f * e.walk,
                   QString("%1: walks at %2 m/s (humans-gen %3)").arg(e.name).arg(double(walk), 0, 'f', 3)
                   .arg(double(e.walk)));
        test.check(idlesStand && walksGo, QString("%1: idles stand (at most %2 m/s), walks go")
                   .arg(e.name).arg(double(worstIdle), 0, 'f', 3));
        if (firstDirection[0] == 0.0f && firstDirection[2] == 0.0f)
            std::copy(walkMotion.direction, walkMotion.direction + 3, firstDirection);
        const float dot = firstDirection[0] * walkMotion.direction[0] + firstDirection[2] * walkMotion.direction[2];
        test.check(dot > 0.97f && walkMotion.direction[1] == 0.0f,
                   QString("%1: walks the way the man does (%2, %3)").arg(e.name)
                   .arg(double(walkMotion.direction[0]), 0, 'f', 3).arg(double(walkMotion.direction[2]), 0, 'f', 3));
    }

    // Tiers: the man at 5, 45 and 300 m from a camera 1.7 m up, a 60°
    // view: near (level 0, its own pose), middle (a coarser level, a shared
    // pose), far.
    std::unique_ptr<GltfShape> man = loadHuman(shapes, "man");
    {
        Humans::Crowd crowd;
        crowd.addKind(man.get(), clipsFile);
        for (float x : {5.0f, 45.0f, 300.0f}) {
            Humans::Crowd::PersonSpec spec;
            spec.position[0] = x;
            spec.seed = 11;
            crowd.addPerson(spec);
        }
        Humans::Crowd::View view;
        view.camera[1] = 1.7f;
        view.fieldOfView = 60.0f;
        crowd.update(0.0f, view);
        test.check(crowd.walkClips(0).size() == 6 && crowd.idleClips(0).size() == 8,
                   QString("the man with the shared clips: %1 walks, %2 idles")
                   .arg(crowd.walkClips(0).size()).arg(crowd.idleClips(0).size()));
        test.check(crowd.tierOf(0) == 0 && crowd.levelOf(0) == 0, QString("5 m: near, level 0 (tier %1, level %2)")
                   .arg(crowd.tierOf(0)).arg(crowd.levelOf(0)));
        test.check(crowd.tierOf(1) == 1 && crowd.levelOf(1) > 0, QString("45 m: middle, level %1 (tier %2)")
                   .arg(crowd.levelOf(1)).arg(crowd.tierOf(1)));
        test.check(crowd.tierOf(2) == 2, QString("300 m: far (tier %1)").arg(crowd.tierOf(2)));
        // One seed: one clip and phase, so the middle and far people share
        // a state, whatever their levels.
        test.check(crowd.stats().ownPoses == 1 && crowd.stats().bankStates == 1 && crowd.stateOf(1) == crowd.stateOf(2)
                   && crowd.stateOf(0) != crowd.stateOf(1),
                   QString("one own pose, one shared phase for the others (%1, %2)").arg(crowd.stats().ownPoses)
                   .arg(crowd.stats().bankStates));
    }

    // The cap: five people near, two with own poses (the nearest).
    {
        Humans::Crowd crowd;
        Humans::Crowd::Tuning tuning;
        tuning.nearCap = 2;
        crowd.setTuning(tuning);
        crowd.addKind(man.get(), clipsFile);
        for (float x : {7.0f, 3.0f, 6.0f, 4.0f, 5.0f}) {
            Humans::Crowd::PersonSpec spec;
            spec.position[0] = x;
            spec.seed = quint32(x);
            crowd.addPerson(spec);
        }
        Humans::Crowd::View view;
        view.camera[1] = 1.7f;
        view.fieldOfView = 60.0f;
        crowd.update(0.0f, view);
        test.check(crowd.tierOf(1) == 0 && crowd.tierOf(3) == 0 && crowd.tierOf(0) == 1 && crowd.tierOf(2) == 1
                   && crowd.tierOf(4) == 1 && crowd.stats().ownPoses == 2,
                   QString("near cap 2: the people at 3 and 4 m pose themselves (%1 own poses)")
                   .arg(crowd.stats().ownPoses));
    }

    // Shared phases: two people with one seed (one clip, one phase) at
    // 45 m draw one state and submit the same packets; a third, apart.
    {
        Humans::Crowd crowd;
        crowd.addKind(man.get(), clipsFile);
        for (float z : {0.0f, 2.0f}) {
            Humans::Crowd::PersonSpec spec;
            spec.position[0] = 45.0f;
            spec.position[2] = z;
            spec.seed = 5;
            crowd.addPerson(spec);
        }
        Humans::Crowd::PersonSpec other;
        other.position[0] = 45.0f;
        other.position[2] = 4.0f;
        other.seed = 6;
        crowd.addPerson(other);
        Humans::Crowd::View view;
        view.camera[1] = 1.7f;
        view.fieldOfView = 60.0f;
        crowd.update(0.0f, view);
        PacketRecorder queue;
        crowd.push(queue);
        const int half = queue.packets.size() / 3;
        bool same = half > 0 && queue.packets.size() == half * 3;
        for (int k = 0; same && k < half; ++k)
            same = queue.packets[k] == queue.packets[half + k];
        bool apart = same;
        for (int k = 0; apart && k < half; ++k)
            apart = queue.packets[k] != queue.packets[2 * half + k];
        test.check(crowd.stateOf(0) == crowd.stateOf(1) && same,
                   QString("one phase, one state, the same %1 packets").arg(half));
        test.check(crowd.stateOf(2) != crowd.stateOf(0) && apart, "another phase: its own packets");
    }

    // A walker keeps its clip's speed along its circle.
    {
        Humans::Crowd crowd;
        crowd.addKind(man.get(), clipsFile);
        Humans::Crowd::PersonSpec spec;
        spec.position[0] = 10.0f;
        spec.behaviour = Humans::Crowd::Behaviour::Circle;
        spec.radius = 2.0f;
        spec.seed = 3;
        crowd.addPerson(spec);
        Humans::Crowd::View view;
        view.camera[1] = 1.7f;
        crowd.update(0.0f, view);
        const float before[3] = {crowd.placeOf(0)[0], crowd.placeOf(0)[1], crowd.placeOf(0)[2]};
        // Its clip: the one it shows (a walk), whose speed the crowd knows.
        float speed = 0.0f;
        for (int clip : crowd.walkClips(0))
            speed = std::max(speed, crowd.clipSpeed(0, clip));
        const float step = 0.1f;
        crowd.update(step, view);
        const float *after = crowd.placeOf(0);
        const float moved = std::sqrt((after[0] - before[0]) * (after[0] - before[0])
                                      + (after[2] - before[2]) * (after[2] - before[2])) / step;
        float slowest = 1e9f;
        for (int clip : crowd.walkClips(0))
            slowest = std::min(slowest, crowd.clipSpeed(0, clip));
        test.check(moved >= slowest * 0.99f && moved <= speed * 1.01f,
                   QString("a walker goes %1 m/s (its walks %2 to %3)").arg(double(moved), 0, 'f', 3)
                   .arg(double(slowest), 0, 'f', 3).arg(double(speed), 0, 'f', 3));
    }

    // The cost of 500 people a frame: the three humans on a 10 x 50 grid,
    // 30 % walking, the camera at its near end.
    {
        std::unique_ptr<GltfShape> woman = loadHuman(shapes, "woman");
        std::unique_ptr<GltfShape> young = loadHuman(shapes, "young");
        Humans::Crowd crowd;
        crowd.addKind(man.get(), clipsFile);
        crowd.addKind(woman.get(), clipsFile);
        crowd.addKind(young.get(), clipsFile);
        for (int i = 0; i < 500; ++i) {
            Humans::Crowd::PersonSpec spec;
            spec.kind = i % 3;
            spec.position[0] = 2.0f + float(i / 10) * 2.0f;
            spec.position[2] = (float(i % 10) - 4.5f) * 1.5f;
            spec.heading = float(i) * 0.7f;
            spec.behaviour = i % 10 < 3 ? Humans::Crowd::Behaviour::Circle : Humans::Crowd::Behaviour::Idle;
            spec.seed = quint32(i) * 7919u;
            crowd.addPerson(spec);
        }
        Humans::Crowd::View view;
        view.camera[0] = -3.0f;
        view.camera[1] = 1.7f;
        view.fieldOfView = 34.0f;
        crowd.update(0.0f, view);
        PacketRecorder warm;
        crowd.push(warm);
        const int frames = 60;
        double update = 0.0, push = 0.0;
        int packets = 0;
        QSet<const RenderItem *> distinct;
        for (int f = 0; f < frames; ++f) {
            crowd.update(1.0f / 60.0f, view);
            PacketRecorder queue;
            crowd.push(queue);
            update += crowd.stats().updateMs;
            push += crowd.stats().pushMs;
            packets = queue.packets.size();
            if (f == frames - 1)
                for (const RenderItem *p : queue.packets)
                    distinct.insert(p);
        }
        const Humans::Crowd::Stats &s = crowd.stats();
        test.check(s.people == 500 && s.waiting == 0 && s.nearTier + s.middleTier + s.farTier == 500
                   && s.nearTier <= 40, QString("500 people drawn: near %1, middle %2, far %3")
                   .arg(s.nearTier).arg(s.middleTier).arg(s.farTier));
        qInfo().noquote() << QString("[tests:humans-crowd] cost of 500: update %1 ms, push %2 ms a frame "
                                     "(%3 own poses, %4 bank states); %5 packets, %6 distinct")
                             .arg(update / frames, 0, 'f', 3).arg(push / frames, 0, 'f', 3).arg(s.ownPoses)
                             .arg(s.bankStates).arg(packets).arg(distinct.size());
    }
    return test.finish();
}

} // namespace TsreTests
