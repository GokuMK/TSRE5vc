/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#include "Character.h"

#include <tsre/Game.h>
#include <tsre/KeyTaker.h>
#include <tsre/camera/CameraMount.h>
#include <tsre/humans/HumanSet.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/ShapeLib.h>
#include <QDebug>
#include <QGuiApplication>
#include <QKeyEvent>
#include <algorithm>
#include <cmath>

namespace Characters {

namespace {
constexpr float TwoPi = 2.0f * float(M_PI);
// The eyes over the feet before the head is known (standing, crouching),
// and from the head joint: up and ahead.
constexpr float EyeHeight = 1.62f;
constexpr float CrouchEyeHeight = 1.05f;
constexpr float EyeOverHead = 0.09f;
constexpr float EyeAheadOfHead = 0.11f;
// How fast the eyes follow the head (s): a step's bob kept, its jerks out.
constexpr float EyeLag = 0.05f;
// Third person: this far behind the eyes, pulled in front of what is
// between.
constexpr float BehindDistance = 3.0f;
}

class Character::Keys : public KeyTaker {
public:
    bool forward = false, back = false, left = false, right = false, run = false, slow = false;
    bool jumpAsked = false, crouchAsked = false, viewAsked = false;
    bool keyPress(const QKeyEvent &event) override { return key(event, true); }
    bool keyRelease(const QKeyEvent &event) override { return key(event, false); }
    void releaseKeys() override { forward = back = left = right = run = slow = false; }

private:
    bool key(const QKeyEvent &event, bool down) {
        const bool first = down && !event.isAutoRepeat();
        switch (event.key()) {
        case Qt::Key_W:
        case Qt::Key_Up:
            forward = down;
            return true;
        case Qt::Key_S:
        case Qt::Key_Down:
            back = down;
            return true;
        case Qt::Key_A:
        case Qt::Key_Left:
            left = down;
            return true;
        case Qt::Key_D:
        case Qt::Key_Right:
            right = down;
            return true;
        case Qt::Key_Shift:
            run = down;
            return true;
        case Qt::Key_Control:
            slow = down;
            return true;
        case Qt::Key_Space:
            jumpAsked = jumpAsked || first;
            return true;
        case Qt::Key_C:
            crouchAsked = crouchAsked || first;
            return true;
        case Qt::Key_V:
            viewAsked = viewAsked || first;
            return true;
        default:
            return false;
        }
    }
};

Character::Character() : keys(std::make_unique<Keys>()) {
}

Character::~Character() {
    crowd.clear();
}

void Character::start(const int *tile, const float *feet, float yaw, float pitch) {
    at[0] = tile[0];
    at[1] = tile[1];
    body.setPosition(feet);
    route.update(at, feet);
    way = yaw;
    look[0] = startLook[0] = yaw;
    look[1] = startLook[1] = pitch;
    velocity[0] = velocity[1] = moved[0] = moved[1] = 0.0f;
    // Standing (moved there again: Walk here).
    turning = false;
    crouching = false;
    if (jumping)
        locomotion.endAction();
    jumping = false;
    rest = false;
    eyeSet = false;
    eye[0] = feet[0];
    eye[1] = feet[1] + EyeHeight;
    eye[2] = feet[2];
    std::copy(eye, eye + 3, cameraPlace);
    loadHuman();
}

void Character::setControlled(bool on) {
    playerOwn = on;
    if (on)
        return;
    keys->releaseKeys();
    keys->jumpAsked = keys->crouchAsked = keys->viewAsked = false;
}

void Character::lookItsWay() {
    startLook[0] = way;
    startLook[1] = 0.0f;
}

void Character::flyPlace(float *out) const {
    placeBehind(look[0], look[1], out);
}

void Character::placeBehind(float yaw, float pitch, float *out) const {
    const float dir[3] = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    const float back[3] = {-dir[0], -dir[1], -dir[2]};
    float distance = BehindDistance, normal[3];
    if (route.world().ray(eye, back, BehindDistance, &distance, normal))
        distance = std::max(0.2f, distance - 0.2f);
    for (int i = 0; i < 3; ++i)
        out[i] = eye[i] + back[i] * distance;
}

// A human of the route's set (else TSRE's: always, whatever the people
// setting says), the same for the route each time.
void Character::loadHuman() {
    if (person >= 0 || Game::currentShapeLib == nullptr)
        return;
    const QString routeFolder = Game::root + "/ROUTES/" + Game::route;
    const Humans::HumanSet humans = Humans::HumanSet::forRoute(routeFolder, Humans::HumanSet::Always,
                                                               Humans::HumanSet::assetsHumansFolder(),
                                                               Humans::HumanSet::basicHumansFolder());
    if (humans.empty()) {
        person = -2;
        qInfo().noquote() << "Character: no humans for" << Game::route;
        return;
    }
    const Humans::HumanSet::List &list = humans.lists.front();
    // By their weights, from the route's name.
    float total = 0.0f;
    for (const Humans::HumanSet::Human &human : list.humans)
        total += human.weight;
    float pick = float(qHash(Game::route) % 1000u) / 1000.0f * total;
    size_t chosen = 0;
    for (size_t i = 0; i < list.humans.size(); ++i) {
        pick -= list.humans[i].weight;
        if (pick <= 0.0f) {
            chosen = i;
            break;
        }
    }
    ShapeLoadOptions options;
    options.insert(QString::fromLatin1(ShapeLoadOption::SkinnedTriangles), true);
    const int id = Game::currentShapeLib->addShape(list.humans[chosen].path, humans.textures, options);
    const auto found = Game::currentShapeLib->shape.find(id);
    GltfShape *shape = found != Game::currentShapeLib->shape.end() ? dynamic_cast<GltfShape *>(found->second) : nullptr;
    if (shape == nullptr) {
        person = -2;
        return;
    }
    crowd.addKind(shape, list.clips);
    Humans::Crowd::PersonSpec spec;
    spec.kind = 0;
    spec.behaviour = Humans::Crowd::Behaviour::Controlled;
    spec.heading = way;
    spec.seed = 1;
    person = crowd.addPerson(spec);
    qInfo().noquote() << "Character:" << list.humans[chosen].path;
}

// Into the tile its feet are in: the frame goes with it.
void Character::keepInTile() {
    const float *p = body.position();
    const int dx = int(std::floor((p[0] + 1024.0f) / 2048.0f)), dz = int(std::floor((p[2] + 1024.0f) / 2048.0f));
    if (dx == 0 && dz == 0)
        return;
    at[0] += dx;
    at[1] += dz;
    const float by[3] = {-2048.0f * float(dx), 0.0f, -2048.0f * float(dz)};
    body.shift(by);
    for (float *v : {eye, cameraPlace}) {
        v[0] += by[0];
        v[2] += by[2];
    }
}

void Character::update(float seconds, float yaw, float pitch, const int *cameraTile, const float *cameraAt) {
    seconds = std::clamp(seconds, 0.0f, 0.1f);
    // The player's looks as the camera; one left, its way.
    if (playerOwn) {
        look[0] = yaw;
        look[1] = pitch;
    } else {
        yaw = way;
        pitch = look[1];
    }
    if (person == -1)
        loadHuman();
    keepInTile();
    // Left far from the camera, standing still on the ground: at rest.
    const float offset[2] = {float(at[0] - cameraTile[0]) * 2048.0f, float(at[1] - cameraTile[1]) * 2048.0f};
    rest = !playerOwn && body.onGround() && std::hypot(velocity[0], velocity[1]) < 0.01f
            && std::hypot(body.position()[0] + offset[0] - cameraAt[0], body.position()[2] + offset[1] - cameraAt[2])
                    > RestDistance;
    if (rest)
        return;
    route.update(at, body.position());

    // What the keys ask: a way and a speed, as the camera looks (none of
    // one left).
    if (!playerOwn)
        keys->jumpAsked = keys->crouchAsked = keys->viewAsked = false;
    if (keys->crouchAsked) {
        crouching = !crouching;
        keys->crouchAsked = false;
    }
    if (keys->viewAsked) {
        behind = !behind;
        keys->viewAsked = false;
    }
    const Qt::KeyboardModifiers modifiers = QGuiApplication::queryKeyboardModifiers();
    float in[2] = {0.0f, 0.0f};
    if (playerOwn) {
        in[0] = float(keys->left) - float(keys->right);
        in[1] = float(keys->forward) - float(keys->back);
    }
    const float pressed = std::hypot(in[0], in[1]);
    float wanted[2] = {0.0f, 0.0f};
    if (pressed > 0.0f) {
        in[0] /= pressed;
        in[1] /= pressed;
        const bool ahead = in[1] > 0.7f;
        const bool slow = keys->slow || (modifiers & Qt::ControlModifier);
        const bool run = keys->run || (modifiers & Qt::ShiftModifier);
        float speed = crouching ? CrouchSpeed : slow ? SlowSpeed : WalkSpeed;
        if (!crouching && run && ahead)
            speed = RunSpeed;
        if (!ahead)
            speed = std::min(speed, AsideSpeed);
        const float f[2] = {std::sin(yaw), std::cos(yaw)}, l[2] = {std::cos(yaw), -std::sin(yaw)};
        wanted[0] = (in[0] * l[0] + in[1] * f[0]) * speed;
        wanted[1] = (in[0] * l[1] + in[1] * f[1]) * speed;
    }
    // Speeding up and slowing down (less in the air).
    const float most = (body.onGround() ? 10.0f : 2.0f) * seconds;
    float dv[2] = {wanted[0] - velocity[0], wanted[1] - velocity[1]};
    const float change = std::hypot(dv[0], dv[1]);
    if (change > most && change > 0.0f) {
        dv[0] *= most / change;
        dv[1] *= most / change;
    }
    velocity[0] += dv[0];
    velocity[1] += dv[1];
    const bool jump = keys->jumpAsked && body.onGround() && !crouching;
    keys->jumpAsked = false;
    const float before[2] = {body.position()[0], body.position()[2]};
    body.move(route.world(), velocity[0], velocity[1], seconds, jump, [this](float x, float z) { return route.allowed(x, z); });
    if (seconds > 0.0f) {
        // As it went (walls stop it): its legs follow that.
        const float k = 1.0f - std::exp(-seconds / 0.1f);
        moved[0] += ((body.position()[0] - before[0]) / seconds - moved[0]) * k;
        moved[1] += ((body.position()[2] - before[1]) / seconds - moved[1]) * k;
        // Stopped by a wall: it no longer pushes into it.
        velocity[0] = std::clamp(velocity[0], -std::fabs(moved[0]) - 0.5f, std::fabs(moved[0]) + 0.5f);
        velocity[1] = std::clamp(velocity[1], -std::fabs(moved[1]) - 0.5f, std::fabs(moved[1]) + 0.5f);
    }

    // Its way: the camera's while it moves; standing, past 60 degrees.
    const float off = std::remainder(yaw - way, TwoPi);
    if (pressed > 0.0f)
        turning = true;
    else if (std::fabs(off) > float(M_PI) / 3.0f)
        turning = true;
    if (turning) {
        const float rate = (pressed > 0.0f ? 10.0f : 4.0f) * seconds;
        way += std::clamp(off, -rate, rate);
        if (pressed == 0.0f && std::fabs(off) < 0.05f)
            turning = false;
    }
    way = std::remainder(way, TwoPi);

    // Its body's clips.
    if (!clipsSet && person >= 0 && crowd.kindReady(0)) {
        Humans::Locomotion::Clips clips;
        if (Humans::Locomotion::clipsOf(crowd, 0, clips)) {
            locomotion.setClips(clips);
            clipsSet = true;
            head = crowd.nodeNamed(0, QStringLiteral("head"));
        }
    }
    const float f[2] = {std::sin(way), std::cos(way)}, l[2] = {std::cos(way), -std::sin(way)};
    locomotion.move(moved[0] * l[0] + moved[1] * l[1], moved[0] * f[0] + moved[1] * f[1], crouching);
    // In the air after a jump, or falling: its jump's loop until it lands.
    const bool airborne = !body.onGround() && (jump || body.verticalSpeed() < -2.0f || jumping);
    if (airborne && !jumping && clipsSet) {
        const int loop = crowd.clipNamed(0, QStringLiteral("jump_loop"));
        if (loop >= 0)
            locomotion.act({{loop, crowd.clipLength(0, loop)}}, true);
        jumping = true;
    } else if (jumping && body.onGround()) {
        locomotion.endAction();
        jumping = false;
    }
    locomotion.update(seconds);

    // Drawn in the camera's tile's space.
    drawnAt[0] = body.position()[0] + offset[0];
    drawnAt[1] = body.position()[1];
    drawnAt[2] = body.position()[2] + offset[1];
    if (person >= 0) {
        const std::vector<Humans::Crowd::Play> &plays = locomotion.plays();
        crowd.control(person, drawnAt, way, plays.data(), int(plays.size()));
        Humans::Crowd::View view;
        std::copy(cameraAt, cameraAt + 3, view.camera);
        view.fieldOfView = Game::cameraFov;
        crowd.update(seconds, view);
    }

    // Its eyes: the head's place, a little up and ahead, smoothed.
    float target[3] = {body.position()[0], body.position()[1] + (crouching ? CrouchEyeHeight : EyeHeight),
                       body.position()[2]};
    float m[16];
    if (person >= 0 && head >= 0 && crowd.nodeMatrixOf(person, head, m)) {
        target[0] = m[12] - offset[0] + f[0] * EyeAheadOfHead;
        target[1] = m[13] + EyeOverHead;
        target[2] = m[14] - offset[1] + f[1] * EyeAheadOfHead;
    }
    if (!eyeSet || seconds <= 0.0f) {
        std::copy(target, target + 3, eye);
        eyeSet = true;
    } else {
        const float k = 1.0f - std::exp(-seconds / EyeLag);
        for (int i = 0; i < 3; ++i)
            eye[i] += (target[i] - eye[i]) * k;
    }

    // Third person: behind the eyes along the look, in front of what is
    // between.
    std::copy(eye, eye + 3, cameraPlace);
    if (behind && playerOwn)
        placeBehind(yaw, pitch, cameraPlace);
}

void Character::push(RenderQueue &queue) {
    if (person >= 0 && !rest)
        crowd.push(queue);
}

bool Character::placeNear(const int *tile, const float *at, int *placeTile, float *feet) {
    RouteCollision route;
    route.update(tile, at);
    const CollisionWorld &world = route.world();
    const CharacterBody::Settings body;
    std::vector<CollisionWorld::Contact> touching;
    const float walkable = std::cos(body.maxSlope * float(M_PI) / 180.0f);
    for (float radius = 0.0f; radius <= 60.0f; radius += 2.0f) {
        const int steps = radius == 0.0f ? 1 : std::max(8, int(TwoPi * radius / 2.0f));
        for (int i = 0; i < steps; ++i) {
            const float angle = TwoPi * float(i) / float(steps);
            const float x = at[0] + radius * std::sin(angle), z = at[2] + radius * std::cos(angle);
            if (!route.allowed(x, z))
                continue;
            CollisionWorld::Floor floor;
            if (!world.floorAt(x, z, at[1], at[1] - 500.0f, floor) || floor.normal[1] < walkable)
                continue;
            // Its body fits there.
            const float a[3] = {x, floor.height + body.step + body.radius, z};
            const float b[3] = {x, floor.height + body.height - body.radius, z};
            touching.clear();
            world.contacts(a, b, body.radius, touching);
            if (!touching.empty())
                continue;
            placeTile[0] = tile[0];
            placeTile[1] = tile[1];
            feet[0] = x;
            feet[1] = floor.height;
            feet[2] = z;
            return true;
        }
    }
    return false;
}

void Character::getCameraPosition(float *out) {
    out[0] = float(at[0]);
    out[1] = float(at[1]);
    out[2] = cameraPlace[0];
    out[3] = cameraPlace[1];
    out[4] = cameraPlace[2];
    out[5] = out[6] = out[7] = 0.0f;
}

bool Character::getCameraMount(CameraMount &mount) {
    // At the place given (no moving of its own), looking freely; the look
    // it had when it began to walk.
    mount = CameraMount();
    mount.position[0] = mount.position[1] = mount.position[2] = 0.0f;
    mount.look[0] = startLook[0];
    mount.look[1] = startLook[1];
    mount.id = 1;
    return true;
}

void Character::cameraAttached(bool attached) {
    if (!attached)
        keys->releaseKeys();
}

KeyTaker *Character::keyTaker() {
    return keys.get();
}

}
