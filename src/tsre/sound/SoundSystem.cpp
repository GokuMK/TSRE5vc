/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/sound/SoundSystem.h>
#include <tsre/sound/SoundEngine.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/Game.h>

#include <QDebug>
#include <QFileInfo>
#include <algorithm>

namespace {

struct Once {
    std::unique_ptr<SoundVoice> voice;
    int tile[2];
    float position[3];
};

struct State {
    SoundListener listener;
    double clock = 0.0;
    double nextLog = 0.0;
    std::mt19937 random{20161012u};
    std::vector<SoundSource *> sources;
    std::vector<Once> once;
};

State &state() {
    static State s;
    return s;
}

void relative(const SoundListener &listener, const int *tile, const float *position, float *out) {
    out[0] = position[0] + 2048.0f * float(tile[0] - listener.tileX);
    out[1] = position[1];
    out[2] = position[2] + 2048.0f * float(tile[1] - listener.tileZ);
}

}

bool SoundSystem::start(bool device) {
    return SoundEngine::get().start(device);
}

void SoundSystem::shutdown() {
    State &s = state();
    for (SoundSource *source : s.sources)
        source->release();
    s.once.clear();
    SoundEngine::get().shutdown();
}

bool SoundSystem::running() {
    return SoundEngine::get().running();
}

void SoundSystem::setListener(const SoundListener &listener) {
    state().listener = listener;
}

const SoundListener &SoundSystem::listener() {
    return state().listener;
}

void SoundSystem::update(float seconds) {
    State &s = state();
    s.clock += std::max(seconds, 0.0f);
    if (!running())
        return;
    SoundEngine::get().setListener(s.listener.position, s.listener.forward, s.listener.up);
    // A source may go while others update (its owner reacting): a copy.
    const std::vector<SoundSource *> sources = s.sources;
    for (SoundSource *source : sources)
        if (std::find(s.sources.begin(), s.sources.end(), source) != s.sources.end())
            source->update(s.listener, s.clock);
    // TSRE_SOUND_LOG: what is heard, every 5 seconds.
    static const bool logging = qEnvironmentVariableIsSet("TSRE_SOUND_LOG");
    if (logging && s.clock >= s.nextLog) {
        s.nextLog = s.clock + 5.0;
        int heard = 0;
        int streams = 0;
        int playing = 0;
        for (SoundSource *source : s.sources) {
            heard += source->active() ? 1 : 0;
            for (int i = 0; i < source->streamCount(); ++i) {
                ++streams;
                playing += source->active() && source->streamPlaying(i) ? 1 : 0;
            }
        }
        qInfo().noquote() << QString("Sound: %1 sources, %2 heard, %3 of %4 streams playing, %5 clips")
                                     .arg(s.sources.size()).arg(heard).arg(playing).arg(streams)
                                     .arg(SoundEngine::get().bank().size());
    }
    for (auto i = s.once.begin(); i != s.once.end();) {
        if (!i->voice->player().busy()) {
            i = s.once.erase(i);
            continue;
        }
        float at[3];
        relative(s.listener, i->tile, i->position, at);
        i->voice->setPosition(at[0], at[1], at[2]);
        i->voice->update();
        ++i;
    }
}

double SoundSystem::clock() {
    return state().clock;
}

std::mt19937 &SoundSystem::random() {
    return state().random;
}

float SoundSystem::uniform(float min, float max) {
    if (!(max > min))
        return min;
    return std::uniform_real_distribution<float>(min, max)(state().random);
}

QStringList SoundSystem::waveFolders(const QString &smsFolder) {
    return {smsFolder, Game::root + "/ROUTES/" + Game::route + "/SOUND", Game::root + "/SOUND"};
}

QString SoundSystem::findSms(const QString &name) {
    if (name.isEmpty())
        return QString();
    return SoundBank::find(name, {Game::root + "/ROUTES/" + Game::route + "/SOUND", Game::root + "/SOUND"});
}

void SoundSystem::playOnce(const QString &wavePath, int tileX, int tileZ, const float *position) {
    if (!running())
        return;
    const SoundClip *clip = SoundEngine::get().bank().get(wavePath, true);
    if (clip == nullptr)
        return;
    Once once;
    once.voice = std::make_unique<SoundVoice>(true);
    if (!once.voice->valid())
        return;
    once.tile[0] = tileX;
    once.tile[1] = tileZ;
    std::copy(position, position + 3, once.position);
    float at[3];
    relative(state().listener, once.tile, once.position, at);
    const float dx = at[0] - state().listener.position[0];
    const float dy = at[1] - state().listener.position[1];
    const float dz = at[2] - state().listener.position[2];
    once.voice->setPosition(at[0], at[1], at[2]);
    once.voice->setGain(SoundSource::distanceGain(std::sqrt(dx * dx + dy * dy + dz * dz),
                                                  SoundSource::rolloffFor(SoundSource::MaxDistance)));
    once.voice->player().queue(clip, StreamPlayer::Mode::OneShot);
    once.voice->update();
    state().once.push_back(std::move(once));
}

void SoundSystem::add(SoundSource *source) {
    state().sources.push_back(source);
}

void SoundSystem::remove(SoundSource *source) {
    auto &sources = state().sources;
    sources.erase(std::remove(sources.begin(), sources.end(), source), sources.end());
}

int SoundSystem::sourceCount() {
    return int(state().sources.size());
}
