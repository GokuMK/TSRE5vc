/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef HUMANS_CROWD_H
#define HUMANS_CROWD_H

#include <QString>
#include <QtGlobal>
#include <map>
#include <memory>
#include <vector>

class GltfShape;
class RenderQueue;

namespace Humans {

// A crowd (task humans 03): many people drawn from a few human files
// (skinned glTF shapes with clips, task humans 02), in tiers by their size
// on the screen (their screen coverage, as glTF levels are chosen):
// - near: the most detailed level and a pose of its own, sampled each
//   frame (later it blends, turns and stops), for the nearest few (a cap);
// - middle: a shared pose. Each clip has a bank of fixed phases (states of
//   the shape, posed once); a person draws the phase nearest its time.
//   People showing one phase at one level share its packets, so the
//   renderer draws them instanced. Its level by coverage;
// - far: past the last level's coverage: an impostor, the human baked
//   from directions around it in an idle pose and walk frames (step 3,
//   ImpostorBake), on a worker once its file has loaded; a quad facing the
//   view's nearest baked direction. Until it is baked, the last level with
//   a shared pose. Far people cast no shadows.
//
// People stand idle or walk. A walk carries its person at the clip's own
// ground speed (SkeletalAnimation::groundMotion), so feet do not slide;
// clips are told apart by that speed: idles stand, walks go.
//
// The crowd's space is the queue's transform when pushed (y up). The
// crowd never owns the human shapes (a ShapeLib does): clear() it before
// they go.
class Crowd {
public:
    Crowd();
    ~Crowd();
    Crowd(const Crowd &) = delete;
    Crowd &operator=(const Crowd &) = delete;

    // A human file; its index. A shared clips file (a glTF file with a
    // skeleton and animations, task humans 02) is loaded once for every
    // kind naming it, and its clips are added to the kind's own by joint
    // names, once the kind has loaded.
    int addKind(GltfShape *shape, const QString &clipsFile = QString());
    int kindCount() const { return int(kinds.size()); }

    // Steered (task humans 05): its owner places it each frame (steer),
    // standing or walking; the crowd picks an idle clip and a walk clip for
    // it and plays the walk at the speed it is moved, so feet keep to the
    // ground.
    enum class Behaviour { Idle, Circle, Steered };
    struct PersonSpec {
        int kind = 0;
        // Where it stands, or the centre of its circle.
        float position[3] = {0.0f, 0.0f, 0.0f};
        // Radians about +y from the shape's own facing (idle people); a
        // steered person's way: it faces (sin, 0, cos) of it.
        float heading = 0.0f;
        float scale = 1.0f;
        Behaviour behaviour = Behaviour::Idle;
        float radius = 1.0f;
        // Its clip, its phase, its playing speed (within 5 %), where on its
        // circle and which way round. Its size is the caller's (humans vary
        // by about 6 %); a walker's speed follows both.
        quint32 seed = 0;
    };
    // Its index; a removed person's index is given again.
    int addPerson(const PersonSpec &spec);
    // Every index given so far (removed people's too).
    int personCount() const;
    // A steered person's place and way (PersonSpec::heading) now, and its
    // speed over the ground (m/s): below WalkSpeed it stands.
    void steer(int person, const float *position, float way, float speed);
    // Gone (its pose given back); its index is free for the next person.
    void removePerson(int person);
    // The speed (m/s) a steered person's walk carries it at its own
    // playing speed and size; 0 until its human file has loaded.
    float walkSpeedOf(int person);
    // Clips played by name (task humans 06: steps into a car, `steps_up_2`):
    // a kind's clip of that name (a shared clips file's has " (file)" after
    // it), -1 for none or not loaded yet; its length (s). Clips named
    // steps_* and stairs_* are never a person's idle or walk.
    int clipNamed(int kind, const QString &name) const;
    float clipLength(int kind, int clip) const;
    int kindOf(int person) const;
    // How far a clip carries a steered person (its size), in the crowd's
    // space with the person turned to `way` (PersonSpec::heading): x, y
    // (up), z. False for none.
    bool clipTravelOf(int person, int clip, float way, float *travel);
    // A steered person playing a clip at a time, placed and turned as by
    // steer() (which ends it).
    void steerClip(int person, const float *position, float way, int clip, float time);
    // Every person and kind gone, their states given back to the shapes.
    void clear();

    // Where the tiers are chosen from: the camera in the crowd's space and
    // the vertical field of view (degrees).
    struct View {
        float camera[3] = {0.0f, 0.0f, 0.0f};
        float fieldOfView = 45.0f;
    };
    // Advances `seconds` (0 keeps the time), places the walkers, chooses
    // each person's tier and level for the view and samples the own poses.
    void update(float seconds, const View &view);
    // Submits every person drawn, as update() left them. Not for picking.
    void push(RenderQueue &queue);

    // The crowd's clock (seconds); captures set it.
    void setTime(double seconds) { clock = seconds; }
    double time() const { return clock; }

    // The quality setting's handles (task step 5): the most people with
    // poses of their own; a factor on the levels' coverages (above 1 the
    // detailed levels and the meshes end nearer); shared phases per second
    // of a clip (4 to 64 a clip).
    // Impostors: whether there are any, their directions (8 or 16) and
    // the height of a cell in pixels (its width half of it); read when a
    // human file is first prepared.
    // Whether middle people cast shadows (near people always do, far ones
    // never).
    struct Tuning {
        int nearCap = 40;
        float coverageScale = 1.0f;
        float bankRate = 15.0f;
        bool impostors = true;
        int impostorDirections = 16;
        int impostorHeight = 128;
        bool middleShadows = true;
    };
    void setTuning(const Tuning &value) { tuning = value; }
    // The player's setting core.rendering.humanQuality (0 Low, 1 Medium, 2
    // High, 3 Ultra) as tuning; its owner reads the setting once and when it
    // changes. Lower draws more people as impostors, never fewer people:
    //
    // |                       | Low  | Medium | High | Ultra |
    // | own poses (near cap)  | 10   | 20     | 40   | 80    |
    // | meshes reach (times)  | 0.5  | 0.7    | 1    | 1.4   |
    // | impostor directions   | 8    | 8      | 16   | 16    |
    // | impostor cell (px)    | 32x64| 48x96  |64x128|64x128 |
    // | middle people shadows | no   | no     | yes  | yes   |
    static Tuning quality(int level);
    const Tuning &currentTuning() const { return tuning; }

    // The last update's and push's figures: people per tier (waiting: their
    // human file not loaded yet), own poses sampled, bank states made so
    // far, and the CPU times.
    struct Stats {
        int people = 0;
        int waiting = 0;
        int nearTier = 0;
        int middleTier = 0;
        int farTier = 0;
        int ownPoses = 0;
        int bankStates = 0;
        // Drawn with a mesh but no pose (its rest pose): none expected.
        int unposed = 0;
        // Far people drawn as impostors; impostors baked so far and the
        // time their bakes took on the workers (ms).
        int impostorsDrawn = 0;
        int impostorsBaked = 0;
        double bakeMs = 0.0;
        double updateMs = 0.0;
        double pushMs = 0.0;
    };
    const Stats &stats() const { return figures; }

    // Whether an impostor is being baked (captures wait for it).
    bool baking() const;
    // Whether a kind's impostor is baked and ready to draw; its directions.
    bool impostorReady(int kind) const;
    int impostorDirections(int kind) const;
    // The impostor cell a person draws now, -1 for none (pose * directions
    // + direction).
    int impostorCellOf(int person) const;

    // A kind's measured clips (for tests and the viewer): its walks' and
    // idles' clip indices and a clip's ground speed (m/s); empty until it
    // has loaded.
    std::vector<int> walkClips(int kind) const;
    std::vector<int> idleClips(int kind) const;
    float clipSpeed(int kind, int clip) const;
    // A person's tier now: 0 near, 1 middle, 2 far, -1 not drawn; its level
    // and the shape state it draws.
    int tierOf(int person) const;
    int levelOf(int person) const;
    unsigned int stateOf(int person) const;
    // Where a person is drawn now (the crowd's space), null for none.
    const float *placeOf(int person) const;

    // How fast a clip's walk must go to count as a walk (m/s).
    static constexpr float WalkSpeed = 0.3f;

private:
    struct Kind;
    struct Person;
    std::vector<std::unique_ptr<Kind>> kinds;
    std::vector<Person> people;
    std::vector<int> freePeople;
    void releaseOwn(Person &person);
    // Shared clips files by path, loaded once.
    std::map<QString, std::unique_ptr<GltfShape>> clipFiles;
    Tuning tuning;
    Stats figures;
    double clock = 0.0;
    std::vector<int> nearCandidates;
    int bakesDone = 0;
    double bakesMs = 0.0;

    bool prepare(Kind &kind);
    void startBake(Kind &kind);
    void finishBake(Kind &kind);
    void syncImpostorTextures(Kind &kind);
    void choosePlay(Person &person, const Kind &kind);
    unsigned int bankState(Kind &kind, int clip, int phase);
    unsigned int ownState(Kind &kind);
};

}

#endif
