/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef ORTSTRACKPROFILE_H
#define ORTSTRACKPROFILE_H

#include <QMap>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVector>

struct OrtsProfileVertex {
    enum class PositionControl {
        None,
        All,
        Inside,
        Outside
    };

    float position[3] = {0, 0, 0};
    float normal[3] = {0, 1, 0};
    float texCoord[2] = {0, 0};
    PositionControl positionControl = PositionControl::All;
    bool valid = true;
};

struct OrtsProfilePolyline {
    QString name;
    float deltaTexCoord[2] = {0, 0};
    QVector<OrtsProfileVertex> vertices;
};

struct OrtsProfileLodItem {
    QString name;
    QString textureName;
    QString shaderName;
    QString lightModelName;
    int alphaTestMode = 0;
    QString textureAddressMode;
    int alternativeTexture = 0;
    float mipMapLodBias = 0;
    QVector<OrtsProfilePolyline> polylines;
};

struct OrtsProfileLod {
    float cutoffRadius = 0;
    QVector<OrtsProfileLodItem> items;
};

struct OrtsTrackProfile {
    enum class LodMethod {
        ComponentAdditive,
        CompleteReplacement
    };
    enum class PitchControl {
        None,
        ChordLength,
        ChordDisplacement
    };
    enum class SuperElevationMethod {
        None,
        Both,
        Outside,
        Inside
    };

    QString id;
    QString name;
    QString sourcePath;
    LodMethod lodMethod = LodMethod::ComponentAdditive;
    float chordSpanDegrees = 1.0f;
    PitchControl pitchControl = PitchControl::None;
    float pitchControlScalar = 0;
    QStringList includedShapes;
    QStringList excludedShapes;
    QStringList includedTextures;
    QStringList excludedTextures;
    float trackGauge = 1.435f;
    SuperElevationMethod superElevationMethod = SuperElevationMethod::Outside;
    QVector<OrtsProfileLod> lods;
    QStringList diagnostics;
    bool valid = false;
};

class OrtsTrackProfileParser {
public:
    static QSharedPointer<OrtsTrackProfile> parseFile(
            const QString &path, QStringList *diagnostics = nullptr);
    static QSharedPointer<OrtsTrackProfile> parseStf(
            const QString &text, const QString &id = QString(),
            QStringList *diagnostics = nullptr);
    static QSharedPointer<OrtsTrackProfile> parseXml(
            const QString &text, const QString &id = QString(),
            QStringList *diagnostics = nullptr);
};

class OrtsTrackProfileCatalog {
public:
    static void load(const QString &routePath, bool forceReload = false);
    static QStringList profileIds();
    static QStringList selectionNames();
    static QStringList diagnostics();
    static QSharedPointer<const OrtsTrackProfile> find(const QString &nameOrAlias);
    static QString routePath();

private:
    static QString loadedRoutePath;
    static QMap<QString, QSharedPointer<OrtsTrackProfile>> profiles;
    static QMap<QString, QString> aliases;
    static QStringList loadDiagnostics;
};

#endif
