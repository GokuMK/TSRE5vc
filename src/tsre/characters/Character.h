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
#include <tsre/characters/CarCollision.h>
#include <tsre/characters/CharacterBody.h>
#include <tsre/characters/RouteCollision.h>
#include <tsre/humans/Crowd.h>
#include <tsre/humans/Locomotion.h>
#include <QString>
#include <memory>
#include <vector>

class Eng;
class RenderQueue;

namespace Characters {

// A walking player's character (task characters 02): its body in the
// route (CharacterBody against RouteCollision, within the route's reach of
// a track), drawn by a crowd of one (a human of the route's humans set, or
// TSRE's, posed by Locomotion), the GameObj the camera rides at its eyes
// (the head joint's place, smoothed) or behind it, its keys:
// - W A S D and the arrows: walking, the way the camera looks (back and
//   aside at most AsideSpeed); Shift running (ahead), Ctrl slowly;
// - Space: a jump; C: crouching or not (its body 1.25 m high; up again
//   only where standing fits); V: first or third person;
// - E: using what it aims at (task 03), as a click.
// Its body faces the camera's way while it moves; standing, it turns to it
// only past 60 degrees. Positions in the editor's frame: a tile (x, z) and
// metres in it, as the camera's.
// The run keeps it (Cast): the player's while walking (controlled: its
// keys and the camera's look), else left as it stands (step 5: Fly), idle,
// facing its way, landing when in the air; past RestDistance from the
// camera and on the ground, at rest (not moved, not drawn).
// In a car (task 03): its place, way and velocity in the car's frame (its
// body's space as drawn), moving against that car only (CarCollision); its
// place in the world the car's matrix now (Eng::placedMatrix) times it, so
// the train carries it exactly; the camera's look kept in the car's frame
// (curves turn the view, its pitch and cant too). The floor under its
// feet says which: a car's triangles in the world put it in that car; in
// a car, off its footprint (the train standing) back in the world. A
// moving car keeps it within its footprint; its car gone, it is on the
// ground where it was. Its body is slimmer in a car (0.44 m across, 0.6 m
// outside: a double-deck car's aisle between its armrests).
class Character : public GameObj {
public:
    static constexpr float WalkSpeed = 1.4f;
    static constexpr float RunSpeed = 4.0f;
    static constexpr float SlowSpeed = 0.7f;
    static constexpr float AsideSpeed = 1.0f;
    static constexpr float CrouchSpeed = 0.8f;
    static constexpr float RestDistance = 400.0f;

    // A place to stand: its tile and its feet in it; in a car also its
    // train, car and its feet in the car's frame.
    struct Place {
        int tile[2] = {0, 0};
        float feet[3] = {0.0f, 0.0f, 0.0f};
        int train = -1;
        int car = -1;
        float inCar[3] = {0.0f, 0.0f, 0.0f};
    };

    Character();
    ~Character() override;
    // Walking from a place (its feet in the editor's tile), facing `yaw`
    // (radians from +z towards +x, as the camera turns), looking `pitch`
    // up; the camera taking it looks so. Again: moved there (Walk here),
    // standing.
    void start(const int *tile, const float *feet, float yaw, float pitch);
    // The same from a place found (placeNear): in its car when it is in one.
    void start(const Place &place, float yaw, float pitch);
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
    // Its way in the world (as the camera's yaw).
    float facing() const;
    // At rest (left far from the camera): neither moved nor drawn.
    bool resting() const { return rest; }
    // The nearest place a character may stand from `at` (a camera's place
    // in its tile): in a car when `at` is in one (the car's floor under
    // it, else along its middle); else the floor straight under it, within
    // the route's reach of a track and where its body fits; else the
    // nearest such place round it (in rings 2 m apart, to 60 m). False for
    // none.
    static bool placeNear(const int *tile, const float *at, Place &place);
    // Each frame before the camera: how long, the camera's look (yaw and
    // pitch, radians) and the camera's place (its tile and position: the
    // crowd's level of detail).
    void update(float seconds, float yaw, float pitch, const int *cameraTile, const float *cameraAt);
    // Drawn in the camera's tile's space (as update() placed it).
    void push(RenderQueue &queue);

    bool thirdPerson() const { return behind; }
    void setThirdPerson(bool on) { behind = on; }
    const int *tile() const { return at; }
    // Its feet in its tile (in a car too: where the car has them now).
    const float *feet() const { return worldFeet; }
    // Its eyes in its tile (the camera's place in first person).
    const float *eyes() const { return worldEye; }
    bool onGround() const { return body.onGround(); }
    // How it moves over the floor (x, z m/s), as it went (in a car: in the
    // car, turned as the car).
    const float *velocityOverGround() const { return worldMoved; }
    // In a car (task 03): its train and car (-1: on the ground), its feet
    // in the car's frame.
    bool inCar() const { return ride.train >= 0; }
    int train() const { return ride.train; }
    int car() const { return ride.car; }
    const float *placeInCar() const { return body.position(); }
    const RouteCollision &routeCollision() const { return route; }
    // Its collision world's (checks: a floor of their own).
    RouteCollision &routeCollision() { return route; }
    const CarCollision &carCollision() const { return carWorld; }

    // Things it may use (task 03): what the eyes' ray (the camera's look,
    // the view's middle) meets within UseReach; the nearest is aimed at, its
    // label shown under the view's mark; a click or E uses it.
    static constexpr float UseReach = 2.5f;
    struct Usable {
        enum Kind { Board, StepOff, Sit, Drive, CabOut, CabIn };
        Kind kind = Board;
        QString label;
        int train = -1;
        int car = -1;
        // The door's (seat's) index among the car's.
        int index = -1;
        // In the car's frame: its middle, how far round it the ray meets
        // it.
        float centre[3] = {0.0f, 0.0f, 0.0f};
        float radius = 0.5f;
    };
    // The thing aimed at now, null for none.
    const Usable *aimed() const { return aim >= 0 && aim < int(usables.size()) ? &usables[size_t(aim)] : nullptr; }
    // Uses it (a click, E); false for nothing.
    bool use();
    // Captures: the nearest usable thing of a kind (within reach of its
    // feet, aimed at or not) used; false for none.
    bool useNearest(Usable::Kind kind);
    // Doing something by itself (boarding, stepping off, sitting down,
    // standing up): its keys wait.
    bool busy() const { return !script.legs.empty(); }
    // Sitting on its car's seat (its index), -1 none; Space stands it up.
    int seat() const { return seated; }
    // Captures: stands it up from its seat (as Space).
    void standUp();
    // The cab (task 03 step 5): the driver's place used asks the view to
    // drive its train (driveAsked: the train, -1 none); the view drove it
    // (driveTaken): it sits there unseen, the camera in the cab. Standing
    // up (E held): beside that place in the cab, standing; false where it
    // finds no floor. Walk while driving: at a car's driver's place
    // (its cab's viewpoint), sitting.
    int driveAsked() const { return askDrive; }
    void driveTaken(bool taken);
    bool atDriverPlace() const { return driving; }
    bool standInCab();
    bool sitAtDriverPlace(const int *tile, int train, int car, int viewpoint);

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
    // Its frame: the world's (its tile's) or its car's. Its way (as the
    // camera's yaw), its velocity over the floor (x, z) as asked and as it
    // went, its eyes: in its frame.
    float way = 0.0f;
    float velocity[2] = {0.0f, 0.0f};
    float moved[2] = {0.0f, 0.0f};
    bool turning = false;
    // The camera's look (world) as last given; the look a camera taking it
    // starts with (in its frame).
    float look[2] = {0.0f, 0.0f};
    float startLook[2] = {0.0f, 0.0f};
    float eye[3] = {0.0f, 0.0f, 0.0f};
    bool eyeSet = false;
    // In its tile: its feet, eyes, the camera's place, its velocity.
    float worldFeet[3] = {0.0f, 0.0f, 0.0f};
    float worldEye[3] = {0.0f, 0.0f, 0.0f};
    float cameraPlace[3] = {0.0f, 0.0f, 0.0f};
    float worldMoved[2] = {0.0f, 0.0f};
    bool behind = false;
    bool crouching = false;
    bool jumping = false;
    bool playerOwn = false;
    bool rest = false;
    // Where it is in the camera's tile's space (the crowd's).
    float drawnAt[3] = {0.0f, 0.0f, 0.0f};
    // Its car (task 03): train and car (-1 none), the car (checked each
    // tick), its matrix now (its frame to the tile's) and back, its yaw,
    // pitch and cant (the camera's riding frame), whether its train moves.
    struct Ride {
        int train = -1;
        int car = -1;
        Eng *eng = nullptr;
        float matrix[16];
        float inverse[16];
        float yaw = 0.0f, pitch = 0.0f, cant = 0.0f;
        bool moving = false;
    };
    Ride ride;
    CarCollision carWorld;
    // The car beside its car at each end (its front, its rear) while that
    // end's gangway is open (task 05), -1 none.
    int nextCars[2] = {-1, -1};
    std::vector<Usable> usables;
    int aim = -1;
    // Moving by itself in its frame (task 03: through a door): legs in
    // turn, each to a place, walking or holding its place playing a clip
    // (its height from where it began to where it ends, its place there at
    // the end), facing a way; then what follows.
    struct Leg {
        float to[3] = {0.0f, 0.0f, 0.0f};
        int clip = -1;
        float seconds = 0.0f;
        float way = 0.0f;
    };
    struct Script {
        std::vector<Leg> legs;
        size_t leg = 0;
        float time = 0.0f;
        float from[3] = {0.0f, 0.0f, 0.0f};
        bool leaveAfter = false;
        // Then sitting (its seat, the sitting clip looped), or its seat
        // left free.
        int sitAfter = -1;
        bool freeSeatAfter = false;
        // Through a door: done at once when the train moves off.
        bool door = false;
    };
    Script script;
    // Its seat (-1 none), where its feet stand to sit and the place it sits
    // at (its origin sunk), its way; the sitting clip.
    int seated = -1;
    // The cab: the driver's place asked for (a train), sitting there, at
    // which viewpoint; its place there (its car's frame).
    int askDrive = -1;
    int askViewpoint = -1;
    bool driving = false;
    int drivePlace = -1;
    // A cab's viewpoint's head in its car's frame (mirrored as the cab
    // says); false for none.
    static bool cabHead(const Eng *eng, int viewpoint, float *head);
    float cabFloor(const float *head) const;
    void climb(const Usable &door, bool in);
    float standAt[3] = {0.0f, 0.0f, 0.0f};
    float sitAt[3] = {0.0f, 0.0f, 0.0f};
    float seatWay = 0.0f;
    int sittingClip = -1;
    void sitDown(const Usable &seatUsed);
    void findUsables();
    void aimAt(float yaw, float pitch);
    // Its legs played on (true while it lasts).
    bool playScript(float seconds);
    void board(const Usable &door);
    void stepOff(const Usable &door);
    // A door of its car (or a car's) in the car's frame: its sill (the
    // floor inside at its outer face), inside it, and outside (the floor
    // there, from the world: the platform).
    bool doorPlaces(Eng *eng, int index, const float *carMatrix, float *sill, float *inside, float *outside,
                    float *side) const;
    void loadHuman();
    void keepInTile();
    // The collision world of its frame.
    const CollisionWorld &frameWorld() const { return inCar() ? carWorld.world() : route.world(); }
    // From its frame to its tile's and back (identity on the ground).
    void toWorld(const float *in, float *out) const;
    void toFrame(const float *in, float *out) const;
    // The car's place now; false when its car is gone.
    bool placeCar();
    void enterCar(int train, int car);
    void leaveCar();
    // Walking between cars (task 05, Gangways): its car's ends opened and
    // closed by the cars beside it; on to the next car past a coupling end
    // (the look in its frame turned with it); into another car of its
    // train where it stands now.
    void gangways();
    bool crossGangway(float &lyaw, float &lpitch);
    bool changeCar(int car);
    void stepInside(int end);
    // Behind the eyes along a look (in its frame), in front of what is
    // between; in its frame.
    void placeBehind(float yaw, float pitch, float *out) const;
};

}

#endif
