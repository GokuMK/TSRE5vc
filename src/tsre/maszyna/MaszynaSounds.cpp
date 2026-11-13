/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/maszyna/MaszynaSounds.h>
#include <tsre/maszyna/MaszynaCabControls.h>
#include <tsre/maszyna/MaszynaFiz.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/sound/SoundBank.h>
#include <tsre/sound/SoundEngine.h>
#include <tsre/sound/SoundSource.h>
#include <tsre/sound/SoundSystem.h>
#include <tsre/sound/StreamPlayer.h>

#include <QDebug>
#include <QMap>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

using namespace MaszynaSound;

namespace {

// MaSzyna's cutoffs (sound.cpp, audiorenderer.cpp): no sound starts beyond
// 2,750 m, none plays on beyond 3,000 m.
constexpr float StartCutoff = 2750.0f;
constexpr float PlayCutoff = 3000.0f;
// OpenAL's settings MaSzyna gives every source: rolloff, Doppler factor;
// velocities capped at 250 km/h.
constexpr float Rolloff = 1.75f;
constexpr float DopplerScale = 0.25f;
constexpr float MaxVelocity = 250.0f / 3.6f;

const SoundClip *clipOf(const QString &path) {
    if (path.isEmpty())
        return nullptr;
    return SoundEngine::get().bank().get(path, SoundBank::Channels::Mixed);
}

// A definition's files asked for now, to be decoded (Ogg Vorbis, on a
// worker) by the time they play: a one-shot asked before is not heard.
void preload(const Definition &d) {
    if (!SoundEngine::get().running())
        return;
    for (const Sample *s : {&d.begin, &d.main, &d.end})
        clipOf(s->path);
    for (const Chunk &c : d.chunks)
        clipOf(c.sample.path);
}

// The cars with MaSzyna sounds by their Eng: a listener riding one hears
// the others through its outside row.
QHash<const void *, const MaszynaSounds *> &cars() {
    static QHash<const void *, const MaszynaSounds *> list;
    return list;
}

}

// ---- MaszynaEmitter ----

MaszynaEmitter::MaszynaEmitter(const Definition &definition, const float *place) : def(definition) {
    for (int i = 0; i < 3; ++i)
        at[i] = place != nullptr ? place[i] : 0.0f;
}

MaszynaEmitter::~MaszynaEmitter() = default;

float MaszynaEmitter::point() const {
    return (basePitch <= 1.0f ? std::clamp(basePitch, 0.0f, 0.99f) : std::max(0.0f, basePitch)) * 100.0f;
}

void MaszynaEmitter::play(int playFlags) {
    const bool hasMain = !def.main.path.isEmpty();
    if (!hasMain && std::none_of(def.chunks.begin(), def.chunks.end(), [](const Chunk &c) { return !c.sample.path.isEmpty(); }))
        return;
    flags = playFlags;
    if (def.range != -1.0f) {
        // Too far to start: as if started, so no begin part when it comes
        // within reach.
        if (heard.distance > std::min(StartCutoff, std::fabs(def.range * 5.0f))) {
            playBeginning = false;
            return;
        }
    }
    if (hasMain)
        playBasic();
    else
        playCombined();
}

void MaszynaEmitter::playBasic() {
    if (!playing()) {
        if (playBeginning && !def.begin.path.isEmpty()) {
            if (spawn(Part::Main, -1, true))
                playBeginning = false;
        } else {
            spawn(Part::Main, -1, false);
        }
    } else if ((flags & (Exclusive | Looping)) == 0 && def.begin.path.isEmpty()) {
        // A one-shot again while one plays: both heard.
        spawn(Part::Main, -1, false);
    }
}

bool MaszynaEmitter::covers(const Definition &d, int index, float point) {
    const Chunk &c = d.chunks[size_t(index)];
    // The last entry also at its own threshold (within half a point), where
    // MaSzyna's ends: there its tables in RPM went silent at the engine's
    // top speed (ST44's at 750).
    const float end = index + 1 == int(d.chunks.size()) ? c.fadeOut + 0.5f : c.fadeOut;
    return point >= c.fadeIn && point < end;
}

void MaszynaEmitter::playCombined() {
    const float p = point();
    for (int i = 0; i < int(def.chunks.size()); ++i) {
        const Chunk &chunk = def.chunks[size_t(i)];
        if (p < chunk.fadeIn)
            break;
        if (!covers(def, i, p))
            continue;
        if (chunk.sample.path.isEmpty() || ((flags & (Exclusive | Looping)) != 0 && chunkPlaying(i)))
            continue;
        if (i > 0 || def.begin.path.isEmpty()) {
            spawn(Part::Chunk, i, false);
        } else if (!beginPlaying()) {
            if (playBeginning) {
                if (spawn(Part::Chunk, i, true))
                    playBeginning = false;
            } else {
                spawn(Part::Chunk, i, false);
            }
        }
    }
}

bool MaszynaEmitter::spawn(Part part, int chunk, bool withBegin) {
    if (!SoundEngine::get().running())
        return false;
    const QString &path = part == Part::Main ? def.main.path : part == Part::End ? def.end.path
                                                                                 : def.chunks[size_t(chunk)].sample.path;
    // Not decoded yet (Ogg Vorbis on a worker): not started now; a sound
    // played each frame starts when it is there.
    const SoundClip *clip = clipOf(path);
    const SoundClip *begin = withBegin ? clipOf(def.begin.path) : nullptr;
    if (clip == nullptr || (withBegin && begin == nullptr))
        return false;
    Instance instance;
    instance.voice = std::make_unique<SoundVoice>(true);
    if (!instance.voice->valid())
        return false;
    instance.part = part;
    instance.chunk = chunk;
    instance.begin = begin;
    // The main part loops when asked; a table's entry after its begin part
    // always does.
    const bool loop = part == Part::End ? false
                      : part == Part::Chunk ? withBegin || (flags & Looping) != 0
                                            : (flags & Looping) != 0;
    if (begin != nullptr)
        instance.voice->player().queue(begin, StreamPlayer::Mode::OneShot);
    instance.voice->player().queue(clip, loop ? StreamPlayer::Mode::Loop : StreamPlayer::Mode::OneShot);
    instances.push_back(std::move(instance));
    ++starts;
    return true;
}

void MaszynaEmitter::stop(bool skipEnd) {
    playBeginning = true;
    flags = 0;
    if (!playing())
        return;
    kill(true);
    if (!skipEnd && !def.end.path.isEmpty() && endsPlaying() < 2)
        spawn(Part::End, -1, false);
}

void MaszynaEmitter::gain(float value) {
    baseGain = std::clamp(value, 0.0f, 2.0f);
}

void MaszynaEmitter::pitch(float value) {
    basePitch = value;
}

bool MaszynaEmitter::playing() const {
    return std::any_of(instances.begin(), instances.end(),
                       [](const Instance &i) { return i.part != Part::End && i.voice->player().busy(); });
}

bool MaszynaEmitter::beginPlaying() const {
    return std::any_of(instances.begin(), instances.end(), [](const Instance &i) {
        return i.begin != nullptr && i.voice->player().busy() && i.voice->player().currentClip() == i.begin;
    });
}

bool MaszynaEmitter::chunkPlaying(int chunk) const {
    return std::any_of(instances.begin(), instances.end(), [chunk](const Instance &i) {
        return i.part == Part::Chunk && i.chunk == chunk && i.voice->player().busy();
    });
}

int MaszynaEmitter::endsPlaying() const {
    return int(std::count_if(instances.begin(), instances.end(),
                             [](const Instance &i) { return i.part == Part::End && i.voice->player().busy(); }));
}

void MaszynaEmitter::kill(bool keepEnds) {
    for (Instance &i : instances)
        if (!(keepEnds && i.part == Part::End)) {
            i.voice->player().stop();
            i.voice->update(false);
        }
    instances.erase(std::remove_if(instances.begin(), instances.end(),
                                   [keepEnds](const Instance &i) { return !(keepEnds && i.part == Part::End); }),
                    instances.end());
}

float MaszynaEmitter::distanceGain(float distance, float range, float soundproofing) {
    if (range == -1.0f)
        return 1.0f;
    // Placed at the listener when its range is negative; its range still
    // fades it.
    const float reference = (range >= 0.0f ? range : 5.0f) / 16.0f * soundproofing;
    const float d = range >= 0.0f ? distance : 0.0f;
    float gain = 1.0f;
    if (d > reference)
        gain = reference > 0.0f ? reference / (reference + Rolloff * (d - reference)) : 0.0f;
    const float r = std::fabs(range);
    if (distance > r) {
        const float fade = 0.75f * r;
        gain *= 1.0f - std::clamp((distance * distance - r * r) / (fade * fade), 0.0f, 1.0f);
    }
    return gain;
}

float MaszynaEmitter::crossfadeGain(const Definition &d, int index, float point) {
    if (d.crossfade == 0)
        return 1.0f;
    const Chunk &c = d.chunks[size_t(index)];
    if (index > 0 && point < float(c.threshold)) {
        const float width = float(c.threshold) - c.fadeIn;
        const float linear = width > 0.0f ? std::clamp((point - c.fadeIn) / width, 0.0f, 1.0f) : 1.0f;
        // MaSzyna's approximation of a logarithmic fade in.
        return linear / (1.0f + (1.0f - linear) * -0.57f);
    }
    if (index + 1 < int(d.chunks.size())) {
        const float width = c.fadeOut - d.chunks[size_t(index) + 1].fadeIn;
        const float start = c.fadeOut - width;
        if (width > 0.0f && point > start) {
            const float linear = std::clamp((point - start) / width, 0.0f, 1.0f);
            return (1.0f - linear) / (1.0f + linear * -0.57f);
        }
    }
    return 1.0f;
}

float MaszynaEmitter::chunkPitch(const Definition &d, int index, float point) {
    const Chunk &c = d.chunks[size_t(index)];
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    if (point < float(c.threshold)) {
        if (index == 0)
            return 1.0f;
        const Chunk &previous = d.chunks[size_t(index) - 1];
        return lerp(previous.pitch / c.pitch, 1.0f,
                    std::clamp((point - float(previous.threshold)) / float(c.threshold - previous.threshold), 0.0f, 1.0f));
    }
    if (index + 1 >= int(d.chunks.size()))
        return 1.0f;
    const Chunk &next = d.chunks[size_t(index) + 1];
    return lerp(1.0f, next.pitch / c.pitch,
                std::clamp((point - float(c.threshold)) / float(next.threshold - c.threshold), 0.0f, 1.0f));
}

void MaszynaEmitter::update() {
    // Ended voices go.
    instances.erase(std::remove_if(instances.begin(), instances.end(),
                                   [](const Instance &i) { return !i.voice->player().busy(); }),
                    instances.end());
    const bool combined = def.main.path.isEmpty();
    const float p = point();
    for (size_t n = 0; n < instances.size();) {
        Instance &i = instances[n];
        const bool onBegin = i.begin != nullptr && i.voice->player().currentClip() == i.begin;
        bool drop = false;
        // A table's entry out of its range stops (a held sound's).
        if (combined && i.part == Part::Chunk && !onBegin && (flags & (Exclusive | Looping)) != 0) {
            drop = !covers(def, i.chunk, p);
        }
        // Too far to play on (sounds of several parts kept longer).
        if (def.range != -1.0f) {
            const float cutoff = i.begin != nullptr ? PlayCutoff : std::min(PlayCutoff, std::fabs(def.range) * 7.5f);
            drop = drop || heard.distance > cutoff;
        }
        if (drop) {
            i.voice->player().stop();
            i.voice->update(false);
            instances.erase(instances.begin() + qsizetype(n));
            continue;
        }
        float gain = baseGain;
        float pitch = basePitch;
        if (combined) {
            if (i.part == Part::Chunk && !onBegin) {
                pitch = chunkPitch(def, i.chunk, p);
                gain *= crossfadeGain(def, i.chunk, p);
            } else {
                // Bookends at their own pitch.
                pitch = 1.0f;
            }
        }
        // OpenAL keeps a source's gain at most 1 (AL_MAX_GAIN), after the
        // distance.
        i.gain = std::min(1.0f, gain * heard.soundproofing * distanceGain(heard.distance, def.range, heard.soundproofing));
        i.speed = std::clamp(pitch * def.pitchVariation, 0.1f, 10.0f) * heard.doppler;
        i.voice->setPosition(heard.position[0], heard.position[1], heard.position[2]);
        i.voice->setGain(i.gain);
        i.voice->player().setSpeed(i.speed);
        i.voice->update(true);
        ++n;
    }
}

void MaszynaEmitter::release() {
    instances.clear();
}

int MaszynaEmitter::voicesPlaying() const {
    return int(std::count_if(instances.begin(), instances.end(), [](const Instance &i) { return i.voice->player().isPlaying(); }));
}

float MaszynaEmitter::voiceGain(int index) const {
    return index >= 0 && index < int(instances.size()) ? instances[size_t(index)].gain : -1.0f;
}

float MaszynaEmitter::voiceSpeed(int index) const {
    return index >= 0 && index < int(instances.size()) ? instances[size_t(index)].speed : -1.0f;
}

// ---- MaszynaSounds ----

MaszynaSounds::MaszynaSounds(std::shared_ptr<const MaszynaVehicle> vehicle, std::shared_ptr<const Sounds> sounds)
    : vehicle(std::move(vehicle)), soundsRead(std::move(sounds)) {
    if (this->vehicle != nullptr) {
        const MaszynaFiz &fiz = this->vehicle->physics();
        physics = Physics::of(fiz);
        // MaszynaFiz gives MaxBP in Pa.
        const double maxBp = fiz.number(u"Brake", u"MaxBP");
        if (maxBp > 0.0)
            maxBrakePress = float(maxBp / 1e6);
        if (this->vehicle->massKg > 0.0)
            massKg = float(this->vehicle->massKg);
    }
    place();
    for (const auto &e : owned)
        preload(e->definition());
    SoundSystem::add(this);
}

MaszynaSounds::~MaszynaSounds() {
    SoundSystem::remove(this);
    if (frame.car != nullptr && cars().value(frame.car) == this)
        cars().remove(frame.car);
}

MaszynaSounds::RunningNoise MaszynaSounds::runningNoise() {
    static const RunningNoise choice = [] {
        const QString value = qEnvironmentVariable("TSRE_MASZYNA_RUNNING_NOISE").toLower();
        return value == QLatin1String("route")     ? RunningNoise::Route
               : value == QLatin1String("vehicle") ? RunningNoise::Vehicle
                                                   : RunningNoise::Both;
    }();
    return choice;
}

bool MaszynaSounds::trackSounds() const {
    return runningNoise() != RunningNoise::Vehicle;
}

QStringList MaszynaSounds::problems() const {
    return soundsRead != nullptr ? soundsRead->problems : QStringList();
}

namespace {

// The powertrain's sounds, at half the car's height when not placed
// (DynObj.cpp's defaults).
bool powertrain(QStringView key) {
    static const char *const keys[] = {
            "inverter", "relay", "shuntfield", "wejscie_na_bezoporow", "wejscie_na_drugi_uklad", "ventilator",
            "engine", "fakeengine", "ignition", "shutdown", "dieselinc", "turbo", "oilpump", "fuelpump", "waterpump",
            "waterheater", "radiatorfan1", "radiatorfan2", "transmission", "engageslippery", "retarder",
            "linebreakerclose", "linebreakeropen", "converter", "brakingresistorventilator", "compressor",
            "compressoridle", "small-compressor", "heater", "battery", "tractionmotor", "tractionacmotor",
            "motorblower"};
    for (const char *k : keys)
        if (key == QLatin1String(k))
            return true;
    return false;
}

bool coupler(QStringView key) {
    return key.endsWith(QLatin1String("attach")) || key.endsWith(QLatin1String("detach"))
           || key.startsWith(QLatin1String("couplerstretch")) || key.startsWith(QLatin1String("bufferclamp"))
           || key.startsWith(QLatin1String("coupleradapter"));
}

bool door(QStringView key) {
    return key == QLatin1String("dooropen") || key == QLatin1String("doorclose") || key == QLatin1String("doorlock")
           || key == QLatin1String("doorunlock") || key == QLatin1String("doorstepopen")
           || key == QLatin1String("doorstepclose") || key == QLatin1String("doorpermit");
}

}

void MaszynaSounds::place() {
    const float length = vehicle != nullptr ? vehicle->length : 0.0f;
    const float height = vehicle != nullptr ? vehicle->height : 0.0f;
    const float width = vehicle != nullptr ? vehicle->width : 0.0f;
    const Locations &locations = soundsRead->locations;
    for (const Definition &d : soundsRead->vehicle) {
        // Passenger announcements are not in this task.
        if (d.key == QLatin1String("departuresignal"))
            continue;
        std::vector<std::array<float, 3>> places;
        const std::array<float, 3> given = {d.offset[0], d.offset[1], d.offset[2]};
        const bool motor = d.key == QLatin1String("tractionmotor") || d.key == QLatin1String("tractionacmotor")
                           || d.key == QLatin1String("motorblower");
        if (motor && !locations.motors.empty()) {
            for (float z : locations.motors)
                places.push_back({given[0], given[1], z});
        } else if (d.key == QLatin1String("outernoise") && !locations.bogies.empty()) {
            for (float z : locations.bogies)
                places.push_back({given[0], given[1], z});
        } else if (coupler(d.key)) {
            // Its two ends, a metre up.
            places.push_back({0.0f, 1.0f, length / 2});
            places.push_back({0.0f, 1.0f, -length / 2});
        } else if (door(d.key)) {
            for (const Locations::Door &door : locations.doors) {
                if (door.left)
                    places.push_back({width / 2, height / 2, door.z});
                if (door.right)
                    places.push_back({-width / 2, 2.0f, door.z});
            }
            // The sides, in the same order.
            std::vector<bool> left;
            for (const Locations::Door &door : locations.doors) {
                if (door.left)
                    left.push_back(true);
                if (door.right)
                    left.push_back(false);
            }
            for (size_t i = 0; i < places.size(); ++i) {
                owned.push_back(std::make_unique<MaszynaEmitter>(d, places[i].data()));
                byKey[d.key].push_back(owned.back().get());
                doorLeft.insert(owned.back().get(), left[i]);
            }
            continue;
        } else if (d.key == QLatin1String("pantographup") || d.key == QLatin1String("pantographdown")) {
            // On the roof over each pantograph (the .mmd's count; MaSzyna
            // takes their places from the model).
            const int count = vehicle != nullptr ? vehicle->animations[5] : 0;
            for (int i = 0; i < count; ++i)
                places.push_back({0.0f, height, count == 1 ? 0.0f : length / 4 * (i == 0 ? 1.0f : -1.0f)});
        } else if (d.hasOffset()) {
            places.push_back(given);
        } else if (powertrain(d.key)) {
            places.push_back({0.0f, height / 2, 0.0f});
        } else {
            places.push_back(given);
        }
        for (const auto &at : places) {
            owned.push_back(std::make_unique<MaszynaEmitter>(d, at.data()));
            byKey[d.key].push_back(owned.back().get());
        }
    }
}

void MaszynaSounds::placeCab() {
    // The cab's middle, a metre up (Train.cpp's default for its sounds);
    // controls' sounds go on their parts (step 6).
    const MaszynaCab::End *end = nullptr;
    if (vehicle != nullptr) {
        const MaszynaCab &cab = vehicle->cab();
        end = cab.ends[1].defined ? &cab.ends[1] : cab.ends[2].defined ? &cab.ends[2] : nullptr;
    }
    std::array<float, 3> middle = {0.0f, 1.0f, 0.0f};
    if (end != nullptr)
        middle = {(end->box[0] + end->box[3]) / 2, (end->box[1] + end->box[4]) / 2 + 1.0f, (end->box[2] + end->box[5]) / 2};
    for (const Definition &d : soundsRead->cab) {
        const std::array<float, 3> at = d.hasOffset() ? std::array<float, 3>{d.offset[0], d.offset[1], d.offset[2]} : middle;
        cabOwned.push_back(std::make_unique<MaszynaEmitter>(d, at.data()));
        cabByKey.insert(d.key, cabOwned.back().get());
        preload(d);
    }
}

const std::vector<MaszynaEmitter *> &MaszynaSounds::emitters(QStringView key) const {
    static const std::vector<MaszynaEmitter *> none;
    const auto found = byKey.constFind(key.toString());
    return found != byKey.constEnd() ? found.value() : none;
}

MaszynaEmitter *MaszynaSounds::cab(QStringView key) const {
    return cabByKey.value(key.toString(), nullptr);
}

void MaszynaSounds::set(const Frame &f) {
    if (frame.car != f.car) {
        if (frame.car != nullptr && cars().value(frame.car) == this)
            cars().remove(frame.car);
        if (f.car != nullptr)
            cars().insert(f.car, this);
    }
    // The car's velocity from its moves, smoothed; a jump is no move.
    const double place[3] = {f.position[0] + 2048.0 * f.tileX, f.position[1], f.position[2] + 2048.0 * f.tileZ};
    if (placeKnown && f.seconds > 0.0f) {
        float moved[3];
        float speed2 = 0.0f;
        for (int i = 0; i < 3; ++i) {
            moved[i] = float((place[i] - lastPlace[i]) / f.seconds);
            speed2 += moved[i] * moved[i];
        }
        const float k = std::min(1.0f, f.seconds / 0.25f);
        for (int i = 0; i < 3; ++i)
            velocity[i] += ((speed2 > 150.0f * 150.0f ? 0.0f : moved[i]) - velocity[i]) * k;
    }
    std::copy(place, place + 3, lastPlace);
    placeKnown = true;
    frame = f;
    framed = true;
    if (frame.lead && cabOwned.empty())
        placeCab();
    else if (!frame.lead && !cabOwned.empty()) {
        cabByKey.clear();
        cabOwned.clear();
        controls.clear();
        controlOwned.clear();
        controlDefinitions.clear();
        controlsOf = nullptr;
    }
}

void MaszynaSounds::event(int trigger) {
    triggers.push_back(trigger);
}

void MaszynaSounds::event(const QString &name) {
    events << name;
}

int MaszynaSounds::column(const SoundListener &listener, const void *car) {
    if (listener.attachedTo == nullptr || listener.attachedTo != car)
        return 5;
    switch (listener.view) {
    case SoundView::Cab:
        return listener.cab == 2 ? 0 : 2;
    case SoundView::Passenger:
        return 1;
    default:
        return 4;
    }
}

float MaszynaSounds::soundproofing(const Definition &d, const std::array<std::array<float, 6>, 5> &table, int column,
                                   const std::array<std::array<float, 6>, 5> *listenerTable, int listenerColumn,
                                   float window) {
    const auto cab = [](int c) { return c == 0 || c == 2; };
    window = std::clamp(window, 0.0f, 1.0f);
    float factor = 1.0f;
    if (d.placement != Placement::General) {
        const int row = int(d.placement) - 1;
        const auto at = [&](int c) {
            return d.soundproofing ? (*d.soundproofing)[size_t(c)] : table[size_t(row)][size_t(c)];
        };
        factor = at(column);
        // Its car's cab, a window open: the outside comes in.
        if (window > 0.0f && cab(column) && d.placement != Placement::Internal)
            factor += window * (std::max(factor, std::min(at(3), at(4))) - factor);
        // The listener in another car: through its outside (and its open
        // window).
        if (listenerTable != nullptr) {
            const auto &outside = (*listenerTable)[size_t(Placement::External) - 1];
            float through = outside[size_t(listenerColumn)];
            if (window > 0.0f && cab(listenerColumn))
                through += window * (std::min(1.0f, std::max(through, outside[3])) - through);
            factor *= through;
        }
    }
    if (d.range < -1.0f)
        factor *= 0.4f;
    // -1 of a sound's own table reaches MaSzyna's OpenAL as a gain it
    // refuses: silent here.
    return std::max(0.0f, factor);
}

void MaszynaSounds::update(const SoundListener &listener, double clock) {
    if (!framed)
        return;
    const float seconds = lastClock >= 0.0 ? float(std::max(0.0, clock - lastClock)) : 0.0f;
    lastClock = clock;

    // Where each emitter is heard from.
    const int own = column(listener, frame.car);
    const std::array<std::array<float, 6>, 5> *listenerTable = nullptr;
    int listenerColumn = 5;
    static const std::array<std::array<float, 6>, 5> defaults = defaultSoundproofing();
    if (listener.attachedTo != nullptr && listener.attachedTo != frame.car) {
        const MaszynaSounds *other = cars().value(listener.attachedTo, nullptr);
        listenerTable = other != nullptr ? &other->soundsRead->soundproofing : &defaults;
        listenerColumn = column(listener, listener.attachedTo);
    }
    float left[3] = {frame.up[1] * frame.front[2] - frame.up[2] * frame.front[1],
                     frame.up[2] * frame.front[0] - frame.up[0] * frame.front[2],
                     frame.up[0] * frame.front[1] - frame.up[1] * frame.front[0]};
    const float base[3] = {frame.position[0] + 2048.0f * float(frame.tileX - listener.tileX), frame.position[1],
                           frame.position[2] + 2048.0f * float(frame.tileZ - listener.tileZ)};
    float sourceVelocity[3];
    const float speed = std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
    const float cap = speed > MaxVelocity ? MaxVelocity / speed : 1.0f;
    float listenerVelocity[3];
    for (int i = 0; i < 3; ++i) {
        sourceVelocity[i] = velocity[i] * cap * DopplerScale;
        listenerVelocity[i] = listener.velocity[i] * DopplerScale;
    }
    const auto hear = [&](MaszynaEmitter &e, const std::array<std::array<float, 6>, 5> &table) {
        const Definition &d = e.definition();
        const float *at = e.place();
        MaszynaEmitter::Hearing h;
        float to[3];
        for (int i = 0; i < 3; ++i) {
            h.position[i] = base[i] + left[i] * at[0] + frame.up[i] * at[1] + frame.front[i] * at[2];
            to[i] = h.position[i] - listener.position[i];
        }
        h.distance = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        if (d.range < 0.0f) {
            // Placed at the listener, no Doppler.
            std::copy(listener.position, listener.position + 3, h.position);
        } else {
            h.doppler = SoundSource::dopplerFactor(to, sourceVelocity, listenerVelocity);
        }
        h.soundproofing = soundproofing(d, table, own, listenerTable, listenerColumn, listener.windowShare());
        e.hear(h);
    };
    for (const auto &list : {&owned, &cabOwned, &controlOwned})
        for (const auto &e : *list)
            hear(*e, soundsRead->soundproofing);

    drive(listener, seconds);

    isHeard = false;
    for (const auto &list : {&owned, &cabOwned, &controlOwned})
        for (const auto &e : *list) {
            e->update();
            isHeard = isHeard || e->voiceCount() > 0;
        }
    static const bool logging = qEnvironmentVariableIsSet("TSRE_SOUND_LOG");
    if (logging && isHeard && clock >= nextLog) {
        nextLog = clock + 5.0;
        QStringList heard;
        QMap<QString, int> started;
        for (const auto &list : {&owned, &cabOwned, &controlOwned})
            for (const auto &e : *list) {
                const QString name = (list == &cabOwned ? QStringLiteral("cab:") : list == &controlOwned ? QStringLiteral("control:") : QString())
                                     + e->definition().key;
                if (e->voiceCount() > 0 && !heard.contains(name))
                    heard << name;
                if (const int n = e->takeStarts())
                    started[name] += n;
            }
        QStringList starts;
        for (auto i = started.cbegin(); i != started.cend(); ++i)
            starts << QString("%1×%2").arg(i.key()).arg(i.value());
        qInfo().noquote() << "Sound: MaSzyna" << (vehicle != nullptr ? vehicle->type : QString()) << heard.join(' ')
                          << "| started:" << starts.join(' ');
    }
}

void MaszynaSounds::drive(const SoundListener &listener, float seconds) {
    driveHost(listener, seconds);
    driveScript();
    events.clear();
    triggers.clear();
}

std::vector<MaszynaEmitter *> MaszynaSounds::select(const QString &key, const QString &place) const {
    if (MaszynaEmitter *e = cab(key))
        return {e};
    std::vector<MaszynaEmitter *> out;
    for (MaszynaEmitter *e : emitters(key)) {
        const bool chosen = place.isEmpty()
                            || (place == QLatin1String("left") && doorLeft.value(e, false))
                            || (place == QLatin1String("right") && doorLeft.contains(e) && !doorLeft.value(e))
                            || (place == QLatin1String("front") && e->place()[2] > 0.0f)
                            || (place == QLatin1String("rear") && e->place()[2] <= 0.0f);
        if (chosen)
            out.push_back(e);
    }
    return out;
}

void MaszynaSounds::driveScript() {
    // Held on: played each frame while given, stopped when no longer. A
    // name may give the place, "motorblower:front".
    const auto placed = [this](const QString &name) {
        const qsizetype colon = name.indexOf(QLatin1Char(':'));
        return colon < 0 ? select(name, QString()) : select(name.left(colon), name.mid(colon + 1));
    };
    QStringList now;
    if (frame.model != nullptr)
        for (auto i = frame.model->named.cbegin(); i != frame.model->named.cend(); ++i) {
            if (i.key().endsWith(QLatin1String(":pitch")))
                continue;
            now << i.key();
            const auto pitch = frame.model->named.constFind(i.key() + QLatin1String(":pitch"));
            for (MaszynaEmitter *e : placed(i.key())) {
                e->gain(i.value());
                if (pitch != frame.model->named.cend())
                    e->pitch(pitch.value());
                e->play(MaszynaEmitter::Exclusive | MaszynaEmitter::Looping);
            }
        }
    for (const QString &key : held)
        if (!now.contains(key))
            for (MaszynaEmitter *e : placed(key))
                e->stop();
    held = now;

    // Played once. Those MaSzyna plays one at a time, and the pairs where
    // one stops the other.
    static const QStringList exclusive = {"pantographup", "pantographdown", "ignition", "shutdown", "springbrake",
                                          "springbrakeoff", "startjolt", "brakeacc", "derail", "dooropen",
                                          "doorclose", "doorstepopen", "doorstepclose", "doorlock", "doorunlock",
                                          "doorpermit", "distancecounter"};
    static const QHash<QString, QString> counterpart = {{"dooropen", "doorclose"}, {"doorclose", "dooropen"},
                                                        {"doorstepopen", "doorstepclose"},
                                                        {"doorstepclose", "doorstepopen"},
                                                        {"springbrake", "springbrakeoff"},
                                                        {"springbrakeoff", "springbrake"},
                                                        {"ignition", "shutdown"}, {"shutdown", "ignition"}};
    for (const QString &name : events) {
        const QStringList parts = name.split(QLatin1Char(':'));
        const QString &key = parts[0];
        QString place;
        std::vector<float> numbers;
        for (int i = 1; i < parts.size(); ++i) {
            bool ok = false;
            const float v = parts[i].toFloat(&ok);
            if (ok)
                numbers.push_back(v);
            else
                place = parts[i];
        }
        const auto other = counterpart.constFind(key);
        if (other != counterpart.cend())
            for (MaszynaEmitter *e : select(other.value(), place))
                e->stop();
        for (MaszynaEmitter *e : select(key, place)) {
            // MaSzyna's pitch (a table's value / 100), its gain.
            if (!numbers.empty())
                e->pitch(numbers[0]);
            if (numbers.size() > 1)
                e->gain(numbers[1]);
            e->play(exclusive.contains(key) ? MaszynaEmitter::Exclusive : MaszynaEmitter::OneShot);
        }
    }
}

void MaszynaSounds::driveHost(const SoundListener &listener, float dt) {
    const SoundValues *values = frame.values;
    const float speed = values != nullptr ? values->get(Sms::Control::Speed, 0) : 0.0f;
    // MaSzyna's units: km/h, MPa.
    const float vel = speed * 3.6f;
    const float vmax = float(physics.vmax);
    const float brakePress = values != nullptr ? values->get(Sms::Control::BrakeCyl, 0) * 0.00689476f : 0.0f;
    // MaSzyna's own pressures are bar numbers (its .fiz's MaxBP 4.2): its
    // ratios and rates taken in them.
    const float brakeBar = brakePress * 10.0f, maxBrakeBar = maxBrakePress * 10.0f;
    const float pressureRatio = std::max(0.0f, brakeBar) / std::max(1.0f, maxBrakeBar);
    // MaSzyna's brake force ratio (UnitBrakeForce against BrakeForceR):
    // the cylinder's share of its most here.
    const bool braking = brakePress > 0.005f && vel > 0.05f;
    const float brakeForceRatio = braking ? std::clamp(brakePress / maxBrakePress, 0.0f, 1.0f) : 0.0f;
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const auto all = [this](QStringView key, const auto &f) {
        for (MaszynaEmitter *e : emitters(key))
            f(*e);
    };
    // The listener aboard this car's train (cab or passenger view), and in
    // this car.
    const bool aboard = frame.train != nullptr && listener.train == frame.train && listener.view != SoundView::External;
    const bool inCar = listener.attachedTo == frame.car && listener.view != SoundView::External;
    const bool own = runningNoise() != RunningNoise::Route;

    // The running noise outside, per bogie; kept from those aboard (they
    // hear the cab's). TSRE's tracks carry no quality: MaSzyna's middle.
    const std::vector<MaszynaEmitter *> &bogies = emitters(u"outernoise");
    if (own && vel > 0.5f && !bogies.empty() && !aboard) {
        const Definition &d = bogies.front()->definition();
        const float normalizer = d.combined() ? vmax * 0.01f : 1.0f;
        const float frequency = d.frequencyOffset + d.frequencyFactor * vel * normalizer;
        float volume = d.amplitudeOffset + d.amplitudeFactor * vel;
        if (brakeForceRatio > 0.0f)
            volume *= 1.0f + 0.125f * brakeForceRatio;
        if (!d.combined())
            volume *= std::clamp(vel / 40.0f, 0.0f, 1.0f);
        for (MaszynaEmitter *e : bogies) {
            if (volume > 0.05f) {
                e->pitch(frequency);
                e->gain(volume);
                e->play(MaszynaEmitter::Exclusive | MaszynaEmitter::Looping);
            } else {
                e->stop();
            }
        }
    } else {
        all(u"outernoise", [&](MaszynaEmitter &e) { e.stop(aboard); });
    }

    // Curves: the sideways acceleration (the host's curve force over the
    // car's mass), louder with speed.
    const float accN = values != nullptr ? std::fabs(values->get(Sms::Control::CurveForce, 0)) / massKg : 0.0f;
    const float curve = vel > 5.0f ? accN * lerp(0.5f, 1.0f, std::clamp(vel / 40.0f, 0.0f, 1.0f)) : 0.0f;
    all(u"curve", [&](MaszynaEmitter &e) {
        if (curve > 0.05f) {
            const Definition &d = e.definition();
            e.pitch(d.combined() ? vel * 0.01f : d.frequencyOffset + d.frequencyFactor);
            e.gain(2.5f * curve);
            e.play(MaszynaEmitter::Exclusive | MaszynaEmitter::Looping);
        } else {
            e.stop();
        }
    });

    // Braking: the shoes' noise, louder with the force, pitched by speed.
    const auto brakeNoise = [&](MaszynaEmitter &e) {
        const Definition &d = e.definition();
        if (braking) {
            e.pitch(d.frequencyOffset + vel * d.frequencyFactor);
            e.gain(d.amplitudeOffset + std::sqrt(brakeForceRatio * lerp(0.4f, 1.0f, vel / (1.0f + vmax))) * d.amplitudeFactor);
            e.play(MaszynaEmitter::Exclusive | MaszynaEmitter::Looping);
        } else {
            e.stop();
        }
    };
    all(u"brakesound", brakeNoise);
    // Squealing shoes, dying away when nearly stopped.
    all(u"brake", [&](MaszynaEmitter &e) {
        const Definition &d = e.definition();
        float volume = 0.0f;
        if (vel > 2.5f) {
            volume = d.amplitudeOffset + lerp(-1.0f, 1.0f, brakeForceRatio) * d.amplitudeFactor;
            if (volume > 0.075f) {
                e.pitch(d.combined() ? vel * 0.01f : d.frequencyOffset + d.frequencyFactor);
                e.gain(volume);
                e.play(MaszynaEmitter::Exclusive | MaszynaEmitter::Looping);
            }
        } else {
            volume = std::max(0.0f, e.gain() - e.gain() * 2.5f * dt);
            e.gain(volume);
        }
        if (volume < 0.05f)
            e.stop();
    });
    // The brake cylinder's piston: a click each fifteenth of its most, in
    // and out (brakecylinderinc, brakecylinderdec; DynObj.cpp's), a combined
    // table's entry by the step.
    if (lastBrakePress >= 0.0f) {
        const int step = int(15.0f * pressureRatio);
        const int was = int(15.0f * std::max(0.0f, lastBrakePress) / std::max(1.0f, maxBrakeBar));
        if (step != was)
            all(step > was ? u"brakecylinderinc" : u"brakecylinderdec", [&](MaszynaEmitter &e) {
                const Definition &d = e.definition();
                e.pitch(d.combined() ? step * 0.01f : d.frequencyOffset + d.frequencyFactor);
                e.play(MaszynaEmitter::OneShot);
            });
    }
    // The release hiss: the cylinder emptying (bar/s).
    if (lastBrakePress >= 0.0f && dt > 0.0f)
        unbrakeRate = lerp(unbrakeRate, (lastBrakePress - brakeBar) / dt, 0.05f);
    lastBrakePress = brakeBar;
    all(u"unbrake", [&](MaszynaEmitter &e) {
        const float volume = e.definition().amplitudeFactor * unbrakeRate * (0.25f + 0.75f * pressureRatio);
        if (unbrakeRate > 0.05f && pressureRatio > 0.05f) {
            e.gain(volume);
            e.play(MaszynaEmitter::Exclusive | MaszynaEmitter::Looping);
        } else {
            const float fading = std::max(0.0f, e.gain() - 0.5f * dt);
            e.gain(fading);
            if (fading < 0.05f)
                e.stop();
        }
    });

    // The jolt of starting, once a start, at 1 to 15 km/h.
    const float acceleration = dt > 0.0f ? (speed - lastSpeed) / dt : 0.0f;
    lastSpeed = speed;
    if (vel < 0.1f)
        startJoltPlayed = false;
    else if (!startJoltPlayed && acceleration > 0.1f && vel > 1.0f && vel < 15.0f) {
        all(u"startjolt", [](MaszynaEmitter &e) { e.play(MaszynaEmitter::Exclusive); });
        startJoltPlayed = true;
    }

    // The ridden cab's noises, heard in it (TTrain::update_sounds_radio).
    const auto runningNoise = [&](MaszynaEmitter &e) {
        const Definition &d = e.definition();
        const float normalizer = d.combined() ? vmax * 0.01f : 1.0f;
        const float frequency = d.frequencyOffset + d.frequencyFactor * vel * normalizer;
        float volume = d.amplitudeOffset + d.amplitudeFactor * lerp(vel / (1.0f + vmax), 1.0f, 0.5f);
        if (vel > 0.0f)
            volume *= 1.0f + 0.125f * brakeForceRatio;
        if (!d.combined())
            volume *= std::clamp(vel / 25.0f, 0.0f, 1.0f);
        return std::pair<float, float>(frequency, volume);
    };
    if (MaszynaEmitter *e = cab(u"runningnoise")) {
        if (own && inCar && vel > 0.5f) {
            const auto [frequency, volume] = runningNoise(*e);
            if (volume > 0.05f) {
                e->pitch(frequency);
                e->gain(volume);
                e->play(MaszynaEmitter::Exclusive | MaszynaEmitter::Looping);
            } else {
                e->stop();
            }
        } else {
            e->stop(!inCar);
        }
    }
    if (MaszynaEmitter *e = cab(u"huntingnoise")) {
        const auto &spring = soundsRead->huntingSpring;
        const float amount = spring[2] > 0.0f && spring[3] > spring[2]
                                     ? std::clamp((vel - spring[2]) / (spring[3] - spring[2]), 0.0f, 1.0f)
                                     : 0.0f;
        if (inCar && vel > 0.5f && amount > 0.025f) {
            const auto [frequency, volume] = runningNoise(*e);
            if (volume > 0.05f) {
                e->pitch(frequency);
                e->gain(volume * amount);
                e->play(MaszynaEmitter::Exclusive | MaszynaEmitter::Looping);
            } else {
                e->stop();
            }
        } else {
            e->stop(!inCar);
        }
    }
    for (const char16_t *key : {u"resonancenoise", u"windsound"})
        if (MaszynaEmitter *e = cab(key)) {
            const Definition &d = e->definition();
            const float frequency = d.frequencyOffset + d.frequencyFactor * vel * vmax * 0.01f;
            const float volume = d.amplitudeOffset + d.amplitudeFactor * lerp(vel / (1.0f + vmax), 1.0f, 0.5f);
            if (inCar && vel > 0.5f && volume > 0.05f) {
                e->pitch(frequency);
                e->gain(volume);
                e->play(MaszynaEmitter::Exclusive | MaszynaEmitter::Looping);
            } else {
                e->stop(!inCar);
            }
        }
    // The cab's brake noise: silent, not stopped, when the listener is out.
    if (MaszynaEmitter *e = cab(u"brakesound")) {
        brakeNoise(*e);
        if (!inCar)
            e->gain(0.0f);
    }
    // Rain on the ridden cab (Train.cpp's rain sound; every cab has one, its
    // own or the default), heard in it, a combined table's entry by the
    // rain's strength. MaSzyna's "window open" is its head-out view (Ctrl+
    // arrows, the camera at the mirror, outside the cab): there it plays the
    // outside rain (its environment's) instead, as in its free view; TSRE
    // has neither that view nor an outside rain sound.
    if (MaszynaEmitter *e = cab(u"rainsound")) {
        const Definition &d = e->definition();
        if (frame.lead && inCar && listener.view == SoundView::Cab && frame.rain > 0.0f) {
            e->pitch(d.combined() ? frame.rain : d.frequencyOffset + d.frequencyFactor);
            e->gain(d.amplitudeOffset + d.amplitudeFactor);
            e->play(MaszynaEmitter::Exclusive | MaszynaEmitter::Looping);
        } else {
            e->stop(!inCar);
        }
    }
    // The cab's own ambience (fadesound: a clock), always in the ridden cab.
    if (MaszynaEmitter *e = cab(u"fadesound")) {
        if (frame.lead)
            e->play(MaszynaEmitter::Exclusive | MaszynaEmitter::Looping);
        else
            e->stop();
    }
}

void MaszynaSounds::control(const CabControls *cabControls, int index, double value, const float *place) {
    const auto *maszyna = dynamic_cast<const MaszynaCabControls *>(cabControls);
    if (maszyna == nullptr || !frame.lead || index < 0 || index >= maszyna->end().controls.size())
        return;
    if (controlsOf != cabControls) {
        // Another cab: its controls' sounds read again.
        controls.clear();
        controlOwned.clear();
        controlDefinitions.clear();
        controlsOf = cabControls;
        random.seed(quint32(qHash(vehicle != nullptr ? vehicle->type : QString())) ^ quint32(quintptr(frame.car)));
    }
    const MaszynaCab::Control &c = maszyna->end().controls[index];
    ControlSounds &s = controls[index];
    // The cab's sounds that fall back for it play at its part (Train.cpp's
    // places for ctrl:, ctrlscnd:, reverserkey:).
    const QString fallback = c.label == QLatin1String("mainctrl") || c.label == QLatin1String("jointctrl") ? QStringLiteral("ctrl")
                             : c.label == QLatin1String("scndctrl")                                     ? QStringLiteral("ctrlscnd")
                             : c.label == QLatin1String("dirkey")                                       ? QStringLiteral("reverserkey")
                             : c.label == QLatin1String("antislip_bt")                                  ? QStringLiteral("pneumaticswitch")
                             : MaszynaCabControls::worked(c.label) && !c.lamp                           ? QStringLiteral("switch")
                                                                                                        : QString();
    if (!s.seen) {
        s.seen = true;
        s.target = value;
        // Its sounds, read with the car's random, at its part.
        for (const auto &key : c.keys) {
            if (!key.first.startsWith(QLatin1String("sound")) || key.first == QLatin1String("soundproofing")
                || key.second.isEmpty())
                continue;
            controlDefinitions.push_back(MaszynaSound::readValue(key.first, key.second, soundsRead->soundFolder, &random));
            const MaszynaSound::Definition &d = controlDefinitions.back();
            const float at[3] = {place != nullptr ? place[0] : 0.0f, place != nullptr ? place[1] : 1.0f,
                                 place != nullptr ? place[2] : 0.0f};
            controlOwned.push_back(std::make_unique<MaszynaEmitter>(d, d.hasOffset() ? d.offset : at));
            s.emitters.insert(key.first, controlOwned.back().get());
            preload(d);
        }
        if (place != nullptr && (fallback == QLatin1String("ctrl") || fallback == QLatin1String("ctrlscnd")
                                 || fallback == QLatin1String("reverserkey")))
            if (MaszynaEmitter *e = cab(fallback); e != nullptr && !e->definition().hasOffset())
                e->setPlace(place);
        return;
    }
    const double previous = s.target;
    // TButton: a lamp lighting or going out.
    if (c.lamp) {
        s.target = value;
        const bool lit = value > 0.5, was = previous > 0.5;
        if (lit != was)
            if (MaszynaEmitter *e = s.emitters.value(lit ? QStringLiteral("soundinc") : QStringLiteral("sounddec")))
                e->play();
        return;
    }
    // TGauge::UpdateValue: nothing unless it changed by a hundredth.
    const long desired = std::lround(100.0 * value);
    if (desired == std::lround(100.0 * previous))
        return;
    s.target = value;
    if (desired % 100 == 0) {
        if (MaszynaEmitter *e = s.emitters.value(QStringLiteral("sound") + QString::number(desired / 100))) {
            e->play();
            return;
        }
    } else {
        s.exclusive = true;
    }
    const int flags = s.exclusive ? MaszynaEmitter::Exclusive : MaszynaEmitter::OneShot;
    MaszynaEmitter *e = value > previous ? s.emitters.value(QStringLiteral("soundinc"))
                        : value < previous ? s.emitters.value(QStringLiteral("sounddec"))
                                           : nullptr;
    if (e == nullptr && !fallback.isEmpty())
        e = cab(fallback);
    if (e != nullptr)
        e->play(flags);
}

void MaszynaSounds::release() {
    for (const auto &list : {&owned, &cabOwned, &controlOwned})
        for (const auto &e : *list)
            e->release();
    isHeard = false;
}

int MaszynaSounds::voiceCount() const {
    int count = 0;
    for (const auto &list : {&owned, &cabOwned, &controlOwned})
        for (const auto &e : *list)
            count += e->voiceCount();
    return count;
}

int MaszynaSounds::voicesPlaying() const {
    int count = 0;
    for (const auto &list : {&owned, &cabOwned, &controlOwned})
        for (const auto &e : *list)
            count += e->voicesPlaying();
    return count;
}
