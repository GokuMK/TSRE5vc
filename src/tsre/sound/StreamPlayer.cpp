/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/sound/StreamPlayer.h>
#include <tsre/sound/SoundBank.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace {

// The queue's lock: held briefly by the game thread and for one read by
// the audio thread.
class Guard {
public:
    explicit Guard(std::atomic_flag &flag) : flag(flag) {
        for (int spins = 0; flag.test_and_set(std::memory_order_acquire); ++spins)
            if (spins > 64)
                std::this_thread::yield();
    }
    ~Guard() { flag.clear(std::memory_order_release); }

private:
    std::atomic_flag &flag;
};

bool looping(StreamPlayer::Mode mode) {
    return mode == StreamPlayer::Mode::Loop || mode == StreamPlayer::Mode::LoopRelease;
}

// Catmull-Rom between y1 and y2.
float cubic(float y0, float y1, float y2, float y3, float x) {
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * x + c2) * x + c1) * x + y1;
}

// Open Rails keeps a one-shot longer than this many bytes when a stream
// goes out of reach (HardCleanQueue).
constexpr size_t LongOneShotBytes = 50000;
// A one-shot stops counting as playing this close to its end, so the next
// sound can be queued to follow it (Open Rails' checkpoint).
constexpr double OneShotTail = 0.2;

}

StreamPlayer::StreamPlayer(int outputRate, int channels)
    : rate(std::max(outputRate, 1)), outputChannels(std::clamp(channels, 1, 2)) {
    position = 1.0;
}

void StreamPlayer::queue(const SoundClip *clip, Mode mode, bool repeated) {
    Guard guard(lock);
    if (mode == Mode::Release || mode == Mode::ReleaseWithJump) {
        const int release = mode == Mode::ReleaseWithJump ? 2 : 1;
        for (int i = count - 1; i >= 0; --i) {
            Item &item = at(i);
            if (!looping(item.mode))
                continue;
            if (!item.started)
                removeAt(i);
            else
                item.release = std::max(item.release, release);
        }
        publish();
        return;
    }
    if (clip == nullptr || clip->parts.empty())
        return;
    // A command repeating one of the last four is ignored; one-shots only
    // when the same trigger repeats.
    for (int i = count - 1, k = 0; i >= 0 && k < 4; --i, ++k) {
        const Item &item = at(i);
        if (item.mode == mode && item.release == 0 && item.clip == clip && (mode != Mode::OneShot || repeated))
            return;
    }
    // A waiting loop-release or one-shot of the same clip becomes a loop.
    if (count > 0) {
        Item &last = at(count - 1);
        if (!last.started && last.clip == clip && mode == Mode::Loop
                && (last.mode == Mode::LoopRelease || last.mode == Mode::OneShot)) {
            last.mode = Mode::Loop;
            publish();
            return;
        }
    }
    if (count == Capacity)
        return;
    Item &item = items[(head + count) % Capacity];
    item = Item();
    item.clip = clip;
    item.mode = mode;
    ++count;
    publish();
}

void StreamPlayer::stop() {
    Guard guard(lock);
    head = 0;
    count = 0;
    std::memset(history, 0, sizeof(history));
    position = 1.0;
    silentPulls = 4;
    publish();
}

void StreamPlayer::deactivate() {
    Guard guard(lock);
    int keep = -1;
    for (int i = count - 1; i >= 0; --i) {
        const Item &item = at(i);
        if (item.release != 0)
            break;
        const bool longOneShot = item.mode == Mode::OneShot && item.clip->samples.size() * 2 > LongOneShotBytes;
        if (looping(item.mode) || longOneShot) {
            keep = i;
            break;
        }
    }
    Item kept = keep >= 0 ? at(keep) : Item();
    head = 0;
    count = 0;
    if (keep >= 0) {
        kept.started = false;
        items[0] = kept;
        count = 1;
    }
    std::memset(history, 0, sizeof(history));
    position = 1.0;
    silentPulls = 4;
    publish();
}

void StreamPlayer::dropIfReleasing() {
    Guard guard(lock);
    if (count > 0 && at(0).started && at(0).release != 0) {
        popFront();
        publish();
    }
}

void StreamPlayer::setSpeed(float value) {
    if (std::isfinite(value) && value > 0.0f)
        speed.store(std::min(value, 32.0f), std::memory_order_relaxed);
}

const SoundClip *StreamPlayer::currentClip() const {
    return current.load(std::memory_order_acquire);
}

void StreamPlayer::read(float *out, uint64_t frames) {
    Guard guard(lock);
    const double playback = speed.load(std::memory_order_relaxed);
    for (uint64_t i = 0; i < frames; ++i) {
        while (position >= 1.0) {
            std::memmove(history[0], history[1], sizeof(history[0]) * 3);
            float frame[2] = {0.0f, 0.0f};
            if (pull(frame))
                silentPulls = 0;
            else
                ++silentPulls;
            history[3][0] = frame[0];
            history[3][1] = frame[1];
            position -= 1.0;
        }
        if (silentPulls >= 4 && count == 0) {
            // Nothing left and the history has run out: silence, and the
            // next clip starts with the next read.
            std::memset(out + i * outputChannels, 0, sizeof(float) * outputChannels * (frames - i));
            position = 1.0;
            break;
        }
        const float x = float(position);
        for (int c = 0; c < outputChannels; ++c)
            out[i * outputChannels + c] = cubic(history[0][c], history[1][c], history[2][c], history[3][c], x);
        position += (pulledRate > 0 ? pulledRate : rate) * playback / rate;
    }
    publish();
}

bool StreamPlayer::pull(float *frame) {
    while (count > 0) {
        Item &item = at(0);
        if (!item.started) {
            if (item.clip == nullptr || item.clip->parts.empty() || !(looping(item.mode) || item.mode == Mode::OneShot)) {
                popFront();
                continue;
            }
            item.started = true;
            item.part = 0;
            item.frame = item.clip->parts[0].begin;
        }
        const SoundClip &clip = *item.clip;
        // Past the end of a part: on to the next one; parts may be empty,
        // but a pass through all of them without a frame ends the item.
        bool done = false;
        for (size_t guard = 0; item.frame >= clip.parts[item.part].end; ++guard) {
            if (guard > 2 * clip.parts.size() + 2 || !nextPart(item)) {
                done = true;
                break;
            }
        }
        if (done) {
            popFront();
            continue;
        }
        const int16_t *sample = clip.samples.data() + size_t(item.frame) * size_t(clip.channels);
        frame[0] = sample[0] / 32768.0f;
        frame[1] = clip.channels == 2 ? sample[1] / 32768.0f : frame[0];
        if (outputChannels == 1 && clip.channels == 2)
            frame[0] = 0.5f * (frame[0] + frame[1]);
        ++item.frame;
        pulledRate = clip.sampleRate;
        return true;
    }
    return false;
}

bool StreamPlayer::nextPart(Item &item) {
    const auto &parts = item.clip->parts;
    const uint32_t n = uint32_t(parts.size());
    switch (item.mode) {
    case Mode::OneShot:
        if (item.part + 1 >= n)
            return false;
        ++item.part;
        break;
    case Mode::Loop:
        if (item.part + 1 < n)
            ++item.part;
        else if (item.release != 0)
            return false;
        else
            item.part = 0;
        break;
    case Mode::LoopRelease: {
        if (n < 3) {
            if (item.release != 0)
                return false;
            item.part = 0;
            break;
        }
        const uint32_t last = n - 1;
        const uint32_t sustainEnd = n - 2;
        if (item.part == last)
            return false;
        // Released: with a jump after this part, otherwise after the intro
        // or at the end of the sustain.
        if (item.release == 2 || (item.release == 1 && (item.part == 0 || item.part == sustainEnd)))
            item.part = last;
        else if (item.part == sustainEnd)
            item.part = 1;
        else
            ++item.part;
        break;
    }
    default:
        return false;
    }
    item.frame = parts[item.part].begin;
    return true;
}

void StreamPlayer::popFront() {
    items[head] = Item();
    head = (head + 1) % Capacity;
    --count;
}

void StreamPlayer::removeAt(int index) {
    for (int i = index; i < count - 1; ++i)
        at(i) = at(i + 1);
    at(count - 1) = Item();
    --count;
}

void StreamPlayer::publish() {
    bool anyPlaying = false;
    const SoundClip *clip = nullptr;
    const double playback = speed.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i) {
        const Item &item = at(i);
        if (clip == nullptr)
            clip = item.clip;
        if (!item.started || (item.release == 0 && item.mode != Mode::OneShot)) {
            anyPlaying = true;
        } else if (item.release == 0) {
            const double left = double(item.clip->parts.back().end) - double(item.frame);
            if (left / (item.clip->sampleRate * playback) > OneShotTail)
                anyPlaying = true;
        }
    }
    playing.store(anyPlaying, std::memory_order_release);
    pending.store(count > 0, std::memory_order_release);
    current.store(clip, std::memory_order_release);
}
