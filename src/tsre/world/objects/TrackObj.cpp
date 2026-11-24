/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/objects/TrackObj.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/shape/ShapeLod.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/math3d/Flex.h>
#include <math.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/TS.h>
#include <QDebug>
#include <tsre/Game.h>
#include <tsre/tdb/TDB.h>
#include <tsre/ogl/TrackItemObj.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/procedural/ProceduralShape.h>
#include <tsre/procedural/ProceduralTrackPolicy.h>
#include <tsre/procedural/TrackProfileFilters.h>
#include <tsre/procedural/ComplexTrackCache.h>
#include <tsre/procedural/ShapeTemplates.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/procedural/OrtsTrackProfileRenderer.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/ErrorMessage.h>
#include <tsre/renderer/Renderer.h>
#include <QSet>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

void fillYawOnlyTrackMatrix(const float *qDirection,
        const float *position, float *matrix) {
    float q[4];
    Quat::fill(q);
    Quat::rotateY(q, q,
            -Flex::TdbYawFromTrackQuaternion(qDirection));
    Mat4::fromRotationTranslation(matrix, q,
            const_cast<float*>(position));
    Mat4::rotate(matrix, matrix, (float)M_PI, 0, -1, 0);
}

}

TrackObj::TrackObj() {
    // An absent ShapeTemplate is distinct from explicit DEFAULT in Enabled mode.
    this->templateName.clear();
    this->shape = -1;
    this->loaded = false;
    this->sectionIdx = 0;
    this->elevation = 0;
    this->staticFlags = 0x200180;
    this->collideFlags = 39;
    this->modified = false;
}

TrackObj::TrackObj(const TrackObj& o) : WorldObj(o) {
    for(int i = 0; i < o.jNodePosn.size(); i++ ){
        jNodePosn.push_back(std::array<float,5>());
        jNodePosn.back()[0] = o.jNodePosn[i][0];
        jNodePosn.back()[1] = o.jNodePosn[i][1];
        jNodePosn.back()[2] = o.jNodePosn[i][2];
        jNodePosn.back()[3] = o.jNodePosn[i][3];
        jNodePosn.back()[4] = o.jNodePosn[i][4];
    }

    elevation = o.elevation;
}

WorldObj* TrackObj::clone(){
    return new TrackObj(*this);
}

TrackObj::~TrackObj() {
    clearProceduralShape();
}

int TrackObj::updateTrackSectionInfo(QHash<unsigned int,unsigned int> shapes, QHash<unsigned int,unsigned int> sect){
    int count = 0;
    if(shapes[sectionIdx] > 0){
        sectionIdx = shapes[sectionIdx];
        count++;
    }
    return count;
}

void TrackObj::loadingFixes(){
    if(WorldObj::positiveQuaternionSerialization()){
        if(qDirection[3] < 0){
            Quat::makePositive(qDirection);
            ErrorMessage *e = new ErrorMessage(
                    ErrorMessage::Type_Info, 
                    ErrorMessage::Source_Editor, 
                    QString("Fixed negative quaternion in tile ") + QString::number(x) + " " + QString::number(y) + " : " + QString::number(typeID) );
            ErrorMessagesLib::PushErrorMessage(e);
            setModified();
        }            
    }
}

void TrackObj::load(int x, int y) {
    this->shape = Game::currentShapeLib->addShape(resPath +"/"+ fileName);
    this->shapePointer = Game::currentShapeLib->shape[this->shape];
    this->x = x;
    this->y = y;
    this->position[2] = -this->position[2];
    this->qDirection[2] = -this->qDirection[2];
    this->loaded = true;
    this->size = -1;
    this->skipLevel = 3;
    this->endp = new float[5];
    this->endp[0] = 0;
    this->endp[1] = 0;
    this->endp[2] = 0;
    this->endp[3] = 1;
    this->endp[4] = 0;
    this->placedAtPosition[0] = this->position[0];
    this->placedAtPosition[1] = this->position[1];
    this->placedAtPosition[2] = this->position[2];
    this->firstPosition[0] = this->position[0];
    this->firstPosition[1] = this->position[1];
    this->firstPosition[2] = this->position[2];
    
    if(Game::trackDB != NULL)
        this->roadShape = Game::trackDB->tsection->isRoadShape(sectionIdx);
    setMartix();
}

void TrackObj::loadInit(){
    if(Game::trackDB != NULL)
        this->roadShape = Game::trackDB->tsection->isRoadShape(sectionIdx);
}

bool TrackObj::allowNew(){
    return true;
}

void TrackObj::setTemplate(QString name){
    templateName = name;
    setModified();
    reload();
}

void TrackObj::reload(){
    reloadProceduralProfile();
    if(shapePointer != NULL)
        shapePointer->reload();
}

void TrackObj::reloadProceduralProfile(){
    proceduralShapeInit = false;
    proceduralFallback = false;
    clearProceduralShape();
}

void TrackObj::clearProceduralShape(){
    if(procShapeOwned)
        for(OglObj *object : procShape)
            delete object;
    procShape.clear();
    procShapeOwned = false;
    proceduralShapeUsesBakedPath = false;
    complexShared.reset();
}

bool TrackObj::useComplexShape(const OrtsTrackProfile &profile, OrtsTrackProfile::ObjectType type,
        const TrackProfileFilters::Filter *filter, const TrackShape &shape,
        const QVector<OrtsProfileRoll> &rolls,
        const QVector<QSharedPointer<const OrtsTrackProfile>> *pathProfiles,
        const QString &routePath, const ProceduralPathTransform &transform){
    if(Game::trackDB == NULL || Game::trackDB->tsection == NULL)
        return false;
    const QSharedPointer<const OrtsTrackProfile> member = OrtsTrackProfileCatalog::find(
            OrtsTrackProfileCatalog::profileId(profile.familyId, OrtsTrackProfile::ObjectRole::Complex), type);
    if(member == nullptr)
        return false;
    const QSharedPointer<ComplexTrackMeshes> shared = ComplexTrackCache::meshes(
            shape, *Game::trackDB->tsection, *member, filter, routePath);
    if(shared == nullptr)
        return false;
    clearProceduralShape();
    complexShared = shared;
    // The shape's own paths in no group (included shapes' paths follow
    // them in the plan), per object as plain track.
    QVector<int> own;
    for(int path : shared->independent)
        if(path < shape.numpaths)
            own.append(path);
    if(!own.isEmpty()){
        TrackShape subset;
        subset.id = shape.id;
        subset.filename = shape.filename;
        subset.numpaths = own.size();
        subset.path = new TrackShape::SectionIdx[own.size()];
        QVector<OrtsProfileRoll> subsetRolls;
        QVector<QSharedPointer<const OrtsTrackProfile>> subsetProfiles;
        for(int k = 0; k < own.size(); k++){
            subset.path[k] = shape.path[own[k]];
            subsetRolls.append(own[k] < rolls.size() ? rolls[own[k]] : OrtsProfileRoll());
            if(pathProfiles != NULL)
                subsetProfiles.append(own[k] < pathProfiles->size() ? pathProfiles->at(own[k])
                                                                    : QSharedPointer<const OrtsTrackProfile>());
        }
        QStringList diagnostics;
        OrtsTrackProfileRenderer::generate(profile, subset, subsetRolls, procShape, routePath,
                &diagnostics, OrtsTrackProfileRenderer::GeneratedTrackEndOverlap, 0, &transform,
                pathProfiles != NULL ? &subsetProfiles : nullptr, UiD);
        delete[] subset.path;
        subset.path = NULL;
        procShapeOwned = true;
    }
    proceduralShapeUsesBakedPath = true;
    proceduralFallback = false;
    return true;
}

namespace {

// Tracks task 17: the route's filter for an object without its own
// template; none when a filter names a family the route has no profile of
// (warned about once a filter), so the object is drawn as without it.
// Each path's place on its vector in a database (track or road; tracks
// task 17 step 8) and, where superelevation is shown, its cant (task 16).
QVector<OrtsProfileRoll> pathRollsOf(TDB *db, int tileX, int tileZ, unsigned int uid,
        const TrackShape &shape, bool cant) {
    if(db == NULL)
        return QVector<OrtsProfileRoll>();
    QVector<OrtsProfileRoll> rolls = OrtsProfileRoll::forObject(*db, tileX, tileZ, uid, shape);
    if(!cant)
        for(OrtsProfileRoll &roll : rolls)
            roll.spans.clear();
    return rolls;
}

const TrackProfileFilters::Filter *routeFilter(const QString &fileName,
        const QString &requested, const TrackShape *shape,
        OrtsTrackProfile::ObjectType type){
    if(!requested.isEmpty() || shape == NULL
            || Game::proceduralTracks == ProceduralTracksMode::Disabled)
        return NULL;
    const TrackProfileFilters::Filter *filter = TrackProfileFilters::match(fileName, [&]() {
        return TrackProfileFilters::kindsOf(*shape, Game::trackDB->tsection->sekcja);
    });
    if(filter == NULL || filter->disabled
            || OrtsTrackProfileCatalog::find(filter->profile, type) != nullptr)
        return filter;
    static QSet<QString> warned;
    const QString key = filter->description() + (type == OrtsTrackProfile::ObjectType::Road ? " road" : " track");
    if(!warned.contains(key)){
        warned.insert(key);
        qWarning().noquote() << TrackProfileFilters::fileName() + " " + filter->description()
                + ": the route has no " + (type == OrtsTrackProfile::ObjectType::Road ? "road" : "track")
                + " profile family " + filter->profile + "; its objects are drawn as without the filter";
    }
    return NULL;
}

}

bool TrackObj::useProceduralShape(){
    if(proceduralShapeInit)
        return !proceduralFallback;

    proceduralShapeInit = true;
    proceduralFallback = true;
    clearProceduralShape();

    ProceduralShape::Load();
    const QString routePath = Game::root + "/ROUTES/" + Game::route;
    OrtsTrackProfileCatalog::load(routePath);
    const OrtsTrackProfile::ObjectType profileType = roadShape
            ? OrtsTrackProfile::ObjectType::Road
            : OrtsTrackProfile::ObjectType::Track;
    QStringList availableTemplates =
            OrtsTrackProfileCatalog::selectionNames(profileType);
    if(ProceduralShape::ShapeTemplateFile != NULL){
        for(const QString &globalName
                : ProceduralShape::ShapeTemplateFile->templates.keys()){
            if(OrtsTrackProfileCatalog::find(globalName, profileType) == nullptr)
                availableTemplates.append(globalName);
        }
    }

    // Task tracks 16: in the Superelevation and Forced modes plain rail
    // track without a template is drawn with a track profile and its
    // superelevation, as Open Rails' DecomposeStaticSuperElevation:
    // Superelevation where its track is superelevated, Forced everywhere;
    // the profile its textures fit, else (ProceduralTrackPolicy::
    // noProfileFallback) the default one. What draws no superelevation
    // tells the track database (TrackCant::setVisualOff).
    const bool superelevationMode = Game::proceduralTracks == ProceduralTracksMode::Superelevation
            || Game::proceduralTracks == ProceduralTracksMode::Forced;
    const bool railCant = superelevationMode && !roadShape && Game::trackDB != NULL;
    TrackShape *ownShape = NULL;
    if(Game::trackDB != NULL && Game::trackDB->tsection != NULL){
        const auto found = Game::trackDB->tsection->shape.find(sectionIdx);
        if(found != Game::trackDB->tsection->shape.end())
            ownShape = found->second;
    }
    // Tracks task 17: an object without its own template takes its profile
    // family from the route's filters (trackprofiles.dat), ahead of Open
    // Rails' guess; a filter's DISABLED keeps the static shape. The filters
    // give the profile wherever track is procedural (decision 29): complex
    // shapes are procedural only in Forced.
    const QString requested = templateName.trimmed();
    const TrackProfileFilters::Filter *filter = routeFilter(fileName, requested, ownShape, profileType);
    proceduralSource = ProfileSource::Static;
    proceduralSourceDetail.clear();
    if(requested.compare("DISABLED", Qt::CaseInsensitive) == 0)
        proceduralSource = ProfileSource::ObjectDisabled;
    if(filter != NULL && filter->disabled){
        proceduralSource = ProfileSource::FilterDisabled;
        proceduralSourceDetail = filter->description();
        if(railCant)
            TrackCant::setVisualOff(*Game::trackDB, x, -y, UiD, true);
        return false;
    }
    if(railCant && ownShape != NULL){
        const bool plain = !ownShape->tunnelshape
                && !(ownShape->numpaths > 1 && (ownShape->clearancedist > 0 || ownShape->mainroute >= 0));
        if(requested.compare("DISABLED", Qt::CaseInsensitive) == 0){
            TrackCant::setVisualOff(*Game::trackDB, x, -y, UiD, true);
        } else if(requested.isEmpty() && plain){
            const QVector<OrtsProfileRoll> rolls = pathRollsOf(Game::trackDB, x, -y, UiD, *ownShape,
                    Game::trackCantShown());
            bool canted = false;
            for(const OrtsProfileRoll &roll : rolls)
                canted = canted || !roll.empty();
            if(Game::proceduralTracks == ProceduralTracksMode::Forced || canted){
                const float gauge = Game::trackDB->cant.route.gauge;
                const bool defaultFallback = Game::proceduralTracks == ProceduralTracksMode::Forced
                        || ProceduralTrackPolicy::noProfileFallback == NoProfileFallback::DefaultProfile;
                QSharedPointer<const OrtsTrackProfile> profile;
                // A filter's family: each path its member (MAIN, or the side
                // roles for parallel paths).
                QVector<QSharedPointer<const OrtsTrackProfile>> pathProfiles;
                ProfileSource source = ProfileSource::Guess;
                if(filter != NULL){
                    source = ProfileSource::Filter;
                    profile = OrtsTrackProfileCatalog::find(filter->profile, profileType);
                    QVector<float> rotations;
                    for(int pathIndex = 0; pathIndex < ownShape->numpaths; pathIndex++)
                        rotations.append(ownShape->path[pathIndex].rotDeg);
                    pathProfiles = OrtsTrackProfileCatalog::profilesForPaths(
                            filter->profile, profileType, rotations);
                } else {
                    // The static shape is never loaded for the guess. Where
                    // the route's profiles match by texture names, only its
                    // file's image list is read, once per shape file
                    // (decision 14).
                    const QString shapePath = ContentPath::join(resPath, fileName);
                    profile = OrtsTrackProfileCatalog::guess(fileName, [&]() {
                        return OrtsTrackProfileCatalog::guessNeedsImages(gauge, defaultFallback)
                                ? SFileComplex::readImageNames(shapePath) : QStringList();
                    }, gauge);
                    if(profile == nullptr && defaultFallback){
                        profile = OrtsTrackProfileCatalog::defaultTrack(gauge);
                        source = ProfileSource::Default;
                    }
                }
                TrackCant::setVisualOff(*Game::trackDB, x, -y, UiD, profile == nullptr
                        || profile->superElevationMethod == OrtsTrackProfile::SuperElevationMethod::None);
                if(profile == nullptr)
                    return false;
                TrackCant::setPivot(*Game::trackDB, x, -y, UiD, OrtsProfileRoll::pivotOf(*profile));
                const ProceduralPathTransform ownTransform =
                        ProceduralPath::bakedObjectTransform(
                            qDirection,
                            Flex::TdbYawFromTrackQuaternion(qDirection));
                // Tracks task 17: frog pieces and other plain shapes whose
                // paths share ties, from the family's COMPLEX member (Forced).
                if(Game::proceduralTracks == ProceduralTracksMode::Forced
                        && useComplexShape(*profile, profileType, filter, *ownShape, rolls,
                                pathProfiles.isEmpty() ? NULL : &pathProfiles, routePath, ownTransform)){
                    proceduralSource = source;
                    proceduralSourceDetail = filter != NULL ? filter->description() : profile->id;
                    return true;
                }
                QStringList diagnostics;
                if(OrtsTrackProfileRenderer::generate(
                        *profile, *ownShape, rolls, procShape, routePath, &diagnostics,
                        OrtsTrackProfileRenderer::GeneratedTrackEndOverlap, 0,
                        &ownTransform, pathProfiles.isEmpty() ? nullptr : &pathProfiles,
                        UiD) && !procShape.isEmpty()){
                    procShapeOwned = true;
                    proceduralShapeUsesBakedPath = true;
                    proceduralFallback = false;
                    proceduralSource = source;
                    proceduralSourceDetail = filter != NULL ? filter->description() : profile->id;
                    return true;
                }
                clearProceduralShape();
                return false;
            }
            TrackCant::setVisualOff(*Game::trackDB, x, -y, UiD, false);
        }
    }

    // In Forced a filter's family takes the place of the default one (for
    // track DefaultTrack; for roads RdProfile, as road DynTracks, task 17's
    // decision 19).
    const bool fromFilter = filter != NULL && requested.isEmpty()
            && Game::proceduralTracks == ProceduralTracksMode::Forced;
    const ProceduralTrackResolution resolution = ProceduralTrackPolicy::resolve(
            Game::proceduralTracks, fromFilter ? filter->profile : templateName,
            availableTemplates,
            roadShape ? QStringLiteral("RdProfile") : QStringLiteral("DefaultTrack"));
    ProceduralTrackPolicy::warnOnce(resolution);
    if(resolution.backend == ProceduralTrackBackend::Fallback)
        return false;
    ProfileSource resolvedSource = ProfileSource::Default;
    QString resolvedDetail = resolution.templateName;
    if(fromFilter && resolution.templateName.compare(filter->profile, Qt::CaseInsensitive) == 0){
        resolvedSource = ProfileSource::Filter;
        resolvedDetail = filter->description();
    } else if(!requested.isEmpty()
            && resolution.templateName.compare(requested, Qt::CaseInsensitive) == 0){
        resolvedSource = ProfileSource::ObjectTemplate;
    }
    if(Game::trackDB == NULL || Game::trackDB->tsection == NULL
            || Game::trackDB->tsection->shape.find(sectionIdx)
                    == Game::trackDB->tsection->shape.end()
            || Game::trackDB->tsection->shape[sectionIdx] == NULL)
        return false;

    TrackShape *tsh = Game::trackDB->tsection->shape[sectionIdx];
    // Task tracks 16: TSRE's old roll from the vector sections' AZ is gone;
    // the profile's roll comes from the track database's superelevation.
    QMap<int, float> angles;

    const ProceduralPathTransform pathTransform =
            ProceduralPath::bakedObjectTransform(
                qDirection,
                Flex::TdbYawFromTrackQuaternion(qDirection));

    const QSharedPointer<const OrtsTrackProfile> routeProfile =
            OrtsTrackProfileCatalog::find(
                resolution.templateName, profileType);
    if(routeProfile != nullptr){
        QVector<float> pathRotations;
        pathRotations.reserve(tsh->numpaths);
        for(int pathIndex = 0; pathIndex < tsh->numpaths; pathIndex++)
            pathRotations.append(tsh->path[pathIndex].rotDeg);
        const QVector<QSharedPointer<const OrtsTrackProfile>> pathProfiles =
                OrtsTrackProfileCatalog::profilesForPaths(
                    resolution.templateName, profileType, pathRotations);
        if(railCant)
            TrackCant::setVisualOff(*Game::trackDB, x, -y, UiD,
                    routeProfile->superElevationMethod == OrtsTrackProfile::SuperElevationMethod::None);
        QStringList diagnostics;
        // Task tracks 16: each path's superelevation from the track
        // database, where it is shown (rail only).
        // Tracks task 17 step 8: and each path's place on its vector (the
        // road database's for roads), its repeats in step with it.
        const bool cantShown = Game::trackCantShown() && !roadShape && Game::trackDB != NULL;
        const QVector<OrtsProfileRoll> pathRolls = pathRollsOf(roadShape ? Game::roadDB : Game::trackDB,
                x, -y, UiD, *tsh, cantShown);
        if(cantShown)
            TrackCant::setPivot(*Game::trackDB, x, -y, UiD, OrtsProfileRoll::pivotOf(*routeProfile));
        // Tracks task 17: junctions, crossings and frog pieces from the
        // family's COMPLEX member, with the filter's parts when a filter
        // gave the family.
        if(useComplexShape(*routeProfile, profileType, fromFilter ? filter : NULL, *tsh, pathRolls,
                &pathProfiles, routePath, pathTransform)){
            proceduralSource = resolvedSource;
            proceduralSourceDetail = resolvedDetail;
            return true;
        }
        if(OrtsTrackProfileRenderer::generate(
                *routeProfile, *tsh, pathRolls, procShape, routePath,
                &diagnostics,
                // Keep independently generated high profiles visually joined
                // without moving their stored track-path endpoints.
                OrtsTrackProfileRenderer::GeneratedTrackEndOverlap, 0,
                &pathTransform, &pathProfiles, UiD)){
            procShapeOwned = true;
            proceduralShapeUsesBakedPath = true;
            static QSet<QString> warnedDiagnostics;
            for(const QString &diagnostic : diagnostics){
                const QString key = routeProfile->id.toLower() + ":" + diagnostic;
                if(!warnedDiagnostics.contains(key)){
                    warnedDiagnostics.insert(key);
                    qWarning() << "ORTS static track profile" << routeProfile->id
                               << diagnostic;
                }
            }
        }
    } else {
        // Native templates are not superelevated (task tracks 16).
        if(railCant)
            TrackCant::setVisualOff(*Game::trackDB, x, -y, UiD, true);
        // The advanced crossing tie generator combines paths after their
        // individual frames are built. Keep its legacy transform until that
        // specialized generator can consume the shared baked-path frame.
        const bool usesAdvancedTies = tsh->numpaths == 2
                && (tsh->xoverpts > 0 || tsh->mainroute > -1);
        if(!usesAdvancedTies){
            ProceduralShape::GenerateShape(
                    resolution.templateName, procShape, tsh, angles,
                    pathTransform);
            procShapeOwned = true;
            proceduralShapeUsesBakedPath = !procShape.isEmpty();
        } else {
            ProceduralShape::GetShape(
                    resolution.templateName, procShape, tsh, angles);
        }
    }
    if(procShape.isEmpty()){
        ProceduralTrackPolicy::warnGenerationFailureOnce(resolution.templateName);
        return false;
    }

    proceduralFallback = false;
    proceduralSource = resolvedSource;
    proceduralSourceDetail = resolvedDetail;
    return true;
}

float TrackObj::getElevation(){
    float vect[3];
    vect[0] = 0; vect[1] = 0; vect [2] = 1000;
    Vec3::transformQuat(vect, vect, qDirection);
    return this->endp[3]*asin(-vect[1]/1000.0);
}

void TrackObj::setElevation(float prom){
    float * q = qDirection;
    float vect[3];
    vect[0] = 0; vect[1] = 0; vect [2] = 1000;
    Vec3::transformQuat(vect, vect, q);
    rotate(asin((vect[1]*this->endp[3]+prom)/1000.0),0,0);
}

void TrackObj::rotate(float x, float y, float z){
    this->tRotation[0] += x;
    this->tRotation[1] += y;
    if(matrix3x3 != NULL) matrix3x3 = NULL;

    qDebug() << "rot" << x << y << z;
    float vect2[3];
    float vect[3];
    float quat[4];
    Quat::fill(quat);
    
    if(x!=0) Quat::rotateX(this->qDirection, this->qDirection, x*this->endp[3]);
    if(y!=0) Quat::rotateY(this->qDirection, this->qDirection, y*this->endp[3]);
    if(z!=0) Quat::rotateZ(this->qDirection, this->qDirection, z*this->endp[3]);    
    
    Vec3::set(vect, 0, 0, 10);
    Vec3::transformQuat(vect, vect, this->qDirection);
    
    Quat::fill(quat);
    Quat::rotateY(quat, quat, endp[4]);

    Vec3::transformQuat(reinterpret_cast<float*>(&vect), this->endp, this->qDirection);
    Vec3::transformQuat(reinterpret_cast<float*>(&vect2), this->endp, quat);

    qDebug() << this->endp[0] << " "<< vect[0] << " " << vect2[0];
    qDebug() << this->endp[2] << " "<< vect[2] << " " << vect2[2];
    
    vect[0] = (vect2[0] - vect[0]);
    vect[2] = (vect2[2] - vect[2]);
    
    this->position[0] = this->placedAtPosition[0] + vect[0];
    this->position[1] = this->placedAtPosition[1] - vect[1];
    this->position[2] = this->placedAtPosition[2] + vect[2];

    if(x != 0 || y != 0 || z != 0){
        proceduralShapeInit = false;
        proceduralFallback = false;
        clearProceduralShape();
    }

    setModified();
    setMartix();
}

void TrackObj::set(QString sh, long long int val) {
    if (sh == ("sectionidx")) {
        sectionIdx = val;
        return;
    }
    if (sh == ("ref_value")) {
        sectionIdx = val;
        return;
    }
    WorldObj::set(sh, val);
    setModified();
}

void TrackObj::set(QString sh, QString val){ 
    if (sh == ("filename")) {
        fileName = val;
        return;
    }
    if (sh == ("ref_filename")) {
        fileName = val;
        return;
    }
    WorldObj::set(sh, val);
    return;
}

void TrackObj::set(TS::TokenId sh, FileBuffer* data) {
    if (sh == TS::FileName) {
        data->skipLabel();
        int slen = data->getShort()*2;
        fileName = *data->getString(data->off, data->off + slen);
        data->off += slen;
        return;
    }
    if (sh == TS::SectionIdx) {
        data->skipLabel();
        sectionIdx = data->getUint();
        return;
    }
    if (sh == TS::Elevation) {
        data->skipLabel();
        elevation = data->getFloat();
        return;
    }
    if (sh == TS::JNodePosn) {
        data->skipLabel();
        jNodePosn.push_back(std::array<float,5>());
        jNodePosn.back()[0] = data->getInt();
        jNodePosn.back()[1] = data->getInt();
        jNodePosn.back()[2] = data->getFloat();
        jNodePosn.back()[3] = data->getFloat();
        jNodePosn.back()[4] = data->getFloat();
        return;
    }
    
    WorldObj::set(sh, data);
    return;
}

void TrackObj::set(QString sh, FileBuffer* data) {
    if (sh == ("filename")) {
        fileName = ParserX::GetString(data);
        return;
    }
    if (sh == ("sectionidx")) {
        sectionIdx = ParserX::GetNumber(data);
        return;
    }
    if (sh == ("elevation")) {
        elevation = ParserX::GetNumber(data);
        return;
    }
    if (sh == ("jnodeposn")) {
        jNodePosn.push_back(std::array<float,5>());
        jNodePosn.back()[0] = ParserX::GetNumber(data);
        jNodePosn.back()[1] = ParserX::GetNumber(data);
        jNodePosn.back()[2] = ParserX::GetNumber(data);
        jNodePosn.back()[3] = ParserX::GetNumber(data);
        jNodePosn.back()[4] = ParserX::GetNumber(data);
        return;
    }
    
    WorldObj::set(sh, data);
    return;
}

QString TrackObj::getShapePath(){
    if (!loaded) return "";
    if (shapePointer == 0) return "";
    return shapePointer->getShapePreviewPath();
}

bool TrackObj::isSimilar(WorldObj* obj){
    if(obj->typeID != this->typeID)
        return false;
    if(obj->fileName == this->fileName)
        return true;
    return false;
}

// Roads do not cast shadows unless marked dynamic.
bool TrackObj::castsShadows(){
    if (getShadowType() == WorldObj::ShadowDynamic)
        return true;
    return !Game::mstsShadows && !roadShape;
}

void TrackObj::pushRenderItems(RenderQueue &queue, float lod, float posx, float posz, float* playerW, float* target, float fov, quint32 selectionId){
    if (!loaded) return;
    if (shape < 0) return;
    if (jestPQ < 2) return;
    
    if (size > 0) {
        if ((lod > size + 150)) {
            float v1[2];
            v1[0] = playerW[0] - (target[0]);
            v1[1] = playerW[2] - (target[2]);
            float v2[2];
            v2[0] = posx;
            v2[1] = posz;
            float iloczyn = v1[0] * v2[0] + v1[1] * v2[1];
            float d1 = sqrt(v1[0] * v1[0] + v1[1] * v1[1]);
            float d2 = sqrt(v2[0] * v2[0] + v2[1] * v2[1]);
            float zz = iloczyn / (d1 * d2);
            if (zz > 0) return;

            // The same field of view for every object: its cosine once (0.25 ms
            // a frame at Prudnik's freight station).
            static float cosineFov = -1.0f;
            static decltype(cos(fov)) cosine = cos(fov);
            if (fov != cosineFov) {
                cosineFov = fov;
                cosine = cos(fov);
            }
            float ccos = cosine + zz;
            float xxx = sqrt(2 * d2 * d2 * (1 - ccos));
            //if((ccos > 0) && (xxx > 200+50)) return;
            if ((ccos > 0) && (xxx > size) && (skipLevel == 1)) return;
        }
    } else {
        //if (!Game::proceduralTracks)
            {
                ComplexShape* shapeAsset = Game::currentShapeLib->shape[shape];
                if (shapeAsset != NULL && shapeAsset->isLoaded())
                    size = shapeAsset->getSize();
            }
    }
    
    
    const bool procedural = useProceduralShape();
    float bakedMatrix[16];
    float *renderMatrix = matrix;
    if(procedural && proceduralShapeUsesBakedPath){
        fillYawOnlyTrackMatrix(qDirection, position, bakedMatrix);
        renderMatrix = bakedMatrix;
    }
    // Tracks task 17: the shape's complex groups, rigid in shape space, with
    // the object's whole matrix as a static shape.
    if(procedural && complexShared != nullptr && !complexShared->objects.isEmpty()){
        queue.pushTransform();
        Mat4::multiply(queue.transform(), queue.transform(), matrix);
        for(OglObj *object : complexShared->objects)
            object->pushRenderItem(queue, selectionId,
                    OrtsTrackProfileRenderer::generatedPartLod(object, matrix, posx, posz));
        queue.popTransform();
    }
    Mat4::multiply(queue.transform(),
            queue.transform(), renderMatrix);
    
    if(Game::showWorldObjPivotPoints){
        if(pointer3d == NULL){
            pointer3d = new TrackItemObj(1);
            pointer3d->setMaterial(0.9,0.9,0.7);
        }
        pointer3d->pushRenderItem(queue, selectionId);
    }
    
    /*if(selectionId != 0){
        gluu->setSelectionId(selectionId);
    } else {
        gluu->enableTextures();
    }*/
    if(!procedural) {
        if(shapePointer != NULL){
            const ShapeLod::Drawing drawing = ShapeLod::apply(*shapePointer, 0, {lod, position[1] - playerW[1]});
            queue.setLightsShown(drawing.lights);
            if (drawing)
                shapePointer->pushRenderItem(queue, selectionId, 0);
            queue.setLightsShown(true);
        }
    } else {
        for(int i = 0; i < procShape.size(); i++){
            const float partLod =
                    OrtsTrackProfileRenderer::generatedPartLod(
                        procShape[i], renderMatrix, posx, posz);
            procShape[i]->pushRenderItem(queue, selectionId, partLod);
        }
    }


    if(selected){
        pushRenderBox(queue);
    }
}

void TrackObj::fillJNodePosn(){
    TDB* tdb = Game::trackDB;
    setModified(tdb->fillJNodePosn(this->x, this->y, this->UiD, &this->jNodePosn));
}

bool TrackObj::getSimpleBorder(float* border){
    if (shapePointer == 0) return false;
    if (!shapePointer->isLoaded())
        return false;
    const float* bound = shapePointer->getBound();
    border[0] = bound[0];
    border[1] = bound[1];
    border[2] = bound[2];
    border[3] = bound[3];
    border[4] = bound[4];
    border[5] = bound[5];
    return true;
}

bool TrackObj::getBoxPoints(QVector<float>& points){
    if (shapePointer == 0) return false;
    if (!shapePointer->isLoaded())
        return false;
    return shapePointer->getBoxPoints(points);
    /*
        float bound[6];
        if (!getSimpleBorder((float*)&bound)) return false;
        
        for(int i=0; i<2; i++)
            for(int j=4; j<6; j++){
                points.push_back(bound[i]);
                points.push_back(bound[2]);
                points.push_back(bound[j]);
                points.push_back(bound[i]);
                points.push_back(bound[3]);
                points.push_back(bound[j]);
            }
        for(int i=0; i<2; i++)
            for(int j=2; j<4; j++){
                points.push_back(bound[i]);
                points.push_back(bound[j]);
                points.push_back(bound[4]);
                points.push_back(bound[i]);
                points.push_back(bound[j]);
                points.push_back(bound[5]);
            }
        for(int i=4; i<6; i++)
            for(int j=2; j<4; j++){
                points.push_back(bound[0]);
                points.push_back(bound[j]);
                points.push_back(bound[i]);
                points.push_back(bound[1]);
                points.push_back(bound[j]);
                points.push_back(bound[i]);
            }
        return true;*/
}

Ref::RefItem* TrackObj::getRefInfo(){
    Ref::RefItem* r = new Ref::RefItem();
    r->type = this->type;
    r->filename.push_back(this->fileName);
    r->value = this->sectionIdx;
    r->staticFlags = this->staticFlags;
    return r;
}

int TrackObj::getDefaultDetailLevel(){
    if (!loaded) return -2;
    if (shapePointer == 0) return -2;
    int esdDLevel = shapePointer->getEsdDetailLevel();
    if(esdDLevel >= 0) return esdDLevel;
    return -2;
}

int TrackObj::getCollisionType(){
    bool enabled = ((this->collideFlags & 2) >> 1) == 0;
    if(enabled){
        if(this->collideFunction == 1 )
            return 2;
        else 
            return 1;
    } else {
        return 0;
    }
}

void TrackObj::save(QTextStream* out){
    if (!loaded) return;
    if (jestPQ < 2) return;
    if(WorldObj::positiveQuaternionSerialization())
        Quat::makePositive(this->qDirection);
    
*(out) << "	TrackObj (\n";
*(out) << "		UiD ( "<<this->UiD<<" )\n";
*(out) << "		SectionIdx ( "<<this->sectionIdx<<" )\n";
*(out) << "		Elevation ( "<<this->elevation<<" )\n";
if(this->jNodePosn.size() != 0)
    for(int i = 0; i < this->jNodePosn.size(); i++ ){
        *(out) << "		JNodePosn ( "<<this->jNodePosn[i][0]<<" "<<this->jNodePosn[i][1]<<" "<<this->jNodePosn[i][2]<<" "<<this->jNodePosn[i][3]<<" "<<this->jNodePosn[i][4]<<" )\n";
    }
*(out) << "		CollideFlags ( "<<this->collideFlags<<" )\n";
if(this->collideFunction > 0 )
*(out) << "		CollideFunction ( "<<this->collideFunction<<" )\n";
if(this->templateName != "")
*(out) << "		ShapeTemplate ( "<<ParserX::AddComIfReq(this->templateName)<<" )\n";
*(out) << "		FileName ( "<<ParserX::AddComIfReq(this->fileName)<<" )\n";
*(out) << "		StaticFlags ( "<<ParserX::MakeFlagsString(this->staticFlags)<<" )\n";
*(out) << "		Position ( "<<this->position[0]<<" "<<this->position[1]<<" "<<-this->position[2]<<" )\n";
if(this->matrix3x3!=NULL)
*(out) << "		Matrix3x3 ( "<<this->matrix3x3[0]<<" "<<this->matrix3x3[1]<<" "<<this->matrix3x3[2]<<" "<<this->matrix3x3[3]<<" "<<this->matrix3x3[4]<<" "<<this->matrix3x3[5]<<" "<<this->matrix3x3[6]<<" "<<this->matrix3x3[7]<<" "<<this->matrix3x3[8]<<" )\n";
else                        
*(out) << "		QDirection ( "<<this->qDirection[0]<<" "<<this->qDirection[1]<<" "<<-this->qDirection[2]<<" "<<this->qDirection[3]<<" )\n";
*(out) << "		VDbId ( "<<this->vDbId<<" )\n";
if(this->staticDetailLevel > -1)
*(out) << "		StaticDetailLevel ( "<<this->staticDetailLevel<<" )\n";
*(out) << "	)\n";
}
