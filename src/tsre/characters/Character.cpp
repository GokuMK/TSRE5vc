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
#include <tsre/humans/StationPeople.h>
#include <tsre/trains/CarAnimation.h>
#include <tsre/trains/CarPlacement.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/VehicleDefinition.h>
#include <QDebug>
#include <QGuiApplication>
#include <QKeyEvent>
#include <algorithm>
#include <limits>
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

// A point through a column-major matrix.
void transformPoint(const float *m, const float *p, float *out) {
    const float x = p[0], y = p[1], z = p[2];
    out[0] = m[0] * x + m[4] * y + m[8] * z + m[12];
    out[1] = m[1] * x + m[5] * y + m[9] * z + m[13];
    out[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
}

// A turn and a move undone (a car's matrix has no scale).
void rigidInverse(const float *m, float *out) {
    out[0] = m[0], out[1] = m[4], out[2] = m[8], out[3] = 0.0f;
    out[4] = m[1], out[5] = m[5], out[6] = m[9], out[7] = 0.0f;
    out[8] = m[2], out[9] = m[6], out[10] = m[10], out[11] = 0.0f;
    out[12] = -(out[0] * m[12] + out[4] * m[13] + out[8] * m[14]);
    out[13] = -(out[1] * m[12] + out[5] * m[13] + out[9] * m[14]);
    out[14] = -(out[2] * m[12] + out[6] * m[13] + out[10] * m[14]);
    out[15] = 1.0f;
}

// A vector over the floor (x, z) turned by a yaw (as the camera's: a way w
// becomes w + yaw).
void turnBy(float *v, float yaw) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float x = v[0] * c + v[1] * s, z = -v[0] * s + v[1] * c;
    v[0] = x;
    v[1] = z;
}

// The car a run's train has at an index, null for none.
Eng *carOf(int train, int car) {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    Consist *consist = simulation != nullptr ? simulation->consistOf(train) : nullptr;
    if (consist == nullptr || car < 0 || car >= consist->engItems.size())
        return nullptr;
    return consist->engItems[car].engPointer;
}
}

class Character::Keys : public KeyTaker {
public:
    bool forward = false, back = false, left = false, right = false, run = false, slow = false;
    bool jumpAsked = false, crouchAsked = false, viewAsked = false, useAsked = false;
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
        case Qt::Key_E:
            useAsked = useAsked || first;
            return true;
        default:
            return false;
        }
    }
};

Character::Character() : keys(std::make_unique<Keys>()) {
}

Character::~Character() {
    if (seated >= 0)
        Humans::StationPeople::instance().setPlayerSeat(-1, -1, -1);
    crowd.clear();
}

void Character::start(const int *tile, const float *feet, float yaw, float pitch) {
    script = Script();
    driving = false;
    askDrive = -1;
    if (seated >= 0) {
        seated = -1;
        Humans::StationPeople::instance().setPlayerSeat(-1, -1, -1);
        locomotion.endAction();
    }
    if (inCar()) {
        ride = Ride();
        carWorld.clear();
    }
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
    std::copy(feet, feet + 3, worldFeet);
    std::copy(eye, eye + 3, worldEye);
    std::copy(eye, eye + 3, cameraPlace);
    loadHuman();
}

void Character::start(const Place &place, float yaw, float pitch) {
    start(place.tile, place.feet, yaw, pitch);
    if (place.train < 0)
        return;
    enterCar(place.train, place.car);
    if (inCar())
        body.setPosition(place.inCar);
}

void Character::setControlled(bool on) {
    playerOwn = on;
    if (on)
        return;
    keys->releaseKeys();
    keys->jumpAsked = keys->crouchAsked = keys->viewAsked = keys->useAsked = false;
    usables.clear();
    aim = -1;
}

void Character::lookItsWay() {
    startLook[0] = way;
    startLook[1] = 0.0f;
}

float Character::facing() const {
    return std::remainder(way + (inCar() ? ride.yaw : 0.0f), TwoPi);
}

void Character::flyPlace(float *out) const {
    float local[3];
    placeBehind(look[0] - ride.yaw, look[1] - ride.pitch, local);
    toWorld(local, out);
}

void Character::placeBehind(float yaw, float pitch, float *out) const {
    const float dir[3] = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    const float back[3] = {-dir[0], -dir[1], -dir[2]};
    float distance = BehindDistance, normal[3];
    if (frameWorld().ray(eye, back, BehindDistance, &distance, normal))
        distance = std::max(0.2f, distance - 0.2f);
    for (int i = 0; i < 3; ++i)
        out[i] = eye[i] + back[i] * distance;
}

void Character::toWorld(const float *in, float *out) const {
    if (inCar()) {
        transformPoint(ride.matrix, in, out);
    } else if (out != in) {
        std::copy(in, in + 3, out);
    }
}

void Character::toFrame(const float *in, float *out) const {
    if (inCar()) {
        transformPoint(ride.inverse, in, out);
    } else if (out != in) {
        std::copy(in, in + 3, out);
    }
}

// Its car's matrix now in its tile (the trains were placed for this tick
// before it), its riding frame; false when its car is gone (the train
// finished, its cars made again).
bool Character::placeCar() {
    Eng *eng = carOf(ride.train, ride.car);
    if (eng == nullptr || eng != ride.eng)
        return false;
    const float tile[2] = {float(at[0]), float(at[1])};
    if (!eng->placedMatrix(tile, ride.matrix))
        return false;
    rigidInverse(ride.matrix, ride.inverse);
    const float *m = ride.matrix;
    ride.yaw = std::atan2(m[8], m[10]);
    const float along = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
    ride.pitch = along > 0.0f ? std::asin(std::clamp(m[9] / along, -1.0f, 1.0f)) : 0.0f;
    // The cant: its up leaning to its left (the camera's up turns the
    // other way for a positive cant).
    const float lean = m[4] * std::cos(ride.yaw) - m[6] * std::sin(ride.yaw);
    ride.cant = -std::asin(std::clamp(lean, -1.0f, 1.0f));
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->viewFrame();
    ride.moving = frame && ride.train < int(frame->trains.size())
            && std::fabs(frame->trains[size_t(ride.train)].state.speed) > 0.05f;
    return true;
}

// Into a car's frame: its place, eyes, way and velocity turned into it;
// a camera riding it keeps looking where it looked.
void Character::enterCar(int train, int car) {
    Eng *eng = carOf(train, car);
    if (eng == nullptr)
        return;
    const float feetNow[3] = {body.position()[0], body.position()[1], body.position()[2]};
    const float eyeNow[3] = {eye[0], eye[1], eye[2]};
    ride = Ride();
    ride.train = train;
    ride.car = car;
    ride.eng = eng;
    if (!placeCar()) {
        ride = Ride();
        return;
    }
    if (carWorld.car() != eng)
        carWorld.build(eng);
    float local[3];
    toFrame(feetNow, local);
    body.setPosition(local);
    toFrame(eyeNow, eye);
    turnBy(velocity, -ride.yaw);
    turnBy(moved, -ride.yaw);
    way = std::remainder(way - ride.yaw, TwoPi);
    startLook[0] = look[0] - ride.yaw;
    startLook[1] = look[1] - ride.pitch;
    qInfo().noquote() << QString("Character: in train %1's car %2 (%3, %4, %5 in it), %6 meshes")
                         .arg(train).arg(car).arg(double(local[0]), 0, 'f', 2).arg(double(local[1]), 0, 'f', 2)
                         .arg(double(local[2]), 0, 'f', 2).arg(carWorld.meshes());
}

// Back in the world: where the car has it now, its way and velocity turned
// out of it.
void Character::leaveCar() {
    driving = false;
    if (seated >= 0) {
        seated = -1;
        Humans::StationPeople::instance().setPlayerSeat(-1, -1, -1);
        locomotion.endAction();
    }
    float feetNow[3], eyeNow[3];
    toWorld(body.position(), feetNow);
    toWorld(eye, eyeNow);
    const float yaw = ride.yaw;
    const int train = ride.train, car = ride.car;
    ride = Ride();
    carWorld.clear();
    body.setPosition(feetNow);
    std::copy(eyeNow, eyeNow + 3, eye);
    turnBy(velocity, yaw);
    turnBy(moved, yaw);
    way = std::remainder(way + yaw, TwoPi);
    startLook[0] = look[0];
    startLook[1] = look[1];
    route.update(at, feetNow);
    qInfo().noquote() << QString("Character: out of train %1's car %2 at tile %3 %4 (%5, %6, %7)")
                         .arg(train).arg(car).arg(at[0]).arg(at[1]).arg(double(feetNow[0]), 0, 'f', 2)
                         .arg(double(feetNow[1]), 0, 'f', 2).arg(double(feetNow[2]), 0, 'f', 2);
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

// Into the tile its feet are in: the frame goes with it (in a car, only
// the car's matrix: its place in the car stays).
void Character::keepInTile() {
    float w[3];
    toWorld(body.position(), w);
    const int dx = int(std::floor((w[0] + 1024.0f) / 2048.0f)), dz = int(std::floor((w[2] + 1024.0f) / 2048.0f));
    if (dx == 0 && dz == 0)
        return;
    at[0] += dx;
    at[1] += dz;
    if (inCar()) {
        placeCar();
        return;
    }
    const float by[3] = {-2048.0f * float(dx), 0.0f, -2048.0f * float(dz)};
    body.shift(by);
    for (float *v : {eye, cameraPlace}) {
        v[0] += by[0];
        v[2] += by[2];
    }
}

void Character::update(float seconds, float yaw, float pitch, const int *cameraTile, const float *cameraAt) {
    seconds = std::clamp(seconds, 0.0f, 0.1f);
    if (person == -1)
        loadHuman();
    // Its car's place now; its car gone: on the ground where it was.
    if (inCar() && !placeCar())
        leaveCar();
    keepInTile();
    toWorld(body.position(), worldFeet);
    // Left far from the camera, standing still on the ground: at rest.
    const float offset[2] = {float(at[0] - cameraTile[0]) * 2048.0f, float(at[1] - cameraTile[1]) * 2048.0f};
    rest = !playerOwn && body.onGround() && std::hypot(velocity[0], velocity[1]) < 0.01f
            && std::hypot(worldFeet[0] + offset[0] - cameraAt[0], worldFeet[2] + offset[1] - cameraAt[2])
                    > RestDistance;
    if (rest)
        return;
    if (!inCar())
        route.update(at, body.position());
    else
        carWorld.readPassages();

    // The look in its frame: the player's as the camera looks; one left,
    // its way.
    float lyaw = way, lpitch = 0.0f;
    if (playerOwn) {
        look[0] = yaw;
        look[1] = pitch;
        lyaw = yaw - ride.yaw;
        lpitch = pitch - ride.pitch;
    }

    // Moving by itself (through a door): its keys wait. The train moving
    // off meanwhile (its doors shut): through at once, out on the ground
    // or in the car.
    if (busy() && script.door && inCar() && ride.moving)
        playScript(1.0e6f);
    // Seated: Space stands it up; it stays on its seat meanwhile.
    if (seated >= 0 && !busy() && playerOwn && keys->jumpAsked)
        standUp();
    const bool scripted = (busy() && playScript(seconds)) || seated >= 0 || driving;
    if (seated >= 0 && !busy())
        body.setPosition(sitAt);
    if (scripted)
        keys->jumpAsked = keys->crouchAsked = keys->useAsked = false;
    // What the keys ask: a way and a speed, as the camera looks (none of
    // one left).
    if (!playerOwn)
        keys->jumpAsked = keys->crouchAsked = keys->viewAsked = keys->useAsked = false;
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
    if (playerOwn && !scripted) {
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
        const float f[2] = {std::sin(lyaw), std::cos(lyaw)}, l[2] = {std::cos(lyaw), -std::sin(lyaw)};
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
    if (scripted) {
        // Its place set by its legs.
    } else if (inCar()) {
        // A moving car keeps it within its footprint.
        const CarCollision &carNow = carWorld;
        const float margin = body.settings().radius;
        std::function<bool(float, float)> keep;
        if (ride.moving)
            keep = [&carNow, margin](float x, float z) { return carNow.within(x, z, margin); };
        body.move(carWorld.world(), velocity[0], velocity[1], seconds, jump, keep);
    } else {
        body.move(route.world(), velocity[0], velocity[1], seconds, jump,
                  [this](float x, float z) { return route.allowed(x, z); });
    }
    if (seconds > 0.0f && !scripted) {
        // As it went (walls stop it): its legs follow that.
        const float k = 1.0f - std::exp(-seconds / 0.1f);
        moved[0] += ((body.position()[0] - before[0]) / seconds - moved[0]) * k;
        moved[1] += ((body.position()[2] - before[1]) / seconds - moved[1]) * k;
        // Stopped by a wall: it no longer pushes into it.
        velocity[0] = std::clamp(velocity[0], -std::fabs(moved[0]) - 0.5f, std::fabs(moved[0]) + 0.5f);
        velocity[1] = std::clamp(velocity[1], -std::fabs(moved[1]) - 0.5f, std::fabs(moved[1]) + 0.5f);
    }
    // Its frame by the floor under its feet: a car's in the world puts it
    // in that car; off a standing car's footprint, back in the world.
    if (scripted) {
    } else if (!inCar()) {
        const RouteCollision::PlacedCar *placed = body.onGround() ? route.carOf(body.floor().placed) : nullptr;
        if (placed != nullptr) {
            const float yawBefore = ride.yaw;
            enterCar(placed->train, placed->car);
            lyaw -= ride.yaw - yawBefore;
            lpitch -= ride.pitch;
        }
    } else if (!body.onGround() && !carWorld.within(body.position()[0], body.position()[2])) {
        lyaw += ride.yaw;
        lpitch += ride.pitch;
        leaveCar();
    }

    // Its way: the camera's while it moves; standing, past 60 degrees.
    const float off = std::remainder(lyaw - way, TwoPi);
    if (scripted)
        turning = false;
    else if (pressed > 0.0f)
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
    {
        const float f[2] = {std::sin(way), std::cos(way)}, l[2] = {std::cos(way), -std::sin(way)};
        locomotion.move(moved[0] * l[0] + moved[1] * l[1], moved[0] * f[0] + moved[1] * f[1], crouching);
    }
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

    // In the world: where its frame has it now.
    toWorld(body.position(), worldFeet);
    worldMoved[0] = moved[0];
    worldMoved[1] = moved[1];
    turnBy(worldMoved, ride.yaw);
    const float worldWay = facing();
    const float fw[2] = {std::sin(worldWay), std::cos(worldWay)};
    // Drawn in the camera's tile's space.
    drawnAt[0] = worldFeet[0] + offset[0];
    drawnAt[1] = worldFeet[1];
    drawnAt[2] = worldFeet[2] + offset[1];
    if (person >= 0) {
        const std::vector<Humans::Crowd::Play> &plays = locomotion.plays();
        crowd.control(person, drawnAt, worldWay, plays.data(), int(plays.size()));
        Humans::Crowd::View view;
        std::copy(cameraAt, cameraAt + 3, view.camera);
        view.fieldOfView = Game::cameraFov;
        crowd.update(seconds, view);
    }

    // Its eyes: the head's place, a little up and ahead, smoothed in its
    // frame (a car's: the train's speed is not lag).
    float target[3] = {body.position()[0], body.position()[1] + (crouching ? CrouchEyeHeight : EyeHeight),
                       body.position()[2]};
    float m[16];
    if (person >= 0 && head >= 0 && crowd.nodeMatrixOf(person, head, m)) {
        const float headAt[3] = {m[12] - offset[0] + fw[0] * EyeAheadOfHead, m[13] + EyeOverHead,
                                 m[14] - offset[1] + fw[1] * EyeAheadOfHead};
        toFrame(headAt, target);
    }
    if (!eyeSet || seconds <= 0.0f) {
        std::copy(target, target + 3, eye);
        eyeSet = true;
    } else {
        const float k = 1.0f - std::exp(-seconds / EyeLag);
        for (int i = 0; i < 3; ++i)
            eye[i] += (target[i] - eye[i]) * k;
    }
    toWorld(eye, worldEye);

    // Third person: behind the eyes along the look, in front of what is
    // between.
    std::copy(worldEye, worldEye + 3, cameraPlace);
    if (behind && playerOwn) {
        float local[3];
        placeBehind(lyaw, lpitch, local);
        toWorld(local, cameraPlace);
    }

    // What it may use, what it aims at; E uses it.
    findUsables();
    aimAt(playerOwn ? yaw : look[0], playerOwn ? pitch : look[1]);
    if (keys->useAsked) {
        keys->useAsked = false;
        use();
    }
}

// Its car's doors (open, the train standing: stepping off), or every open
// door of a standing train's car about it (boarding).
void Character::findUsables() {
    usables.clear();
    aim = -1;
    if (!playerOwn || busy())
        return;
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->viewFrame();
    const auto standing = [&frame](int train) {
        return frame && train >= 0 && train < int(frame->trains.size())
                && std::fabs(frame->trains[size_t(train)].state.speed) < 0.05f;
    };
    std::vector<Humans::CarDoors::Door> doors;
    const auto addDoors = [&](int train, int car, Eng *eng, Usable::Kind kind) {
        if (eng == nullptr || !standing(train) || !Humans::StationPeople::doorsOfCar(*eng, doors))
            return;
        const bool left = eng->isAnimated(CarAnimation::DoorLeft), right = eng->isAnimated(CarAnimation::DoorRight);
        for (size_t i = 0; i < doors.size(); ++i) {
            const Humans::CarDoors::Door &d = doors[i];
            const bool open = d.group == CarAnimation::DoorLeft ? left : d.group == CarAnimation::DoorRight ? right
                                                                                                             : left || right;
            if (!open)
                continue;
            Usable u;
            u.kind = kind;
            u.label = kind == Usable::Board ?
                //% "Board"
                qtTrId("characters.use.board") :
                //% "Step off"
                qtTrId("characters.use.step.off");
            u.train = train;
            u.car = car;
            u.index = int(i);
            const float bottom = std::isnan(d.bottom) ? CarCollision::AssumedFloor - CarPlacement::RailHeight : d.bottom;
            u.centre[0] = d.x;
            u.centre[1] = bottom + 0.9f;
            u.centre[2] = d.z;
            u.radius = std::max(0.6f, d.width * 0.5f);
            usables.push_back(u);
        }
    };
    // A cab's driver's place and its doors (step 5).
    const auto addCab = [&](int train, int car, Eng *eng, bool inside) {
        const std::shared_ptr<const VehicleDefinition::Cab3D> cab = eng != nullptr && eng->definition
                ? eng->definition->cab3DOf() : nullptr;
        if (!cab)
            return;
        const float half = eng->definition->size[0] > 0.1f ? eng->definition->size[0] / 2.0f : 1.5f;
        for (int vp = 0; vp < cab->viewpoints.size(); ++vp) {
            float head[3];
            if (!cabHead(eng, vp, head) || eng->cab3DShapePath(vp).isEmpty())
                continue;
            if (inside && !driving && simulation->drivable(train)) {
                Usable u;
                u.kind = Usable::Drive;
                //% "Drive"
                u.label = qtTrId("characters.use.drive");
                u.train = train;
                u.car = car;
                u.index = vp;
                u.centre[0] = head[0];
                u.centre[1] = head[1] - 0.35f;
                u.centre[2] = head[2];
                u.radius = 0.45f;
                usables.push_back(u);
            }
            if (!standing(train))
                continue;
            for (const float side : {-1.0f, 1.0f}) {
                Usable u;
                u.kind = inside ? Usable::CabOut : Usable::CabIn;
                u.label = inside ?
                    //% "Get out"
                    qtTrId("characters.use.cab.out") :
                    //% "Climb in"
                    qtTrId("characters.use.cab.in");
                u.train = train;
                u.car = car;
                u.index = vp;
                u.centre[0] = side * (half - (inside ? 0.15f : 0.0f));
                u.centre[1] = head[1] - (inside ? 0.6f : 1.2f);
                u.centre[2] = head[2];
                u.radius = 0.6f;
                usables.push_back(u);
            }
        }
    };
    if (inCar()) {
        addDoors(ride.train, ride.car, ride.eng, Usable::StepOff);
        addCab(ride.train, ride.car, ride.eng, true);
        // Its car's free seats (no rider's).
        const std::shared_ptr<const ShapeSeats::Seats> seats = Humans::StationPeople::seatsOfCar(*ride.eng);
        if (seated < 0 && seats) {
            const Humans::StationPeople &people = Humans::StationPeople::instance();
            for (int i = 0; i < seats->seats.size(); ++i) {
                const ShapeSeats::Seat &seatAt = seats->seats[i];
                if (std::hypot(seatAt.position[0] - body.position()[0], seatAt.position[2] - body.position()[2]) > UseReach + 1.0f
                        || !people.seatFree(ride.train, ride.car, i))
                    continue;
                Usable u;
                u.kind = Usable::Sit;
                //% "Sit down"
                u.label = qtTrId("characters.use.sit");
                u.train = ride.train;
                u.car = ride.car;
                u.index = i;
                u.centre[0] = seatAt.position[0];
                u.centre[1] = seatAt.position[1] + 0.15f;
                u.centre[2] = seatAt.position[2];
                u.radius = 0.35f;
                usables.push_back(u);
            }
        }
        return;
    }
    for (const RouteCollision::PlacedCar &placed : route.cars()) {
        addDoors(placed.train, placed.car, carOf(placed.train, placed.car), Usable::Board);
        addCab(placed.train, placed.car, carOf(placed.train, placed.car), false);
    }
}

bool Character::cabHead(const Eng *eng, int viewpoint, float *head) {
    const std::shared_ptr<const VehicleDefinition::Cab3D> cab = eng != nullptr && eng->definition
            ? eng->definition->cab3DOf() : nullptr;
    if (!cab || viewpoint < 0 || viewpoint >= cab->viewpoints.size())
        return false;
    const float *p = cab->viewpoints[viewpoint].position;
    head[0] = cab->mirrored ? -p[0] : p[0];
    head[1] = p[1];
    head[2] = p[2];
    return true;
}

// The cab's floor about a head (its car's collision): the lowest of the
// highest floors round it between knee and foot below a driver's eyes (a
// seat's cushion and the desk are higher than the floor beside them), else
// a seated driver's eyes over it.
float Character::cabFloor(const float *head) const {
    const float around[][2] = {{0.0f, 0.0f}, {0.5f, 0.0f}, {-0.5f, 0.0f}, {0.0f, 0.5f}, {0.0f, -0.5f},
                               {-head[0], 0.0f}, {-head[0] * 0.5f, 0.4f}, {-head[0] * 0.5f, -0.4f}};
    float lowest = std::numeric_limits<float>::max();
    for (const auto &off : around) {
        CollisionWorld::Floor floor;
        if (carWorld.world().floorAt(head[0] + off[0], head[2] + off[1], head[1] - 0.6f, head[1] - 2.4f, floor))
            lowest = std::min(lowest, floor.height);
    }
    return lowest < std::numeric_limits<float>::max() ? lowest : head[1] - 1.65f;
}

void Character::driveTaken(bool taken) {
    if (taken && inCar()) {
        float head[3];
        if (cabHead(ride.eng, askViewpoint, head)) {
            script = Script();
            if (seated >= 0) {
                seated = -1;
                Humans::StationPeople::instance().setPlayerSeat(-1, -1, -1);
            }
            locomotion.endAction();
            const float place[3] = {head[0], cabFloor(head), head[2]};
            body.setPosition(place);
            way = head[2] >= 0.0f ? 0.0f : float(M_PI);
            driving = true;
            drivePlace = askViewpoint;
            velocity[0] = velocity[1] = moved[0] = moved[1] = 0.0f;
            qInfo().noquote() << QString("Character: at the driver's place of train %1's car %2 (viewpoint %3)")
                                 .arg(ride.train).arg(ride.car).arg(drivePlace);
        }
    }
    askDrive = -1;
    askViewpoint = -1;
}

bool Character::sitAtDriverPlace(const int *tile, int train, int car, int viewpoint) {
    Eng *eng = carOf(train, car);
    float head[3], matrix[16];
    const float tileAt[2] = {float(tile[0]), float(tile[1])};
    if (eng == nullptr || !cabHead(eng, viewpoint, head) || !eng->placedMatrix(tileAt, matrix))
        return false;
    Place place;
    place.tile[0] = tile[0];
    place.tile[1] = tile[1];
    place.train = train;
    place.car = car;
    place.inCar[0] = head[0];
    place.inCar[1] = head[1] - 1.65f;
    place.inCar[2] = head[2];
    transformPoint(matrix, place.inCar, place.feet);
    const float yaw = std::atan2(matrix[8], matrix[10]) + (head[2] >= 0.0f ? 0.0f : float(M_PI));
    start(place, yaw, 0.0f);
    if (!inCar())
        return false;
    askViewpoint = viewpoint;
    driveTaken(true);
    return driving;
}

// Up from the driver's place: beside it in the cab, standing, facing the
// cab's front (the camera at its eyes again).
bool Character::standInCab() {
    if (!driving || !inCar())
        return false;
    float head[3];
    if (!cabHead(ride.eng, drivePlace, head))
        return false;
    // Beside it in the cab: places round the seat (within the cab's head
    // box when it has one), nearest first, on the cab's floor (not a seat
    // or the desk), where a slim body fits; else where it sits.
    const float floorY = cabFloor(head);
    const std::shared_ptr<const VehicleDefinition::Cab3D> cab = ride.eng->definition->cab3DOf();
    float box[6] = {head[0] - 1.0f, 0.0f, head[2] - 1.0f, head[0] + 1.0f, 0.0f, head[2] + 1.0f};
    if (cab && drivePlace < cab->viewpoints.size() && cab->viewpoints[drivePlace].hasBox) {
        const float *b = cab->viewpoints[drivePlace].box;
        const float sign = cab->mirrored ? -1.0f : 1.0f;
        box[0] = std::min(sign * b[0], sign * b[3]) - 0.3f;
        box[3] = std::max(sign * b[0], sign * b[3]) + 0.3f;
        box[2] = std::min(b[2], b[5]) - 0.3f;
        box[5] = std::max(b[2], b[5]) + 0.3f;
    }
    std::vector<std::pair<float, std::pair<float, float>>> tried;
    for (float x = box[0]; x <= box[3] + 1e-3f; x += 0.15f)
        for (float z = box[2]; z <= box[5] + 1e-3f; z += 0.15f) {
            const float d = std::hypot(x - head[0], z - head[2]);
            if (d >= 0.4f && carWorld.within(x, z, 0.25f))
                tried.push_back({d, {x, z}});
        }
    std::sort(tried.begin(), tried.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    tried.push_back({0.0f, {head[0], head[2]}});
    const CharacterBody::Settings &set = body.settings();
    constexpr float Slim = 0.22f;
    std::vector<CollisionWorld::Contact> touching;
    for (const auto &entry : tried) {
        const float xz[2] = {entry.second.first, entry.second.second};
        const bool last = &entry == &tried.back();
        CollisionWorld::Floor floor;
        if (!carWorld.world().floorAt(xz[0], xz[1], floorY + 0.3f, floorY - 0.3f, floor) && !last)
            continue;
        const float y = last ? floorY : floor.height;
        if (!last && std::fabs(y - floorY) > 0.15f)
            continue;
        const float a[3] = {xz[0], y + set.step + Slim, xz[1]};
        const float b[3] = {xz[0], y + set.height - Slim, xz[1]};
        touching.clear();
        carWorld.world().contacts(a, b, Slim, touching);
        if (!touching.empty() && !last)
            continue;
        const float place[3] = {xz[0], y, xz[1]};
        body.setPosition(place);
        driving = false;
        way = head[2] >= 0.0f ? 0.0f : float(M_PI);
        lookItsWay();
        eyeSet = false;
        qInfo().noquote() << QString("Character: stands in train %1's car %2's cab (%3, %4, %5 in it)")
                             .arg(ride.train).arg(ride.car).arg(double(place[0]), 0, 'f', 2)
                             .arg(double(place[1]), 0, 'f', 2).arg(double(place[2]), 0, 'f', 2);
        return true;
    }
    return false;
}

// Into a cab by its side (from the ground) or out of it: up or down its
// steps (the steps clip, three steps), its place held meanwhile.
void Character::climb(const Usable &door, bool in) {
    if (in) {
        enterCar(door.train, door.car);
        if (!inCar() || ride.car != door.car || ride.train != door.train)
            return;
    } else {
        if (!inCar())
            return;
        route.update(at, worldFeet);
    }
    float head[3];
    if (!cabHead(ride.eng, door.index, head))
        return;
    const float side = door.centre[0] >= 0.0f ? 1.0f : -1.0f;
    const float half = std::max(carWorld.halfWidth(), 1.0f);
    const float floorY = cabFloor(head);
    // Beside it (in its frame): the first floor a step down reaches going
    // out (a platform past its edge's gap), else the ground.
    float out[3] = {side * (half + 0.7f), floorY, head[2]};
    float w[3];
    CollisionWorld::Floor floor;
    bool found = false;
    for (float away = 0.4f; away <= 2.01f && !found; away += 0.2f) {
        const float probe[3] = {side * (half + away), floorY + 0.3f, head[2]};
        transformPoint(ride.matrix, probe, w);
        const float sillY = w[1] - 0.3f;
        if (route.world().floorAt(w[0], w[2], w[1], w[1] - 4.0f, floor) && route.carOf(floor.placed) == nullptr
                && floor.height > sillY - 1.3f && floor.height < sillY + 0.1f) {
            w[1] = floor.height;
            transformPoint(ride.inverse, w, out);
            found = true;
        }
    }
    if (!found) {
        transformPoint(ride.matrix, out, w);
        if (route.world().floorAt(w[0], w[2], w[1], w[1] - 4.0f, floor) && route.carOf(floor.placed) == nullptr)
            w[1] = floor.height;
        else
            w[1] -= 1.6f;
        transformPoint(ride.inverse, w, out);
    }
    if (in)
        out[1] = body.position()[1];
    const float sill[3] = {side * (half - 0.6f), floorY, head[2]};
    const int steps = std::clamp(int(std::lround((floorY - out[1]) / 0.35f)), 0, 3);
    const int clip = steps > 0 && person >= 0
            ? crowd.clipNamed(0, QString::fromLatin1(in ? "steps_up_%1" : "steps_down_%1").arg(steps)) : -1;
    const float inward = -side * float(M_PI) / 2.0f, outward = side * float(M_PI) / 2.0f;
    script = Script();
    std::copy(body.position(), body.position() + 3, script.from);
    if (in) {
        Leg to;
        std::copy(out, out + 3, to.to);
        to.way = std::atan2(out[0] - body.position()[0], out[2] - body.position()[2]);
        to.seconds = std::max(0.2f, std::hypot(out[0] - body.position()[0], out[2] - body.position()[2]) / 1.2f);
        script.legs.push_back(to);
        Leg up;
        std::copy(sill, sill + 3, up.to);
        up.clip = clip;
        up.way = inward;
        up.seconds = clip >= 0 ? crowd.clipLength(0, clip) : 0.8f;
        script.legs.push_back(up);
        Leg on;
        on.to[0] = side * (half - 1.0f);
        on.to[1] = floorY;
        on.to[2] = head[2];
        on.way = inward;
        on.seconds = 0.5f;
        script.legs.push_back(on);
    } else {
        Leg to;
        std::copy(sill, sill + 3, to.to);
        to.way = std::atan2(sill[0] - body.position()[0], sill[2] - body.position()[2]);
        to.seconds = std::max(0.2f, std::hypot(sill[0] - body.position()[0], sill[2] - body.position()[2]) / 1.0f);
        script.legs.push_back(to);
        Leg down;
        std::copy(out, out + 3, down.to);
        down.clip = clip;
        down.way = outward;
        down.seconds = clip >= 0 ? crowd.clipLength(0, clip) : 0.8f;
        script.legs.push_back(down);
        script.leaveAfter = true;
    }
    script.door = true;
    crouching = false;
    velocity[0] = velocity[1] = 0.0f;
}

// The nearest usable thing the eyes' ray meets within reach (in each car's
// frame as placed now).
void Character::aimAt(float yaw, float pitch) {
    aim = -1;
    if (usables.empty())
        return;
    const float dir[3] = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    const float tile[2] = {float(at[0]), float(at[1])};
    float best = UseReach;
    int lastTrain = -2, lastCar = -2;
    float inverse[16];
    bool placed = false;
    for (size_t i = 0; i < usables.size(); ++i) {
        const Usable &u = usables[i];
        if (u.train != lastTrain || u.car != lastCar) {
            lastTrain = u.train;
            lastCar = u.car;
            float matrix[16];
            placed = false;
            if (inCar() && u.train == ride.train && u.car == ride.car) {
                std::copy(ride.inverse, ride.inverse + 16, inverse);
                placed = true;
            } else if (Eng *eng = carOf(u.train, u.car); eng != nullptr && eng->placedMatrix(tile, matrix)) {
                rigidInverse(matrix, inverse);
                placed = true;
            }
        }
        if (!placed)
            continue;
        float o[3];
        transformPoint(inverse, worldEye, o);
        const float d[3] = {inverse[0] * dir[0] + inverse[4] * dir[1] + inverse[8] * dir[2],
                            inverse[1] * dir[0] + inverse[5] * dir[1] + inverse[9] * dir[2],
                            inverse[2] * dir[0] + inverse[6] * dir[1] + inverse[10] * dir[2]};
        const float v[3] = {u.centre[0] - o[0], u.centre[1] - o[1], u.centre[2] - o[2]};
        const float along = v[0] * d[0] + v[1] * d[1] + v[2] * d[2];
        if (along < 0.0f)
            continue;
        const float across = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] - along * along;
        if (across > u.radius * u.radius)
            continue;
        const float hit = along - std::sqrt(std::max(0.0f, u.radius * u.radius - across));
        if (hit < best) {
            best = hit;
            aim = int(i);
        }
    }
}

bool Character::useNearest(Usable::Kind kind) {
    if (busy())
        return false;
    if (usables.empty()) {
        const bool own = playerOwn;
        playerOwn = true;
        findUsables();
        playerOwn = own;
    }
    const float tile[2] = {float(at[0]), float(at[1])};
    float best = 6.0f;
    int found = -1;
    for (size_t i = 0; i < usables.size(); ++i) {
        const Usable &u = usables[i];
        float matrix[16], w[3];
        Eng *eng = carOf(u.train, u.car);
        if (u.kind != kind || eng == nullptr || !eng->placedMatrix(tile, matrix))
            continue;
        transformPoint(matrix, u.centre, w);
        const float d = std::hypot(w[0] - worldFeet[0], w[2] - worldFeet[2]);
        if (d < best) {
            best = d;
            found = int(i);
        }
    }
    if (found < 0) {
        static const bool log = qEnvironmentVariable("TSRE_CHARACTER_LOG") == "1";
        if (log) {
            const Eng *eng = inCar() ? ride.eng : nullptr;
            const auto cab = eng != nullptr && eng->definition ? eng->definition->cab3DOf() : nullptr;
            qInfo().noquote() << QString("Character: nothing of kind %1 among %2 usables; its car's cab: %3 viewpoints, "
                                         "%4, drivable %5")
                                 .arg(int(kind)).arg(usables.size()).arg(cab ? cab->viewpoints.size() : -1)
                                 .arg(eng != nullptr ? eng->cab3DShapePath(-1) : QString())
                                 .arg(Sim::SimulationController::instance()->drivable(ride.train));
        }
        return false;
    }
    aim = found;
    return use();
}

bool Character::use() {
    const Usable *aimedAt = aimed();
    if (aimedAt == nullptr || busy())
        return false;
    const Usable u = *aimedAt;
    qInfo().noquote() << QString("Character: uses %1 (train %2's car %3, %4)").arg(u.label).arg(u.train).arg(u.car)
                         .arg(u.index);
    if (u.kind == Usable::Board)
        board(u);
    else if (u.kind == Usable::StepOff)
        stepOff(u);
    else if (u.kind == Usable::Sit)
        sitDown(u);
    else if (u.kind == Usable::Drive) {
        askDrive = u.train;
        askViewpoint = u.index;
    } else if (u.kind == Usable::CabOut || u.kind == Usable::CabIn)
        climb(u, u.kind == Usable::CabIn);
    usables.clear();
    aim = -1;
    return true;
}

bool Character::doorPlaces(Eng *eng, int index, const float *carMatrix, float *sill, float *inside, float *outside,
                           float *side) const {
    std::vector<Humans::CarDoors::Door> doors;
    if (eng == nullptr || !Humans::StationPeople::doorsOfCar(*eng, doors) || index < 0 || index >= int(doors.size()))
        return false;
    const Humans::CarDoors::Door &d = doors[size_t(index)];
    *side = d.x >= 0.0f ? 1.0f : -1.0f;
    const float bottom = std::isnan(d.bottom) ? CarCollision::AssumedFloor - CarPlacement::RailHeight : d.bottom;
    sill[0] = d.x - *side * 0.15f;
    sill[1] = bottom;
    sill[2] = d.z;
    inside[0] = d.x - *side * 0.9f;
    inside[1] = bottom;
    inside[2] = d.z;
    // Outside: the first floor beyond its face a step down reaches (the
    // platform: its edge may be a gap away, as StationPeople probes out to
    // its walk area), else the ground 0.6 m out, else a train's floor below.
    float inverse[16];
    rigidInverse(carMatrix, inverse);
    float sillWorld[3];
    transformPoint(carMatrix, sill, sillWorld);
    CollisionWorld::Floor floor;
    bool found = false;
    static const bool log = qEnvironmentVariable("TSRE_CHARACTER_LOG") == "1";
    for (float away = 0.4f; away <= 2.01f && !found; away += 0.2f) {
        const float out[3] = {d.x + *side * away, bottom + 0.3f, d.z};
        float w[3];
        transformPoint(carMatrix, out, w);
        const bool any = route.world().floorAt(w[0], w[2], w[1], w[1] - 2.5f, floor);
        if (log)
            qInfo().noquote() << QString("Character: door %1 probed %2 m out (%3, %4, %5): %6, sill %7, %8 placed, %9 cars")
                                 .arg(index).arg(double(away), 0, 'f', 1).arg(double(w[0]), 0, 'f', 2)
                                 .arg(double(w[1]), 0, 'f', 2).arg(double(w[2]), 0, 'f', 2)
                                 .arg(any ? QString("floor %1 (placed %2%3)").arg(double(floor.height), 0, 'f', 2)
                                                .arg(floor.placed).arg(route.carOf(floor.placed) ? ", a car" : "")
                                          : QString("no floor"))
                                 .arg(double(sillWorld[1]), 0, 'f', 2).arg(route.world().count())
                                 .arg(route.cars().size())
                             + QString(" (objects %1, placed %2, pending %3, unready %4; frame tile %5 %6, at %7 %8)")
                                 .arg(route.stats().objects).arg(route.stats().placed).arg(route.stats().pending)
                                 .arg(route.stats().unready).arg(route.frameTile()[0]).arg(route.frameTile()[1])
                                 .arg(at[0]).arg(at[1]);
        if (any && floor.height > sillWorld[1] - 1.1f
                && floor.height < sillWorld[1] + 0.1f && route.carOf(floor.placed) == nullptr) {
            w[1] = floor.height;
            transformPoint(inverse, w, outside);
            found = true;
        }
    }
    if (!found) {
        const float out[3] = {d.x + *side * 0.6f, bottom + 0.3f, d.z};
        float w[3];
        transformPoint(carMatrix, out, w);
        if (route.world().floorAt(w[0], w[2], w[1], w[1] - 2.5f, floor))
            w[1] = floor.height;
        else
            w[1] -= 1.4f;
        transformPoint(inverse, w, outside);
    }
    return true;
}

// Through an open door into its car: to where the steps begin, up them
// (the steps clip, its place held), on to the inside.
void Character::board(const Usable &door) {
    enterCar(door.train, door.car);
    if (!inCar() || ride.train != door.train || ride.car != door.car)
        return;
    float sill[3], inside[3], outside[3], side = 1.0f;
    if (!doorPlaces(ride.eng, door.index, ride.matrix, sill, inside, outside, &side))
        return;
    const float in = -side * float(M_PI) / 2.0f;
    const int steps = std::clamp(int(std::lround((sill[1] - outside[1]) / 0.35f)), 0, 3);
    const int clip = steps > 0 && person >= 0 ? crowd.clipNamed(0, QStringLiteral("steps_up_%1").arg(steps)) : -1;
    float travel[3] = {0.0f, 0.0f, 0.0f};
    if (clip >= 0)
        crowd.clipTravelOf(person, clip, in + ride.yaw, travel, Humans::Crowd::Travel::Body);
    float across[2] = {travel[0], travel[2]};
    turnBy(across, -ride.yaw);
    script = Script();
    std::copy(body.position(), body.position() + 3, script.from);
    Leg start;
    start.to[0] = sill[0] - (clip >= 0 ? across[0] : 0.0f);
    start.to[1] = outside[1];
    start.to[2] = sill[2] - (clip >= 0 ? across[1] : 0.0f);
    start.way = std::atan2(start.to[0] - body.position()[0], start.to[2] - body.position()[2]);
    start.seconds = std::max(0.2f, std::hypot(start.to[0] - body.position()[0], start.to[2] - body.position()[2]) / 1.2f);
    script.legs.push_back(start);
    Leg up;
    std::copy(sill, sill + 3, up.to);
    up.clip = clip;
    up.way = in;
    up.seconds = clip >= 0 ? crowd.clipLength(0, clip) : 0.6f;
    script.legs.push_back(up);
    Leg on;
    std::copy(inside, inside + 3, on.to);
    on.way = in;
    on.seconds = 0.6f;
    script.legs.push_back(on);
    script.door = true;
    crouching = false;
    velocity[0] = velocity[1] = 0.0f;
}

// Out by an open door of its car: to the door, down its steps (the steps
// clip), then on the ground (the world's frame).
void Character::stepOff(const Usable &door) {
    if (!inCar())
        return;
    route.update(at, worldFeet);
    float sill[3], inside[3], outside[3], side = 1.0f;
    if (!doorPlaces(ride.eng, door.index, ride.matrix, sill, inside, outside, &side))
        return;
    const float out = side * float(M_PI) / 2.0f;
    const int steps = std::clamp(int(std::lround((sill[1] - outside[1]) / 0.35f)), 0, 3);
    const int clip = steps > 0 && person >= 0 ? crowd.clipNamed(0, QStringLiteral("steps_down_%1").arg(steps)) : -1;
    float travel[3] = {0.0f, 0.0f, 0.0f};
    if (clip >= 0)
        crowd.clipTravelOf(person, clip, out + ride.yaw, travel, Humans::Crowd::Travel::Body);
    float across[2] = {travel[0], travel[2]};
    turnBy(across, -ride.yaw);
    script = Script();
    std::copy(body.position(), body.position() + 3, script.from);
    Leg toDoor;
    std::copy(inside, inside + 3, toDoor.to);
    toDoor.way = std::atan2(inside[0] - body.position()[0], inside[2] - body.position()[2]);
    toDoor.seconds = std::max(0.2f, std::hypot(inside[0] - body.position()[0], inside[2] - body.position()[2]) / 1.2f);
    script.legs.push_back(toDoor);
    Leg toSill;
    std::copy(sill, sill + 3, toSill.to);
    toSill.way = out;
    toSill.seconds = 0.6f;
    script.legs.push_back(toSill);
    Leg down;
    if (clip >= 0) {
        down.to[0] = sill[0] + across[0];
        down.to[1] = outside[1];
        down.to[2] = sill[2] + across[1];
        down.clip = clip;
        down.seconds = crowd.clipLength(0, clip);
    } else {
        std::copy(outside, outside + 3, down.to);
        down.seconds = 0.6f;
    }
    down.way = out;
    script.legs.push_back(down);
    script.leaveAfter = true;
    script.door = true;
    crouching = false;
    velocity[0] = velocity[1] = 0.0f;
}

// Its legs played on: walking ones from place to place; a clip's holding
// its place while the clip plays (its height going from where it began to
// where it ends), at its end where the clip took it.
bool Character::playScript(float seconds) {
    if (script.legs.empty())
        return false;
    if (script.leg >= script.legs.size()) {
        script = Script();
        return false;
    }
    // All at once (the train moving off).
    if (seconds >= 1.0e5f) {
        const Leg &last = script.legs.back();
        if (locomotion.acting())
            locomotion.endAction();
        body.setPosition(last.to);
        way = last.way;
        const bool leave = script.leaveAfter;
        script = Script();
        moved[0] = moved[1] = 0.0f;
        if (leave)
            leaveCar();
        return false;
    }
    const Leg &leg = script.legs[script.leg];
    if (script.time == 0.0f && leg.clip >= 0)
        locomotion.act({{leg.clip, leg.seconds}}, false);
    script.time += seconds;
    const float t = leg.seconds > 0.0f ? std::min(1.0f, script.time / leg.seconds) : 1.0f;
    float p[3];
    for (int i = 0; i < 3; ++i)
        p[i] = script.from[i] + (leg.to[i] - script.from[i]) * t;
    if (leg.clip >= 0) {
        p[0] = script.from[0];
        p[2] = script.from[2];
    }
    body.setPosition(p);
    // Turned to its way quickly; walking legs move its legs.
    way += std::clamp(std::remainder(leg.way - way, TwoPi), -8.0f * seconds, 8.0f * seconds);
    way = std::remainder(way, TwoPi);
    if (leg.clip < 0 && leg.seconds > 0.0f) {
        moved[0] = (leg.to[0] - script.from[0]) / leg.seconds;
        moved[1] = (leg.to[2] - script.from[2]) / leg.seconds;
    } else {
        moved[0] = moved[1] = 0.0f;
    }
    if (t < 1.0f)
        return true;
    if (leg.clip >= 0)
        locomotion.endAction();
    body.setPosition(leg.to);
    std::copy(leg.to, leg.to + 3, script.from);
    script.time = 0.0f;
    ++script.leg;
    if (script.leg < script.legs.size())
        return true;
    const bool leave = script.leaveAfter;
    const int sitOn = script.sitAfter;
    const bool freeSeat = script.freeSeatAfter;
    script = Script();
    moved[0] = moved[1] = 0.0f;
    if (sitOn >= 0) {
        seated = sitOn;
        if (sittingClip >= 0)
            locomotion.act({{sittingClip, std::max(crowd.clipLength(0, sittingClip), 0.1f)}}, true);
    }
    if (freeSeat) {
        seated = -1;
        Humans::StationPeople::instance().setPlayerSeat(-1, -1, -1);
    }
    if (leave)
        leaveCar();
    return true;
}

// To a free seat of its car and down on it as the riders sit (task 03): its
// feet where the sit_down clip's hips land on the cushion, its origin sunk
// as the clip lowers it, then the sitting clip; the seat marked taken.
void Character::sitDown(const Usable &seatUsed) {
    if (!inCar() || person < 0 || seated >= 0)
        return;
    const std::shared_ptr<const ShapeSeats::Seats> seats = Humans::StationPeople::seatsOfCar(*ride.eng);
    if (!seats || seatUsed.index < 0 || seatUsed.index >= seats->seats.size())
        return;
    const ShapeSeats::Seat &s = seats->seats[seatUsed.index];
    const int sit = crowd.clipNamed(0, QStringLiteral("sit_down"));
    sittingClip = crowd.clipNamed(0, QStringLiteral("sitting"));
    const float w = s.heading * float(M_PI) / 180.0f;
    float travel[3] = {0.0f, 0.0f, 0.0f};
    if (sit < 0 || sittingClip < 0 || !crowd.clipTravelOf(person, sit, w, travel, Humans::Crowd::Travel::Hips))
        return;
    // As StationPeople's sitPlan: a 0.1 m sit back, its origin sunk by the
    // clip's drop (0.44 over its 0.335 m).
    constexpr float SitBack = 0.1f, SeatPerDrop = 0.44f / 0.335f;
    standAt[0] = s.position[0] - travel[0] - SitBack * std::sin(w);
    standAt[2] = s.position[2] - travel[2] - SitBack * std::cos(w);
    const int aisle = seatUsed.index < int(seats->seatAisles.size()) ? seats->seatAisles[seatUsed.index] : -1;
    CollisionWorld::Floor floor;
    if (aisle >= 0)
        standAt[1] = seats->aisles[aisle].yAt(standAt[2]);
    else if (carWorld.world().floorAt(standAt[0], standAt[2], s.position[1], s.position[1] - 1.5f, floor))
        standAt[1] = floor.height;
    else
        standAt[1] = body.position()[1];
    sitAt[0] = standAt[0];
    sitAt[1] = s.position[1] - SeatPerDrop * std::max(0.0f, -travel[1]);
    sitAt[2] = standAt[2];
    seatWay = w;
    script = Script();
    std::copy(body.position(), body.position() + 3, script.from);
    Leg to;
    std::copy(standAt, standAt + 3, to.to);
    to.way = std::atan2(standAt[0] - body.position()[0], standAt[2] - body.position()[2]);
    to.seconds = std::max(0.3f, std::hypot(standAt[0] - body.position()[0], standAt[2] - body.position()[2]) / 1.0f);
    script.legs.push_back(to);
    Leg down;
    std::copy(sitAt, sitAt + 3, down.to);
    down.clip = sit;
    down.way = w;
    down.seconds = crowd.clipLength(0, sit);
    script.legs.push_back(down);
    script.sitAfter = seatUsed.index;
    crouching = false;
    velocity[0] = velocity[1] = 0.0f;
    Humans::StationPeople::instance().setPlayerSeat(ride.train, ride.car, seatUsed.index);
}

// Up from its seat (Space): the stand_up clip, its origin back on the
// floor, the seat free again.
void Character::standUp() {
    if (seated < 0 || busy())
        return;
    const int up = crowd.clipNamed(0, QStringLiteral("stand_up"));
    locomotion.endAction();
    script = Script();
    std::copy(sitAt, sitAt + 3, script.from);
    Leg rise;
    std::copy(standAt, standAt + 3, rise.to);
    rise.clip = up;
    rise.way = seatWay;
    rise.seconds = up >= 0 ? crowd.clipLength(0, up) : 0.8f;
    script.legs.push_back(rise);
    script.freeSeatAfter = true;
}

void Character::push(RenderQueue &queue) {
    // At the driver's place the camera is the cab's: not drawn.
    if (person >= 0 && !rest && !driving)
        crowd.push(queue);
}

bool Character::placeNear(const int *tile, const float *at, Place &place) {
    RouteCollision route;
    route.update(tile, at);
    const CharacterBody::Settings body;
    std::vector<CollisionWorld::Contact> touching;
    auto fits = [&](const CollisionWorld &world, float x, float floor, float z) {
        const float a[3] = {x, floor + body.step + body.radius, z};
        const float b[3] = {x, floor + body.height - body.radius, z};
        touching.clear();
        world.contacts(a, b, body.radius, touching);
        return touching.empty();
    };
    // In a car (task 03): the camera within its footprint and height (any
    // train's car on the track, as placed now, drawn or not); the car's
    // floor under it, else along its middle (its aisle) nearby.
    const float tileAt[2] = {float(tile[0]), float(tile[1])};
    struct Near {
        int train, car;
        Eng *eng;
        float matrix[16];
    };
    std::vector<Near> cars;
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const std::shared_ptr<const Sim::SimulationFrame> frame = simulation != nullptr ? simulation->viewFrame() : nullptr;
    for (int train = 0; frame && train < int(frame->trains.size()); ++train) {
        Consist *consist = simulation->consistOf(train);
        if (consist == nullptr || !consist->isOnTrack)
            continue;
        for (int v = 0; v < consist->engItems.size(); ++v) {
            Near near{train, v, consist->engItems[v].engPointer, {}};
            if (near.eng == nullptr || !near.eng->definition || !near.eng->placedMatrix(tileAt, near.matrix))
                continue;
            const float half = std::max(near.eng->definition->size[2] / 2.0f, 15.0f);
            if (std::hypot(near.matrix[12] - at[0], near.matrix[14] - at[2]) <= half + 2.0f)
                cars.push_back(near);
        }
    }
    static const bool log = qEnvironmentVariable("TSRE_CHARACTER_LOG") == "1";
    if (log)
        qInfo().noquote() << QString("Character: a place near tile %1 %2 (%3, %4, %5), %6 cars near")
                             .arg(tile[0]).arg(tile[1]).arg(double(at[0]), 0, 'f', 2).arg(double(at[1]), 0, 'f', 2)
                             .arg(double(at[2]), 0, 'f', 2).arg(cars.size());
    for (const Near &placed : cars) {
        Eng *eng = placed.eng;
        const float *matrix = placed.matrix;
        float inverse[16], local[3];
        rigidInverse(matrix, inverse);
        transformPoint(inverse, at, local);
        if (log)
            qInfo().noquote() << QString("Character: train %1 car %2 at (%3, %4, %5), the place in it (%6, %7, %8), size %9 x %10 x %11")
                                 .arg(placed.train).arg(placed.car).arg(double(matrix[12]), 0, 'f', 2)
                                 .arg(double(matrix[13]), 0, 'f', 2).arg(double(matrix[14]), 0, 'f', 2)
                                 .arg(double(local[0]), 0, 'f', 2).arg(double(local[1]), 0, 'f', 2)
                                 .arg(double(local[2]), 0, 'f', 2).arg(double(eng->definition->size[0]), 0, 'f', 2)
                                 .arg(double(eng->definition->size[1]), 0, 'f', 2).arg(double(eng->definition->size[2]), 0, 'f', 2);
        if (std::fabs(local[0]) > eng->definition->size[0] / 2.0f + 0.5f && eng->definition->size[0] > 0.1f)
            continue;
        CarCollision car;
        car.build(eng);
        if (!car.within(local[0], local[2]) || local[1] < 0.0f || local[1] > car.height() + 1.0f)
            continue;
        const float centre = std::clamp(local[2], -car.halfLength() + 1.0f, car.halfLength() - 1.0f);
        std::vector<std::pair<float, float>> tried = {{local[0], local[2]}, {0.0f, local[2]}};
        for (int k = 1; k <= 10; ++k)
            for (float side : {-1.0f, 1.0f})
                tried.push_back({0.0f, centre + side * 0.5f * float(k)});
        for (const auto &xz : tried) {
            if (!car.within(xz.first, xz.second, body.radius))
                continue;
            CollisionWorld::Floor floor;
            if (!car.world().floorAt(xz.first, xz.second, local[1], local[1] - 5.0f, floor)
                    || !fits(car.world(), xz.first, floor.height, xz.second))
                continue;
            place = Place();
            place.tile[0] = tile[0];
            place.tile[1] = tile[1];
            place.train = placed.train;
            place.car = placed.car;
            place.inCar[0] = xz.first;
            place.inCar[1] = floor.height;
            place.inCar[2] = xz.second;
            transformPoint(matrix, place.inCar, place.feet);
            return true;
        }
    }
    const CollisionWorld &world = route.world();
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
            if (!fits(world, x, floor.height, z))
                continue;
            place = Place();
            place.tile[0] = tile[0];
            place.tile[1] = tile[1];
            place.feet[0] = x;
            place.feet[1] = floor.height;
            place.feet[2] = z;
            return true;
        }
    }
    return false;
}

// Its eyes (or behind them), in its car's riding frame: the look kept in
// the car's frame (curves turn the view), its pitch and cant.
void Character::getCameraPosition(float *out) {
    out[0] = float(at[0]);
    out[1] = float(at[1]);
    out[2] = cameraPlace[0];
    out[3] = cameraPlace[1];
    out[4] = cameraPlace[2];
    out[5] = ride.yaw;
    out[6] = ride.pitch;
    out[7] = ride.cant;
}

bool Character::getCameraMount(CameraMount &mount) {
    // At the place given (no moving of its own), looking freely; the look
    // it had when it began to walk, or when its frame changed (each car's
    // its own mount: the camera takes the look again).
    mount = CameraMount();
    mount.position[0] = mount.position[1] = mount.position[2] = 0.0f;
    mount.look[0] = startLook[0];
    mount.look[1] = startLook[1];
    mount.id = inCar() ? 2 + ride.train * 1024 + ride.car : 1;
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
