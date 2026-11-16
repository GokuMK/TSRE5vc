/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef PROCEDURALTRACKPOLICY_H
#define PROCEDURALTRACKPOLICY_H

#include <QString>
#include <QStringList>

enum class ProceduralTracksMode {
    Disabled,
    Enabled,
    // Enabled, and superelevated track procedural as Open Rails makes it
    // (task tracks 16); tracks and cars show superelevation.
    Superelevation,
    Forced
};

enum class ProceduralTrackBackend {
    Fallback,
    Procedural
};

struct ProceduralTrackResolution {
    ProceduralTrackBackend backend = ProceduralTrackBackend::Fallback;
    QString requestedName;
    QString templateName;
    bool missingRequested = false;
    bool missingDefault = false;
};

// Task tracks 16: superelevated static track whose textures fit no track
// profile. A switch in the code (the user, 2026-11-14): DefaultProfile for
// now; KeepStatic is Open Rails' (the static shape, no superelevation
// drawn there). Later: a profile written from the shape.
enum class NoProfileFallback {
    DefaultProfile,
    KeepStatic
};

class ProceduralTrackPolicy {
public:
    static constexpr NoProfileFallback noProfileFallback = NoProfileFallback::DefaultProfile;
    static ProceduralTracksMode modeFromSetting(const QString &value);
    static ProceduralTrackResolution resolve(
            ProceduralTracksMode mode,
            const QString &requestedName,
            const QStringList &availableTemplateNames);
    static void warnOnce(const ProceduralTrackResolution &resolution);
    static void warnGenerationFailureOnce(const QString &templateName);
};

#endif
