/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MASZYNASOUNDS_H
#define MASZYNASOUNDS_H

#include <tsre/maszyna/MaszynaSound.h>
#include <tsre/sound/CarSounds.h>
#include <tsre/sound/SoundEmitter.h>

#include <QHash>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <array>
#include <deque>
#include <memory>
#include <random>
#include <vector>

class SoundVoice;
struct SoundClip;
struct SoundListener;
struct MaszynaVehicle;

// One MaSzyna sound at one place of its car, played as MaSzyna's
// sound_source plays it (audio/sound.cpp at 66a940d4) through TSRE's voices.
// Its driver calls play() each frame while the sound's condition holds
// (Exclusive or Looping; a begin part first, then the main one looping) or
// once for a one-shot (which may overlap another), sets gain() and pitch()
// (for a table pitch is the controlling value × 0.01, as MaSzyna's drivers
// pass it), and stop() when it ends (the main cut, the end part played).
class MaszynaEmitter {
public:
    enum Flags { OneShot = 0, Exclusive = 1, Looping = 2 };

    // place: in the car's frame (+x left, +y up, +z front).
    MaszynaEmitter(const MaszynaSound::Definition &definition, const float *place);
    ~MaszynaEmitter();
    MaszynaEmitter(const MaszynaEmitter &) = delete;
    MaszynaEmitter &operator=(const MaszynaEmitter &) = delete;

    void play(int flags = OneShot);
    void stop(bool skipEnd = false);
    // 0 to 2.
    void gain(float gain);
    float gain() const { return baseGain; }
    void pitch(float pitch);
    float pitch() const { return basePitch; }
    // A begin or main part, or a table's entry, plays (an end part not).
    bool playing() const;
    const MaszynaSound::Definition &definition() const { return def; }
    const float *place() const { return at; }
    void setPlace(const float *place) { std::copy(place, place + 3, at); }

    // Where the listener hears it this frame, set before the driver's
    // calls (play() drops a sound starting too far away).
    struct Hearing {
        // In the listener tile's space.
        float position[3] = {0.0f, 0.0f, 0.0f};
        float distance = 0.0f;
        float soundproofing = 1.0f;
        float doppler = 1.0f;
    };
    void hear(const Hearing &hearing) { heard = hearing; }
    // The voices' gain, speed and place; ended ones go.
    void update();
    void release();
    int voiceCount() const { return int(instances.size()); }
    int voicesPlaying() const;
    // Voices started since asked last (TSRE_SOUND_LOG).
    int takeStarts() {
        const int n = starts;
        starts = 0;
        return n;
    }

    // The gain a voice got last (tests): its instance's of the given
    // part, -1 without one.
    float voiceGain(int index) const;
    float voiceSpeed(int index) const;

    // MaSzyna's distance model (audiorenderer.cpp): full within range / 16
    // (times the soundproofing), inverse distance with rolloff 1.75 beyond,
    // faded out from range to 1.25 range; range -1 or below heard at the
    // listener (below -1 still faded by its distance).
    static float distanceGain(float distance, float range, float soundproofing);
    // MaSzyna's crossfade gain of a table's entry at a value (×100).
    static float crossfadeGain(const MaszynaSound::Definition &d, int index, float point);
    // An entry's relative pitch at a value (×100): toward its neighbour's
    // recorded pitch.
    static float chunkPitch(const MaszynaSound::Definition &d, int index, float point);
    // A table's entry plays at a value (×100): from its fade in to its fade
    // out, the last one to its threshold too.
    static bool covers(const MaszynaSound::Definition &d, int index, float point);

private:
    enum class Part { Main, End, Chunk };
    struct Instance {
        std::unique_ptr<SoundVoice> voice;
        Part part = Part::Main;
        int chunk = -1;
        // The begin part queued before it.
        const SoundClip *begin = nullptr;
        float gain = 0.0f;
        float speed = 1.0f;
    };

    float point() const;
    void playBasic();
    void playCombined();
    bool spawn(Part part, int chunk, bool withBegin);
    bool beginPlaying() const;
    bool chunkPlaying(int chunk) const;
    int endsPlaying() const;
    void kill(bool keepEnds);

    const MaszynaSound::Definition &def;
    float at[3];
    int flags = 0;
    bool playBeginning = true;
    float baseGain = 1.0f;
    float basePitch = 1.0f;
    Hearing heard;
    std::vector<Instance> instances;
    int starts = 0;
};

// A MaSzyna car's sounds (task maszyna 05): its .mmd's read for the car (its
// random picks), placed as MaSzyna places them (motors, bogies, doors,
// couplers, pantographs, the powertrain at half height), heard through
// MaSzyna's soundproofing and distance model, driven by the host's values
// for what every car does and by its loco script's for the rest.
class MaszynaSounds : public CarSounds, public SoundEmitter {
public:
    MaszynaSounds(std::shared_ptr<const MaszynaVehicle> vehicle, std::shared_ptr<const MaszynaSound::Sounds> sounds);
    ~MaszynaSounds() override;

    void set(const Frame &frame) override;
    void event(int trigger) override;
    void event(const QString &name) override;
    bool trackSounds() const override;
    QStringList problems() const override;
    // A cab control's value changed: its click as TGauge::UpdateValue and
    // TButton plays it (a value's sound<N>, else soundinc / sounddec, else
    // the cab's ctrl:, ctrlscnd:, reverserkey:, switch: or
    // pneumaticswitch:); a control's first value taken silently.
    void control(const CabControls *controls, int index, double value, const float *place) override;

    void update(const SoundListener &listener, double clock) override;
    void release() override;
    bool heard() const override { return isHeard; }
    int voiceCount() const override;
    int voicesPlaying() const override;

    const MaszynaSound::Sounds &sounds() const { return *soundsRead; }
    // A key's emitters: one per place (motors, bogies, doors, couplers,
    // pantographs), none without the key.
    const std::vector<MaszynaEmitter *> &emitters(QStringView key) const;
    // A cab sound's (on the car playing its cab's sounds).
    MaszynaEmitter *cab(QStringView key) const;

    // The listener's place for MaSzyna's soundproofing: 0 this car's rear
    // cab, 1 its engine room (a passenger view), 2 its front cab, 3 an open
    // window, 4 outside following it, 5 elsewhere.
    static int column(const SoundListener &listener, const void *car);
    // The factor for a sound of a car (its table, the sound's own
    // soundproofing or its placement's row), heard from `column`, and, the
    // listener in another car, through that car's outside row.
    static float soundproofing(const MaszynaSound::Definition &d, const std::array<std::array<float, 6>, 5> &table,
                               int column, const std::array<std::array<float, 6>, 5> *listenerTable,
                               int listenerColumn);

    // TSRE_MASZYNA_RUNNING_NOISE (both, route or vehicle; read once): which
    // running noise MaSzyna cars play, the route's track sounds, their own
    // (outernoise:, the cab's runningnoise:), or both (the default).
    enum class RunningNoise { Both, Route, Vehicle };
    static RunningNoise runningNoise();

private:
    void place();
    void placeCab();
    void drive(const SoundListener &listener, float seconds);
    // What every car does, from the host's values (DynObj.cpp's
    // RenderSounds, TTrain's noises): running noise, curve, brakes, the
    // release hiss, the start jolt, doors, the ridden cab's noises.
    void driveHost(const SoundListener &listener, float seconds);
    // What the loco script holds on (its sound.<key> values) and plays
    // once (its events: key, key:pitch[:gain] with MaSzyna's pitch, a
    // table's value / 100, or key:side), and the host's events (doors by
    // side).
    void driveScript();
    // A key's emitters, the cab's first; `place`: left or right (doors),
    // front or rear (pantographs, couplers, motor blowers), empty for all.
    std::vector<MaszynaEmitter *> select(const QString &key, const QString &place) const;

    std::shared_ptr<const MaszynaVehicle> vehicle;
    std::shared_ptr<const MaszynaSound::Sounds> soundsRead;
    MaszynaSound::Physics physics;
    // The brake cylinder's most (.fiz MaxBP, MPa) and the car's mass (kg).
    float maxBrakePress = 0.38f;
    float massKg = 50000.0f;
    // The host's sounds' state: the release hiss's rate (bar/s), the last
    // cylinder pressure (bar), the last speed, the start jolt played.
    float unbrakeRate = 0.0f;
    float lastBrakePress = -1.0f;
    float lastSpeed = 0.0f;
    bool startJoltPlayed = false;
    // Door sounds' sides: true left.
    QHash<const MaszynaEmitter *, bool> doorLeft;
    // The script's keys held on last frame.
    QStringList held;
    // TSRE_SOUND_LOG: the keys heard, every 5 seconds.
    double nextLog = 0.0;
    // The cab's controls' sounds: each control's last value, whether its
    // sounds play one at a time (once a value between whole ones came, as
    // TGauge's continuous controls), its emitters by key; their
    // definitions kept here (emitters refer to them); the car's random.
    struct ControlSounds {
        bool seen = false;
        bool exclusive = false;
        double target = 0.0;
        QHash<QString, MaszynaEmitter *> emitters;
    };
    QHash<qint64, ControlSounds> controls;
    std::deque<MaszynaSound::Definition> controlDefinitions;
    std::vector<std::unique_ptr<MaszynaEmitter>> controlOwned;
    const CabControls *controlsOf = nullptr;
    std::mt19937 random;
    std::vector<std::unique_ptr<MaszynaEmitter>> owned;
    QHash<QString, std::vector<MaszynaEmitter *>> byKey;
    std::vector<std::unique_ptr<MaszynaEmitter>> cabOwned;
    QHash<QString, MaszynaEmitter *> cabByKey;
    Frame frame;
    bool framed = false;
    bool isHeard = false;
    QStringList events;
    std::vector<int> triggers;
    double lastClock = -1.0;
    // The car's velocity from its moves (m/s, sound space).
    float velocity[3] = {0.0f, 0.0f, 0.0f};
    double lastPlace[3] = {0.0, 0.0, 0.0};
    bool placeKnown = false;
};

#endif // MASZYNASOUNDS_H
