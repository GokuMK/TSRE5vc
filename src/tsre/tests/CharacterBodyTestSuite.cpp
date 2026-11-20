/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// A walking character's body (task characters 02) against made-up shapes:
// the ground, a step, a platform's edge, walls slid along, ramps walked up
// or not, stairs, falling off a ledge, a jump and a ceiling, a turned and a
// scaled mesh, a mesh moving into it, where it may not go, a ray. With
// TSRE_COLLISION_SHAPES (a folder of shapes: S files, glTF, e3d; at most
// TSRE_COLLISION_LIMIT of them, 400 when not given), each shape's collision
// read: Default every part but vegetation, Solid every part, None nothing,
// Box boxes.

#include "CharacterBodyTestSuite.h"
#include "TokenTestSupport.h"
#include <tsre/characters/CharacterBody.h>
#include <tsre/characters/CollisionWorld.h>
#include <tsre/characters/ShapeCollision.h>
#include <QDir>
#include <QElapsedTimer>
#include <cmath>
#include <memory>

namespace {

using Characters::CharacterBody;
using Characters::CollisionMesh;
using Characters::CollisionWorld;

void quad(std::vector<float> &t, const float *a, const float *b, const float *c, const float *d) {
    for (const float *p : {a, b, c, a, c, d})
        t.insert(t.end(), p, p + 3);
}

// A box's six faces.
std::shared_ptr<const CollisionMesh> box(float x0, float y0, float z0, float x1, float y1, float z1) {
    std::vector<float> t;
    const float p[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1},
                           {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}};
    quad(t, p[0], p[1], p[2], p[3]);
    quad(t, p[4], p[5], p[6], p[7]);
    quad(t, p[0], p[1], p[5], p[4]);
    quad(t, p[1], p[2], p[6], p[5]);
    quad(t, p[2], p[3], p[7], p[6]);
    quad(t, p[3], p[0], p[4], p[7]);
    return std::make_shared<CollisionMesh>(t);
}

// A ramp rising `rise` from x0 to x1, between z0 and z1, from the ground.
std::shared_ptr<const CollisionMesh> ramp(float x0, float x1, float rise, float z0, float z1) {
    std::vector<float> t;
    const float a[3] = {x0, 0.0f, z0}, b[3] = {x1, rise, z0}, c[3] = {x1, rise, z1}, d[3] = {x0, 0.0f, z1};
    quad(t, a, b, c, d);
    return std::make_shared<CollisionMesh>(t);
}

const float Identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

CollisionWorld ground() {
    CollisionWorld world;
    world.setTerrain([](float, float, float *height) {
        *height = 0.0f;
        return true;
    });
    return world;
}

// Walks at (vx, vz) for `seconds` at 60 frames a second.
void walk(CharacterBody &body, const CollisionWorld &world, float vx, float vz, float seconds, bool jump = false,
          const std::function<bool(float, float)> &allowed = {}) {
    const int frames = int(std::lround(seconds * 60.0f));
    for (int i = 0; i < frames; ++i)
        body.move(world, vx, vz, 1.0f / 60.0f, jump && i == 0, allowed);
}

QString at(const CharacterBody &body) {
    const float *p = body.position();
    return QString("%1, %2, %3").arg(double(p[0]), 0, 'f', 2).arg(double(p[1]), 0, 'f', 2).arg(double(p[2]), 0, 'f', 2);
}

} // namespace

namespace TsreTests {

int runCharacterBodySuite(bool verbose) {
    TokenTest::Suite test{"[tests:character-body]", verbose};
    const float origin[3] = {0.0f, 0.0f, 0.0f};

    {
        CollisionWorld world = ground();
        CharacterBody body;
        body.setPosition(origin);
        walk(body, world, 1.4f, 0.0f, 1.0f);
        test.check(std::fabs(body.position()[0] - 1.4f) < 0.02f && body.position()[1] == 0.0f && body.onGround(),
                   QString("on the ground: 1.4 m/s for a second goes 1.4 m (%1)").arg(at(body)));
    }
    {
        CollisionWorld world = ground();
        world.add(box(1.0f, 0.0f, -2.0f, 3.0f, 0.3f, 2.0f), Identity);
        CharacterBody body;
        body.setPosition(origin);
        walk(body, world, 1.0f, 0.0f, 2.0f);
        test.check(body.position()[0] > 1.9f && std::fabs(body.position()[1] - 0.3f) < 1e-3f,
                   QString("a 0.3 m step is walked up (%1)").arg(at(body)));
    }
    {
        CollisionWorld world = ground();
        world.add(box(1.0f, 0.0f, -2.0f, 3.0f, 1.0f, 2.0f), Identity);
        CharacterBody body;
        body.setPosition(origin);
        walk(body, world, 1.0f, 0.0f, 2.0f);
        test.check(body.position()[0] < 0.71f && body.position()[0] > 0.6f && body.position()[1] == 0.0f,
                   QString("a 1 m platform's edge stops it a radius off (%1)").arg(at(body)));
    }
    {
        CollisionWorld world = ground();
        world.add(box(1.0f, 0.0f, -10.0f, 1.2f, 3.0f, 10.0f), Identity);
        CharacterBody body;
        body.setPosition(origin);
        walk(body, world, 1.0f, 1.0f, 2.0f);
        test.check(body.position()[0] < 0.71f && body.position()[2] > 1.9f,
                   QString("walking into a wall at 45 degrees: stopped at it, sliding along it (%1)").arg(at(body)));
    }
    {
        CollisionWorld world = ground();
        // 30 degrees: 1 m up over 1.73 m, then a top.
        world.add(ramp(1.0f, 2.732f, 1.0f, -2.0f, 2.0f), Identity);
        world.add(box(2.732f, 0.0f, -2.0f, 5.0f, 1.0f, 2.0f), Identity);
        CharacterBody body;
        body.setPosition(origin);
        walk(body, world, 1.0f, 0.0f, 4.0f);
        test.check(body.position()[0] > 3.5f && std::fabs(body.position()[1] - 1.0f) < 0.01f,
                   QString("a 30 degree ramp is walked up (%1)").arg(at(body)));
    }
    {
        CollisionWorld world = ground();
        // 60 degrees: 1 m up over 0.58 m.
        world.add(ramp(1.0f, 1.577f, 1.0f, -2.0f, 2.0f), Identity);
        CharacterBody body;
        body.setPosition(origin);
        walk(body, world, 1.0f, 0.0f, 3.0f);
        test.check(body.position()[1] < 0.2f, QString("a 60 degree ramp is not walked up (%1)").arg(at(body)));
    }
    {
        CollisionWorld world = ground();
        // Six steps of 0.17 m rise and 0.28 m going, then a landing.
        for (int i = 0; i < 6; ++i)
            world.add(box(1.0f + 0.28f * float(i), 0.0f, -1.0f, 1.0f + 0.28f * float(i + 1), 0.17f * float(i + 1), 1.0f),
                      Identity);
        world.add(box(2.68f, 0.0f, -1.0f, 5.0f, 1.02f, 1.0f), Identity);
        CharacterBody body;
        body.setPosition(origin);
        walk(body, world, 1.0f, 0.0f, 4.0f);
        test.check(body.position()[0] > 3.5f && std::fabs(body.position()[1] - 1.02f) < 0.01f,
                   QString("stairs of 0.17 m are climbed (%1)").arg(at(body)));
    }
    {
        CollisionWorld world = ground();
        world.add(box(-2.0f, 0.0f, -2.0f, 1.0f, 1.0f, 2.0f), Identity);
        CharacterBody body;
        const float top[3] = {0.0f, 1.0f, 0.0f};
        body.setPosition(top);
        // Off the edge (1 m, its feet held half a radius on) after 1.3 s.
        walk(body, world, 1.0f, 0.0f, 1.3f);
        const bool falling = !body.onGround() && body.position()[1] < 1.0f;
        walk(body, world, 1.0f, 0.0f, 1.0f);
        test.check(falling && body.onGround() && body.position()[1] == 0.0f && body.position()[0] > 1.3f,
                   QString("off a 1 m ledge: it falls and lands on the ground (%1)").arg(at(body)));
    }
    {
        CollisionWorld world = ground();
        CharacterBody body;
        body.setPosition(origin);
        float highest = 0.0f;
        body.move(world, 0.0f, 0.0f, 1.0f / 60.0f, true);
        for (int i = 0; i < 90; ++i) {
            body.move(world, 0.0f, 0.0f, 1.0f / 60.0f);
            highest = std::max(highest, body.position()[1]);
        }
        test.check(highest > 0.45f && highest < 0.6f && body.onGround() && body.position()[1] == 0.0f,
                   QString("a jump: %1 m up, back on the ground").arg(double(highest), 0, 'f', 2));
        // Under a ceiling 2 m up: its head stops it.
        world.add(box(-3.0f, 2.0f, -3.0f, 3.0f, 2.2f, 3.0f), Identity);
        body.setPosition(origin);
        highest = 0.0f;
        body.move(world, 0.0f, 0.0f, 1.0f / 60.0f, true);
        for (int i = 0; i < 90; ++i) {
            body.move(world, 0.0f, 0.0f, 1.0f / 60.0f);
            highest = std::max(highest, body.position()[1]);
        }
        test.check(highest < 0.25f && body.onGround(), QString("a jump under a ceiling 2 m up: its head stops it (feet %1 m up)")
                   .arg(double(highest), 0, 'f', 2));
    }
    {
        // A wall made across x at x = 1, turned 90 degrees about y: across
        // z at z = -1 (its x goes to -z, its z to x).
        CollisionWorld world = ground();
        const float turned[16] = {0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1};
        world.add(box(1.0f, 0.0f, -10.0f, 1.2f, 3.0f, 10.0f), turned);
        CharacterBody body;
        body.setPosition(origin);
        walk(body, world, 0.0f, -1.0f, 2.0f);
        test.check(body.position()[2] > -0.71f && body.position()[2] < -0.6f,
                   QString("a turned wall stops it where it is turned to (%1)").arg(at(body)));
        // Scaled twice: a box from x 0.5 to 1.5 becomes 1 to 3, 1 m up.
        CollisionWorld scaled = ground();
        const float twice[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1};
        scaled.add(box(0.5f, 0.0f, -1.0f, 1.5f, 0.5f, 1.0f), twice);
        body.setPosition(origin);
        walk(body, scaled, 1.0f, 0.0f, 2.0f);
        test.check(body.position()[0] < 0.71f && body.position()[0] > 0.6f,
                   QString("a mesh scaled twice stops it at its scaled edge (%1)").arg(at(body)));
    }
    {
        // A box moving into it (a car): pushed aside.
        CollisionWorld world = ground();
        const int moving = world.add(box(-0.5f, 0.0f, -2.0f, 0.5f, 3.0f, 2.0f), Identity);
        float matrix[16];
        std::copy(Identity, Identity + 16, matrix);
        matrix[12] = 2.0f;
        world.place(moving, matrix);
        CharacterBody body;
        body.setPosition(origin);
        for (int i = 0; i < 30; ++i) {
            matrix[12] = 2.0f - 0.05f * float(i + 1);
            world.place(moving, matrix);
            body.move(world, 0.0f, 0.0f, 1.0f / 60.0f);
        }
        test.check(body.position()[0] < 0.5f - 0.29f, QString("a mesh moving into it pushes it ahead of it (%1, its face at %2)")
                   .arg(at(body)).arg(double(matrix[12] - 0.5f), 0, 'f', 2));
    }
    {
        CollisionWorld world = ground();
        CharacterBody body;
        body.setPosition(origin);
        walk(body, world, 1.0f, 0.0f, 3.0f, false, [](float x, float) { return x < 2.0f; });
        test.check(body.position()[0] < 2.0f && body.position()[0] > 1.85f,
                   QString("where it may not go it stays out (%1)").arg(at(body)));
    }
    {
        CollisionWorld world = ground();
        world.add(box(1.0f, 0.0f, -10.0f, 1.2f, 3.0f, 10.0f), Identity);
        const float from[3] = {0.0f, 1.0f, 0.0f}, dir[3] = {1.0f, 0.0f, 0.0f}, down[3] = {0.0f, -1.0f, 0.0f};
        float distance = 0.0f, normal[3];
        int placed = -2;
        const bool hit = world.ray(from, dir, 10.0f, &distance, normal, &placed);
        float groundDistance = 0.0f;
        const bool groundHit = world.ray(from, down, 5.0f, &groundDistance, normal, &placed);
        test.check(hit && std::fabs(distance - 1.0f) < 1e-3f && groundHit && placed == -1 && std::fabs(groundDistance - 1.0f) < 0.51f,
                   QString("a ray meets the wall 1 m off (%1) and the ground below (%2)").arg(double(distance), 0, 'f', 3)
                   .arg(double(groundDistance), 0, 'f', 2));
    }
    const QString folder = qEnvironmentVariable("TSRE_COLLISION_SHAPES");
    if (!folder.isEmpty()) {
        const int limit = qEnvironmentVariableIsSet("TSRE_COLLISION_LIMIT") ? qEnvironmentVariableIntValue("TSRE_COLLISION_LIMIT") : 400;
        QStringList files = QDir(folder).entryList({"*.s", "*.S", "*.glb", "*.gltf", "*.e3d"}, QDir::Files, QDir::Name);
        if (files.size() > limit)
            files = files.mid(0, limit);
        int read = 0, withVegetation = 0, withAlphaTest = 0, wrong = 0, boxesWrong = 0;
        qint64 triangles = 0;
        QStringList examples;
        QElapsedTimer timer;
        timer.start();
        for (const QString &name : files) {
            const QString path = folder + "/" + name;
            const QString textures = folder + "/../TEXTURES";
            const auto mesh = Characters::shapeCollision(path, textures, Characters::Collision::Default);
            const Characters::ShapeCollisionInfo info = Characters::shapeCollisionInfo(path, textures, Characters::Collision::Default);
            const auto solid = Characters::shapeCollision(path, textures, Characters::Collision::Solid);
            const auto box = Characters::shapeCollision(path, textures, Characters::Collision::Box);
            if (info.parts == 0)
                continue;
            ++read;
            const int own = mesh ? mesh->triangleCount() : 0, all = solid ? solid->triangleCount() : 0;
            triangles += all;
            withVegetation += info.vegetationParts > 0 ? 1 : 0;
            withAlphaTest += info.alphaTestedParts > 0 ? 1 : 0;
            // Default: as Solid without vegetation (unless its .sd says).
            const bool ownSay = info.taken != Characters::Collision::Default;
            if (!ownSay && (own > all || (info.vegetationParts == 0 && own != all) || (info.vegetationParts > 0 && own >= all)))
                ++wrong;
            if (!box || box->triangleCount() % 12 != 0)
                ++boxesWrong;
            if (info.vegetationParts > 0 && examples.size() < 3)
                examples << QString("%1 (%2 of %3 triangles)").arg(name).arg(own).arg(all);
        }
        const double ms = double(timer.nsecsElapsed()) / 1e6;
        qInfo().noquote() << QString("[tests:character-body] %1: %2 of %3 shapes read, %4 with vegetation (%5), %6 with"
                                     " alpha-tested parts, %7 triangles; %8 ms a shape (three modes)")
                             .arg(folder).arg(read).arg(files.size()).arg(withVegetation).arg(examples.join(", "))
                             .arg(withAlphaTest).arg(triangles).arg(files.isEmpty() ? 0.0 : ms / double(files.size()), 0, 'f', 1);
        test.check(read > 0 && wrong == 0 && boxesWrong == 0,
                   QString("%1 shapes' collisions: Default every part but vegetation (%2 wrong), Box boxes (%3 wrong)")
                   .arg(read).arg(wrong).arg(boxesWrong));
        test.check(!Characters::shapeCollision(folder + "/" + (files.isEmpty() ? QString() : files.first()), folder + "/../TEXTURES",
                                               Characters::Collision::None), "None: nothing to collide with");
    }
    return test.finish();
}

} // namespace TsreTests
