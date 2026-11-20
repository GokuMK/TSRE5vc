/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#ifndef HUMANS_LOCOMOTION_H
#define HUMANS_LOCOMOTION_H

#include <tsre/humans/Crowd.h>
#include <vector>

namespace Humans {

// How a controlled person's body moves (task characters 01): what it does,
// as the clips it plays (Crowd::control).
//
// Moving, by its velocity in its own frame (x to its left, z ahead; m/s):
// - standing (below Still) its idle; up to Moving the idle fades into the
//   moving clips;
// - by its way: the two clips whose ways are nearest on either side of it
//   (ahead, back, left, right, as their feet carry them), by angle;
// - ahead, by speed: the clips going ahead in order of their speeds, the
//   two on either side of its speed blended so theirs give it; below the
//   slowest or above the fastest, that one played slower or faster;
// - in step: one stride's share for every clip, each at that share of its
//   own stride counted from where its stride begins (its first foot set
//   down ahead); the share goes on at the speed asked for over the blend's
//   speed (from half to twice), over the blend's stride time;
// - crouching: its crouching idle and walk (any way), faded in and out
//   over Fade.
//
// Acting: clips played in turn over the moving (sitting down, then
// sitting; the jump's start, then its loop), each once, the last looped or
// back to moving when it ends; faded in and out over Fade. A new action
// fades the one before out.
class Locomotion {
public:
    // A clip as it moves the person (Crowd::ClipMotion), -1 for none.
    struct Clip {
        int clip = -1;
        float duration = 0.0f;
        float speed = 0.0f;
        float way[2] = {0.0f, 0.0f};
        float strideStart = 0.0f;
        bool valid() const { return clip >= 0 && duration > 0.0f; }
    };
    struct Clips {
        Clip idle;
        // Going ahead, the slowest first.
        std::vector<Clip> ahead;
        // Going other ways (back, to either side).
        std::vector<Clip> aside;
        Clip crouchIdle, crouchWalk;
    };
    // A crowd kind's, by their names in the shared clips file (task
    // characters 01): `idle_neutral` (else the kind's first idle); ahead
    // `walk`, `run`, `jog`, `sprint` (as many as it has, by speed); aside
    // `walk_back`, `walk_left`, `walk_right`; `crouch_idle`, `crouch_walk`.
    // False until the kind has loaded.
    static bool clipsOf(Crowd &crowd, int kind, Clips &clips);
    static Clip clipOf(Crowd &crowd, int kind, int clip);

    void setClips(const Clips &value) { set = value; }
    const Clips &clips() const { return set; }
    // The person's size: its clips' speeds scaled by it.
    void setScale(float value) { scale = value; }

    // Its velocity in its own frame (m/s: x to its left, z ahead), and
    // whether it crouches.
    void move(float x, float z, bool crouch = false);
    // An action: its clips in turn (each its length), the last looped when
    // `loop`.
    struct Step {
        int clip = -1;
        float duration = 0.0f;
    };
    void act(const std::vector<Step> &steps, bool loop);
    // Back to moving: the action fades out.
    void endAction();
    // Whether an action plays (not fading out); the step it plays and how
    // far into it (s); -1 and 0 for none.
    bool acting() const;
    int actionStep() const;
    float actionTime() const;

    // Advances `seconds` and makes the plays.
    void update(float seconds);
    // Clips at their times and weights (summing to 1); empty: the rest pose.
    const std::vector<Crowd::Play> &plays() const { return output; }
    // The stride's share now (0 to 1) and the moving clips' rate.
    float strideShare() const { return phase; }
    float rate() const { return moveRate; }

    static constexpr float Fade = 0.2f;
    static constexpr float Still = 0.05f;
    static constexpr float Moving = 0.3f;

private:
    struct Weighted {
        const Clip *clip = nullptr;
        float weight = 0.0f;
    };
    struct Action {
        std::vector<Step> steps;
        bool loop = false;
        int step = 0;
        float time = 0.0f;
        float weight = 0.0f;
        float target = 1.0f;
    };
    Clips set;
    float scale = 1.0f;
    float velocity[2] = {0.0f, 0.0f};
    bool crouching = false;
    float crouch = 0.0f;
    float phase = 0.0f;
    float crouchPhase = 0.0f;
    double idleTime = 0.0;
    double crouchIdleTime = 0.0;
    float moveRate = 1.0f;
    float moveWeight = 1.0f;
    std::vector<Action> actions;
    std::vector<Crowd::Play> output;
    void moving(float seconds, float weight, std::vector<Crowd::Play> &out);
    void crouched(float seconds, float weight, std::vector<Crowd::Play> &out);
    std::vector<Weighted> byWay(float speed) const;
};

}

#endif
