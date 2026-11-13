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
#include <tsre/humans/ImpostorBake.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/SkeletalAnimation.h>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>
#include <QThread>
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

// Loaded for a crowd: its triangles kept for the impostor.
std::unique_ptr<GltfShape> loadHuman(const QString &shapes, const QString &name) {
    const QString path = shapes + "/" + name + ".glb";
    auto shape = std::make_unique<GltfShape>(path, QFileInfo(path).fileName(), shapes + "/../TEXTURES");
    ShapeLoadOptions options;
    options.insert(QString::fromLatin1(ShapeLoadOption::SkinnedTriangles), true);
    shape->setLoadOptions(options);
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
        // Its playing speed within 5 % of its clip's.
        test.check(moved >= slowest * 0.949f && moved <= speed * 1.051f,
                   QString("a walker goes %1 m/s (its walks %2 to %3)").arg(double(moved), 0, 'f', 3)
                   .arg(double(slowest), 0, 'f', 3).arg(double(speed), 0, 'f', 3));
    }

    // The impostor: the man baked from 16 directions, an idle and 8 walk
    // frames. Opposite views see mirrored silhouettes (no perspective);
    // heads' normals point up.
    {
        const std::shared_ptr<const GltfShape::SkinnedSource> source = man->skinnedSource();
        const SkeletalAnimation *rig = man->skeleton();
        test.check(source && source->level == 1 && !source->primitives.empty(),
                   QString("the man keeps level %1's triangles (%2 primitives)").arg(source ? source->level : -1)
                   .arg(source ? int(source->primitives.size()) : 0));
        if (source && rig != nullptr) {
            Humans::ImpostorBake::Request request;
            const int walk = rig->clipIndex("walk"), idle = rig->clipIndex("idle");
            request.poses.push_back({idle, 1.25f});
            for (int f = 0; f < 8; ++f)
                request.poses.push_back({walk, rig->clipDuration(walk) * float(f) / 8.0f});
            const Humans::ImpostorBake::Result r = Humans::ImpostorBake::bake(*source, *rig, request);
            qint64 colorBytes = 0, normalBytes = 0;
            for (const QByteArray &level : r.colorLevels)
                colorBytes += level.size();
            for (const QByteArray &level : r.normalLevels)
                normalBytes += level.size();
            // Every level down to 1 x 1: 1024 x 1152 halves 11 times.
            test.check(r.ok && r.directions == 16 && r.poses == 9 && r.color.width() == 16 * 64
                       && r.color.height() == 9 * 128 && r.top - r.bottom > 1.75f && r.colorLevels.size() == 11
                       && r.normalLevels.size() == 11 && colorBytes > 1024 * 1152 && normalBytes > 1024 * 1152 / 2,
                       QString("baked: %1 x %2 px, %3 m tall, in %4 ms; %5 levels, colour %6 MB, normals %7 MB")
                       .arg(r.color.width()).arg(r.color.height()).arg(double(r.top - r.bottom), 0, 'f', 2)
                       .arg(r.milliseconds, 0, 'f', 1).arg(r.colorLevels.size()).arg(colorBytes / 1048576.0, 0, 'f', 2)
                       .arg(normalBytes / 1048576.0, 0, 'f', 2));
            auto alphaAt = [&](int cell, int x, int y) {
                float uv[4];
                r.cellRect(cell, uv);
                const int left = int(std::lround(uv[0] * r.color.width())), top = int(std::lround(uv[1] * r.color.height()));
                return int(r.color.constScanLine(top + y)[(left + x) * 4 + 3]);
            };
            for (int pose : {0, 3}) {
                int covered = 0, mismatched = 0;
                for (int y = 0; y < r.cellHeight; ++y)
                    for (int x = 0; x < r.cellWidth; ++x) {
                        const int front = alphaAt(pose * 16, x, y), back = alphaAt(pose * 16 + 8, r.cellWidth - 1 - x, y);
                        covered += front > 0;
                        mismatched += std::abs(front - back) > 64;
                    }
                const double share = double(covered) / double(r.cellWidth * r.cellHeight);
                test.check(r.ok && share > 0.1 && share < 0.6 && mismatched <= covered / 50,
                           QString("pose %1: %2 % of the cell covered; front and back silhouettes mirror (%3 pixels "
                                   "of %4 apart)").arg(pose).arg(share * 100.0, 0, 'f', 1).arg(mismatched).arg(covered));
            }
            // The top tenth of the front idle cell's covered pixels: normals up.
            if (r.ok) {
                float uv[4];
                r.cellRect(0, uv);
                const int left = int(std::lround(uv[0] * r.normal.width())), top = int(std::lround(uv[1] * r.normal.height()));
                int first = -1;
                for (int y = 0; y < r.cellHeight && first < 0; ++y)
                    for (int x = 0; x < r.cellWidth; ++x)
                        if (alphaAt(0, x, y) > 128) {
                            first = y;
                            break;
                        }
                double up = 0.0;
                int count = 0;
                for (int y = first; y >= 0 && y < first + 4; ++y)
                    for (int x = 0; x < r.cellWidth; ++x)
                        if (alphaAt(0, x, y) > 128) {
                            up += double(r.normal.constScanLine(top + y)[(left + x) * 4 + 1]) / 127.5 - 1.0;
                            ++count;
                        }
                test.check(count > 0 && up / count > 0.3, QString("the top of the head faces up (normal y %1)")
                           .arg(count > 0 ? up / count : 0.0, 0, 'f', 2));
            }
        }
    }

    // Far people draw impostor cells: the direction nearest the camera's,
    // in the person's turn, and walkers a walk frame.
    {
        Humans::Crowd crowd;
        crowd.addKind(man.get(), clipsFile);
        struct Far {
            float x, z, heading;
            bool walking;
            int direction;
        };
        // The camera at the origin; the man faces +z at heading 0.
        const Far far[] = {{0.0f, -300.0f, 0.0f, false, 0}, {0.0f, 300.0f, 0.0f, false, 8},
                           {-300.0f, 0.0f, 0.0f, false, 4}, {0.0f, -300.0f, float(M_PI) / 2.0f, false, 12},
                           {0.0f, -300.0f, 0.0f, true, -1}};
        for (const Far &f : far) {
            Humans::Crowd::PersonSpec spec;
            spec.position[0] = f.x;
            spec.position[2] = f.z;
            spec.heading = f.heading;
            spec.behaviour = f.walking ? Humans::Crowd::Behaviour::Circle : Humans::Crowd::Behaviour::Idle;
            spec.radius = 0.5f;
            spec.seed = 9;
            crowd.addPerson(spec);
        }
        Humans::Crowd::View view;
        view.camera[1] = 1.7f;
        view.fieldOfView = 60.0f;
        QElapsedTimer waited;
        waited.start();
        while (!crowd.impostorReady(0) && waited.elapsed() < 30000) {
            crowd.update(0.0f, view);
            QThread::msleep(10);
        }
        crowd.update(0.0f, view);
        test.check(crowd.impostorReady(0) && crowd.impostorDirections(0) == 16 && !crowd.baking(),
                   QString("the crowd's impostor ready after %1 ms").arg(waited.elapsed()));
        // One seed: one idle clip, so one idle pose (one per distinct idle,
        // before the walk frames).
        bool directions = true;
        QStringList cells;
        const int idlePose = crowd.impostorCellOf(0) / 16;
        for (int i = 0; i < 4; ++i) {
            const int cell = crowd.impostorCellOf(i);
            cells << QString::number(cell);
            directions = directions && crowd.tierOf(i) == 2 && cell % 16 == far[i].direction && cell / 16 == idlePose;
        }
        test.check(directions && idlePose < 4, QString("idle far people face the camera's direction in their idle's "
                                                       "pose (cells %1)").arg(cells.join(", ")));
        test.check(crowd.tierOf(4) == 2 && crowd.impostorCellOf(4) >= 4 * 16,
                   QString("a far walker draws a walk frame, after the 4 idles (cell %1)").arg(crowd.impostorCellOf(4)));
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

    // Steered people (task humans 05): placed by their owner each frame,
    // their walk played at the speed they are moved; a removed person's
    // index given again.
    {
        Humans::Crowd crowd;
        crowd.addKind(man.get(), clipsFile);
        Humans::Crowd::PersonSpec spec;
        spec.behaviour = Humans::Crowd::Behaviour::Steered;
        spec.seed = 5;
        const int walker = crowd.addPerson(spec);
        spec.seed = 6;
        const int stander = crowd.addPerson(spec);
        Humans::Crowd::View view;
        view.camera[1] = 1.7f;
        view.camera[2] = -6.0f;
        view.fieldOfView = 60.0f;
        crowd.update(0.0f, view);
        const float walk = crowd.walkSpeedOf(walker);
        test.check(walk > 0.5f && walk < 1.5f, QString("a steered man walks at his walk's own speed (%1 m/s)")
                   .arg(double(walk), 0, 'f', 2));
        const float a[3] = {1.0f, 0.0f, 2.0f}, b[3] = {-1.0f, 0.0f, 2.0f};
        crowd.steer(walker, a, 0.5f, walk);
        crowd.steer(stander, b, 0.0f, 0.0f);
        crowd.update(0.1f, view);
        const float *placed = crowd.placeOf(walker);
        test.check(placed != nullptr && placed[0] == a[0] && placed[2] == a[2] && crowd.stateOf(walker) != crowd.stateOf(stander),
                   "steered people stand where put, the walker and the stander posed apart");
        crowd.removePerson(walker);
        crowd.update(0.1f, view);
        test.check(crowd.stats().people == 1 && crowd.tierOf(walker) == -1, QString("a removed person is not drawn (%1 left)")
                   .arg(crowd.stats().people));
        spec.seed = 7;
        test.check(crowd.addPerson(spec) == walker, "a removed person's index is given to the next");
        // A step clip carries the body as a whole (task humans 06): the
        // man's two steps up, 0.7 m up and about 0.6 m on (his size 1).
        const int steps = crowd.clipNamed(0, "steps_up_2");
        float travel[3] = {0.0f, 0.0f, 0.0f};
        const bool measured = steps >= 0 && crowd.clipTravelOf(walker, steps, 0.0f, travel);
        const float on = std::hypot(travel[0], travel[2]);
        test.check(measured && travel[1] > 0.6f && travel[1] < 0.8f && on > 0.4f && on < 0.9f,
                   QString("two steps up carry the man %1 m up and %2 m on").arg(double(travel[1]), 0, 'f', 2)
                   .arg(double(on), 0, 'f', 2));
        // Sitting down (task humans 09): the man's hips 0.355 m down and
        // 0.26 m back; his joints' median only 0.2 m down (his hands), which
        // sat him 0.2 m above the cushion (the user's review).
        const int sit = crowd.clipNamed(0, "sit_down");
        float hips[3] = {0.0f, 0.0f, 0.0f}, body[3] = {0.0f, 0.0f, 0.0f};
        const bool sat = sit >= 0 && crowd.clipTravelOf(walker, sit, 0.0f, hips, Humans::Crowd::Travel::Hips)
                && crowd.clipTravelOf(walker, sit, 0.0f, body);
        test.check(sat && hips[1] < -0.33f && hips[1] > -0.38f && hips[2] < -0.2f && hips[2] > -0.3f && body[1] > -0.25f,
                   QString("sitting down carries the man's hips %1 m up and %2 m on (his joints' median %3 m up)")
                   .arg(double(hips[1]), 0, 'f', 3).arg(double(hips[2]), 0, 'f', 3).arg(double(body[1]), 0, 'f', 3));
    }
    return test.finish();
}

} // namespace TsreTests
