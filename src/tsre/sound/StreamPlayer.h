/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef STREAMPLAYER_H
#define STREAMPLAYER_H

#include <array>
#include <atomic>
#include <cstdint>

struct SoundClip;

// The playing of one MSTS sound stream: one clip at a time from a queue, in
// Open Rails' play modes, read on the audio thread at the output rate with
// the stream's playback speed. The queue's rules follow Open Rails'
// ALSoundSource.Queue; the parts follow the clip's loop points:
// - OneShot plays the clip once;
// - Loop plays it over and over, a release lets it finish the pass;
// - LoopRelease plays the intro, then loops the sustain parts; a release
//   plays on to the end of the sustain and then the release part, a release
//   with jump goes to the release part after the part playing;
// - Release and ReleaseWithJump are not played: they release the loops
//   playing or waiting (waiting ones are dropped).
// The queue is changed on the game thread and read on the audio thread.
class StreamPlayer {
public:
    enum class Mode { OneShot, Loop, LoopRelease, Release, ReleaseWithJump };

    // channels: 1 (sounds placed in 3D) or 2.
    StreamPlayer(int outputRate, int channels);

    int outputRate() const { return rate; }
    int channels() const { return outputChannels; }

    // Game thread. repeated: the trigger that queued the last clip queues
    // it again (a one-shot then does not stack up).
    void queue(const SoundClip *clip, Mode mode, bool repeated = false);
    // Drops everything now.
    void stop();
    // Out of reach: drops all but the newest unreleased loop (or long
    // one-shot), which starts again from its beginning when played again.
    void deactivate();
    // A releasing clip with no volume left is stopped at once.
    void dropIfReleasing();
    // 1 plays a clip at its own rate.
    void setSpeed(float speed);
    // Something plays that is not on its way out: Open Rails restarts a
    // stream's initial trigger when this goes false.
    bool isPlaying() const { return playing.load(std::memory_order_acquire); }
    // Anything left to play, released or not.
    bool busy() const { return pending.load(std::memory_order_acquire); }
    // The clip playing, or the next one (its sample rate sets the speed).
    const SoundClip *currentClip() const;

    // Audio thread (and tests): fills frames at the output rate, silence
    // when the queue is empty.
    void read(float *out, uint64_t frames);

private:
    struct Item {
        const SoundClip *clip = nullptr;
        Mode mode = Mode::OneShot;
        bool started = false;
        // 0 none, 1 at the end of the sustain (or of the pass), 2 jump.
        int release = 0;
        uint32_t part = 0;
        uint32_t frame = 0;
    };
    static constexpr int Capacity = 16;

    bool pull(float *frame);
    bool nextPart(Item &item);
    void popFront();
    void removeAt(int index);
    void publish();
    Item &at(int index) { return items[(head + index) % Capacity]; }

    const int rate;
    const int outputChannels;
    std::array<Item, Capacity> items;
    int head = 0;
    int count = 0;
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
    std::atomic<float> speed{1.0f};
    std::atomic<bool> playing{false};
    std::atomic<bool> pending{false};
    std::atomic<const SoundClip *> current{nullptr};
    // Resampling: four source frames and the position between the middle two.
    float history[4][2] = {};
    double position = 0.0;
    int silentPulls = 4;
    int pulledRate = 0;
};

#endif
