/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/sound/SoundSource.h>
#include <tsre/sound/SoundEngine.h>
#include <tsre/sound/SoundSystem.h>

#include <QFileInfo>
#include <algorithm>
#include <cfloat>
#include <cmath>

using Sms::Command;
using Sms::Control;
using Sms::Trigger;

float SoundValues::get(Control control, int index) const {
    const std::vector<float> *list = nullptr;
    switch (control) {
    case Control::Variable1: list = &variable1; break;
    case Control::EngineRpm: list = &engineRpm; break;
    case Control::EnginePower: list = &enginePower; break;
    case Control::EngineTorque: list = &engineTorque; break;
    default: break;
    }
    if (list != nullptr && !list->empty())
        return index >= 0 && index < int(list->size()) ? (*list)[size_t(index)] : 0.0f;
    if (list != nullptr && index != 0)
        return 0.0f;
    return control < Control::Count ? value[int(control)] : 0.0f;
}

float SoundSource::rolloffFor(float maxDistance) {
    // OpenAL's inverse distance: gain = ref / (ref + rolloff (d - ref)),
    // GainAtMaxDistance at maxDistance.
    const float distance = std::max(maxDistance, 2.0f * ReferenceDistance);
    return ReferenceDistance * (1.0f / GainAtMaxDistance - 1.0f) / (distance - ReferenceDistance);
}

float SoundSource::dopplerFactor(const float *toSource, const float *sourceVelocity, const float *listenerVelocity) {
    constexpr float SpeedOfSound = 343.3f;
    const float length = std::sqrt(toSource[0] * toSource[0] + toSource[1] * toSource[1] + toSource[2] * toSource[2]);
    if (length < 0.001f)
        return 1.0f;
    float listenerTowards = 0.0f;
    float sourceAway = 0.0f;
    for (int i = 0; i < 3; ++i) {
        listenerTowards += listenerVelocity[i] * toSource[i] / length;
        sourceAway += sourceVelocity[i] * toSource[i] / length;
    }
    // Nothing goes near the speed of sound here; keep it finite.
    const float limit = 0.9f * SpeedOfSound;
    listenerTowards = std::clamp(listenerTowards, -limit, limit);
    sourceAway = std::clamp(sourceAway, -limit, limit);
    return (SpeedOfSound + listenerTowards) / (SpeedOfSound + sourceAway);
}

void SoundSource::setCutoffDistance(float metres) {
    cutoff2 = std::max(CutoffDistanceSquared, metres * metres);
}

float SoundSource::distanceGain(float distance, float rolloff) {
    const float d = std::clamp(distance, ReferenceDistance, MaxDistance);
    return ReferenceDistance / (ReferenceDistance + rolloff * (d - ReferenceDistance));
}

SoundSource::SoundSource(const QString &path, Kind kind, const void *owner)
    : smsPath(path), sourceKind(kind), owner(owner) {
    SoundSystem::add(this);
    folders = SoundSystem::waveFolders(QFileInfo(path).path());
    hasLocation = kind != Kind::Everywhere;
    file = Sms::shared(path);
    if (file == nullptr)
        return;
    group = file->group(SoundSystem::DetailLevel);
    if (group == nullptr)
        return;
    external = group->activation.externalCam;
    ignore3d = group->ignore3d || group->stereo;
    slowRolloff = kind == Kind::World;
    const float deactivation = group->deactivation.present && group->deactivation.distance != 0.0f
            ? group->deactivation.distance : MaxDistance;
    rolloff = slowRolloff ? SlowRolloff : rolloffFor(std::min(MaxDistance, deactivation));

    streams.resize(group->streams.size());
    for (size_t i = 0; i < streams.size(); ++i) {
        StreamState &s = streams[i];
        s.def = &group->streams[i];
        s.volume = s.def->volume;
        s.triggers.resize(s.def->triggers.size());
        for (size_t j = 0; j < s.triggers.size(); ++j) {
            const Trigger &t = s.def->triggers[j];
            // Triggers that need a car, or a car's values, are not played
            // for other sounds; joints, switches and crossovers need the
            // track under the wheels (not yet).
            bool applies = t.command.type != Command::Type::None;
            switch (t.type) {
            case Trigger::Type::DistanceTravelled:
                applies &= kind == Kind::Car;
                break;
            case Trigger::Type::Variable:
                applies &= kind != Kind::Everywhere;
                break;
            case Trigger::Type::Joint:
            case Trigger::Type::Switch:
            case Trigger::Type::Crossover:
            case Trigger::Type::Unknown:
                applies = false;
                break;
            default:
                break;
            }
            if (!applies)
                continue;
            s.triggers[j].def = &t;
            if (t.type == Trigger::Type::Initial)
                s.initial = int(j);
            s.releasedWithJump |= t.command.type == Command::Type::ReleaseLoopReleaseWithJump;
        }
    }
}

SoundSource::~SoundSource() {
    SoundSystem::remove(this);
}

void SoundSource::setLocation(int tileX, int tileZ, const float *position) {
    locationTile[0] = tileX;
    locationTile[1] = tileZ;
    std::copy(position, position + 3, location);
    locationSet = true;
}

void SoundSource::setEnabled(bool value) {
    enabled = value;
}

void SoundSource::event(int id) {
    for (StreamState &s : streams)
        for (TriggerState &t : s.triggers)
            if (t.def != nullptr && t.def->type == Trigger::Type::Discrete && t.def->event == id)
                t.triggered = true;
}

void SoundSource::release() {
    for (StreamState &s : streams)
        s.voice.reset();
    wasOutOfDistance = true;
    isActive = false;
}

float SoundSource::streamGain(int stream) const {
    return stream >= 0 && stream < int(streams.size()) ? streams[size_t(stream)].gain : 0.0f;
}

float SoundSource::streamSpeed(int stream) const {
    return stream >= 0 && stream < int(streams.size()) ? streams[size_t(stream)].speed : 0.0f;
}

bool SoundSource::streamPlaying(int stream) const {
    if (stream < 0 || stream >= int(streams.size()) || streams[size_t(stream)].voice == nullptr)
        return false;
    return const_cast<SoundVoice &>(*streams[size_t(stream)].voice).player().isPlaying();
}

const SoundVoice *SoundSource::voice(int stream) const {
    return stream >= 0 && stream < int(streams.size()) ? streams[size_t(stream)].voice.get() : nullptr;
}

void SoundSource::update(const SoundListener &listener, double clock) {
    currentListener = &listener;
    currentClock = clock;
    if (locator) {
        int tile[2];
        float position[3];
        enabled = locator(tile[0], tile[1], position);
        if (enabled)
            setLocation(tile[0], tile[1], position);
    }
    if (!valid() || (hasLocation && !locationSet))
        return;

    // Out of reach (or off): the streams are cleared once.
    relative[0] = relative[1] = relative[2] = 0.0f;
    distance2 = 0.0f;
    if (hasLocation) {
        // Its velocity from its moves, smoothed; a jump (a new place) is not
        // a move.
        const double place[3] = {location[0] + 2048.0 * locationTile[0], location[1],
                                 location[2] + 2048.0 * locationTile[1]};
        const double step = clock - lastClock;
        if (lastClock >= 0.0 && step > 0.0) {
            float moved[3];
            float speed2 = 0.0f;
            for (int i = 0; i < 3; ++i) {
                moved[i] = float((place[i] - lastPlace[i]) / step);
                speed2 += moved[i] * moved[i];
            }
            const float k = float(std::min(1.0, step / 0.25));
            for (int i = 0; i < 3; ++i)
                velocity[i] += ((speed2 > 150.0f * 150.0f ? 0.0f : moved[i]) - velocity[i]) * k;
        }
        if (step != 0.0)
            std::copy(place, place + 3, lastPlace);
        lastClock = clock;
        relative[0] = location[0] + 2048.0f * float(locationTile[0] - listener.tileX);
        relative[1] = location[1];
        relative[2] = location[2] + 2048.0f * float(locationTile[1] - listener.tileZ);
        const float dx = relative[0] - listener.position[0];
        const float dy = relative[1] - listener.position[1];
        const float dz = relative[2] - listener.position[2];
        distance2 = dx * dx + dy * dy + dz * dz;
    }
    const bool outOfDistance = !enabled || (hasLocation && distance2 > cutoff2);
    if (outOfDistance) {
        if (!wasOutOfDistance) {
            for (StreamState &s : streams) {
                if (s.voice != nullptr) {
                    s.voice->player().stop();
                    s.voice->update(false);
                }
            }
            wasOutOfDistance = true;
            isActive = false;
        }
    } else if (wasOutOfDistance) {
        // Coming within reach: the initial triggers run, and loops that
        // should be playing start (Open Rails' InitInitials).
        for (StreamState &s : streams) {
            hardActivate(s);
            bool released = false;
            for (size_t i = 0; i < s.triggers.size(); ++i) {
                TriggerState &t = s.triggers[i];
                if (t.def == nullptr)
                    continue;
                initTrigger(s, int(i));
                tryTrigger(s, int(i));
                released |= t.signaled && t.def->command.releases();
                if (t.def->type == Trigger::Type::Discrete)
                    t.signaled = false;
            }
            if (!released && s.voice != nullptr && !s.voice->player().isPlaying()) {
                for (size_t i = 0; i < s.triggers.size(); ++i) {
                    const TriggerState &t = s.triggers[i];
                    if (t.def != nullptr && t.signaled && t.enabled
                            && (t.def->command.type == Command::Type::StartLoop
                                || t.def->command.type == Command::Type::StartLoopRelease))
                        run(s, int(i));
                }
            }
        }
        wasOutOfDistance = false;
    }
    if (wasOutOfDistance) {
        // Variable triggers keep their state, to be right when back.
        for (StreamState &s : streams)
            for (size_t i = 0; i < s.triggers.size(); ++i)
                if (s.triggers[i].def != nullptr && s.triggers[i].def->type == Trigger::Type::Variable)
                    tryTrigger(s, int(i));
        return;
    }

    if (!isActive) {
        isActive = activate(listener);
    } else if (deactivate(listener)) {
        for (StreamState &s : streams)
            if (s.voice != nullptr)
                s.voice->player().deactivate();
        isActive = false;
    }
    for (StreamState &s : streams)
        updateStream(s, relative);
}

bool SoundSource::conditionsMet(const Sms::Activation &conditions, const SoundListener &listener) const {
    if (!conditions.present)
        return false;
    // World sounds, and cars the camera does not ride, are heard from
    // outside.
    SoundView view = listener.view;
    if (sourceKind == Kind::World || (view != SoundView::External && listener.attachedTo != owner))
        view = SoundView::External;
    return (conditions.cabCam && view == SoundView::Cab) || (conditions.passengerCam && view == SoundView::Passenger)
            || (conditions.externalCam && view == SoundView::External);
}

bool SoundSource::activate(const SoundListener &listener) const {
    const Sms::Activation &a = group->activation;
    if (!conditionsMet(a, listener))
        return false;
    if (!hasLocation)
        return true;
    return (a.distance == 0.0f || distance2 < a.distance * a.distance) && distance2 < cutoff2;
}

bool SoundSource::deactivate(const SoundListener &listener) const {
    const Sms::Activation &d = group->deactivation;
    if (!d.present)
        return false;
    if (conditionsMet(d, listener))
        return true;
    return hasLocation
            && ((d.distance != 0.0f && distance2 > d.distance * d.distance) || distance2 > cutoff2);
}

void SoundSource::hardActivate(StreamState &s) {
    if (!SoundEngine::get().running())
        return;
    const bool placed = hasLocation && !ignore3d && external;
    if (s.voice == nullptr || !s.voice->valid())
        s.voice = std::make_unique<SoundVoice>(placed);
    else
        s.voice->player().deactivate();
}

const SoundValues *SoundSource::car() const {
    if (sourceKind == Kind::Car)
        return &carValues;
    return currentListener != nullptr ? currentListener->attachedValues : nullptr;
}

float SoundSource::readValue(Control control, int index, const SoundValues *values) const {
    if (control == Control::Distance)
        return distance2;
    return values != nullptr ? values->get(control, index) : 0.0f;
}

void SoundSource::initTrigger(StreamState &s, int index) {
    TriggerState &t = s.triggers[size_t(index)];
    const Trigger &def = *t.def;
    switch (def.type) {
    case Trigger::Type::Initial:
        if (t.enabled) {
            s.repeated = s.lastTriggered == index;
            run(s, index);
            s.lastTriggered = index;
        }
        t.signaled = true;
        break;
    case Trigger::Type::Variable:
        t.startValue = def.control == Control::Distance && !def.increasing ? FLT_MAX : 0.0f;
        t.isBelow = t.startValue < def.threshold;
        break;
    case Trigger::Type::Random:
        t.next = currentClock + SoundSystem::uniform(def.min, def.max);
        break;
    case Trigger::Type::DistanceTravelled:
        t.next = carValues.distanceTravelled
                + (def.max != def.min ? SoundSystem::uniform(def.min, def.max)
                                      : SoundSystem::uniform(0.0f, 1.0f) * def.min + def.min);
        break;
    default:
        break;
    }
}

void SoundSource::tryTrigger(StreamState &s, int index) {
    TriggerState &t = s.triggers[size_t(index)];
    const Trigger &def = *t.def;
    switch (def.type) {
    case Trigger::Type::Discrete:
        t.triggered &= t.enabled;
        if (t.triggered) {
            t.triggered = false;
            s.repeated = s.lastTriggered == index;
            run(s, index);
            s.lastTriggered = index;
            t.signaled = true;
        }
        // Not heard: what it queued keeps only its newest loop.
        if (!isActive && s.voice != nullptr)
            s.voice->player().deactivate();
        break;
    case Trigger::Type::Variable: {
        const float value = readValue(def.control, def.index, car());
        bool fire = false;
        t.signaled = false;
        if (!def.increasing && value < def.threshold) {
            t.signaled = true;
            fire = def.threshold <= t.startValue;
        } else if (def.increasing && value > def.threshold) {
            t.signaled = true;
            fire = def.threshold >= t.startValue;
        }
        t.startValue = value;
        t.isBelow = value < def.threshold;
        if (fire && t.enabled) {
            s.repeated = s.lastTriggered == index;
            run(s, index);
            s.lastTriggered = index;
        }
        break;
    }
    case Trigger::Type::Random:
    case Trigger::Type::DistanceTravelled: {
        const bool random = def.type == Trigger::Type::Random;
        const double now = random ? currentClock : carValues.distanceTravelled;
        if (now > t.next) {
            t.signaled = true;
            if (t.enabled) {
                s.repeated = s.lastTriggered == index;
                run(s, index);
                s.volume = SoundSystem::uniform(def.volumeMin, def.volumeMax);
                s.lastTriggered = index;
            }
            initTrigger(s, index);
        } else {
            t.signaled = false;
        }
        break;
    }
    default:
        break;
    }
}

QString SoundSource::nextFile(StreamState &, TriggerState &t) {
    const QStringList &files = t.def->command.files;
    if (files.isEmpty())
        return QString();
    if (t.def->command.randomSelection)
        t.file = std::uniform_int_distribution<int>(0, int(files.size()) - 1)(SoundSystem::random());
    else
        t.file = (t.file + 1) % int(files.size());
    return files[t.file];
}

const SoundClip *SoundSource::clip(const QString &name) {
    auto found = wavePaths.constFind(name);
    QString path;
    if (found != wavePaths.constEnd()) {
        path = found.value();
    } else {
        path = SoundBank::find(name, folders);
        wavePaths.insert(name, path);
    }
    if (path.isEmpty())
        return nullptr;
    // Sounds heard from outside are placed in 3D: mono.
    return SoundEngine::get().bank().get(path, external);
}

void SoundSource::run(StreamState &s, int index) {
    TriggerState &t = s.triggers[size_t(index)];
    const Command &command = t.def->command;
    const int number = int(&s - streams.data());
    StreamPlayer *player = s.voice != nullptr && s.voice->valid() ? &s.voice->player() : nullptr;
    QString logged;
    switch (command.type) {
    case Command::Type::PlayOneShot:
    case Command::Type::StartLoop:
    case Command::Type::StartLoopRelease: {
        const QString name = nextFile(s, t);
        logged = name;
        const StreamPlayer::Mode mode = command.type == Command::Type::PlayOneShot ? StreamPlayer::Mode::OneShot
                : command.type == Command::Type::StartLoop ? StreamPlayer::Mode::Loop
                                                           : StreamPlayer::Mode::LoopRelease;
        if (player != nullptr)
            player->queue(clip(name), mode, s.repeated);
        break;
    }
    case Command::Type::ReleaseLoopRelease:
        if (player != nullptr)
            player->queue(nullptr, StreamPlayer::Mode::Release);
        break;
    case Command::Type::ReleaseLoopReleaseWithJump:
        if (player != nullptr)
            player->queue(nullptr, StreamPlayer::Mode::ReleaseWithJump);
        break;
    case Command::Type::EnableTrigger:
    case Command::Type::DisableTrigger:
        if (command.trigger >= 1 && command.trigger <= int(s.triggers.size()))
            s.triggers[size_t(command.trigger - 1)].enabled = command.type == Command::Type::EnableTrigger;
        logged = QString::number(command.trigger);
        break;
    case Command::Type::SetStreamVolume:
        s.volume = command.volume;
        logged = QString::number(command.volume);
        break;
    default:
        break;
    }
    if (log != nullptr) {
        static const char *names[] = {"None", "PlayOneShot", "StartLoop", "StartLoopRelease", "ReleaseLoopRelease",
                                      "ReleaseLoopReleaseWithJump", "EnableTrigger", "DisableTrigger",
                                      "SetStreamVolume"};
        QString entry = QString("%1: %2").arg(number).arg(names[int(command.type)]);
        if (!logged.isEmpty())
            entry += " " + logged;
        log->append(entry);
    }
}

void SoundSource::updateStream(StreamState &s, const float *at) {
    if (s.voice == nullptr || !s.voice->valid())
        return;
    StreamPlayer &player = s.voice->player();
    s.voice->setPosition(at[0], at[1], at[2]);
    for (size_t i = 0; i < s.triggers.size(); ++i)
        if (s.triggers[i].def != nullptr)
            tryTrigger(s, int(i));

    // The initial trigger again when nothing plays and every variable
    // trigger is below its threshold; signaled no more once several loops
    // were started.
    if (s.initial >= 0) {
        TriggerState &initial = s.triggers[size_t(s.initial)];
        int variables = 0;
        int below = 0;
        int applicable = 0;
        for (const TriggerState &t : s.triggers) {
            if (t.def == nullptr)
                continue;
            ++applicable;
            if (t.def->type == Trigger::Type::Variable) {
                ++variables;
                below += t.isBelow ? 1 : 0;
            }
        }
        if (!player.isPlaying()) {
            const Command &command = initial.def->command;
            if ((variables > 0 || applicable == 1) && below == variables && command.plays()
                    && !(command.type == Command::Type::PlayOneShot && initial.signaled))
                initTrigger(s, s.initial);
        } else {
            int loops = 0;
            for (const TriggerState &t : s.triggers)
                if (t.def != nullptr && t.signaled
                        && (t.def->command.type == Command::Type::StartLoop
                            || t.def->command.type == Command::Type::StartLoopRelease))
                    ++loops;
            if (loops > 1 && initial.signaled)
                initial.signaled = false;
        }
    }
    setFrequencyAndVolume(s);
    s.voice->update(isActive);
}

void SoundSource::setFrequencyAndVolume(StreamState &s) {
    StreamPlayer &player = s.voice->player();
    const SoundValues *values = car();
    if (s.def->frequencyCurve && values != nullptr) {
        const Sms::Curve &curve = *s.def->frequencyCurve;
        float hz = curve.valueAt(readValue(curve.control, curve.index, values));
        const SoundClip *current = player.currentClip();
        // MSTS plays mono waves of groups marked stereo at twice the rate.
        if (current != nullptr && group->stereo && current->originallyMono)
            hz *= 2.0f;
        if (current != nullptr && current->sampleRate > 0 && hz > 0.0f)
            s.baseSpeed = hz / float(current->sampleRate);
    }
    // A sound placed in 3D pitches with its and the listener's moves.
    float doppler = 1.0f;
    if (s.voice->placed() && currentListener != nullptr) {
        const float toSource[3] = {relative[0] - currentListener->position[0], relative[1] - currentListener->position[1],
                                   relative[2] - currentListener->position[2]};
        doppler = dopplerFactor(toSource, velocity, currentListener->velocity);
    }
    s.speed = s.baseSpeed * doppler;
    player.setSpeed(s.speed);
    float volume = group->volume * s.volume;
    for (const Sms::Curve &curve : s.def->volumeCurves) {
        const float x = values != nullptr ? readValue(curve.control, curve.index, values) : distance2;
        volume *= curve.valueAt(x);
    }
    // Outside sounds through the cab's walls.
    if (external && currentListener != nullptr && currentListener->view != SoundView::External)
        volume *= OutsideHeardInside;
    volume = std::max(volume, 0.0f);
    if (volume == 0.0f)
        player.dropIfReleasing();
    float gain = isActive ? volume : 0.0f;
    if (s.voice->placed())
        gain *= distanceGain(std::sqrt(distance2), rolloff);
    s.gain = std::min(gain, 1.0f);
    s.voice->setGain(s.gain);
}
