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
#include <QVector>
#include <tsre/tdb/TSection.h>

class OglObj;
class TrackShape;
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

class OrtsTrackProfileRenderer {
public:
    static bool buildMeshes(const OrtsTrackProfile &profile,
            const QVector<TSection> &sections,
            QVector<OrtsGeneratedProfileMesh> &meshes,
            QStringList *diagnostics = nullptr);
    static bool generate(const OrtsTrackProfile &profile,
            const QVector<TSection> &sections,
            QVector<OglObj*> &shape,
            const QString &routePath,
            QStringList *diagnostics = nullptr);
    static bool generate(const OrtsTrackProfile &profile,
            const TrackShape &trackShape,
            const QMap<int, float> &angles,
            QVector<OglObj*> &shape,
            const QString &routePath,
            QStringList *diagnostics = nullptr);
};

#endif
