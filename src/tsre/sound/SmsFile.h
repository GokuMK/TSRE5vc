/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SMSFILE_H
#define SMSFILE_H

#include <QString>
#include <QStringList>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

// An MSTS sound definition (.sms), as Open Rails reads it
// (Orts.Formats.Msts/SoundManagmentFile.cs): scalability groups of streams,
// each stream a set of triggers that queue waves, and curves that set its
// volume and frequency from a value of the train or the distance.
namespace Sms {

// What a curve or a variable trigger reads. Distance is squared, as Open
// Rails compares it (curve points and thresholds are squared when read).
enum class Control {
    None,
    Distance,
    Speed,
    Variable1,
    Variable2,
    Variable2Booster,
    Variable3,
    BrakeCyl,
    CurveForce,
    AngleOfAttack,
    CarFriction,
    WheelRpm,
    ConcreteSleepers,
    CarInTunnel,
    CarDistanceTrack,
    CarTunnelDistance,
    BackPressure,
    TractiveEffort,
    TractivePower,
    EngineRpm,
    EnginePower,
    EngineTorque,
    Count
};

// A control by its name in a curve or trigger ("Speed", "Variable1_2",
// "Engine2RPM"); index: which engine or Variable1 (0 based).
Control controlFromName(const QString &name, int *index = nullptr);

struct Curve {
    Control control = Control::None;
    int index = 0;
    float granularity = 1.0f;
    std::vector<std::pair<float, float>> points;
    // Open Rails' interpolation: the end values outside the points.
    float valueAt(float x) const;
};

struct Command {
    enum class Type {
        None,
        PlayOneShot,
        StartLoop,
        StartLoopRelease,
        ReleaseLoopRelease,
        ReleaseLoopReleaseWithJump,
        EnableTrigger,
        DisableTrigger,
        SetStreamVolume
    };
    Type type = Type::None;
    QStringList files;
    bool randomSelection = false;
    // Enable and disable: the trigger's number in the stream (1 based).
    int trigger = 0;
    float volume = 1.0f;

    bool plays() const {
        return type == Type::PlayOneShot || type == Type::StartLoop || type == Type::StartLoopRelease;
    }
    bool releases() const {
        return type == Type::ReleaseLoopRelease || type == Type::ReleaseLoopReleaseWithJump;
    }
};

struct Trigger {
    enum class Type { Unknown, Initial, Discrete, Variable, Random, DistanceTravelled, Joint, Switch, Crossover };
    Type type = Type::Unknown;
    // Discrete: the event's number.
    int event = 0;
    // Variable: the control, rising (Inc_Past) or falling past the threshold.
    Control control = Control::None;
    int index = 0;
    bool increasing = true;
    float threshold = 0.0f;
    // Random: seconds between; distance travelled: metres between.
    float min = 80.0f;
    float max = 100.0f;
    float volumeMin = 0.9f;
    float volumeMax = 1.0f;
    // Joint, switch and crossover triggers: axles and camera distance.
    int axles = 0;
    float cameraDistance = 80.0f;
    Command command;
};

struct Stream {
    int priority = 0;
    float volume = 1.0f;
    std::vector<Trigger> triggers;
    std::vector<Curve> volumeCurves;
    std::optional<Curve> frequencyCurve;
    // Open Rails extensions, read but not applied yet.
    std::vector<std::pair<int, int>> hours;
    int seasons = -1;
    int weathers = -1;
};

struct Activation {
    bool present = false;
    bool externalCam = false;
    bool cabCam = false;
    bool passengerCam = false;
    // 0: any distance.
    float distance = 1000.0f;
    int trackType = -1;
};

struct Group {
    int detailLevel = 0;
    float volume = 1.0f;
    bool stereo = false;
    bool ignore3d = false;
    Activation activation;
    Activation deactivation;
    bool hasStreams = false;
    std::vector<Stream> streams;
};

struct File {
    QString path;
    std::vector<Group> groups;
    // Tokens and blocks the reader skipped, with the reason.
    QStringList warnings;

    // The group Open Rails plays at a sound detail level: the first one at
    // that level or below; none if it has no streams.
    const Group *group(int detailLevel) const;
};

// The reading of .sms text (the SIMISA header line is skipped): false
// without a Tr_SMS block; an empty one has no groups.
bool parse(const QString &text, File &out);
// Decodes the file (UTF-16 with or without BOM, or 8-bit) and parses it.
bool load(const QString &path, File &out, QString *error = nullptr);
// load() once per file for the run.
std::shared_ptr<const File> shared(const QString &path);
void clearShared();

}

#endif
