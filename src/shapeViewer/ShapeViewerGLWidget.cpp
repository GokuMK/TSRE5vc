/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ShapeViewerGLWidget.h"
#include <QMouseEvent>
#include <QTimer>
#include <QOpenGLShaderProgram>
#include <QCoreApplication>
#include <QDateTime>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <tsre/renderer/EnvironmentMap.h>
#include <QtMath>
#include <math.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/maszyna/MaszynaPaths.h>
#include <shapeViewer/ShapeHierarchyInfo.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>
#include <tsre/Game.h>
#include <tsre/ogl/GLH.h>
#include <tsre/camera/CameraFree.h>
#include <tsre/camera/CameraConsist.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Consist.h> 
#include <tsre/shape/ShapeLib.h>
#include <tsre/shape/GltfShape.h>
#include <tsre/humans/Crowd.h>
#include <tsre/renderer/RenderStats.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/texture/TexLib.h>
#include <shapeViewer/ShapeTextureInfo.h>
#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/renderer/ExhaustParticles.h>
#include <settings/SettingsAccess.h>
#include <settings/SettingsManager.h>
#include <algorithm>

ShapeViewerGLWidget::ShapeViewerGLWidget(QWidget *parent, ShapeLib::MstsBackend backend)
: QWidget(parent),
m_xRot(0),
m_yRot(0),
m_zRot(0) {
    backgroundGlColor[0] = -2;
    currentShapeLib = new ShapeLib(backend);
    surface = RenderSurface::create(this, this);
}

ShapeViewerGLWidget::~ShapeViewerGLWidget() {
    // The surface is destroyed after this destructor has run; it must not
    // call back into a destroyed object.
    surface->detachClient();
    cleanup();
}

void ShapeViewerGLWidget::update() {
    surface->requestUpdate();
}

void ShapeViewerGLWidget::makeCurrent() {
    surface->makeCurrent();
}

void ShapeViewerGLWidget::doneCurrent() {
    surface->doneCurrent();
}

QImage ShapeViewerGLWidget::grabFramebuffer() {
    return surface->grabFramebuffer();
}

void ShapeViewerGLWidget::paintEvent(QPaintEvent *) {
    // The surface fills the widget and paints itself.
}

void ShapeViewerGLWidget::surfaceRelease() {
    cleanup();
}

void ShapeViewerGLWidget::cleanup() {
    makeCurrent();
    releaseDisplays();
    // The environment map's storage belongs to the renderer's backend.
    delete environmentMap;
    environmentMap = nullptr;
    delete renderer;
    renderer = nullptr;
    doneCurrent();
}

QSize ShapeViewerGLWidget::minimumSizeHint() const {
    return QSize(50, 50);
}

QSize ShapeViewerGLWidget::sizeHint() const {
    return QSize(1500, 700);
}

void ShapeViewerGLWidget::timerEvent(QTimerEvent * event) {

    timeNow = QDateTime::currentMSecsSinceEpoch();
    if (timeNow - lastTime < 1)
        fps = 1;
    else
        fps = 1000.0 / (timeNow - lastTime);
    if (fps < 10) fps = 10;
    lastTime = timeNow;


    if (Game::objectLoadingTokens < Game::maxObjLag)
        Game::objectLoadingTokens += 2;

    animate(1.0f / fps);
    camera->update(fps);
    update();
    
}

void ShapeViewerGLWidget::setAnimationPlaying(bool on) {
    animationPlaying = on;
}

void ShapeViewerGLWidget::setAnimationEnd(bool on) {
    animationEnd = on;
}

void ShapeViewerGLWidget::setCarAnimation(int group, bool on, bool now) {
    if (group >= 0 && group < CarAnimation::GroupCount)
        carGroups[group] = on;
    carGroupsNow = now;
}

void ShapeViewerGLWidget::animateEng(Eng *shown) {
    if (shown == NULL)
        return;
    shown->setTravelled(animationTravelled, false);
    for (int g = 0; g < CarAnimation::GroupCount; ++g)
        shown->setAnimated(CarAnimation::Group(g), carGroups[g], carGroupsNow);
}

void ShapeViewerGLWidget::animate(float seconds) {
    if (animationPlaying)
        animationTravelled += 15.0 / 3.6 * seconds;
    if (crowdActive()) {
        animateCrowd(seconds);
        return;
    }
    if ((renderItem == 2 || renderItem == 3 || renderItem == 5) && Game::currentEngLib != NULL) {
        animateEng(eng);
        if (con != NULL)
            for (const Consist::EngItem &item : con->engItems)
                if (item.eng >= 0)
                    animateEng(Game::currentEngLib->eng[item.eng]);
    }
    if (renderItem == 4 && complexShape != NULL && complexShape->isLoaded() && complexShape->clipCount() > 0) {
        animateClips(seconds);
        return;
    }
    if (renderItem == 4 && complexShape != NULL && complexShape->isLoaded()) {
        complexShape->setAnimated(0, animationPlaying);
        if (animationPlaying)
            complexShape->updateSim(seconds, 0);
        complexShape->clearPose(0);
        if (animationEnd && !animationPlaying) {
            for (int part = 0; part < complexShape->partCount(); ++part)
                if (complexShape->partLastFrame(part) >= 0)
                    complexShape->setPartFrame(0, part, complexShape->partLastFrame(part));
        } else if (!animationPlaying) {
            // Its pantographs, doors, mirrors, windows and wipers as the
            // menu sets them, as an engine's: only engines and consists
            // took them, and a shape opened on its own did not move (the
            // user, 2026-10-28, CZ-AWT_383-053.s). Play runs all its key
            // frames instead, Last Frame shows them all at the end.
            if (!shapeCar || shapeCarFor != complexShape || shapeCarParts != complexShape->partCount()) {
                shapeCar = std::make_unique<CarAnimation>(*complexShape, false, false, 0.0f, 0.0f);
                shapeCarFor = complexShape;
                shapeCarParts = complexShape->partCount();
            }
            for (int g = 0; g < CarAnimation::GroupCount; ++g)
                shapeCar->set(CarAnimation::Group(g), carGroups[g], carGroupsNow);
            shapeCar->update(0.0f, seconds);
            shapeCar->forgetPose();
            shapeCar->pose(*complexShape, 0, nullptr);
        }
    }
}

void ShapeViewerGLWidget::setCrowd(const CrowdOptions &options) {
    crowdOptions = options;
    if (crowdScene)
        crowdScene->clear();
    crowdScene.reset();
    crowdFor = nullptr;
    crowdCameraPending = options.count > 0;
    if (options.count <= 0)
        cameraInit = true;
}

namespace {

// A repeatable random number in [0, 1) for the crowd's layout.
float layoutRandom(quint32 seed, quint32 salt) {
    quint32 x = seed * 0x9E3779B9u + salt * 0x85EBCA6Bu + 0x165667B1u;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return float(x >> 8) * (1.0f / 16777216.0f);
}

}

// The crowd (task humans 03): the shown human first, then the other files;
// people on a grid, a little off it, facing anywhere, the walkers on
// circles about their places.
void ShapeViewerGLWidget::buildCrowd() {
    if (crowdScene)
        crowdScene->clear();
    crowdScene = std::make_unique<Humans::Crowd>();
    crowdFor = complexShape;
    GltfShape *shown = dynamic_cast<GltfShape *>(complexShape);
    if (shown == nullptr)
        return;
    if (humanQuality < 0) {
        humanQuality = Settings::variant("core.rendering.humanQuality", SettingType::Enum).toInt();
        connect(&SettingsManager::instance(), &SettingsManager::runtimeSettingsChanged, this,
                [this](const QStringList &keys) {
            if (!keys.contains("core.rendering.humanQuality"))
                return;
            humanQuality = Settings::variant("core.rendering.humanQuality", SettingType::Enum).toInt();
            crowdFor = nullptr;
        });
    }
    Humans::Crowd::Tuning tuning = Humans::Crowd::quality(humanQuality);
    if (crowdOptions.nearCap > 0)
        tuning.nearCap = crowdOptions.nearCap;
    if (crowdOptions.coverageScale > 0.0f)
        tuning.coverageScale = crowdOptions.coverageScale;
    if (crowdOptions.directions > 0)
        tuning.impostorDirections = crowdOptions.directions;
    crowdScene->setTuning(tuning);
    // Loaded for the crowd, keeping their triangles for the impostors.
    ShapeLoadOptions options;
    options.insert(QString::fromLatin1(ShapeLoadOption::SkinnedTriangles), true);
    QStringList paths = crowdOptions.humans;
    paths.prepend(shown->getPathId());
    for (const QString &path : paths) {
        const int id = currentShapeLib->addShape(path, textureDirectory(path), options);
        const auto found = currentShapeLib->shape.find(id);
        GltfShape *human = found != currentShapeLib->shape.end() ? dynamic_cast<GltfShape *>(found->second) : nullptr;
        if (human != nullptr)
            crowdScene->addKind(human, crowdOptions.clipsFile);
    }
    const int kinds = crowdScene->kindCount();
    const int columns = std::max(1, crowdOptions.columns);
    for (int i = 0; i < crowdOptions.count; ++i) {
        const quint32 seed = crowdOptions.seed * 7919u + quint32(i);
        Humans::Crowd::PersonSpec spec;
        spec.kind = std::min(int(layoutRandom(seed, 1) * float(kinds)), kinds - 1);
        const int row = i / columns, column = i % columns;
        spec.position[0] = crowdOptions.start + float(row) * crowdOptions.along
                + (layoutRandom(seed, 2) - 0.5f) * 0.4f * crowdOptions.along;
        spec.position[2] = (float(column) - 0.5f * float(columns - 1)) * crowdOptions.across
                + (layoutRandom(seed, 3) - 0.5f) * 0.4f * crowdOptions.across;
        spec.heading = layoutRandom(seed, 4) * 2.0f * float(M_PI);
        spec.behaviour = layoutRandom(seed, 5) < crowdOptions.walking ? Humans::Crowd::Behaviour::Circle
                                                                       : Humans::Crowd::Behaviour::Idle;
        spec.radius = 0.6f + 0.6f * layoutRandom(seed, 6);
        if (crowdOptions.heights)
            spec.scale = 0.94f + 0.12f * layoutRandom(seed, 7);
        spec.seed = seed;
        crowdScene->addPerson(spec);
    }
}

void ShapeViewerGLWidget::animateCrowd(float seconds) {
    if (!crowdScene || crowdFor != complexShape)
        buildCrowd();
    if (crowdTime >= 0.0) {
        crowdScene->setTime(crowdTime);
        seconds = 0.0f;
    } else if (!animationPlaying) {
        seconds = 0.0f;
    }
    Humans::Crowd::View view;
    std::copy(camera->getPos(), camera->getPos() + 3, view.camera);
    // The camera's angle is across the view; the view's height spans less.
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.0f;
    view.fieldOfView = camera->fov / aspect;
    crowdScene->update(seconds, view);
}

QStringList ShapeViewerGLWidget::currentClipNames() const {
    QStringList names;
    if (renderItem == 4 && complexShape != NULL && complexShape->isLoaded())
        for (int c = 0; c < complexShape->clipCount(); ++c)
            names << complexShape->clipName(c);
    return names;
}

// A glTF shape's clips (task humans 02): a clips file added once, the clip
// and the blended one found by name, played at the time.
void ShapeViewerGLWidget::animateClips(float seconds) {
    GltfShape *gltf = dynamic_cast<GltfShape *>(complexShape);
    if (gltf != nullptr && !clipsFile.isEmpty() && clipsFileFor != complexShape) {
        clipsFileFor = complexShape;
        GltfShape clips(clipsFile, QFileInfo(clipsFile).fileName(), QFileInfo(clipsFile).path());
        clips.load();
        // Named after the file ("walk (people_clips)"): a human's own clips
        // keep their names.
        const QString suffix = QString(" (%1)").arg(QFileInfo(clipsFile).completeBaseName());
        const int added = clips.isLoaded() ? gltf->addClipsFrom(clips, suffix) : 0;
        qInfo() << "Clips file" << clipsFile << "added" << added << "clips";
    }
    if (animationPlaying)
        clipTime += seconds * clipSpeed;
    auto indexOf = [this](const QString &name) {
        for (int c = 0; c < complexShape->clipCount(); ++c)
            if (complexShape->clipName(c) == name)
                return c;
        return -1;
    };
    const int clip = clipName.isEmpty() ? -1 : indexOf(clipName);
    const int blend = blendName.isEmpty() || blendWeight <= 0.0f ? -1 : indexOf(blendName);
    ComplexShape::ClipPlay plays[2];
    int count = 0;
    // The share of the first clip's length the time is at; the blended one
    // plays at the same share of its own (a walk into a jog in step).
    float share = 0.0f;
    if (clip >= 0) {
        const float duration = complexShape->clipDuration(clip);
        share = duration > 0.0f ? clipTime / duration - std::floor(clipTime / duration) : 0.0f;
        plays[count].clip = clip;
        plays[count].time = share * duration;
        plays[count].weight = blend >= 0 ? 1.0f - blendWeight : 1.0f;
        ++count;
    }
    if (blend >= 0) {
        const float duration = complexShape->clipDuration(blend);
        if (clip < 0)
            share = duration > 0.0f ? clipTime / duration - std::floor(clipTime / duration) : 0.0f;
        plays[count].clip = blend;
        plays[count].time = share * duration;
        plays[count].weight = blendWeight;
        ++count;
    }
    complexShape->setClips(0, plays, count);
}

void ShapeViewerGLWidget::setCamera(Camera* cam){
    camera = cam;
}

void ShapeViewerGLWidget::surfaceInitialize() {
    Game::currentShapeLib = currentShapeLib;
    /*if(currentEngLib == NULL){
         currentEngLib = new EngLib();
         Game::currentEngLib = currentEngLib;
    }*/
    //qDebug() << "GLUU::get();";
    gluu = GLUU::get();
    //context()->set
    //qDebug() << "initializeOpenGLFunctions();";
    const bool openGL = surface->backend() == RenderSurface::OpenGL;
    if(openGL)
        initializeOpenGLFunctions();
    if(backgroundGlColor[0] == -2){
        backgroundGlColor[0] = 25.0/255;
        backgroundGlColor[1] = 25.0/255;
        backgroundGlColor[2] = 25.0/255;
        if(Game::systemTheme){
            backgroundGlColor[0] = 1;
            backgroundGlColor[1] = 1;
            backgroundGlColor[2] = 1;
        }
    }
    if(openGL)
        gluu->initShader();
    renderer = surface->createRenderer();
    renderer->setSurface(surface);
    renderer->clear(false, false, backgroundGlColor);
    renderer->resetState();


    //sFile = new SFile("F:/TrainSim/trains/trainset/pkp_sp47/pkp_sp47-001.s", "F:/TrainSim/trains/trainset/pkp_sp47");
    //sFile = new SFile("f:/train simulator/routes/cmk/shapes/cottage3.s", "cottage3.s", "f:/train simulator/routes/cmk/textures");

    //eng = new Eng("F:/Train Simulator/trains/trainset/PKP-ST44-992/","PKP-ST44-992.eng");

    //qDebug() << eng->loaded;
    //sFile->Load("f:/train simulator/routes/cmk/shapes/cottage3.s");
    //tile = new Tile(-5303,-14963);
    //qDebug() << "route = new Route();";
    
    float * aaa = new float[2]{0,0};
    if(camera == NULL){
        camera = new CameraFree(aaa);
        float spos[3];
        spos[0] = 0; spos[1] = 0; spos[2] = 0;
        camera->setPos((float*)&spos);
    }
    
    lastTime = QDateTime::currentMSecsSinceEpoch();
    timer.start(15, this);
    setFocus();
    setMouseTracking(true);
}

void ShapeViewerGLWidget::setBackgroundGlColor(float r, float g, float b){
    backgroundGlColor[0] = r;
    backgroundGlColor[1] = g;
    backgroundGlColor[2] = b;
    if(renderItem == 3 && con != NULL){
        con->setTextColor(backgroundGlColor);
    }
}

void ShapeViewerGLWidget::fillCurrentShapeHierarchyInfo(ShapeHierarchyInfo* info){
    Game::currentShapeLib = currentShapeLib;
    info->lights.clear();
    info->emitters.clear();
    if(renderItem == 4 && complexShape != NULL){
        complexShape->fillShapeHierarchyInfo(info);
        // Its lights (task shapes 14), on the matrices they hang on.
        for(const ShapeLights::Light &light : complexShape->lights()){
            ShapeHierarchyInfo::Light listed;
            listed.name = light.name;
            listed.spot = light.type == ShapeLights::Light::Type::Spot;
            listed.intensity = light.intensity;
            listed.matrixId = ShapeLights::partOf(*complexShape, light);
            info->lights.push_back(listed);
        }
        // Its emitters (task shapes 15): on their matrices, the root the
        // first.
        for(const ShapeEmitters::Emitter &emitter : complexShape->emitters()){
            ShapeHierarchyInfo::Emitter listed;
            listed.name = emitter.name;
            listed.kind = emitter.kind;
            listed.flow = emitter.output.flow;
            listed.matrixId = ShapeEmitters::partOf(*complexShape, emitter);
            info->emitters.push_back(listed);
        }
    }
}

void ShapeViewerGLWidget::fillCurrentShapeTextureInfo(QHash<int, ShapeTextureInfo*>& list){
    Game::currentShapeLib = currentShapeLib;
    if(renderItem == 4 && complexShape != NULL){
        complexShape->fillShapeTextureInfo(list);
    }
}

void ShapeViewerGLWidget::fillCurrentContentHierarchyInfo(QVector<ContentHierarchyInfo*>& list){
    Game::currentShapeLib = currentShapeLib;
    if(renderItem == 4 && complexShape != NULL){
        complexShape->fillContentHierarchyInfo(list, -1);
    }
    if(renderItem == 2 && eng != NULL){
        eng->fillContentHierarchyInfo(list, -1);
    }
    if(renderItem == 5 && con != NULL){
        con->fillContentHierarchyInfo(list, -1);
    }
}

void ShapeViewerGLWidget::surfacePaint() {
    if(selection){
        selection = false;
        if((renderItem == 3 && con != nullptr) || (pickPrimitives && shownShape() != nullptr))
            renderFrame(true);
    }
    // Always finish a pick with a visible frame in the widget framebuffer.
    renderFrame(false);
}

void ShapeViewerGLWidget::renderFrame(bool selectionPass) {
    Game::currentShapeLib = currentShapeLib;
    if(renderer == nullptr || !renderer->programsReady())
        return;
    // Counted when the frame statistics are on (crowd captures).
    RenderStats::ScopedFrame statsFrame(!selectionPass);
    const int selectionHeight = qRound(height() * devicePixelRatioF());
    if(selectionPass){
        const qreal pixelRatio = devicePixelRatioF();
        if(!renderer->beginSelection(qRound(width() * pixelRatio), selectionHeight))
            return;
    } else {
        renderer->clear(true, true, backgroundGlColor);
    }
    renderer->useProgram(selectionPass ? Renderer::PROGRAM_SELECTION : Renderer::PROGRAM_MAIN);
    // Zero is the background; cars and primitives are ShapePart ids (task
    // shapes 13): the car is the instance.
    const quint32 selectionId = selectionPass ? SelectionIdCodec::shapePart(0, 0) : 0;

    // disable shadows temporaily
    int shadowsState = Game::shadowsEnabled;
    Game::shadowsEnabled = 0;
    // Its own light for this frame: the route's sun and light stay for the
    // route editor's views.
    float routeSun[3], routeSunColor[4], routeAmbient[4], routeSpecular[4];
    std::copy(Game::sunLightDirection, Game::sunLightDirection + 3, routeSun);
    std::copy(gluu->diffuseColor, gluu->diffuseColor + 4, routeSunColor);
    std::copy(gluu->ambientColor, gluu->ambientColor + 4, routeAmbient);
    std::copy(light.sun, light.sun + 3, Game::sunLightDirection);
    std::fill(gluu->diffuseColor, gluu->diffuseColor + 3, light.sunColor);
    std::fill(gluu->ambientColor, gluu->ambientColor + 3, light.ambient);
    // Highlights reflect its own light only.
    std::copy(gluu->specularColor, gluu->specularColor + 4, routeSpecular);
    std::fill(gluu->specularColor, gluu->specularColor + 4, 0.0f);
    
    renderer->clear(false, false, backgroundGlColor);
    
    float aspect = float(this->width()) / float(this->height());
    float* lookAt = camera->getMatrix();
    const float zNear = renderItem == 4 ? nearPlane : 0.2f;
    Mat4::perspective(gluu->pMatrix, camera->fov*M_PI/180*(1/aspect), aspect, zNear, Game::objectLod);
    std::copy(gluu->pMatrix, gluu->pMatrix + 16, sceneProjection);
    sceneNear = zNear;
    sceneFar = Game::objectLod;
    Mat4::multiply(gluu->pMatrix, gluu->pMatrix, lookAt);
    
    Mat4::perspective(gluu->fMatrix, camera->fov*M_PI/180*(1/aspect), aspect, zNear, Game::objectLod);
    Mat4::multiply(gluu->fMatrix, gluu->pMatrix, lookAt);
    
    Mat4::identity(gluu->mvMatrix);

    Mat4::identity(gluu->objStrMatrix);
    
    renderer->applyFrameUniforms();
    std::copy(camera->getPos(), camera->getPos() + 3, gluu->cameraPosition);
    gluu->environmentMapLevels = 0;
    if(!selectionPass){
        if(environmentMap == nullptr)
            environmentMap = new EnvironmentMap(renderer);
        // A fixed warehouse interior: filled once, at the largest face size.
        if(environmentMap->complete() || environmentMap->fillWarehouse(256)){
            environmentMap->bind();
            gluu->environmentMapLevels = environmentMap->levels();
        }
    }
    if(renderer != nullptr)
        renderGathered(selectionId);
    if(!selectionPass && Game::environmentMapPreview && environmentMap != nullptr
            && environmentMap->complete())
        environmentMap->drawPreview(8, 8, qRound(48 * devicePixelRatioF()));
    if(crowdActive() && crowdCameraPending){
        // The crowd's own place to look from, once (its humans are its own
        // copies: the shown shape is not drawn, nor loaded).
        crowdCameraPending = false;
        cameraInit = false;
        nearPlane = 0.1f;
        camera->setPos(crowdOptions.camera[0], crowdOptions.camera[1], crowdOptions.camera[2]);
        camera->setPlayerRot(M_PI / 2.0, crowdOptions.look * M_PI / 180.0);
    }
    if(renderItem == 4 && complexShape != NULL && !crowdActive()){
        if(cameraInit && complexShape->isLoaded()){
            cameraInit = false;
            const float* bound = complexShape->getBound();
            const float dx = fabs(bound[0]-bound[1]);
            const float dy = fabs(bound[2]-bound[3]);
            const float dz = fabs(bound[4]-bound[5]);
            const float cx = (bound[0] + bound[1]) / 2.0f;
            const float cy = (bound[2] + bound[3]) / 2.0f;
            const float cz = (bound[4] + bound[5]) / 2.0f;
            // Fit the bounding sphere into the narrower field of view, and keep
            // the near plane in proportion so small shapes are not clipped.
            const float radius = 0.5f * std::sqrt(dx*dx + dy*dy + dz*dz);
            const float fovY = camera->fov*M_PI/180*(1/aspect);
            const float fovX = 2.0f * std::atan(std::tan(fovY / 2.0f) * aspect);
            float dist = 1.05f * radius / std::sin(0.5f * std::min(fovX, fovY));
            if(!(dist > 0.0f) || !std::isfinite(dist))
                dist = 1.0f;
            nearPlane = std::clamp(dist * 0.02f, 0.001f, 0.2f);
            // The model turns about its origin in "rot" mode (Z, then Y), so aim
            // at its turned centre.
            float centre[3] = {cx, cy, cz};
            if(mode == "rot"){
                const float zc = std::cos(rotZ), zs = std::sin(rotZ);
                const float x = centre[0] * zc - centre[1] * zs;
                centre[1] = centre[0] * zs + centre[1] * zc;
                centre[0] = x;
                const float yc = std::cos(rotY), ys = std::sin(rotY);
                const float z = -centre[0] * ys + centre[2] * yc;
                centre[0] = centre[0] * yc + centre[2] * ys;
                centre[2] = z;
            }
            camera->setPos(centre[0] - dist, centre[1], centre[2]);
        }
    }

    if (selectionPass) {
        const qreal pixelRatio = devicePixelRatioF();
        const quint32 id = renderer->readSelection(
            qFloor(selectionPosition.x() * pixelRatio),
            selectionHeight - qFloor(selectionPosition.y() * pixelRatio) - 1);
        renderer->endSelection();
        const SelectionIdCodec::DecodedSelection pick = SelectionIdCodec::decode(id);
        const bool shapePart = pick.valid && pick.kind == SelectionIdCodec::Kind::ShapePart;
        if(renderItem == 3 && con != nullptr){
            if(shapePart && pick.primaryId < static_cast<quint32>(con->engItems.size())){
                const int index = static_cast<int>(pick.primaryId);
                con->select(index);
                emit selected(index);
            }
        } else if(pickPrimitives && shownShape() != nullptr){
            emit primitivePicked(shapePart ? int(pick.part) : -1, pickAdd);
        }
    }
    
    renderer->releaseProgram();
    Game::shadowsEnabled = shadowsState;
    std::copy(routeSun, routeSun + 3, Game::sunLightDirection);
    std::copy(routeSunColor, routeSunColor + 4, gluu->diffuseColor);
    std::copy(routeAmbient, routeAmbient + 4, gluu->ambientColor);
    std::copy(routeSpecular, routeSpecular + 4, gluu->specularColor);
}

void ShapeViewerGLWidget::setExhaust(bool on) {
    exhaust = on;
    if (!on || exhaustSettingWatched)
        return;
    exhaustSettingWatched = true;
    exhaustQuality = Settings::variant("core.rendering.exhaust", SettingType::Enum).toInt();
    connect(&SettingsManager::instance(), &SettingsManager::runtimeSettingsChanged, this,
            [this](const QStringList &keys) {
        if (keys.contains("core.rendering.exhaust"))
            exhaustQuality = Settings::variant("core.rendering.exhaust", SettingType::Enum).toInt();
    });
}

// The particles' frame in the standalone viewer (as the route view's):
// the caps by the setting, the frame's time, still air on a mild day.
bool ShapeViewerGLWidget::beginExhaustFrame() {
    if (!exhaust) {
        // Child views draw none (the particles are the main view's).
        renderer->setExhaustQuality(0);
        return false;
    }
    renderer->setExhaustQuality(exhaustQuality);
    ExhaustParticles &particles = ExhaustParticles::instance();
    particles.setQuality(renderer->drawsExhaust() ? exhaustQuality : 0);
    if (particles.limit() == 0) {
        exhaustClock.invalidate();
        return false;
    }
    // Another item shown: the last one's smoke is gone.
    const void *shown = renderItem == 4 ? static_cast<const void *>(complexShape)
            : renderItem == 2 ? static_cast<const void *>(eng) : static_cast<const void *>(con);
    if (shown != exhaustShown) {
        particles.clear();
        exhaustShown = shown;
    }
    const qint64 ns = exhaustClock.isValid() ? exhaustClock.nsecsElapsed() : 0;
    exhaustClock.start();
    particles.beginFrame(float(double(ns) / 1e9), ExhaustParticles::Air());
    return true;
}

void ShapeViewerGLWidget::renderGathered(quint32 selectionId) {
    // Smoke and steam (task shapes 15): what the item gives out as it is
    // gathered.
    const bool exhaustFrame = selectionId == 0 && beginExhaustFrame();
    renderer->resetFrame();
    renderer->setSceneProjection(sceneProjection, sceneNear, sceneFar);
    // The exhaust's pass takes the view's directions (the route's views
    // give them with their sky).
    if(exhaustFrame)
        renderer->setViewDirections(sceneProjection, camera->getMatrix());
    renderer->setViewPosition(camera->getPos());
    RenderQueue &queue = *renderer;
    float *mv = renderer->transform();
    Mat4::identity(mv);
    if(mode == "rot"){
        Mat4::rotate(mv, mv, rotY, 0,1,0);
        Mat4::rotate(mv, mv, rotZ, 0,0,1);
    }
    if(renderItem == 2 && eng != NULL)
        eng->pushRenderItems(queue, selectionId);
    if(renderItem == 3 && con != NULL)
        con->pushRenderItems(queue, selectionId, true);
    if(renderItem == 5 && con != NULL)
        con->pushRenderItems(queue, selectionId, false);
    if(renderItem == 2 && con != NULL){
        Mat4::rotate(mv, mv, M_PI, 0,1,0);
        Mat4::translate(mv, mv, 0, 0, -con->conLength/2);
        con->pushRenderItems(queue, selectionId);
    }
    if(crowdActive()){
        // The crowd in its own space, whatever the model's turn.
        if(selectionId == 0 && crowdScene && crowdFor == complexShape){
            Mat4::identity(mv);
            crowdScene->push(queue);
        }
    } else if(renderItem == 4 && complexShape != NULL){
        if(selectionId == 0)
            updateDisplays();
        float model[16];
        std::copy(mv, mv + 16, model);
        complexShape->pushRenderItem(queue, selectionId, 0);
        if(exhaustFrame)
            ShapeEmitters::giveOut(*complexShape, 0, model, camera->getPos(), complexShape, 0);
        if(displays && displaysShape == complexShape && complexShape->isLoaded())
            displays->push(queue, *complexShape, 0, selectionId, 0, 0.0f);
        // Picked primitives over it (task shapes 13).
        if(selectionId == 0 && pickPrimitives && !picked.isEmpty()){
            static const float PickedColor[4] = {1.0f, 0.55f, 0.1f, 0.45f};
            queue.setLayer(RenderQueue::LAYER_OVERLAY);
            complexShape->pushPrimitiveHighlight(queue, 0, picked, PickedColor);
            queue.setLayer(RenderQueue::LAYER_SCENE);
        }
    }
    if(exhaustFrame)
        ExhaustParticles::instance().endFrame();
    renderer->renderFrame();
}

// The shown shape's displays (task scripting 02): its .sd's documents drawn
// with sample data, the clock's time now; drawn again while they refresh.
void ShapeViewerGLWidget::updateDisplays() {
    // Made once the shape has loaded (its .sd read).
    if (displaysShape != complexShape || (!displays && complexShape != NULL && complexShape->isLoaded())) {
        releaseDisplays();
        displaysShape = complexShape;
        if (complexShape != NULL && complexShape->isLoaded() && !complexShape->displays().isEmpty())
            displays = std::make_unique<Ui::ShapeDisplays>(*complexShape,
                                                           Ui::ShapeDisplays::ownDocuments(*complexShape));
        else if (complexShape != NULL && complexShape->isLoaded())
            Ui::ShapeDisplays::report(*complexShape);
    }
    if (!displays || complexShape == NULL || !complexShape->isLoaded())
        return;
    Ui::DisplayDocument::Data data;
    data.station = "Prudnik";
    data.platform = "1";
    // Rows: two trains with their fields, one with its timetable name only.
    auto row = [](const char *time, const QString &id, const QString &category, const QString &number,
                  const QString &origin, const QString &destination, const char *platform, const char *delay) {
        Ui::DisplayDocument::Row r;
        r.time = time;
        r.id = id;
        r.fields[TrainFields::Category] = category;
        r.fields[TrainFields::Number] = number;
        r.fields[TrainFields::Origin] = origin;
        r.fields[TrainFields::Destination] = destination;
        r.platform = platform;
        r.delay = delay;
        return r;
    };
    data.departures = {row("12:34", "1234_IC113_Prudnik", "IC", "113", "", QString::fromUtf8("Częstochowa"), "1", ""),
                       row("12:50", "1250_KS40123_Prudnik_Kluczbork", "", "", "", "Kluczbork", "2", "5"),
                       row("13:15", "1315_IC6100_Prudnik", "IC", "6100", "", QString::fromUtf8("Kraków Główny"), "1", "")};
    data.arrivals = {row("12:30", "1230_IC113_Gliwice_Prudnik", "IC", "113", "Gliwice", "", "1", ""),
                     row("12:45", "1245_KS40122_Opole_Prudnik", "", "", "Opole", "", "2", "10")};
    // A platform's next trains (task scripting 03): IC 113 standing, leaving
    // at 12:34, KS 40123 coming at 12:48, each with its cars; a third without.
    {
        auto car = [](int index, const char *number, const char *tags, const char *kind) {
            Ui::DisplayDocument::Car c;
            c.index = index;
            c.number = number;
            c.tags = tags;
            c.kind = kind;
            return c;
        };
        Ui::DisplayDocument::Row first = row("12:34", "1234_IC113_Prudnik", "IC", "113", "Gliwice",
                                             QString::fromUtf8("Częstochowa"), "1", "");
        first.arrives = "12:30";
        first.departs = "12:34";
        first.cars = {car(0, "", "", "engine"), car(1, "11", "A", "carriage"), car(2, "12", "WR", "carriage"),
                      car(3, "13", "B", "carriage"), car(4, "14", "B bike", "carriage"), car(5, "15", "B", "carriage")};
        // A long train: second class coaches up to its 13th vehicle (two rows
        // on the platform screen example).
        for (int k = int(first.cars.size()); k < 13; ++k) {
            Ui::DisplayDocument::Car c = car(k, "", "B", "carriage");
            c.number = QString::number(10 + k);
            first.cars << c;
        }
        Ui::DisplayDocument::Row second = row("12:48", "1250_KS40123_Prudnik_Kluczbork", "", "", "Opole", "Kluczbork", "1", "");
        second.arrives = "12:48";
        second.departs = "12:50";
        second.cars = {car(0, "1", "B", "engine"), car(1, "2", "B", "engine"), car(2, "3", "B", "engine")};
        Ui::DisplayDocument::Row third = row("13:15", "1315_IC6100_Prudnik", "IC", "6100", "",
                                             QString::fromUtf8("Kraków Główny"), "1", "");
        third.departs = "13:15";
        data.trains = {first, second, third};
    }
    // A car's train (Train): one with its fields (#tsre.<field> rows), its
    // origin and destination beyond the route, running to its second stop.
    Ui::DisplayDocument::Train &train = data.train;
    train.id = "1600_IC108_Warszawa_Rijeka";
    train.fields[TrainFields::Operator] = "PKP IC";
    train.fields[TrainFields::Category] = "IC";
    train.fields[TrainFields::Number] = "108";
    train.fields[TrainFields::Name] = "ADRIATIC";
    train.fields[TrainFields::Origin] = "Warszawa";
    train.fields[TrainFields::Destination] = "Rijeka";
    train.state = "running";
    train.previous = QString::fromUtf8("Częstochowa");
    train.next = "Zawiercie";
    train.platform = "1";
    train.arrives = "15:52";
    train.stops = {{QString::fromUtf8("Częstochowa"), "2", "15:20", "15:22", true},
                   {"Zawiercie", "1", "15:52", "15:54", false},
                   {"Katowice", "4", "16:20", "16:30", false}};
    train.extra = {{"kind", "fast"}};
    // Its cars (task scripting 03): a loco, a first-class coach, a
    // restaurant car and a coach with room for bikes; a Car display on the
    // second.
    auto car = [](int index, const char *number, const char *tags, const char *kind, const char *name) {
        Ui::DisplayDocument::Car c;
        c.index = index;
        c.number = number;
        c.tags = tags;
        c.kind = kind;
        c.name = name;
        return c;
    };
    train.cars = {car(0, "", "", "engine", "PKPIC_EP09-039"), car(1, "10", "A", "carriage", "PKPIC_A9mnopuz-90018"),
                  car(2, "11", "WR", "carriage", "PKPIC_WRmnouz-70202"),
                  car(3, "12", "B bike", "carriage", "PKPIC_B11mnouz-70082")};
    data.car = train.cars[1];
    const QDateTime now = QDateTime::currentDateTime();
    for (const std::shared_ptr<Ui::DisplayDocument> &document : displays->documents())
        if (document && document->due())
            document->draw(*renderer, now, data);
    // Clocks tick: drawn again soon.
    QTimer::singleShot(250, this, [this]() { update(); });
}

void ShapeViewerGLWidget::releaseDisplays() {
    if (displays && displaysShape != NULL)
        displays->release(*displaysShape, 0);
    if (displays && renderer != nullptr)
        for (const std::shared_ptr<Ui::DisplayDocument> &document : displays->documents())
            if (document)
                document->release(*renderer);
    displays.reset();
}

void ShapeViewerGLWidget::setPrimitivePicking(bool on) {
    pickPrimitives = on;
    update();
}

void ShapeViewerGLWidget::setPickedPrimitives(const QVector<int> &primitives) {
    picked = primitives;
    update();
}

void ShapeViewerGLWidget::surfaceResize(int w, int h) {
}

void ShapeViewerGLWidget::keyPressEvent(QKeyEvent * event) {
    if (event->key() == Qt::Key_F10
            && (event->modifiers() & Qt::ControlModifier)
            && (event->modifiers() & Qt::ShiftModifier)) {
        TexLib::dumpStats("ShapeViewerGLWidget");
        return;
    }
    camera->keyDown(event);
    Game::currentShapeLib = currentShapeLib;
    //Game::currentEngLib = currentEngLib;
    if(renderItem == 3 && con != NULL){
        switch (event->key()) {
            case Qt::Key_Delete:
                if(con != NULL)
                    con->deteleSelected();
                break;
            case Qt::Key_F:
                if(con != NULL)
                    con->flipSelected();
                break;
            case Qt::Key_Right:
                if(con != NULL)
                    con->moveRightSelected();
                break;
            case Qt::Key_Left:
                if(con != NULL)
                con->moveLeftSelected();
                break;
        }
        
        emit refreshItem();
    }
}

void ShapeViewerGLWidget::wheelEvent(QWheelEvent *event) {
    camera->MouseWheel(event);
    event->accept();
}

void ShapeViewerGLWidget::keyReleaseEvent(QKeyEvent * event) {
    camera->keyUp(event);
}

void ShapeViewerGLWidget::mousePressEvent(QMouseEvent *event) {
    m_lastPos = event->position();
    m_lastPos *= Game::PixelRatio;
    mousePressed = true;
    selectionPosition = event->position();
    pressPosition = event->position();
    selection = renderItem == 3 && con != nullptr;
    update();
    if(event->button() == Qt::RightButton)
        mouseRPressed = true;
    if(event->button() == Qt::LeftButton)
        mouseLPressed = true;
    camera->MouseDown(event);
    setFocus();
}

void ShapeViewerGLWidget::mouseReleaseEvent(QMouseEvent* event) {
    camera->MouseUp(event);
    mousePressed = false;
    mouseRPressed = false;
    mouseLPressed = false;
    // A click, not a drag turning the view, picks a primitive.
    if(event->button() == Qt::LeftButton && pickPrimitives && shownShape() != nullptr
            && (event->position() - pressPosition).manhattanLength() < 5.0){
        selectionPosition = event->position();
        pickAdd = (event->modifiers() & Qt::ControlModifier) != 0;
        selection = true;
        update();
    }

    if ((event->button()) == Qt::RightButton) {
        showContextMenu(event->pos());
    }
}

void ShapeViewerGLWidget::mouseMoveEvent(QMouseEvent *event) {
    mousex = event->position().x()*Game::PixelRatio;
    mousey = event->position().y()*Game::PixelRatio;
    
    if(mode == "rot" && !crowdActive()){
        if (mousePressed) {
            if(mouseRPressed){
                rotZ += (float) (m_lastPos.y() - mousey) / 30 * (camera->fov/45.0);
            }
            if(mouseLPressed){
                float fff = (float) (m_lastPos.x() - mousex) / 30 * (camera->fov/45.0);
                //qDebug() << fff;
                rotY += fff;
            }
            if (rotZ > 1.57)
                rotZ = (float) 1.57;
            if (rotZ < -1.57)
                rotZ = (float) - 1.57;
        }
    } else {
        camera->MouseMove(event);
    }
    m_lastPos.setX(mousex);
    m_lastPos.setY(mousey);
    //m_lastPos *= Game::PixelRatio;
}

void ShapeViewerGLWidget::showContextMenu(const QPoint & point) {
    if(defaultMenuActions["flipSelected"] == NULL){
        defaultMenuActions["flipSelected"] = new QAction(
            //% "&Flip"
            qtTrId("shape.viewer.shape.viewer.glwidget.action.flip"), this);
        QObject::connect(defaultMenuActions["flipSelected"], SIGNAL(triggered()), this, SLOT(flipConSelected()));
        defaultMenuActions["leftSelected"] = new QAction(
            //% "&Move Left"
            qtTrId("shape.viewer.shape.viewer.glwidget.action.move.left"), this);
        QObject::connect(defaultMenuActions["leftSelected"], SIGNAL(triggered()), this, SLOT(leftConSelected()));
        defaultMenuActions["rightSelected"] = new QAction(
            //% "&Move right"
            qtTrId("shape.viewer.shape.viewer.glwidget.action.move.right"), this);
        QObject::connect(defaultMenuActions["rightSelected"], SIGNAL(triggered()), this, SLOT(rightConSelected()));
        defaultMenuActions["deleteSelected"] = new QAction(
            //% "&Delete"
            qtTrId("shape.viewer.shape.viewer.glwidget.action.delete"), this);
        QObject::connect(defaultMenuActions["deleteSelected"], SIGNAL(triggered()), this, SLOT(deleteConSelected()));
        defaultMenuActions["copyUnit"] = new QAction(
            //% "&Copy"
            qtTrId("shape.viewer.shape.viewer.glwidget.action.copy"), this);
        QObject::connect(defaultMenuActions["copyUnit"], SIGNAL(triggered()), this, SLOT(copyUnitConSelected()));
        defaultMenuActions["pasteUnit"] = new QAction(
            //% "&Paste Right"
            qtTrId("shape.viewer.shape.viewer.glwidget.action.paste.right"), this);
        QObject::connect(defaultMenuActions["pasteUnit"], SIGNAL(triggered()), this, SLOT(pasteUnitConSelected()));
    }
    
    if(renderItem == 3 && con != NULL){
        QMenu menu;

        QString menuStyle = QString(
            "QMenu::separator {\
              color: ")+Game::StyleMainLabel+";\
            }";
        menu.setStyleSheet(menuStyle);
        menu.addSection(
            //% "Selected Unit"
            qtTrId("shape.viewer.context.selected.unit"));
        menu.addAction(defaultMenuActions["flipSelected"]);
        menu.addAction(defaultMenuActions["leftSelected"]);
        menu.addAction(defaultMenuActions["rightSelected"]);
        menu.addAction(defaultMenuActions["deleteSelected"]);
        menu.addAction(defaultMenuActions["copyUnit"]);
        menu.addAction(defaultMenuActions["pasteUnit"]);
        menu.exec(mapToGlobal(point));
    }
}

void ShapeViewerGLWidget::resetRot(){
    rotY = M_PI;
    rotZ = 0;
}

void ShapeViewerGLWidget::setLight(const Light &newLight){
    light = newLight;
}

void ShapeViewerGLWidget::setModelRotation(float yaw, float pitch){
    rotY = M_PI + yaw;
    rotZ = std::clamp(pitch, -1.57f, 1.57f);
}


void ShapeViewerGLWidget::showEng(Eng *e){
    eng = e;
    con = NULL;
    renderItem = 2;
}

void ShapeViewerGLWidget::showEng(QString path, QString name){
    int idx = Game::currentEngLib->addEng(path, name);
    qDebug() << "eng id "<< idx;
    eng = Game::currentEngLib->eng[idx];
    con = NULL;
    renderItem = 2;
}

void ShapeViewerGLWidget::showEngSet(int id){
    Game::currentShapeLib = currentShapeLib;
    qDebug() << "eng set id "<< id;
    con = ConLib::con[id];
    con->setTextColor(backgroundGlColor);
    eng = NULL;
    renderItem = 2;
}

void ShapeViewerGLWidget::flipConSelected(){
    if(con != NULL){
        con->flipSelected();
        emit refreshItem();
    }
}

void ShapeViewerGLWidget::leftConSelected(){
    if(con != NULL){
        con->moveLeftSelected();
        emit refreshItem();
    }
}

void ShapeViewerGLWidget::rightConSelected(){
    if(con != NULL){
        con->moveRightSelected();
        emit refreshItem();
    }
}

void ShapeViewerGLWidget::deleteConSelected(){
    if(con != NULL){
        con->deteleSelected();
        emit refreshItem();
    }
}

void ShapeViewerGLWidget::copyUnitConSelected(){
    if(con == NULL)
        return;
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(QString("ENGID ")+QString::number(con->getSelectedEngId()));
}

void ShapeViewerGLWidget::pasteUnitConSelected(){
    if(con == NULL)
        return;
    QClipboard *clipboard = QApplication::clipboard();
    QStringList args = clipboard->text().split(" ");
    if(args[0] != "ENGID")
        return;
    if(args.size() != 2)
        return;
    int val = args[1].toInt();
    con->appendEngItem(val, 1, false);
    
    emit refreshItem();
}

void ShapeViewerGLWidget::showCon(int id){
    if(id < 0){
        con = NULL;
        eng = NULL;
        complexShape = NULL;
        renderItem = 3;
        return;
    }
    qDebug() << "con id "<< id;
    con = ConLib::con[id];
    con->setTextColor(backgroundGlColor);
    eng = NULL;
    renderItem = 3;
}

void ShapeViewerGLWidget::showConSimple(Consist *currentCon){
    if(currentCon == NULL){
        con = NULL;
        eng = NULL;
        complexShape = NULL;
        renderItem = 5;
        return;
    }
    con = currentCon;
    con->setTextColor(backgroundGlColor);
    eng = NULL;
    renderItem = 5;
}

void ShapeViewerGLWidget::showConSimple(int id){
    if(id < 0){
        con = NULL;
        eng = NULL;
        complexShape = NULL;
        renderItem = 5;
        return;
    }
    qDebug() << "con id "<< id;
    con = ConLib::con[id];
    con->setTextColor(backgroundGlColor);
    eng = NULL;
    renderItem = 5;
}

void ShapeViewerGLWidget::showCon(int aid, int id){
    qDebug() << "con aid "<< aid<< " con id "<< id;
    con = ActLib::Act[aid]->activityObjects[id]->con;
    con->setTextColor(backgroundGlColor);
    complexShape = NULL;
    renderItem = 3;
}

QString ShapeViewerGLWidget::textureDirectory(const QString &shapePath){
    const QString dir = shapePath.section("/", 0, -2);
    if(dir.endsWith("/SHAPES", Qt::CaseInsensitive)){
        const QString textures = dir.section("/", 0, -2) + "/TEXTURES";
        if(QFileInfo::exists(textures))
            return textures;
    }
    return dir;
}

void ShapeViewerGLWidget::showShape(QString path, QString texPath, ComplexShape **currentShape){
    int shapeId;
    // A MaSzyna model beside its vehicle's .mmd finds its files as the
    // vehicle does; any other model by TSRE's rules (task shapes 18).
    ShapeLoadOptions options;
    if(path.endsWith(".e3d", Qt::CaseInsensitive) && MaszynaPaths::besideVehicle(path))
        options.insert(QString::fromLatin1(ShapeLoadOption::MaszynaLookups), true);
    if(texPath.length() > 0)
        shapeId = currentShapeLib->addShape(path, texPath, options);
    else
        shapeId = currentShapeLib->addShape(path, Game::root+"/ROUTES/"+Game::route+"/TEXTURES", options);
    complexShape = NULL;
    if(shapeId >= 0){
        complexShape = currentShapeLib->shape[shapeId];
        if(currentShape != NULL)
            *currentShape = complexShape;
        cameraInit = true;
    } else if(currentShape != NULL){
        *currentShape = NULL;
    }
    renderItem = 4;
    con = NULL;
    eng = NULL;
}

void ShapeViewerGLWidget::showShape(ComplexShape *currentShape){
    if(currentShape == NULL)
        return;
    nearPlane = 0.2f;
    complexShape = currentShape;
    cameraInit = true;
    renderItem = 4;
    con = NULL;
    eng = NULL;
}

void ShapeViewerGLWidget::setMode(QString n){
    mode = n;
}
