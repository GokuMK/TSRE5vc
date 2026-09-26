/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/objects/RulerObj.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/world/objects/TrackObj.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/TrackItemObj.h>
#include <math.h>
#include <tsre/fileFunctions/ParserX.h>
#include <QDebug>
#include <tsre/Game.h>
#include <tsre/world/objects/DynTrackObj.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TrackShape.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/world/Route.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/procedural/OrtsTrackProfileRenderer.h>
#include <tsre/procedural/ProceduralShape.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/renderer/SelectionId.h>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <utility>

bool RulerObj::TwoPointRuler = false;
bool RulerObj::DrawPoints = false;

namespace {

float proceduralPartLod(OglObj *object, const float *transform,
        float baseX, float baseZ, float objectOffsetX, float objectOffsetZ) {
    if(object == nullptr)
        return 0;
    float bounds[6];
    if(!object->getSimpleBorder(bounds))
        return std::hypot(baseX + objectOffsetX,
                          baseZ + objectOffsetZ);
    const float localCenter[3] = {
        (bounds[0] + bounds[1]) * 0.5f,
        (bounds[2] + bounds[3]) * 0.5f,
        (bounds[4] + bounds[5]) * 0.5f
    };
    float centerX = localCenter[0];
    float centerZ = localCenter[2];
    if(transform != nullptr){
        centerX = transform[0] * localCenter[0]
                + transform[4] * localCenter[1]
                + transform[8] * localCenter[2] + transform[12];
        centerZ = transform[2] * localCenter[0]
                + transform[6] * localCenter[1]
                + transform[10] * localCenter[2] + transform[14];
    }
    const float halfX = (bounds[0] - bounds[1]) * 0.5f;
    const float halfY = (bounds[2] - bounds[3]) * 0.5f;
    const float halfZ = (bounds[4] - bounds[5]) * 0.5f;
    const float radius = std::sqrt(
            halfX * halfX + halfY * halfY + halfZ * halfZ);
    const float centerDistance = std::hypot(
            baseX + objectOffsetX + centerX,
            baseZ + objectOffsetZ + centerZ);
    return std::max(0.0f, centerDistance - radius);
}

}

RulerObj::RulerObj() {
    this->internalLodControl = true;
    this->shape = -1;
    this->loaded = false;
    this->modified = false;
}

bool RulerObj::allowNew(){
    return true;
}

RulerObj::RulerObj(const RulerObj& o) : WorldObj(o){

    for(int i = 0; i < o.points.size(); i++){
        Point point;
        point.position[0] = o.points[i].position[0];
        point.position[1] = o.points[i].position[1];
        point.position[2] = o.points[i].position[2];
        points.push_back(point);
    }
    selectionValue = o.selectionValue;
    length = o.length;
    internalLodControl = o.internalLodControl;
    shapeEnabled = o.shapeEnabled;
    proceduralShapeInit = false;
}

WorldObj* RulerObj::clone(){
    return new RulerObj(*this);
}

RulerObj::~RulerObj() {
    clearProceduralShape();
}

void RulerObj::load(int x, int y) {
    this->x = x;
    this->y = y;
    this->position[2] = -this->position[2];
    this->qDirection[2] = -this->qDirection[2];
    this->loaded = true;
    this->size = -1;
    this->skipLevel = 1;
    this->box.loaded = false;
    setMartix();
    //this->point3d = new TrackItemObj();
    //this->point3d->setMaterial(1,1,1);
    //this->point3dSelected = new TrackItemObj();
    //this->point3dSelected->setMaterial(0.5,0.5,0.5);

    if(this->points.size() == 0){
        Point point;
        Vec3::copy(point.position, this->position);
        this->points.push_back(point);
        if(TwoPointRuler){
            Point point2;
            Vec3::copy(point2.position, this->position);
            point2.position[2] += 1;
            this->points.push_back(point2);
            selectionValue = 1;
        }
    }
}

void RulerObj::set(QString sh, QString val){
    
    WorldObj::set(sh, val);
    return;
}

void RulerObj::set(QString sh, FileBuffer* data) {
    if (sh == ("points")) {
        int pointCount = ParserX::GetNumber(data);
        for(int i=0; i< pointCount; i++){
            Point point;
            point.position[0] = ParserX::GetNumber(data);
            point.position[1] = ParserX::GetNumber(data);
            point.position[2] = -ParserX::GetNumber(data);
            points.push_back(point);
        }
        ParserX::SkipToken(data);
        return;
    }
    if (sh == ("shapetemplate")) {
        shapeEnabled = true;
        templateName = ParserX::GetStringInside(data);
        return;
    }
    
    WorldObj::set(sh, data);
    return;
}

void RulerObj::reload(){
    clearProceduralShape();
}

void RulerObj::clearProceduralShape(){
    for(Point &point : points){
        if(point.procShapeOwned){
            for(OglObj *object : point.procShape){
                object->deleteVBO();
                delete object;
            }
        }
        point.procShape.clear();
        point.procShapeOwned = false;
    }
    for(ProceduralInstance &instance : proceduralInstances){
        if(instance.object != nullptr){
            instance.object->deleteVBO();
            delete instance.object;
            instance.object = nullptr;
        }
    }
    proceduralInstances.clear();
    proceduralShapeInit = false;
}

void RulerObj::ensureProceduralShape(){
    if(proceduralShapeInit || !shapeEnabled || points.size() < 2)
        return;

    clearProceduralShape();
    ProceduralShape::Load();
    const QString routePath = Game::root + "/ROUTES/" + Game::route;
    OrtsTrackProfileCatalog::load(routePath);
    const QSharedPointer<const OrtsTrackProfile> routeProfile =
            OrtsTrackProfileCatalog::find(
                templateName, OrtsTrackProfile::ObjectType::Static);

    if(routeProfile != nullptr){
        QVector<ComplexLinePoint> linePoints;
        linePoints.reserve(points.size());
        for(const Point &point : points){
            ComplexLinePoint linePoint;
            linePoint.position[0] = point.position[0];
            linePoint.position[1] = point.position[1];
            linePoint.position[2] = point.position[2];
            linePoints.append(linePoint);
        }
        ComplexLine line;
        line.init(linePoints);

        // The point-backed ComplexLine is local to the first ruler point and
        // carries every following position and node frame. Render the single
        // generated multiline mesh with translation only; its span frames
        // already contain heading and elevation.
        Quat::fill(points[0].quat);
        Mat4::fromRotationTranslation(
                points[0].matrix, points[0].quat, points[0].position);

        QStringList diagnostics;
        QVector<OrtsGeneratedProfileInstanceObject> generatedInstances;
        OrtsTrackProfileRenderer::generateWithInstances(
                *routeProfile, line, points[0].procShape,
                generatedInstances,
                routePath, &diagnostics, 0);
        proceduralInstances.reserve(generatedInstances.size());
        for(OrtsGeneratedProfileInstanceObject &generated
                : generatedInstances){
            ProceduralInstance instance;
            instance.object = generated.object;
            instance.transforms = std::move(generated.transforms);
            proceduralInstances.append(std::move(instance));
            generated.object = nullptr;
        }
        points[0].procShapeOwned = !points[0].procShape.isEmpty();

        static QSet<QString> warnedDiagnostics;
        for(const QString &diagnostic : diagnostics){
            const QString key = routeProfile->id.toLower()
                    + ":ruler:" + diagnostic;
            if(!warnedDiagnostics.contains(key)){
                warnedDiagnostics.insert(key);
                qWarning() << "ORTS Ruler profile" << routeProfile->id
                           << diagnostic;
            }
        }
        proceduralShapeInit = true;
        return;
    }

    for(int i = 0; i < points.size() - 1; i++){
        const float tlength = Vec3::distance(
                points[i].position, points[i + 1].position);
        const int someval = (((points[i + 1].position[2]
                - points[i].position[2]) + 0.00001f)
                / fabs((points[i + 1].position[2]
                - points[i].position[2]) + 0.00001f));
        const float rotY = ((float)someval + 1.0f) * (M_PI / 2)
                + (float)atan((points[i].position[0]
                - points[i + 1].position[0])
                / (points[i].position[2] - points[i + 1].position[2]));
        const float rotX = (float)asin((points[i].position[1]
                - points[i + 1].position[1]) / tlength);

        Quat::fill(points[i].quat);
        Quat::rotateY(points[i].quat, points[i].quat, rotY + M_PI);
        Quat::rotateX(points[i].quat, points[i].quat, rotX);
        Mat4::fromRotationTranslation(
                points[i].matrix, points[i].quat, points[i].position);

        QVector<TSection> sections;
        sections.push_back(TSection());
        sections.back().size = floor((tlength * 10) + 0.5) / 10;

        ProceduralShape::GetShape(
                templateName, points[i].procShape, sections, i);
    }
    proceduralShapeInit = true;
}

void RulerObj::setTemplate(QString name){
    if(templateName == name && shapeEnabled == !name.isEmpty())
        return;
    templateName = name;
    shapeEnabled = !name.isEmpty();
    setModified();
    reload();
}

void RulerObj::setPosition(int x, int z, float* p){
    if(selectionValue > 0){
        points[selectionValue].position[0] = -2048*(this->x-x) + p[0];
        points[selectionValue].position[1] = p[1];
        points[selectionValue].position[2] = -2048*(this->y-z) + p[2];
    } else {
        Point point;
        point.position[0] = -2048*(this->x-x) + p[0];
        point.position[1] = p[1];
        point.position[2] = -2048*(this->y-z) + p[2];
        if(Vec3::dist(points.back().position, point.position) > 1)
            points.push_back(point);
    }
    setModified();
    if(line3d != NULL)
        line3d->deleteVBO();
    if(shapeEnabled)
        reload();
}

void RulerObj::refreshLength(){
    length = 0;
    geoLength = 0;
    
    IghCoordinate igh1, igh2;
    LatitudeLongitudeCoordinate latlon1, latlon2;
    PreciseTileCoordinate coords1, coords2;
    
    for(int i = 0; i < points.size() - 1; i++){
        length += Vec3::distance(points[i].position, points[i+1].position);
        
        coords1.setTWxyz(x, -y, points[i].position[0], points[i].position[1], points[i].position[2]);
        coords2.setTWxyz(x, -y, points[i+1].position[0], points[i+1].position[1], points[i+1].position[2]);
        Game::GeoCoordConverter->ConvertToInternal(&coords1, &igh1);
        Game::GeoCoordConverter->ConvertToInternal(&coords2, &igh2);
        Game::GeoCoordConverter->ConvertToLatLon(&igh1, &latlon1);
        Game::GeoCoordConverter->ConvertToLatLon(&igh2, &latlon2);
        geoLength += latlon1.distanceTo(&latlon2);
        
    }
    
    
}

float RulerObj::getElevation(){
    if(points.size() < 2)
        return 0;
    float height = points[points.size()-1].position[1] - points[0].position[1];
    return asin(height/length);
}

float RulerObj::getLength(){
    return length;
    
}float RulerObj::getGeoLength(){
    return geoLength;
}

void RulerObj::createRoadPaths(){
    float tlength = 0;
    DynTrackObj* dobj = new DynTrackObj();
    dobj->load(x, y);
    float p[3];
    float q[4];
    for(int i = 0; i < points.size() - 1; i++){
        tlength = Vec3::distance(points[i].position, points[i+1].position);
        Vec3::copy(p, points[i].position);
        
        int someval = (((points[i+1].position[2]-points[i].position[2])+0.00001f)/fabs((points[i+1].position[2]-points[i].position[2])+0.00001f));
        float rotY = ((float)someval+1.0)*(M_PI/2)+(float)(atan((points[i].position[0]-points[i+1].position[0])/(points[i].position[2]-points[i+1].position[2]))); 
        float rotX = -(float)(asin((points[i].position[1]-points[i+1].position[1])/(tlength))); 

        Quat::fill(q);
        Quat::rotateY(q, q, rotY);
        Quat::rotateX(q, q, rotX);

        dobj->sections[0].a = floor((tlength * 10 ) + 0.5) / 10;
        Game::roadDB->fillDynTrack(dobj);
        Game::roadDB->placeTrack(x, y, (float*) &p, (float*) &q, dobj->sectionIdx, UiD);
        
    }
}

void RulerObj::removeRoadPaths(){
    bool ok;
    ok = Game::roadDB->removeTrackFromTDB(x, y, UiD);
    //if(ok)
}

void RulerObj::enableShape(){
    if(points.size() < 2)
        return;
    
    shapeEnabled = true;
    setModified();

}

void RulerObj::render(GLUU* gluu, float lod, float posx, float posz, float* pos, float* target, float fov, quint32 selectionId, int renderMode) {
    if (!loaded) return;
    if (jestPQ < 2) return;
    
    if(Game::showWorldObjPivotPoints){
        if(pointer3d == NULL){
            pointer3d = new TrackItemObj(1);
            pointer3d->setMaterial(0.9,0.9,0.7);
        }
        pointer3d->render(selectionId);
    }


    if(shapeEnabled){
        ensureProceduralShape();
        for(int j = 0; j < points.size() - 1; j++){
            gluu->mvPushMatrix();
            Mat4::multiply(gluu->mvMatrix, gluu->mvMatrix, points[j].matrix);
            gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->mvMatrix));
            for(int i = 0; i < points[j].procShape.size(); i++){
                const float partLod = proceduralPartLod(
                        points[j].procShape[i], nullptr, posx, posz,
                        points[j].position[0] - position[0],
                        points[j].position[2] - position[2]);
                points[j].procShape[i]->render(
                        SelectionIdCodec::withPart(selectionId, i), partLod);
            }
            gluu->mvPopMatrix();
        }
        if(!proceduralInstances.isEmpty()){
            gluu->mvPushMatrix();
            Mat4::multiply(gluu->mvMatrix, gluu->mvMatrix, points[0].matrix);
            int part = points[0].procShape.size();
            for(const ProceduralInstance &instance
                    : proceduralInstances){
                if(instance.object == nullptr)
                    continue;
                for(const std::array<float, 16> &transform
                        : instance.transforms){
                    gluu->mvPushMatrix();
                    Mat4::multiply(gluu->mvMatrix, gluu->mvMatrix,
                                   const_cast<float*>(transform.data()));
                    gluu->currentShader->setUniformValue(
                            gluu->currentShader->mvMatrixUniform,
                            *reinterpret_cast<float(*)[4][4]>(gluu->mvMatrix));
                    const float partLod = proceduralPartLod(
                            instance.object, transform.data(), posx, posz,
                            points[0].position[0] - position[0],
                            points[0].position[2] - position[2]);
                    instance.object->render(
                            SelectionIdCodec::withPart(selectionId, part++),
                            partLod);
                    gluu->mvPopMatrix();
                }
            }
            gluu->mvPopMatrix();
        }
    }
    
    if (renderMode == GLUU::RENDER_SHADOWMAP) return;
    if(!Game::viewInteractives) 
        return;
    
    if(point3d == NULL){
        point3d = new OglObj();
        point3d->setMaterial(1,1,1);
        point3d->setLineWidth(8);
        float *punkty = new float[6];
        int ptr = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 10;
        punkty[ptr++] = 0;
        point3d->init(punkty, ptr, RenderItem::V, GL_LINES);
        
        point3dSelected = new OglObj();
        point3dSelected->setLineWidth(8);
        point3dSelected->setMaterial(0.5,0.5,0.5);
        ptr = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 10;
        punkty[ptr++] = 0;
        point3dSelected->init(punkty, ptr, RenderItem::V, GL_LINES);
        delete[] punkty;
    }
    if(line3d == NULL){
        line3d = new OglObj();
        line3d->setLineWidth(2);
        line3d->setMaterial(1,1,1);
    }
    if(!line3d->loaded){
        float *punkty = new float[points.size()*6*2]; 
        int ptr = 0;
        
        for(int i = 0; i < points.size() - 1; i++){
            punkty[ptr++] = points[i].position[0];
            punkty[ptr++] = points[i].position[1]+1;
            punkty[ptr++] = points[i].position[2];
            punkty[ptr++] = points[i+1].position[0];
            punkty[ptr++] = points[i+1].position[1]+1;
            punkty[ptr++] = points[i+1].position[2];
        }
        line3d->init(punkty, ptr, RenderItem::V, GL_LINES);
        delete[] punkty;
        refreshLength();
    }
    
    gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->mvMatrix));
    line3d->render(selectionId);
    
    for(int i = 0; i < points.size(); i++){
        gluu->mvPushMatrix();
        Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, points[i].position[0], points[i].position[1], points[i].position[2]);
        gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->mvMatrix));
        if(i == 0 || i == points.size() - 1 || DrawPoints){
            if(this->selected && this->selectionValue == i) 
                point3dSelected->render(SelectionIdCodec::withPart(selectionId, i));
            else
                point3d->render(SelectionIdCodec::withPart(selectionId, i));
        }
        gluu->mvPopMatrix();
    }
};

void RulerObj::pushRenderItems(float lod, float posx, float posz, float* playerW, float* target, float fov, quint32 selectionId) {
    if (!loaded)
        return;
    if (jestPQ < 2)
        return;
    if (Game::currentRenderer == NULL)
        return;

    if(Game::showWorldObjPivotPoints){
        if(pointer3d == NULL){
            pointer3d = new TrackItemObj(1);
            pointer3d->setMaterial(0.9,0.9,0.7);
        }
        pointer3d->pushRenderItem(selectionId);
    }


    if(shapeEnabled){
        ensureProceduralShape();
        for(int j = 0; j < points.size() - 1; j++){
            Game::currentRenderer->mvPushMatrix();
            Mat4::multiply(Game::currentRenderer->mvMatrix, Game::currentRenderer->mvMatrix, points[j].matrix);
            for(int i = 0; i < points[j].procShape.size(); i++){
                const float partLod = proceduralPartLod(
                        points[j].procShape[i], nullptr, posx, posz,
                        points[j].position[0] - position[0],
                        points[j].position[2] - position[2]);
                points[j].procShape[i]->pushRenderItem(
                        SelectionIdCodec::withPart(selectionId, i), partLod);
            }
            Game::currentRenderer->mvPopMatrix();
        }
        if(!proceduralInstances.isEmpty()){
            Game::currentRenderer->mvPushMatrix();
            Mat4::multiply(Game::currentRenderer->mvMatrix,
                    Game::currentRenderer->mvMatrix, points[0].matrix);
            int part = points[0].procShape.size();
            for(const ProceduralInstance &instance
                    : proceduralInstances){
                if(instance.object == nullptr)
                    continue;
                for(const std::array<float, 16> &transform
                        : instance.transforms){
                    Game::currentRenderer->mvPushMatrix();
                    Mat4::multiply(Game::currentRenderer->mvMatrix,
                            Game::currentRenderer->mvMatrix,
                            const_cast<float*>(transform.data()));
                    const float partLod = proceduralPartLod(
                            instance.object, transform.data(), posx, posz,
                            points[0].position[0] - position[0],
                            points[0].position[2] - position[2]);
                    instance.object->pushRenderItem(
                            SelectionIdCodec::withPart(selectionId, part++),
                            partLod);
                    Game::currentRenderer->mvPopMatrix();
                }
            }
            Game::currentRenderer->mvPopMatrix();
        }
    }

    if(!Game::viewInteractives)
        return;

    if(point3d == NULL){
        point3d = new OglObj();
        point3d->setMaterial(1,1,1);
        point3d->setLineWidth(8);
        float *punkty = new float[6];
        int ptr = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 10;
        punkty[ptr++] = 0;
        point3d->init(punkty, ptr, RenderItem::V, GL_LINES);

        point3dSelected = new OglObj();
        point3dSelected->setLineWidth(8);
        point3dSelected->setMaterial(0.5,0.5,0.5);
        ptr = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 0;
        punkty[ptr++] = 10;
        punkty[ptr++] = 0;
        point3dSelected->init(punkty, ptr, RenderItem::V, GL_LINES);
        delete[] punkty;
    }
    if(line3d == NULL){
        line3d = new OglObj();
        line3d->setLineWidth(2);
        line3d->setMaterial(1,1,1);
    }
    if(!line3d->loaded){
        float *punkty = new float[points.size()*6*2];
        int ptr = 0;

        for(int i = 0; i < points.size() - 1; i++){
            punkty[ptr++] = points[i].position[0];
            punkty[ptr++] = points[i].position[1]+1;
            punkty[ptr++] = points[i].position[2];
            punkty[ptr++] = points[i+1].position[0];
            punkty[ptr++] = points[i+1].position[1]+1;
            punkty[ptr++] = points[i+1].position[2];
        }
        line3d->init(punkty, ptr, RenderItem::V, GL_LINES);
        delete[] punkty;
        refreshLength();
    }

    line3d->pushRenderItem(selectionId);

    for(int i = 0; i < points.size(); i++){
        Game::currentRenderer->mvPushMatrix();
        Mat4::translate(Game::currentRenderer->mvMatrix, Game::currentRenderer->mvMatrix, points[i].position[0], points[i].position[1], points[i].position[2]);
        if(i == 0 || i == points.size() - 1 || DrawPoints){
            if(this->selected && this->selectionValue == i)
                point3dSelected->pushRenderItem(SelectionIdCodec::withPart(selectionId, i));
            else
                point3d->pushRenderItem(SelectionIdCodec::withPart(selectionId, i));
        }
        Game::currentRenderer->mvPopMatrix();
    }
}

bool RulerObj::hasLinePoints(){
    return true;
}

void RulerObj::getLinePoints(float *&punkty){
    
    float pos[3];
    for(float i = 0; i < this->length; i += 1){
        getPosition(i, pos);
        *punkty++ = pos[0];
        *punkty++ = pos[1];
        *punkty++ = pos[2];
    }
    //for(int i = 0; i < points.size(); i++){
    //    *punkty++ = points[i].position[0];
    //    *punkty++ = points[i].position[1];
    //    *punkty++ = points[i].position[2];
    //}
    return;
}

void RulerObj::getPosition(float len, float* pos){
    float slen = 0;
    float tlen;
    for(int i = 0; i < points.size()-1; i++){
        tlen = Vec3::dist(points[i].position, points[i+1].position);
        slen += tlen;
        if(slen > len){
            float len1 = len - slen + tlen;
            float len2 = slen - len;
            len1 = len1/tlen;
            len2 = len2/tlen;
            pos[0] = len1*points[i].position[0] + len2*points[i+1].position[0];
            pos[1] = len1*points[i].position[1] + len2*points[i+1].position[1];
            pos[2] = len1*points[i].position[2] + len2*points[i+1].position[2];
            return;
        }
    }
    pos[0] = points[points.size()-1].position[0];
    pos[1] = points[points.size()-1].position[1];
    pos[2] = points[points.size()-1].position[2];
    
}

bool RulerObj::select(int value){
    this->selectionValue = value;
    this->selected = true;
    return true;
}


void RulerObj::save(QTextStream* out){
    if (!loaded) return;
    if (jestPQ < 2) return;
    
*(out) << "	Ruler (\n";

*(out) << "		UiD ( "<<this->UiD<<" )\n";
*(out) << "		Position ( "<<this->position[0]<<" "<<this->position[1]<<" "<<-this->position[2]<<" )\n";
*(out) << "		QDirection ( "<<this->qDirection[0]<<" "<<this->qDirection[1]<<" "<<-this->qDirection[2]<<" "<<this->qDirection[3]<<" )\n";
*(out) << "		Points ( " << points.size()<<" \n";
for(int i = 0; i < points.size(); i++)
*(out) << "			Point ( "<<points[i].position[0]<<" "<<points[i].position[1]<<" "<<-points[i].position[2]<<" )\n";
*(out) << "		)\n";
if(shapeEnabled){
*(out) << "		ShapeTemplate ( "<<ParserX::AddComIfReq(templateName)<<" )\n";
}
*(out) << "	)\n";
}
