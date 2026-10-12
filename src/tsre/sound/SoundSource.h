/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SOUNDSOURCE_H
#define SOUNDSOURCE_H

#include <tsre/sound/SmsFile.h>

#include <QHash>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <vector>

class SoundVoice;
struct SoundClip;

// The values a train car's sounds read (Open Rails' SoundStream.ReadValue):
// speed in m/s, brake cylinder in psi, forces in N and so on, by control.
struct SoundValues {
    float value[int(Sms::Control::Count)] = {};
    // Variable1 by index (value[Variable1] is index 0), and the engines'.
    std::vector<float> variable1;
    std::vector<float> engineRpm;
    std::vector<float> enginePower;
    std::vector<float> engineTorque;
    // Metres the car has run, for distance-travelled triggers.
    double distanceTravelled = 0.0;

    float get(Sms::Control control, int index) const;
    void set(Sms::Control control, float v) { value[int(control)] = v; }
};

enum class SoundView { External, Cab, Passenger };

// Where and how the sounds are heard: the camera, in its tile.
struct SoundListener {
    int tileX = 0;
    int tileZ = 0;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float forward[3] = {0.0f, 0.0f, -1.0f};
    float up[3] = {0.0f, 1.0f, 0.0f};
    SoundView view = SoundView::External;
    // The car the camera rides (cab sounds are its own), and its values,
    // which sounds without a car read (Open Rails' attached car).
    const void *attachedTo = nullptr;
    const SoundValues *attachedValues = nullptr;
};

// One sound definition playing for one thing, as Open Rails' SoundSource,
// SoundStream and triggers do it: the .sms group of the sound detail level,
// its streams each with a voice, their triggers queueing waves and their
// curves setting volume and frequency; heard when its activation
// conditions are met (camera, distance) and within 2 km.
class SoundSource {
public:
    // A train car's, a place's in the world (a route's sound source), or
    // one heard everywhere without a place.
    enum class Kind { Car, World, Everywhere };

    // owner: the car for cab sounds (compared with the listener's
    // attachedTo). World sounds fade slower (Open Rails' slow rolloff).
    SoundSource(const QString &smsPath, Kind kind, const void *owner = nullptr);
    ~SoundSource();
    SoundSource(const SoundSource &) = delete;
    SoundSource &operator=(const SoundSource &) = delete;

    // The file has a group to play at the detail level.
    bool valid() const { return group != nullptr; }
    const QString &path() const { return smsPath; }
    Kind kind() const { return sourceKind; }

    // Where it is (tile and position in it, TSRE's coordinates).
    void setLocation(int tileX, int tileZ, const float *position);
    // Off: silent and its streams cleared (a deleted object).
    void setEnabled(bool enabled);
    // Asked before each update where the source is now; false: off.
    std::function<bool(int &tileX, int &tileZ, float *position)> locator;
    SoundValues &values() { return carValues; }
    // A discrete event (a car's control, a horn...).
    void event(int id);

    void update(const SoundListener &listener, double clock);
    // Lets go of the voices (the engine stops); they come back with the
    // next update when it runs again.
    void release();

    // Gain with distance: OpenAL's inverse distance clamped model with
    // Open Rails' values: full within 8 m, from 2 km on at most
    // GainAtMaxDistance at the normal rolloff; world sounds fade slower.
    static constexpr float ReferenceDistance = 8.0f;
    static constexpr float MaxDistance = 2000.0f;
    static constexpr float GainAtMaxDistance = 0.025f;
    static constexpr float SlowRolloff = 0.4f;
    // Beyond this (squared) nothing is heard and the streams are cleared.
    static constexpr float CutoffDistanceSquared = 2000.0f * 2000.0f;
    static float rolloffFor(float maxDistance);
    static float distanceGain(float distance, float rolloff);

    // For tests: the commands run, as "stream: Command file".
    QStringList *log = nullptr;
    bool active() const { return isActive; }
    bool inRange() const { return !wasOutOfDistance; }
    float distanceSquared() const { return distance2; }
    int streamCount() const { return int(streams.size()); }
    float streamGain(int stream) const;
    float streamSpeed(int stream) const;
    bool streamPlaying(int stream) const;
    const SoundVoice *voice(int stream) const;

private:
    struct TriggerState {
        const Sms::Trigger *def = nullptr;
        bool enabled = true;
        bool signaled = false;
        bool triggered = false;
        // Variable triggers.
        float startValue = 0.0f;
        bool isBelow = false;
        // Random (clock) and distance-travelled (metres) triggers.
        double next = 0.0;
        int file = -1;
    };
    struct StreamState {
        const Sms::Stream *def = nullptr;
        std::unique_ptr<SoundVoice> voice;
        std::vector<TriggerState> triggers;
        int initial = -1;
        float volume = 1.0f;
        int lastTriggered = -1;
        bool repeated = false;
        bool releasedWithJump = false;
        float gain = 0.0f;
        float speed = 1.0f;
    };

    bool conditionsMet(const Sms::Activation &conditions, const SoundListener &listener) const;
    bool activate(const SoundListener &listener) const;
    bool deactivate(const SoundListener &listener) const;
    void hardActivate(StreamState &stream);
    // The car whose values curves and triggers read: its own, or the one
    // the camera rides for sounds without a car.
    const SoundValues *car() const;
    const SoundClip *clip(const QString &name);
    void initTrigger(StreamState &stream, int index);
    void tryTrigger(StreamState &stream, int index);
    void run(StreamState &stream, int index);
    void updateStream(StreamState &stream, const float *relative);
    void setFrequencyAndVolume(StreamState &stream);
    float readValue(Sms::Control control, int index, const SoundValues *car) const;
    float distanceGain() const;
    QString nextFile(StreamState &stream, TriggerState &trigger);

    QString smsPath;
    QStringList folders;
    QHash<QString, QString> wavePaths;
    Kind sourceKind;
    const void *owner;
    std::shared_ptr<const Sms::File> file;
    const Sms::Group *group = nullptr;
    std::vector<StreamState> streams;
    SoundValues carValues;
    bool external = true;
    bool ignore3d = false;
    bool slowRolloff = false;
    float rolloff = 1.0f;
    bool hasLocation = false;
    bool locationSet = false;
    int locationTile[2] = {0, 0};
    float location[3] = {0.0f, 0.0f, 0.0f};
    bool enabled = true;
    bool isActive = false;
    bool wasOutOfDistance = true;
    float distance2 = 0.0f;
    // Per update.
    const SoundListener *currentListener = nullptr;
    double currentClock = 0.0;
};

#endif
