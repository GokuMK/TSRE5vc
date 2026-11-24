/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/procedural/ComplexTrackCache.h>

#include <QDebug>
#include <QHash>
#include <QSet>
#include <QWeakPointer>
#include <tsre/ogl/OglObj.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/procedural/OrtsTrackProfileRenderer.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tdb/TrackShape.h>

ComplexTrackMeshes::~ComplexTrackMeshes() {
    qDeleteAll(objects);
}

namespace {

// A tsection shape by its FileName (any case) or index.
const TrackShape *tsectionShape(const TSectionDAT &tsection, const TrackProfileFilters::IncludedTrackShape &included) {
    if(included.index >= 0){
        const auto found = tsection.shape.find(included.index);
        return found == tsection.shape.end() ? nullptr : found->second;
    }
    static const TSectionDAT *indexed = nullptr;
    static int indexedShapes = -1;
    static QHash<QString, int> byName;
    if(indexed != &tsection || indexedShapes != int(tsection.shape.size())){
        byName.clear();
        for(const auto &entry : tsection.shape)
            if(entry.second != nullptr && !byName.contains(entry.second->filename.toLower()))
                byName.insert(entry.second->filename.toLower(), entry.first);
        indexed = &tsection;
        indexedShapes = int(tsection.shape.size());
    }
    const auto found = byName.constFind(included.shape.toLower());
    if(found == byName.constEnd())
        return nullptr;
    const auto shape = tsection.shape.find(found.value());
    return shape == tsection.shape.end() ? nullptr : shape->second;
}

QString filterKey(const TrackProfileFilters::Filter *filter) {
    if(filter == nullptr)
        return "-";
    // The description has HandOperated and SwitchSideFlip.
    QString key = filter->description() + "|" + QString::number(filter->parts)
            + "|" + QString::number(filter->includedTrackShapeParts);
    for(const TrackProfileFilters::IncludedTrackShape &included : filter->includedTrackShapes)
        key += QString("|%1 %2 %3 %4 %5").arg(included.shape).arg(included.index)
                .arg(included.position[0]).arg(included.position[2]).arg(included.rotationDeg);
    return key;
}

}

QVector<ComplexTrackPlan::Path> ComplexTrackCache::paths(const TrackShape &shape,
        const TSectionDAT &tsection, const TrackProfileFilters::Filter *filter,
        QStringList *diagnostics) {
    QVector<ComplexTrackPlan::Path> result;
    for(int i = 0; i < shape.numpaths; i++){
        ComplexTrackPlan::Path path;
        path.line = TrackShapeAnalysis::samplePath(shape, i, tsection.sekcja);
        if(path.line.size() < 2)
            return {};
        path.parts = filter != nullptr ? filter->parts : TrackProfileFilters::AllParts;
        path.main = i == shape.mainroute;
        result.append(path);
    }
    if(filter == nullptr)
        return result;
    for(const TrackProfileFilters::IncludedTrackShape &included : filter->includedTrackShapes){
        const TrackShape *other = tsectionShape(tsection, included);
        if(other == nullptr){
            if(diagnostics != nullptr)
                diagnostics->append("IncludedTrackShape " + (included.index >= 0
                        ? QString::number(included.index) : included.shape) + ": no such tsection shape");
            continue;
        }
        for(int k = 0; k < other->numpaths; k++){
            const QVector<TrackShapeAnalysis::Point> line =
                    TrackShapeAnalysis::samplePath(*other, k, tsection.sekcja);
            if(line.size() < 2)
                continue;
            ComplexTrackPlan::Path path;
            path.line = TrackShapeAnalysis::placed(line, included.position[0], included.position[2],
                    included.rotationDeg);
            path.parts = filter->includedTrackShapeParts;
            result.append(path);
        }
    }
    return result;
}

ComplexTrackPlan ComplexTrackCache::plan(const TrackShape &shape, const TSectionDAT &tsection,
        const OrtsTrackProfile &member, const TrackProfileFilters::Filter *filter,
        QStringList *diagnostics) {
    const QVector<ComplexTrackPlan::Path> shapePaths = paths(shape, tsection, filter, diagnostics);
    if(shapePaths.isEmpty())
        return ComplexTrackPlan();
    const int flags = (shape.tunnelshape ? TrackShapeAnalysis::Tunnel : 0)
            | (shape.roadshape ? TrackShapeAnalysis::Road : 0);
    ComplexTrackPlan::Settings settings = OrtsTrackProfileRenderer::complexSettings(member, diagnostics);
    if(filter != nullptr){
        settings.handOperated = filter->handOperated;
        settings.switchSideFlip = filter->switchSideFlip;
    }
    return ComplexTrackPlan::make(shapePaths, settings, flags);
}

QSharedPointer<ComplexTrackMeshes> ComplexTrackCache::meshes(const TrackShape &shape,
        const TSectionDAT &tsection, const OrtsTrackProfile &member,
        const TrackProfileFilters::Filter *filter, const QString &routePath) {
    // Most objects are plain track: their shape's kinds (kept per shape)
    // say so before any plan.
    if((TrackProfileFilters::kindsOf(shape, tsection.sekcja) & TrackShapeAnalysis::Complex) == 0
            && (filter == nullptr || filter->includedTrackShapes.isEmpty()))
        return {};
    static QHash<QString, QWeakPointer<ComplexTrackMeshes>> cache;
    static QSet<QString> withoutGroups;
    static QSet<QString> warned;
    const QString key = QString("%1|%2|%3|%4").arg(shape.id)
            .arg(quintptr(&member)).arg(OrtsTrackProfileCatalog::generation()).arg(filterKey(filter));
    if(withoutGroups.contains(key))
        return {};
    QSharedPointer<ComplexTrackMeshes> shared = cache.value(key).toStrongRef();
    if(shared != nullptr)
        return shared;

    QStringList diagnostics;
    shared.reset(new ComplexTrackMeshes());
    shared->plan = plan(shape, tsection, member, filter, &diagnostics);
    if(shared->plan.groups.isEmpty()){
        withoutGroups.insert(key);
        return {};
    }
    shared->independent = shared->plan.independent;
    OrtsTrackProfileRenderer::generateComplex(member, shared->plan, shared->objects, routePath, &diagnostics);
    for(const QString &diagnostic : diagnostics){
        const QString message = member.id + " on " + shape.filename + ": " + diagnostic;
        if(!warned.contains(message)){
            warned.insert(message);
            qWarning().noquote() << "ORTS complex track" << message;
        }
    }
    // Forget sets nothing holds any more.
    for(auto it = cache.begin(); it != cache.end();)
        it = it.value().isNull() ? cache.erase(it) : std::next(it);
    cache.insert(key, shared);
    return shared;
}
