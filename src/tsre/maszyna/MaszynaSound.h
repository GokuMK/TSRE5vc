/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MASZYNASOUND_H
#define MASZYNASOUND_H

#include <QString>
#include <QStringList>
#include <QStringView>
#include <array>
#include <optional>
#include <random>
#include <utility>
#include <vector>

struct MaszynaFiz;

// A MaSzyna vehicle's sounds from its .mmd (task maszyna 05), read as
// MaSzyna reads them at 66a940d4 (sound_source::deserialize,
// TDynamicObject::LoadMMediaFile, TTrain::LoadMMediaFile; the review in
// docs/tasks/maszyna/reports/mmd-sounds.md), quirks included: a one-line
// entry takes its key's fixed count of numbers whatever follows, pitch<N>
// counts only after its sound<N>, the last of a doubled key wins, unknown
// keys are passed over. Factors are as MaSzyna holds them after loading:
// rescaled by the vehicle's physics where it rescales them.
namespace MaszynaSound {

// Where a sound sits, for soundproofing (MaSzyna's sound_placement; the
// vehicle's table has a row for each from Internal to Custom).
enum class Placement { General, Internal, Engine, External, ExternalAmbient, Custom };

// A sample: its name as read (lower case, / for \) and its file, empty when
// found nowhere.
struct Sample {
    QString name;
    QString path;
    bool empty() const { return name.isEmpty(); }
};

// A table's entry (sound<N>:): played from fadeIn to fadeOut around its
// threshold, recorded at `pitch` (pitch<N>:).
struct Chunk {
    int threshold = 0;
    Sample sample;
    float pitch = 1.0f;
    float fadeIn = 0.0f;
    float fadeOut = 0.0f;
};

struct Definition {
    // The key without its colon ("tractionmotor", "horn1", "soundinc").
    QString key;
    Sample begin, main, end;
    // Sorted by threshold.
    std::vector<Chunk> chunks;
    // Percent of an entry's span from the previous one that crossfades.
    int crossfade = 0;
    Placement placement = Placement::General;
    // Metres; -1 at the listener.
    float range = 50.0f;
    // In the car's frame: +x left, +y up, +z front; all 0 when not given.
    float offset[3] = {0.0f, 0.0f, 0.0f};
    // Its own factors by the listener's place (square roots of the written
    // ones, -1 kept); none: the vehicle's table.
    std::optional<std::array<float, 6>> soundproofing;
    float amplitudeFactor = 1.0f, amplitudeOffset = 0.0f;
    float frequencyFactor = 1.0f, frequencyOffset = 0.0f;
    // The pitch multiplier this car plays it at (pitchvariation:, else
    // MaSzyna's 0.975 to 1.025).
    float pitchVariation = 1.0f;
    float startOffset = 0.0f;
    // Written as a { } block.
    bool block = false;

    // A table without a main sample (MaSzyna's combined sound).
    bool combined() const { return !chunks.empty() && main.empty(); }
    bool hasOffset() const { return offset[0] != 0.0f || offset[1] != 0.0f || offset[2] != 0.0f; }
    // Any sample, found or not.
    bool hasSample() const;
};

// What MaSzyna rescales some factors by when it loads them, from the
// .fiz, in MaSzyna's units (its formulas take them so).
struct Physics {
    // Vmax, km/h; Power (PWR), kW.
    double vmax = 0.0;
    double power = 0.0;
    // Engine's nmax and RList's RVentnmax, rev/s (rpm / 60); 1 for the
    // ventilator of an induction motor or one not given.
    double nmax = 0.0;
    double rventnmax = 1.0;
    // Engine's MotorBlowersSpeed: a share of the motor's speed above 0,
    // rpm (negated) below.
    double blowerSpeed = 0.0;
    // DieselEngine or DieselElectric: the engine's factor is not rescaled.
    bool diesel = false;

    // From a vehicle's .fiz as MaSzyna loads them: nmax for series motors
    // and diesel engines only, RVentnmax when RList's RVent is on.
    static Physics of(const MaszynaFiz &fiz);
};

// locations: where the motors, bogies and doors are along the car, +z
// front (the written values negated, as MaSzyna does).
struct Locations {
    std::vector<float> motors;
    std::vector<float> bogies;
    struct Door {
        float z = 0.0f;
        bool left = false;
        bool right = false;
    };
    std::vector<Door> doors;
};

struct Sounds {
    // sounds: and the vehicle's internaldata: keys (DynObj's), in the order
    // first given, the last of a doubled key.
    std::vector<Definition> vehicle;
    // The cab's internaldata: keys (Train's).
    std::vector<Definition> cab;
    // wheel_clatter: its range and its axles (+z front); read, not played.
    float clatterRange = 0.0f;
    std::vector<std::pair<float, Definition>> clatter;
    Locations locations;
    // The vehicle's soundproofing: rows Internal, Engine, External,
    // ExternalAmbient, Custom; columns the listener in this car's rear cab,
    // its engine room, its front cab, at an open window, outside following
    // it, anywhere else (MaSzyna's defaults, -1 in soundproofing: keeps one).
    std::array<std::array<float, 6>, 5> soundproofing;
    // huntingspring: its four numbers (the cab's hunting noise fades in
    // between the last two, km/h).
    std::array<float, 4> huntingSpring = {0.0f, 0.0f, 0.0f, 0.0f};
    // Names found nowhere and keys MaSzyna does not know, once each.
    QStringList problems;
    // Every file read; the folder its samples were found in (MaSzyna's
    // sounds/), where its cab controls' are found too.
    QStringList files;
    QString soundFolder;

    const Definition *find(QStringView key) const;
    const Definition *findCab(QStringView key) const;
};

// MaSzyna's defaults for the vehicle's soundproofing table.
std::array<std::array<float, 6>, 5> defaultSoundproofing();

// The sounds of the .mmd `type` in the vehicle's `folder`, for one car.
// soundFolder: MaSzyna's sounds/ (a game root's SOUND/MASZYNA); random: the
// car's, which picks a set's word and the pitch variations (none: a set's
// first word, no variation).
Sounds read(const QString &folder, const QString &type, const QString &soundFolder, const Physics &physics,
            std::mt19937 *random = nullptr);

// A cab control's sound value as MaszynaCab keeps it (a name, [ a set ] or
// { a block }), for the key it came with (soundinc, sound12 ...): a cab
// sound, found in soundFolder only.
Definition readValue(const QString &key, const QString &text, const QString &soundFolder,
                     std::mt19937 *random = nullptr);

// A sample's file by MaSzyna's rule: its extension dropped, then .ogg,
// .flac and .wav in the vehicle's folder (vehicle sounds; empty for the
// cab's), as a path below MaSzyna's folder when the name has a /, and in
// soundFolder. Empty when none is there.
QString find(const QString &name, const QString &vehicleFolder, const QString &soundFolder);

}

#endif // MASZYNASOUND_H
