/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef ORTSTRACKPROFILERENDERER_H
#define ORTSTRACKPROFILERENDERER_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QSharedPointer>
#include <QVector>
#include <array>
#include <tsre/procedural/ComplexTrackPlan.h>
#include <tsre/procedural/ProceduralPath.h>
#include <tsre/shape/MstsMaterials.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TrackCant.h>

class OglObj;
class TrackShape;
class ComplexLine;
class TDB;
class TSectionDAT;
struct OrtsTrackProfile;

// Superelevation along one generated path (task tracks 16): its sections'
// cant in path order, each read its vector section's way or reversed, as
// Open Rails' SuperElevationPrimitive takes them. Empty: flat.
struct OrtsProfileRoll {
    // Open Rails: the rail top, 0.275 m above a track section's origin.
    static constexpr float PivotHeight = 0.275f;
    struct Span {
        // Along the path.
        float from = 0.0f;
        float length = 0.0f;
        TrackCant::Section cant;
        // The path runs against its vector section.
        bool reversed = false;
    };
    QVector<Span> spans;
    // The pivot's distance from the centre, to the curve's side (Open Rails'
    // rollOffsetM: Outside half the gauge, Inside minus it, Both 0).
    float pivot = 0.0f;

    bool empty() const { return spans.isEmpty(); }
    // The angle (radians, profile space, Open Rails' roll) and the side the
    // curve turns there (Open Rails' Direction: -1, 0, 1).
    float angle(float distance, int *side = nullptr) const;
    // Whether it rolls anywhere between two distances.
    bool rolls(float from, float to) const;
    // A profile's pivot distance by its SuperElevationMethod.
    static float pivotOf(const OrtsTrackProfile &profile);
    // A static track object's paths (world tile and UiD as the track
    // database writes them): each path's vector sections found by the
    // shape's endpoint numbers they carry (MSTS and TSRE number them alike),
    // in path order. Flat where the database has none.
    static QVector<OrtsProfileRoll> forObject(TDB &tdb, int tileX, int tileZ,
            unsigned int uid, const TrackShape &shape);
    // A DynTrack's one path over its sections (those it draws).
    static OrtsProfileRoll forDynTrack(TDB &tdb, int tileX, int tileZ,
            unsigned int uid, const QVector<TSection> &sections);
};

// A PBR LODItem's material (task shapes 13; OrtsProfileLodItem::shader):
// shader Other for none.
struct OrtsGeneratedProfilePbr {
    MstsMaterials::Shader shader = MstsMaterials::Shader::Other;
    QString ormTextureName;
    QString normalTextureName;
    MstsMaterials::Factors factors;
    bool alphaTest = false;
};

struct OrtsGeneratedProfileMesh {
    enum class MaterialPass {
        Opaque,
        AlphaTest,
        Blended
    };

    QString textureName;
    MaterialPass materialPass = MaterialPass::Opaque;
    // A blended layer drawn once, in order, at the end of the alpha-tested
    // pass (RenderItem::Material::blendInOrder): a track or road profile's
    // item whose cross-section is flat (ballast, sleepers, road surfaces).
    // Static profiles' (fences, walls) and steep ones are drawn in two in
    // the blended pass.
    bool blendInOrder = false;
    // The LODItem's MSTS light material (OrtsProfileLodItem::lightMaterial).
    int lightMaterial = -5;
    OrtsGeneratedProfilePbr pbr;
    float minimumDistance = -1;
    float maximumDistance = 999999;
    QVector<float> vertices;
    // TSRE shape convention: maxX, minX, maxY, minY, maxZ, minZ.
    float bounds[6] = {0, 0, 0, 0, 0, 0};
};

struct OrtsGeneratedProfileSharedMesh {
    QString textureName;
    OrtsGeneratedProfileMesh::MaterialPass materialPass =
            OrtsGeneratedProfileMesh::MaterialPass::Opaque;
    // 3D template items: never (OrtsGeneratedProfileMesh::blendInOrder).
    bool blendInOrder = false;
    int lightMaterial = -5;
    OrtsGeneratedProfilePbr pbr;
    float minimumDistance = -1;
    float maximumDistance = 999999;
    QVector<float> vertices;
    // TSRE shape convention: maxX, minX, maxY, minY, maxZ, minZ.
    float bounds[6] = {0, 0, 0, 0, 0, 0};
    QVector<std::array<float, 16>> transforms;
};

struct OrtsGeneratedProfileInstanceObject {
    OglObj *object = nullptr;
    QVector<std::array<float, 16>> transforms;
};

class OrtsTrackProfileRenderer {
public:
    // Temporary seam mitigation. Future procedural-template stitching should
    // generate continuous joints and remove this terminal mesh overlap.
    static constexpr float GeneratedTrackEndOverlap = 0.10f;
    // Rendering chunks are an output detail: they do not add path nodes or
    // split Stretch templates. Keep the two values together so experiments
    // can tune the policy without changing profile data.
    static constexpr float GeneratedChunkTargetLength = 100.0f;
    static constexpr float GeneratedChunkingThreshold = 120.0f;
    // Authoring safety limit. Requests above this length are rejected as a
    // whole so PathStart/PathEnd never acquire misleading truncated meaning.
    static constexpr float MaximumGeneratedPathLength = 2048.0f;

    // Drop cached route-local OBJ sources after an author edits a profile or
    // mesh on disk. Generated render objects remain owned by their consumers.
    static void clearTemplateMeshCache();

    // Profile vertices (RenderItem::VNTA, triangles) in the PBR layout, as
    // S files' PBR meshes: tangents from the texture coordinates, the
    // second coordinates the first, white vertex colour.
    static QVector<float> pbrVertices(const QVector<float> &vertices);

    static float generatedPartLod(OglObj *object,
            const float *objectRotation, float objectX, float objectZ);

    static bool buildMeshes(const OrtsTrackProfile &profile,
            const QVector<TSection> &sections,
            QVector<OrtsGeneratedProfileMesh> &meshes,
            QStringList *diagnostics = nullptr,
            float endExtension = 0,
            float endDrop = 0,
            const ProceduralPathTransform *pathTransform = nullptr,
            int objectIndex = 0,
            const OrtsProfileRoll *roll = nullptr);
    static bool buildMeshes(const OrtsTrackProfile &profile,
            ComplexLine &line,
            QVector<OrtsGeneratedProfileMesh> &meshes,
            QStringList *diagnostics = nullptr,
            int objectIndex = 0,
            QVector<OrtsGeneratedProfileSharedMesh> *sharedMeshes = nullptr);
    static bool generate(const OrtsTrackProfile &profile,
            const QVector<TSection> &sections,
            QVector<OglObj*> &shape,
            const QString &routePath,
            QStringList *diagnostics = nullptr,
            float endExtension = 0,
            float endDrop = 0,
            const ProceduralPathTransform *pathTransform = nullptr,
            int objectIndex = 0,
            const OrtsProfileRoll *roll = nullptr);
    static bool generate(const OrtsTrackProfile &profile,
            ComplexLine &line,
            QVector<OglObj*> &shape,
            const QString &routePath,
            QStringList *diagnostics = nullptr,
            int objectIndex = 0);
    static bool generateWithInstances(const OrtsTrackProfile &profile,
            ComplexLine &line,
            QVector<OglObj*> &shape,
            QVector<OrtsGeneratedProfileInstanceObject> &instances,
            const QString &routePath,
            QStringList *diagnostics = nullptr,
            int objectIndex = 0);
    // Tracks task 17: a COMPLEX member's plan settings (its ComplexTrack
    // values and gauge, the Tie mesh's length and width, the Rail part's
    // offset).
    static ComplexTrackPlan::Settings complexSettings(const OrtsTrackProfile &member,
            QStringList *diagnostics = nullptr);
    // A COMPLEX member's parts laid on a plan: the bed (its polylines), the
    // rails, ties and bearers, fasteners, conductor and rack rails, each
    // LOD's items; in shape space as a static shape is drawn (the object's
    // whole matrix). Other Template3D modes are not drawn on junctions yet.
    static bool buildComplexMeshes(const OrtsTrackProfile &member,
            const ComplexTrackPlan &plan,
            QVector<OrtsGeneratedProfileMesh> &meshes,
            QStringList *diagnostics = nullptr);
    static bool generateComplex(const OrtsTrackProfile &member,
            const ComplexTrackPlan &plan, QVector<OglObj*> &objects,
            const QString &routePath, QStringList *diagnostics = nullptr);
    // Each path rolled by its entry of pathRolls (none or empty: flat).
    static bool generate(const OrtsTrackProfile &profile,
            const TrackShape &trackShape,
            const QVector<OrtsProfileRoll> &pathRolls,
            QVector<OglObj*> &shape,
            const QString &routePath,
            QStringList *diagnostics = nullptr,
            float endExtension = 0,
            float endDrop = 0,
            const ProceduralPathTransform *pathTransform = nullptr,
            const QVector<QSharedPointer<const OrtsTrackProfile>>
                *pathProfiles = nullptr,
            int objectIndex = 0,
            // The sections' source: the route's by default (a viewer
            // without a route gives its own).
            const TSectionDAT *tsection = nullptr);
};

#endif
