/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "RouteEditorGLWidget.h"
#include <QMouseEvent>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QPainter>
#include <math.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/world/Route.h>
#include <tsre/world/TerrainMaterialMap.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/trains/Eng.h>
#include <tsre/world/Tile.h>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include <tsre/ogl/GLH.h>
#include <tsre/math3d/Vector2f.h>
#include <tsre/math3d/Flex.h>
#include <tsre/world/objects/DynTrackObj.h>
#include <tsre/world/objects/RulerObj.h>
#include <tsre/world/objects/TelepoleObj.h>
#include <tsre/procedural/ProceduralShape.h>
#include <tsre/procedural/ShapeTemplates.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/procedural/OrtsTrackProfileRenderer.h>

#include <tsre/world/TerrainLib.h>
#include <tsre/texture/Brush.h>
#include <tsre/texture/TexLib.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/geo/HeightWindow.h>
#include <tsre/geo/MapWindow.h>
#include <tsre/geo/ImageryWindow.h>
#include "TerrainTreeWindow.h"
#include <tsre/shape/ShapeLib.h>
#include <tsre/trains/EngLib.h>
#include "QOpenGLFunctions_3_3_Core"
#include <tsre/Undo.h>
#include <tsre/world/Environment.h>
#include <tsre/world/Terrain.h>
#include <tsre/trains/ActivityObject.h>
#include <tsre/gui/GuiFunct.h>
#include <tsre/hud/GuiGlCompass.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/Activity.h>
#include "PlayActivitySelectWindow.h"
#include <tsre/sound/SoundManager.h>
#include <tsre/world/Skydome.h>
#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/renderer/EnvironmentMap.h>
#include <tsre/renderer/PlanarReflection.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/renderer/SelectionRenderer.h>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <routeEditor/RouteEditorClient.h>
#include "tools/EditorTool.h"
#include "tools/ToolRegistry.h"
#include <tsre/camera/CameraMap.h>
#include <tsre/map/MapPalette.h>
#include <tsre/map/MapView.h>
#include <tsre/map/ActivityMapLayer.h>
#include <tsre/map/TrackItemMapLayer.h>
#include <tsre/map/TrackMapLayer.h>
#include <tsre/ogl/OglObj.h>
#include <routeEditor/TerrainTileCreationDialog.h>
#include <tsre/world/RouteClient.h>
#include <tsre/ClientInfo.h>
#include <tsre/renderer/RenderStats.h>
#include <QMessageBox>

// The active 8-byte paged layout derives local X/Z in StandardFog.
// StandardFogStoredCoords is retained as the Stage 1 shader reference, but it
// requires reverting the paged vertex layout to TerrainVertex12 before use.
#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif

static const QString MainRenderShaderName = "StandardFog";
// Objects farther than this are left out of the water reflection.
static const float WaterReflectionObjectDistance = 500.0f;
static constexpr unsigned long long LiveContinuousUpdateIntervalMs = 50;

RouteEditorGLWidget::RouteEditorGLWidget(QWidget *parent)
: QOpenGLWidget(parent),
m_xRot(0),
m_yRot(0),
m_zRot(0),
tools(std::make_unique<ToolRegistry>()) {
    
    this->installEventFilter(this);
}


bool RouteEditorGLWidget::eventFilter(QObject *object, QEvent *event){
    if (event->type() == QEvent::FocusIn){
        //qDebug() << "aaaaa";
        bolckContextMenu = true;
    }
    return false;
}

RouteEditorGLWidget::~RouteEditorGLWidget() {
    // QOpenGLWidget destroys the context after this destructor has run; its
    // aboutToBeDestroyed signal must not call cleanup() on a destroyed object.
    if (context() != NULL)
        disconnect(context(), &QOpenGLContext::aboutToBeDestroyed,
                   this, &RouteEditorGLWidget::cleanup);
    cleanup();
}

QSize RouteEditorGLWidget::minimumSizeHint() const {
    return QSize(50, 50);
}

QSize RouteEditorGLWidget::sizeHint() const {
    return QSize(1000, 700);
}

void RouteEditorGLWidget::cleanup() {
    makeCurrent();
    if(selectionRenderer != NULL){
        selectionRenderer->release();
        delete selectionRenderer;
        selectionRenderer = NULL;
    }
    delete renderer;
    renderer = NULL;
    delete environmentMap;
    environmentMap = NULL;
    delete waterReflection;
    waterReflection = NULL;
    //delete gluu->m_program;
    //gluu->m_program = 0;
    doneCurrent();
}

void RouteEditorGLWidget::timerEvent(QTimerEvent * event) {
    Game::currentShapeLib = currentShapeLib;
    timeNow = QDateTime::currentMSecsSinceEpoch();
    unsigned long long frameTimeMs = timeNow - lastTime;
    if(frameTimeMs < 1)
        frameTimeMs = 1;
    float rawFps = 1000.0f / (float)frameTimeMs;

    fps = (int)rawFps;
    if (fps < 10) fps = 10;

    fpsDisplayAccumMs += (double)frameTimeMs;
    fpsDisplayAccumFrames++;

    // Update visible FPS at 4 Hz using average frame time from the whole window.
    if(timeNow - fpsDisplayLastUpdate >= 250){
        if(fpsDisplayAccumFrames > 0 && fpsDisplayAccumMs > 0.0){
            double avgFrameTimeMs = fpsDisplayAccumMs / (double)fpsDisplayAccumFrames;
            fpsDisplay = (int)(1000.0 / avgFrameTimeMs + 0.5);
        } else {
            fpsDisplay = (int)(rawFps + 0.5f);
        }
        fpsDisplayAccumMs = 0.0;
        fpsDisplayAccumFrames = 0;
        fpsDisplayLastUpdate = timeNow;
    }

    if (timeNow % 200 < lastTime % 200) {
        //qDebug() << "new second" << timeNow;
        if (selectedObj != NULL)
            emit updateProperties(selectedObj);
        if(!liveFlexActive && !liveRulerActive && !liveTelepoleActive)
            Undo::StateEndIfLongTime();
    }
    
    if (timeNow % 100 < lastTime % 100) {
        //qDebug() << "new second" << timeNow;
        if(Game::serverClient != NULL){
            Game::serverClient->updatePointerPosition((int) camera->pozT[0], (int) camera->pozT[1], aktPointerPos[0], aktPointerPos[1], aktPointerPos[2]);
        }
    }
    
    if(Game::soundEnabled){
        if (timeNow % 200 < lastTime % 200) {
            SoundManager::UpdateListenerPos((int)camera->pozT[0], (int)camera->pozT[1], camera->getPos(), camera->getTarget(), camera->getUp());
            SoundManager::UpdateAll();
        }

        if (timeNow % 50 < lastTime % 50) {
            SoundManager::UpdateAll();
        }
    }

    if (!simulationPaused)
        route->updateSim(camera->pozT, (float) (timeNow - lastTime) / 1000.0);

    lastTime = timeNow;

    if (Game::objectLoadingTokens < Game::maxObjLag)
        Game::objectLoadingTokens += 2;

    camera->update(fps);
    
    update();
}

bool RouteEditorGLWidget::initRoute(){
    // Init Shape and Trains libs
    currentShapeLib = new ShapeLib();
    Game::currentShapeLib = currentShapeLib;
    engLib = new EngLib();
    Game::currentEngLib = engLib;
    
    // Init Route
    if(Game::serverClient != NULL){
        qDebug() << "RouteClient";
        route = new RouteClient();
        QObject::connect(route, SIGNAL(initDone()), this, SLOT(initRoute2()));
        route->load();
        return true;
    } else {
        route = new Route();
        route->load();
        if (!route->loaded){ 
            return false;
        }
        initRoute2();
        return true;
    }
    return false;
}

void RouteEditorGLWidget::initRoute2(){
    QObject::connect(route, SIGNAL(objectSelected(GameObj*)), this, SLOT(objectSelected(GameObj*)));
    QObject::connect(route, SIGNAL(objectSelected(QVector<GameObj*>)), this, SLOT(objectSelected(QVector<GameObj*>)));
    QObject::connect(route, SIGNAL(sendMsg(QString)), this, SLOT(msg(QString)));

    // Init Camera
    cameraInit();

    // Play?
    if(Game::ActivityToPlay.length() > 0){
        playInit();
    }
    
    emit routeLoaded(route);
    
    emit showWindow();
    return;
}

void RouteEditorGLWidget::playInit(){
        int actId = ActLib::GetAct(Game::root + "/ROUTES/" + Game::route + "/ACTIVITIES", Game::ActivityToPlay );
        qDebug() << "======== actId" << actId << Game::ActivityToPlay;
        if(actId < 0){
            PlayActivitySelectWindow actWindow;
            actWindow.setRoute(route);
            actWindow.exec();
            actId = actWindow.actId;
        }
        if(actId >= 0){
            ActLib::Act[actId]->initToPlay();
            route->activitySelected(ActLib::Act[actId]);
            setSelectedObj((GameObj*)route->getActivityConsist(0));
            camera->setCameraObject((GameObj*)route->getActivityConsist(0));
        }
}

void RouteEditorGLWidget::cameraInit(){
    float * aaa = new float[2] { 0, 0 };
    cameraFree = new CameraFree(aaa);
    //cameraObj = new CameraConsist();
    camera = cameraFree;
    if (cameraMap == NULL)
        cameraMap = new CameraMap();
    if (!trackMap)
        trackMap = std::make_unique<TrackMapLayer>();
    if (!trackItemMap)
        trackItemMap = std::make_unique<TrackItemMapLayer>();
    if (!activityMap)
        activityMap = std::make_unique<ActivityMapLayer>();
    float spos[3];
    if (Game::start == 2) {
        camera->setPozT(Game::startTileX, -Game::startTileY);
    } else {
        camera->setPozT(route->getStartTileX(), -route->getStartTileZ());
        spos[0] = route->getStartpX();
        spos[2] = -route->getStartpZ();
    }
    if (Game::terrainLib->load(route->getStartTileX(), -route->getStartTileZ())) {
        spos[1] = 20 + Game::terrainLib->getHeight(route->getStartTileX(), -route->getStartTileZ(), route->getStartpX(), -route->getStartpZ());
    } else {
        spos[1] = 0;
    }
    camera->setPos((float*) &spos);
}

void RouteEditorGLWidget::initializeGL() {
    
    if(Game::soundEnabled)
        SoundManager::InitAl();

    gluu = GLUU::get();
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &RouteEditorGLWidget::cleanup);
    qDebug() << "# InitializeOpenGLFunctions";

    initializeOpenGLFunctions();

    renderer = new OpenGL3Renderer();
    
    //funcs = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_3_Core>();
    //if (!funcs) {
    //    qWarning() << "Could not obtain required OpenGL context version";
    //    exit(1);
    //}
    //funcs->initializeOpenGLFunctions();/**/
    const float black[3] = {0.0f, 0.0f, 0.0f};
    renderer->clear(false, false, black);
    //qDebug() << "gluu->initShader();";
    qDebug() << "# InitShaders";
    gluu->initShader();
    qDebug() << "# InitShaders finished";
    selectionRenderer = new SelectionRenderer();
    renderer->resetState();

    //sFile = new SFile("F:/TrainSim/trains/trainset/pkp_sp47/pkp_sp47-001.s", "F:/TrainSim/trains/trainset/pkp_sp47");
    //sFile = new SFile("f:/train simulator/routes/cmk/shapes/cottage3.s", "cottage3.s", "f:/train simulator/routes/cmk/textures");
    //eng = new Eng("F:/Train Simulator/trains/trainset/PKP-ST44-992/","PKP-ST44-992.eng",0);
    //sFile->Load("f:/train simulator/routes/cmk/shapes/cottage3.s");
    //tile = new Tile(-5303,-14963);
    //qDebug() << "route = new Route();";

    
    /*PlayActivitySelectWindow *actWindow = new PlayActivitySelectWindow();
    actWindow->setRoute(route);
    qDebug() << "=1a";
    actWindow->exec();
     qDebug() << "=1b";*/

    lastTime = QDateTime::currentMSecsSinceEpoch();
    fpsDisplayLastUpdate = lastTime;
    fpsDisplayAccumMs = 0.0;
    fpsDisplayAccumFrames = 0;
    int timerStep = 15;
    const int fpsLimit = Settings::integer("core.system.fpsLimit");
    if (fpsLimit > 0)
        timerStep = 1000 / fpsLimit;
    timer.start(timerStep, this);
    setFocus();
    setMouseTracking(true);
    pointer3d = new Pointer3d();
    compass = new GuiGlCompass();
    compassPointer = new OglObj();
    float *punkty = new float[3 * 6];
    int ptr = 0;
    punkty[ptr++] = 0;
    punkty[ptr++] = 0.98;
    punkty[ptr++] = 0;
    punkty[ptr++] = 0;
    punkty[ptr++] = 1.0;
    punkty[ptr++] = 0;
    compassPointer->setLineWidth(2);
    compassPointer->setMaterial(0.0, 0.0, 0.0);
    compassPointer->init(punkty, ptr, RenderItem::V, GL_LINES);
    delete[] punkty;    
    
    //selectedObj = NULL;
    setSelectedObj(NULL);
    groupObj = new GroupObj();
    copyPasteGroupObj = new GroupObj();
    defaultPaintBrush = new Brush();
    mapWindow = new MapWindow();
    imageryWindow = new ImageryWindow();
    Quat::fill(this->placeRot);

    SoundManager::listenerX = camera->pozT[0];
    SoundManager::listenerZ = camera->pozT[1];
    
    //emit routeLoaded(route);
    emit mkrList(route->getMkrList());

    shadowMapSize = Settings::variant(
                "core.rendering.shadow.primaryMapSize", SettingType::Enum).toInt();
    distantShadowMapSize = Settings::variant(
                "core.rendering.shadow.distantMapSize", SettingType::Enum).toInt();
    gluu->makeShadowFramebuffer(FramebufferName0, depthTexture0,
            shadowMapSize, GL_TEXTURE9);
    gluu->makeShadowFramebuffer(FramebufferName1, depthTexture1,
            shadowMapSize, GL_TEXTURE2);
    gluu->makeShadowFramebuffer(FramebufferName2, depthTexture2,
            distantShadowMapSize, GL_TEXTURE3);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glActiveTexture(GL_TEXTURE0);
        
    
    moveStep = Game::DefaultMoveStep;
    moveMaxStep = Game::DefaultMoveStep;
    defaultMoveStep = moveStep;
}

void RouteEditorGLWidget::reloadRefFile(){
    route->loadAddons();
    //route->ref = new Ref((Game::root + "/ROUTES/" + Game::route + "/" + Game::routeName + ".ref"));
    emit refreshObjLists();
}

void RouteEditorGLWidget::reloadTrackProfiles(){
    if(route == NULL)
        return;

    const QString routePath = Game::root + "/ROUTES/" + Game::route;
    OrtsTrackProfileRenderer::clearTemplateMeshCache();
    OrtsTrackProfileCatalog::load(routePath, true);

    // Ruler and static-track generated objects own OpenGL resources, so
    // invalidate them while this widget's context is current. DynTracks defer
    // their deletion until the next render pass as usual.
    makeCurrent();
    for(Tile *tile : route->tile){
        if(tile == NULL || tile->loaded != 1)
            continue;
        for(const auto &entry : tile->obiekty){
            if(entry.second != NULL)
                entry.second->reloadProceduralProfile();
        }
    }
    doneCurrent();

    emit refreshObjLists();
    if(selectedObj != NULL)
        emit updateProperties(selectedObj);
    update();
}

void RouteEditorGLWidget::refreshMarkerList() {
    if (route != NULL) emit mkrList(route->getMkrList());
}

void RouteEditorGLWidget::setCameraObject(GameObj* obj){
    camera->setCameraObject(obj);
}

void RouteEditorGLWidget::setMoveStep(float val){
    moveStep = val;
    moveMaxStep = val; 
}

bool RouteEditorGLWidget::canRenderFrame() const{
    if(route == NULL) return false;
    if(!route->loaded) return false;
    if(gluu == NULL) return false;
    if(camera == NULL) return false;
    return true;
}

void RouteEditorGLWidget::paintGL(){
    Game::currentShapeLib = currentShapeLib;
    if (!canRenderFrame()) return;
    Terrain::beginProceduralFrame();
    renderer->resetState();

    if (currentViewMode == ViewMode::Map) {
        paintMap();
        return;
    }

    const bool selectionPass = selection;
    paintScene();

    if(selectionPass && !selection){
        // QOpenGLWidget does not preserve its color buffer by default. Finish
        // every selection callback with a visible frame before returning to Qt.
        renderer->resetState();
        paintScene();
    }
}

void RouteEditorGLWidget::paintScene(){
    Game::currentShapeLib = currentShapeLib;
    if (!canRenderFrame()) return;
    if (renderer == NULL) return;
    const bool selectionPass = selection;
    const QString shaderName = selectionPass ? "Selection" : MainRenderShaderName;
    if (gluu->shaders[shaderName] == NULL){
        if(selectionPass){
            qWarning() << "Selection shader is unavailable";
            selection = false;
            update();
        }
        return;
    }
    if(selectionPass && selectionRenderer == NULL){
        qWarning() << "Selection renderer is unavailable";
        selection = false;
        update();
        return;
    }
    RenderStats::ScopedFrame statsFrame(!selectionPass);
    // View-dependent shading (PBR materials) reads the camera position.
    std::copy(camera->getPos(), camera->getPos() + 3, gluu->cameraPosition);
    // Secondary views must not read last frame's water reflection.
    std::fill(gluu->waterReflectionView, gluu->waterReflectionView + 4, 0.0f);
    // Drop anything left from an interrupted frame and rebalance the matrix stack.
    renderer->resetFrame();
    renderer->setViewPosition(camera->getPos());
    RenderQueue &queue = *renderer;

    // Live placement tools rebuild their geometry from the pointer position.
    // Apply the position read in the previous frame now, before that geometry
    // is gathered; queued packets must not lose their buffers mid-frame.
    const bool drawPointerEnabled = !selectionPass && !Game::playerMode;
    if (drawPointerEnabled)
        applyPointerToLiveTools();

    // Gather the scene before drawing anything: the shadow maps are drawn
    // from the same queue before the main passes sample them.
    const int gatherMode = selectionPass ? GLUU::RENDER_SELECTION : GLUU::RENDER_DEFAULT;
    Mat4::identity(renderer->transform());
    Mat4::perspective(gluu->fMatrix, Game::cameraFov * M_PI / 180, float(this->width()) / this->height(), 0.2f, Game::objectLod);
    Mat4::multiply(gluu->fMatrix, gluu->fMatrix, camera->getMatrix());
    // Terrain patch culling tests against the current projection.
    Mat4::perspective(gluu->pMatrix, Game::cameraFov * M_PI / 180, float(this->width()) / this->height(), 0.2f, Game::objectLod);
    Mat4::multiply(gluu->pMatrix, gluu->pMatrix, camera->getMatrix());
    RenderStats::setCategory(RenderStats::CategoryTerrain);
    // Terrain receives shadows but does not cast them.
    renderer->setShadowCasting(false);
    // Environment map faces look in every direction: gather terrain patches
    // all around the camera and let the renderer cull each view.
    const bool renderEnvironment = !selectionPass && Game::environmentMapEnabled;
    Terrain::gatherAllDirections = renderEnvironment;
    Game::terrainLib->pushRenderItems(queue, camera->pozT, camera->getPos(), camera->getTarget(), 3.14f / 3, gatherMode);
    renderer->setShadowCasting(true);
    RenderStats::setCategory(RenderStats::CategoryWorld);
    route->pushRenderItems(queue, camera->pozT, camera->getPos(), camera->getTarget(), camera->getRotX(), 3.14f / 3, gatherMode);
    RenderStats::setCategory(RenderStats::CategoryOverlay);
    renderer->setLayer(RenderQueue::LAYER_OVERLAY);
    route->pushRenderOverlays(queue, camera->pozT, camera->getPos(), camera->getRotX(), gatherMode);
    RenderStats::setCategory(RenderStats::CategoryOther);
    renderer->setLayer(RenderQueue::LAYER_WATER);
    // Shaded water draws the top layer only; the others colour it.
    const int waterSurface = route->env->surfaceWaterLayer();
    for(int i = 0; i < route->env->waterCount; i++)
        if(!Game::waterShaded || i == waterSurface)
            Game::terrainLib->pushRenderItemsWater(queue, camera->pozT, camera->getPos(), camera->getTarget(), 3.14f / 3, gatherMode, i);
    renderer->setLayer(RenderQueue::LAYER_SCENE);

    // Sky and distant terrain are gathered here too, so environment map faces
    // can draw them before the main passes do.
    const float aspect = float(this->width()) / this->height();
    Mat4::identity(renderer->transform());
    Mat4::translate(renderer->transform(), renderer->transform(), camera->getPos());
    Mat4::translate(renderer->transform(), renderer->transform(), 0, -50, 0);
    Mat4::rotate(renderer->transform(), renderer->transform(), 2.0, 0, 1, 0);
    renderer->setLayer(RenderQueue::LAYER_SKY);
    route->skydome->pushRenderItems(queue);
    Mat4::identity(renderer->transform());
    // Distant terrain patches are culled against the distant projection.
    Mat4::perspective(gluu->pMatrix, Game::cameraFov * M_PI / 180, aspect, 600.0f, Game::distantLod);
    Mat4::multiply(gluu->pMatrix, gluu->pMatrix, camera->getMatrix());
    Mat4::translate(renderer->transform(), renderer->transform(), 0, route->getDistantTerrainYOffset(), 0);
    renderer->setLayer(RenderQueue::LAYER_DISTANT);
    Game::terrainLib->pushRenderItemsLo(queue, camera->pozT, camera->getPos(), camera->getTarget(), 3.14f / 3, gatherMode);
    for(int i = 0; i < route->env->waterCount; i++)
        if(!Game::waterShaded || i == waterSurface)
            Game::terrainLib->pushRenderItemsWaterLo(queue, camera->pozT, camera->getPos(), camera->getTarget(), 3.14f / 3, gatherMode, i);
    Terrain::gatherAllDirections = false;
    renderer->setLayer(RenderQueue::LAYER_SCENE);
    Mat4::identity(renderer->transform());

    // Render Shadows
    if (!selectionPass && Game::shadowsEnabled > 0){
        RenderStats::beginPhase(RenderStats::PhaseShadow);
        renderShadowMaps();
        RenderStats::endPhase(RenderStats::PhaseShadow);
    }
    if (renderEnvironment){
        RenderStats::beginPhase(RenderStats::PhaseEnvironment);
        renderEnvironmentMap();
        RenderStats::endPhase(RenderStats::PhaseEnvironment);
    }
    bool reflectWater = false;
    if (!selectionPass && Game::waterShaded && Game::waterReflection){
        RenderStats::beginPhase(RenderStats::PhaseReflection);
        reflectWater = renderWaterReflection();
        RenderStats::endPhase(RenderStats::PhaseReflection);
    }

    // Render Scene
    if(selectionPass){
        const int selectionWidth = qRound((float)this->width() * Game::PixelRatio);
        const int selectionHeight = qRound((float)this->height() * Game::PixelRatio);
        if(!selectionRenderer->begin(selectionWidth, selectionHeight)){
            qWarning() << "Could not start the integer selection pass";
            selection = false;
            update();
            return;
        }
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glActiveTexture(GL_TEXTURE0);
        renderer->clear(true, true);
    }
    gluu->currentShader = gluu->shaders[shaderName];
    gluu->currentShader->bind();

    const bool blendingWasEnabled = selectionPass ? renderer->setBlending(false) : true;
    // Reflecting materials sample the environment map on its own unit.
    gluu->environmentMapLevels = 0;
    if (!selectionPass && Game::environmentMapEnabled && environmentMap != NULL
            && environmentMap->complete()) {
        environmentMap->bind();
        gluu->environmentMapLevels = environmentMap->levels();
    }
    std::fill(gluu->waterReflectionView, gluu->waterReflectionView + 4, 0.0f);
    if (reflectWater) {
        waterReflection->bind();
        gluu->waterReflectionView[0] = 1.0f / (this->width() * Game::PixelRatio);
        gluu->waterReflectionView[1] = 1.0f / (this->height() * Game::PixelRatio);
        std::copy(waterReflectionPlane, waterReflectionPlane + 4, gluu->waterReflectionPlane);
        gluu->waterReflectionView[3] = float(waterReflection->levels());
    }

    renderer->clear(false, false, gluu->skyColor);
    if(!selectionPass)
        renderer->setViewport(0, 0, qRound((float)this->width() * Game::PixelRatio),
                              qRound((float)this->height() * Game::PixelRatio));
    Mat4::identity(gluu->mvMatrix);
    Mat4::identity(renderer->transform());

    // Sky, distant terrain, then terrain and world, each with its own
    // projection.
    const float fov = Game::cameraFov * M_PI / 180;
    Renderer::LayeredView mainView;
    mainView.view = camera->getMatrix();
    mainView.projection = [fov, aspect](float nearPlane, float farPlane, float *out) {
        Mat4::perspective(out, fov, aspect, nearPlane, farPlane);
    };
    mainView.sceneFar = Game::objectLod;
    mainView.distantFar = Game::distantLod;
    renderer->beginViewBand(mainView, Renderer::BAND_SKY);
    RenderStats::beginPhase(RenderStats::PhaseSky);
    renderer->renderPasses(Renderer::PASS_SKY, Renderer::PASS_SKY);
    RenderStats::endPhase(RenderStats::PhaseSky);
    Mat4::identity(renderer->transform());

    renderer->beginViewBand(mainView, Renderer::BAND_DISTANT);
    RenderStats::beginPhase(RenderStats::PhaseDistant);
    renderer->renderPasses(Renderer::PASS_DISTANT, Renderer::PASS_DISTANT);
    RenderStats::endPhase(RenderStats::PhaseDistant);
    Mat4::identity(renderer->transform());

    renderer->beginViewBand(mainView, Renderer::BAND_SCENE);
    RenderStats::beginPhase(RenderStats::PhaseScene);

    const bool drawPointerOnTerrain = drawPointerEnabled && stickPointerToTerrain && Game::viewTerrainShape;
    const bool drawPointerAfterWorld = drawPointerEnabled && (!stickPointerToTerrain || !Game::viewTerrainShape);

    if (drawPointerOnTerrain) {
        // The pointer reads terrain depth, so terrain must be drawn first.
        renderer->renderPasses(Renderer::PASS_TERRAIN, Renderer::PASS_TERRAIN);
        pushRenderPointer(queue);
    }

    renderer->renderPasses(Renderer::PASS_TERRAIN, Renderer::PASS_OVERLAY);
    renderWaterPass(!selectionPass && Game::waterShaded && Game::waterReflection);
    // Glass and other transmissive glTF materials see the frame drawn so far.
    renderer->renderPasses(Renderer::PASS_TRANSMISSION, Renderer::PASS_TRANSMISSION);

    if (drawPointerAfterWorld) {
        pushRenderPointer(queue);
        renderer->renderPasses(Renderer::PASS_TERRAIN, Renderer::PASS_WATER);
    }
    RenderStats::endPhase(RenderStats::PhaseScene);

    renderer->endView(mainView);
    // render compass
    RenderStats::beginPhase(RenderStats::PhaseUi);
    if (!selectionPass && Game::viewCompass){
        Mat4::identity(gluu->mvMatrix);
        Mat4::ortho(gluu->pMatrix, -1.0, 1.0, 1.0 - 2*(float(this->height()) / this->width()), 1.0, 0.0, 1.0);
        Mat4::identity(gluu->objStrMatrix);
        gluu->setMatrixUniforms();
        gluu->currentShader->setUniformValue(gluu->currentShader->lod, 0.0f);

        renderer->setLayer(RenderQueue::LAYER_UI);
        compass->pushRenderItem(queue, camera->getRotX()+M_PI);
        compassPointer->pushRenderItem(queue);
        renderer->setLayer(RenderQueue::LAYER_SCENE);
        renderer->renderPasses(Renderer::PASS_UI, Renderer::PASS_UI);
    }
    
    
    // HUD
    if(!selectionPass && Game::hudEnabled){
        int shadowsState = Game::shadowsEnabled;
        Game::shadowsEnabled = 0;
        float hudScale = Game::hudScale;
        Mat4::identity(gluu->mvMatrix);
        Mat4::ortho(gluu->pMatrix, -1.0, -1.0+2.0*hudScale, 1.0 - 2*(float(this->height()) / this->width())*hudScale, 1.0, 0.0, 1.0);
        Mat4::identity(gluu->objStrMatrix);
        gluu->setMatrixUniforms();
        gluu->currentShader->setUniformValue(gluu->currentShader->lod, 0.0f);
        renderer->setLayer(RenderQueue::LAYER_UI);
        camera->pushRenderHud(queue);
        renderer->setLayer(RenderQueue::LAYER_SCENE);
        renderer->renderPasses(Renderer::PASS_UI, Renderer::PASS_UI);
        Game::shadowsEnabled = shadowsState;
        gluu->currentShader->release();
    }
    renderer->renderFrame();
    if (!selectionPass && Game::environmentMapPreview && environmentMap != NULL
            && environmentMap->complete())
        environmentMap->drawPreview(8, 8, qRound(48 * Game::PixelRatio));
    RenderStats::endPhase(RenderStats::PhaseUi);

    // Handle Selection
    if (selectionPass && blendingWasEnabled)
        renderer->setBlending(true);
    if(selectionPass){
        handleSelection();
        selectionRenderer->end();
        gluu->currentShader->release();
        return;
    }

    
    // Set Info
    if (this->isActiveWindow()) {
        emit this->naviInfo(route->getTileObjCount((int) camera->pozT[0], (int) camera->pozT[1]), route->getTileHiddenObjCount((int) camera->pozT[0], (int) camera->pozT[1]));
        emit this->posInfo(camera->getCurrentPos());
        emit this->pointerInfo(aktPointerPos);
    }
    drawEditorFpsHud();
}

void RouteEditorGLWidget::drawEditorFpsHud(){
    if(!Game::editorFpsHudEnabled)
        return;
    if(selection)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(QColor(72, 30, 112));

    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(10);
    painter.setFont(font);

    const QString label = QString("FPS: %1").arg(fpsDisplay);
    const QRect backgroundRect(12, 12, 92, 24);
    painter.fillRect(backgroundRect, QColor(0, 0, 0, 150));
    painter.drawText(backgroundRect.adjusted(8, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
    painter.end();

    // QPainter over QOpenGLWidget can leave GL state changed (depth/cull/blend).
    // Restore defaults to prevent cross-frame rendering regressions.
    renderer->resetState();
}

namespace {
// Half-widths and half depth ranges in metres of the near, middle and far
// shadow maps.
constexpr float ShadowHalfExtent[3] = {80.0f, 300.0f, 700.0f};
constexpr float ShadowHalfDepth[3] = {200.0f, 600.0f, 700.0f};
// Blur reference in metres: the near and middle maps blur by
// 2 * ShadowBlurReference / shadow1Res metres (shadow1Res comes with the
// primary map size), and the bias of surfaces without normals grows with
// each map's width relative to it.
constexpr float ShadowBlurReference = 80.0f;
// Half depth range the bias of surfaces without normals was tuned for.
constexpr float ShadowBiasReferenceHalfDepth = 200.0f;
// Direction towards the shadow-casting sun.
constexpr float ShadowLightDirection[3] = {-1.0f, 1.5f, 1.0f};
// Normal offset and depth bias of the near and middle maps for surfaces with
// normals, in texels or filter radii, whichever is larger. The fragment
// shaders also bias each filter tap by the receiver's slope, so the offset
// only has to cover texel quantization.
constexpr float ShadowNormalOffsetTexels[2] = {1.0f, 1.0f};
constexpr float ShadowDepthBiasTexels[2] = {1.0f, 1.0f};
}

// Light-space matrices of the near, mid and far shadow maps, centred on the
// camera. The near and mid maps share the primary map size; the near map
// covers a third of the mid map's width, so shadows close to the camera get
// three times the detail.
void RouteEditorGLWidget::computeShadowMatrices() {
    float* lookAt = Mat4::create();
    float* out1 = Vec3::create();
    Vec3::set(out1, 0, 1, 0);
    float *ld = Vec3::create();
    Vec3::set(ld, ShadowLightDirection[0], ShadowLightDirection[1], ShadowLightDirection[2]);
    float *aaa = camera->getPos();
    Vec3::add(ld, ld, aaa);
    float *matrices[3] = {gluu->pShadowMatrix0, gluu->pShadowMatrix, gluu->pShadowMatrix2};
    for (int map = 0; map < 3; map++)
        Mat4::ortho(matrices[map], -ShadowHalfExtent[map], ShadowHalfExtent[map],
                    -ShadowHalfExtent[map], ShadowHalfExtent[map],
                    -ShadowHalfDepth[map], ShadowHalfDepth[map]);
    // Keep the blur and the bias of surfaces without normals the same in world
    // space in every map: texels grow with the map width, depth units with
    // the depth range.
    for (int map = 0; map < 2; map++) {
        const float texelScale = ShadowHalfExtent[map] / ShadowBlurReference;
        gluu->shadowMapScale[map] = texelScale;
        gluu->shadowMapScale[2 + map] = texelScale * ShadowBiasReferenceHalfDepth / ShadowHalfDepth[map];
    }
    // Surfaces with normals look up the near and middle maps from a point moved
    // along the normal, which needs only a small depth bias. The far map keeps
    // its constant bias.
    const float tapSpread = 2.0f * ShadowBlurReference / gluu->shadow1Res;
    for (int map = 0; map < 2; map++) {
        const float texel = 2.0f * ShadowHalfExtent[map] / shadowMapSize;
        const float filter = std::max(texel, tapSpread);
        gluu->shadowNormalOffset[map] = ShadowNormalOffsetTexels[map] * filter;
        gluu->shadowDepthBias[map] = ShadowDepthBiasTexels[map] * filter / (2.0f * ShadowHalfDepth[map]);
    }
    gluu->shadowNormalOffset[2] = 0.0f;
    float lightDirection[3] = {ShadowLightDirection[0], ShadowLightDirection[1], ShadowLightDirection[2]};
    Vec3::normalize(gluu->shadowLightDirection, lightDirection);
    Mat4::lookAt(lookAt, ld, aaa, out1);
    Mat4::multiply(gluu->pShadowMatrix0, gluu->pShadowMatrix0, lookAt);
    Mat4::multiply(gluu->pShadowMatrix, gluu->pShadowMatrix, lookAt);
    Mat4::multiply(gluu->pShadowMatrix2, gluu->pShadowMatrix2, lookAt);
    delete[] lookAt;
    delete[] out1;
    delete[] ld;
}

// Draws the near, middle and far shadow maps from the gathered queue, with
// casters up to 250 m, 600 m and 1000 m away.
void RouteEditorGLWidget::renderShadowMaps() {
    computeShadowMatrices();
    gluu->currentShader = gluu->shaders["Shadows"];
    gluu->currentShader->bind();
    Mat4::identity(gluu->mvMatrix);
    Mat4::identity(gluu->objStrMatrix);

    // The shadow shader reads uShadowPMatrix; swap in the near map's matrix.
    std::swap(gluu->pShadowMatrix, gluu->pShadowMatrix0);
    gluu->setMatrixUniforms();
    glBindFramebuffer(GL_FRAMEBUFFER, FramebufferName0);
    glActiveTexture(GL_TEXTURE0);
    renderer->clear(true, true);
    renderer->setViewport(0, 0, shadowMapSize, shadowMapSize);
    renderer->renderShadowCasters(250.0f, RenderStats::FrameStats::PassSlots - 3,
                                  gluu->pShadowMatrix);
    std::swap(gluu->pShadowMatrix, gluu->pShadowMatrix0);

    gluu->setMatrixUniforms();
    glBindFramebuffer(GL_FRAMEBUFFER, FramebufferName1);
    glActiveTexture(GL_TEXTURE0);
    renderer->clear(true, true);
    renderer->setViewport(0, 0, shadowMapSize, shadowMapSize);
    renderer->renderShadowCasters(600.0f, RenderStats::FrameStats::PassSlots - 2,
                                  gluu->pShadowMatrix);

    std::swap(gluu->pShadowMatrix, gluu->pShadowMatrix2);
    gluu->setMatrixUniforms();
    glBindFramebuffer(GL_FRAMEBUFFER, FramebufferName2);
    glActiveTexture(GL_TEXTURE0);
    renderer->clear(true, true);
    renderer->setViewport(0, 0, distantShadowMapSize, distantShadowMapSize);
    renderer->renderShadowCasters(1000.0f, RenderStats::FrameStats::PassSlots - 1,
                                  gluu->pShadowMatrix);
    std::swap(gluu->pShadowMatrix, gluu->pShadowMatrix2);
    gluu->currentShader->release();
}

// Renders the faces scheduled for this frame from the camera position, from
// the queue gathered for the main view: sky, distant terrain, then terrain,
// objects and water without editor overlays. Objects are limited by distance
// and size; every face is rendered once before the round-robin starts.
void RouteEditorGLWidget::renderEnvironmentMap() {
    if (environmentMap == NULL)
        environmentMap = new EnvironmentMap();
    if (!environmentMap->ensure(Game::environmentMapSize))
        return;
    // The faces must not sample the cube they are drawn into.
    EnvironmentMap::unbind();
    gluu->environmentMapLevels = 0;
    gluu->currentShader = gluu->shaders[MainRenderShaderName];
    gluu->currentShader->bind();
    Mat4::identity(gluu->mvMatrix);
    Mat4::identity(gluu->objStrMatrix);
    const QVector<int> faces = environmentMap->nextFaces(environmentMap->complete()
            ? Game::environmentMapFacesPerFrame : EnvironmentMap::FaceCount);
    Renderer::ViewLimits limits;
    limits.maxDistance = Game::environmentMapObjectDistance;
    // A face spans 90 degrees over its texels: skip objects under one texel.
    limits.minAngularRadius = 1.0f / Game::environmentMapSize;
    float *eye = camera->getPos();
    float view[16];
    Renderer::LayeredView faceView;
    faceView.view = view;
    faceView.projection = EnvironmentMap::faceProjection;
    faceView.sceneFar = Game::objectLod;
    faceView.distantFar = Game::distantLod;
    faceView.limits = &limits;
    for (int face : faces) {
        environmentMap->beginFace(face, gluu->skyColor);
        EnvironmentMap::faceView(face, eye, view);
        renderer->renderLayeredView(faceView);
    }
    environmentMap->endFaces(defaultFramebufferObject());
}

void RouteEditorGLWidget::renderWaterPass(bool measure) {
    if (measure)
        renderer->renderPassesMeasured(Renderer::PASS_WATER, Renderer::PASS_WATER);
    else
        renderer->renderPasses(Renderer::PASS_WATER, Renderer::PASS_WATER);
}

bool RouteEditorGLWidget::renderWaterReflection() {
    // Water hidden behind terrain still passes the view test, so the last
    // measured water pass decides; until a count is in, reflect.
    if (renderer->measuredSamples() == 0)
        return false;
    // The plane of the water in view; no water, no reflection.
    const float aspect = float(this->width()) / this->height();
    const float fov = Game::cameraFov * M_PI / 180;
    float projection[16];
    float viewProjection[16];
    Mat4::perspective(projection, fov, aspect, 0.2f, Game::objectLod);
    Mat4::multiply(viewProjection, projection, camera->getMatrix());
    float plane[4];
    renderer->visibleBounds(Renderer::PASS_WATER, viewProjection, waterBounds);
    if (waterBounds.empty())
        return false;
    // The plane is fitted to all the water gathered around the camera, not
    // only the patches in view: a sloping river seen through a patch or two
    // would give a level plane, off by a metre at the camera, and turning
    // would flip nearby water between reflection and plain shading.
    renderer->visibleBounds(Renderer::PASS_WATER, nullptr, waterBounds);
    const float *eye = camera->getPos();
    if (!PlanarReflection::fitPlane(waterBounds, eye, plane))
        return false;
    if (plane[0] * eye[0] + plane[1] * eye[1] + plane[2] * eye[2] + plane[3] <= 0.0f)
        return false;
    if (waterReflection == NULL)
        waterReflection = new PlanarReflection();
    const int width = std::max(1, qRound(this->width() * Game::PixelRatio * 0.5f));
    const int targetHeight = std::max(1, qRound(this->height() * Game::PixelRatio * 0.5f));
    if (!waterReflection->ensure(width, targetHeight))
        return false;
    // The mirrored view must not sample the texture it is drawn into.
    PlanarReflection::unbind();
    gluu->currentShader = gluu->shaders[MainRenderShaderName];
    gluu->currentShader->bind();
    Mat4::identity(gluu->mvMatrix);
    Mat4::identity(gluu->objStrMatrix);
    // The camera looks at the scene mirrored in the plane.
    float mirror[16];
    float view[16];
    PlanarReflection::mirrorMatrix(plane, mirror);
    Mat4::multiply(view, camera->getMatrix(), mirror);
    Renderer::ViewLimits limits;
    limits.maxDistance = WaterReflectionObjectDistance;
    // Skip objects under about a texel of the half-resolution view.
    limits.minAngularRadius = 1.0f / targetHeight;
    Renderer::LayeredView mirrored;
    mirrored.view = view;
    mirrored.projection = [fov, aspect](float nearPlane, float farPlane, float *out) {
        Mat4::perspective(out, fov, aspect, nearPlane, farPlane);
    };
    mirrored.sceneFar = Game::objectLod;
    mirrored.distantFar = Game::distantLod;
    mirrored.limits = &limits;
    mirrored.mirrorPlane = plane;
    mirrored.water = false;
    waterReflection->begin(gluu->skyColor);
    renderer->renderLayeredView(mirrored);
    waterReflection->end(defaultFramebufferObject());
    std::copy(plane, plane + 4, waterReflectionPlane);
    return true;
}

void RouteEditorGLWidget::handleSelection() {
    if (!selection)
        return;
    if(selectionRenderer == NULL || !selectionRenderer->isActive()){
        qWarning() << "Selection read requested without an active selection target";
        selection = false;
        update();
        return;
    }

    if(!selectionProbePoints.isEmpty()){
        for(const QPoint &point : selectionProbePoints){
            const int probeY = selectionRenderer->height() - point.y() - 1;
            selectionProbeResults.push_back(selectionRenderer->readPixel(point.x(), probeY));
        }
        selection = false;
        return;
    }

    const int x = mousex;
    const int realy = selectionRenderer->height() - (int)mousey - 1;
    const quint32 selectionId = selectionRenderer->readPixel(x, realy);
    const int cameraTileX = static_cast<int>(camera->pozT[0]);
    const int cameraTileZ = static_cast<int>(camera->pozT[1]);

    // Object/property/preview updates can synchronously repolish widgets and
    // re-enter QOpenGLWidget painting. Defer them until the caller has ended
    // the integer selection target and paintGL has restored a visible frame.
    // TODO: Capture Ctrl/Shift here if queued modifier timing becomes observable.
    selection = false;
    QMetaObject::invokeMethod(this, [this, selectionId, cameraTileX, cameraTileZ] {
        applySelection(selectionId, cameraTileX, cameraTileZ);
    }, Qt::QueuedConnection);
}

void RouteEditorGLWidget::applySelection(quint32 selectionId,
        int cameraTileX, int cameraTileZ) {
    if(route == NULL || !route->loaded)
        return;

    const SelectionIdCodec::DecodedSelection decoded =
            SelectionIdCodec::decode(selectionId);
    qDebug() << selectionId;
    qDebug() << "selector" << static_cast<int>(decoded.selector);

    // WorldObj Selected
    if(decoded.valid && decoded.kind == SelectionIdCodec::Kind::None){
        if (selectedObj != NULL) {
            selectedObj->unselect();
            if (autoAddToTDB)
                route->addToTDBIfNotExist((WorldObj*)selectedObj);
            setSelectedObj(NULL);
        }
    } else if(decoded.valid
              && decoded.kind == SelectionIdCodec::Kind::WorldObject){
        const int objectIndex = static_cast<int>(decoded.primaryId);
        const int part = decoded.part;
        const int wx = cameraTileX + decoded.tileXOffset;
        const int wz = cameraTileZ + decoded.tileZOffset;
        qDebug() << "part:" << part;
        qDebug() << wx << " " << wz << " " << objectIndex;
        WorldObj *selectedWorldObj = (WorldObj*) selectedObj;
        if (keyControlEnabled) {
            if (selectedWorldObj == NULL){
                setSelectedObj(groupObj);
                selectedWorldObj = (WorldObj*) selectedObj;
            } else if (selectedWorldObj->typeObj != GameObj::worldobj){
                selectedWorldObj->unselect();
                setSelectedObj(groupObj);
            } else if (selectedWorldObj->typeObj == GameObj::worldobj) {
                groupObj->addObject(selectedWorldObj);
                setSelectedObj(groupObj);
            }
            groupObj->addObject(route->getObj(wx, wz, objectIndex));
            if (groupObj->count() == 0) {
                qDebug() << "brak obiektu";
                groupObj->unselect();
                setSelectedObj(NULL);
            }
        } else {
            WorldObj* twobj = route->getObj(wx, wz, objectIndex);
            if (selectedWorldObj != NULL && twobj != selectedWorldObj) {
                selectedWorldObj->unselect();
                if (autoAddToTDB) {
                    route->addToTDBIfNotExist(selectedWorldObj);
                }
            }
            lastSelectedObj = selectedObj;
            setSelectedObj(twobj);
            if (selectedObj == NULL) {
                qDebug() << "brak obiektu";
            } else {
                selectedObj->select(part);
            }
        }
    } else if(decoded.valid
              && decoded.kind == SelectionIdCodec::Kind::Terrain){
        const int wx = cameraTileX + decoded.tileXOffset;
        const int wz = cameraTileZ + decoded.tileZOffset;
        qDebug() << wx << wz << decoded.patchId << decoded.feature;
        if (selectedObj != NULL) {
            if ((keyControlEnabled || keyShiftEnabled) && selectedObj->typeObj == GameObj::terrainobj ) {
                Terrain * tt = (Terrain*) selectedObj;
                if(!tt->isXYinside(wx, wz)){// >mojex != wx || tt->mojez != wz){
                    selectedObj->unselect();
                    setSelectedObj(NULL);
                }
            } else {
                selectedObj->unselect();
                if (autoAddToTDB)
                    route->addToTDBIfNotExist((WorldObj*)selectedObj);
                setSelectedObj(NULL);
            }
        }
        Terrain *t = Game::terrainLib->getTerrainByXY(wx, wz);
        if (t == NULL) {
            qDebug() << "brak obiektu";
        } else {
            t->select(decoded.patchId, keyControlEnabled);
        }
        setSelectedObj((GameObj*)t);
    } else if(decoded.valid
              && decoded.kind == SelectionIdCodec::Kind::ActivityObject){
        if (selectedObj != NULL) {
            selectedObj->unselect();
            if (autoAddToTDB)
                route->addToTDBIfNotExist((WorldObj*)selectedObj);
            setSelectedObj(NULL);
        }
        const int CID = static_cast<int>(decoded.primaryId);
        const int EID = decoded.part;
        qDebug() << CID << EID;
        setSelectedObj((GameObj*)route->getActivityObject(CID));
        if (selectedObj == NULL) {
            qDebug() << "brak obiektu";
        } else {
            //qDebug() << "eid"<<EID;
            selectedObj->select(EID);
            setSelectedObj(selectedObj);
        }
    } else if(decoded.valid
              && decoded.kind == SelectionIdCodec::Kind::DatabaseItem){
        if (selectedObj != NULL) {
            selectedObj->unselect();
            if (autoAddToTDB)
                route->addToTDBIfNotExist((WorldObj*)selectedObj);
            setSelectedObj(NULL);
        }
        const int TID = static_cast<int>(decoded.databaseKind);
        const int UID = static_cast<int>(decoded.databaseItemId);
        qDebug() << TID << UID;
        setSelectedObj((GameObj*)route->getTrackItem(TID, UID));
        if (selectedObj == NULL) {
            qDebug() << "brak obiektu";
        } else {
            selectedObj->select();
        }
    } else if(decoded.valid
              && decoded.kind == SelectionIdCodec::Kind::ActivityService){
        if (selectedObj != NULL) {
            selectedObj->unselect();
            if (autoAddToTDB)
                route->addToTDBIfNotExist((WorldObj*)selectedObj);
            setSelectedObj(NULL);
        }
        const int CID = static_cast<int>(decoded.primaryId);
        const int EID = decoded.part;
        qDebug() << CID << EID;
        setSelectedObj((GameObj*)route->getActivityConsist(CID));
        if (selectedObj == NULL) {
            qDebug() << "brak obiektu";
        } else {
            //qDebug() << "eid"<<EID;
            selectedObj->select(EID);
            setSelectedObj(selectedObj);
        }
    } else {
        if (selectedObj != NULL) {
            selectedObj->unselect();
            if (autoAddToTDB)
                route->addToTDBIfNotExist((WorldObj*)selectedObj);
            setSelectedObj(NULL);
        }
    }

    update();
}

void RouteEditorGLWidget::updatePointerPosition() {
    readPointerPosition();
    applyPointerToLiveTools();
}

// Reads the scene depth under the mouse and updates the pointer position.
// Needs the scene drawn up to this point.
void RouteEditorGLWidget::readPointerPosition() {
    int x = mousex;
    int y = mousey;

    static unsigned long long int oldTime = 0;
    unsigned long long int newTime = QDateTime::currentMSecsSinceEpoch();
    static float winZ[4];
    int viewport[4];

    renderer->viewport(viewport);
    int realy = viewport[3] - (int) y - 1;
    if(newTime - oldTime > 50){
        winZ[0] = renderer->readDepth(x, realy);
        oldTime = newTime;
    }
    GLH::glhUnProjectf((float) x, (float) realy, winZ[0], // 
            gluu->mvMatrix,
            gluu->pMatrix,
            viewport,
            aktPointerPos);
}

// Moves the live flex, ruler and telepole tools to the pointer; this rebuilds
// their geometry.
void RouteEditorGLWidget::applyPointerToLiveTools() {
    if(liveFlexActive && !mouseRPressed)
        updateLiveFlex((int)camera->pozT[0], (int)camera->pozT[1], aktPointerPos);
    else if(liveRulerActive && !mouseRPressed)
        updateLiveRuler((int)camera->pozT[0], (int)camera->pozT[1], aktPointerPos);
    else if(liveTelepoleActive && !mouseRPressed)
        updateLiveTelepole((int)camera->pozT[0], (int)camera->pozT[1], aktPointerPos);
}

float RouteEditorGLWidget::pointerDisplayY() const {
    return aktPointerPos[1]
            + ((continuousFlexMode || liveFlexActive
                || continuousRulerMode || liveRulerActive
                || liveTelepoleActive)
                ? continuousPlacementYOffset : 0.0f);
}

// Submits the 3D pointer and other users' markers; the caller draws them.
// The live tools were already moved at the start of the frame, before their
// geometry was gathered, so only the pointer position is read here.
void RouteEditorGLWidget::pushRenderPointer(RenderQueue &queue) {
    readPointerPosition();
    if (!Game::viewPointer3d)
        return;
    float *mv = queue.transform();
    queue.pushTransform();
    Mat4::translate(mv, mv, aktPointerPos[0], pointerDisplayY(), aktPointerPos[2]);
    pointer3d->pushRenderItem(queue);
    queue.popTransform();

    if(Game::serverClient != NULL){
        foreach(ClientInfo *info, Game::serverClient->clientUsersList){
            if(info == NULL)
                continue;
            if(info->username == Game::serverClient->username)
                continue;
            queue.pushTransform();
            Mat4::translate(mv, mv, 2048*(info->X-camera->pozT[0])+info->x, info->y, 2048*(info->Z-camera->pozT[1])+info->z);
            info->pushRenderItem(queue, camera->getRotX());
            queue.popTransform();
        }
    }
}

void RouteEditorGLWidget::resizeGL(int w, int h) {
    //gluu->m_proj.setToIdentity();
    //gluu->m_proj.perspective(45.0f, GLfloat(w) / h, 0.01f, 100.0f);
}

void RouteEditorGLWidget::keyPressEvent(QKeyEvent * event) {
    Game::currentShapeLib = currentShapeLib;
    
    if (event->key() == Qt::Key_F10
            && (event->modifiers() & Qt::ControlModifier)
            && (event->modifiers() & Qt::ShiftModifier)) {
        TexLib::dumpStats("RouteEditorGLWidget");
        return;
    }

    if (event->key() == Qt::Key_QuoteLeft && !event->isAutoRepeat()) {
        toggleViewMode();
        event->accept();
        return;
    }
    if (currentViewMode == ViewMode::Map) {
        // The map takes navigation keys only; the 3D keys stay in 3D.
        camera->keyDown(event);
        event->accept();
        return;
    }
    
    if (liveFlexActive && event->key() == Qt::Key_Escape) {
        // Escape cancels only the unfinished continuous segment. Keep the
        // Flex tool armed so the next click can begin a separate line.
        finishLiveFlex(false, continuousFlexMode);
        event->accept();
        return;
    }

    if (liveRulerActive && event->key() == Qt::Key_Escape) {
        // Finish the accepted polyline, discard only its mouse-following
        // endpoint, and keep Ruler placement armed for a new line.
        finishLiveRuler(continuousRulerMode);
        event->accept();
        return;
    }

    if (liveTelepoleActive && event->key() == Qt::Key_Escape) {
        // Discard only the unfinished two-point span. The ordinary PLACE tool
        // and selected Telepole REF item remain active for another attempt.
        finishLiveTelepole(false);
        event->accept();
        return;
    }

    if (route == NULL) return;
    if (!route->loaded) return;

    if(liveFlexActive && event->key() == Qt::Key_F) {
        // The mouse-following preview can move while its own terrain edit is
        // being applied. Grade only the last section whose endpoint was
        // accepted by a click. Do not alter the vertical offset or rebuild
        // the unfinished mouse-following preview.
        if(!event->isAutoRepeat()) {
            if(continuousFlexMode && lastAcceptedFlexObj != NULL) {
                const bool independentUndo = Undo::StateBeginIndependent();
                route->setTerrainToTrackObj(
                        lastAcceptedFlexObj, defaultPaintBrush);
                if(independentUndo)
                    Undo::StateEndIndependent();
            }
            update();
        }
        event->accept();
        return;
    }

    camera->keyDown(event);

    Undo::StateBeginIfNotExist();

    switch (event->key()) {
        case Qt::Key_Control:
            moveStep = moveMaxStep / 10.0;
            keyControlEnabled = true;
            break;
        case Qt::Key_Shift:
            keyShiftEnabled = true;
            break;
        case Qt::Key_Alt:
            moveStep = moveMaxStep * 10.0;
            keyAltEnabled = true;
            break;
            //case Qt::Key_N:
            //if(selectedObj != NULL)
            //    route->deleteTDBTree(selectedObj);
            /*if(this->selectedObj != NULL){
                this->selectedObj->unselect();
                this->selectedObj = NULL;
            }
            this->selectedObj = route->makeFlexTrack((int)camera->pozT[0], (int)camera->pozT[1], aktPointerPos);
            if(this->selectedObj != NULL){
                this->selectedObj->select();
                lastNewObjPosT[0] = this->selectedObj->x;
                lastNewObjPosT[1] = this->selectedObj->y;
                lastNewObjPos[0] = this->selectedObj->position[0];
                lastNewObjPos[1] = this->selectedObj->position[1];
                lastNewObjPos[2] = this->selectedObj->position[2];
            }*/
            //    break;
        case Qt::Key_B:
            TerrainTileCreationDialog::showForTile(
                    this, route,
                    static_cast<int>(camera->pozT[0]),
                    static_cast<int>(camera->pozT[1]));
            break;
        //case Qt::Key_E:
        //    enableTool("selectTool");
        //    break;
        case Qt::Key_R:
            enableTool("selectTool");
            rotateTool = true;
            break;
        case Qt::Key_T:
            enableTool("selectTool");
            translateTool = true;
            break;
        case Qt::Key_Y:
            if(startLiveFlex())
                return;
            enableTool("selectTool");
            resizeTool = true;
            break;
        case Qt::Key_Q:
            if (keyControlEnabled)
                autoAddToTDB = !autoAddToTDB;
            else if (keyShiftEnabled)
                stickPointerToTerrain = !stickPointerToTerrain;
            else
                enableTool("placeTool");
            break;
        case Qt::Key_Home:
            aktPointerPos[1] += 40;
            jumpTo(camera->pozT, aktPointerPos);
            aktPointerPos[1] -= 40;
            break;
        default:
            break;
    }
    if (EditorTool *tool = activeTool())
        tool->key(*this, event);
}

void RouteEditorGLWidget::keyReleaseEvent(QKeyEvent * event) {
    Game::currentShapeLib = currentShapeLib;
    if (!route->loaded) return;
    camera->keyUp(event);
    switch (event->key()) {
            //case Qt::Key_Alt:
        case Qt::Key_Control:
            moveStep = moveMaxStep;
            keyControlEnabled = false;
            break;
        case Qt::Key_Shift:
            keyShiftEnabled = false;
            break;
        case Qt::Key_Alt:
            moveStep = moveMaxStep;
            keyAltEnabled = false;
            break;
        default:
            break;
    }
}

void RouteEditorGLWidget::mousePressEvent(QMouseEvent *event) {
    Game::currentShapeLib = currentShapeLib;
    bolckContextMenu = false;
    if (!route->loaded) return;
    m_lastPos = event->position();
    m_lastPos *= Game::PixelRatio;
    mouseClick = true;
    if (currentViewMode == ViewMode::Map) {
        // No tool works on the map yet: both buttons move the map.
        camera->MouseDown(event);
        setFocus();
        return;
    }
    if ((event->button()) == Qt::RightButton) {
        mouseRPressed = true;
        camera->MouseDown(event);
    }
    if ((event->button()) == Qt::LeftButton) {
        if(liveFlexActive) {
            const bool solutionValid = updateLiveFlex(
                    (int)camera->pozT[0],
                    (int)camera->pozT[1],
                    aktPointerPos,
                    true);
            if(solutionValid) {
                finishLiveFlex(true);
            } else if(continuousFlexMode) {
                // A failed solve ends the current chain without accepting a
                // stale preview. Reuse this click as the first point of a new
                // line, while keeping all earlier TDB/RDB segments intact.
                finishLiveFlex(false, true);
                Undo::StateBegin();
                float q[4];
                Quat::copy(q, placeRot);
                if(!placeContinuousFlexTrack(
                        (int)camera->pozT[0],
                        (int)camera->pozT[1],
                        aktPointerPos,
                        q,
                        true))
                    Undo::StateCancel();
            }
            mouseLPressed = false;
            mouseClick = false;
            setFocus();
            return;
        }
        if(liveRulerActive) {
            if(updateLiveRuler(
                    (int)camera->pozT[0],
                    (int)camera->pozT[1],
                    aktPointerPos,
                    true))
                acceptLiveRulerPoint();
            mouseLPressed = false;
            mouseClick = false;
            setFocus();
            return;
        }
        if(liveTelepoleActive) {
            const bool solutionValid = updateLiveTelepole(
                    (int)camera->pozT[0],
                    (int)camera->pozT[1],
                    aktPointerPos, true);
            finishLiveTelepole(solutionValid);
            mouseLPressed = false;
            mouseClick = false;
            setFocus();
            return;
        }
        Undo::StateBegin();
        mouseLPressed = true;
        lastMousePressTime = QDateTime::currentMSecsSinceEpoch();
        if (EditorTool *tool = activeTool()) {
            if (!tool->press(*this, ToolMouse{m_lastPos, m_lastPos})) {
                mouseLPressed = false;
                mouseClick = false;
                setFocus();
                return;
            }
        }
        if (toolEnabled == "") {
            camera->MouseDown(event);
        }
    }
    setFocus();
}

void RouteEditorGLWidget::wheelEvent(QWheelEvent *event) {
    float numDegrees = 0.01 * event->angleDelta().y();
    if (currentViewMode == ViewMode::Map) {
        const QPointF position = event->position() * Game::PixelRatio;
        cameraMap->zoomAt(float(position.x()), float(position.y()), numDegrees);
        update();
        event->accept();
        return;
    }

    if(continuousFlexMode || liveFlexActive
            || continuousRulerMode || liveRulerActive
            || liveTelepoleActive) {
        const float step = (event->modifiers() & Qt::ControlModifier)
                ? moveMaxStep / 10.0f
                : moveMaxStep;
        continuousPlacementYOffset += numDegrees * step;
        if(liveFlexActive) {
            // Wheel elevation is an explicit edit, so apply every step. GPU
            // cleanup remains safe because DynTrack defers it to rendering.
            liveFlexHasLastTarget = false;
            updateLiveFlex(
                    (int)camera->pozT[0],
                    (int)camera->pozT[1],
                    aktPointerPos,
                    true);
        } else if(liveRulerActive) {
            liveRulerHasLastTarget = false;
            updateLiveRuler(
                    (int)camera->pozT[0],
                    (int)camera->pozT[1],
                    aktPointerPos,
                    true);
        } else if(liveTelepoleActive) {
            liveTelepoleHasLastTarget = false;
            updateLiveTelepole(
                    (int)camera->pozT[0],
                    (int)camera->pozT[1],
                    aktPointerPos,
                    true);
        }
        update();
        event->accept();
        return;
    }

    if (EditorTool *tool = activeTool())
        tool->wheel(*this, numDegrees);
    event->accept();
}

void RouteEditorGLWidget::mouseReleaseEvent(QMouseEvent* event) {
    Game::currentShapeLib = currentShapeLib;
    if (!route->loaded) return;
    camera->MouseUp(event);
    if (currentViewMode == ViewMode::Map) {
        mouseClick = false;
        return;
    }
    if ((event->button()) == Qt::RightButton) {
        mouseRPressed = false;
        if(mouseClick && !bolckContextMenu)
            showContextMenu(event->pos());
    }
    if ((event->button()) == Qt::LeftButton) {
        mouseLPressed = false;
        if(!liveRulerActive && !liveTelepoleActive)
            Undo::StateEnd();
    }
    mouseClick = false;
    bolckContextMenu = false;
}

void RouteEditorGLWidget::mouseMoveEvent(QMouseEvent *event) {
    mouseClick = false;
    bolckContextMenu = false;
    Game::currentShapeLib = currentShapeLib;
    if (!route->loaded) return;
    /*int dx = event->x() - m_lastPos.x();
    int dy = event->y() - m_lastPos.y();

    if (event->buttons() & Qt::LeftButton) {

    } else if (event->buttons() & Qt::RightButton) {

    }*/
    mousex = event->position().x() * Game::PixelRatio;
    mousey = event->position().y() * Game::PixelRatio;
    if (currentViewMode == ViewMode::Map) {
        if (event->buttons() & (Qt::LeftButton | Qt::RightButton))
            camera->MouseMove(event);
        m_lastPos = event->position();
        m_lastPos *= Game::PixelRatio;
        return;
    }

    if(liveFlexActive || liveRulerActive || liveTelepoleActive) {
        if((event->buttons() & Qt::RightButton) == Qt::RightButton)
            camera->MouseMove(event);
        m_lastPos = event->position();
        m_lastPos *= Game::PixelRatio;
        return;
    }

    if ((event->buttons() & 2) == Qt::RightButton) {
        camera->MouseMove(event);
    }
    if ((event->buttons() & 1) == Qt::LeftButton) {
        if (mouseLPressed)
            if (EditorTool *tool = activeTool())
                tool->drag(*this, ToolMouse{QPointF(mousex, mousey), m_lastPos});
        if (toolEnabled == "") {
            camera->MouseMove(event);
        }
    }
    m_lastPos = event->position();
    m_lastPos *= Game::PixelRatio;
}

void RouteEditorGLWidget::toggleViewMode() {
    setViewMode(currentViewMode == ViewMode::Map ? ViewMode::Scene3D : ViewMode::Map);
}

void RouteEditorGLWidget::setViewMode(ViewMode mode) {
    if (mode == currentViewMode || route == NULL || !route->loaded || cameraMap == NULL)
        return;
    if (mode == ViewMode::Map) {
        // The map centres on the 3D pointer when it is near the camera, else
        // on the camera: a pointer on the horizon (the middle of a level
        // view) lies a kilometre or more away, and the map would open far
        // from where the user is.
        constexpr float PointerNearMetres = 500.0f;
        const float *eye = camera->getPos();
        const float dx = aktPointerPos[0] - eye[0];
        const float dz = aktPointerPos[2] - eye[2];
        const bool pointerNear = std::isfinite(dx) && std::isfinite(dz)
                && dx * dx + dz * dz < PointerNearMetres * PointerNearMetres;
        cameraMap->setPozT(int(camera->pozT[0]), int(camera->pozT[1]));
        cameraMap->setPos(pointerNear ? aktPointerPos[0] : eye[0], 0.0f,
                          pointerNear ? aktPointerPos[2] : eye[2]);
        toolBeforeMap = toolEnabled;
        currentViewMode = ViewMode::Map;
        if (!tools->allowed(toolEnabled, ViewMode::Map))
            enableTool("");
        camera = cameraMap;
        trackMap->invalidate();
        trackItemMap->invalidate();
        activityMap->invalidate();
    } else {
        // The 3D camera looks at the map pointer from behind and above, in
        // the map's heading.
        const float heading = cameraMap->view.heading;
        constexpr float Back = 60.0f, Above = 35.0f;
        currentViewMode = ViewMode::Scene3D;
        camera = cameraFree;
        cameraFree->setPozT(cameraMap->view.tileX, cameraMap->view.tileZ);
        cameraFree->setPos(aktPointerPos[0] - std::sin(heading) * Back, 0.0f,
                           aktPointerPos[2] - std::cos(heading) * Back);
        cameraFree->check_coords();
        const float *position = cameraFree->getPos();
        const float x = position[0], z = position[2];
        Game::terrainLib->load(int(cameraFree->pozT[0]), int(cameraFree->pozT[1]));
        const float ground = Game::terrainLib->getHeight(int(cameraFree->pozT[0]),
                                                         int(cameraFree->pozT[1]), x, z);
        cameraFree->setPos(x, ground + Above, z);
        cameraFree->setPlayerRot(heading, -std::atan2(Above, Back));
        if (!toolBeforeMap.isEmpty() && tools->allowed(toolBeforeMap, ViewMode::Scene3D))
            enableTool(toolBeforeMap);
    }
    emit sendMsg("viewMode", QString(mode == ViewMode::Map ? "map" : "3d"));
    update();
}

void RouteEditorGLWidget::setMapLayerVisible(MapLayer layer, bool visible) {
    mapLayers.set(layer, visible);
    update();
}

void RouteEditorGLWidget::updateMapPointer() {
    float x, z;
    cameraMap->view.groundAt(mousex, mousey, x, z);
    aktPointerPos[0] = x;
    aktPointerPos[1] = 0.0f;
    aktPointerPos[2] = z;
}

void RouteEditorGLWidget::paintMap() {
    if (renderer == NULL || gluu->shaders[MainRenderShaderName] == NULL)
        return;
    // Selection and the tools that would ask for it are 3D only for now.
    selection = false;
    const int width = qRound(float(this->width()) * Game::PixelRatio);
    const int height = qRound(float(this->height()) * Game::PixelRatio);
    cameraMap->setViewport(width, height);
    updateMapPointer();
    if (mapPaletteSetting != Game::mapPalette) {
        mapPalette = MapPalette::named(Game::mapPalette);
        mapPaletteSetting = Game::mapPalette;
    }
    const MapPalette &palette = mapPalette;

    RenderStats::ScopedFrame statsFrame(true);
    renderer->resetFrame();
    renderer->setViewPosition(camera->getPos());
    RenderQueue &queue = *renderer;
    Mat4::identity(renderer->transform());
    renderer->setShadowCasting(false);
    renderer->setLayer(RenderQueue::LAYER_OVERLAY);
    const MapView &view = cameraMap->view;
    trackMap->pushRenderItems(queue, view, palette, Game::trackDB, Game::roadDB, mapLayers);
    if (mapLayers.shows(MapLayer::TrackObjects))
        trackItemMap->pushRenderItems(queue, view, palette, route, Game::trackDB, Game::roadDB);
    activityMap->pushRenderItems(queue, view, palette, route, mapLayers.shows(MapLayer::Activity),
                                 mapLayers.shows(MapLayer::Paths));
    // The pointer: a square of a fixed screen size above everything.
    if (mapPointer == NULL)
        mapPointer = new OglObj();
    std::vector<float> square;
    float rx, rz, ux, uz;
    view.right(rx, rz);
    view.up(ux, uz);
    TrackMapLayer::appendSquare(square, aktPointerPos[0], TrackMapLayer::PointerHeight,
                                aktPointerPos[2], 9.0f * view.metresPerPixel, rx, rz, ux, uz);
    mapPointer->setMaterial(float(palette.pointer.redF()), float(palette.pointer.greenF()),
                            float(palette.pointer.blueF()));
    mapPointer->init(square.data(), int(square.size()), RenderItem::V, GL_TRIANGLES);
    if (mapLayers.shows(MapLayer::Pointer))
        mapPointer->pushRenderItem(queue);
    renderer->setLayer(RenderQueue::LAYER_SCENE);
    renderer->setShadowCasting(true);

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glActiveTexture(GL_TEXTURE0);
    gluu->currentShader = gluu->shaders[MainRenderShaderName];
    gluu->currentShader->bind();
    const float background[4] = {float(palette.background.redF()), float(palette.background.greenF()),
                                 float(palette.background.blueF()), 1.0f};
    renderer->clear(true, true, background);
    renderer->setViewport(0, 0, width, height);
    Mat4::identity(gluu->mvMatrix);
    Mat4::identity(renderer->transform());
    // The eye is kilometres above the map: no fog, no shadows.
    const float fogDensity = gluu->fogDensity;
    gluu->fogDensity = 0.0f;
    const int shadows = Game::shadowsEnabled;
    Game::shadowsEnabled = 0;
    Renderer::LayeredView mapView;
    mapView.view = camera->getMatrix();
    const MapView projectionView = view;
    mapView.projection = [projectionView](float, float, float *out) {
        projectionView.projection(out);
    };
    mapView.sceneFar = MapView::FarPlane;
    mapView.distantFar = MapView::FarPlane;
    mapView.water = false;
    renderer->beginViewBand(mapView, Renderer::BAND_SCENE);
    RenderStats::beginPhase(RenderStats::PhaseScene);
    renderer->renderPasses(Renderer::PASS_TERRAIN, Renderer::PASS_OVERLAY);
    RenderStats::endPhase(RenderStats::PhaseScene);
    renderer->endView(mapView);
    if (Game::viewCompass) {
        Mat4::identity(gluu->mvMatrix);
        Mat4::ortho(gluu->pMatrix, -1.0, 1.0, 1.0 - 2*(float(this->height()) / this->width()), 1.0, 0.0, 1.0);
        Mat4::identity(gluu->objStrMatrix);
        gluu->setMatrixUniforms();
        gluu->currentShader->setUniformValue(gluu->currentShader->lod, 0.0f);
        renderer->setLayer(RenderQueue::LAYER_UI);
        compass->pushRenderItem(queue, camera->getRotX()+M_PI);
        compassPointer->pushRenderItem(queue);
        renderer->setLayer(RenderQueue::LAYER_SCENE);
        renderer->renderPasses(Renderer::PASS_UI, Renderer::PASS_UI);
    }
    renderer->renderFrame();
    gluu->fogDensity = fogDensity;
    Game::shadowsEnabled = shadows;
    gluu->currentShader->release();
    if (this->isActiveWindow()) {
        emit this->posInfo(camera->getCurrentPos());
        emit this->pointerInfo(aktPointerPos);
    }
    drawEditorFpsHud();
}

EditorTool *RouteEditorGLWidget::activeTool() const {
    return tools->find(toolEnabled);
}

int RouteEditorGLWidget::tileX() const {
    return int(camera->pozT[0]);
}

int RouteEditorGLWidget::tileZ() const {
    return int(camera->pozT[1]);
}

float RouteEditorGLWidget::cameraHeading() const {
    return camera->getRotX();
}

ToolContext::ObjectEdit RouteEditorGLWidget::objectEdit() const {
    if (rotateTool)
        return ObjectEdit::Rotate;
    if (translateTool)
        return ObjectEdit::Translate;
    if (resizeTool)
        return ObjectEdit::Resize;
    return ObjectEdit::Select;
}

void RouteEditorGLWidget::setObjectEdit(ObjectEdit edit) {
    rotateTool = edit == ObjectEdit::Rotate;
    translateTool = edit == ObjectEdit::Translate;
    resizeTool = edit == ObjectEdit::Resize;
}

void RouteEditorGLWidget::rememberPlacement() {
    lastNewObjPosT[0] = camera->pozT[0];
    lastNewObjPosT[1] = camera->pozT[1];
    std::copy(aktPointerPos, aktPointerPos + 3, lastNewObjPos);
}

bool RouteEditorGLWidget::placeContinuousFlex(float *rotation) {
    return placeContinuousFlexTrack(tileX(), tileZ(), aktPointerPos, rotation, true);
}

bool RouteEditorGLWidget::placeContinuousRulerPoint(const float *rotation) {
    return placeContinuousRuler(tileX(), tileZ(), aktPointerPos, rotation);
}

void RouteEditorGLWidget::openMapTileWindow(Terrain *terrain) {
    terrain->getLowCornerTileXY(mapWindow->tileX, mapWindow->tileZ);
    mapWindow->tileSize = terrain->getSampleCount()*terrain->getSampleSize();
    mapWindow->exec();
}

void RouteEditorGLWidget::openImageryWindow(Terrain *terrain) {
    terrain->getLowCornerTileXY(imageryWindow->tileX, imageryWindow->tileZ);
    imageryWindow->terrainSize = terrain->getSampleCount()*terrain->getSampleSize();
    imageryWindow->distantTerrain = terrain->lowTile;
    imageryWindow->exec();
}

void RouteEditorGLWidget::sendFlexData() {
    emit flexData(tileX(), tileZ(), aktPointerPos);
}

void RouteEditorGLWidget::enableTool(QString name) {
    // A tool the view mode cannot use stays off (map mode, task editor 04).
    if (!tools->allowed(name, currentViewMode))
        return;
    EditorTool *previous = activeTool();
    EditorTool *next = tools->find(name);
    if (previous != nullptr && previous != next)
        previous->deactivate(*this);
    if(liveFlexActive
            && name != "liveFlexTool"
            && name != toolEnabled)
        finishLiveFlex(false);
    if(liveRulerActive
            && name != "continuousRulerTool"
            && name != toolEnabled)
        finishLiveRuler(false);
    if(liveTelepoleActive
            && name != "placeTool"
            && name != toolEnabled)
        finishLiveTelepole(false);
    const bool wasContinuousTool = continuousFlexMode
            || liveFlexActive
            || toolEnabled == "liveFlexTool"
            || continuousRulerMode
            || liveRulerActive;
    if(name == "continuousFlexTool") {
        continuousFlexMode = true;
        continuousFlexRoadMode = false;
        continuousRulerMode = false;
    } else if(name == "continuousFlexRoadTool") {
        continuousFlexMode = true;
        continuousFlexRoadMode = true;
        continuousRulerMode = false;
    } else if(name == "continuousRulerTool") {
        continuousFlexMode = false;
        continuousFlexRoadMode = false;
        continuousRulerMode = true;
    } else if(name != "liveFlexTool") {
        continuousFlexMode = false;
        continuousFlexRoadMode = false;
        continuousRulerMode = false;
    }
    if(wasContinuousTool
            && name != "continuousFlexTool"
            && name != "continuousFlexRoadTool"
            && name != "continuousRulerTool"
            && name != "liveFlexTool")
        continuousPlacementYOffset = 0.0f;
    qDebug() << name;
    toolEnabled = name;
    resizeTool = false;
    translateTool = false;
    rotateTool = false;
    if (next != nullptr && next != previous)
        next->activate(*this);
    emit sendMsg("toolEnabled", name);
}

void RouteEditorGLWidget::jumpTo(PreciseTileCoordinate* c) {
    jumpTo(c->TileX, -c->TileZ, c->wX, c->wY, -c->wZ);
}

void RouteEditorGLWidget::jumpTo(float *posT, float *pos) {
    int X = posT[0];
    int Z = posT[1];
    float x = pos[0];
    float z = pos[2];
    Game::check_coords(X, Z, x, z);
    jumpTo(X, Z, x, pos[1], z);
}

void RouteEditorGLWidget::jumpTo(int X, int Z, float x, float y, float z) {
    qDebug() << "jump: " << X << " " << Z;
    Game::terrainLib->load(X, Z);
    float h = Game::terrainLib->getHeight(X, Z, x, z);
    //if(h == -1)
        y = y + 10;
    if ((y < h) || (y > h + 100)) 
        y = h + 20;

    camera->setPozT(X, Z);
    camera->setPos(x, y, z);

}

void RouteEditorGLWidget::setDiagnosticView(int tileX, int tileZ,
        float x, float y, float z, float rotX, float rotY) {
    if (currentViewMode == ViewMode::Map) {
        currentViewMode = ViewMode::Scene3D;
        camera = cameraFree;
    }
    Game::terrainLib->load(tileX, tileZ);
    camera->setPozT(tileX, tileZ);
    camera->setPos(x, y, z);
    camera->setPlayerRot(rotX, rotY);
}

void RouteEditorGLWidget::setDiagnosticActivity(const QString &activity, const QString &path) {
    if (route == NULL)
        return;
    if (!activity.isEmpty()) {
        const int id = ActLib::GetAct(Game::root + "/ROUTES/" + Game::route + "/ACTIVITIES",
                                      activity);
        if (id >= 0 && ActLib::Act[id] != NULL)
            route->activitySelected(ActLib::Act[id]);
        else
            qWarning() << "diagnostic activity not found" << activity;
    }
    if (!path.isEmpty()) {
        bool found = false;
        for (Path *candidate : route->path) {
            if (candidate == NULL)
                continue;
            const bool match = candidate->name.compare(path, Qt::CaseInsensitive) == 0
                    || candidate->nameId.compare(path, Qt::CaseInsensitive) == 0;
            if (match)
                candidate->select();
            found = found || match;
        }
        if (!found)
            qWarning() << "diagnostic path not found" << path;
    }
}

void RouteEditorGLWidget::setDiagnosticMapView(int tileX, int tileZ, float x, float z,
        float metresPerPixel, float bearingDegrees) {
    if (cameraMap == NULL)
        return;
    cameraMap->setPozT(tileX, tileZ);
    cameraMap->setPos(x, 0.0f, z);
    cameraMap->view.metresPerPixel = metresPerPixel;
    cameraMap->view.heading = MapView::NorthUp - bearingDegrees * float(M_PI) / 180.0f;
    if (currentViewMode != ViewMode::Map) {
        toolBeforeMap = toolEnabled;
        currentViewMode = ViewMode::Map;
        if (!tools->allowed(toolEnabled, ViewMode::Map))
            enableTool("");
        camera = cameraMap;
    }
    trackMap->invalidate();
    trackItemMap->invalidate();
    activityMap->invalidate();
}

void RouteEditorGLWidget::diagnosticView(int &tileX, int &tileZ, float *pos,
        float &rotX, float &rotY) const {
    tileX = static_cast<int>(camera->pozT[0]);
    tileZ = static_cast<int>(camera->pozT[1]);
    const float *cameraPos = camera->getPos();
    pos[0] = cameraPos[0];
    pos[1] = cameraPos[1];
    pos[2] = cameraPos[2];
    rotX = camera->getRotX();
    rotY = camera->getRotY();
}

void RouteEditorGLWidget::setSimulationPaused(bool paused) {
    simulationPaused = paused;
}

QVector<quint32> RouteEditorGLWidget::probeSelectionIds(
        const QVector<QPoint> &devicePoints) {
    selectionProbePoints = devicePoints;
    selectionProbeResults.clear();
    selection = true;
    grabFramebuffer();
    // A failed selection pass leaves no results; never leave picking armed.
    selection = false;
    selectionProbePoints.clear();
    QVector<quint32> results;
    results.swap(selectionProbeResults);
    return results;
}

void RouteEditorGLWidget::objectSelected(GameObj* obj){
    if (selectedObj != NULL) {
        selectedObj->unselect();
    }
    if(obj == NULL){
        setSelectedObj(NULL);
        return;
    }
    obj->select();
    setSelectedObj(obj);
}

void RouteEditorGLWidget::objectSelected(QVector<GameObj*> obj){
    if (selectedObj != NULL) {
        selectedObj->unselect();
    }
    if(obj.size() == 0){
        setSelectedObj(NULL);
        return;
    }
    groupObj->clear();
    for(int i = 0; i < obj.size(); i++){
        groupObj->addObject((WorldObj*)obj[i]);
    }
    groupObj->select();
    setSelectedObj(groupObj);
}

void RouteEditorGLWidget::setPaintBrush(Brush* brush) {
    this->defaultPaintBrush = brush;
    Terrain::DefaultBrush = brush;
}

void RouteEditorGLWidget::setSelectedObj(GameObj* o) {
    if(liveFlexActive && o != liveFlexObj)
        finishLiveFlex(false);
    if(liveRulerActive && o != liveRulerObj)
        finishLiveRuler(false);
    if(liveTelepoleActive && o != liveTelepoleObj)
        finishLiveTelepole(false);
    selectedObj = o;
    Game::currentSelectedGameObj = selectedObj;
    emit showProperties(selectedObj);
    if (o != NULL)
        if (o->typeObj == o->worldobj)
           emit sendMsg("showShape", ((WorldObj*) o)->getShapePath());
}

bool RouteEditorGLWidget::startLiveFlex(bool reuseUndoState, bool deleteOnCancel,
        bool initialDirectionFromMouse) {
    if(liveFlexActive)
        return true;
    if(route == NULL || selectedObj == NULL)
        return false;
    if(selectedObj->typeObj != GameObj::worldobj)
        return false;

    WorldObj *worldObj = (WorldObj*)selectedObj;
    if(worldObj->typeID != WorldObj::dyntrack)
        return false;

    if((Game::trackDB != NULL && Game::trackDB->ifTrackExist(worldObj->x, worldObj->y, worldObj->UiD))
            || (Game::roadDB != NULL && Game::roadDB->ifTrackExist(worldObj->x, worldObj->y, worldObj->UiD))) {
        qWarning() << "Live Flex: DynTrack already exists in TDB";
        return false;
    }

    DynTrackObj *dynTrack = (DynTrackObj*)worldObj;
    if(dynTrack->sections == NULL)
        return false;

    enableTool(continuousFlexMode
            ? (dynTrack->isRoad()
                ? "continuousFlexRoadTool"
                : "continuousFlexTool")
            : "liveFlexTool");
    liveFlexObj = dynTrack;
    liveFlexStartTileX = dynTrack->x;
    liveFlexStartTileZ = dynTrack->y;
    Vec3::copy(liveFlexStartPosition, dynTrack->position);
    Quat::copy(liveFlexStartQ, dynTrack->qDirection);
    for(int i = 0; i < 5; i++) {
        liveFlexOriginalSections[i * 2] = dynTrack->sections[i].a;
        liveFlexOriginalSections[i * 2 + 1] = dynTrack->sections[i].r;
    }
    liveFlexHasLastTarget = false;
    liveFlexLastEndpointId = -2;
    liveFlexLastUpdateTime = 0;
    liveFlexDeleteOnCancel = deleteOnCancel;
    liveFlexInitialDirectionFromMouse = initialDirectionFromMouse;
    liveFlexSolutionValid = false;
    liveFlexCompanionsValid = true;

    if(continuousFlexMode && !createLiveFlexCompanions())
        return false;
    if(continuousFlexMode)
        applyContinuousFlexProfiles();

    liveFlexActive = true;

    if(!reuseUndoState) {
        Undo::StateBegin();
        Undo::PushGameObjData(dynTrack);
    }
    updateLiveFlex((int)camera->pozT[0], (int)camera->pozT[1], aktPointerPos);
    return true;
}

bool RouteEditorGLWidget::placeContinuousFlexTrack(
        int tileX,
        int tileZ,
        float *position,
        float *quaternion,
        bool initialMousePlacement) {
    if(route == NULL || position == NULL || quaternion == NULL)
        return false;

    if(initialMousePlacement)
        lastAcceptedFlexObj = NULL;

    Ref::RefItem dynTrackRef;
    dynTrackRef.type = "dyntrack";
    dynTrackRef.value = -1;
    dynTrackRef.staticFlags = continuousFlexRoadMode
            ? DynTrackObj::RoadStaticFlags
            : DynTrackObj::DefaultStaticFlags;
    dynTrackRef.description = continuousFlexRoadMode
            ? "Road Dynamic Track"
            : "Dynamic Track";

    float p[3];
    float q[4];
    Vec3::copy(p, position);
    if(initialMousePlacement)
        p[1] += continuousPlacementYOffset;
    Quat::copy(q, quaternion);
    DynTrackObj *dynTrack = (DynTrackObj*)route->placeObject(
            tileX, tileZ, p, q, 0, &dynTrackRef);
    if(dynTrack == NULL)
        return false;

    bool startsAtTrackEndpoint = false;
    if(initialMousePlacement) {
        TDB *database = continuousFlexRoadMode ? Game::roadDB : Game::trackDB;
        if(database != NULL) {
            int probeTileX = dynTrack->x;
            int probeTileZ = dynTrack->y;
            float probePosition[3];
            float probeQ[4];
            Vec3::copy(probePosition, dynTrack->position);
            Quat::copy(probeQ, dynTrack->qDirection);
            // Inspect the final placed pose so every placement snapping path
            // (including global stick-to-target) is classified consistently.
            startsAtTrackEndpoint = database->findNearestNode(
                    probeTileX,
                    probeTileZ,
                    probePosition,
                    probeQ,
                    0.1f,
                    false) >= 0;
        }
    }

    if(selectedObj != NULL)
        selectedObj->unselect();
    setSelectedObj(dynTrack);
    dynTrack->select();
    if(startLiveFlex(true, true,
            initialMousePlacement && !startsAtTrackEndpoint))
        return true;

    route->undoPlaceObj(dynTrack->x, dynTrack->y, dynTrack->UiD);
    setSelectedObj(NULL);
    return false;
}

DynTrackObj* RouteEditorGLWidget::placeRawDynTrack(
        int tileX,
        int tileZ,
        float *position,
        float *quaternion) {
    if(route == NULL || position == NULL || quaternion == NULL)
        return NULL;
    Tile *tile = route->requestTile(tileX, tileZ);
    if(tile == NULL || tile->loaded != 1)
        return NULL;

    Ref::RefItem dynTrackRef;
    dynTrackRef.type = "dyntrack";
    dynTrackRef.value = -1;
    const bool road = liveFlexObj != NULL && liveFlexObj->isRoad();
    dynTrackRef.staticFlags = road
            ? DynTrackObj::RoadStaticFlags
            : DynTrackObj::DefaultStaticFlags;
    dynTrackRef.description = road ? "Road Dynamic Track" : "Dynamic Track";
    DynTrackObj *track = (DynTrackObj*)tile->placeObject(
            position, quaternion, &dynTrackRef, NULL);
    if(track != NULL)
        Undo::PushWorldObjPlaced(track);
    return track;
}

bool RouteEditorGLWidget::createLiveFlexCompanions() {
    liveFlexCompanions.clear();
    liveFlexCompanionOffsets.clear();
    if(!continuousFlexMode)
        return true;

    QVector<float> offsets;
    if(continuousFlexLeftEnabled)
        offsets.push_back(-continuousFlexSeparation);
    if(continuousFlexRightEnabled)
        offsets.push_back(continuousFlexSeparation);

    for(float offset : offsets) {
        int tileX = 0;
        int tileZ = 0;
        float position[3] = {0, 0, 0};
        float quaternion[4] = {0, 0, 0, 1};
        if(!Flex::OffsetWorldPose(
                liveFlexStartTileX,
                liveFlexStartTileZ,
                liveFlexStartPosition,
                liveFlexStartQ,
                offset,
                tileX,
                tileZ,
                position,
                quaternion)) {
            discardLiveFlexCompanions();
            return false;
        }

        DynTrackObj *track = placeRawDynTrack(
                tileX, tileZ, position, quaternion);
        if(track == NULL) {
            discardLiveFlexCompanions();
            return false;
        }
        liveFlexCompanions.push_back(track);
        liveFlexCompanionOffsets.push_back(offset);
    }
    return true;
}

QString RouteEditorGLWidget::continuousFlexProfileForRole(
        const QString &role) const {
    QString base = continuousFlexProfile.trimmed();
    const bool road = liveFlexObj != NULL && liveFlexObj->isRoad();
    if(base.isEmpty() || role.isEmpty())
        return base;

    const OrtsTrackProfile::ObjectType profileType = road
            ? OrtsTrackProfile::ObjectType::Road
            : OrtsTrackProfile::ObjectType::Track;
    OrtsTrackProfileCatalog::load(Game::root + "/ROUTES/" + Game::route);
    const QSharedPointer<const OrtsTrackProfile> selectedProfile =
            OrtsTrackProfileCatalog::find(base, profileType);
    if(selectedProfile != nullptr){
        OrtsTrackProfile::ObjectRole objectRole =
                OrtsTrackProfile::ObjectRole::Main;
        if(role.compare("left", Qt::CaseInsensitive) == 0)
            objectRole = OrtsTrackProfile::ObjectRole::Left;
        else if(role.compare("middle", Qt::CaseInsensitive) == 0)
            objectRole = OrtsTrackProfile::ObjectRole::Middle;
        else if(role.compare("right", Qt::CaseInsensitive) == 0)
            objectRole = OrtsTrackProfile::ObjectRole::Right;
        const QSharedPointer<const OrtsTrackProfile> resolvedProfile =
                OrtsTrackProfileCatalog::findRole(
                    base, profileType, objectRole);
        return resolvedProfile == nullptr ? selectedProfile->id
                                          : resolvedProfile->id;
    }

    QString groupBase = base;
    if(groupBase.endsWith("_single", Qt::CaseInsensitive))
        groupBase.chop(QString("_single").size());
    const QString candidate = groupBase + "_" + role;

    ProceduralShape::Load();
    if(ProceduralShape::ShapeTemplateFile != NULL){
        QMapIterator<QString, ShapeTemplate*> iterator(
                ProceduralShape::ShapeTemplateFile->templates);
        while(iterator.hasNext()){
            iterator.next();
            if(iterator.value() != NULL
                    && iterator.value()->name.compare(
                            candidate, Qt::CaseInsensitive) == 0)
                return iterator.value()->name;
        }
    }

    // Generic profiles may not provide road-specific role variants. Reusing
    // the selected base profile is safer than saving a missing template name.
    return base;
}

void RouteEditorGLWidget::applyContinuousFlexProfiles() {
    if(liveFlexObj == NULL)
        return;

    QString mainRole;
    if(continuousFlexLeftEnabled && continuousFlexRightEnabled)
        mainRole = "middle";
    else if(continuousFlexLeftEnabled)
        mainRole = "right";
    else if(continuousFlexRightEnabled)
        mainRole = "left";
    liveFlexObj->setTemplate(continuousFlexProfileForRole(mainRole));

    for(int i = 0; i < liveFlexCompanions.size(); i++){
        DynTrackObj *companion = liveFlexCompanions[i];
        if(companion == NULL)
            continue;
        QString role;
        if(i < liveFlexCompanionOffsets.size())
            role = liveFlexCompanionOffsets[i] < 0 ? "left" : "right";
        companion->setTemplate(continuousFlexProfileForRole(role));
    }
}

void RouteEditorGLWidget::discardLiveFlexCompanions() {
    if(route != NULL) {
        for(DynTrackObj *track : liveFlexCompanions)
            if(track != NULL && track->UiD >= 0)
                route->undoPlaceObj(track->x, track->y, track->UiD);
    }
    liveFlexCompanions.clear();
    liveFlexCompanionOffsets.clear();
    liveFlexCompanionsValid = true;
}

bool RouteEditorGLWidget::updateLiveFlexCompanions(const float *mainSections) {
    if(mainSections == NULL || liveFlexCompanions.isEmpty()) {
        liveFlexCompanionsValid = true;
        return true;
    }

    float mainLength = 0.0f;
    for(int i = 0; i < 5; i++) {
        if((i % 2) == 0)
            mainLength += std::max(0.0f, mainSections[i * 2]);
        else
            mainLength += std::fabs(mainSections[i * 2])
                    * std::max(0.0f, mainSections[i * 2 + 1]);
    }
    if(!std::isfinite(mainLength))
        return liveFlexCompanionsValid = false;

    int mainEndTileX = 0;
    int mainEndTileZ = 0;
    float mainEndPosition[3] = {0, 0, 0};
    float mainEndQ[4] = {0, 0, 0, 1};
    if(!Flex::DyntrackEndpoint(
            liveFlexObj->x,
            liveFlexObj->y,
            liveFlexObj->position,
            liveFlexObj->qDirection,
            mainSections,
            mainEndTileX,
            mainEndTileZ,
            mainEndPosition,
            mainEndQ))
        return liveFlexCompanionsValid = false;

    struct CompanionPreview {
        float sections[10] = {0};
        float elevation = 0.0f;
    };
    QVector<CompanionPreview> previews;
    previews.reserve(liveFlexCompanions.size());
    for(int i = 0; i < liveFlexCompanions.size(); i++) {
        DynTrackObj *track = liveFlexCompanions[i];
        if(track == NULL)
            return liveFlexCompanionsValid = false;

        CompanionPreview preview;
        if(mainLength < 0.1f) {
            previews.push_back(preview);
            continue;
        }

        int endTileX = 0;
        int endTileZ = 0;
        float endPosition[3] = {0, 0, 0};
        float endQ[4] = {0, 0, 0, 1};
        if(!Flex::OffsetWorldPose(
                mainEndTileX,
                mainEndTileZ,
                mainEndPosition,
                mainEndQ,
                liveFlexCompanionOffsets[i],
                endTileX,
                endTileZ,
                endPosition,
                endQ))
            return liveFlexCompanionsValid = false;

        if(!Flex::ParallelDyntrackSections(
                mainSections,
                liveFlexCompanionOffsets[i],
                preview.sections))
            return liveFlexCompanionsValid = false;

        // Keep every companion's persisted endpoint on the same cross-road
        // plane. ORTS-profile rendering removes the rigid-object bank from
        // the generated cross-sections, so visual lane continuity no longer
        // requires corrupting inner/outer endpoint heights.
        if(!Flex::RigidElevationForEndpointHeight(
                    preview.sections,
                    endPosition[1] - track->position[1],
                    preview.elevation)) {
            return liveFlexCompanionsValid = false;
        }
        previews.push_back(preview);
    }

    // Keep all old companion meshes until every new companion solution is
    // ready, so a failed solve cannot leave a mixed left/right preview.
    for(int i = 0; i < liveFlexCompanions.size(); i++) {
        liveFlexCompanions[i]->set("dyntrackdata", previews[i].sections);
        liveFlexCompanions[i]->setElevation(previews[i].elevation);
    }

    liveFlexCompanionsValid = true;
    return true;
}

bool RouteEditorGLWidget::placeContinuousRuler(
        int tileX,
        int tileZ,
        const float *position,
        const float *quaternion) {
    if(route == NULL || position == NULL || quaternion == NULL)
        return false;

    Ref::RefItem rulerRef;
    rulerRef.type = "ruler";
    rulerRef.value = -1;
    rulerRef.description = "Ruler";
    if(!continuousRulerNodeShape.isEmpty())
        rulerRef.filename.push_back(continuousRulerNodeShape);

    int placedTileX = tileX;
    int placedTileZ = tileZ;
    float placedPosition[3] = {
        position[0],
        position[1] + continuousPlacementYOffset,
        position[2]
    };
    quantizeContinuousPoint(placedTileX, placedTileZ, placedPosition,
            Game::DefaultMoveStep);
    float placedQuaternion[4] = {
        quaternion[0], quaternion[1], quaternion[2], quaternion[3]
    };
    RulerObj *ruler = (RulerObj*)route->placeObject(
            placedTileX, placedTileZ, placedPosition, placedQuaternion,
            0, &rulerRef);
    if(ruler == NULL)
        return false;

    ruler->setTemplate(continuousRulerProfile);
    ruler->setNodeShape(continuousRulerNodeShape);
    if(!ruler->duplicateLastPoint()) {
        route->undoPlaceObj(ruler->x, ruler->y, ruler->UiD);
        return false;
    }

    if(selectedObj != NULL)
        selectedObj->unselect();
    setSelectedObj(ruler);
    ruler->select(ruler->pointCount() - 1);

    liveRulerObj = ruler;
    liveRulerActive = true;
    liveRulerHasCommittedSegment = false;
    liveRulerSolutionValid = false;
    liveRulerHasLastTarget = false;
    liveRulerLastUpdateTime = 0;
    liveRulerDraftTemplate = ruler->getTemplate();
    liveRulerDraftNodeShape = ruler->getNodeShape();
    updateLiveRuler(tileX, tileZ, position, true);
    return true;
}

bool RouteEditorGLWidget::updateLiveRuler(
        int pointerTileX,
        int pointerTileZ,
        const float *pointerPosition,
        bool force) {
    if(!liveRulerActive || liveRulerObj == NULL || pointerPosition == NULL)
        return false;
    for(int i = 0; i < 3; i++)
        if(!std::isfinite(pointerPosition[i])) {
            liveRulerSolutionValid = false;
            return false;
        }

    const unsigned long long now = QDateTime::currentMSecsSinceEpoch();
    if(!force && liveRulerLastUpdateTime != 0
            && now - liveRulerLastUpdateTime
                < LiveContinuousUpdateIntervalMs)
        return liveRulerSolutionValid;
    liveRulerLastUpdateTime = now;

    int targetTileX = pointerTileX;
    int targetTileZ = pointerTileZ;
    float targetPosition[3] = {
        pointerPosition[0],
        pointerPosition[1] + continuousPlacementYOffset,
        pointerPosition[2]
    };
    quantizeContinuousPoint(targetTileX, targetTileZ, targetPosition,
            Game::DefaultMoveStep);

    const bool sameTarget = liveRulerHasLastTarget
            && targetTileX == liveRulerLastTargetTileX
            && targetTileZ == liveRulerLastTargetTileZ
            && std::fabs(targetPosition[0]
                - liveRulerLastTargetPosition[0]) < 0.001f
            && std::fabs(targetPosition[1]
                - liveRulerLastTargetPosition[1]) < 0.001f
            && std::fabs(targetPosition[2]
                - liveRulerLastTargetPosition[2]) < 0.001f;
    if(sameTarget)
        return liveRulerSolutionValid;

    liveRulerHasLastTarget = true;
    liveRulerLastTargetTileX = targetTileX;
    liveRulerLastTargetTileZ = targetTileZ;
    Vec3::copy(liveRulerLastTargetPosition, targetPosition);
    if(!liveRulerObj->updateLastPoint(
            targetTileX, targetTileZ, targetPosition)) {
        liveRulerSolutionValid = false;
        return false;
    }

    constexpr float kMinimumRulerSegmentLength = 0.1f;
    const float segmentLength = liveRulerObj->lastSegmentLength();
    liveRulerSolutionValid = std::isfinite(segmentLength)
            && segmentLength >= kMinimumRulerSegmentLength;
    return liveRulerSolutionValid;
}

bool RouteEditorGLWidget::acceptLiveRulerPoint() {
    if(!liveRulerActive || liveRulerObj == NULL
            || !liveRulerSolutionValid)
        return false;

    // The current provisional endpoint is now committed. Close the undo
    // action for this segment, then snapshot the committed polyline before
    // appending the next mouse-following endpoint.
    liveRulerHasCommittedSegment = true;
    Undo::StateEnd();
    Undo::StateBegin();
    Undo::PushGameObjData(liveRulerObj);
    if(!liveRulerObj->duplicateLastPoint()) {
        Undo::StateCancel();
        return false;
    }
    liveRulerObj->select(liveRulerObj->pointCount() - 1);
    liveRulerDraftTemplate = liveRulerObj->getTemplate();
    liveRulerDraftNodeShape = liveRulerObj->getNodeShape();

    liveRulerSolutionValid = false;
    liveRulerHasLastTarget = false;
    liveRulerLastUpdateTime = 0;
    return true;
}

void RouteEditorGLWidget::finishLiveRuler(bool keepContinuousTool) {
    if(!liveRulerActive)
        return;

    RulerObj *ruler = liveRulerObj;
    const bool keepRuler = liveRulerHasCommittedSegment
            && ruler != NULL && ruler->pointCount() >= 2;
    const bool settingsChanged = keepRuler
            && (ruler->getTemplate() != liveRulerDraftTemplate
                || ruler->getNodeShape() != liveRulerDraftNodeShape);
    liveRulerActive = false;
    liveRulerObj = NULL;
    liveRulerHasCommittedSegment = false;
    liveRulerSolutionValid = false;
    liveRulerHasLastTarget = false;
    liveRulerLastUpdateTime = 0;
    liveRulerDraftTemplate.clear();
    liveRulerDraftNodeShape.clear();

    // The last point exists only for the mouse preview. Undo::StateCancel()
    // discards its snapshot but does not restore object data, so remove the
    // point explicitly before closing the draft transaction.
    if(ruler != NULL)
        ruler->removeLastPoint();
    if(!keepRuler && ruler != NULL) {
        route->undoPlaceObj(ruler->x, ruler->y, ruler->UiD);
        setSelectedObj(NULL);
    }
    if(settingsChanged)
        Undo::StateEnd();
    else
        Undo::StateCancel();

    if(keepContinuousTool && continuousRulerMode)
        enableTool("continuousRulerTool");
}

bool RouteEditorGLWidget::beginLiveTelepole(TelepoleObj *telepole) {
    if(telepole == NULL)
        return false;
    continuousPlacementYOffset = 0.0f;
    telepole->select(2);

    liveTelepoleObj = telepole;
    liveTelepoleActive = true;
    liveTelepoleSolutionValid = false;
    liveTelepoleHasLastTarget = false;
    liveTelepoleLastUpdateTime = 0;
    return true;
}

bool RouteEditorGLWidget::updateLiveTelepole(
        int pointerTileX, int pointerTileZ,
        const float *pointerPosition, bool force) {
    if(!liveTelepoleActive || liveTelepoleObj == NULL
            || pointerPosition == NULL)
        return false;
    for(int axis = 0; axis < 3; axis++){
        if(!std::isfinite(pointerPosition[axis])){
            liveTelepoleSolutionValid = false;
            return false;
        }
    }

    const unsigned long long now = QDateTime::currentMSecsSinceEpoch();
    if(!force && liveTelepoleLastUpdateTime != 0
            && now - liveTelepoleLastUpdateTime
                < LiveContinuousUpdateIntervalMs)
        return liveTelepoleSolutionValid;
    liveTelepoleLastUpdateTime = now;

    int targetTileX = pointerTileX;
    int targetTileZ = pointerTileZ;
    float targetPosition[3] = {
        pointerPosition[0],
        pointerPosition[1] + continuousPlacementYOffset,
        pointerPosition[2]
    };
    quantizeContinuousPoint(targetTileX, targetTileZ, targetPosition,
            Game::DefaultMoveStep);
    const bool sameTarget = liveTelepoleHasLastTarget
            && targetTileX == liveTelepoleLastTargetTileX
            && targetTileZ == liveTelepoleLastTargetTileZ
            && std::fabs(targetPosition[0]
                - liveTelepoleLastTargetPosition[0]) < 0.001f
            && std::fabs(targetPosition[1]
                - liveTelepoleLastTargetPosition[1]) < 0.001f
            && std::fabs(targetPosition[2]
                - liveTelepoleLastTargetPosition[2]) < 0.001f;
    if(sameTarget)
        return liveTelepoleSolutionValid;

    liveTelepoleHasLastTarget = true;
    liveTelepoleLastTargetTileX = targetTileX;
    liveTelepoleLastTargetTileZ = targetTileZ;
    Vec3::copy(liveTelepoleLastTargetPosition, targetPosition);
    if(!liveTelepoleObj->setEndPosition(
            targetTileX, targetTileZ, targetPosition)){
        liveTelepoleSolutionValid = false;
        return false;
    }
    liveTelepoleSolutionValid = liveTelepoleObj->spanLength() >= 0.1f;
    return liveTelepoleSolutionValid;
}

void RouteEditorGLWidget::finishLiveTelepole(bool accept) {
    if(!liveTelepoleActive)
        return;
    TelepoleObj *telepole = liveTelepoleObj;
    liveTelepoleActive = false;
    liveTelepoleObj = NULL;
    liveTelepoleSolutionValid = false;
    liveTelepoleHasLastTarget = false;
    liveTelepoleLastUpdateTime = 0;

    if(accept && telepole != NULL){
        telepole->select(0);
        Undo::StateEnd();
    } else {
        if(telepole != NULL){
            route->undoPlaceObj(telepole->x, telepole->y, telepole->UiD);
            if(selectedObj == telepole)
                setSelectedObj(NULL);
        }
        Undo::StateCancel();
    }

}

void RouteEditorGLWidget::quantizeContinuousPoint(int &tileX, int &tileZ, float *position, float step) {
    if(position == NULL)
        return;
    step = std::max(0.01f, std::fabs(step));

    double absoluteX = tileX * 2048.0 + (double)position[0];
    double absoluteZ = tileZ * 2048.0 + (double)position[2];
    absoluteX = std::round(absoluteX / (double)step) * (double)step;
    absoluteZ = std::round(absoluteZ / (double)step) * (double)step;

    tileX = (int)std::floor((absoluteX + 1024.0) / 2048.0);
    tileZ = (int)std::floor((absoluteZ + 1024.0) / 2048.0);
    position[0] = (float)(absoluteX - tileX * 2048.0);
    position[2] = (float)(absoluteZ - tileZ * 2048.0);
}

float RouteEditorGLWidget::effectiveContinuousFlexMinimumRadius() const {
    float radius = std::max(5.0f, continuousFlexMinimumRadius);
    if(continuousFlexMode
            && (continuousFlexLeftEnabled || continuousFlexRightEnabled)) {
        // An inside companion has its separation subtracted from the main
        // radius. Retain a margin above the generator's 0.1 m validity floor.
        radius = std::max(radius,
                std::fabs(continuousFlexSeparation) + 0.25f);
    }
    return radius;
}

bool RouteEditorGLWidget::updateLiveFlex(
        int pointerTileX,
        int pointerTileZ,
        const float *pointerPosition,
        bool force) {
    if(!liveFlexActive || liveFlexObj == NULL || pointerPosition == NULL)
        return false;
    for(int i = 0; i < 3; i++)
        if(!std::isfinite(pointerPosition[i])) {
            liveFlexSolutionValid = false;
            return false;
        }

    // Rendering remains independent; only endpoint search, solving, and mesh
    // invalidation are limited to 20 Hz.
    constexpr float kLiveFlexSnapRadius = 1.0f;
    const unsigned long long now = QDateTime::currentMSecsSinceEpoch();
    if(!force && liveFlexLastUpdateTime != 0
            && now - liveFlexLastUpdateTime < LiveContinuousUpdateIntervalMs)
        return liveFlexSolutionValid;
    liveFlexLastUpdateTime = now;

    const int rawTargetTileX = pointerTileX;
    const int rawTargetTileZ = pointerTileZ;
    float rawTargetPosition[3] = {
        pointerPosition[0], pointerPosition[1] + continuousPlacementYOffset, pointerPosition[2]
    };
    int targetTileX = pointerTileX;
    int targetTileZ = pointerTileZ;
    float targetPosition[3] = {
        pointerPosition[0], pointerPosition[1] + continuousPlacementYOffset, pointerPosition[2]
    };
    float endpointQ[4] = {0, 0, 0, 1};
    int endpointId = -1;

    TDB *database = liveFlexObj->isRoad()
            ? Game::roadDB : Game::trackDB;
    if(database != NULL) {
        endpointId = database->findNearestNode(
                targetTileX,
                targetTileZ,
                targetPosition,
                endpointQ,
                kLiveFlexSnapRadius,
                true);
    }

    if(endpointId >= 0) {
        const float startDx = (targetTileX - liveFlexStartTileX) * 2048.0f
                + targetPosition[0] - liveFlexStartPosition[0];
        const float startDy = targetPosition[1] - liveFlexStartPosition[1];
        const float startDz = (targetTileZ - liveFlexStartTileZ) * 2048.0f
                + targetPosition[2] - liveFlexStartPosition[2];
        if(startDx * startDx + startDy * startDy + startDz * startDz < 0.01f) {
            // Do not pose-to-pose solve against the endpoint on which this
            // segment starts. Besides snapping the pointer backwards, that
            // degenerate solve can be substantially more expensive.
            endpointId = -1;
            targetTileX = rawTargetTileX;
            targetTileZ = rawTargetTileZ;
            Vec3::copy(targetPosition, rawTargetPosition);
        }
    }

    if(endpointId < 0)
        quantizeContinuousPoint(targetTileX, targetTileZ, targetPosition, Game::DefaultMoveStep);

    const bool sameHorizontalTarget = liveFlexHasLastTarget
            && endpointId == liveFlexLastEndpointId
            && targetTileX == liveFlexLastTargetTileX
            && targetTileZ == liveFlexLastTargetTileZ
            && std::fabs(targetPosition[0] - liveFlexLastTargetPosition[0]) < 0.001f
            && std::fabs(targetPosition[2] - liveFlexLastTargetPosition[2]) < 0.001f;
    // A free pointer's XZ cell is the preview identity. Ignore small raw
    // terrain-height differences until the pointer enters another grid cell.
    const bool sameTarget = sameHorizontalTarget
            && (endpointId < 0
                || std::fabs(targetPosition[1] - liveFlexLastTargetPosition[1]) < 0.001f);
    if(sameTarget)
        return liveFlexSolutionValid;

    liveFlexHasLastTarget = true;
    liveFlexLastEndpointId = endpointId;
    liveFlexLastTargetTileX = targetTileX;
    liveFlexLastTargetTileZ = targetTileZ;
    Vec3::copy(liveFlexLastTargetPosition, targetPosition);
    liveFlexSolutionValid = false;

    float dyntrackData[10] = {0};
    if(liveFlexInitialDirectionFromMouse) {
        const double dx = (targetTileX - liveFlexStartTileX) * 2048.0
                + (double)targetPosition[0]
                - (double)liveFlexStartPosition[0];
        const double dz = (targetTileZ - liveFlexStartTileZ) * 2048.0
                + (double)targetPosition[2]
                - (double)liveFlexStartPosition[2];
        const double horizontalDistance = std::hypot(dx, dz);
        if(!std::isfinite(horizontalDistance))
            return false;

        if(horizontalDistance < 0.1) {
            liveFlexObj->set("dyntrackdata", dyntrackData);
            updateLiveFlexCompanions(dyntrackData);
            return false;
        }

        const float startYaw = (float)std::atan2(dx, -dz);
        int planarTargetTileX = 0;
        int planarTargetTileZ = 0;
        float planarTargetPosition[3] = {0, 0, 0};
        float elevation = 0.0f;
        if(!Flex::ElevatedPlanarTarget(
                liveFlexStartTileX,
                liveFlexStartTileZ,
                liveFlexStartPosition,
                startYaw,
                targetTileX,
                targetTileZ,
                targetPosition,
                planarTargetTileX,
                planarTargetTileZ,
                planarTargetPosition,
                elevation))
            return false;
        const double planarDx =
                (planarTargetTileX - liveFlexStartTileX) * 2048.0
                + (double)planarTargetPosition[0]
                - (double)liveFlexStartPosition[0];
        const double planarDz =
                (planarTargetTileZ - liveFlexStartTileZ) * 2048.0
                + (double)planarTargetPosition[2]
                - (double)liveFlexStartPosition[2];
        const double planarDistance = std::hypot(planarDx, planarDz);
        if(!std::isfinite(planarDistance))
            return false;

        float startQ[4];
        Quat::fill(startQ);
        Quat::rotateY(startQ, startQ, -startYaw);
        liveFlexObj->setQdirection(startQ);
        liveFlexObj->setMartix();

        dyntrackData[0] = (float)std::min(2048.0, planarDistance);
        liveFlexObj->set("dyntrackdata", dyntrackData);
        liveFlexObj->setElevation(elevation);

        // Companion starts were provisionally created on the first click,
        // before the second point established this segment's direction.
        for(int i = 0; i < liveFlexCompanions.size(); i++) {
            DynTrackObj *track = liveFlexCompanions[i];
            if(track == NULL)
                continue;
            int companionTileX = 0;
            int companionTileZ = 0;
            float companionPosition[3] = {0, 0, 0};
            float companionQ[4] = {0, 0, 0, 1};
            if(!Flex::OffsetWorldPose(
                    liveFlexStartTileX,
                    liveFlexStartTileZ,
                    liveFlexStartPosition,
                    liveFlexObj->qDirection,
                    liveFlexCompanionOffsets[i],
                    companionTileX,
                    companionTileZ,
                    companionPosition,
                    companionQ)) {
                liveFlexCompanionsValid = false;
                return false;
            }
            track->setPosition(
                    companionTileX, companionTileZ, companionPosition);
            track->setQdirection(companionQ);
            track->setMartix();
        }
        liveFlexSolutionValid = updateLiveFlexCompanions(dyntrackData);
        return liveFlexSolutionValid;
    }

    const float startYaw = Flex::TdbYawFromTrackQuaternion(liveFlexStartQ);
    const float minimumCurveRadius = effectiveContinuousFlexMinimumRadius();
    int planarTargetTileX = 0;
    int planarTargetTileZ = 0;
    float planarTargetPosition[3] = {0, 0, 0};
    float elevation = 0.0f;
    if(!Flex::ElevatedPlanarTarget(
            liveFlexStartTileX,
            liveFlexStartTileZ,
            liveFlexStartPosition,
            startYaw,
            targetTileX,
            targetTileZ,
            targetPosition,
            planarTargetTileX,
            planarTargetTileZ,
            planarTargetPosition,
            elevation))
        return false;
    bool success = false;

    if(endpointId >= 0) {
        float startQ[4] = {0, startYaw, 0, 1};
        endpointQ[1] = std::fmod(endpointQ[1] + (float)M_PI, 2.0f * (float)M_PI);
        if(endpointQ[1] > (float)M_PI)
            endpointQ[1] -= 2.0f * (float)M_PI;
        success = Flex::NewFlex(
                liveFlexStartTileX,
                liveFlexStartTileZ,
                liveFlexStartPosition,
                startQ,
                planarTargetTileX,
                planarTargetTileZ,
                planarTargetPosition,
                endpointQ,
                dyntrackData,
                0.0f,
                false,
                minimumCurveRadius);
    } else {
        // World X/Z quantization can miss an arbitrarily rotated track axis
        // by up to half a square grid cell's diagonal. Treat that displacement
        // as straight so the mouse can reach the zero-angle state.
        constexpr float kHalfGridDiagonalFactor = 0.70710678118f;
        const float straightEndpointTolerance = std::max(
                0.05f,
                std::fabs(Game::DefaultMoveStep) * kHalfGridDiagonalFactor);
        success = Flex::NewFlexToPoint(
                liveFlexStartTileX,
                liveFlexStartTileZ,
                liveFlexStartPosition,
                startYaw,
                planarTargetTileX,
                planarTargetTileZ,
                planarTargetPosition,
                dyntrackData,
                minimumCurveRadius,
                straightEndpointTolerance);
    }

    if(!success)
        return false;
    for(int i = 0; i < 10; i++)
        if(!std::isfinite(dyntrackData[i]))
            return false;
    if(std::fabs(elevation) > 1e-6f
            && !Flex::CanUseRigidElevation(dyntrackData))
        return false;

    float centerlineLength = 0.0f;
    for(int i = 0; i < 5; i++) {
        if((i % 2) == 0)
            centerlineLength += std::max(0.0f, dyntrackData[i * 2]);
        else
            centerlineLength += std::fabs(dyntrackData[i * 2])
                    * std::max(0.0f, dyntrackData[i * 2 + 1]);
    }
    if(!std::isfinite(centerlineLength) || centerlineLength < 0.1f)
        return false;

    liveFlexObj->set("dyntrackdata", dyntrackData);
    liveFlexObj->setElevation(elevation);
    liveFlexSolutionValid = updateLiveFlexCompanions(dyntrackData);
    return liveFlexSolutionValid;
}

void RouteEditorGLWidget::finishLiveFlex(bool accept, bool keepContinuousTool) {
    if(!liveFlexActive)
        return;

    DynTrackObj *dynTrack = liveFlexObj;
    const bool continuePlacement = accept && continuousFlexMode;
    if(continuePlacement && !liveFlexCompanionsValid) {
        qWarning() << "Continuous Flex: companion track geometry is invalid";
        return;
    }
    if(continuePlacement && dynTrack != NULL) {
        float centerlineLength = 0.0f;
        for(int i = 0; i < 5; i++) {
            if(dynTrack->sections[i].sectIdx > 1000000)
                continue;
            if((i % 2) == 0)
                centerlineLength += std::max(0.0f, dynTrack->sections[i].a);
            else
                centerlineLength += std::fabs(dynTrack->sections[i].a)
                        * std::max(0.0f, dynTrack->sections[i].r);
        }
        if(!std::isfinite(centerlineLength) || centerlineLength < 0.1f) {
            qWarning() << "Continuous Flex: segment is too short or invalid";
            return;
        }
    }
    const bool deleteOnCancel = liveFlexDeleteOnCancel;
    const QVector<DynTrackObj*> acceptedCompanions = liveFlexCompanions;
    liveFlexActive = false;
    liveFlexObj = NULL;
    liveFlexDeleteOnCancel = false;
    liveFlexInitialDirectionFromMouse = false;
    liveFlexSolutionValid = false;
    liveFlexHasLastTarget = false;
    liveFlexLastEndpointId = -2;
    liveFlexLastUpdateTime = 0;

    if(!accept) {
        discardLiveFlexCompanions();
        if(deleteOnCancel && dynTrack != NULL) {
            route->undoPlaceObj(dynTrack->x, dynTrack->y, dynTrack->UiD);
            setSelectedObj(NULL);
        } else if(dynTrack != NULL) {
            dynTrack->set("dyntrackdata", liveFlexOriginalSections);
            dynTrack->setPosition(liveFlexStartPosition);
            dynTrack->setQdirection(liveFlexStartQ);
            dynTrack->setMartix();
            dynTrack->setModified();
        }
        Undo::StateCancel();
    }

    int nextTileX = 0;
    int nextTileZ = 0;
    float nextPosition[3] = {0, 0, 0};
    float nextQuaternion[4] = {0, 0, 0, 1};
    bool hasNextPose = false;
    if(continuePlacement && dynTrack != NULL) {
        // Match the existing DynTrack Z operation before deriving the next
        // start. TDB placement may normalize the accepted object's pose.
        route->addToTDB(dynTrack);
        for(DynTrackObj *track : acceptedCompanions)
            if(track != NULL)
                route->addToTDB(track);
        Undo::StateEnd();
        lastAcceptedFlexObj = dynTrack;
        liveFlexCompanions.clear();
        liveFlexCompanionOffsets.clear();
        liveFlexCompanionsValid = true;

        float currentSections[10];
        for(int i = 0; i < 5; i++) {
            currentSections[i * 2] = dynTrack->sections[i].a;
            currentSections[i * 2 + 1] = dynTrack->sections[i].r;
        }
        hasNextPose = Flex::DyntrackEndpoint(
                dynTrack->x,
                dynTrack->y,
                dynTrack->position,
                dynTrack->qDirection,
                currentSections,
                nextTileX,
                nextTileZ,
                nextPosition,
                nextQuaternion);
    }

    if(accept && !continuePlacement)
        Undo::StateEnd();

    if(continuePlacement && hasNextPose) {
        if(dynTrack != NULL)
            dynTrack->unselect();
        Undo::StateBegin();
        if(placeContinuousFlexTrack(
                nextTileX,
                nextTileZ,
                nextPosition,
                nextQuaternion))
            return;
        Undo::StateCancel();
    }
    if(!accept && keepContinuousTool && continuousFlexMode) {
        enableTool(continuousFlexRoadMode
                ? "continuousFlexRoadTool"
                : "continuousFlexTool");
        return;
    }
    enableTool("selectTool");
}

void RouteEditorGLWidget::editCopy() {
    if (toolEnabled == "selectTool" || toolEnabled == "placeTool") {
        if (selectedObj != NULL) {
            if (selectedObj->typeObj == GameObj::worldobj) {
                WorldObj *selectedWorldObj = (WorldObj*) selectedObj;
                if (selectedWorldObj->typeID == WorldObj::groupobject) {
                    delete copyPasteGroupObj;
                    copyPasteGroupObj = new GroupObj(*(GroupObj*)selectedWorldObj);
                    copyPasteObj = copyPasteGroupObj;
                } else {
                    copyPasteObj = selectedWorldObj;
                }
            }
        }
    }
}

void RouteEditorGLWidget::editPaste() {
    qDebug() << "EditPaste Start";
    Undo::StateBeginIfNotExist();
    if (toolEnabled == "selectTool" || toolEnabled == "placeTool") {
        if (copyPasteObj != NULL) {
            //qDebug() << "EditPaste selectedObj->unselect";
            if (selectedObj != NULL)
                selectedObj->unselect();
            lastNewObjPosT[0] = camera->pozT[0];
            lastNewObjPosT[1] = camera->pozT[1];
            lastNewObjPos[0] = aktPointerPos[0];
            lastNewObjPos[1] = aktPointerPos[1];
            lastNewObjPos[2] = aktPointerPos[2];
            //qDebug() << "EditPaste copyPasteObj->typeObj";
            if(copyPasteObj->typeObj == GameObj::worldobj) {
               // qDebug() << "EditPaste copyPasteObj->typeID";
                if (copyPasteObj->typeID == WorldObj::groupobject) {
                    //qDebug() << "EditPaste groupobject";
                    groupObj->fromNewObjects((GroupObj*) copyPasteObj, route, (int) camera->pozT[0], (int) camera->pozT[1], aktPointerPos);
                    //qDebug() << "EditPaste setSelectedObj";
                    setSelectedObj(groupObj);
                } else {
                    //qDebug() << "EditPaste object";
                    float *q = Quat::create();
                    Quat::copy(q, copyPasteObj->qDirection);
                    setSelectedObj(route->placeObject((int) camera->pozT[0], (int) camera->pozT[1], aktPointerPos, q, 0, copyPasteObj->getRefInfo()));
                    //qDebug() << "EditPaste setSelectedObj";
                    if (selectedObj != NULL)
                        selectedObj->select();
                }
            }
        }
    }
    qDebug() << "EditPaste End";
}

void RouteEditorGLWidget::editSelect() {
    enableTool("selectTool");
}

void RouteEditorGLWidget::selectToolresetMoveStep(){
    Game::DefaultMoveStep = defaultMoveStep;
    moveStep = defaultMoveStep;
    moveMaxStep = defaultMoveStep;
}

void RouteEditorGLWidget::selectToolresetRot(){
    Quat::fill(this->placeRot);
    placeElev = 0;
}

void RouteEditorGLWidget::editFind1x1() {
    editFind(0);
}

void RouteEditorGLWidget::editFind3x3() {
    editFind(1);
}

void RouteEditorGLWidget::editFind(int radius) {
    if (selectedObj != NULL)
        if (selectedObj->typeObj == GameObj::worldobj){
            selectedObj->unselect();
            route->findSimilar((WorldObj*)selectedObj, groupObj, camera->pozT, radius);
            setSelectedObj(groupObj);
            if (groupObj->count() == 0) {
                groupObj->unselect();
                setSelectedObj(NULL);
            }
        }
}

void RouteEditorGLWidget::editUndo() {
    if(liveFlexActive)
        finishLiveFlex(false, continuousFlexMode);
    if(liveRulerActive) {
        finishLiveRuler(continuousRulerMode);
        setSelectedObj(NULL);
    }
    if(liveTelepoleActive) {
        finishLiveTelepole(false);
        setSelectedObj(NULL);
    }
    Undo::UndoLast();
}

void RouteEditorGLWidget::showTrkEditr() {
    if (route != NULL)
        route->showTrkEditr();
}

void RouteEditorGLWidget::setTerrainToObj(){
    Undo::StateBegin();
    if (selectedObj != NULL)
        route->setTerrainToTrackObj((WorldObj*)selectedObj, defaultPaintBrush);
    else
        route->setTerrainToTrackObj((WorldObj*)lastSelectedObj, defaultPaintBrush);
    Undo::StateEnd();
}

void RouteEditorGLWidget::adjustObjPositionToTerrainMenu(){
    Undo::StateBeginIfNotExist();
    Undo::PushGameObjData(selectedObj);
    if (selectedObj != NULL)
        if(selectedObj->typeObj == GameObj::worldobj)
            ((WorldObj*)selectedObj)->adjustPositionToTerrain();
}

void RouteEditorGLWidget::adjustObjRotationToTerrainMenu(){
    Undo::StateBeginIfNotExist();
    Undo::PushGameObjData(selectedObj);
    if (selectedObj != NULL)
        if(selectedObj->typeObj == GameObj::worldobj)
            ((WorldObj*)selectedObj)->adjustRotationToTerrain();
}

void RouteEditorGLWidget::pickObjForPlacement(){
    if (selectedObj != NULL) {
        if(selectedObj->typeObj == GameObj::worldobj){
            Quat::copy(this->placeRot, ((WorldObj*)selectedObj)->qDirection);
            route->ref->selected = ((WorldObj*)selectedObj)->getRefInfo();
            emit itemSelected(route->ref->selected);
        }
    }
}

void RouteEditorGLWidget::pickObjRotForPlacement(){
    if (selectedObj != NULL) {
        if(selectedObj->typeObj == GameObj::worldobj){
            placeElev = 0;
            Quat::copy(this->placeRot, ((WorldObj*)selectedObj)->qDirection);
        }
    }
}

void RouteEditorGLWidget::pickObjRotElevForPlacement(){
    if (selectedObj != NULL) {
        if(selectedObj->typeObj == GameObj::worldobj){
            Quat::fill(this->placeRot);
            placeElev = ((WorldObj*)selectedObj)->getElevation();
        }
    }
}

void RouteEditorGLWidget::showContextMenu(const QPoint & point) {
    if(defaultMenuActions["undo"] == NULL){
        defaultMenuActions["undo"] = new QAction(
            //% "&Undo"
            qtTrId("route.editor.route.editor.glwidget.action.undo"), this);
        QObject::connect(defaultMenuActions["undo"], SIGNAL(triggered()), this, SLOT(editUndo()));
    }
    if(defaultMenuActions["copy"] == NULL){
        defaultMenuActions["copy"] = new QAction(
            //% "&Copy"
            qtTrId("route.editor.route.editor.glwidget.action.copy"), this);
        QObject::connect(defaultMenuActions["copy"], SIGNAL(triggered()), this, SLOT(editCopy()));
    }
    if(defaultMenuActions["paste"] == NULL){
        defaultMenuActions["paste"] = new QAction(
            //% "&Paste"
            qtTrId("route.editor.route.editor.glwidget.action.paste"), this);
        QObject::connect(defaultMenuActions["paste"], SIGNAL(triggered()), this, SLOT(editPaste()));
    }
    if(defaultMenuActions["find1x1"] == NULL){
        defaultMenuActions["find1x1"] = new QAction(
            //% "&Select Similar 1x1"
            qtTrId("route.editor.route.editor.glwidget.action.select.similar.1x1"), this);
        QObject::connect(defaultMenuActions["find1x1"], SIGNAL(triggered()), this, SLOT(editFind1x1()));
    }
    if(defaultMenuActions["find3x3"] == NULL){
        defaultMenuActions["find3x3"] = new QAction(
            //% "&Select Similar 3x3"
            qtTrId("route.editor.route.editor.glwidget.action.select.similar.3x3"), this);
        QObject::connect(defaultMenuActions["find3x3"], SIGNAL(triggered()), this, SLOT(editFind3x3()));
    }
    if(defaultMenuActions["select"] == NULL){
        defaultMenuActions["select"] = new QAction(
            //% "&Select Tool"
            qtTrId("route.editor.route.editor.glwidget.action.select.tool"), this);
        QObject::connect(defaultMenuActions["select"], SIGNAL(triggered()), this, SLOT(editSelect()));
    }
    if(defaultMenuActions["setTerrToObj"] == NULL){
        defaultMenuActions["setTerrToObj"] = new QAction(
            //% "&Set Terrain to Object"
            qtTrId("route.editor.route.editor.glwidget.action.set.terrain.object"));
        QObject::connect(defaultMenuActions["setTerrToObj"], SIGNAL(triggered()), this, SLOT(setTerrainToObj()));
    }
    if(defaultMenuActions["setPosToTerr"] == NULL){
        defaultMenuActions["setPosToTerr"] = new QAction(
            //% "&Set position to Terrain"
            qtTrId("route.editor.route.editor.glwidget.action.set.position.terrain"));
        QObject::connect(defaultMenuActions["setPosToTerr"], SIGNAL(triggered()), this, SLOT(adjustObjPositionToTerrainMenu()));
    }
    if(defaultMenuActions["setRotToTerr"] == NULL){
        defaultMenuActions["setRotToTerr"] = new QAction(
            //% "&Set rotation to Terrain"
            qtTrId("route.editor.route.editor.glwidget.action.set.rotation.terrain"));
        QObject::connect(defaultMenuActions["setRotToTerr"], SIGNAL(triggered()), this, SLOT(adjustObjRotationToTerrainMenu()));
    }
    if(defaultMenuActions["pickObj"] == NULL){
        defaultMenuActions["pickObj"] = new QAction(
            //% "&Pick for placement"
            qtTrId("route.editor.route.editor.glwidget.action.pick.for.placement"));
        QObject::connect(defaultMenuActions["pickObj"], SIGNAL(triggered()), this, SLOT(pickObjForPlacement()));
    }
    if(defaultMenuActions["pickObjRot"] == NULL){
        defaultMenuActions["pickObjRot"] = new QAction(
            //% "&Pick rotation for placement"
            qtTrId("route.editor.route.editor.glwidget.action.pick.rotation.for.placement"));
        QObject::connect(defaultMenuActions["pickObjRot"], SIGNAL(triggered()), this, SLOT(pickObjRotForPlacement()));
    }
    if(defaultMenuActions["pickObjElev"] == NULL){
        defaultMenuActions["pickObjElev"] = new QAction(
            //% "&Pick elevation for placement"
            qtTrId("route.editor.route.editor.glwidget.action.pick.elevation.for.placement"));
        QObject::connect(defaultMenuActions["pickObjElev"], SIGNAL(triggered()), this, SLOT(pickObjRotElevForPlacement()));
    }
    
    QMenu menu;
    QString menuStyle = QString(
        "QMenu::separator {\
          color: ")+Game::StyleMainLabel+";\
        }";
    menu.setStyleSheet(menuStyle);
    if(selectedObj != NULL){
        //% "Object: %1"
        menu.addSection(qtTrId("route.editor.context.object").arg(
                            selectedObj->getName()));
        selectedObj->pushContextMenuActions(&menu);
        
        if(selectedObj->typeObj == selectedObj->worldobj){
            menu.addAction(defaultMenuActions["setTerrToObj"]);
            menu.addAction(defaultMenuActions["setPosToTerr"]);
            menu.addAction(defaultMenuActions["setRotToTerr"]);
            menu.addAction(defaultMenuActions["pickObj"]);
            menu.addAction(defaultMenuActions["pickObjRot"]);
            menu.addAction(defaultMenuActions["pickObjElev"]);
            menu.addAction(defaultMenuActions["find1x1"]);
            menu.addAction(defaultMenuActions["find3x3"]);
        }
    }
    if(toolEnabled == ""){
        menu.addSection(
            //% "No Tool"
            qtTrId("route.editor.context.no.tool"));
    } else {
        QString toolName = toolEnabled;
        toolName[0] = toolName[0].toUpper();
        menu.addSection(toolName);
        if (EditorTool *tool = activeTool())
            tool->contextMenu(*this, menu);

    }
    
    //menu.addSeparator();
    menu.addSection(
        //% "Edit"
        qtTrId("route.editor.context.edit"));
    menu.addAction(defaultMenuActions["undo"]);
    menu.addAction(defaultMenuActions["copy"]); 
    menu.addAction(defaultMenuActions["paste"]);
    menu.addSeparator();
    menu.addAction(defaultMenuActions["select"]);
    
    
    menu.exec(mapToGlobal(point));
}

void RouteEditorGLWidget::createNewTiles(QMap<int, QPair<int, int>*> list){
    HeightWindow::resetLoadCancellation();
    int x, z;
    QMapIterator<int, QPair<int, int>*> i2(list);
    while (i2.hasNext()) {
        i2.next();
        if(i2.value() == NULL)
            continue;
        x = i2.value()->first;
        z = i2.value()->second;
        qDebug() << x << z;
        route->newTile(x, -z);
        if (Game::autoGeoTerrain && HeightWindow::lastLoadWasCancelled()) break;
    }
}

void RouteEditorGLWidget::createNewLoTiles(QMap<int, QPair<int, int>*> list){
    HeightWindow::resetLoadCancellation();
    int x, z;
    QMapIterator<int, QPair<int, int>*> i2(list);
    if (!Game::writeEnabled) return;
    while (i2.hasNext()) {
        i2.next();
        if(i2.value() == NULL)
            continue;
        x = i2.value()->first;
        z = i2.value()->second;
        qDebug() << x << z;
        Game::terrainLib->setLowTerrainAsCurrent();
        Game::terrainLib->saveEmpty(x, z);
        Game::terrainLib->reload(x, -z);
        if(Game::autoGeoTerrain){
            float pos[3];
            Vec3::set(pos, 0, 0, 0);
            Game::terrainLib->setHeightFromGeo(x, -z, (float*)&pos);
        }
        Game::terrainLib->setDetailedTerrainAsCurrent();
        if (Game::autoGeoTerrain && HeightWindow::lastLoadWasCancelled()) break;
    }
}

void RouteEditorGLWidget::getUnsavedInfo(QVector<QString> &items) {
    if (this->route == NULL)
        return;
    route->getUnsavedInfo(items);
}

void RouteEditorGLWidget::msg(QString text) {
    qDebug() << text;
    if (text == "save") {
        route->save();
        return;
    }
    if (text == "unselect") {
        setSelectedObj(NULL);
        lastSelectedObj = NULL;
        return;
    }
    if (text == "createPaths") {
        route->createNewPaths();
        return;
    }
    if (text == "resetPlaceRotation") {
        Quat::fill(this->placeRot);
        return;
    }
    if (text == "showTerrainTreeEditr") {
        TerrainTreeWindow ttWindow;
        ttWindow.exec();
        return;
    }
    if (text == "engItemSelected") {
        //QString pathid = ;
        //QString name = pathid.split("/").last();
        //QString texpath = pathid.left(pathid.length() - name.length());
        emit sendMsg("showShape", route->ref->selected->getShapePath());
    }
    if (text == "autoPlacementDeleteLast") {
        this->setSelectedObj(NULL);
        route->autoPlacementDeleteLast();
    }
    if (text == "editDetailedTerrain") {
        Game::terrainLib->setDetailedAsCurrent();
    }if (text == "editDistantTerrain") {
        Game::terrainLib->setDistantAsCurrent();
    }
}

void RouteEditorGLWidget::msg(QString text, bool val) {
    qDebug() << text;
    if (text == "continuousFlexLeft") {
        continuousFlexLeftEnabled = val;
        return;
    }
    if (text == "continuousFlexRight") {
        continuousFlexRightEnabled = val;
        return;
    }
    if (text == "stickToTDB") {
        this->route->placementStickToTarget = val;
        return;
    }
}

void RouteEditorGLWidget::msg(QString text, int val) {
    Q_UNUSED(text);
    Q_UNUSED(val);
}

void RouteEditorGLWidget::msg(QString text, float val) {
    qDebug() << text;
    if (text == "continuousFlexSeparation") {
        if(std::isfinite(val))
            continuousFlexSeparation = std::clamp(val, 1.0f, 20.0f);
        return;
    }
    if (text == "continuousFlexMinimumRadius") {
        if(std::isfinite(val))
            continuousFlexMinimumRadius = std::clamp(val, 5.0f, 10000.0f);
        return;
    }
    if (text == "autoPlacementLength") {
        this->route->placementAutoLength = val;
        return;
    }
}

void RouteEditorGLWidget::msg(QString text, QString val) {
    //qDebug() << text;
    if (text == "continuousFlexProfile") {
        continuousFlexProfile = val;
        if(liveFlexActive)
            applyContinuousFlexProfiles();
        return;
    }
    if (text == "continuousRulerProfile") {
        continuousRulerProfile = val;
        if(liveRulerActive && liveRulerObj != NULL)
            liveRulerObj->setTemplate(continuousRulerProfile);
        return;
    }

    if (text == "continuousRulerNodeShape") {
        continuousRulerNodeShape = val.trimmed();
        if(liveRulerActive && liveRulerObj != NULL)
            liveRulerObj->setNodeShape(continuousRulerNodeShape);
        return;
    }
    if (text == "mkrFile") {
        this->route->setMkrFile(val);
        return;
    }
}
