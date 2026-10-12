/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/sound/SoundEngine.h>
#include <tsre/sound/MiniaudioConfig.h>

#include <QDebug>
#include <cstring>

// A StreamPlayer as a miniaudio data source: at the engine's rate, so the
// engine does not resample; the player applies the stream's speed itself.
struct PlayerSource {
    ma_data_source_base base;
    StreamPlayer *player = nullptr;
};

namespace {

ma_result readPlayer(ma_data_source *dataSource, void *out, ma_uint64 frames, ma_uint64 *read) {
    PlayerSource *source = reinterpret_cast<PlayerSource *>(dataSource);
    source->player->read(static_cast<float *>(out), frames);
    if (read != nullptr)
        *read = frames;
    return MA_SUCCESS;
}

ma_result seekPlayer(ma_data_source *, ma_uint64) {
    return MA_SUCCESS;
}

ma_result playerFormat(ma_data_source *dataSource, ma_format *format, ma_uint32 *channels, ma_uint32 *rate,
                       ma_channel *map, size_t mapCapacity) {
    const PlayerSource *source = reinterpret_cast<const PlayerSource *>(dataSource);
    *format = ma_format_f32;
    *channels = ma_uint32(source->player->channels());
    *rate = ma_uint32(source->player->outputRate());
    if (map != nullptr)
        ma_channel_map_init_standard(ma_standard_channel_map_default, map, mapCapacity, *channels);
    return MA_SUCCESS;
}

ma_result playerCursor(ma_data_source *, ma_uint64 *cursor) {
    *cursor = 0;
    return MA_SUCCESS;
}

ma_data_source_vtable PlayerVtable = {readPlayer, seekPlayer, playerFormat, playerCursor, nullptr, nullptr, 0};

}

SoundEngine &SoundEngine::get() {
    static SoundEngine instance;
    return instance;
}

SoundEngine::~SoundEngine() {
    shutdown();
}

bool SoundEngine::start(bool device, int rate) {
    if (engine != nullptr)
        return true;
    auto created = std::make_unique<ma_engine>();
    ma_engine_config config = ma_engine_config_init();
    if (!device) {
        config.noDevice = MA_TRUE;
        config.channels = 2;
        config.sampleRate = ma_uint32(rate);
    }
    const ma_result result = ma_engine_init(&config, created.get());
    if (result != MA_SUCCESS) {
        qWarning().noquote() << "Sound: no playback device -" << ma_result_description(result);
        return false;
    }
    engine = std::move(created);
    if (device)
        qInfo().noquote() << QString("Sound: playing at %1 Hz, %2 channels").arg(sampleRate()).arg(channels());
    return true;
}

void SoundEngine::shutdown() {
    if (engine == nullptr)
        return;
    ma_engine_uninit(engine.get());
    engine.reset();
    clips.clear();
}

int SoundEngine::sampleRate() const {
    return engine != nullptr ? int(ma_engine_get_sample_rate(engine.get())) : 0;
}

int SoundEngine::channels() const {
    return engine != nullptr ? int(ma_engine_get_channels(engine.get())) : 0;
}

void SoundEngine::setListener(const float *position, const float *forward, const float *up) {
    if (engine == nullptr)
        return;
    ma_engine_listener_set_position(engine.get(), 0, position[0], position[1], position[2]);
    ma_engine_listener_set_direction(engine.get(), 0, forward[0], forward[1], forward[2]);
    ma_engine_listener_set_world_up(engine.get(), 0, up[0], up[1], up[2]);
}

void SoundEngine::setVolume(float volume) {
    if (engine != nullptr)
        ma_engine_set_volume(engine.get(), volume);
}

void SoundEngine::render(float *out, uint64_t frames) {
    if (engine == nullptr) {
        return;
    }
    ma_engine_read_pcm_frames(engine.get(), out, frames, nullptr);
}

SoundVoice::SoundVoice(bool placed) : inSpace(placed) {
    SoundEngine &soundEngine = SoundEngine::get();
    if (!soundEngine.running())
        return;
    stream = std::make_unique<StreamPlayer>(soundEngine.sampleRate(), placed ? 1 : 2);
    source = std::make_unique<PlayerSource>();
    source->player = stream.get();
    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &PlayerVtable;
    if (ma_data_source_init(&config, &source->base) != MA_SUCCESS) {
        source.reset();
        return;
    }
    auto created = std::make_unique<ma_sound>();
    ma_uint32 flags = MA_SOUND_FLAG_NO_PITCH;
    if (!placed)
        flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_data_source(soundEngine.handle(), &source->base, flags, nullptr, created.get()) != MA_SUCCESS) {
        ma_data_source_uninit(&source->base);
        source.reset();
        return;
    }
    if (placed) {
        // The gain with distance is the caller's (Open Rails' model); the
        // engine only pans. Its model "none" would not pan either: inverse
        // distance with no rolloff keeps the gain at 1.
        ma_sound_set_attenuation_model(created.get(), ma_attenuation_model_inverse);
        ma_sound_set_rolloff(created.get(), 0.0f);
        ma_sound_set_doppler_factor(created.get(), 0.0f);
    }
    sound = std::move(created);
}

SoundVoice::~SoundVoice() {
    if (sound != nullptr) {
        ma_sound_uninit(sound.get());
        sound.reset();
    }
    if (source != nullptr)
        ma_data_source_uninit(&source->base);
}

void SoundVoice::setPosition(float x, float y, float z) {
    if (sound != nullptr && inSpace)
        ma_sound_set_position(sound.get(), x, y, z);
}

void SoundVoice::setGain(float gain) {
    if (sound != nullptr)
        ma_sound_set_volume(sound.get(), gain);
}

void SoundVoice::update() {
    if (sound == nullptr)
        return;
    const bool busy = stream->busy();
    if (busy && !started) {
        ma_sound_start(sound.get());
        started = true;
    } else if (!busy && started) {
        ma_sound_stop(sound.get());
        started = false;
    }
}
