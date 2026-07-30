/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef ORTSTRACKPROFILERENDERER_H
#define ORTSTRACKPROFILERENDERER_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <tsre/tdb/TSection.h>

class OglObj;
struct OrtsTrackProfile;

struct OrtsGeneratedProfileMesh {
    QString textureName;
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
};

#endif
