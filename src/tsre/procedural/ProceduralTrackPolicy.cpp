/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/procedural/ProceduralTrackPolicy.h>

#include <QDebug>
#include <QSet>

namespace {

QString canonicalName(const QString &name, const QStringList &availableNames) {
    for (const QString &availableName : availableNames) {
        if (availableName.compare(name, Qt::CaseInsensitive) == 0)
            return availableName;
    }
    return QString();
}

}

ProceduralTracksMode ProceduralTrackPolicy::modeFromSetting(const QString &value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized == "true" || normalized == "forced")
        return ProceduralTracksMode::Forced;
    if (normalized == "enabled")
        return ProceduralTracksMode::Enabled;
    return ProceduralTracksMode::Disabled;
}

ProceduralTrackResolution ProceduralTrackPolicy::resolve(
        ProceduralTracksMode mode,
        const QString &requestedName,
        const QStringList &availableTemplateNames) {
    ProceduralTrackResolution result;
    result.requestedName = requestedName.trimmed();

    if (mode == ProceduralTracksMode::Disabled)
        return result;
    if (result.requestedName.compare("DISABLED", Qt::CaseInsensitive) == 0)
        return result;
    if (result.requestedName.isEmpty() && mode == ProceduralTracksMode::Enabled)
        return result;

    QString lookupName = result.requestedName;
    if (lookupName.isEmpty() || lookupName.compare("DEFAULT", Qt::CaseInsensitive) == 0)
        lookupName = "DefaultTrack";

    QString resolvedName = canonicalName(lookupName, availableTemplateNames);
    if (!resolvedName.isEmpty()) {
        result.backend = ProceduralTrackBackend::Procedural;
        result.templateName = resolvedName;
        return result;
    }

    const bool requestedDefault = lookupName.compare("DefaultTrack", Qt::CaseInsensitive) == 0;
    if (!requestedDefault)
        result.missingRequested = true;

    if (mode == ProceduralTracksMode::Forced && !requestedDefault) {
        resolvedName = canonicalName("DefaultTrack", availableTemplateNames);
        if (!resolvedName.isEmpty()) {
            result.backend = ProceduralTrackBackend::Procedural;
            result.templateName = resolvedName;
            return result;
        }
    }

    result.missingDefault = requestedDefault || mode == ProceduralTracksMode::Forced;
    return result;
}

void ProceduralTrackPolicy::warnOnce(const ProceduralTrackResolution &resolution) {
    static QSet<QString> warnedRequests;
    static bool warnedDefault = false;

    if (resolution.missingRequested) {
        const QString key = resolution.requestedName.toLower();
        if (!warnedRequests.contains(key)) {
            warnedRequests.insert(key);
            qWarning() << "Procedural track template not found:" << resolution.requestedName
                       << "- using configured fallback";
        }
    }
    if (resolution.missingDefault && !warnedDefault) {
        warnedDefault = true;
        qWarning() << "Default procedural track template DefaultTrack not found"
                   << "- using static/hardcoded fallback";
    }
}

void ProceduralTrackPolicy::warnGenerationFailureOnce(const QString &templateName) {
    static QSet<QString> warnedTemplates;
    const QString key = templateName.toLower();
    if (warnedTemplates.contains(key))
        return;
    warnedTemplates.insert(key);
    qWarning() << "Procedural track template produced no geometry:" << templateName
               << "- using static/hardcoded fallback";
}
