/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SOUNDENGINE_H
#define SOUNDENGINE_H

#include <tsre/sound/SoundBank.h>
#include <tsre/sound/StreamPlayer.h>

#include <memory>

struct ma_engine;
struct ma_sound;
struct PlayerSource;

// miniaudio's engine, one per process: the default playback device, or no
// device for tests, which mix with render(). Its listener is the camera.
// Every call is harmless when it is not running (sound off, no device).
class SoundEngine {
public:
    static SoundEngine &get();

    // device false: nothing is played, render() mixes on request.
    bool start(bool device = true, int rate = 48000);
    // Voices must be gone first.
    void shutdown();
    bool running() const { return engine != nullptr; }
    ma_engine *handle() { return engine.get(); }
    int sampleRate() const;
    int channels() const;

    // In the camera tile's coordinates, as the sound sources' positions.
    void setListener(const float *position, const float *forward, const float *up);
    void setVolume(float volume);
    // Without a device: mixes the next frames of everything playing.
    void render(float *out, uint64_t frames);

    SoundBank &bank() { return clips; }

private:
    SoundEngine() = default;
    ~SoundEngine();

    std::unique_ptr<ma_engine> engine;
    SoundBank clips;
};

// One stream's player as a sound in the engine: placed in 3D (mono, panned
// by its position, its gain set by the caller) or heard as it is (stereo).
class SoundVoice {
public:
    explicit SoundVoice(bool placed);
    ~SoundVoice();
    SoundVoice(const SoundVoice &) = delete;
    SoundVoice &operator=(const SoundVoice &) = delete;

    bool valid() const { return sound != nullptr; }
    bool placed() const { return inSpace; }
    StreamPlayer &player() { return *stream; }

    void setPosition(float x, float y, float z);
    void setGain(float gain);
    // Runs the sound while its player has something (and it is allowed:
    // its source heard), stops it otherwise; a stopped player is not read.
    void update(bool allowed = true);

private:
    bool inSpace = false;
    bool started = false;
    std::unique_ptr<StreamPlayer> stream;
    std::unique_ptr<PlayerSource> source;
    std::unique_ptr<ma_sound> sound;
};

#endif
