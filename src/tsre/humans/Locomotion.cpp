/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#include "Locomotion.h"

#include <algorithm>
#include <cmath>

namespace Humans {

namespace {

constexpr float TwoPi = 2.0f * float(M_PI);
// Weights below this are left out.
constexpr float Least = 1e-4f;

float aroundOnce(float angle) {
    angle = std::fmod(angle, TwoPi);
    return angle < 0.0f ? angle + TwoPi : angle;
}

float wrapped(double time, float duration) {
    if (!(duration > 0.0f))
        return 0.0f;
    const double d = double(duration);
    const double t = std::fmod(time, d);
    return float(t < 0.0 ? t + d : t);
}

float toward(float value, float target, float seconds) {
    const float step = seconds / Locomotion::Fade;
    return value < target ? std::min(value + step, target) : std::max(value - step, target);
}

}

Locomotion::Clip Locomotion::clipOf(Crowd &crowd, int kind, int clip) {
    Clip out;
    const Crowd::ClipMotion motion = crowd.clipMotion(kind, clip);
    if (clip < 0 || !(motion.duration > 0.0f))
        return out;
    out.clip = clip;
    out.duration = motion.duration;
    out.speed = motion.speed;
    out.way[0] = motion.way[0];
    out.way[1] = motion.way[1];
    out.strideStart = motion.strideStart;
    return out;
}

bool Locomotion::clipsOf(Crowd &crowd, int kind, Clips &clips) {
    if (!crowd.kindReady(kind))
        return false;
    clips = Clips();
    auto named = [&](const char *name) { return clipOf(crowd, kind, crowd.clipNamed(kind, QLatin1String(name))); };
    clips.idle = named("idle_neutral");
    if (!clips.idle.valid()) {
        const std::vector<int> idles = crowd.idleClips(kind);
        if (!idles.empty())
            clips.idle = clipOf(crowd, kind, idles.front());
    }
    for (const char *name : {"walk", "run", "jog", "sprint"}) {
        const Clip clip = named(name);
        if (clip.valid() && clip.speed > 0.0f)
            clips.ahead.push_back(clip);
    }
    std::sort(clips.ahead.begin(), clips.ahead.end(), [](const Clip &a, const Clip &b) { return a.speed < b.speed; });
    for (const char *name : {"walk_back", "walk_left", "walk_right"}) {
        const Clip clip = named(name);
        if (clip.valid() && clip.speed > 0.0f)
            clips.aside.push_back(clip);
    }
    clips.crouchIdle = named("crouch_idle");
    clips.crouchWalk = named("crouch_walk");
    return true;
}

void Locomotion::move(float x, float z, bool crouch) {
    velocity[0] = x;
    velocity[1] = z;
    crouching = crouch;
}

void Locomotion::act(const std::vector<Step> &steps, bool loop) {
    if (steps.empty())
        return;
    for (Action &a : actions)
        a.target = 0.0f;
    Action action;
    action.steps = steps;
    action.loop = loop;
    actions.push_back(action);
}

void Locomotion::endAction() {
    for (Action &a : actions)
        a.target = 0.0f;
}

bool Locomotion::acting() const {
    return !actions.empty() && actions.back().target > 0.0f;
}

int Locomotion::actionStep() const {
    return acting() ? actions.back().step : -1;
}

float Locomotion::actionTime() const {
    return acting() ? actions.back().time : 0.0f;
}

// The moving clips for the way it goes at `speed`, their weights summing
// to 1: the two ways either side of it by angle, the clips ahead by speed.
std::vector<Locomotion::Weighted> Locomotion::byWay(float speed) const {
    struct Way {
        float angle = 0.0f;
        std::vector<Weighted> clips;
    };
    std::vector<Way> ways;
    // Ahead: the two either side of the speed.
    std::vector<const Clip *> ahead;
    for (const Clip &clip : set.ahead)
        if (clip.valid() && clip.speed > 0.0f)
            ahead.push_back(&clip);
    if (!ahead.empty()) {
        Way way;
        way.angle = aroundOnce(std::atan2(ahead.front()->way[0], ahead.front()->way[1]));
        size_t below = 0;
        while (below + 1 < ahead.size() && ahead[below + 1]->speed * scale <= speed)
            ++below;
        if (below + 1 < ahead.size() && ahead[below]->speed * scale < speed) {
            const float low = ahead[below]->speed * scale, high = ahead[below + 1]->speed * scale;
            const float t = high > low ? (speed - low) / (high - low) : 0.0f;
            way.clips.push_back({ahead[below], 1.0f - t});
            way.clips.push_back({ahead[below + 1], t});
        } else {
            way.clips.push_back({ahead[below], 1.0f});
        }
        ways.push_back(way);
    }
    for (const Clip &clip : set.aside)
        if (clip.valid() && clip.speed > 0.0f) {
            Way way;
            way.angle = aroundOnce(std::atan2(clip.way[0], clip.way[1]));
            way.clips.push_back({&clip, 1.0f});
            ways.push_back(way);
        }
    std::vector<Weighted> out;
    if (ways.empty())
        return out;
    std::sort(ways.begin(), ways.end(), [](const Way &a, const Way &b) { return a.angle < b.angle; });
    const float wanted = aroundOnce(std::atan2(velocity[0], velocity[1]));
    size_t first = 0, second = 0;
    float t = 0.0f;
    for (size_t i = 0; i < ways.size() && ways.size() > 1; ++i) {
        const size_t next = (i + 1) % ways.size();
        const float gap = aroundOnce(ways[next].angle - ways[i].angle);
        const float into = aroundOnce(wanted - ways[i].angle);
        if (into <= gap) {
            first = i;
            second = next;
            t = gap > 0.0f ? into / gap : 0.0f;
            break;
        }
    }
    for (const Weighted &w : ways[first].clips)
        out.push_back({w.clip, w.weight * (1.0f - t)});
    if (second != first)
        for (const Weighted &w : ways[second].clips)
            out.push_back({w.clip, w.weight * t});
    return out;
}

void Locomotion::moving(float seconds, float weight, std::vector<Crowd::Play> &out) {
    const float speed = std::hypot(velocity[0], velocity[1]);
    const float share = std::clamp((speed - Still) / (Moving - Still), 0.0f, 1.0f);
    if (set.idle.valid() && (1.0f - share) * weight > Least)
        out.push_back({set.idle.clip, wrapped(idleTime, set.idle.duration), (1.0f - share) * weight});
    if (!(share > 0.0f))
        return;
    const std::vector<Weighted> clips = byWay(speed);
    float natural = 0.0f, stride = 0.0f;
    for (const Weighted &w : clips) {
        natural += w.weight * w.clip->speed * scale;
        stride += w.weight * w.clip->duration;
    }
    moveRate = natural > 0.0f ? std::clamp(speed / natural, 0.5f, 2.0f) : 1.0f;
    if (stride > 0.0f)
        phase = float(wrapped(double(phase) + double(seconds * moveRate / stride), 1.0f));
    for (const Weighted &w : clips)
        if (w.weight * share * weight > Least)
            out.push_back({w.clip->clip, wrapped(double(phase * w.clip->duration + w.clip->strideStart), w.clip->duration),
                           w.weight * share * weight});
}

void Locomotion::crouched(float seconds, float weight, std::vector<Crowd::Play> &out) {
    const float speed = std::hypot(velocity[0], velocity[1]);
    const float share = set.crouchWalk.valid() ? std::clamp((speed - Still) / (Moving - Still), 0.0f, 1.0f) : 0.0f;
    if (set.crouchIdle.valid() && (1.0f - share) * weight > Least)
        out.push_back({set.crouchIdle.clip, wrapped(crouchIdleTime, set.crouchIdle.duration), (1.0f - share) * weight});
    if (!(share > 0.0f))
        return;
    const float natural = set.crouchWalk.speed * scale;
    const float rate = natural > 0.0f ? std::clamp(speed / natural, 0.5f, 2.0f) : 1.0f;
    crouchPhase = float(wrapped(double(crouchPhase) + double(seconds * rate / set.crouchWalk.duration), 1.0f));
    if (share * weight > Least)
        out.push_back({set.crouchWalk.clip,
                       wrapped(double(crouchPhase * set.crouchWalk.duration + set.crouchWalk.strideStart), set.crouchWalk.duration),
                       share * weight});
}

void Locomotion::update(float seconds) {
    seconds = std::max(seconds, 0.0f);
    idleTime += double(seconds);
    crouchIdleTime += double(seconds);
    const bool canCrouch = set.crouchIdle.valid() || set.crouchWalk.valid();
    crouch = toward(crouch, crouching && canCrouch ? 1.0f : 0.0f, seconds);

    // Actions: on through their steps; faded in, out when done or replaced.
    for (Action &a : actions) {
        a.time += seconds;
        while (true) {
            const float duration = std::max(a.steps[size_t(a.step)].duration, 1e-3f);
            if (a.time < duration)
                break;
            if (a.step + 1 < int(a.steps.size())) {
                a.time -= duration;
                ++a.step;
            } else if (a.loop) {
                a.time = std::fmod(a.time, duration);
                break;
            } else {
                // Held at its end while it fades out.
                a.time = duration;
                a.target = 0.0f;
                break;
            }
        }
        a.weight = toward(a.weight, a.target, seconds);
    }
    actions.erase(std::remove_if(actions.begin(), actions.end(),
                                 [](const Action &a) { return a.target <= 0.0f && a.weight <= 0.0f; }),
                  actions.end());
    moveWeight = toward(moveWeight, acting() ? 0.0f : 1.0f, seconds);

    output.clear();
    moving(seconds, moveWeight * (1.0f - crouch), output);
    crouched(seconds, moveWeight * crouch, output);
    for (const Action &a : actions)
        if (a.weight > Least)
            output.push_back({a.steps[size_t(a.step)].clip, a.time, a.weight});
    float total = 0.0f;
    for (const Crowd::Play &play : output)
        total += play.weight;
    if (total > 0.0f)
        for (Crowd::Play &play : output)
            play.weight /= total;
}

}
