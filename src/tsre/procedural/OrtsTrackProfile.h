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

struct OrtsProfileTemplate3D {
    enum class GenerationMode {
        Sweep,
        Stretch,
        Repeat,
        Place
    };
    enum class ShapeSelectionMode {
        First,
        ByObject,
        Cycle,
        DeterministicRandom
    };
    enum class GeometryMode {
        Baked,
        Shared
    };
    enum class PlacementLocation {
        SpanStart,
        SpanEnd,
        PathStart,
        PathEnd,
        Nodes
    };
    enum class PlacementFacing {
        AlongPath,
        AgainstPath,
        Outward,
        Inward
    };
    struct Placement {
        PlacementLocation location = PlacementLocation::SpanStart;
        PlacementFacing facing = PlacementFacing::AlongPath;
    };

    GenerationMode generationMode = GenerationMode::Sweep;
    ShapeSelectionMode shapeSelectionMode = ShapeSelectionMode::First;
    GeometryMode geometryMode = GeometryMode::Baked;
    QStringList shapes;
    float offset[3] = {0, 0, 0};
    float spacing = 0;
    float phase = 0;
    QVector<Placement> placements;
    bool valid = true;
};

struct OrtsProfileLodItem {
    enum class PathFrameMode {
        Full,
        NoRoll,
        Upright
    };

    QString name;
    QString textureName;
    QString shaderName;
    QString lightModelName;
    // The MSTS light material (vtx_state) LightModelName stands for, as Open
    // Rails reads it (Viewer3D/Common/Helpers.cs): OptSpecular0 -5,
    // OptSpecular25 -6, OptSpecular750 -7, OptFullBright -8, Cruciform -9,
    // OptHalfBright -11, DarkShade -12 (any case). None or another name: -5,
    // with known false for another name.
    int lightMaterial(bool *known = nullptr) const;
    // TSRE PBR materials (task shapes 13), as S files have them: beside
    // TexName (the base colour) the ORM(C) map (TSREOrmTexName: occlusion,
    // roughness, metalness, a clearcoat mask in alpha) and the normal map
    // (TSRENormalTexName), and the factors (ESD_TSREMaterial, the .sd's
    // token: its values; a texture key in front, as a copied .sd line has
    // it, is skipped).
    QString ormTextureName;
    QString normalTextureName;
    QVector<float> materialFactors;
    bool hasMaterialFactors = false;
    // The shader the item draws with: ShaderName, but TexDiff and
    // BlendATexDiff with all three TSRE tokens are PbrTexDiff and
    // BlendAPbrTexDiff (upgraded true), so Open Rails can still draw such a
    // profile with its own shaders.
    QString shader(bool *upgraded = nullptr) const;
    int alphaTestMode = 0;
    QString textureAddressMode;
    int alternativeTexture = 0;
    float mipMapLodBias = 0;
    PathFrameMode pathFrameMode = PathFrameMode::Full;
    QVector<OrtsProfilePolyline> polylines;
    QVector<OrtsProfileTemplate3D> templates3D;
};

struct OrtsProfileLod {
    float cutoffRadius = 0;
    QVector<OrtsProfileLodItem> items;
};

struct OrtsTrackProfile {
    enum class ObjectType {
        Track,
        Road,
        Static
    };
    enum class ObjectRole {
        Main,
        Single,
        Left,
        Middle,
        Right
    };
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
    QString familyId;
    QString name;
    QString sourcePath;
    ObjectType objectType = ObjectType::Track;
    ObjectRole objectRole = ObjectRole::Main;
    bool objectTypeExplicit = false;
    bool objectTypeValid = true;
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
    static QVector<QSharedPointer<OrtsTrackProfile>> parseFileProfiles(
            const QString &path, QStringList *diagnostics = nullptr);
    static QVector<QSharedPointer<OrtsTrackProfile>> parseStfProfiles(
            const QString &text, const QString &id = QString(),
            QStringList *diagnostics = nullptr);
    static QVector<QSharedPointer<OrtsTrackProfile>> parseXmlProfiles(
            const QString &text, const QString &id = QString(),
            QStringList *diagnostics = nullptr);
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
    static QStringList profileIds(OrtsTrackProfile::ObjectType objectType,
            bool selectableOnly = false);
    static QStringList familyIds(OrtsTrackProfile::ObjectType objectType);
    static QVector<OrtsTrackProfile::ObjectRole> familyRoles(
            const QString &familyId,
            OrtsTrackProfile::ObjectType objectType);
    static bool hasFamily(const QString &familyId,
            OrtsTrackProfile::ObjectType objectType);
    static QStringList selectionNames();
    static QStringList selectionNames(OrtsTrackProfile::ObjectType objectType,
            bool selectableOnly = false);
    static QStringList diagnostics();
    static QSharedPointer<const OrtsTrackProfile> find(const QString &name);
    static QSharedPointer<const OrtsTrackProfile> find(
            const QString &name,
            OrtsTrackProfile::ObjectType objectType);
    static QSharedPointer<const OrtsTrackProfile> findRole(
            const QString &name,
            OrtsTrackProfile::ObjectType objectType,
            OrtsTrackProfile::ObjectRole role);
    static QVector<QSharedPointer<const OrtsTrackProfile>> profilesForPaths(
            const QString &name,
            OrtsTrackProfile::ObjectType objectType,
            const QVector<float> &pathRotations);
    static QString profileId(const QString &familyId,
            OrtsTrackProfile::ObjectRole role);
    static QString routePath();

private:
    static QString loadedRoutePath;
    static QMap<QString, QSharedPointer<OrtsTrackProfile>> profiles;
    static QStringList loadDiagnostics;
};

#endif
