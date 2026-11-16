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
#include <QFile>
#include <cmath>
#include <cstdio>
#include <cstring>

// TSRE_SOUND_RECORD's file: the mix as played, 32-bit float WAV, written
// on the audio thread (C stdio: no Qt object there); its header's sizes
// set again every second, so a run killed leaves a file that plays.
struct SoundEngine::Recorder {
    std::FILE *file = nullptr;
    QString path;
    uint32_t channels = 2;
    uint32_t rate = 48000;
    uint64_t frames = 0;
    uint64_t nextHeader = 0;
    float peak = 0.0f;
    uint64_t over = 0;

    void put16(uint16_t v) { std::fwrite(&v, 2, 1, file); }
    void put32(uint32_t v) { std::fwrite(&v, 4, 1, file); }
    // RIFF, fmt (IEEE float, 18 bytes), fact, data: 58 bytes.
    void header() {
        const uint32_t data = uint32_t(std::min<uint64_t>(frames * channels * 4, 0xFFFFFF00u));
        std::fseek(file, 0, SEEK_SET);
        std::fwrite("RIFF", 1, 4, file);
        put32(4 + 26 + 12 + 8 + data);
        std::fwrite("WAVEfmt ", 1, 8, file);
        put32(18);
        put16(3);
        put16(uint16_t(channels));
        put32(rate);
        put32(rate * channels * 4);
        put16(uint16_t(channels * 4));
        put16(32);
        put16(0);
        std::fwrite("fact", 1, 4, file);
        put32(4);
        put32(uint32_t(std::min<uint64_t>(frames, 0xFFFFFFFFu)));
        std::fwrite("data", 1, 4, file);
        put32(data);
        std::fseek(file, 0, SEEK_END);
        std::fflush(file);
    }
};

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

// The engine's mix, at the end of each read (the device's thread).
static void recordMix(void *user, float *mix, ma_uint64 frames) {
    SoundEngine::Recorder *r = static_cast<SoundEngine::Recorder *>(user);
    if (r->file == nullptr || mix == nullptr)
        return;
    std::fwrite(mix, sizeof(float) * r->channels, size_t(frames), r->file);
    const uint64_t n = frames * r->channels;
    for (uint64_t i = 0; i < n; ++i) {
        const float a = std::fabs(mix[i]);
        r->peak = std::max(r->peak, a);
        r->over += a > 1.0f ? 1 : 0;
    }
    r->frames += frames;
    if (r->frames >= r->nextHeader) {
        r->nextHeader = r->frames + r->rate;
        r->header();
    }
}

SoundEngine &SoundEngine::get() {
    static SoundEngine instance;
    return instance;
}

SoundEngine::~SoundEngine() {
    shutdown();
}

bool SoundEngine::start(Output output, const QString &record, int rate) {
    if (engine != nullptr)
        return true;
    auto created = std::make_unique<ma_engine>();
    ma_engine_config config = ma_engine_config_init();
    if (output == Output::None) {
        config.noDevice = MA_TRUE;
        config.channels = 2;
        config.sampleRate = ma_uint32(rate);
    } else if (output == Output::NullDevice) {
        auto made = std::make_unique<ma_context>();
        const ma_backend backends[] = {ma_backend_null};
        if (ma_context_init(backends, 1, nullptr, made.get()) != MA_SUCCESS) {
            qWarning().noquote() << "Sound: no null device";
            return false;
        }
        context = std::move(made);
        config.pContext = context.get();
        config.channels = 2;
        config.sampleRate = ma_uint32(rate);
    }
    // Started once the recording knows the engine's format.
    config.noAutoStart = MA_TRUE;
    std::unique_ptr<Recorder> recording;
    if (!record.isEmpty()) {
        recording = std::make_unique<Recorder>();
        recording->path = record;
        recording->file = std::fopen(QFile::encodeName(record).constData(), "wb");
        if (recording->file == nullptr) {
            qWarning().noquote() << "Sound: cannot record to" << record;
            recording.reset();
        } else {
            config.onProcess = recordMix;
            config.pProcessUserData = recording.get();
        }
    }
    const ma_result result = ma_engine_init(&config, created.get());
    if (result != MA_SUCCESS) {
        qWarning().noquote() << "Sound: no playback device -" << ma_result_description(result);
        if (recording != nullptr)
            std::fclose(recording->file);
        if (context != nullptr) {
            ma_context_uninit(context.get());
            context.reset();
        }
        return false;
    }
    engine = std::move(created);
    if (recording != nullptr) {
        recording->channels = ma_engine_get_channels(engine.get());
        recording->rate = ma_engine_get_sample_rate(engine.get());
        recording->header();
        recorder = std::move(recording);
        qInfo().noquote() << "Sound: recording the mix to" << record;
    }
    if (output != Output::None) {
        ma_engine_start(engine.get());
        qInfo().noquote() << QString("Sound: playing at %1 Hz, %2 channels%3").arg(sampleRate()).arg(channels())
                                     .arg(output == Output::NullDevice ? QStringLiteral(", to the null device") : QString());
    }
    return true;
}

void SoundEngine::shutdown() {
    if (engine == nullptr)
        return;
    ma_engine_uninit(engine.get());
    engine.reset();
    if (context != nullptr) {
        ma_context_uninit(context.get());
        context.reset();
    }
    if (recorder != nullptr) {
        recorder->header();
        std::fclose(recorder->file);
        qInfo().noquote() << QString("Sound: recorded %1 s to %2, peak %3, %4 samples over full scale")
                                     .arg(double(recorder->frames) / std::max(1u, recorder->rate), 0, 'f', 1)
                                     .arg(recorder->path).arg(recorder->peak, 0, 'f', 3).arg(recorder->over);
        recorder.reset();
    }
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

void SoundVoice::update(bool allowed) {
    if (sound == nullptr)
        return;
    const bool busy = stream->busy() && allowed;
    if (busy && !started) {
        ma_sound_start(sound.get());
        started = true;
    } else if (!busy && started) {
        ma_sound_stop(sound.get());
        started = false;
    }
}
