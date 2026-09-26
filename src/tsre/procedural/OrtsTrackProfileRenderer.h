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
#include <tsre/procedural/ProceduralPath.h>
#include <tsre/tdb/TSection.h>

class OglObj;
class TrackShape;
class ComplexLine;
struct OrtsTrackProfile;

struct OrtsGeneratedProfileMesh {
    enum class MaterialPass {
        Opaque,
        AlphaTest,
        Blended
    };

    QString textureName;
    MaterialPass materialPass = MaterialPass::Opaque;
    float minimumDistance = -1;
    float maximumDistance = 999999;
    QVector<float> vertices;
    float bounds[6] = {0, 0, 0, 0, 0, 0};
};

struct OrtsGeneratedProfileSharedMesh {
    QString textureName;
    OrtsGeneratedProfileMesh::MaterialPass materialPass =
            OrtsGeneratedProfileMesh::MaterialPass::Opaque;
    float minimumDistance = -1;
    float maximumDistance = 999999;
    QVector<float> vertices;
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

    static bool buildMeshes(const OrtsTrackProfile &profile,
            const QVector<TSection> &sections,
            QVector<OrtsGeneratedProfileMesh> &meshes,
            QStringList *diagnostics = nullptr,
            float endExtension = 0,
            float endDrop = 0,
            const ProceduralPathTransform *pathTransform = nullptr,
            int objectIndex = 0);
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
            int objectIndex = 0);
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
    static bool generate(const OrtsTrackProfile &profile,
            const TrackShape &trackShape,
            const QMap<int, float> &angles,
            QVector<OglObj*> &shape,
            const QString &routePath,
            QStringList *diagnostics = nullptr,
            float endExtension = 0,
            float endDrop = 0,
            const ProceduralPathTransform *pathTransform = nullptr,
            const QVector<QSharedPointer<const OrtsTrackProfile>>
                *pathProfiles = nullptr,
            int objectIndex = 0);
};

#endif
