/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <QHash>
#include <QSet>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/ErrorMessage.h>
#include <tsre/shape/ShapeLod.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLights.h>
#include <tsre/trains/CarLights.h>
#include <tsre/map/MapFeatures.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/shape/ComplexShape.h>
#include <QDebug>
#include <QFile>
#include <algorithm>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/Ruch.h>
#include <tsre/sound/SoundBank.h>
#include <tsre/sound/SoundSource.h>
#include <tsre/sound/SoundSystem.h>
#include <tsre/sound/TrackSound.h>
#include <tsre/trains/PlayDriver.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/trains/TrainNetworkEng.h>
#include <tsre/geo/GeoCoordinates.h>
#include <shapeViewer/ContentHierarchyInfo.h>
#include <tsre/world/ObjectBorders.h>
#include <QElapsedTimer>
#include <tsre/trains/CarPlacement.h>
#include <QFileInfo>
#include <cmath>

namespace {

// The way along the track from an end of a car toward its rear (as its
// consist lists it), relative to `at` (the end's place, getCurrentPosition)
// in at's tile: a place half a metre on along the end's node (the other way,
// reversed, near the node's end); false where the node cannot tell.
bool towardRear(const Ruch &end, const float *at, float *out) {
    const float step = 0.5f;
    float distance = end.nodeDistance() - step * end.nodeWay();
    float sign = 1.0f;
    if (distance < 0.0f || distance > end.nodeSpan()) {
        distance = end.nodeDistance() + step * end.nodeWay();
        sign = -1.0f;
    }
    float p[8];
    if (distance < 0.0f || distance > end.nodeSpan()
            || !Game::trackDB->getDrawPositionOnTrNode(p, end.nodeIndex(), distance))
        return false;
    out[0] = sign * (p[0] + 2048 * (p[5] - at[5]) - at[0]);
    out[1] = sign * (p[1] - at[1]);
    out[2] = sign * (p[2] + 2048 * (p[6] - at[6]) - at[2]);
    const float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (length < 1e-4f)
        return false;
    for (int i = 0; i < 3; ++i)
        out[i] /= length;
    return true;
}

QElapsedTimer &animationTimer() {
    static QElapsedTimer timer;
    if (!timer.isValid())
        timer.start();
    return timer;
}

}

Eng::Eng() {
    
}

Eng::Eng(Eng *o) {
    // Its file's data shared, not copied (task driving 01).
    definition = o->definition;
    name = o->name;
    path = o->path;
    pathid = o->pathid;
    hashid = o->hashid;
    flip = o->flip;
    loaded = o->loaded;
    kierunek = o->kierunek;
    ref = o->ref;
}

Eng::~Eng() {
    if (posedShape != nullptr)
        posedShape->releaseState(posedState);
    delete carAnimation;
    delete carLights;
    delete ruch1;
    delete ruch2;
    delete cabSound;
    delete outsideSound;
    delete trackSound;
    delete soundData;
    delete driver;
}

float Eng::soundRegion(bool ahead, int *trackType) {
    if (ruch1 == NULL || Game::trackDB == NULL)
        return -1;
    auto isRegion = [](int id) {
        TRitem *item = Game::trackDB->trackItems[id];
        return item != NULL && item->type == "soundregionitem" && item->trItemSRData != NULL;
    };
    int id = -1;
    const float distance = ruch1->distanceToItem(ahead, 8192, isRegion, &id);
    if (distance >= 0 && trackType != NULL)
        *trackType = (int)Game::trackDB->trackItems[id]->trItemSRData[1];
    return distance;
}

float Eng::soundThrottle() const {
    return driver != NULL ? driver->throttle() : 0;
}

float Eng::soundBrake() const {
    return driver != NULL ? driver->brake() : 1;
}

void Eng::updateSounds(bool lead, int trackType, float speed, float move, float throttle, float brake,
                       float cutoff, float seconds) {
    if (ruch1 == NULL || ruch2 == NULL)
        return;
    if (soundData == NULL)
        soundData = new SoundValues();
    SoundValues &values = *soundData;
    const float absSpeed = fabs(speed);
    values.set(Sms::Control::Speed, absSpeed);
    values.distanceTravelled += fabs(move);
    values.set(Sms::Control::WheelRpm, TrainMotion::wheelRpm(absSpeed, definition->wheelRadius > 0 ? definition->wheelRadius : 0.45f));
    // Full brake as a full service application, 50 psi.
    values.set(Sms::Control::BrakeCyl, brake * 50.0f);
    // Curve squeal: smoothed over a second, as the wheels settle.
    const float radius = ruch1->curveRadius();
    const float kilograms = std::isfinite(definition->massKg) && definition->massKg > 0 ? float(definition->massKg) : 50000.0f;
    const float k = std::min(seconds, 1.0f);
    curveForceHeard += (TrainMotion::curveForce(kilograms, absSpeed, radius) - curveForceHeard) * k;
    angleOfAttackHeard += (TrainMotion::angleOfAttack(radius) - angleOfAttackHeard) * k;
    values.set(Sms::Control::CurveForce, curveForceHeard);
    values.set(Sms::Control::AngleOfAttack, angleOfAttackHeard);
    // Engines: Variable1-3 as Open Rails fills them for the engine type,
    // from the stand-in driver's throttle.
    if (definition->type.toLower() == "engine") {
        const QString kind = definition->engType.toLower();
        float variable1 = 0;
        float variable2Target = 0;
        float variable2Rate = 1;
        if (kind.contains("diesel")) {
            variable1 = throttle;
            variable2Target = throttle;
            variable2Rate = 0.2f;
        } else if (kind.contains("electric")) {
            variable1 = throttle * 100;
            variable2Target = throttle * 100;
            variable2Rate = 100;
        } else if (kind.contains("steam")) {
            const float driving = definition->driverWheelRadius > 0 ? definition->driverWheelRadius : 0.9f;
            variable1 = absSpeed / driving / float(M_PI) * 5;
            // Cylinder cocks open while starting.
            variable2Target = throttle > 0 && absSpeed < 3 ? 70 : 0;
            variable2Rate = 1000;
        }
        const float step = variable2Rate * seconds;
        variable2Heard += std::clamp(variable2Target - variable2Heard, -step, step);
        values.set(Sms::Control::Variable1, variable1);
        values.variable1.assign(1, variable1);
        values.set(Sms::Control::Variable2, variable2Heard);
        values.set(Sms::Control::Variable3, 0);
    }

    // The car's middle.
    float *front = ruch1->getCurrentPosition();
    const int tileX = (int)front[5];
    const int tileZ = (int)front[6];
    const float x1 = front[0], y1 = front[1], z1 = front[2];
    float *back = ruch2->getCurrentPosition();
    const float position[3] = {(x1 + back[0] + 2048 * (back[5] - tileX)) / 2, (y1 + back[1]) / 2,
                               -(z1 + back[2] + 2048 * (back[6] - tileZ)) / 2};

    if (!outsideSoundTried) {
        outsideSoundTried = true;
        const QString sms = definition->soundPath;
        if (!sms.isEmpty())
            outsideSound = new SoundSource(sms, SoundSource::Kind::Car, this);
    }
    if (lead && !cabSoundTried) {
        cabSoundTried = true;
        const QString sms = definition->cabSoundPath;
        if (!sms.isEmpty())
            cabSound = new SoundSource(sms, SoundSource::Kind::Car, this);
    }
    for (SoundSource *source : {outsideSound, cabSound}) {
        if (source == NULL)
            continue;
        source->values() = values;
        source->setLocation(tileX, -tileZ, position);
        source->setCutoffDistance(cutoff);
    }
    if (trackSound == NULL)
        trackSound = new TrackSound(this, lead);
    trackSound->setType(trackType);
    trackSound->update(values, cutoff, tileX, -tileZ, position);
}

void Eng::silenceSounds() {
    delete cabSound;
    cabSound = NULL;
    cabSoundTried = false;
    delete outsideSound;
    outsideSound = NULL;
    outsideSoundTried = false;
    delete trackSound;
    trackSound = NULL;
}

const SoundValues *Eng::soundValues() {
    return soundData;
}

Eng::Eng(QString p, QString n) {
    pathid = p + "/" + n;
    pathid = ContentPath::normalize(pathid);
    hashid = ContentPath::key(pathid);
    path = p;
    name = n;
    flip = 0;
    loaded = -1;
    kierunek = false;
    load();
}

Eng::Eng(QString src, QString p, QString n) {
    pathid = src;
    pathid = ContentPath::normalize(pathid);
    hashid = ContentPath::key(pathid);
    path = p;
    name = n;
    flip = 0;
    loaded = -1;
    kierunek = false;
    load();
}

void Eng::load(){
    // As Open Rails reads it, its OPENRAILS copy where the setting asks
    // (task driving 01).
    definition = VehicleDefinition::read(path, name, Game::root,
                                         Settings::boolean("core.content.loading.preferOpenRailsEng"));
    // Its problems in the editor's error list, each once (a shared
    // include's are found again by every vehicle including it).
    static QSet<QString> reported;
    const auto report = [](const SimisDocument::Problem &p) {
        const QString where = p.line > 0 ? QString("%1:%2").arg(p.file).arg(p.line) : p.file;
        const QString text = QString("Rolling stock %1: %2").arg(where, p.message);
        if (reported.contains(text))
            return;
        reported.insert(text);
        ErrorMessagesLib::PushErrorMessage(new ErrorMessage(ErrorMessage::Type_Warning, ErrorMessage::Source_Other, text));
    };
    for (const SimisDocument::Problem &p : definition->document->problems())
        report(p);
    for (const SimisDocument::Problem &p : definition->problems)
        report(p);
    if (!definition->ok()) {
        qDebug() << pathid << "not exist";
        return;
    }
    loaded = 1;
}

bool Eng::engFilter(QString q){
    if(q == "") return true;
    if(q == "electric" && definition->wagonTypeId == 4)
        return true;
    if(q == "diesel" && definition->wagonTypeId == 5)
        return true;
    if(q == "steam" && definition->wagonTypeId == 6)
        return true;    
    if(q == "carriage" && definition->wagonTypeId == 1)
        return true;
    if(q == "freight" && definition->wagonTypeId == 2)
        return true;
    if(q == "tender" && definition->wagonTypeId == 3)
        return true;   
    return false;
}

bool Eng::couplingFilter(QString q){
    if(q == "") return true;
    for(int i = 0; i < definition->couplings.size(); i++){
       if(q.toLower() == definition->couplings[i].type.toLower()) return true; 
    }
    return false;
}
bool Eng::searchFilter(QString q){
    if(q == "") return true;
    if(definition->displayName.contains(q, Qt::CaseInsensitive)) return true;
    if(definition->engName.contains(q, Qt::CaseInsensitive)) return true;
    if(definition->searchKeywords.contains(q, Qt::CaseInsensitive)) return true;
    return false;
}

QString Eng::getCouplingsName(){
    QString name;
    for(int i = 0; i < definition->couplings.size(); i++){
       if(i != 0) name += " , ";
       name += definition->couplings[i].type; 
    }
    return name;
}

float Eng::getFullWidth(){
    return definition->length();
}

void Eng::select(){
    selected = true;
}

void Eng::unselect(){
    selected = false;
}

bool Eng::isSelected(){
    return selected;
}

void Eng::initBorder(){
    if (borderObj == NULL) {
        borderObj = new OglObj();
        
        float width = this->getFullWidth() / 2;
        
        float* punkty = new float[24];
        float* ptr = punkty;
        
        *ptr++ = 0;
        *ptr++ = 0;
        *ptr++ = -width;
        *ptr++ = 0;
        *ptr++ = 0;
        *ptr++ = width;
        *ptr++ = 0;
        *ptr++ = 6;
        *ptr++ = -width;
        *ptr++ = 0;
        *ptr++ = 6;
        *ptr++ = width;
        
        *ptr++ = 0;
        *ptr++ = 0;
        *ptr++ = -width;
        *ptr++ = 0;
        *ptr++ = 6;
        *ptr++ = -width;
        *ptr++ = 0;
        *ptr++ = 0;
        *ptr++ = width;
        *ptr++ = 0;
        *ptr++ = 6;
        *ptr++ = width;
        borderObj->setMaterial(1.0, 0.0, 0.0);
        borderObj->init(punkty, ptr-punkty, RenderItem::V, GL_LINES);
        delete[] punkty;
    }
}

void Eng::pushDrawBorder(RenderQueue &queue){
    initBorder();
    borderObj->pushRenderItem(queue);
}

void Eng::pushDrawBorder3d(RenderQueue &queue) {
    if (!ObjectBorders::shown)
        return;
    if (borderObj3d == NULL) {
        borderObj3d = new OglObj();

        float width = this->getFullWidth() / 2;

        float* punkty = new float[96];
        float* ptr = punkty;

        float sizexx = definition->size[0] / 2.0;
        for (int j = 0; j <= 1; j++)
            for (int i = -1; i <= 1; i += 2) {
                *ptr++ = sizexx*i;
                *ptr++ = definition->size[1]*j;
                *ptr++ = -width;
                *ptr++ = sizexx*i;
                *ptr++ = definition->size[1]*j;
                *ptr++ = width;
            }
        for (int j = 0; j <= 1; j++)
            for (int i = -1; i <= 1; i += 2) {
                *ptr++ = sizexx;
                *ptr++ = definition->size[1]*j;
                *ptr++ = width*i;
                *ptr++ = -sizexx;
                *ptr++ = definition->size[1]*j;
                *ptr++ = width*i;
            }
        for (int j = -1; j <= 1; j += 2)
            for (int i = -1; i <= 1; i += 2) {
                *ptr++ = sizexx*j;
                *ptr++ = 0;
                *ptr++ = width*i;
                *ptr++ = sizexx*j;
                *ptr++ = definition->size[1];
                *ptr++ = width*i;
            }
        borderObj3d->setMaterial(1.0, 0.0, 0.0);
        borderObj3d->init(punkty, ptr - punkty, RenderItem::V, GL_LINES);
        delete[] punkty;
    }

    borderObj3d->pushRenderItem(queue);
}

void Eng::updateSim(float deltaTime){
    // A stand-in driver until TSRE simulates trains; throttle and brake
    // follow from its speed. Choose its mode here: Sine runs and stands in
    // turn (22 m/s, a 160 s period), Constant runs at 40 m/s from the start
    // (the old test drive). TSRE_PLAY_TOP_SPEED (m/s) and TSRE_PLAY_PERIOD
    // (s) override the speed and period.
    if(driver == NULL){
        const PlayDriver::Mode mode = PlayDriver::Mode::Sine;
        const float defaultSpeed = mode == PlayDriver::Mode::Constant ? 40 : 22;
        const float top = qEnvironmentVariableIsSet("TSRE_PLAY_TOP_SPEED") ? qEnvironmentVariable("TSRE_PLAY_TOP_SPEED").toFloat() : defaultSpeed;
        const float period = qEnvironmentVariableIsSet("TSRE_PLAY_PERIOD") ? qEnvironmentVariable("TSRE_PLAY_PERIOD").toFloat() : 160;
        driver = new PlayDriver(mode, top, period);
    }
    if(!Game::useNetworkEng){
        driver->run(deltaTime);
        currentSpeed = driver->speed();
    }
    if(Game::useNetworkEng) {
        static IghCoordinate* igh = new IghCoordinate();
        static LatitudeLongitudeCoordinate* latlon = new LatitudeLongitudeCoordinate();
        static PreciseTileCoordinate* coords = new PreciseTileCoordinate();
        if(networkEng == NULL){
            networkEng = new TrainNetworkEng();
        }
        currentSpeed = networkEng->getSpeed();
        driver->follow(currentSpeed, deltaTime);
        
        float data[4];
        data[0] = getCurrentElevation();
        if(isBroken())
            data[0] = 666;
        data[1] = getTotalDistanceDownPath();
        float cpos[8];
        getCameraPosition((float*)cpos);
        coords->setTWxyz(cpos[0], -cpos[1], cpos[2], cpos[3], cpos[4]);
        Game::GeoCoordConverter->ConvertToInternal(coords, igh);
        Game::GeoCoordConverter->ConvertToLatLon(igh, latlon);
        data[2] = latlon->Latitude;
        data[3] = latlon->Longitude;
        networkEng->writeData(data);
    } 
    
    if(Game::soundEnabled && SoundSystem::running()){
        if(ruch1->onJunction > 0){
            // A click at junctions, if the engine has one.
            static QHash<QString, bool> clicks;
            const QString click = path + "/j1.wav";
            if(!clicks.contains(click))
                clicks.insert(click, QFile::exists(click));
            if(clicks.value(click)){
                float *pos = ruch1->getCurrentPosition();
                const float at[3] = {pos[0], pos[1], -pos[2]};
                SoundSystem::playOnce(click, (int)pos[5], -(int)pos[6], at);
            }
            ruch1->onJunction = 0;
        }
    }
}

const EngLights *Eng::getLights() {
    return definition->lights != nullptr ? definition->lights->lights() : nullptr;
}

float Eng::getCurrentSpeed(){
    return currentSpeed;
}

// Adds this engine's shapes to the current shape library once and returns
// the library key of the shape ids.
long long int Eng::resolveShapeIds() {
    long long int shapeLibId = reinterpret_cast<long long int>(Game::currentShapeLib);
    if (!shapeIds.contains(shapeLibId)) {
        if (definition->shape.length() > 1)
            shapeIds[shapeLibId] = Game::currentShapeLib->addShape(definition->shapePath, path);
        else
            shapeIds[shapeLibId] = -1;
    }

    freightShapeIds.resize(definition->freightAnims.size());
    for (int i = 0; i < freightShapeIds.size(); i++) {
        if (!freightShapeIds[i].contains(shapeLibId)) {
            if (definition->freightAnims[i].shape.length() > 1)
                freightShapeIds[i][shapeLibId] = Game::currentShapeLib->addShape(definition->freightAnims[i].path, path);
            else
                freightShapeIds[i][shapeLibId] = -1;
        }
    }
    return shapeLibId;
}

// Submits the engine for the Shape Viewer and Consist Editor.
void Eng::pushRenderItems(RenderQueue &queue, quint32 selectionId) {
    if (loaded != 1) return;
    const long long int shapeLibId = resolveShapeIds();
    if (shapeIds[shapeLibId] >= 0) {
        ComplexShape *carShape = Game::currentShapeLib->shape[shapeIds[shapeLibId]];
        carShape->pushRenderItem(queue, selectionId, poseShape(carShape, nullptr));
    }

    float *mv = queue.transform();
    for (int i = 0; i < freightShapeIds.size(); i++) {
        if (freightShapeIds[i][shapeLibId] < 0)
            continue;
        queue.pushTransform();
        Mat4::translate(mv, mv, -definition->freightAnims[i].offset[0], definition->freightAnims[i].offset[1], -definition->freightAnims[i].offset[2]);
        Game::currentShapeLib->shape[freightShapeIds[i][shapeLibId]]->pushRenderItem(queue, selectionId, 0);
        queue.popTransform();
    }
}

void Eng::reload(){
    // Its file read again into a new definition (cars made from it before
    // keep theirs; files not changed are not read again), and what this car
    // made from the old one dropped: its lights, its posed parts, sounds.
    load();
    delete carLights;
    carLights = NULL;
    if (posedShape != nullptr)
        posedShape->releaseState(posedState);
    posedShape = nullptr;
    delete carAnimation;
    carAnimation = nullptr;
    placedKnown = false;
    silenceSounds();
    if (Game::currentShapeLib == nullptr)
        return;
    // Its shapes named again (a file name may have changed), then read
    // again from their files.
    const long long int shapeLibId = reinterpret_cast<long long int>(Game::currentShapeLib);
    shapeIds.remove(shapeLibId);
    for (auto &ids : freightShapeIds)
        ids.remove(shapeLibId);
    cab3DShapeIds.remove(shapeLibId);
    resolveShapeIds();
    if (shapeIds.value(shapeLibId, -1) >= 0)
        Game::currentShapeLib->shape[shapeIds[shapeLibId]]->reload();
    for (const auto &ids : freightShapeIds)
        if (ids.value(shapeLibId, -1) >= 0)
            Game::currentShapeLib->shape[ids[shapeLibId]]->reload();
}

bool Eng::isBroken(){
    QString name = ruch1->getLastItemName();
    if(name == "E_STOP"){
        return true;
    }
    return false;
}

float *Eng::getCurrentPositionOnTrack(){
    if (loaded != 1) return NULL;
    return ruch1->getCurrentPosition();
}

QString Eng::cab3DShapePath() const {
    return definition->cab3DShapePath;
}

int Eng::cab3DViewpoint(bool rear) const {
    if (definition->cab3D == nullptr)
        return -1;
    for (int i = 0; i < definition->cab3D->viewpoints.size(); ++i) {
        const float yaw = std::fabs(std::remainder(definition->cab3D->viewpoints[i].start[1], 360.0f));
        if ((yaw > 90.0f) == rear)
            return i;
    }
    return -1;
}

void Eng::cab3DCameraPosition(int viewpoint, float *out) {
    getCameraPosition(out);
    if (out == NULL || loaded != 1 || ruch1 == NULL || ruch2 == NULL || definition->cab3D == nullptr
            || viewpoint < 0 || viewpoint >= definition->cab3D->viewpoints.size())
        return;
    float selev1, selev2;
    float front[7], rear[7];
    const float *first = ruch1->getCurrentPosition(&selev1);
    std::copy(first, first + 7, front);
    const float *second = ruch2->getCurrentPosition(&selev2);
    std::copy(second, second + 7, rear);
    const float roll = (selev1 + selev2) / 2.0;
    // As its body is drawn: on its bogies once its animation knows them
    // (task shapes 12), its frame turned and tilted with that body.
    float placedFront[7], placedRear[7];
    std::vector<float> bogieTurns;
    if (placeOnCarriers(front, rear, roll, placedFront, placedRear, bogieTurns)) {
        std::copy(placedFront, placedFront + 7, front);
        std::copy(placedRear, placedRear + 7, rear);
        const float dx = front[0] - (rear[0] + 2048 * (rear[5] - front[5]));
        const float dy = front[1] - rear[1];
        const float dz = front[2] - (rear[2] + 2048 * (rear[6] - front[6]));
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
        // As getCameraPosition turns it.
        const int side = int((dz + 0.00001f) / std::fabs(dz + 0.00001f));
        const float rotY = (float(side) + 1.0f) * float(M_PI / 2.0) + std::atan(dx / dz);
        out[5] = -rotY + float(M_PI);
        out[6] = length > 0.0f ? std::asin(dy / length) : 0.0f;
    }
    // Relative to the front's tile, as getCameraPosition gives it.
    const float tile[2] = {front[5], -front[6]};
    float car[16];
    Mat4::identity(car);
    CarPlacement::placeCar(car, tile, front, rear, roll, flip);
    // As a point of its shape, whose root mirrors x.
    const float *head = definition->cab3D->viewpoints[viewpoint].position;
    float at[3] = {-head[0], head[1], head[2]};
    Vec3::transformMat4(at, at, car);
    std::copy(at, at + 3, out + 2);
}

// The head's start direction (Open Rails': degrees down, then round from
// ahead towards the shape's +x) in the riding frame, which has the car's
// shape facing ahead (-z) unless it is flipped.
bool Eng::cab3DMount(int viewpoint, CameraMount &mount) const {
    if (definition->cab3D == nullptr || viewpoint < 0 || viewpoint >= definition->cab3D->viewpoints.size())
        return false;
    const Cab3D::Viewpoint &v = definition->cab3D->viewpoints[viewpoint];
    const float degree = float(M_PI / 180.0);
    mount = CameraMount();
    std::fill(mount.position, mount.position + 3, 0.0f);
    mount.look[0] = (flip ? 0.0f : float(M_PI)) - v.start[1] * degree;
    mount.look[1] = -v.start[0] * degree;
    std::copy(CabMove, CabMove + 3, mount.moveBox);
    mount.moveStep = CabMoveStep;
    mount.turnLimit[0] = v.limit[1] * degree;
    mount.turnLimit[1] = v.limit[0] * degree;
    mount.id = viewpoint + 1;
    return true;
}

ComplexShape *Eng::cab3DShapeOf(long long int shapeLibId) {
    if (!cab3DShapeIds.contains(shapeLibId)) {
        const QString file = cab3DShapePath();
        ShapeLoadOptions options;
        options.insert(QString::fromLatin1(ShapeLoadOption::DoubleSided), true);
        cab3DShapeIds[shapeLibId] = file.isEmpty() ? -1
                : Game::currentShapeLib->addShape(file, QFileInfo(file).path(), options);
    }
    const int id = cab3DShapeIds[shapeLibId];
    return id >= 0 ? Game::currentShapeLib->shape[id] : nullptr;
}

void Eng::getCameraPosition(float* out){
    if(out == NULL)
        return;
    //qDebug() << "get position";
    float selev1, selev2;
    float *drawPosition1 = ruch1->getCurrentPosition(&selev1);
    float *drawPosition2 = ruch2->getCurrentPosition(&selev2);
    selev1 = (selev1 + selev2) / 2.0;
    
    
    float pos[5];
    pos[3] = drawPosition1[5];
    pos[4] = drawPosition1[6];
    drawPosition2[0] += 2048*(drawPosition2[5]-drawPosition1[5]);
    drawPosition2[2] += 2048*(drawPosition2[6]-drawPosition1[6]);
    Vec3::add(pos, drawPosition1, drawPosition2);
    Vec3::scale(pos, pos, 0.5);
    float dlugosc = Vec3::distance(drawPosition1, drawPosition2);
    
    int someval = (((drawPosition1[2]-drawPosition2[2])+0.00001f)/fabs((drawPosition1[2]-drawPosition2[2])+0.00001f));
    float rotY = ((float)someval+1.0)*(M_PI/2.0)+(float)(atan((drawPosition1[0]-drawPosition2[0])/(drawPosition1[2]-drawPosition2[2]))); 
    float rotX = (float)(asin((drawPosition1[1]-drawPosition2[1])/(dlugosc))); 

    out[0] = pos[3];
    out[1] = -pos[4];
    out[2] = drawPosition1[0]; // pos?
    out[3] = drawPosition1[1];
    out[4] = -drawPosition1[2];
    out[5] = -rotY+M_PI;
    out[6] = rotX;
    //out[6] = 0; // distant camera test only
    out[7] = selev1;
    //Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, pos[0] + 2048 * (pos[3] - playerT[0]), pos[1]+0.28, -pos[2] + 2048 * (-pos[4] - playerT[1]));
    //Mat4::rotateY(gluu->mvMatrix, gluu->mvMatrix, -rotY /* + flip*M_PI+ M_PI*/);
    //Mat4::rotateX(gluu->mvMatrix, gluu->mvMatrix, rotX );
    //Mat4::rotateY(gluu->mvMatrix, gluu->mvMatrix, flip*M_PI /*+ M_PI*/);
}

// Submits the wagon at its track position; the selection border is submitted
// inside the wagon transform.
void Eng::getMapFeatures(MapFeatures &features, int tileX, int tileZ) {
    if (loaded != 1 || ruch1 == NULL || ruch2 == NULL)
        return;
    // Positions are rebuilt by each call: the front's copied first.
    float front[7];
    const float *first = ruch1->getCurrentPosition();
    std::copy(first, first + 7, front);
    getMapFeaturesBetween(features, tileX, tileZ, front, ruch2->getCurrentPosition());
}

void Eng::getMapFeaturesBetween(MapFeatures &features, int tileX, int tileZ, const float *front,
                                const float *rear) const {
    if (loaded != 1)
        return;
    // Both ends, relative to the tile.
    float ends[2][2];
    const float *end[2] = {front, rear};
    for (int i = 0; i < 2; i++) {
        const float *p = end[i];
        ends[i][0] = (p[5] - tileX) * 2048 + p[0];
        ends[i][1] = (-p[6] - tileZ) * 2048 - p[2];
    }
    float dx = ends[1][0] - ends[0][0];
    float dz = ends[1][1] - ends[0][1];
    const float span = sqrt(dx * dx + dz * dz);
    if (span > 0.001f) {
        dx /= span;
        dz /= span;
    } else {
        dx = 0;
        dz = 1;
    }
    const float vehicle[MapFeatures::VehicleFloats] = {
        (ends[0][0] + ends[1][0]) * 0.5f, (ends[0][1] + ends[1][1]) * 0.5f, dx, dz,
        definition->size[2] > 0 ? definition->size[2] : span, definition->size[0] > 0 ? definition->size[0] : 3.0f, definition->wagonTypeId >= 4 ? 1.0f : 0.0f};
    features.vehicles.insert(features.vehicles.end(), vehicle, vehicle + MapFeatures::VehicleFloats);
}

void Eng::pushRenderItemOnTrack(RenderQueue &queue, float* playerT, quint32 selectionId, bool selected) {
    if (loaded != 1 || ruch1 == NULL || ruch2 == NULL) return;

    float selev1, selev2;
    float drawPosition1[7];
    const float *first = ruch1->getCurrentPosition(&selev1);
    std::copy(first, first + 7, drawPosition1);
    float drawPosition2[7];
    const float *second = ruch2->getCurrentPosition(&selev2);
    std::copy(second, second + 7, drawPosition2);
    const float roll = (selev1 + selev2) / 2.0;
    // On its bogies (task shapes 12), once its animation knows them.
    float placedFront[7], placedRear[7];
    std::vector<float> bogieTurns;
    if (placeOnCarriers(drawPosition1, drawPosition2, roll, placedFront, placedRear, bogieTurns))
        pushRenderItemBetween(queue, playerT, selectionId, selected, placedFront, placedRear, roll, flip, &bogieTurns);
    else
        pushRenderItemBetween(queue, playerT, selectionId, selected, drawPosition1, drawPosition2, roll, flip);
}

void Eng::setAnimated(CarAnimation::Group group, bool on, bool now) {
    animationWanted[group] = on;
    if (carAnimation != nullptr)
        carAnimation->set(group, on, now);
}

bool Eng::isAnimated(CarAnimation::Group group) const {
    return animationWanted[group];
}

void Eng::setPantograph(int n, bool up) {
    if (n < 1 || n > 2)
        return;
    pantographs[n - 1] = up ? 1 : 0;
    if (carAnimation != nullptr)
        carAnimation->setPantograph(n, up, flip != 0);
}

void Eng::soundEvent(int trigger) {
    if (cabSound != NULL)
        cabSound->event(trigger);
    if (outsideSound != NULL)
        outsideSound->event(trigger);
}

void Eng::setTravelled(double metres, bool reversed) {
    if (trainTravelledKnown) {
        // Along the train's way: toward this car's front (as its consist
        // lists it) unless the train runs reversed; its shape's front the
        // other way when flipped.
        const double gone = (metres - trainTravelled) * (reversed ? -1.0 : 1.0) * (flip ? -1.0 : 1.0);
        carTravelled += gone;
    }
    trainTravelled = metres;
    trainTravelledKnown = true;
}

unsigned int Eng::poseShape(ComplexShape *carShape, const std::vector<float> *bogieTurns) {
    if (carShape == nullptr || !carShape->isLoaded())
        return 0;
    if (carAnimation == nullptr || posedShape != carShape) {
        if (posedShape != nullptr)
            posedShape->releaseState(posedState);
        delete carAnimation;
        posedShape = carShape;
        posedState = carShape->newState();
        carAnimation = new CarAnimation(*carShape, definition->wagonTypeId >= 4, definition->wagonTypeId == 6, definition->wheelRadius, definition->driverWheelRadius);
        for (int g = 0; g < CarAnimation::GroupCount; ++g)
            carAnimation->set(CarAnimation::Group(g), animationWanted[g]);
        for (int n = 1; n <= 2; ++n)
            if (pantographs[n - 1] >= 0)
                carAnimation->setPantograph(n, pantographs[n - 1] > 0, flip != 0);
        // What it found, with TSRE_ANIMATION_LOG set.
        static const bool log = qEnvironmentVariableIsSet("TSRE_ANIMATION_LOG");
        if (log)
            qInfo().noquote() << "animation" << carShape->getPathId() << carAnimation->summary();
        carTravelledSeen = carTravelled;
        animationClock = -1;
        placedKnown = false;
    }
    const qint64 now = animationTimer().elapsed();
    const float dt = animationClock < 0 ? 0.0f : std::clamp(float(now - animationClock) / 1000.0f, 0.0f, 0.5f);
    animationClock = now;
    carAnimation->update(float(carTravelled - carTravelledSeen), dt);
    carTravelledSeen = carTravelled;
    carAnimation->pose(*carShape, posedState, bogieTurns);
    return posedState;
}

bool Eng::placeOnCarriers(const float *front, const float *rear, float roll, float *placedFront, float *placedRear,
                          std::vector<float> &bogieTurns) {
    if (carAnimation == nullptr || carAnimation->carriers().size() < 2 || ruch1 == NULL || ruch2 == NULL
            || posedShape == nullptr)
        return false;
    // Both ends in the front's tile, the way toward the rear at each.
    const float p0[3] = {front[0], front[1], front[2]};
    const float p1[3] = {rear[0] + 2048 * (rear[5] - front[5]), rear[1], rear[2] + 2048 * (rear[6] - front[6])};
    // Placed as last time while its ends and roll are.
    float key[16];
    std::copy(front, front + 7, key);
    std::copy(rear, rear + 7, key + 7);
    key[14] = roll;
    key[15] = flip ? 1.0f : 0.0f;
    if (placedKnown && std::equal(key, key + 16, placedKey)) {
        std::copy(placedEnds, placedEnds + 7, placedFront);
        std::copy(placedEnds + 7, placedEnds + 14, placedRear);
        bogieTurns = placedTurns;
        return placedOk;
    }
    float t0[3], t1[3];
    // Where the track cannot tell its way: the chord's.
    float chord[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    const float chordLength = std::sqrt(chord[0] * chord[0] + chord[1] * chord[1] + chord[2] * chord[2]);
    for (float &c : chord)
        c = chordLength > 0.0f ? c / chordLength : 0.0f;
    if (!towardRear(*ruch1, front, t0))
        std::copy(chord, chord + 3, t0);
    if (!towardRear(*ruch2, rear, t1))
        std::copy(chord, chord + 3, t1);
    std::vector<CarPlacement::Bogie> bogies;
    for (const CarAnimation::Bogie &b : carAnimation->bogies()) {
        bogies.emplace_back();
        bogies.back().z = b.z;
        if (!posedShape->partRest(b.part, bogies.back().rest))
            return false;
    }
    placedOk = CarPlacement::onCarriers(front, p0, t0, p1, t1, roll, flip != 0, carAnimation->carriers(), bogies,
                                        placedFront, placedRear, bogieTurns);
    std::copy(key, key + 16, placedKey);
    std::copy(placedFront, placedFront + 7, placedEnds);
    std::copy(placedRear, placedRear + 7, placedEnds + 7);
    placedTurns = bogieTurns;
    placedKnown = true;
    return placedOk;
}

void Eng::pushRenderItemBetween(RenderQueue &queue, const float *playerT, quint32 selectionId, bool selected,
                                const float *front, const float *rear, float roll, bool flipped,
                                const std::vector<float> *bogieTurns) {
    if (loaded != 1) return;

    const long long int shapeLibId = resolveShapeIds();

    queue.pushTransform();
    CarPlacement::placeCar(queue.transform(), playerT, front, rear, roll, flipped);

    // Its distance level (shapes task 11) as world objects choose theirs,
    // from where its origin is from the camera; the shadow maps take the
    // same, so a near car shadows itself in detail.
    const float *model = queue.transform();
    const float *camera = GLUU::get()->cameraPosition;
    const float dx = model[12] - camera[0], dy = model[13] - camera[1], dz = model[14] - camera[2];
    const ShapeLod::Place place{std::sqrt(dx * dx + dz * dz), dy, {-dx, -dy, -dz}, model};
    // A static consist's car stays in the cached shadow maps and the sky
    // enclosure: its place goes into their sums, so they are drawn again
    // when it is placed, moved or gone.
    if (staticOnTrack) {
        const float placed[16] = {front[0], front[1], front[2], front[5], front[6],
                                  rear[0], rear[1], rear[2], rear[5], rear[6], flipped ? 1.0f : 0.0f,
                                  float(shapeIds[shapeLibId]), roll, carAnimation != nullptr ? 1.0f : 0.0f,
                                  float(cabViewpoint), 0.0f};
        const quint64 hash = quint64(qHashBits(placed, sizeof(placed), 0x9e3779b9U)) * 0x9e3779b97f4a7c15ULL + 1;
        queue.addShadowSignature(hash);
        queue.addSceneSignature(hash);
    }

    // Activities submit their consists with the editor overlays; the
    // rolling stock itself is scene geometry, lit and shadowed. Trains count
    // as animated (they move, their parts animate): they cast on top of the
    // cached static shadow maps (task renderer 37) and are no roofs in the
    // cached sky enclosure maps (task renderer 27); static consists do
    // neither.
    const RenderQueue::Layer layer = queue.layer();
    const bool animated = queue.animatedEnabled();
    queue.setLayer(RenderQueue::LAYER_SCENE);
    queue.setAnimated(!staticOnTrack);
    // Seen from inside its 3D cab, the cab's shape in place of its body and
    // its loads (Open Rails draws the cab alone); the body until the cab has
    // loaded (pushed, a shape starts loading).
    ComplexShape *cab = cabViewpoint >= 0 ? cab3DShapeOf(shapeLibId) : nullptr;
    if (cab != nullptr && !cab->isLoaded()) {
        cab->pushRenderItem(queue, selectionId, 0);
        cab = nullptr;
    }
    if (cab != nullptr) {
        if (ShapeLod::apply(*cab, 0, place))
            cab->pushRenderItem(queue, selectionId, 0);
    } else if (shapeIds[shapeLibId] >= 0) {
        // Its parts posed in its own state (task shapes 12).
        ComplexShape *carShape = Game::currentShapeLib->shape[shapeIds[shapeLibId]];
        const unsigned int state = poseShape(carShape, bogieTurns);
        if (ShapeLod::apply(*carShape, state, place))
            carShape->pushRenderItem(queue, selectionId, state);
    }

    for (int i = 0; i < freightShapeIds.size() && cab == nullptr; i++) {
        if (freightShapeIds[i][shapeLibId] >= 0) {
            queue.pushTransform();
            // Its offset as Open Rails adds it (z mirrored from MSTS's), in
            // the car's shape space (x mirrored: the shapes' mirrored root),
            // as the Shape Viewer has it: x was not mirrored here, and a
            // driver sat outside the cab (the user, 2026-10-28, SA103-013).
            Mat4::translate(queue.transform(), queue.transform(), -definition->freightAnims[i].offset[0], definition->freightAnims[i].offset[1], -definition->freightAnims[i].offset[2]);
            ComplexShape *freight = Game::currentShapeLib->shape[freightShapeIds[i][shapeLibId]];
            if (ShapeLod::apply(*freight, 0, place))
                freight->pushRenderItem(queue, selectionId, 0);
            queue.popTransform();
        }
    }
    // Its lamps (task renderer 34), once its shape shows; from its cab their
    // beams alone.
    const EngLights *carLightsData = getLights();
    if (carLightsData != nullptr && shapeIds[shapeLibId] >= 0
            && Game::currentShapeLib->shape[shapeIds[shapeLibId]]->isLoaded()) {
        if (carLights == nullptr)
            carLights = new CarLights();
        carLights->push(queue, *carLightsData, lightsTrain, definition->size[2] * 0.5f, selectionId, cab == nullptr);
    }
    queue.setLayer(layer);
    queue.setAnimated(animated);
    if (selected && cab == nullptr)
        pushDrawBorder3d(queue);
    queue.popTransform();
}

void Eng::move(float m){
    // Its wheels turned by it (task shapes 12): toward its front as its
    // consist lists it, its shape's the other way when flipped.
    carTravelled += flip ? -m : m;
    if(ruch1 != NULL)
        ruch1->next(m);
    if(ruch2 != NULL)
        ruch2->next(m);
}

void Eng::placeOnTrack(int frontNode, float frontDistance, int frontDirection,
                       int rearNode, float rearDistance, int rearDirection){
    if(ruch1 == NULL)
        ruch1 = new Ruch();
    if(ruch2 == NULL)
        ruch2 = new Ruch();
    ruch1->place(frontNode, frontDistance, frontDirection);
    ruch2->place(rearNode, rearDistance, rearDirection);
}

float Eng::getCurrentElevation(){
    if(ruch1 == NULL)
        return 0;
    if(ruch2 == NULL)
        return 0;
    
    float *drawPosition1 = ruch1->getCurrentPosition();
    float *drawPosition2 = ruch2->getCurrentPosition();
    drawPosition2[0] += 2048*(drawPosition2[5]-drawPosition1[5]);
    drawPosition2[2] += 2048*(drawPosition2[6]-drawPosition1[6]);
    float d = Vec3::distance(drawPosition1, drawPosition2);
    float h = drawPosition1[1] - drawPosition2[1];
    return (h/d)*1000.0;
}

float Eng::getTotalDistanceDownPath(){
    if(ruch1 == NULL)
        return 0;
    return ruch1->getDistanceDownPath();
}

void Eng::initOnTrack(float *tpos, int direction, QMap<int, int>* junctionDirections, float offset){
    TDB* tdb = Game::trackDB;
    
    if(ruch1 == NULL)
        ruch1 = new Ruch();
    ruch1->set(tpos[0], tpos[1], direction, junctionDirections);
    ruch1->trackPassingItems(true);
    if(offset != 0)
        ruch1->next(offset);
    if(ruch2 == NULL)
        ruch2 = new Ruch();
    ruch2->set(tpos[0], tpos[1], direction, junctionDirections);
    ruch2->next(offset - getFullWidth());
    //ruch1->getCurrentPosition();
    
    //qDebug() << drawPosition[0] << drawPosition[1] << drawPosition[2];
    //qDebug() << drawPosition[5] << drawPosition[6];

    //    qDebug() << "fail";
    //    this->loaded = -1;
    this->loaded = 1;
    qDebug() << "ok";
}

void Eng::fillContentHierarchyInfo(QVector<ContentHierarchyInfo*>& list, int parent){
    ContentHierarchyInfo *info = new ContentHierarchyInfo();
    info->parent = parent;
    info->name = name;
    info->eng = this;
    info->type = "eng";
    list.push_back(info);
    parent = list.size()-1;

    long long int shapeLibId = reinterpret_cast<long long int>(Game::currentShapeLib);
    if(shapeIds[shapeLibId] >= 0) 
        if(Game::currentShapeLib->shape[shapeIds[shapeLibId]] != NULL){
            Game::currentShapeLib->shape[shapeIds[shapeLibId]]->fillContentHierarchyInfo(list, list.size()-1);
        }

    for(int i = 0; i < freightShapeIds.size(); i++){
        if(freightShapeIds[i][shapeLibId] >= 0) {
            if(Game::currentShapeLib->shape[freightShapeIds[i][shapeLibId]] != NULL){
                Game::currentShapeLib->shape[freightShapeIds[i][shapeLibId]]->fillContentHierarchyInfo(list, parent);
            }
        }
    }
}
