/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ShapeAnimation.h"
#include <algorithm>
#include <cmath>

namespace ShapeAnimation {

namespace {

// The keys around the frame and how far between them (Open Rails' rule):
// the last key at or before the frame and the one after it.
template <typename Key>
void around(const std::vector<Key> &keys, float frame, const Key *&a, const Key *&b, float &amount) {
    size_t index = 0;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].frame <= frame)
            index = i;
        else
            break;
    }
    a = &keys[index];
    b = index + 1 < keys.size() ? &keys[index + 1] : a;
    amount = a->frame < b->frame ? std::clamp((frame - a->frame) / (b->frame - a->frame), 0.0f, 1.0f) : 0.0f;
}

// The shorter way round.
void slerp(const float *a, const float *b, float t, float *out) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float sign = 1.0f;
    if (d < 0.0f) {
        d = -d;
        sign = -1.0f;
    }
    float wa = 1.0f - t, wb = t;
    if (d < 0.9999f) {
        const float angle = std::acos(d);
        const float s = std::sin(angle);
        wa = std::sin((1.0f - t) * angle) / s;
        wb = std::sin(t * angle) / s;
    }
    float length = 0.0f;
    for (int i = 0; i < 4; ++i) {
        out[i] = wa * a[i] + sign * wb * b[i];
        length += out[i] * out[i];
    }
    length = std::sqrt(length);
    if (length > 0.0f)
        for (int i = 0; i < 4; ++i)
            out[i] /= length;
}

}

float Channel::lastFrame() const {
    float last = 0.0f;
    for (const PositionKey &k : positions)
        last = std::max(last, k.frame);
    for (const RotationKey &k : rotations)
        last = std::max(last, k.frame);
    return last;
}

void fromMsts(const float msts[4], float out[4]) {
    out[0] = -msts[0];
    out[1] = -msts[1];
    out[2] = -msts[2];
    out[3] = msts[3];
}

void pose(const Channel &channel, float frame, const float rest[16], float out[16]) {
    std::copy(rest, rest + 16, out);
    if (!channel.rotations.empty()) {
        const RotationKey *a, *b;
        float amount;
        around(channel.rotations, frame, a, b, amount);
        float q[4];
        slerp(a->rotation, b->rotation, amount, q);
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        out[0] = 1 - 2 * (y * y + z * z);
        out[1] = 2 * (x * y + z * w);
        out[2] = 2 * (x * z - y * w);
        out[4] = 2 * (x * y - z * w);
        out[5] = 1 - 2 * (x * x + z * z);
        out[6] = 2 * (y * z + x * w);
        out[8] = 2 * (x * z + y * w);
        out[9] = 2 * (y * z - x * w);
        out[10] = 1 - 2 * (x * x + y * y);
        out[3] = out[7] = out[11] = 0.0f;
    }
    if (!channel.positions.empty()) {
        const PositionKey *a, *b;
        float amount;
        around(channel.positions, frame, a, b, amount);
        for (int i = 0; i < 3; ++i)
            out[12 + i] = a->position[i] + (b->position[i] - a->position[i]) * amount;
    }
}

void Pose::setFrame(int part, float frame) {
    if (part < 0)
        return;
    if (size_t(part) >= parts.size())
        parts.resize(size_t(part) + 1);
    parts[size_t(part)].mode = Part::Frame;
    parts[size_t(part)].frame = frame;
    posed = true;
}

void Pose::setTransform(int part, const float *transform) {
    if (part < 0)
        return;
    if (size_t(part) >= parts.size())
        parts.resize(size_t(part) + 1);
    parts[size_t(part)].mode = transform != nullptr ? Part::Transform : Part::Rest;
    if (transform != nullptr)
        std::copy(transform, transform + 16, parts[size_t(part)].local.begin());
    posed = std::any_of(parts.begin(), parts.end(), [](const Part &p) { return p.mode != Part::Rest; });
}

void Pose::clear() {
    parts.clear();
    posed = false;
}

void Pose::local(int part, const Channel *channel, bool looping, float loopFrame, const float rest[16],
                 float out[16]) const {
    const Part *own = part >= 0 && size_t(part) < parts.size() ? &parts[size_t(part)] : nullptr;
    if (own != nullptr && own->mode == Part::Transform) {
        std::copy(own->local.begin(), own->local.end(), out);
    } else if (channel != nullptr && !channel->empty() && own != nullptr && own->mode == Part::Frame) {
        pose(*channel, own->frame, rest, out);
    } else if (channel != nullptr && !channel->empty() && looping) {
        pose(*channel, loopFrame, rest, out);
    } else {
        std::copy(rest, rest + 16, out);
    }
}

void sortKeys(Channel &channel) {
    auto less = [](const auto &a, const auto &b) { return a.frame < b.frame; };
    std::stable_sort(channel.positions.begin(), channel.positions.end(), less);
    std::stable_sort(channel.rotations.begin(), channel.rotations.end(), less);
}

}
