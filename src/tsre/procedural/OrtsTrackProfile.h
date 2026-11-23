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
#include <functional>

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
        Place,
        // Tracks task 17: the parts of a COMPLEX member, laid by the complex
        // track planner, read only in COMPLEX members (design section 2).
        // The rail section swept along every rail line.
        Rail,
        // A sleeper laid by the tie planner.
        Tie,
        // A shared tie's mesh; without one the Tie mesh is stretched.
        Bearer,
        // Placed where a rail crosses a tie or bearer.
        Fastener,
        // A third or fourth rail at Offset's x, cut where it would cross
        // another track.
        ConductorRail,
        // The rack in the middle of the track, cut the same way.
        RackRail,
        // Phase 2: a check rail opposite every crossing nose (a bridge's
        // guard rails are ordinary profile items).
        CheckRail
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
    // Tie, Bearer: the band of x that stretches when the mesh is laid
    // longer (vertices outside it keep their distance from their end);
    // (0 0) by default: x < 0 follows the left end, x > 0 the right.
    float stretchZone[2] = {0, 0};
    bool valid = true;

    // One of the COMPLEX member's parts (Rail to CheckRail).
    bool complexPart() const {
        return generationMode == GenerationMode::Rail
                || generationMode == GenerationMode::Tie
                || generationMode == GenerationMode::Bearer
                || generationMode == GenerationMode::Fastener
                || generationMode == GenerationMode::ConductorRail
                || generationMode == GenerationMode::RackRail
                || generationMode == GenerationMode::CheckRail;
    }
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

// Tracks task 17: a COMPLEX member's ComplexTrack block (design section 2).
struct OrtsProfileComplexTrack {
    // Ties along the reference line, as Repeat's Spacing and Phase.
    float tieSpacing = 0.6f;
    float tiePhase = 0.3f;
    // The least gap between two tie ends.
    float tieGap = 0.1f;
    // The longest shared tie before tracks get their own ties.
    float maxBearerLength = 4.8f;
    // Degrees: crossings steeper than this keep the first track's ties
    // through (decision 9).
    float maxSharedAngle = 60.0f;
    // The least distance kept between a conductor or rack rail and another
    // track's running rails (decision 24).
    float conductorClearance = 0.2f;
    // Phase 2: the flangeway beside noses, wing and check rails; an open
    // switch blade's tip from its stock rail; a check rail's length.
    float flangeway = 0.045f;
    float bladeOpening = 0.16f;
    float checkRailLength = 4.8f;
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
        Right,
        // Tracks task 17: the family's member for shapes whose paths share
        // ties (junctions, crossings, frog pieces); chosen by the shape's
        // geometry, never by hand.
        Complex
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
    // COMPLEX members only.
    OrtsProfileComplexTrack complexTrack;
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
    // Grows with every load (tracks task 17: shared complex track meshes
    // are made again after a reload).
    static int generation();

    // Task tracks 16. The built-in default track profile: the Track Main
    // profile of the content's GLOBAL/TRACKPROFILES/TrProfile.xml or .stf
    // when it has one (read by load(); the user, 2026-11-19), else Open
    // Rails' canned Kuju profile (rails and ballast, acleantrack1/2); either
    // laid across at `gauge`.
    static QSharedPointer<const OrtsTrackProfile> builtInDefault(float gauge);
    // The route's default track profile, as Open Rails' TRPs[0]: its
    // TrProfile file's Track Main profile, else the built-in one.
    static QSharedPointer<const OrtsTrackProfile> defaultTrack(float gauge);
    // Open Rails' GetBestTrackProfile for a static track shape: the Track
    // Main profile its texture names or the profiles' include/exclude
    // filters fit best (the default too); none when nothing fits. `images`
    // the shape's image file names.
    static QSharedPointer<const OrtsTrackProfile> guess(const QString &shapeFile,
            const QStringList &images, float gauge);
    // The same, the image names asked for only when the shape file has not
    // been guessed yet (one read per shape file).
    static QSharedPointer<const OrtsTrackProfile> guess(const QString &shapeFile,
            const std::function<QStringList()> &images, float gauge);
    // Whether guess() can need a shape's image names: some Track Main
    // profile matches by texture (its texture filters, or its texture
    // names where that can choose another than the default). False: guess()
    // without them chooses as it would with any, so the static shape need
    // not be read. defaultFallback: no fit is drawn with the default too.
    static bool guessNeedsImages(float gauge, bool defaultFallback);

private:
    // Grows with every load, so guesses never outlive their profiles.
    static int loadGeneration;
    static QString loadedRoutePath;
    static QMap<QString, QSharedPointer<OrtsTrackProfile>> profiles;
    static QStringList loadDiagnostics;
    // GLOBAL/TRACKPROFILES/TrProfile's Track Main profile, at its own gauge;
    // none: the canned one is built in.
    static QSharedPointer<const OrtsTrackProfile> globalDefault;
    // Reads it for the route at routePath (<root>/ROUTES/<route>).
    static void loadGlobalDefault(const QString &routePath);
};

#endif
