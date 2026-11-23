/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef COMPLEXTRACKCACHE_H
#define COMPLEXTRACKCACHE_H

#include <QSharedPointer>
#include <QStringList>
#include <QVector>
#include <tsre/procedural/ComplexTrackPlan.h>
#include <tsre/procedural/TrackProfileFilters.h>

class OglObj;
class TrackShape;
class TSectionDAT;
struct OrtsTrackProfile;

// Tracks task 17: a shape's complex groups drawn from a family's COMPLEX
// member, rigid in shape space and drawn with each object's matrix like a
// static shape (design section 8, decision 6).
struct ComplexTrackMeshes {
    QVector<OglObj*> objects;
    // The shape's paths in no group: each object draws them as plain track.
    QVector<int> independent;
    ComplexTrackPlan plan;
    ~ComplexTrackMeshes();
};

class ComplexTrackCache {
public:
    // A shape's paths with the filter's included track shapes after them
    // (IncludedTrackShape), each with the parts it lays: the shape's by the
    // filter's IncludedParts/ExcludedParts (all without a filter), an
    // included one's by IncludedTrackShapeParts. Empty when a section is
    // missing.
    static QVector<ComplexTrackPlan::Path> paths(const TrackShape &shape,
            const TSectionDAT &tsection, const TrackProfileFilters::Filter *filter,
            QStringList *diagnostics = nullptr);
    // The plan of a shape for a COMPLEX member.
    static ComplexTrackPlan plan(const TrackShape &shape, const TSectionDAT &tsection,
            const OrtsTrackProfile &member, const TrackProfileFilters::Filter *filter,
            QStringList *diagnostics = nullptr);
    // The meshes of a shape's complex groups, one set shared by every
    // object with the same shape, member and filter while any holds it (a
    // profile reload makes new ones). None when the shape has no complex
    // group.
    static QSharedPointer<ComplexTrackMeshes> meshes(const TrackShape &shape,
            const TSectionDAT &tsection, const OrtsTrackProfile &member,
            const TrackProfileFilters::Filter *filter, const QString &routePath);
};

#endif
