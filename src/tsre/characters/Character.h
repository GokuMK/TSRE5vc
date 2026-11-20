/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#ifndef CHARACTERS_CHARACTER_H
#define CHARACTERS_CHARACTER_H

#include <tsre/GameObj.h>
#include <tsre/characters/CharacterBody.h>
#include <tsre/characters/RouteCollision.h>
#include <tsre/humans/Crowd.h>
#include <tsre/humans/Locomotion.h>
#include <memory>

class RenderQueue;

namespace Characters {

// A walking player's character (task characters 02): its body in the
// route (CharacterBody against RouteCollision, within the route's reach of
// a track), drawn by a crowd of one (a human of the route's humans set, or
// TSRE's, posed by Locomotion), the GameObj the camera rides at its eyes
// (the head joint's place, smoothed) or behind it, its keys:
// - W A S D and the arrows: walking, the way the camera looks (back and
//   aside at most AsideSpeed); Shift running (ahead), Ctrl slowly;
// - Space: a jump; C: crouching or not; V: first or third person.
// Its body faces the camera's way while it moves; standing, it turns to it
// only past 60 degrees. Positions in the editor's frame: a tile (x, z) and
// metres in it, as the camera's.
// The run keeps it (Cast): the player's while walking (controlled: its
// keys and the camera's look), else left as it stands (step 5: Fly), idle,
// facing its way, landing when in the air; past RestDistance from the
// camera and on the ground, at rest (not moved, not drawn).
class Character : public GameObj {
public:
    static constexpr float WalkSpeed = 1.4f;
    static constexpr float RunSpeed = 4.0f;
    static constexpr float SlowSpeed = 0.7f;
    static constexpr float AsideSpeed = 1.0f;
    static constexpr float CrouchSpeed = 0.8f;
    static constexpr float RestDistance = 400.0f;

    Character();
    ~Character() override;
    // Walking from a place (its feet in the editor's tile), facing `yaw`
    // (radians from +z towards +x, as the camera turns), looking `pitch`
    // up; the camera taking it looks so. Again: moved there (Walk here),
    // standing.
    void start(const int *tile, const float *feet, float yaw, float pitch);
    // The player's (its keys and the camera's look) or left as it stands
    // (its keys let go of). Cast::setPlayer says.
    void setControlled(bool on);
    bool controlled() const { return playerOwn; }
    // Taken again where it stands (Walk): the camera taking it looks the
    // way it faces, level.
    void lookItsWay();
    // Where a camera leaving it goes (Fly): behind its eyes along the
    // camera's look, in front of what is between (third person's place),
    // in its tile.
    void flyPlace(float *out) const;
    // Its way (as the camera's yaw).
    float facing() const { return way; }
    // At rest (left far from the camera): neither moved nor drawn.
    bool resting() const { return rest; }
    // The nearest place a character may stand from `at` (a camera's place
    // in its tile): the floor straight under it, within the route's reach
    // of a track and where its body fits; else the nearest such place
    // round it (in rings 2 m apart, to 60 m). False for none.
    static bool placeNear(const int *tile, const float *at, int *placeTile, float *feet);
    // Each frame before the camera: how long, the camera's look (yaw and
    // pitch, radians) and the camera's place (its tile and position: the
    // crowd's level of detail).
    void update(float seconds, float yaw, float pitch, const int *cameraTile, const float *cameraAt);
    // Drawn in the camera's tile's space (as update() placed it).
    void push(RenderQueue &queue);

    bool thirdPerson() const { return behind; }
    void setThirdPerson(bool on) { behind = on; }
    const int *tile() const { return at; }
    const float *feet() const { return body.position(); }
    // Its eyes in its tile (the camera's place in first person).
    const float *eyes() const { return eye; }
    bool onGround() const { return body.onGround(); }
    // How it moves over the ground (x, z m/s), as it went.
    const float *velocityOverGround() const { return moved; }
    const RouteCollision &routeCollision() const { return route; }
    // Its collision world's (checks: a floor of their own).
    RouteCollision &routeCollision() { return route; }

    // GameObj.
    void getCameraPosition(float *out) override;
    bool getCameraMount(CameraMount &mount) override;
    void cameraAttached(bool attached) override;
    KeyTaker *keyTaker() override;

private:
    class Keys;
    std::unique_ptr<Keys> keys;
    CharacterBody body;
    RouteCollision route;
    int at[2] = {0, 0};
    Humans::Crowd crowd;
    int person = -1;
    int head = -1;
    bool clipsSet = false;
    Humans::Locomotion locomotion;
    // Its way (as the camera's yaw), its velocity over the ground (x, z) as
    // asked and as it went.
    float way = 0.0f;
    float velocity[2] = {0.0f, 0.0f};
    float moved[2] = {0.0f, 0.0f};
    bool turning = false;
    float look[2] = {0.0f, 0.0f};
    float startLook[2] = {0.0f, 0.0f};
    float eye[3] = {0.0f, 0.0f, 0.0f};
    bool eyeSet = false;
    float cameraPlace[3] = {0.0f, 0.0f, 0.0f};
    bool behind = false;
    bool crouching = false;
    bool jumping = false;
    bool playerOwn = false;
    bool rest = false;
    // Where it is in the camera's tile's space (the crowd's).
    float drawnAt[3] = {0.0f, 0.0f, 0.0f};
    void loadHuman();
    void keepInTile();
    // Behind the eyes along a look, in front of what is between.
    void placeBehind(float yaw, float pitch, float *out) const;
};

}

#endif
