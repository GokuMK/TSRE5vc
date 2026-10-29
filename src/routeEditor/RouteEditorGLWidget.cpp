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
#include <tsre/renderer/FrameLog.h>
#include <QMouseEvent>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QPainter>
#include <QRegularExpression>
#include <cmath>
#include <math.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/world/Route.h>
#include <tsre/world/TerrainMaterialMap.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLights.h>
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
#include <tsre/texture/Texture.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/geo/HeightWindow.h>
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
#include <tsre/sound/SoundSystem.h>
#include <tsre/trains/Consist.h>
#include <tsre/ui/UiHud.h>
#include <tsre/ui/UiRuntime.h>
#include <tsre/ui/UiScreen.h>
#include <tsre/world/Skydome.h>
#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/renderer/EnvironmentMap.h>
#include <tsre/renderer/PlanarReflection.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/geo/SunPosition.h>
#include <tsre/geo/MoonPosition.h>
#include <settings/SettingsManager.h>
#include <tsre/world/Daylight.h>
#include <tsre/shape/NightTextures.h>
#include <QDebug>
#include <QMatrix4x4>
#include <algorithm>
#include <cmath>
#include <set>
#include <routeEditor/RouteEditorClient.h>
#include "tools/EditorTool.h"
#include "tools/ToolRegistry.h"
#include <tsre/camera/CameraMap.h>
#include <tsre/map/MapPalette.h>
#include <tsre/map/MapView.h>
#include <tsre/map/ActivityMapLayer.h>
#include <tsre/map/MapSelection.h>
#include <tsre/map/MapOverlayFade.h>
#include <tsre/map/OsmMapLayer.h>
#include <tsre/map/ImageryMapLayer.h>
#include <tsre/map/MapMeasureLayer.h>
#include <tsre/map/MapScaleBar.h>
#include <routeEditor/tools/MapTools.h>
#include <QLabel>
#include <tsre/map/MapLabelLayer.h>
#include <tsre/map/MapLabelSources.h>
#include <tsre/trains/Activity.h>
#include <tsre/coords/Coords.h>
#include <tsre/geo/osm/OsmConversionUi.h>
#include <tsre/map/TerrainMapLayer.h>
#include <tsre/map/TrackItemMapLayer.h>
#include <tsre/map/TrackMapLayer.h>
#include <tsre/ogl/OglObj.h>
#include <routeEditor/TerrainTileCreationDialog.h>
#include <tsre/world/RouteClient.h>
#include <tsre/ClientInfo.h>
#include <tsre/renderer/RenderStats.h>
#include <tsre/shape/ShapeLoader.h>
#include <QDir>
#include <QMessageBox>
#include <tsre/map/WeatherMapLayer.h>
#include <tsre/map/FogMapLayer.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <QFileInfo>
#include <tsre/environment/WeatherPresets.h>
#include <tsre/environment/WeatherDetail.h>
#include <tsre/environment/Atmosphere.h>
#include <tsre/world/Trk.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/sim/EventResults.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/environment/WorldClock.h>

// The active 8-byte paged layout derives local X/Z in StandardFog.
// StandardFogStoredCoords is retained as the Stage 1 shader reference, but it
// requires reverting the paged vertex layout to TerrainVertex12 before use.
#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif

// Objects farther than this are left out of the water reflection.
static const float WaterReflectionObjectDistance = 500.0f;
static constexpr unsigned long long LiveContinuousUpdateIntervalMs = 50;

RouteEditorGLWidget::RouteEditorGLWidget(QWidget *parent)
: QWidget(parent),
m_xRot(0),
m_yRot(0),
m_zRot(0),
tools(std::make_unique<ToolRegistry>()) {
    surface = RenderSurface::create(this, this);
    connect(this, &RouteEditorGLWidget::weatherChanged, this, [this] { ++weatherRevision; });
    this->installEventFilter(this);
    lampsByDay = Settings::variant("core.rendering.lampsByDay", SettingType::Enum).toInt();
    mistQuality = Settings::variant("core.rendering.mist", SettingType::Enum).toInt();
    warmth = float(Settings::floating("core.rendering.warmth"));
    // Undo replaces activity data: a selected loose consist, speed zone or
    // failed signal is gone.
    connect(ActivityNotifier::instance(), &ActivityNotifier::restoring, this, [this] {
        if (selectedObj != NULL && selectedObj->typeObj == GameObj::activityobj)
            setSelectedObj(NULL);
        if (lastSelectedObj != NULL && lastSelectedObj->typeObj == GameObj::activityobj)
            lastSelectedObj = NULL;
    });
    // The simulated trains' consists going (Stop, a reset): the camera and
    // the selection let go of them.
    connect(Sim::SimulationController::instance(), &Sim::SimulationController::trainsRemoving, this, [this] {
        Sim::SimulationController *simulation = Sim::SimulationController::instance();
        stopRiding();
        if (selectedObj != NULL && simulation->trainOfObject(selectedObj) >= 0)
            setSelectedObj(NULL);
        if (lastSelectedObj != NULL && simulation->trainOfObject(lastSelectedObj) >= 0)
            lastSelectedObj = NULL;
    });
    // A simulation starting: a tool editing the activity gives way.
    connect(ActivityNotifier::instance(), &ActivityNotifier::editingLockChanged, this, [this](bool locked) {
        if (locked && editsActivity(toolEnabled))
            enableTool("selectTool");
    });
    // Sky and fog settings change while the editor runs (environment window,
    // task editor 05); the next frame takes them.
    connect(&SettingsManager::instance(), &SettingsManager::runtimeSettingsChanged, this,
            [this](const QStringList &keys) {
        for (const QString &key : keys) {
            if (key == "core.rendering.skyColor" || key == "core.rendering.fogColor"
                    || key == "core.rendering.fogDensity")
                environmentChanged = true;
            if (key == "core.rendering.lampsByDay")
                lampsByDay = Settings::variant("core.rendering.lampsByDay", SettingType::Enum).toInt();
            if (key == "core.rendering.mist")
                mistQuality = Settings::variant("core.rendering.mist", SettingType::Enum).toInt();
            if (key == "core.rendering.warmth")
                warmth = float(Settings::floating("core.rendering.warmth"));
            // The frame-rate limit applies at once.
            if (key == "core.system.fpsLimit" && timer.isActive())
                timer.start(renderTimerStep(), this);
        }
    });
}

// Milliseconds between frames for core.system.fpsLimit (frames per second;
// 0 is the built-in 15 ms).
int RouteEditorGLWidget::renderTimerStep() {
    const int fpsLimit = Settings::integer("core.system.fpsLimit");
    return fpsLimit > 0 ? std::max(1, 1000 / fpsLimit) : 15;
}

void RouteEditorGLWidget::update(){
    surface->requestUpdate();
}

void RouteEditorGLWidget::makeCurrent(){
    surface->makeCurrent();
}

void RouteEditorGLWidget::doneCurrent(){
    surface->doneCurrent();
}

QImage RouteEditorGLWidget::grabFramebuffer(){
    return surface->grabFramebuffer();
}

unsigned int RouteEditorGLWidget::defaultFramebufferObject() const{
    return surface->defaultFramebufferObject();
}

void RouteEditorGLWidget::paintEvent(QPaintEvent *){
    // The surface fills the widget and paints itself.
}

void RouteEditorGLWidget::surfaceRelease(){
    cleanup();
}


bool RouteEditorGLWidget::eventFilter(QObject *object, QEvent *event){
    if (event->type() == QEvent::FocusIn){
        //qDebug() << "aaaaa";
        bolckContextMenu = true;
    }
    return false;
}

RouteEditorGLWidget::~RouteEditorGLWidget() {
    // The surface is destroyed after this destructor has run; it must not
    // call back into a destroyed object.
    surface->detachClient();
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
    // The reflections' storage belongs to the renderer's backend.
    delete environmentMap;
    environmentMap = NULL;
    delete waterReflection;
    waterReflection = NULL;
    delete renderer;
    renderer = NULL;
    //delete gluu->m_program;
    //gluu->m_program = 0;
    doneCurrent();
}

void RouteEditorGLWidget::timerEvent(QTimerEvent * event) {
    Game::currentShapeLib = currentShapeLib;
    // The simulation's trains as one frame has them, from here (sounds,
    // Follow, the camera riding a train) to the next tick: the paints draw
    // them where the camera was placed by them.
    Sim::SimulationController::instance()->pinViewFrame();
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
        gpuMsDisplay = renderer != nullptr ? renderer->gpuFrameMs() : -1.0f;
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
    
    if(Game::soundEnabled && SoundSystem::running())
        updateSound((float) (timeNow - lastTime) / 1000.0f);

    if (!simulationPaused)
        route->updateSim(camera->pozT, (float) (timeNow - lastTime) / 1000.0);
    // The clock: the time of day, and with it the weather, the clouds and
    // the fog, run on (WorldClock: the main thread's, or a simulation's).
    WorldClock::instance()->tick(simulationPaused ? 0.0 : (timeNow - lastTime) / 1000.0);

    lastTime = timeNow;

    if (Game::objectLoadingTokens < Game::maxObjLag)
        Game::objectLoadingTokens += 2;

    followTrain();
    // The camera moves by real time. The frame time is smoothed a little,
    // so an uneven frame rate does not change the speed from frame to frame
    // (a frame shows the time the one before took), and a stall counts as a
    // quarter of a second at most (not 100 ms: under 10 frames a second the
    // camera slowed at every long frame).
    const double movedMs = std::min(250.0, double(frameTimeMs));
    cameraFrameMs = cameraFrameMs <= 0.0 ? movedMs : 0.5 * cameraFrameMs + 0.5 * movedMs;
    camera->update(float(1000.0 / cameraFrameMs));
    
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
            rideCab3D((GameObj*)route->getActivityConsist(0));
            camera->setCameraObject((GameObj*)route->getActivityConsist(0));
        }
}

namespace {
// core.startup.camera, "tileX,tileZ,x,y,z[,yaw,pitch]" with the tile and
// position as the navigation window shows them (tile Z and z with the
// opposite sign to the camera's) and the angles in degrees.
bool parseStartupCamera(const QString &text, int &tileX, int &tileZ, float *pos, float *rot) {
    const QStringList parts = text.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
    if (parts.size() != 5 && parts.size() != 7)
        return false;
    double values[7] = {0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false;
        values[i] = parts[i].toDouble(&ok);
        if (!ok || !std::isfinite(values[i]))
            return false;
    }
    tileX = int(values[0]);
    tileZ = -int(values[1]);
    pos[0] = float(values[2]);
    pos[1] = float(values[3]);
    pos[2] = -float(values[4]);
    rot[0] = float(values[5] * M_PI / 180.0);
    rot[1] = float(values[6] * M_PI / 180.0);
    Game::check_coords(tileX, tileZ, pos[0], pos[2]);
    return true;
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
    if (!terrainMap)
        terrainMap = std::make_unique<TerrainMapLayer>();
    if (!mapFade)
        mapFade = std::make_unique<MapOverlayFade>();
    if (!mapLabels)
        mapLabels = std::make_unique<MapLabelLayer>();
    if (!mapMeasure)
        mapMeasure = std::make_unique<MapMeasureLayer>();
    if (!mapScaleBar)
        mapScaleBar = std::make_unique<MapScaleBar>();
    if (!weatherMap) {
        weatherMap = std::make_unique<WeatherMapLayer>();
        // From the worker thread: draw again once an image is ready.
        weatherMap->setReadyCallback([this] {
            QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);
        });
    }
    if (!imageryMap) {
        imageryMap = std::make_unique<ImageryMapLayer>();
        // From the fetch thread: draw again once tiles arrive.
        imageryMap->setReadyCallback([this] {
            QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);
        });
    }
    if (!osmMap) {
        osmMap = std::make_unique<OsmMapLayer>();
        // From the worker thread: draw again once a build is ready.
        osmMap->setReadyCallback([this] {
            QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);
        });
    }
    int tileX = 0, tileZ = 0;
    float spos[3] = {0.0f, 0.0f, 0.0f};
    float rot[2] = {0.0f, 0.0f};
    const QString startCamera = Settings::string("core.startup.camera").trimmed();
    if (!startCamera.isEmpty()) {
        if (parseStartupCamera(startCamera, tileX, tileZ, spos, rot)) {
            setDiagnosticView(tileX, tileZ, spos[0], spos[1], spos[2], rot[0], rot[1]);
            return;
        }
        qWarning() << "core.startup.camera: expected tileX,tileZ,x,y,z[,yaw,pitch], got" << startCamera;
    }
    if (Settings::boolean("core.startup.useTilePosition")) {
        // The centre of the startup tile.
        tileX = Settings::integer("core.startup.tileX");
        tileZ = -Settings::integer("core.startup.tileZ");
    } else {
        tileX = route->getStartTileX();
        tileZ = -route->getStartTileZ();
        spos[0] = route->getStartpX();
        spos[2] = -route->getStartpZ();
    }
    camera->setPozT(tileX, tileZ);
    if (Game::terrainLib->load(tileX, tileZ))
        spos[1] = 20 + Game::terrainLib->getHeight(tileX, tileZ, spos[0], spos[2]);
    camera->setPos(spos);
}

QString RouteEditorGLWidget::cameraSetting() const {
    int tileX = 0, tileZ = 0;
    float pos[3];
    float rotX = 0.0f, rotY = 0.0f;
    diagnosticView(tileX, tileZ, pos, rotX, rotY);
    return QString("%1,%2,%3,%4,%5,%6,%7").arg(tileX).arg(-tileZ)
            .arg(pos[0], 0, 'f', 2).arg(pos[1], 0, 'f', 2).arg(-pos[2], 0, 'f', 2)
            .arg(rotX * 180.0 / M_PI, 0, 'f', 2).arg(rotY * 180.0 / M_PI, 0, 'f', 2);
}

// The camera is the listener; riding a consist (play mode, or a simulated
// train followed) it hears the train as playSoundView says: from outside,
// or in the lead engine's cab.
void RouteEditorGLWidget::updateSound(float seconds) {
    SoundListener listener;
    listener.tileX = (int)camera->pozT[0];
    listener.tileZ = (int)camera->pozT[1];
    const float *pos = camera->getPos();
    const float *target = camera->getTarget();
    const float *up = camera->getUp();
    float forward[3] = {target[0] - pos[0], target[1] - pos[1], target[2] - pos[2]};
    const float length = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    for (int i = 0; i < 3; i++) {
        listener.position[i] = pos[i];
        listener.forward[i] = length > 0.0f ? forward[i] / length : (i == 2 ? -1.0f : 0.0f);
        listener.up[i] = up[i];
    }
    Consist *consist = dynamic_cast<Consist*>(camera->getCameraObject());
    if (consist != NULL && consist->engItems.size() > 0 && consist->engItems[0].engPointer != NULL) {
        Eng *lead = consist->engItems[0].engPointer;
        listener.view = playSoundView;
        listener.attachedTo = lead;
        listener.attachedValues = lead->soundValues();
    }
    SoundSystem::setListener(listener);
    // The simulated trains near the camera.
    Sim::SimulationController::instance()->updateSounds(seconds, listener.tileX, listener.tileZ,
                                                        listener.position);
    SoundSystem::update(seconds);
}

void RouteEditorGLWidget::surfaceInitialize() {
    
    if(Game::soundEnabled)
        SoundSystem::start();

    gluu = GLUU::get();
    qDebug() << "# InitializeOpenGLFunctions";

    const bool openGL = surface->backend() == RenderSurface::OpenGL;
    if(openGL)
        initializeOpenGLFunctions();

    renderer = surface->createRenderer();
    renderer->setSurface(surface);
    
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
    // The QRhi renderer builds its own programs.
    if(openGL)
        gluu->initShader();
    qDebug() << "# InitShaders finished";
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
    timer.start(renderTimerStep(), this);
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
    imageryWindow = new ImageryWindow();
    Quat::fill(this->placeRot);

    
    //emit routeLoaded(route);
    emit mkrList(route->getMkrList());

    shadowMapSize = Settings::variant(
                "core.rendering.shadow.primaryMapSize", SettingType::Enum).toInt();
    // The middle map: its own size, or 0 the near map's (as before it had a
    // setting of its own: users wanted a sharper near map, 4096, with the
    // middle one at 2048).
    middleShadowMapSize = Settings::variant(
                "core.rendering.shadow.middleMapSize", SettingType::Enum).toInt();
    if (middleShadowMapSize <= 0)
        middleShadowMapSize = shadowMapSize;
    distantShadowMapSize = Settings::variant(
                "core.rendering.shadow.distantMapSize", SettingType::Enum).toInt();
    renderer->createShadowMaps(shadowMapSize, middleShadowMapSize, distantShadowMapSize);
    renderer->bindTarget(Renderer::TARGET_VIEW);
        
    
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

// Riding a consist: a simulated train's is followed (the Timetable window's
// Follow, its cab view); another ends Follow.
void RouteEditorGLWidget::setCameraObject(GameObj* obj){
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const int train = simulation->trainOfObject(obj);
    if (train >= 0) {
        simulation->choose(train);
        simulation->follow(train);
        followedTrain = -1;
        return;
    }
    if (simulation->followed() >= 0)
        simulation->follow(-1);
    stopRiding();
    rideCab3D(obj);
    cameraFree->setCameraObject(obj);
}

// A consist is ridden from its first vehicle's 3D cab at its front (as
// listed), where it has one.
void RouteEditorGLWidget::rideCab3D(GameObj *obj) {
    if (obj == NULL || obj->typeObj != GameObj::consistobj)
        return;
    Consist *consist = static_cast<Consist*>(obj);
    const Eng *eng = consist->engItems.isEmpty() ? NULL : consist->engItems[0].engPointer;
    if (eng == NULL || !consist->rideCab3D(0, eng->flip != 0))
        consist->leaveCab3D();
}

void RouteEditorGLWidget::releaseCameraObject(){
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    if (simulation->followed() >= 0)
        simulation->follow(-1);
    if (cameraFree != NULL)
        cameraFree->releaseObject();
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

void RouteEditorGLWidget::surfacePaint(){
    Game::currentShapeLib = currentShapeLib;
    if (!canRenderFrame()) return;
    if (FrameLog::active())
        FrameLog::paintStart();
    Terrain::beginProceduralFrame();
    renderer->resetState();

    const bool selectionPass = selection;
    showImageryAttribution(currentViewMode == ViewMode::Map && mapLayers.shows(MapLayer::Imagery));
    if (currentViewMode == ViewMode::Map) {
        paintMap();
        // As in 3D, a selection pass is followed by a visible frame.
        if (selectionPass && !selection) {
            renderer->resetState();
            paintMap();
        }
        return;
    }

    paintScene();

    if(selectionPass && !selection){
        // QOpenGLWidget does not preserve its color buffer by default. Finish
        // every selection callback with a visible frame before returning to Qt.
        renderer->resetState();
        paintScene();
    }
    if (FrameLog::active()) {
        FrameLog::setGpuMs(renderer->gpuFrameMs());
        FrameLog::setCamera(int(camera->pozT[0]), int(camera->pozT[1]), camera->getPos(),
                            camera->getRotX(), camera->getRotY());
    }
}

// Loads the shapes, forests and transfers of the view without the loading
// limits, before it is drawn: the world is gathered and dropped until it asks
// for nothing more (on the workers when shapes load threaded, so the gathers
// wait for them).
void RouteEditorGLWidget::loadWholeView(const char *reason, qint64 limitMs) {
    wholeViewPending = false;
    RenderQueue &queue = *renderer;
    ShapeLoader::WholeView unlimited(limitMs);
    QElapsedTimer timer;
    timer.start();
    wholeViewShown.start();
    wholeViewReason = reason;
    // Terrain loads within the time too; it culls with the new camera.
    Mat4::perspective(gluu->pMatrix, Game::cameraFov * M_PI / 180, float(this->width()) / this->height(), 0.2f, Game::objectLod);
    Mat4::multiply(gluu->pMatrix, gluu->pMatrix, camera->getMatrix());
    const unsigned jobs = ShapeLoader::progress();
    int pass = 0;
    while (pass < 8) {
        ++pass;
        renderer->resetFrame();
        Mat4::identity(renderer->transform());
        Game::terrainLib->pushRenderItems(queue, camera->pozT, camera->getPos(), camera->getTarget(), 3.14f / 3,
                                          GLUU::RENDER_DEFAULT);
        Game::ignoreLoadLimits = true; // reset by Route::pushRenderItems
        route->pushRenderItems(queue, camera->pozT, camera->getPos(), camera->getTarget(),
                               camera->getRotX(), 3.14f / 3, GLUU::RENDER_DEFAULT);
        if (!ShapeLoader::busy())
            break;
        if (limitMs < 0) {
            ShapeLoader::waitForAll();
        } else {
            // Out of time, the view is shown and the rest loads while it is.
            const qint64 left = limitMs - timer.elapsed();
            if (left <= 0 || !ShapeLoader::waitForAll(int(left)))
                break;
        }
    }
    wholeViewMarks[0] = wholeViewShown.elapsed();
    qInfo() << "Whole view (" << reason << ") loaded in" << timer.elapsed() << "ms," << pass << "gathers,"
            << ShapeLoader::progress() - jobs << "shapes on workers"
            << (ShapeLoader::busy() ? "- out of time, the rest loads in view" : "");
}

void RouteEditorGLWidget::paintScene(){
    Game::currentShapeLib = currentShapeLib;
    if (!canRenderFrame()) return;
    if (renderer == NULL) return;
    const bool selectionPass = selection;
    if (!renderer->programsReady()){
        if(selectionPass){
            qWarning() << "Selection shader is unavailable";
            selection = false;
            update();
        }
        return;
    }
    RenderStats::ScopedFrame statsFrame(!selectionPass);
    renderer->setPlainImage(false);
    // The sun and the sky of the chosen time of day (or the fixed light).
    applyTimeOfDay();
    applySky();
    applyClouds();
    updateFog();
    applyFog();
    // Train lights (task renderer 34): day while the sun is up; the
    // weather's rain or snow at the camera.
    {
        int trainWeather = EngLights::Train::Clear;
        if (const Weather *weather = currentWeather(); weather != nullptr && !weather->keyframes.isEmpty()) {
            double east, north;
            cameraWorldPosition(east, north);
            const WeatherSurface s = WeatherDetail::precipitation(*weather, weather->frame(weatherHours()), east,
                                                                  north, 0.0);
            if (s.precipitationRate > 0.05f)
                trainWeather = s.snowShare > 0.5f ? EngLights::Train::Snow : EngLights::Train::Rain;
        }
        EngLights::setEnvironment(skyState.sunElevation > 0.0f, trainWeather);
    }
    applyTerrainShade();
    // View-dependent shading (PBR materials) reads the camera position.
    std::copy(camera->getPos(), camera->getPos() + 3, gluu->cameraPosition);
    // Secondary views must not read last frame's water reflection.
    std::fill(gluu->waterReflectionView, gluu->waterReflectionView + 4, 0.0f);
    if (!selectionPass && wholeViewReason != nullptr) {
        // The frame of the whole view has been drawn and shown.
        const qint64 shown = wholeViewShown.elapsed();
        qInfo() << "Whole view (" << wholeViewReason << ") shown" << shown << "ms after it began: load"
                << wholeViewMarks[0] << "ms, frame gather" << wholeViewMarks[1] - wholeViewMarks[0]
                << "ms, drawing (with uploads)" << wholeViewMarks[2] - wholeViewMarks[1]
                << "ms, GPU and present" << shown - wholeViewMarks[2] << "ms";
        wholeViewReason = nullptr;
    }
    if (!selectionPass) {
        // A camera moving farther than this in one frame jumped.
        const double jumpDistance = 500.0;
        const double position[2] = {camera->pozT[0] * 2048.0 + camera->getPos()[0],
                                    camera->pozT[1] * 2048.0 + camera->getPos()[2]};
        const bool jumped = std::hypot(position[0] - lastViewPosition[0],
                                       position[1] - lastViewPosition[1]) > jumpDistance;
        lastViewPosition[0] = position[0];
        lastViewPosition[1] = position[1];
        // The first view waits for all its shapes; after a jump the wait is
        // limited, so a long load does not hold the editor.
        const qint64 jumpWaitMs = 2000;
        // The exposure takes its target at once after a jump of the camera
        // or of the time of day; it adapts over time otherwise.
        const qint64 frameNs = exposureClock.isValid() ? exposureClock.nsecsElapsed() : 0;
        exposureClock.restart();
        exposure.seconds = float(frameNs * 1e-9);
        exposure.snap = jumped || wholeViewPending || frameNs == 0
                || std::abs(Game::timeOfDayHours - lastExposureHours) > 0.1f
                || std::abs(std::log2(std::max(exposure.sceneLight, 1e-6f) / std::max(lastSceneLight, 1e-6f))) > 0.5f;
        lastSceneLight = exposure.sceneLight;
        lastExposureHours = Game::timeOfDayHours;
        renderer->setExposureControl(exposure);
        renderer->setWarmth(warmth);
        if (wholeViewPending)
            loadWholeView("first view", -1);
        else if (jumped)
            loadWholeView("camera jump", jumpWaitMs);
        // That frame shows everything at once; others make GPU resources
        // within the per-frame budget.
        renderer->setUploadsUnlimited(wholeViewReason != nullptr);
    }
    if (FrameLog::active())
        FrameLog::mark(FrameLog::WholeView);
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

    // The main view's motion since last frame (task renderer 38).
    if (!selectionPass)
        updateViewMotion();

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
    // Environment map faces look in every direction, and the sky enclosure
    // needs the terrain over a tunnel, above the view: gather terrain patches
    // all around the camera and let the renderer cull each view.
    const bool renderEnvironment = !selectionPass && Game::environmentMapEnabled;
    Terrain::gatherAllDirections = renderEnvironment || (!selectionPass && Game::skyEnclosure);
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
    if (!renderer->drawsPhysicalSky())
        route->skydome->pushRenderItems(queue);
    Mat4::identity(renderer->transform());
    Mat4::translate(renderer->transform(), renderer->transform(), camera->getPos());
    skySatellites.push(queue, skyState);
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

    if (wholeViewReason != nullptr)
        wholeViewMarks[1] = wholeViewShown.elapsed();
    const bool frameLog = FrameLog::active();
    if (frameLog)
        FrameLog::mark(FrameLog::Gather);
    // Render Shadows
    if (!selectionPass && Game::shadowsEnabled > 0 && sunCastsShadows){
        RenderStats::beginPhase(RenderStats::PhaseShadow);
        renderShadowMaps();
        RenderStats::endPhase(RenderStats::PhaseShadow);
    }
    if (frameLog)
        FrameLog::mark(FrameLog::Shadows);
    if (!selectionPass){
        RenderStats::beginPhase(RenderStats::PhaseShadow);
        renderSkyEnclosure();
        RenderStats::endPhase(RenderStats::PhaseShadow);
    }
    if (frameLog)
        FrameLog::mark(FrameLog::Enclosure);
    if (renderEnvironment){
        RenderStats::beginPhase(RenderStats::PhaseEnvironment);
        renderEnvironmentMap();
        RenderStats::endPhase(RenderStats::PhaseEnvironment);
    }
    if (frameLog)
        FrameLog::mark(FrameLog::Environment);
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
        if(!renderer->beginSelection(selectionWidth, selectionHeight)){
            qWarning() << "Could not start the integer selection pass";
            selection = false;
            update();
            return;
        }
    } else {
        renderer->bindTarget(Renderer::TARGET_VIEW);
        renderer->clear(true, true);
    }
    renderer->useProgram(selectionPass ? Renderer::PROGRAM_SELECTION : Renderer::PROGRAM_MAIN);

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
    if (frameLog)
        FrameLog::mark(FrameLog::Scene);
    if (wholeViewReason != nullptr)
        wholeViewMarks[2] = wholeViewShown.elapsed();
    // render compass
    RenderStats::beginPhase(RenderStats::PhaseUi);
    if (!selectionPass && Game::viewCompass){
        Mat4::identity(gluu->mvMatrix);
        Mat4::ortho(gluu->pMatrix, -1.0, 1.0, 1.0 - 2*(float(this->height()) / this->width()), 1.0, 0.0, 1.0);
        Mat4::identity(gluu->objStrMatrix);
        renderer->applyFrameUniforms();
        renderer->setFogLod(0.0f);

        renderer->setLayer(RenderQueue::LAYER_UI);
        compass->pushRenderItem(queue, camera->getRotX()+M_PI);
        compassPointer->pushRenderItem(queue);
        renderer->setLayer(RenderQueue::LAYER_SCENE);
        renderer->renderPasses(Renderer::PASS_UI, Renderer::PASS_UI);
    }
    
    
    // HUD: the UI document's over the finished frame (task scripting 01)
    // for a ridden train, otherwise the built-in one in the UI pass.
    Consist *hudTrain = !selectionPass && Game::hudEnabled && Game::hudDocument
            ? dynamic_cast<Consist*>(camera->getCameraObject()) : nullptr;
    const bool hudFromDocument = hudTrain != nullptr && (!uiScreen || uiScreen->usable())
            && (!uiHud || uiHud->usable());
    if(!selectionPass && Game::hudEnabled && !hudFromDocument){
        int shadowsState = Game::shadowsEnabled;
        Game::shadowsEnabled = 0;
        float hudScale = Game::hudScale;
        Mat4::identity(gluu->mvMatrix);
        Mat4::ortho(gluu->pMatrix, -1.0, -1.0+2.0*hudScale, 1.0 - 2*(float(this->height()) / this->width())*hudScale, 1.0, 0.0, 1.0);
        Mat4::identity(gluu->objStrMatrix);
        renderer->applyFrameUniforms();
        renderer->setFogLod(0.0f);
        renderer->setLayer(RenderQueue::LAYER_UI);
        camera->pushRenderHud(queue);
        renderer->setLayer(RenderQueue::LAYER_SCENE);
        renderer->renderPasses(Renderer::PASS_UI, Renderer::PASS_UI);
        Game::shadowsEnabled = shadowsState;
        renderer->releaseProgram();
    }
    if (frameLog)
        FrameLog::mark(FrameLog::Overlay);
    renderer->renderFrame();
    if (frameLog)
        FrameLog::mark(FrameLog::Finish);
    // The UI over the view: its documents updated and drawn.
    if (!selectionPass) {
        if (hudFromDocument && uiScreenReady()) {
            if (!uiHud)
                uiHud = std::make_unique<Ui::Hud>(*uiScreen);
            Ui::Hud::Values values;
            hudTrain->hudValues(values.speed, values.distance, values.throttle, values.brake);
            uiHud->update(values);
        } else if (uiHud) {
            uiHud->hide();
        }
        if (uiScreen && uiScreen->context() != nullptr) {
            uiScreen->update();
            uiScreen->draw(renderer);
        }
    }
    if (!selectionPass && Game::environmentMapPreview && environmentMap != NULL
            && environmentMap->complete())
        environmentMap->drawPreview(8, 8, qRound(48 * Game::PixelRatio));
    RenderStats::endPhase(RenderStats::PhaseUi);

    // Handle Selection
    if (selectionPass && blendingWasEnabled)
        renderer->setBlending(true);
    if(selectionPass){
        selectionTargetHeight = qRound((float)this->height() * Game::PixelRatio);
        handleSelection();
        selectionTargetHeight = 0;
        renderer->endSelection();
        renderer->releaseProgram();
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

    const QRect backgroundRect(12, 12, gpuMsDisplay >= 0.0f ? 190 : 92, 24);
    QPaintDevice *overlay = surface->overlayPaintDevice(backgroundRect);
    if(overlay == NULL)
        return;
    QPainter painter(overlay);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(QColor(72, 30, 112));

    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(10);
    painter.setFont(font);

    // GPU time near the frame time means the GPU limits the frame rate.
    const QString label = gpuMsDisplay >= 0.0f
            ? QString("FPS: %1  GPU %2 ms").arg(fpsDisplay).arg(gpuMsDisplay, 0, 'f', 1)
            : QString("FPS: %1").arg(fpsDisplay);
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
// Normal offset and depth bias of the near and middle maps for surfaces with
// normals, in texels or filter radii, whichever is larger. The fragment
// shaders also bias each filter tap by the receiver's slope, so the offset
// only has to cover texel quantization.
constexpr float ShadowNormalOffsetTexels[2] = {1.0f, 1.0f};
constexpr float ShadowDepthBiasTexels[2] = {1.0f, 1.0f};
// The grid each map's centre is snapped to, in its texels (task 37).
constexpr float ShadowSnapTexels = 1.0f;
// How far ahead of the camera, in the view's direction, each map's centre
// lies, and how far the camera may go from where a cached map was drawn
// before it is drawn again: a tenth of the map's width (task 37). The map
// then still reaches its half width ahead when it is drawn again.
constexpr float ShadowLead[3] = {16.0f, 60.0f, 140.0f};
}

void RouteEditorGLWidget::applyTimeOfDay() {
    TimeOfDayState &state = timeOfDay;
    // Off unless time of day runs with lamps by day on auto exposure.
    exposure = Renderer::ExposureControl();
    if (!state.saved) {
        std::copy(Game::sunLightDirection, Game::sunLightDirection + 3, state.sunDirection);
        std::copy(gluu->skyColor, gluu->skyColor + 4, state.sky);
        std::copy(gluu->fogColor, gluu->fogColor + 4, state.fog);
        std::copy(gluu->diffuseColor, gluu->diffuseColor + 4, state.diffuse);
        std::copy(gluu->ambientColor, gluu->ambientColor + 4, state.ambient);
        state.saved = true;
    }
    // Changed sky and fog settings are the day colours of time of day, and
    // the colours without it.
    if (environmentChanged) {
        environmentChanged = false;
        std::copy(Game::skyColor, Game::skyColor + 4, state.sky);
        std::copy(Game::fogColor, Game::fogColor + 4, state.fog);
        gluu->fogDensity = Game::fogDensity;
        if (!Game::timeOfDayEnabled) {
            std::copy(state.sky, state.sky + 4, gluu->skyColor);
            std::copy(state.fog, state.fog + 4, gluu->fogColor);
        }
    }
    if (!Game::timeOfDayEnabled) {
        // The sun where the fixed light comes from; no moon without a time.
        float sun[3] = {state.sunDirection[0], state.sunDirection[1], state.sunDirection[2]};
        const float length = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
        for (float &c : sun)
            c = length > 0.0f ? c / length : 0.0f;
        std::copy(sun, sun + 3, skyState.sunDirection);
        skyState.sunElevation = float(std::asin(std::clamp(sun[1], -1.0f, 1.0f)) * 180.0 / M_PI);
        skyState.moon = false;
        std::fill(moonLight, moonLight + 3, 0.0f);
        std::copy(gluu->skyColor, gluu->skyColor + 3, skyState.skyColor);
        reportEnvironment(skyState.sunElevation, std::nanf(""), std::nanf(""), std::nanf(""));
        // No night textures under the fixed daytime light (task 35): at the
        // lamps' full strength they would light every building up.
        NightTextures::setFrame({});
        if (state.applied) {
            std::copy(state.sunDirection, state.sunDirection + 3, Game::sunLightDirection);
            std::copy(state.sky, state.sky + 4, gluu->skyColor);
            std::copy(state.fog, state.fog + 4, gluu->fogColor);
            std::copy(state.diffuse, state.diffuse + 4, gluu->diffuseColor);
            std::copy(state.ambient, state.ambient + 4, gluu->ambientColor);
            std::fill(gluu->specularColor, gluu->specularColor + 4, 0.0f);
            // Lamps at full strength under the fixed daytime light, by design:
            // time of day off is an editing mode that keeps every lamp's light
            // visible (tasks 21 and 22). Only time of day scales them down.
            gluu->localLightAdaptation = 1.0f;
            gluu->signalLightAdaptation = 1.0f;
            const float fixed[3] = {-1.0f, 1.5f, 1.0f};
            std::copy(fixed, fixed + 3, shadowSunDirection);
            sunCastsShadows = true;
            state.applied = false;
        }
        return;
    }
    state.applied = true;
    // Where the camera is; within a tile the sun hardly moves.
    PreciseTileCoordinate *position = camera->getCurrentPos();
    if (position != NULL && Game::GeoCoordConverter != NULL
            && (!state.located || position->TileX != state.tileX || position->TileZ != state.tileZ)) {
        // The place, and a point 100 m along -z for the grid's bearing.
        auto locate = [](PreciseTileCoordinate *tile, LatitudeLongitudeCoordinate &out) {
            IghCoordinate internal;
            return Game::GeoCoordConverter->ConvertToInternal(tile, &internal) != NULL
                    && Game::GeoCoordConverter->ConvertToLatLon(&internal, &out) != NULL
                    && std::isfinite(out.Latitude) && std::isfinite(out.Longitude);
        };
        PreciseTileCoordinate ahead = *position;
        ahead.setTWxyz(position->TileX, position->TileZ, position->wX, position->wY, position->wZ - 100.0f);
        LatitudeLongitudeCoordinate here, north;
        if (locate(position, here)) {
            state.latitude = here.Latitude;
            state.longitude = here.Longitude;
            if (locate(&ahead, north)) {
                const double east = (north.Longitude - here.Longitude) * std::cos(here.Latitude * M_PI / 180.0);
                const double up = north.Latitude - here.Latitude;
                if (east != 0.0 || up != 0.0)
                    state.gridNorth = std::atan2(east, up) * 180.0 / M_PI;
            }
        }
        state.tileX = position->TileX;
        state.tileZ = position->TileZ;
        state.located = true;
    }
    QDate date = QDate::fromString(Game::timeOfDayDate.trimmed(), "yyyy-MM-dd");
    if (!date.isValid())
        date = QDate(2026, 6, 21);
    SunPosition::Result sun = SunPosition::atSolarTime(state.latitude, state.longitude, date,
                                                       Game::timeOfDayHours);
    MoonPosition::Result moon = MoonPosition::atSolarTime(state.latitude, state.longitude, date,
                                                          Game::timeOfDayHours);
    reportEnvironment(float(sun.elevation), float(sun.azimuth), float(moon.elevation),
                      float(moon.illuminatedFraction));
    // From true bearings to the world's grid.
    sun.azimuth -= state.gridNorth;
    moon.azimuth -= state.gridNorth;
    float sunDirection[3];
    SunPosition::direction(sun, sunDirection);
    std::copy(sunDirection, sunDirection + 3, Game::sunLightDirection);
    // Shadows of a sun near the horizon would stretch across the maps.
    SunPosition::Result shadowSun = sun;
    shadowSun.elevation = std::max(sun.elevation, Daylight::MinShadowElevation);
    SunPosition::direction(shadowSun, shadowSunDirection);
    // The sun's light through the air towards it (Atmosphere), from the
    // camera's height: its colour lights the shading, its strength the
    // highlights.
    const double airHeight = std::max(0.001, double(camera->getPos()[1]) / 1000.0);
    const Atmosphere::Rgb sunThrough = Atmosphere::model().sunLight(airHeight,
                                                                    std::sin(sun.elevation * M_PI / 180.0));
    const Atmosphere::Rgb sunOverhead = Atmosphere::model().sunLight(airHeight, 1.0);
    const float sunColour[3] = {sunThrough[0], sunThrough[1], sunThrough[2]};
    const Daylight::Light light = Daylight::forElevation(sun.elevation, state.sky, state.fog, sunColour);
    std::copy(light.sky, light.sky + 4, gluu->skyColor);
    std::copy(light.fog, light.fog + 4, gluu->fogColor);
    std::copy(light.diffuse, light.diffuse + 4, gluu->diffuseColor);
    std::copy(light.ambient, light.ambient + 4, gluu->ambientColor);
    // The moon lights the night (task 31): up at night, it is the light of
    // the shading and the shadows; the sky keeps the sun.
    const Daylight::Moonlight moonLight = Daylight::moonlight(sun.elevation, moon.elevation,
                                                              moon.illuminatedFraction);
    const bool moonLit = moonLight.strength > 0.0f;
    if (moonLit) {
        MoonPosition::direction(moon, Game::sunLightDirection);
        MoonPosition::Result shadowMoon = moon;
        shadowMoon.elevation = std::max(moon.elevation, Daylight::MinShadowElevation);
        MoonPosition::direction(shadowMoon, shadowSunDirection);
        std::copy(moonLight.diffuse, moonLight.diffuse + 4, gluu->diffuseColor);
    }
    // Highlights (MSTS specular) mirror the sun's disc: the day's sun through
    // the air towards it (Atmosphere), as bright overhead as the lit shaders'
    // noon sun, where that is brighter than the lit shaders' sun, which fades
    // by the horizon as a look (Daylight), as the fog's light does
    // (RhiFog.cpp). An evening sun on the rails ahead glinted with nearly
    // nothing (the user, aaa, 2026-10-27). Under the moon, the shading's
    // light alone.
    std::fill(gluu->specularColor, gluu->specularColor + 4, 0.0f);
    if (!moonLit && light.sunUp) {
        const float noon = std::pow(Daylight::SunLight, 2.2f);
        const float overhead = 0.2126f * sunOverhead[0] + 0.7152f * sunOverhead[1] + 0.0722f * sunOverhead[2];
        for (int c = 0; c < 3; ++c)
            gluu->specularColor[c] = noon * sunThrough[c] / std::max(overhead, 1e-6f);
        gluu->specularColor[3] = 1.0f;
    }
    for (int c = 0; c < 3; ++c)
        gluu->ambientColor[c] += moonLight.ambient[c];
    std::copy(moonLight.diffuse, moonLight.diffuse + 3, this->moonLight);
    // Lamps by day (task 29): dimmed as the sun rises, or full strength.
    gluu->localLightAdaptation = lampsByDay == 1 ? 1.0f : light.localLights;
    gluu->signalLightAdaptation = lampsByDay == 1 ? 1.0f : light.signalLights;
    // Auto exposure: lamps at one scale, the light of the sun and the sky in
    // the scene's scale, the view adapting (task 29). Signals keep their
    // scale; the renderer undoes the exposure on them.
    if (lampsByDay == 2 && renderer != NULL && renderer->autoExposureAvailable()) {
        gluu->localLightAdaptation = Daylight::DayLocalLights;
        exposure.enabled = true;
        exposure.sceneLight = light.sceneLight;
        exposure.referenceLog2 = std::log2(light.sceneLight) + Daylight::viewLuminanceLog2(sun.elevation);
        exposure.curvePoints = Daylight::adaptationCurve(exposure.curveLuminance, exposure.curveExposure,
                                                         Renderer::ExposureControl::CurveSize);
        exposure.biasStops = Daylight::exposureBias(sun.elevation);
    }
    // Night textures (task 35): loaded from the sun 20 degrees up, shining as
    // the lamps do: at the lamps' constant scale with auto exposure, less its
    // bias (by night 1.75 stops brighter: a night texture copying the day one
    // came out three times its own brightness, paler than in the sun; user,
    // 2026-10-19, Prudnik ZB1), so that by night it shows as authored;
    // otherwise with the lamps' fade (also with lamps at full strength by day,
    // which would light every building up); under cover in full without auto
    // exposure.
    {
        NightTextures::Frame night;
        night.wanted = NightTextures::wantedForSun(sun.elevation, NightTextures::frame().wanted);
        if (night.wanted) {
            night.scale = exposure.enabled ? Daylight::DayLocalLights * std::exp2(-exposure.biasStops)
                                           : light.localLights;
            night.enclosedScale = exposure.enabled ? night.scale : 1.0f;
        }
        NightTextures::setFrame(night);
    }
    sunCastsShadows = light.sunUp || moonLit;
    // The sun and the moon on the sky.
    std::copy(sunDirection, sunDirection + 3, skyState.sunDirection);
    skyState.sunElevation = float(sun.elevation);
    skyState.moon = true;
    MoonPosition::direction(moon, skyState.moonDirection);
    skyState.moonElevation = float(moon.elevation);
    skyState.moonFraction = float(moon.illuminatedFraction);
    std::copy(gluu->skyColor, gluu->skyColor + 3, skyState.skyColor);
}

// The sky for the renderer. A physical sky (QRhi) replaces the skydome and
// gives the fog its colour: the horizon's, averaged around it.
void RouteEditorGLWidget::applySky() {
    Renderer::Sky sky;
    sky.physical = Game::skyModel == 1;
    // The sun of the sky, also when the moon lights the scene; the moon
    // lights the clouds at night (task 31).
    std::copy(skyState.sunDirection, skyState.sunDirection + 3, sky.sun);
    if (skyState.moon)
        std::copy(skyState.moonDirection, skyState.moonDirection + 3, sky.moon);
    for (int c = 0; c < 3; ++c)
        sky.moonLight[c] = std::pow(moonLight[c] / Daylight::SunLight, 2.2f);
    sky.cameraHeight = camera->getPos()[1];
    sky.groundHeight = Game::terrainLib != nullptr
            ? std::min(Game::terrainLib->getHeight(int(camera->pozT[0]), int(camera->pozT[1]), camera->getPos()[0],
                                                   camera->getPos()[2]), sky.cameraHeight)
            : sky.cameraHeight;
    // The night sky's floor, as the classic sky's (Daylight).
    const float night[3] = {0.015f, 0.022f, 0.05f};
    std::copy(night, night + 3, sky.night);
    // Haze from the weather's visibility at the camera (Koschmieder).
    if (const Weather *weather = currentWeather(); weather != nullptr && !weather->keyframes.isEmpty()) {
        double east, north;
        cameraWorldPosition(east, north);
        const float visibility = weather->surface(weatherHours(), east, north).visibility;
        if (visibility > 0.0f)
            sky.haze = 3.912f / std::max(visibility, 200.0f);
    }
    // With a physical sky the fog density scales that haze (the air's
    // haze of terrain, clouds and light shafts): 0.7 (the default, the
    // classic fog's) as the weather has it, 0.3 (Advanced Environment; the
    // user: above it the far mountains' blue goes) about a fifth, 0 clear
    // air, its Rayleigh blue only; 1 twice.
    const float hazeScale = gluu->fogDensity / 0.7f;
    sky.shaftHaze = sky.haze;
    sky.haze *= hazeScale * hazeScale;
    // Last frame's (applyClouds measures it after this).
    sky.overcast = Game::cloudQuality > 0 ? cloudOvercast : 0.0f;
    renderer->setSky(sky);
    if (!renderer->drawsPhysicalSky()) {
        if (physicalSkyApplied && !Game::timeOfDayEnabled) {
            std::copy(timeOfDay.sky, timeOfDay.sky + 4, gluu->skyColor);
            std::copy(timeOfDay.fog, timeOfDay.fog + 4, gluu->fogColor);
        }
        physicalSkyApplied = false;
        return;
    }
    physicalSkyApplied = true;
    const Atmosphere::Rgb horizon = Atmosphere::horizonColour(std::max(0.001, sky.cameraHeight / 1000.0), sky.sun);
    for (int c = 0; c < 3; ++c) {
        gluu->fogColor[c] = std::max(horizon[c], night[c]);
        gluu->skyColor[c] = gluu->fogColor[c];
        skyState.skyColor[c] = gluu->fogColor[c];
    }
}

// The weather of the environment around the camera, for the renderer's
// clouds: 128 x 128 km at 2 km a texel per layer (the weather's own cells
// are 2 km or more; the renderer's noise makes the clouds), moved in 8 km
// steps, rebuilt when the weather or its time changes or the camera leaves
// its step: a rebuild samples the weather 12000 times.
void RouteEditorGLWidget::applyClouds() {
    Renderer::Clouds &c = cloudState;
    const Weather *weather = currentWeather();
    if (Game::cloudQuality <= 0 || !renderer->drawsPhysicalSky() || weather == nullptr
            || weather->keyframes.isEmpty()) {
        if (c.quality != 0) {
            c.quality = 0;
            renderer->setClouds(c);
        }
        if (overcastApplied && !Game::timeOfDayEnabled)
            std::copy(timeOfDay.ambient, timeOfDay.ambient + 4, gluu->ambientColor);
        overcastApplied = false;
        return;
    }
    constexpr int Size = 64;
    constexpr float Texel = 2000.0f;
    constexpr double Step = 8000.0;
    c.quality = Game::cloudQuality;
    // Captures want each frame complete and the same.
    static const bool still = qEnvironmentVariableIsSet("TSRE_CLOUDS_STILL");
    c.still = still;
    double east, north;
    cameraWorldPosition(east, north);
    c.cameraEast = east;
    c.cameraNorth = north;
    const double hours = weatherHours();
    const Weather::Frame frame = weather->frame(hours);
    for (int l = 0; l < 3; ++l) {
        c.drift[l][0] = float(frame.drift[l][0]);
        c.drift[l][1] = float(frame.drift[l][1]);
    }
    c.seed = weather->seed;
    // A fast timelapse refreshes the clouds faster (the user: from 2 min a
    // second twice, at 4 min four times; static scenes, a lower frame rate
    // is fine): the history cannot follow clouds that change that much.
    const double clockRate = simulationPaused ? 0.0 : WorldClock::instance()->rate();
    c.passes = clockRate >= 240.0 ? 4 : (clockRate >= 120.0 ? 2 : 1);
    const double west = std::floor((east - Size * Texel * 0.5) / Step) * Step;
    const double top = std::ceil((north + Size * Texel * 0.5) / Step) * Step;
    // The fields move with their layers' wind, which the renderer follows
    // (drift - built): sampled again only for another place or weather, a
    // shift of 2 km (the near cover map reaches 2.7 km past where it is
    // read; the high layer drifts four times as fast as the low one),
    // keyframes blending on, or an hour later.
    constexpr double RebuildShift = 2000.0;
    double shifted = 0.0;
    for (int l = 0; l < 3; ++l)
        shifted = std::max(shifted, std::hypot(frame.drift[l][0] - c.built[l][0], frame.drift[l][1] - c.built[l][1]));
    const bool blendMoved = frame.blend.a != cloudBlendA || frame.blend.b != cloudBlendB
            || std::abs(frame.blend.weight - cloudBlendWeight) > 0.02;
    if (c.size != Size || west != c.west || top != c.north || weatherRevision != cloudWeatherRevision
            || !(std::abs(hours - cloudHours) < 1.0) || shifted > RebuildShift || blendMoved) {
        c.size = Size;
        c.west = west;
        c.north = top;
        c.texel = Texel;
        cloudHours = hours;
        cloudBlendA = frame.blend.a;
        cloudBlendB = frame.blend.b;
        cloudBlendWeight = frame.blend.weight;
        cloudWeatherRevision = weatherRevision;
        for (int l = 0; l < 3; ++l) {
            c.built[l][0] = float(frame.drift[l][0]);
            c.built[l][1] = float(frame.drift[l][1]);
        }
        auto layers = std::make_shared<std::vector<float>>(size_t(3) * Size * Size * 4);
        auto extra = std::make_shared<std::vector<float>>(size_t(Size) * Size * 4, 0.0f);
        float bottom = 1e9f, highest = -1e9f, deepest = 0.0f, stormiest = 0.0f;
        for (int l = 0; l < 3; ++l)
            for (int j = 0; j < Size; ++j)
                for (int i = 0; i < Size; ++i) {
                    const WeatherLayerValues v = weather->layer(frame, west + (i + 0.5) * Texel,
                                                                top - (j + 0.5) * Texel, l);
                    float *out = &(*layers)[((size_t(l) * Size + j) * Size + i) * 4];
                    out[0] = v.coverage;
                    out[1] = v.type;
                    out[2] = v.base / 1000.0f;
                    out[3] = v.top / 1000.0f;
                    if (l < 2)
                        (*extra)[(size_t(j) * Size + i) * 4 + l] = v.density;
                    if (l < 2 && v.coverage > 0.0f) {
                        bottom = std::min(bottom, out[2]);
                        highest = std::max(highest, out[3]);
                        deepest = std::max(deepest, out[3] - out[2]);
                        stormiest = std::max(stormiest, out[1]);
                    }
                }
        if (bottom > highest)
            bottom = highest = 0.0f;
        // Rain and snow: the area's (texels of 2 km are coarser than the
        // showers' cells; the renderer puts them under its clouds).
        float rainiest = 0.0f;
        for (int j = 0; j < Size; ++j)
            for (int i = 0; i < Size; ++i) {
                const WeatherSurface s = WeatherDetail::precipitation(*weather, frame, west + (i + 0.5) * Texel,
                                                                      top - (j + 0.5) * Texel, 0.0);
                float *out = &(*extra)[(size_t(j) * Size + i) * 4];
                out[2] = s.precipitationRate;
                out[3] = s.snowShare;
                rainiest = std::max(rainiest, s.precipitationRate);
            }
        c.bottom = bottom;
        c.top = highest;
        c.deepest = deepest;
        c.stormiest = stormiest;
        c.rainiest = rainiest;
        c.layers = layers;
        c.extra = extra;
        ++c.revision;
        // Overcast: the low and mid layers' cover within 10 km of the
        // camera, the two combined as independent.
        double sum = 0.0;
        int count = 0;
        const int ci = std::clamp(int((east - west) / Texel), 5, Size - 6);
        const int cj = std::clamp(int((top - north) / Texel), 5, Size - 6);
        for (int j = cj - 5; j <= cj + 5; ++j)
            for (int i = ci - 5; i <= ci + 5; ++i) {
                const float lowCover = (*layers)[((size_t(0) * Size + j) * Size + i) * 4];
                const float midCover = (*layers)[((size_t(1) * Size + j) * Size + i) * 4];
                sum += 1.0 - (1.0 - lowCover) * (1.0 - midCover);
                ++count;
            }
        cloudOvercast = float(sum / std::max(1, count));
    }
    renderer->setClouds(c);
    // Under a covered sky the light comes grey from all around: less
    // colour, and a grey horizon. How much of it the clouds let through is
    // the renderer's: the sun's through their shadow, the sky's ambient
    // through them (RhiRenderer::cloudSkyLight; before, the ambient was
    // raised by half under an overcast to make up for the sun, whatever the
    // clouds' depth).
    const float o = std::clamp((cloudOvercast - 0.3f) / 0.6f, 0.0f, 1.0f);
    const float *ambient = Game::timeOfDayEnabled ? gluu->ambientColor : timeOfDay.ambient;
    float base[3] = {ambient[0], ambient[1], ambient[2]};
    const float grey = (base[0] + base[1] + base[2]) / 3.0f;
    for (int k = 0; k < 3; ++k)
        gluu->ambientColor[k] = base[k] + (grey - base[k]) * 0.6f * o;
    const float fogGrey = (gluu->fogColor[0] * 0.3f + gluu->fogColor[1] * 0.55f + gluu->fogColor[2] * 0.15f) * 0.85f;
    for (int k = 0; k < 3; ++k) {
        gluu->fogColor[k] += (fogGrey - gluu->fogColor[k]) * 0.8f * o;
        gluu->skyColor[k] = gluu->fogColor[k];
        skyState.skyColor[k] = gluu->fogColor[k];
    }
    overcastApplied = true;
}

// Tells the environment window where the sun and moon stand (true bearing;
// NaN where there is nothing to tell), when it changes noticeably.
void RouteEditorGLWidget::reportEnvironment(float sunElevation, float sunAzimuth, float moonElevation,
                                            float moonFraction) {
    const float values[4] = {sunElevation, sunAzimuth, moonElevation, moonFraction};
    bool changed = false;
    for (int i = 0; i < 4; ++i) {
        const bool wasNan = std::isnan(reportedEnvironment[i]), isNan = std::isnan(values[i]);
        if (wasNan != isNan || (!isNan && std::fabs(values[i] - reportedEnvironment[i]) > (i == 3 ? 0.002f : 0.05f)))
            changed = true;
    }
    if (!changed)
        return;
    std::copy(values, values + 4, reportedEnvironment);
    emit environmentInfo(sunElevation, sunAzimuth, moonElevation, moonFraction);
}

// Light-space matrices of the near, mid and far shadow maps, centred ahead
// of the camera. The near and mid maps share the primary map size; the near
// map covers a third of the mid map's width, so shadows close to the camera
// get three times the detail.
// Each map's centre lies ShadowLead ahead of the camera in the view's
// direction, on a grid of its own texels in the light's frame (task 37): its
// texels keep their place on the ground while the camera moves, so shadow
// edges do not crawl.
void RouteEditorGLWidget::computeShadowMatrices() {
    float up[3] = {0.0f, 1.0f, 0.0f};
    float sun[3] = {shadowSunDirection[0], shadowSunDirection[1], shadowSunDirection[2]};
    Vec3::normalize(sun, sun);
    // The light's frame: the rows of the rotation Mat4::lookAt makes. Its
    // inverse is its transpose.
    float rotation[16];
    float origin[3] = {0.0f, 0.0f, 0.0f};
    Mat4::lookAt(rotation, sun, origin, up);
    const float right[3] = {rotation[0], rotation[4], rotation[8]};
    const float upward[3] = {rotation[1], rotation[5], rotation[9]};
    // No frame with the sun straight overhead (lookAt leaves right zero).
    const bool snap = right[0] * right[0] + right[1] * right[1] + right[2] * right[2] > 0.5f;
    const float *position = camera->getPos();
    shadowSnapped = snap;
    // The view's direction on the ground.
    const float *target = camera->getTarget();
    float forward[2] = {target[0] - position[0], target[2] - position[2]};
    const float forwardLength = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
    for (float &c : forward)
        c = forwardLength > 1e-6f ? c / forwardLength : 0.0f;
    std::copy(forward, forward + 2, shadowForward);
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
        const float texel = 2.0f * ShadowHalfExtent[map] / (map == 0 ? shadowMapSize : middleShadowMapSize);
        const float filter = std::max(texel, tapSpread);
        gluu->shadowNormalOffset[map] = ShadowNormalOffsetTexels[map] * filter;
        gluu->shadowDepthBias[map] = ShadowDepthBiasTexels[map] * filter / (2.0f * ShadowHalfDepth[map]);
    }
    gluu->shadowNormalOffset[2] = 0.0f;
    std::copy(sun, sun + 3, gluu->shadowLightDirection);
    for (int map = 0; map < 3; map++) {
        const int size = map == 0 ? shadowMapSize : map == 1 ? middleShadowMapSize : distantShadowMapSize;
        const float cell = ShadowSnapTexels * 2.0f * ShadowHalfExtent[map] / float(std::max(size, 1));
        const float ahead[3] = {position[0] + forward[0] * ShadowLead[map], position[1],
                                position[2] + forward[1] * ShadowLead[map]};
        float centre[3] = {ahead[0], ahead[1], ahead[2]};
        if (snap) {
            // In the light's frame, on the grid, and back.
            float q[3] = {right[0] * ahead[0] + right[1] * ahead[1] + right[2] * ahead[2],
                          upward[0] * ahead[0] + upward[1] * ahead[1] + upward[2] * ahead[2],
                          sun[0] * ahead[0] + sun[1] * ahead[1] + sun[2] * ahead[2]};
            for (float &c : q)
                c = std::round(c / cell) * cell;
            for (int c = 0; c < 3; ++c)
                centre[c] = right[c] * q[0] + upward[c] * q[1] + sun[c] * q[2];
        }
        std::copy(centre, centre + 3, shadowCentres[map]);
        float eye[3] = {centre[0] + sun[0], centre[1] + sun[1], centre[2] + sun[2]};
        float view[16];
        Mat4::lookAt(view, eye, centre, up);
        Mat4::multiply(matrices[map], matrices[map], view);
    }
}

// Draws the near, middle and far shadow maps from the gathered queue, with
// casters up to 250 m, 600 m and 1000 m away.
// Static casters are cached (task 37) where the renderer can: each map's
// cache holds them for the place and sun it was drawn for, and the frame's
// map starts as a copy of it with the moving casters drawn on top. A cache
// is drawn again at once when what it holds changes (the static objects'
// signature) or the camera's tile changed, and one a frame when its centre
// would have moved more than its ShadowLead, the view has turned 30 degrees
// (objects culling themselves by the view come and go), the sun has moved,
// it is a few seconds old, or content it missed (casters whose shape, mesh
// or texture was still coming) may have come and it is ArrivalMs old:
// content streams in while the camera moves, and redrawing every cache for
// each texture or shape, anywhere, cost a third of a frame.
// TSRE_SHADOW_CACHE=0 turns the caches off, TSRE_SHADOW_CACHE_LOG=1 logs
// their redraws.
void RouteEditorGLWidget::renderShadowMaps() {
    computeShadowMatrices();
    renderer->useProgram(Renderer::PROGRAM_SHADOW);
    Mat4::identity(gluu->mvMatrix);
    Mat4::identity(gluu->objStrMatrix);

    static const bool cacheEnabled = qEnvironmentVariable("TSRE_SHADOW_CACHE", "1") != "0";
    static const bool cacheLog = qEnvironmentVariableIsSet("TSRE_SHADOW_CACHE_LOG");
    static QElapsedTimer clock;
    if (!clock.isValid())
        clock.start();
    // The sun moved by more than about 0.1 degrees; the view turned 30.
    constexpr float SunMoved = 0.9999985f;
    const float ViewTurned = std::cos(float(M_PI) / 6.0f);
    constexpr qint64 RefreshMs = 4000;
    constexpr qint64 ArrivalMs = 500;
    const float ranges[3] = {250.0f, 600.0f, 1000.0f};
    const int sizes[3] = {shadowMapSize, middleShadowMapSize, distantShadowMapSize};
    const int statsSlots[3] = {RenderStats::FrameStats::PassSlots - 3, RenderStats::FrameStats::PassSlots - 2,
                               RenderStats::FrameStats::PassSlots - 1};
    const Renderer::Target targets[3] = {Renderer::TARGET_SHADOW_NEAR, Renderer::TARGET_SHADOW_MID,
                                         Renderer::TARGET_SHADOW_FAR};
    float *matrices[3] = {gluu->pShadowMatrix0, gluu->pShadowMatrix, gluu->pShadowMatrix2};
    float *const midMatrix = gluu->pShadowMatrix;
    // The shadow shader reads uShadowPMatrix.
    auto drawWith = [&](float *matrix) {
        gluu->pShadowMatrix = matrix;
        renderer->applyFrameUniforms();
    };

    const bool caching = cacheEnabled && shadowSnapped;
    const quint64 signature = caching ? renderer->shadowSignature() : 0;
    const qint64 now = clock.elapsed();
    float sun[3] = {shadowSunDirection[0], shadowSunDirection[1], shadowSunDirection[2]};
    Vec3::normalize(sun, sun);
    const int tile[2] = {int(camera->pozT[0]), int(camera->pozT[1])};
    // At once: the cache misses what it holds, or its place is in another
    // tile's frame. In turn: it was drawn for another place, view, sun or
    // time; one a frame, the one drawn longest ago, so a fast camera still
    // moves the middle and far maps along.
    bool atOnce[3] = {}, due[3] = {}, arrived[3] = {};
    int inTurn = -1;
    for (int map = 0; caching && map < 3; ++map) {
        const ShadowCache &cache = shadowCaches[map];
        atOnce[map] = !cache.valid || cache.size != sizes[map] || cache.signature != signature
                || cache.tile[0] != tile[0] || cache.tile[1] != tile[1];
        arrived[map] = now - cache.drawnAt > ArrivalMs && cache.incomplete
                && (cache.uploads != Texture::uploads || cache.shapes != ShapeLoader::progress());
        float moved = 0.0f;
        for (int c = 0; c < 3; ++c)
            moved += (shadowCentres[map][c] - cache.centre[c]) * (shadowCentres[map][c] - cache.centre[c]);
        due[map] = moved > ShadowLead[map] * ShadowLead[map]
                || cache.forward[0] * shadowForward[0] + cache.forward[1] * shadowForward[1] < ViewTurned
                || cache.sun[0] * sun[0] + cache.sun[1] * sun[1] + cache.sun[2] * sun[2] < SunMoved
                || now - cache.drawnAt > RefreshMs || arrived[map];
        if (due[map] && !atOnce[map] && (inTurn < 0 || cache.drawnAt < shadowCaches[inTurn].drawnAt))
            inTurn = map;
    }

    // Vegetation in its own maps where the renderer has them (task 37),
    // drawn with the main maps' matrices and cached with them.
    // TSRE_VEGETATION_SHADOWS=0 casts it into the main maps as before, its
    // own maps left empty.
    static const bool splitVegetation = qEnvironmentVariable("TSRE_VEGETATION_SHADOWS", "1") != "0";
    const bool vegetationMaps = renderer->vegetationShadows();
    const bool vegetationSet = vegetationMaps && splitVegetation;
    const Renderer::ShadowSet mainSet = vegetationSet ? Renderer::SET_MAIN : Renderer::SET_ALL;
    const Renderer::Target vegetationTargets[3] = {Renderer::TARGET_VEGETATION_NEAR, Renderer::TARGET_VEGETATION_MID,
                                                   Renderer::TARGET_VEGETATION_FAR};
    for (int map = 0; map < 3; ++map) {
        ShadowCache &cache = shadowCaches[map];
        // The caches of both sets: the main one, then vegetation's.
        bool drawn = false;
        const quint64 waiting = renderer->drawsWaiting();
        if (caching && (atOnce[map] || map == inTurn) && renderer->beginShadowCache(map)) {
            drawWith(matrices[map]);
            renderer->renderShadowCasters(ranges[map], statsSlots[map], matrices[map], Renderer::CASTERS_STATIC,
                                          mainSet);
            drawn = !vegetationSet || renderer->beginShadowCache(map + 3);
            if (vegetationSet && drawn)
                renderer->renderShadowCasters(ranges[map], statsSlots[map], matrices[map],
                                              Renderer::CASTERS_STATIC, Renderer::SET_VEGETATION);
        }
        if (drawn) {
            if (cacheLog)
                qInfo().noquote() << "shadow cache" << map << "drawn:"
                                  << (!cache.valid ? "new" : cache.signature != signature ? "casters"
                                      : cache.size != sizes[map] ? "size"
                                      : cache.tile[0] != tile[0] || cache.tile[1] != tile[1] ? "tile"
                                      : arrived[map] ? "content came"
                                      : now - cache.drawnAt > RefreshMs ? "refresh" : "place, view or sun");
            cache.valid = true;
            std::copy(matrices[map], matrices[map] + 16, cache.matrix);
            std::copy(shadowCentres[map], shadowCentres[map] + 3, cache.centre);
            std::copy(shadowForward, shadowForward + 2, cache.forward);
            std::copy(tile, tile + 2, cache.tile);
            std::copy(sun, sun + 3, cache.sun);
            cache.size = sizes[map];
            cache.signature = signature;
            cache.uploads = Texture::uploads;
            cache.shapes = ShapeLoader::progress();
            cache.incomplete = renderer->drawsWaiting() != waiting;
            cache.drawnAt = now;
        } else if (!caching || atOnce[map]) {
            cache.valid = false;
        }
        if (caching && cache.valid)
            // The cache's place and sun until it is drawn again; the lit
            // shaders read the maps with this matrix.
            std::copy(cache.matrix, cache.matrix + 16, matrices[map]);
        // The frame's map: its cache with the moving casters on top, or every
        // caster. A renderer made since has no cache: drawn again next frame.
        auto drawMap = [&](Renderer::Target target, int cacheIndex, Renderer::ShadowSet set) {
            renderer->bindTarget(target);
            renderer->clear(true, true);
            const bool copied = caching && cache.valid && renderer->copyShadowCache(cacheIndex);
            if (caching && cache.valid && !copied)
                cache.valid = false;
            drawWith(matrices[map]);
            renderer->setViewport(0, 0, sizes[map], sizes[map]);
            renderer->renderShadowCasters(ranges[map], statsSlots[map], matrices[map],
                                          copied ? Renderer::CASTERS_DYNAMIC : Renderer::CASTERS_ALL, set);
        };
        drawMap(targets[map], map, mainSet);
        if (vegetationSet) {
            drawMap(vegetationTargets[map], map + 3, Renderer::SET_VEGETATION);
        } else if (vegetationMaps) {
            renderer->bindTarget(vegetationTargets[map]);
            renderer->clear(true, true);
        }
    }
    gluu->pShadowMatrix = midMatrix;
    renderer->releaseProgram();
}

namespace {
// The inverse of a column-major 4 x 4 matrix (Mat4 has none; QMatrix4x4
// reads its array row by row).
void invertMatrix(float *out, const float *in) {
    const QMatrix4x4 inverse = QMatrix4x4(in).transposed().inverted();
    std::copy(inverse.constData(), inverse.constData() + 16, out);
}
}

void RouteEditorGLWidget::diagnosticTick() {
    Sim::SimulationController::instance()->pinViewFrame();
    // Placed before the camera takes the place of the one it rides (a
    // train followed is placed by followTrain; one ridden only, by the
    // paint, after the camera).
    Sim::SimulationController::instance()->placeViewTrains();
    followTrain();
    camera->update(float(1000.0 / std::max(1.0, cameraFrameMs)));
}

void RouteEditorGLWidget::diagnosticCameraStep(const float move[3], float yaw, float pitch) {
    if (move[0] != 0.0f || move[1] != 0.0f || move[2] != 0.0f) {
        const float *at = camera->getPos();
        camera->setPos(at[0] + move[0], at[1] + move[1], at[2] + move[2]);
        camera->check_coords();
    }
    camera->patrzX(yaw);
    camera->patrzY(pitch);
}

void RouteEditorGLWidget::updateViewMotion() {
    if (!renderer->motionBlurOn())
        return;
    Renderer::ViewMotion motion;
    float view[16];
    std::copy(camera->getMatrix(), camera->getMatrix() + 16, view);
    const float aspect = float(this->width()) / std::max(1, this->height());
    float projection[16];
    Mat4::perspective(projection, Game::cameraFov * M_PI / 180, aspect, 0.2f, Game::objectLod);
    Mat4::multiply(motion.viewProjection, projection, view);
    motion.nearPlane = 0.2f;
    motion.farPlane = Game::objectLod;
    const float tile[2] = {camera->pozT[0], camera->pozT[1]};
    const float *at = camera->getPos();
    const float position[3] = {at[0], at[1], at[2]};
    float placement[8];
    const bool riding = camera->ridingPlacement(placement);
    const qint64 ns = motionClock.isValid() ? motionClock.nsecsElapsed() : 0;
    motionClock.start();
    motion.frameSeconds = motionFrameSeconds > 0.0f ? motionFrameSeconds
            : ns > 0 ? float(double(ns) / 1e9) : 1.0f / 60.0f;
    renderer->beginMotionFrame();
    MotionHistory &h = motionHistory;
    if (h.valid) {
        // Last frame's positions in this frame's space: they lay its tiles
        // over from this one's.
        const float dx = (h.tile[0] - tile[0]) * 2048.0f;
        const float dz = (h.tile[1] - tile[1]) * 2048.0f;
        if (dx != 0.0f || dz != 0.0f)
            renderer->shiftMotion(dx, dz);
        float shift[16];
        Mat4::identity(shift);
        shift[12] = -dx;
        shift[14] = -dz;
        float previous[16];
        Mat4::multiply(previous, h.view, shift);
        float inverse[16];
        invertMatrix(inverse, view);
        Mat4::multiply(motion.world, inverse, previous);
        // Riding the same object: the reference is its last place with the
        // camera's own as now.
        if (riding && h.riding && h.object == camera->getCameraObject()) {
            float before[8];
            std::copy(h.placement, h.placement + 8, before);
            before[2] += (before[0] - tile[0]) * 2048.0f;
            before[4] += (before[1] - tile[1]) * 2048.0f;
            float reference[16];
            camera->viewAtPlacement(before, reference);
            Mat4::multiply(motion.reference, inverse, reference);
        }
        // A jump (a new place, another camera, the view's angle changed): no
        // motion this frame.
        const float moved[3] = {position[0] - (h.position[0] + dx), position[1] - h.position[1],
                                position[2] - (h.position[2] + dz)};
        const float distance = std::sqrt(moved[0] * moved[0] + moved[1] * moved[1] + moved[2] * moved[2]);
        motion.valid = distance < 200.0f && h.fov == Game::cameraFov;
    }
    renderer->setViewMotion(motion);
    h.valid = true;
    std::copy(view, view + 16, h.view);
    std::copy(tile, tile + 2, h.tile);
    std::copy(position, position + 3, h.position);
    h.fov = Game::cameraFov;
    h.riding = riding;
    if (riding)
        std::copy(placement, placement + 8, h.placement);
    h.object = camera->getCameraObject();
}

// The sky enclosure (task 27): terrain and opaque surfaces seen from 2 km
// above the camera, down to 2 km below it, in two maps. The lit shaders find
// in them whether something stands over a point.
// - Near: 400 m across at 1024 texels (0.39 m), everything opaque within
//   300 m. Drawn from a point snapped to cells of 80 of its texels (31.25 m;
//   16 m in height), so its texels keep their place on the ground, and kept
//   while the camera stays in its cell; once a second it is drawn anyway,
//   for edits.
// - Far: 1200 m across at 1024 texels (1.17 m), terrain and objects of 3 m
//   radius and more, behind the near map (bridges, halls, tunnels farther
//   off). Cells of 128 texels (150 m), drawn anyway every 600 frames, and
//   never in a frame the near map is drawn in (a redraw costs a few ms).
// Each map is drawn again from below (the lowest surfaces); where the two
// lie apart there is cover, and the renderer finds from both how open the sky
// is around each point (OpenRadius), so the light fades from the open sky in.
// The terrain's heights under each map go with them: under cover over the
// ground, what lies under the terrain is buried, not a floor.
void RouteEditorGLWidget::renderSkyEnclosure() {
    Renderer::SkyEnclosure enclosure;
    enclosure.enabled = Game::skyEnclosure;
    if (!enclosure.enabled) {
        enclosureAge = farEnclosureAge = -1;
        renderer->setSkyEnclosure(enclosure);
        return;
    }
    constexpr int Size = 1024;
    constexpr float Half = 200.0f;
    constexpr float FarHalf = 600.0f;
    // Over the terrain above deep tunnels, and down to the ground under high
    // bridges (from 300 m above with 600 m of depth, the terrain over a
    // deep tunnel was cut off, and ground more than 300 m below the camera
    // read as covered: a dark square seen from above; user, 2026-10-12).
    constexpr float Above = 2000.0f;
    constexpr float Depth = 4000.0f;
    constexpr float Cell = 80.0f * 2.0f * Half / Size;
    constexpr float FarCell = 128.0f * 2.0f * FarHalf / Size;
    constexpr float CellHeight = 16.0f;
    constexpr int RedrawFrames = 60;
    constexpr int FarRedrawFrames = 600;
    constexpr float FarMinRadius = 3.0f;
    // How far around a point open sky counts for the gradual darkening
    // under cover (a tunnel from its portals in).
    constexpr float OpenRadius = 30.0f;
    // The terrain's heights under a map: texels across (6.25 m on the near
    // map, about the terrain's own 8 m; 18.75 m on the far one).
    constexpr int GroundSize = 64;
    const float *position = camera->getPos();
    auto snap = [position](float cell, float *eye) {
        eye[0] = std::floor(position[0] / cell + 0.5f) * cell;
        eye[1] = std::floor(position[1] / CellHeight + 0.5f) * CellHeight + Above;
        eye[2] = std::floor(position[2] / cell + 0.5f) * cell;
    };
    // From above, or from as far below (the lowest surfaces; its rows run
    // the other way round).
    auto look = [](float half, const float *eye, float *viewProjection, bool below = false) {
        const float sign = below ? -1.0f : 1.0f;
        float from[3] = {eye[0], eye[1] - (below ? 2.0f * Above : 0.0f), eye[2]};
        float centre[3] = {eye[0], from[1] - sign * Above, eye[2]};
        float up[3] = {0.0f, 0.0f, -sign};
        float projection[16], view[16];
        Mat4::ortho(projection, -half, half, -half, half, 0.0f, Depth);
        Mat4::lookAt(view, from, centre, up);
        Mat4::multiply(viewProjection, projection, view);
    };
    auto draw = [this](int map, const float *viewProjection, float range, float minRadius) {
        // Renderers without the maps (OpenGL) draw nothing.
        if (!renderer->beginSkyEnclosure(Size, map))
            return false;
        // The shadow program draws with uShadowPMatrix.
        renderer->useProgram(Renderer::PROGRAM_SHADOW);
        Mat4::identity(gluu->mvMatrix);
        Mat4::identity(gluu->objStrMatrix);
        float *saved = gluu->pShadowMatrix;
        float matrix[16];
        std::copy(viewProjection, viewProjection + 16, matrix);
        gluu->pShadowMatrix = matrix;
        renderer->applyFrameUniforms();
        renderer->renderEnclosureCasters(range, RenderStats::FrameStats::PassSlots - 1, matrix, minRadius);
        gluu->pShadowMatrix = saved;
        renderer->applyFrameUniforms();
        renderer->releaseProgram();
        return true;
    };
    // The terrain's heights under a map, in its coordinates (the map's x and
    // y from the world's x and z: a view from straight above), over its
    // middle height (the eye less Above).
    std::vector<float> ground;
    auto groundUnder = [this, &ground](const float *viewProjection, const float *eye) {
        ground.assign(size_t(GroundSize) * GroundSize, Renderer::EnclosureNoGround);
        const float *m = viewProjection;
        const float determinant = m[0] * m[9] - m[8] * m[1];
        if (Game::terrainLib == nullptr || std::abs(determinant) < 1e-12f)
            return ground.data();
        const int tileX = int(camera->pozT[0]), tileZ = int(camera->pozT[1]);
        const float middle = eye[1] - Above;
        for (int j = 0; j < GroundSize; ++j)
            for (int i = 0; i < GroundSize; ++i) {
                const float u = (i + 0.5f) / GroundSize * 2.0f - 1.0f - m[12];
                const float v = (j + 0.5f) / GroundSize * 2.0f - 1.0f - m[13];
                const float x = (m[9] * u - m[8] * v) / determinant;
                const float z = (m[0] * v - m[1] * u) / determinant;
                float height;
                if (Game::terrainLib->tryGetHeight(tileX, tileZ, x, z, height))
                    ground[size_t(j) * GroundSize + i] = height - middle;
            }
        return ground.data();
    };
    float eye[3], farEye[3];
    snap(Cell, eye);
    snap(FarCell, farEye);
    // What the maps hold: an object dragged in the editor draws the near map
    // again every frame, so its cover follows it; the far map after.
    const quint64 scene = renderer->sceneSignature() + 0x9e3779b97f4a7c15ULL * ShapeLoader::progress();
    const bool redraw = enclosureAge < 0 || enclosureAge >= RedrawFrames
            || !std::equal(eye, eye + 3, enclosureEye) || scene != enclosureScene;
    const bool farRedraw = farEnclosureAge < 0
            || (!redraw && (farEnclosureAge >= FarRedrawFrames || !std::equal(farEye, farEye + 3, farEnclosureEye)
                            || scene != farEnclosureScene));
    look(Half, eye, enclosure.viewProjection);
    enclosure.metresPerDepth = Depth;
    enclosure.filter = 2.0f / Size;
    enclosure.farFilter = 2.0f / Size;
    enclosure.ambientFloor = route != NULL && route->trk != NULL && route->trk->tsreEnclosedAmbient >= 0.0f
            ? route->trk->tsreEnclosedAmbient : Game::enclosedAmbient;
    // TSRE_SHADOW_CACHE_LOG=1 logs these redraws too (task 37).
    static const bool cacheLog = qEnvironmentVariableIsSet("TSRE_SHADOW_CACHE_LOG");
    if (cacheLog && redraw)
        qInfo().noquote() << "enclosure near drawn:"
                          << (enclosureAge < 0 ? "new" : !std::equal(eye, eye + 3, enclosureEye) ? "cell"
                              : scene != enclosureScene ? "objects" : "age");
    if (cacheLog && farRedraw)
        qInfo().noquote() << "enclosure far drawn:"
                          << (farEnclosureAge < 0 ? "new" : !std::equal(farEye, farEye + 3, farEnclosureEye) ? "cell"
                              : scene != farEnclosureScene ? "objects" : "age");
    if (redraw) {
        if (!draw(0, enclosure.viewProjection, Half * 1.5f, 0.0f)) {
            enclosureAge = farEnclosureAge = -1;
            renderer->setSkyEnclosure(Renderer::SkyEnclosure());
            return;
        }
        float below[16];
        look(Half, eye, below, true);
        if (draw(2, below, Half * 1.5f, 0.0f))
            renderer->finishSkyEnclosure(false, OpenRadius / (2.0f * Half), Depth,
                                         groundUnder(enclosure.viewProjection, eye), GroundSize);
        std::copy(eye, eye + 3, enclosureEye);
        enclosureAge = 0;
        enclosureScene = scene;
    } else {
        ++enclosureAge;
    }
    if (farRedraw) {
        float farViewProjection[16];
        look(FarHalf, farEye, farViewProjection);
        if (draw(1, farViewProjection, FarHalf * 1.5f, FarMinRadius)) {
            float below[16];
            look(FarHalf, farEye, below, true);
            if (draw(3, below, FarHalf * 1.5f, FarMinRadius))
                renderer->finishSkyEnclosure(true, OpenRadius / (2.0f * FarHalf), Depth,
                                             groundUnder(farViewProjection, farEye), GroundSize);
            std::copy(farEye, farEye + 3, farEnclosureEye);
            farEnclosureAge = 0;
            farEnclosureScene = scene;
        } else {
            farEnclosureAge = -1;
        }
    } else {
        ++farEnclosureAge;
    }
    // The far map as last drawn (its redraw may wait a frame for the near
    // map's).
    enclosure.farEnabled = farEnclosureAge >= 0;
    if (enclosure.farEnabled)
        look(FarHalf, farEnclosureEye, enclosure.farViewProjection);
    renderer->setSkyEnclosure(enclosure);
}

// Renders the faces scheduled for this frame from the camera position, from
// the queue gathered for the main view: sky, distant terrain, then terrain,
// objects and water without editor overlays. Objects are limited by distance
// and size; every face is rendered once before the round-robin starts.
void RouteEditorGLWidget::renderEnvironmentMap() {
    if (environmentMap == NULL)
        environmentMap = new EnvironmentMap(renderer);
    if (!environmentMap->ensure(Game::environmentMapSize))
        return;
    // The faces must not sample the cube they are drawn into.
    environmentMap->unbind();
    gluu->environmentMapLevels = 0;
    renderer->useProgram(Renderer::PROGRAM_MAIN);
    Mat4::identity(gluu->mvMatrix);
    Mat4::identity(gluu->objStrMatrix);
    // The scene's light (the sun's and the sky's, linear). Faces drawn under a
    // light far from this one mix two lightings in the cube: after a time of
    // day jump PBR surfaces reflected the old sky and took a tilted light
    // from half-updated faces, lit up, then darkened a face a frame (the user,
    // 2026-10-27). Such a jump draws every face at once; a running clock or a
    // slow slider keeps a face a frame.
    auto linearLuminance = [](const float *c) {
        auto linear = [](float v) { return std::pow(std::max(v, 0.0f), 2.2f); };
        return 0.2126f * linear(c[0]) + 0.7152f * linear(c[1]) + 0.0722f * linear(c[2]);
    };
    const float light = linearLuminance(gluu->ambientColor) + linearLuminance(gluu->diffuseColor);
    bool stale = !environmentMap->complete();
    for (int face = 0; face < EnvironmentMap::FaceCount && !stale; ++face) {
        const float drawn = environmentFaceLight[face];
        stale = std::fabs(light - drawn) > 0.25f * std::max({light, drawn, 1e-4f});
    }
    const QVector<int> faces = environmentMap->nextFaces(stale ? EnvironmentMap::FaceCount
            : Game::environmentMapFacesPerFrame);
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
        environmentFaceLight[face] = light;
    }
    environmentMap->endFaces();
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
        waterReflection = new PlanarReflection(renderer);
    const int width = std::max(1, qRound(this->width() * Game::PixelRatio * 0.5f));
    const int targetHeight = std::max(1, qRound(this->height() * Game::PixelRatio * 0.5f));
    if (!waterReflection->ensure(width, targetHeight))
        return false;
    // The mirrored view must not sample the texture it is drawn into.
    waterReflection->unbind();
    renderer->useProgram(Renderer::PROGRAM_MAIN);
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
    waterReflection->end();
    std::copy(plane, plane + 4, waterReflectionPlane);
    return true;
}

void RouteEditorGLWidget::handleSelection() {
    if (!selection)
        return;
    if(selectionTargetHeight <= 0){
        qWarning() << "Selection read requested without an active selection target";
        selection = false;
        update();
        return;
    }

    if(!selectionProbePoints.isEmpty()){
        for(const QPoint &point : selectionProbePoints){
            const int probeY = selectionTargetHeight - point.y() - 1;
            selectionProbeResults.push_back(renderer->readSelection(point.x(), probeY));
        }
        selection = false;
        return;
    }

    const int x = mousex;
    const int realy = selectionTargetHeight - (int)mousey - 1;
    const quint32 selectionId = renderer->readSelection(x, realy);
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

    appliedSelectionId = selectionId;
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
        // While a simulation runs the activity: its train's own consist.
        Sim::SimulationController *simulation = Sim::SimulationController::instance();
        if (simulation->shows(route->getCurrentActivity()))
            setSelectedObj((GameObj*)simulation->consistOf(simulation->trainOfService(CID)));
        else
            setSelectedObj((GameObj*)route->getActivityConsist(CID));
        if (selectedObj == NULL) {
            qDebug() << "brak obiektu";
        } else {
            //qDebug() << "eid"<<EID;
            selectedObj->select(EID);
            setSelectedObj(selectedObj);
            emit activityTrainSelected(CID);
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

    unsigned long long int newTime = QDateTime::currentMSecsSinceEpoch();
    static float winZ[4];
    int viewport[4];

    renderer->viewport(viewport);
    int realy = viewport[3] - (int) y - 1;
    if(newTime - pointerReadTime > 50){
        winZ[0] = pointerReadExact ? renderer->readDepth(x, realy)
                                   : renderer->readDepthLatest(x, realy);
        pointerReadTime = newTime;
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

void RouteEditorGLWidget::surfaceResize(int w, int h) {
    //gluu->m_proj.setToIdentity();
    //gluu->m_proj.perspective(45.0f, GLfloat(w) / h, 0.01f, 100.0f);
}

bool RouteEditorGLWidget::uiScreenReady() {
    if (!uiScreen) {
        uiScreen = std::make_unique<Ui::Screen>();
        // The camera lets go of the keys it holds when the UI takes them.
        uiScreen->setKeysTaken([this] {
            if (camera != NULL)
                camera->releaseKeys();
        });
    }
    if (!uiScreen->ready(qRound(this->width() * Game::PixelRatio), qRound(this->height() * Game::PixelRatio),
                         Game::PixelRatio, Game::hudScale))
        return false;
    if (!uiCursor) {
        uiCursor = true;
        // The pointer shapes the documents ask for (a handle's "move").
        Ui::Runtime::get()->setCursorHandler([this](const QString &name) {
            static const QHash<QString, Qt::CursorShape> shapes = {
                {"move", Qt::SizeAllCursor}, {"pointer", Qt::PointingHandCursor}, {"text", Qt::IBeamCursor},
                {"resize", Qt::SizeFDiagCursor}, {"cross", Qt::CrossCursor}};
            const auto shape = shapes.find(name);
            if (shape != shapes.end())
                setCursor(shape.value());
            else
                unsetCursor();
        });
    }
    return true;
}

void RouteEditorGLWidget::focusOutEvent(QFocusEvent *event) {
    if (camera != NULL)
        camera->releaseKeys();
    QWidget::focusOutEvent(event);
}

void RouteEditorGLWidget::keyPressEvent(QKeyEvent * event) {
    Game::currentShapeLib = currentShapeLib;
    // The UI first: a menu or a text field has the keys, or Esc and F8.
    if (uiScreen && uiScreen->keyPress(event))
        return;
    
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
        // The map takes navigation keys and the active map tool's keys (Escape
        // ends a measurement); the 3D keys stay in 3D.
        EditorTool *tool = activeTool();
        if (tool != nullptr && tool->supports(ViewMode::Map) && tool->key(*this, event)) {
            update();
            event->accept();
            return;
        }
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

    // Following a train in 3D (the Timetable window's Follow): 1 at its
    // cab's window, 2 ahead of its front, 3 behind its back, the camera
    // riding it (the keypad's digits stay the object tools').
    if ((event->key() == Qt::Key_1 || event->key() == Qt::Key_2 || event->key() == Qt::Key_3)
            && followedTrain >= 0 && Sim::SimulationController::instance()->followed() == followedTrain
            && !(event->modifiers() & (Qt::KeypadModifier | Qt::ControlModifier | Qt::AltModifier))) {
        if (!event->isAutoRepeat()) {
            followedView = event->key() == Qt::Key_1 ? RideView::Cab
                         : event->key() == Qt::Key_2 ? RideView::Front : RideView::Back;
            rideTrain(followedTrain, followedView);
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
    if (uiScreen && uiScreen->keyRelease(event))
        return;
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
    // A press over a document's widget is the UI's, with its drag.
    if (uiScreen && uiScreen->mousePress(event))
        return;
    bolckContextMenu = false;
    if (!route->loaded) return;
    m_lastPos = event->position();
    m_lastPos *= Game::PixelRatio;
    mouseClick = true;
    if (currentViewMode == ViewMode::Map) {
        // Left and right drags move the map (middle turns it); a left click that does not move it
        // goes to the active tool (mouseReleaseEvent). A left press on the
        // selected activity object drags it with the select tool instead.
        mapPressPos = m_lastPos;
        mapDraggingObject = false;
        mapEditing = false;
        EditorTool *tool = activeTool();
        if (event->button() == Qt::LeftButton && tool != nullptr
                && tool->supports(ViewMode::Map) && tool->editsByDragging()) {
            mapEditing = true;
            mousex = float(m_lastPos.x());
            mousey = float(m_lastPos.y());
            updateMapPointer();
            Undo::StateBegin();
            tool->press(*this, ToolMouse{m_lastPos, m_lastPos});
            terrainMap->rebuild();
            update();
            setFocus();
            return;
        }
        if (event->button() == Qt::LeftButton && tool != nullptr && tool->id() == "selectTool"
                && selectedObj != NULL && selectedObj->typeObj == GameObj::activityobj
                && appliedSelectionId != 0) {
            const QVector<quint32> picked = probeSelectionIds({m_lastPos.toPoint()});
            mapDraggingObject = !picked.isEmpty() && picked.first() == appliedSelectionId;
        }
        if (mapDraggingObject)
            Undo::StateBegin();
        else
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
    if (uiScreen && uiScreen->wheel(event))
        return;
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
    if (uiScreen && uiScreen->mouseRelease(event))
        return;
    if (!route->loaded) return;
    camera->MouseUp(event);
    if (currentViewMode == ViewMode::Map) {
        mouseClick = false;
        if (mapDraggingObject || mapEditing) {
            if (event->button() == Qt::LeftButton) {
                mapDraggingObject = false;
                mapEditing = false;
                Undo::StateEnd();
            }
            return;
        }
        const QPointF position = event->position() * Game::PixelRatio;
        const QPointF moved = position - mapPressPos;
        EditorTool *tool = activeTool();
        // A right click that does not move the map: the map's menu, acting at
        // the pointer.
        if (event->button() == Qt::RightButton
                && std::abs(moved.x()) + std::abs(moved.y()) <= MapClickPixels) {
            mousex = float(position.x());
            mousey = float(position.y());
            updateMapPointer();
            showMapContextMenu(event->position().toPoint());
            // What the actions changed shows on the next draw.
            terrainMap->invalidate();
            trackItemMap->rebuild();
            activityMap->invalidate();
            mapLabelsInvalid = true;
            update();
            return;
        }
        if (event->button() == Qt::LeftButton && tool != nullptr
                && tool->supports(ViewMode::Map)
                && std::abs(moved.x()) + std::abs(moved.y()) <= MapClickPixels) {
            mousex = float(position.x());
            mousey = float(position.y());
            updateMapPointer();
            Undo::StateBegin();
            tool->press(*this, ToolMouse{position, position});
            Undo::StateEnd();
            // What the tool changed shows on the next draw.
            trackItemMap->rebuild();
            activityMap->invalidate();
            mapLabelsInvalid = true;
            terrainMap->rebuild();
            update();
        }
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
    // Hover for the documents; the UI's drag stays the UI's.
    if (uiScreen && uiScreen->mouseMove(event))
        return;
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
        const QPointF position = event->position() * Game::PixelRatio;
        if (mapEditing && (event->buttons() & Qt::LeftButton)) {
            if (EditorTool *tool = activeTool()) {
                updateMapPointer();
                tool->drag(*this, ToolMouse{position, m_lastPos});
                terrainMap->rebuild();
                update();
            }
        } else if (mapDraggingObject && (event->buttons() & Qt::LeftButton)) {
            if (EditorTool *tool = activeTool()) {
                updateMapPointer();
                tool->drag(*this, ToolMouse{position, m_lastPos});
                activityMap->invalidate();
                mapLabelsInvalid = true;
                update();
            }
        } else if (event->buttons() & (Qt::LeftButton | Qt::RightButton | Qt::MiddleButton)) {
            camera->MouseMove(event);
        }
        m_lastPos = position;
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
        terrainMap->invalidate();
        activityMap->invalidate();
        mapLabelsInvalid = true;
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
        // The camera jumps here from the map: "Create new tiles if not exist"
        // must not write a tile under it unless the user moves on in 3D.
        route->holdAutoNewTile(int(cameraFree->pozT[0]), int(cameraFree->pozT[1]));
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

void RouteEditorGLWidget::setMapTrackObjectKind(int kind, bool visible) {
    MapLayers::set(mapLayers.trackObjectKinds, kind, visible);
    mapLabelsInvalid = true;
    update();
}

void RouteEditorGLWidget::setMapActivityKind(MapActivityKind kind, bool visible) {
    MapLayers::set(mapLayers.activityKinds, int(kind), visible);
    mapLabelsInvalid = true;
    update();
}

bool RouteEditorGLWidget::prepareOsmLayer() {
    //% "OSM Data"
    const QString title = qtTrId("route.editor.route.editor.glwidget.osm.title");
    const QString directory = Settings::string("core.paths.osmData", SettingType::Directory).trimmed();
    if (directory.isEmpty() || !QDir(directory).exists()) {
        QMessageBox::information(this, title,
            //% "Set the OpenStreetMap data directory first, in Settings > Maps and geodata > Geodata. It holds the .osm.pbf files downloaded for your area, for example from Geofabrik."
            qtTrId("route.editor.route.editor.glwidget.osm.no.directory"));
        return false;
    }
    if (Game::GeoCoordConverter == NULL) {
        QMessageBox::information(this, title,
            //% "This route has no geographic reference, so OSM data cannot be placed on it."
            qtTrId("route.editor.route.editor.glwidget.osm.no.reference"));
        return false;
    }
    const MapView &view = cameraMap->view;
    float rect[4];
    OsmMapLayer::viewRect(view, rect);
    Osm::ensureConverted(this, OsmMapLayer::areaOf(Game::GeoCoordConverter, view.tileX, view.tileZ, rect));
    osmMap->invalidate();
    return true;
}

QVector<RouteEditorGLWidget::EnvironmentSlot> RouteEditorGLWidget::environmentSlots() const {
    QVector<EnvironmentSlot> list;
    if (route == NULL || route->trk == NULL)
        return list;
    for (const char *season : {"Spring", "Summer", "Autumn", "Winter"})
        for (const char *kind : {"Clear", "Rain", "Snow"}) {
            const QString name = QString::fromLatin1(season) + QString::fromLatin1(kind);
            for (const auto &entry : route->trk->environment)
                if (QString::fromStdString(entry.first).compare(name, Qt::CaseInsensitive) == 0)
                    list.push_back({name, entry.second});
        }
    return list;
}

QStringList RouteEditorGLWidget::environmentFiles() const {
    const QDir directory(Game::root + "/ROUTES/" + Game::route + "/ENVFILES");
    return directory.entryList({"*.env", "*.ENV"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
}

bool RouteEditorGLWidget::chooseEnvironment(const QString &file, const QString &slot, QString *error) {
    if (route == NULL)
        return false;
    const QString path = ContentPath::normalize(Game::root + "/ROUTES/" + Game::route + "/ENVFILES/" + file);
    if (!QFileInfo::exists(path)) {
        if (error) *error = "No env file " + path;
        return false;
    }
    Env::SlotWeather kind = Env::SlotWeather::Unknown;
    if (slot.endsWith("Clear", Qt::CaseInsensitive)) kind = Env::SlotWeather::Clear;
    else if (slot.endsWith("Rain", Qt::CaseInsensitive)) kind = Env::SlotWeather::Rain;
    else if (slot.endsWith("Snow", Qt::CaseInsensitive)) kind = Env::SlotWeather::Snow;
    Environment *environment = new Environment(path, kind);
    delete route->env;
    route->env = environment;
    currentEnvironmentFile = file;
    currentEnvironmentSlot = slot;
    weatherModified = false;
    if (weatherMap)
        weatherMap->invalidate();
    emit weatherChanged();
    update();
    return true;
}

void RouteEditorGLWidget::setWeatherPreset(const QString &preset, quint32 seed) {
    if (route == NULL || route->env == NULL)
        return;
    Environment &environment = *route->env;
    if (preset.isEmpty()) {
        environment.weather = environment.file.weatherOrDerived(environment.slot);
        QString error;
        if (!environment.weather.loadMaps(QFileInfo(environment.path).absolutePath(), error))
            qWarning() << "Weather maps:" << error;
        weatherModified = false;
    } else {
        environment.weather = WeatherPresets::weather(preset, seed);
        weatherModified = true;
    }
    if (weatherMap)
        weatherMap->invalidate();
    emit weatherChanged();
    update();
}

bool RouteEditorGLWidget::saveEnvironment(QString *error) {
    if (route == NULL || route->env == NULL)
        return false;
    if (!Game::writeEnabled) {
        if (error) *error = "Route saving is disabled.";
        return false;
    }
    Environment &environment = *route->env;
    environment.file.weather = environment.weather;
    QString message;
    if (!environment.file.save(environment.path, message)
            || !environment.weather.saveMaps(QFileInfo(environment.path).absolutePath(), message)) {
        if (error) *error = message;
        return false;
    }
    weatherModified = false;
    emit weatherChanged();
    return true;
}

const Weather *RouteEditorGLWidget::currentWeather() const {
    return route != NULL && route->env != NULL ? &route->env->weather : nullptr;
}

double RouteEditorGLWidget::weatherHours() const {
    if (!std::isnan(diagnosticWeatherHours))
        return diagnosticWeatherHours;
    const Weather *weather = currentWeather();
    if (weather == nullptr)
        return 0.0;
    QDate date = QDate::fromString(Game::timeOfDayDate.trimmed(), "yyyy-MM-dd");
    if (!date.isValid())
        date = QDate(2026, 6, 21);
    return weather->editorHours(date, Game::timeOfDayHours);
}

bool RouteEditorGLWidget::routeArea(double &west, double &north, double &east, double &south) const {
    if (Game::trackDB == NULL)
        return false;
    bool any = false;
    int minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    for (const auto &entry : Game::trackDB->trackNodes) {
        const TRnode *node = entry.second;
        if (node == NULL)
            continue;
        for (int i = 0; i < node->iTrv; ++i) {
            const int x = node->trVectorSection[i].tileX, z = node->trVectorSection[i].tileZ;
            minX = any ? std::min(minX, x) : x;
            maxX = any ? std::max(maxX, x) : x;
            minZ = any ? std::min(minZ, z) : z;
            maxZ = any ? std::max(maxZ, z) : z;
            any = true;
        }
    }
    if (!any)
        return false;
    // Whole tiles; MSTS tile z counts north.
    west = minX * 2048.0 - 1024.0;
    east = maxX * 2048.0 + 1024.0;
    south = minZ * 2048.0 - 1024.0;
    north = maxZ * 2048.0 + 1024.0;
    return true;
}

void RouteEditorGLWidget::cameraWorldPosition(double &east, double &north) const {
    east = camera->pozT[0] * 2048.0 + camera->getPos()[0];
    north = -(camera->pozT[1] * 2048.0 + camera->getPos()[2]);
}

WeatherFog::State RouteEditorGLWidget::weatherFog() const {
    const Weather *weather = currentWeather();
    if (weather == nullptr || weather->keyframes.isEmpty() || camera == nullptr)
        return WeatherFog::State();
    QDate date = QDate::fromString(Game::timeOfDayDate.trimmed(), "yyyy-MM-dd");
    if (!date.isValid())
        date = QDate(2026, 6, 21);
    // Weather hours to the editor's solar time, over the days around.
    const double now = weatherHours();
    const double latitude = timeOfDay.latitude, longitude = timeOfDay.longitude;
    const double solarNow = Game::timeOfDayHours;
    auto sun = [&](double hours) {
        const double solar = solarNow + (hours - now);
        const int days = int(std::floor(solar / 24.0));
        return SunPosition::atSolarTime(latitude, longitude, date.addDays(days), solar - days * 24.0).elevation;
    };
    double east, north;
    cameraWorldPosition(east, north);
    return WeatherFog::at(*weather, now, east, north, sun);
}

void RouteEditorGLWidget::applyFog() {
    if (renderer == NULL)
        return;
    Renderer::Fog f;
    f.quality = mistQuality;
    if (mistQuality > 0 && fogNow.amount > 0.0f && fogFloor.valid()) {
        constexpr int N = FogFloor::Size;
        if (fogMapBuilt != fogFloorBuilds) {
            // Cells without terrain lie far below everything: no fog.
            fogMapCells.resize(size_t(N) * N * 4);
            for (int row = 0; row < N; ++row)
                for (int column = 0; column < N; ++column) {
                    const FogFloor::Sample s = fogFloor.cell(column, row);
                    float *cell = &fogMapCells[(size_t(row) * N + column) * 4];
                    cell[0] = s.known ? s.floor : -10000.0f;
                    cell[1] = FogFloor::share(s);
                    cell[2] = s.known ? s.ground : -10000.0f;
                    cell[3] = s.known ? s.moisture : 0.0f;
                }
            fogMapBuilt = fogFloorBuilds;
            ++fogMapRevision;
        }
        f.amount = fogNow.amount;
        f.visibility = fogNow.visibility;
        f.depth = fogNow.depth;
        f.base = fogNow.base;
        f.patchiness = fogNow.patchiness;
        f.pooling = fogNow.pooling;
        f.map = &fogMapCells;
        f.size = N;
        f.west = fogFloor.west();
        f.north = fogFloor.north();
        f.cell = float(FogFloor::CellSize);
        f.revision = fogMapRevision;
        cameraWorldPosition(f.cameraEast, f.cameraNorth);
        // Its patches drift with the low clouds' wind (about 100 m up),
        // weaker near the ground.
        if (const Weather *weather = currentWeather()) {
            double east = 0.0, north = 0.0;
            weather->drift(weatherHours(), WeatherLayer::Low, east, north);
            f.driftEast = east * 0.4;
            f.driftNorth = north * 0.4;
        }
    }
    // Rain or snow near the view (the volume carries it; with the fog off
    // too): the area's at the camera, the low clouds' base there.
    if (Game::cloudQuality > 0 && renderer->drawsPhysicalSky()) {
        if (const Weather *weather = currentWeather(); weather != nullptr && !weather->keyframes.isEmpty()) {
            double east, north;
            cameraWorldPosition(east, north);
            const Weather::Frame frame = weather->frame(weatherHours());
            const WeatherSurface s = WeatherDetail::precipitation(*weather, frame, east, north, 0.0);
            if (s.precipitationRate > 0.05f) {
                f.rain = s.precipitationRate;
                f.snow = s.snowShare;
                f.rainBase = weather->layer(frame, east, north, WeatherLayer::Low).base;
                // At the camera itself, under a shower's cell or not (none
                // under the terrain: in a tunnel; under roofs the sky
                // enclosure's maps take the drops away); the low wind.
                f.rainHere = WeatherDetail::precipitation(*weather, frame, east, north, 1.0).precipitationRate;
                const float *position = camera->getPos();
                if (Game::terrainLib != nullptr
                        && Game::terrainLib->getHeight(int(camera->pozT[0]), int(camera->pozT[1]), position[0],
                                                       position[2]) > position[1] + 2.0f)
                    f.rainHere = 0.0f;
                Weather::velocity(weather->wind(weatherHours(), WeatherLayer::Low), f.rainWindEast, f.rainWindNorth);
            }
        }
    }
    renderer->setFog(f);
}

bool RouteEditorGLWidget::terrainShadeWanted() const {
    // Diagnostics: TSRE_NO_TERRAIN_SHADE=1 leaves it out.
    static const bool off = qEnvironmentVariableIntValue("TSRE_NO_TERRAIN_SHADE") != 0;
    return !off && (Game::shadowsEnabled > 0 || Game::lightShafts == 2 || mistQuality > 0);
}

// The terrain's shade of the sun or moon (TerrainShade): made again on a
// worker thread (1.2 ms on the Steam Deck) when the floor map is new or the
// light has moved by 0.03 degrees (a ridge 1 km off shifts its shade half a
// metre up or down); the last one is used until it is ready.
void RouteEditorGLWidget::applyTerrainShade() {
    if (renderer == NULL)
        return;
    if (terrainShadeBuilding.valid()
            && terrainShadeBuilding.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        terrainShade = terrainShadeBuilding.get();
        ++terrainShadeRevision;
        // Diagnostics: TSRE_FOG_LOG=1 logs each shade made for a new floor
        // map, and the light at the camera.
        static const bool log = qEnvironmentVariableIsSet("TSRE_FOG_LOG");
        if (log && terrainShade.valid() && terrainShadeLogged != terrainShadeFloor) {
            terrainShadeLogged = terrainShadeFloor;
            double east, north;
            cameraWorldPosition(east, north);
            const float *position = camera->getPos();
            qInfo().noquote() << QString("[fog] terrain shade: light %1 deg up; at the camera (%2 m) %3")
                                 .arg(std::asin(std::clamp(terrainShadeLight[1], -1.0f, 1.0f)) * 180.0 / M_PI, 0, 'f', 2)
                                 .arg(position[1], 0, 'f', 1)
                                 .arg(terrainShade.light(east, north, position[1]), 0, 'f', 2);
        }
    }
    Renderer::TerrainShade s;
    const float *l = Game::sunLightDirection;
    const float length = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    if (terrainShadeWanted() && fogFloor.valid() && length > 0.0f) {
        const std::array<float, 3> towards = {l[0] / length, l[1] / length, l[2] / length};
        const float moved = std::sqrt(std::pow(towards[0] - terrainShadeLight[0], 2.0f)
                                      + std::pow(towards[1] - terrainShadeLight[1], 2.0f)
                                      + std::pow(towards[2] - terrainShadeLight[2], 2.0f));
        if (!terrainShadeBuilding.valid()
                && (terrainShadeFloor != fogFloorBuilds || moved > 0.03f * float(M_PI) / 180.0f)) {
            std::copy(towards.begin(), towards.end(), terrainShadeLight);
            terrainShadeFloor = fogFloorBuilds;
            terrainShadeBuilding = std::async(std::launch::async, [west = fogFloor.west(), north = fogFloor.north(),
                                                                   lowest = fogFloor.lowestGround(), towards] {
                TerrainShade shade;
                shade.build(west, north, lowest, towards.data());
                return shade;
            });
        }
        if (terrainShade.valid()) {
            s.map = &terrainShade.tops();
            s.size = TerrainShade::Size;
            s.west = terrainShade.west();
            s.north = terrainShade.north();
            s.cell = float(TerrainShade::CellSize);
            s.reference = terrainShade.reference();
            s.revision = terrainShadeRevision;
            cameraWorldPosition(s.cameraEast, s.cameraNorth);
        }
    }
    // The next frame takes in the shade being made.
    if (terrainShadeBuilding.valid())
        update();
    renderer->setTerrainShade(s);
}

void RouteEditorGLWidget::updateFog() {
    if (camera == nullptr || Game::terrainLib == nullptr)
        return;
    double east, north;
    cameraWorldPosition(east, north);
    const double hours = weatherHours();
    const bool weather = currentWeather() != nullptr;
    if (weather && (std::isnan(fogNowHours) || fogNowRevision != weatherRevision
                    || std::abs(hours - fogNowHours) > 1.0 / 60.0
                    || std::hypot(east - fogNowEast, north - fogNowNorth) > 1000.0)) {
        const WeatherFog::State before = fogNow;
        fogNow = weatherFog();
        fogNowHours = hours;
        fogNowRevision = weatherRevision;
        fogNowEast = east;
        fogNowNorth = north;
        if (fogMap && (fogNow.amount != before.amount || fogNow.depth != before.depth || fogNow.base != before.base
                       || fogNow.pooling != before.pooling))
            fogMap->invalidate();
    }
    // The floor map: for the fog, and for the terrain's shade.
    if (!(weather && fogNow.amount > 0.0f) && !terrainShadeWanted())
        return;
    // A new floor map once the camera is 1 km from the last one's centre, or
    // when terrain tiles have loaded or gone since (at most once a second
    // while they stream in).
    if (!fogFloorSampler.active()) {
        const bool moved = !fogFloor.valid()
                || std::hypot(east - fogFloorSampler.centreEast(), north - fogFloorSampler.centreNorth()) > 1000.0;
        const bool tiles = Game::terrainLib->availabilityRevision() != fogFloorTerrain
                && (!fogFloorAge.isValid() || fogFloorAge.elapsed() > 1000);
        if (moved || tiles) {
            fogFloorTerrain = Game::terrainLib->availabilityRevision();
            fogFloorSampler.start(east, north);
        }
    }
    if (fogFloorSampler.active()) {
        // World tiles for about 1.5 ms a frame, then the map built on a
        // worker thread (in a frame with all, 6 ms on the Steam Deck).
        QElapsedTimer sampling;
        sampling.start();
        bool built = false;
        do
            built = fogFloorSampler.step(*Game::terrainLib, 1, fogFloor);
        while (!built && fogFloorSampler.sampling() && sampling.nsecsElapsed() < 1500000);
        fogFloorSamplingNs += sampling.nsecsElapsed();
        fogFloorSamplingPeakNs = std::max(fogFloorSamplingPeakNs, sampling.nsecsElapsed());
        ++fogFloorSamplingSteps;
        if (built) {
            fogFloorAge.restart();
            ++fogFloorBuilds;
            if (fogMap)
                fogMap->invalidate();
            // Tiles whose terrain is far off their neighbours' are left out
            // of the fog's floor: once a tile, a warning (MSTS tile numbers).
            static std::set<std::pair<int, int>> warned;
            for (const auto &tile : fogFloorSampler.rejectedTiles)
                if (warned.insert(tile).second)
                    qWarning().noquote() << QString("Terrain tile %1, %2 lies far off its neighbours' at their "
                                                    "edges; the fog's floor leaves it out")
                                            .arg(tile.first).arg(-tile.second);
            // Diagnostics: TSRE_FOG_LOG=1 logs each floor map built.
            static const bool log = qEnvironmentVariableIsSet("TSRE_FOG_LOG");
            if (log) {
                float groundLow = 1e9f, groundHigh = -1e9f, floorLow = 1e9f, floorHigh = -1e9f;
                int fogged = 0, wet = 0;
                for (int row = 0; row < FogFloor::Size; ++row)
                    for (int column = 0; column < FogFloor::Size; ++column) {
                        const FogFloor::Sample s = fogFloor.cell(column, row);
                        if (!s.known)
                            continue;
                        groundLow = std::min(groundLow, s.ground);
                        groundHigh = std::max(groundHigh, s.ground);
                        floorLow = std::min(floorLow, s.floor);
                        floorHigh = std::max(floorHigh, s.floor);
                        fogged += FogFloor::top(s, fogNow.depth, fogNow.pooling) > s.ground ? 1 : 0;
                        wet += s.moisture > 0.5f ? 1 : 0;
                    }
                qInfo().noquote() << QString("[fog] floor map: %1 of %2 cells known, ground %3-%4 m, floor %5-%6 m, "
                                             "%7 under %8 m of fog, %9 wet; world tiles %10 detailed, %11 distant, "
                                             "%12 without terrain, %13 left out as wrong")
                                     .arg(FogFloor::Size * FogFloor::Size - fogFloor.unknownCells())
                                     .arg(FogFloor::Size * FogFloor::Size).arg(groundLow, 0, 'f', 0).arg(groundHigh, 0, 'f', 0)
                                     .arg(floorLow, 0, 'f', 0).arg(floorHigh, 0, 'f', 0).arg(fogged)
                                     .arg(fogNow.depth, 0, 'f', 0).arg(wet).arg(fogFloorSampler.detailedTiles)
                                     .arg(fogFloorSampler.distantTiles).arg(fogFloorSampler.missingTiles)
                                     .arg(fogFloorSampler.rejectedTiles.size());
                qInfo().noquote() << QString("[fog] floor map sampled in %1 ms over %2 frames, at most %3 ms in one")
                                     .arg(fogFloorSamplingNs / 1e6, 0, 'f', 1).arg(fogFloorSamplingSteps)
                                     .arg(fogFloorSamplingPeakNs / 1e6, 0, 'f', 2);
            }
            fogFloorSamplingNs = fogFloorSamplingPeakNs = 0;
            fogFloorSamplingSteps = 0;
        }
        // The next tiles in the next frame.
        update();
    }
}

namespace {

// A route position (MSTS world metres) on the globe.
bool worldToLatLon(double east, double north, double &latitude, double &longitude) {
    if (Game::GeoCoordConverter == nullptr)
        return false;
    const int tileX = int(std::floor((east + 1024.0) / 2048.0));
    const int tileZ = int(std::floor((north + 1024.0) / 2048.0));
    PreciseTileCoordinate tile;
    // As the camera fills it: MSTS tile z, local z pointing south.
    tile.setTWxyz(tileX, tileZ, float(east - tileX * 2048.0), 0.0f, float(-(north - tileZ * 2048.0)));
    IghCoordinate internal;
    LatitudeLongitudeCoordinate out;
    if (Game::GeoCoordConverter->ConvertToInternal(&tile, &internal) == NULL
            || Game::GeoCoordConverter->ConvertToLatLon(&internal, &out) == NULL
            || !std::isfinite(out.Latitude) || !std::isfinite(out.Longitude))
        return false;
    latitude = out.Latitude;
    longitude = out.Longitude;
    return true;
}

} // namespace

bool RouteEditorGLWidget::importWeather(const WeatherImport::Request &asked, QString *error,
                                        const std::function<bool()> &cancelled) {
    if (route == NULL || route->env == NULL)
        return false;
    QString message;
    WeatherImport::Request request = asked;
    // Automatic: the finest model with data over the area.
    if (request.model == QLatin1String(WeatherImport::Automatic)
            && !WeatherImport::chooseModel(request, worldToLatLon, request.model, message, cancelled)) {
        if (error) *error = message;
        return false;
    }
    QVector<WeatherImport::Sample> samples;
    int columns = 0, rows = 0;
    if (!WeatherImport::samples(request, worldToLatLon, samples, columns, rows, message)) {
        if (error) *error = message;
        return false;
    }
    // The bearing of the grid's north at the area's centre, for the winds.
    const double centreE = (request.west + request.east) * 0.5, centreN = (request.north + request.south) * 0.5;
    double lat0, lon0, lat1, lon1, bearing = 0.0;
    if (worldToLatLon(centreE, centreN, lat0, lon0) && worldToLatLon(centreE, centreN + 1000.0, lat1, lon1))
        bearing = std::atan2((lon1 - lon0) * std::cos(lat0 * M_PI / 180.0), lat1 - lat0) * 180.0 / M_PI;
    QVector<WeatherImport::Series> series;
    Weather weather;
    if (!WeatherImport::download(request, samples, series, message, cancelled)
            || !WeatherImport::build(request, samples, columns, rows, series, bearing, weather, message)) {
        if (error) *error = message;
        return false;
    }
    weather.maps = "WEATHER/" + QFileInfo(route->env->path).completeBaseName();
    route->env->weather = weather;
    weatherModified = true;
    if (weatherMap)
        weatherMap->invalidate();
    emit weatherChanged();
    update();
    return true;
}

bool RouteEditorGLWidget::generateWeather(const WeatherGenerator::Settings &settings) {
    if (route == NULL || route->env == NULL)
        return false;
    Weather weather = WeatherGenerator::generate(settings);
    weather.maps = "WEATHER/" + QFileInfo(route->env->path).completeBaseName();
    route->env->weather = weather;
    weatherModified = true;
    if (weatherMap)
        weatherMap->invalidate();
    emit weatherChanged();
    update();
    return true;
}

void RouteEditorGLWidget::setDiagnosticWeather(const QString &preset, quint32 seed, double hours) {
    setWeatherPreset(preset, seed);
    diagnosticWeatherHours = hours;
}

bool RouteEditorGLWidget::prepareImageryLayer() {
    QString error;
    if (imageryMap && imageryMap->prepare(error))
        return true;
    QMessageBox::information(this,
        //% "Imagery"
        qtTrId("route.editor.route.editor.glwidget.imagery.title"),
        Game::GeoCoordConverter == NULL ?
            //% "This route has no geographic reference, so imagery cannot be placed on it."
            qtTrId("route.editor.route.editor.glwidget.imagery.no.reference") :
            //% "No imagery source can be shown: %1"
            qtTrId("route.editor.route.editor.glwidget.imagery.no.source").arg(error));
    return false;
}

void RouteEditorGLWidget::showImageryAttribution(bool show) {
    if (!show) {
        if (imageryAttribution != NULL)
            imageryAttribution->hide();
        return;
    }
    if (imageryAttribution == NULL) {
        imageryAttribution = new QLabel(this);
        imageryAttribution->setAttribute(Qt::WA_TransparentForMouseEvents);
        imageryAttribution->setStyleSheet(
            QStringLiteral("QLabel { background: rgba(255, 255, 255, 180); color: black; padding: 1px 4px; }"));
    }
    const QString text = imageryMap ? imageryMap->attribution() : QString();
    if (imageryAttribution->text() != text) {
        imageryAttribution->setText(text);
        imageryAttribution->adjustSize();
    }
    const QPoint corner(width() - imageryAttribution->width(), height() - imageryAttribution->height());
    if (imageryAttribution->pos() != corner)
        imageryAttribution->move(corner);
    if (!imageryAttribution->isVisible() && !text.isEmpty()) {
        imageryAttribution->show();
        imageryAttribution->raise();
    }
}

void RouteEditorGLWidget::setMapMeasurement(const MapGroundPoint &from, const MapGroundPoint &to) {
    mapMeasure->set(from, to);
    update();
}

void RouteEditorGLWidget::setDiagnosticMapMeasurement(const MapGroundPoint *from, const MapGroundPoint *to) {
    if (from != nullptr && to != nullptr)
        setMapMeasurement(*from, *to);
    else
        clearMapMeasurement();
}

void RouteEditorGLWidget::clearMapMeasurement() {
    mapMeasure->clear();
    update();
}

void RouteEditorGLWidget::showMapContextMenu(const QPoint &position) {
    QMenu menu;
    // Always first: measuring is wanted at any moment, whatever the tool.
    QAction *measure = menu.addAction(
        //% "Measure Distance"
        qtTrId("route.editor.map.measure.distance"));
    measure->setCheckable(true);
    measure->setChecked(toolEnabled == MapTools::MeasureToolId);
    EditorTool *tool = activeTool();
    if (tool != nullptr && tool->supports(ViewMode::Map)) {
        QAction *separator = menu.addSeparator();
        tool->contextMenu(*this, menu);
        if (menu.actions().constLast() == separator)
            menu.removeAction(separator);
    }
    if (menu.exec(mapToGlobal(position)) != measure)
        return;
    if (measure->isChecked()) {
        toolBeforeMeasure = toolEnabled;
        enableTool(MapTools::MeasureToolId);
    } else {
        enableTool(tools->allowed(toolBeforeMeasure, ViewMode::Map) ? toolBeforeMeasure : QString());
    }
}

bool RouteEditorGLWidget::mapLayersBusy() const {
    return (osmMap && osmMap->busy()) || (imageryMap && mapLayers.shows(MapLayer::Imagery) && imageryMap->busy())
            || (weatherMap && mapLayers.shows(MapLayer::Weather) && weatherMap->busy());
}

void RouteEditorGLWidget::updateMapLabels() {
    Coords *markers = mapLayers.shows(MapLayer::Markers) && route != NULL ? route->currentMkr() : NULL;
    TDB *track = mapLayers.shows(MapLayer::TrackObjects) ? Game::trackDB : NULL;
    Activity *activity = mapLayers.shows(MapLayer::Activity)
            && MapLayers::has(mapLayers.activityKinds, int(MapActivityKind::Events)) && route != NULL
            ? route->getCurrentActivity() : NULL;
    const OsmMapLayer *osm = mapLayers.shows(MapLayer::OsmData) ? osmMap.get() : NULL;
    // The simulation's messages beside its trains (task simulation 06): they
    // follow the trains while shown.
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const bool messages = activity != NULL && simulation->shows(activity);
    const unsigned long long messagesVersion = messages ? simulation->messagesVersion() : 0;
    const void *messageFrame = messagesVersion % 1000 != 0 ? simulation->viewFrame().get() : NULL;
    // The activity's trains named beside them (Activity Objects > Trains and
    // Train Names). Running, they follow the trains, ten times a second at
    // most; before a run, their consists placed at their starts.
    Activity *trainsNamed = mapLayers.shows(MapLayer::Activity)
            && MapLayers::has(mapLayers.activityKinds, int(MapActivityKind::Trains))
            && MapLayers::has(mapLayers.activityKinds, int(MapActivityKind::TrainNames)) && route != NULL
            ? route->getCurrentActivity() : NULL;
    if (trainsNamed != NULL && (!trainLabelsTaken.isValid() || trainLabelsTaken.elapsed() >= 100)) {
        std::vector<MapLabel> now;
        MapLabelSources::appendTrains(now, trainsNamed);
        trainLabelsTaken.start();
        auto same = [](const MapLabel &a, const MapLabel &b) {
            return a.tileX == b.tileX && a.tileZ == b.tileZ && a.x == b.x && a.z == b.z && a.text == b.text
                    && a.selectionId == b.selectionId;
        };
        if (!std::equal(now.begin(), now.end(), trainLabels.begin(), trainLabels.end(), same)) {
            trainLabels = std::move(now);
            ++trainLabelsVersion;
        }
    } else if (trainsNamed == NULL && !trainLabels.empty()) {
        trainLabels.clear();
        ++trainLabelsVersion;
    }
    // What is labelled, and how much of it: a change rebuilds the labels.
    const std::vector<const void *> sources = {
        osm, reinterpret_cast<const void *>(quintptr(osm != NULL ? osm->labelsVersion() : 0)),
        markers, reinterpret_cast<const void *>(quintptr(markers != NULL ? markers->markerList.size() : 0)),
        track, reinterpret_cast<const void *>(quintptr(track != NULL ? track->trackItems.size() : 0)),
        activity, reinterpret_cast<const void *>(quintptr(activity != NULL ? activity->event.size() : 0)),
        reinterpret_cast<const void *>(quintptr(Sim::EventResultStore::instance()->version())),
        reinterpret_cast<const void *>(quintptr(messagesVersion)), messageFrame,
        reinterpret_cast<const void *>(quintptr(trainLabelsVersion)),
        mapMeasure.get(), reinterpret_cast<const void *>(quintptr(mapMeasure->version()))};
    if (!mapLabelsInvalid && sources == mapLabelSources)
        return;
    std::vector<MapLabel> labels;
    if (mapMeasure->shown())
        labels.push_back(mapMeasure->label(Game::GeoCoordConverter));
    if (messages)
        simulation->messageLabels(labels);
    labels.insert(labels.end(), trainLabels.begin(), trainLabels.end());
    MapLabelSources::appendActivity(labels, activity);
    MapLabelSources::appendTrackDatabase(labels, track,
                                         MapLayers::has(mapLayers.trackObjectKinds, TrackItemMapLayer::Platform),
                                         MapLayers::has(mapLayers.trackObjectKinds, TrackItemMapLayer::Siding));
    MapLabelSources::appendMarkers(labels, markers);
    if (osm != NULL)
        labels.insert(labels.end(), osm->labels().begin(), osm->labels().end());
    mapLabels->setLabels(std::move(labels));
    mapLabelSources = sources;
    mapLabelsInvalid = false;
}

void RouteEditorGLWidget::updateMapPointer() {
    float x, z;
    cameraMap->view.groundAt(mousex, mousey, x, z);
    aktPointerPos[0] = x;
    aktPointerPos[1] = 0.0f;
    aktPointerPos[2] = z;
}

void RouteEditorGLWidget::paintMap() {
    if (renderer == NULL || !renderer->programsReady()) {
        if (selection) {
            qWarning() << "Selection shader is unavailable";
            selection = false;
            update();
        }
        return;
    }
    // The map shows its palette as it is: no auto exposure (task 29), tone
    // curve, exposure or bloom.
    renderer->setExposureControl(Renderer::ExposureControl());
    renderer->setPlainImage(true);
    const int width = qRound(float(this->width()) * Game::PixelRatio);
    const int height = qRound(float(this->height()) * Game::PixelRatio);
    cameraMap->setViewport(width, height);
    updateMapPointer();
    if (selection) {
        paintMapSelection(width, height);
        return;
    }
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
    if (mapLayers.shows(MapLayer::Terrain))
        terrainMap->pushRenderItems(queue, view, palette, Game::terrainLib);
    if (mapLayers.shows(MapLayer::Imagery))
        imageryMap->pushRenderItems(queue, view);
    else
        imageryMap->pause();
    if (mapLayers.shows(MapLayer::OsmData))
        osmMap->pushRenderItems(queue, view, palette,
                                Settings::string("core.paths.osmData", SettingType::Directory),
                                mapLayers.shows(MapLayer::OsmTransparentAreas));
    // Over terrain and OSM data, under the route's own data.
    if (mapLayers.shows(MapLayer::FadedOverlay))
        mapFade->pushRenderItems(queue, view, palette);
    // The quadtree tool: the quad its menu would act on.
    if (toolEnabled == "quadTreeTool" && Game::terrainLib != NULL) {
        if (QuadTree *tree = Game::terrainLib->currentTree()) {
            int x = tileX(), z = tileZ();
            float px = aktPointerPos[0], pz = aktPointerPos[2];
            Game::check_coords(x, z, px, pz);
            const QuadTree::Quad quad = tree->quadAt(x, -z);
            terrainMap->pushQuadHighlight(queue, view, palette, quad.x, quad.y, quad.level);
        }
    }
    trackMap->pushRenderItems(queue, view, palette, Game::trackDB, Game::roadDB, mapLayers);
    if (mapLayers.shows(MapLayer::TrackObjects))
        trackItemMap->pushRenderItems(queue, view, palette, route, Game::trackDB, Game::roadDB,
                                      mapLayers.trackObjectKinds);
    activityMap->pushRenderItems(queue, view, palette, route, mapLayers.shows(MapLayer::Activity),
                                 mapLayers.shows(MapLayer::Paths), mapLayers.activityKinds);
    // The weather over the route's data, as a weather app shows it.
    if (mapLayers.shows(MapLayer::Weather) && weatherMap && currentWeather())
        weatherMap->pushRenderItems(queue, view, palette, *currentWeather(), weatherHours(), true);
    // Where the fog lies, over the clouds.
    if (mapLayers.shows(MapLayer::Weather) && currentWeather()) {
        updateFog();
        if (!fogMap)
            fogMap = std::make_unique<FogMapLayer>();
        fogMap->pushRenderItems(queue, view, palette, fogFloor, fogNow);
    }
    mapMeasure->pushRenderItems(queue, view, palette, Game::PixelRatio);
    updateMapLabels();
    mapLabels->pushRenderItems(queue, view, palette, Game::PixelRatio);
    if (mapLayers.shows(MapLayer::ScaleBar))
        mapScaleBar->pushRenderItems(queue, view, palette, Game::PixelRatio);
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

    renderer->bindTarget(Renderer::TARGET_VIEW);
    renderer->useProgram(Renderer::PROGRAM_MAIN);
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
        renderer->applyFrameUniforms();
        renderer->setFogLod(0.0f);
        renderer->setLayer(RenderQueue::LAYER_UI);
        compass->pushRenderItem(queue, camera->getRotX()+M_PI);
        compassPointer->pushRenderItem(queue);
        renderer->setLayer(RenderQueue::LAYER_SCENE);
        renderer->renderPasses(Renderer::PASS_UI, Renderer::PASS_UI);
    }
    renderer->renderFrame();
    gluu->fogDensity = fogDensity;
    Game::shadowsEnabled = shadows;
    renderer->releaseProgram();
    if (this->isActiveWindow()) {
        emit this->posInfo(camera->getCurrentPos());
        emit this->pointerInfo(aktPointerPos);
    }
    drawEditorFpsHud();
}

void RouteEditorGLWidget::paintMapSelection(int width, int height) {
    if (!renderer->beginSelection(width, height)) {
        qWarning() << "Could not start the map selection pass";
        selection = false;
        update();
        return;
    }
    renderer->resetFrame();
    renderer->setViewPosition(camera->getPos());
    RenderQueue &queue = *renderer;
    Mat4::identity(renderer->transform());
    renderer->setShadowCasting(false);
    renderer->setLayer(RenderQueue::LAYER_OVERLAY);
    const MapView &view = cameraMap->view;
    if (!mapSelection)
        mapSelection = std::make_unique<MapSelection>();
    mapSelection->clear();
    if (mapLayers.shows(MapLayer::TrackObjects))
        trackItemMap->pushSelection(*mapSelection, view);
    activityMap->pushSelection(*mapSelection, view, route, mapLayers.shows(MapLayer::Activity),
                               mapLayers.activityKinds);
    // The trains' names pick their trains (above the trains, as drawn).
    mapLabels->pushSelection(*mapSelection);
    mapSelection->pushRenderItems(queue);
    renderer->setLayer(RenderQueue::LAYER_SCENE);
    renderer->setShadowCasting(true);

    renderer->useProgram(Renderer::PROGRAM_SELECTION);
    const bool blendingWasEnabled = renderer->setBlending(false);
    Mat4::identity(gluu->mvMatrix);
    Mat4::identity(renderer->transform());
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
    renderer->renderPasses(Renderer::PASS_TERRAIN, Renderer::PASS_OVERLAY);
    renderer->endView(mapView);
    renderer->renderFrame();
    if (blendingWasEnabled)
        renderer->setBlending(true);
    selectionTargetHeight = height;
    handleSelection();
    selectionTargetHeight = 0;
    renderer->endSelection();
    renderer->releaseProgram();
}

bool RouteEditorGLWidget::pointerOnTrack(int &tileX, int &tileZ, float *position) {
    if (Game::trackDB == NULL)
        return false;
    Game::check_coords(tileX, tileZ, position);
    float posT[2] = {float(tileX), float(tileZ)};
    if (Game::trackDB->findNearestPositionOnTDB(posT, position, NULL, NULL, false) < 0)
        return false;
    tileX = int(posT[0]);
    tileZ = int(posT[1]);
    return true;
}

bool RouteEditorGLWidget::prepareTerrainEdit(bool procedural) {
    if (currentViewMode != ViewMode::Map)
        return true;
    const MapView &view = cameraMap->view;
    // Distant tiles being edited show their patches at every zoom.
    const bool patches = (Game::terrainLib != NULL && Game::terrainLib->distantIsCurrent())
            || TerrainMapLayer::drawsDetailedPatches(view);
    if (procedural ? !TerrainMapLayer::drawsProcedural(view) : !patches)
        return false;
    return prepareTerrainTile();
}

bool RouteEditorGLWidget::prepareTerrainTile() {
    if (currentViewMode != ViewMode::Map)
        return true;
    if (Game::terrainLib == NULL)
        return false;
    int x = tileX(), z = tileZ();
    float px = aktPointerPos[0], pz = aktPointerPos[2];
    Game::check_coords(x, z, px, pz);
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j)
            Game::terrainLib->getTerrainByXY(x + i, z + j, true);
    return true;
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

bool RouteEditorGLWidget::runDiagnosticOverlayAction(const QString &action) {
    EditorTool *tool = tools->find(QStringLiteral("terrainOverlayTool"));
    if (tool == nullptr)
        return false;
    if (currentViewMode == ViewMode::Map) {
        // Before the first frame the view has no size yet (paintMap sets it).
        const int width = qRound(float(this->width()) * Game::PixelRatio);
        const int height = qRound(float(this->height()) * Game::PixelRatio);
        cameraMap->setViewport(width, height);
        mousex = float(width) / 2.0f;
        mousey = float(height) / 2.0f;
        updateMapPointer();
    }
    tool->run(*this, action);
    terrainChanged();
    return true;
}

bool RouteEditorGLWidget::runDiagnosticPathAction(const QString &action, float dx, float dz) {
    EditorTool *tool = tools->find(QStringLiteral("trackPathTool"));
    if (tool == nullptr || currentViewMode != ViewMode::Map || route == NULL)
        return false;
    for (Path *path : route->path)
        if (path != NULL && path->isSelected())
            setSelectedObj(path);
    const int width = qRound(float(this->width()) * Game::PixelRatio);
    const int height = qRound(float(this->height()) * Game::PixelRatio);
    cameraMap->setViewport(width, height);
    mousex = float(width) / 2.0f;
    mousey = float(height) / 2.0f;
    updateMapPointer();
    aktPointerPos[0] += dx;
    aktPointerPos[2] += dz;
    QMenu menu;
    tool->contextMenu(*this, menu);
    // The menu in order; the wait entries ask for a time and do not suit captures.
    static const QStringList order = {"start", "end", "other", "reverse", "removeReverse",
                                      "addWait", "editWait", "removeWait", "repair"};
    QList<QAction *> entries;
    for (QAction *entry : menu.actions())
        if (!entry->isSeparator())
            entries << entry;
    for (QAction *entry : entries)
        qInfo().noquote() << "path tool menu:" << entry->text()
                          << (entry->isEnabled() ? "enabled" : "disabled");
    const int index = order.indexOf(action);
    if (index < 0 || index >= entries.size() || !entries[index]->isEnabled())
        return false;
    entries[index]->trigger();
    if (Path *path = dynamic_cast<Path *>(selectedObj))
        qInfo().noquote() << "path tool: the path has" << path->nodes().size() << "nodes,"
                          << path->legs().size() << "legs";
    if (activityMap)
        activityMap->invalidate();
    update();
    return true;
}

void RouteEditorGLWidget::terrainChanged() {
    if (terrainMap)
        terrainMap->invalidate();
    update();
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

bool RouteEditorGLWidget::editsActivity(const QString &tool) {
    static const QStringList tools = {"trackPathTool", "actNewLooseConsistTool", "actNewSpeedZoneTool",
                                      "pickNewEventLocationTool", "pickSoundLocationTool"};
    return tools.contains(EditorTool::idOf(tool));
}

// The camera follows the train the Timetable window follows: in 3D it rides
// the train's consist as Play mode rides one (at its cab's window at first,
// keys 1 to 3 there, ahead of its front, behind its back), the user's own
// moves kept; on the map it is centred on the train and moved as it moves.
void RouteEditorGLWidget::followTrain() {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const int train = simulation->followed();
    if (train != followedTrain) {
        stopRiding();
        followedTrain = train;
        followedView = RideView::Cab;
        followedKnown = false;
    }
    int tile[2];
    float place[3], heading[3];
    if (train < 0 || !simulation->trainPlace(train, tile, place, heading))
        return;
    if (currentViewMode == ViewMode::Map && !followedKnown) {
        // Where it is first known (it may come on the track later): centred
        // on it, then moved as it moves.
        viewTrain(train);
    } else if (currentViewMode == ViewMode::Map) {
        const float *at = camera->getPos();
        camera->setPos(at[0] + place[0] - followedPlace[0] + 2048.0f * (tile[0] - followedTile[0]),
                       at[1] + place[1] - followedPlace[1],
                       at[2] + place[2] - followedPlace[2] + 2048.0f * (tile[1] - followedTile[1]));
        camera->check_coords();
    } else if (cameraFree->getCameraObject() != simulation->consistOf(train)) {
        // Not riding it yet: it has just come on the track, or the view
        // has come back from the map.
        rideTrain(train, followedView);
    }
    // Where it is first known: its consist selected, its properties shown.
    if (!followedKnown) {
        Consist *consist = simulation->consistOf(train);
        if (consist != NULL && selectedObj != consist) {
            if (selectedObj != NULL)
                selectedObj->unselect();
            setSelectedObj(consist);
        }
    }
    followedTile[0] = tile[0];
    followedTile[1] = tile[1];
    std::copy(place, place + 3, followedPlace);
    followedKnown = true;
}

void RouteEditorGLWidget::stopRiding() {
    GameObj *ridden = cameraFree != NULL ? cameraFree->getCameraObject() : NULL;
    if (ridden != NULL && Sim::SimulationController::instance()->trainOfObject(ridden) >= 0)
        cameraFree->releaseObject();
}

bool RouteEditorGLWidget::viewTrain(int train) {
    int tile[2];
    float place[3], heading[3], length = 0.0f;
    if (!Sim::SimulationController::instance()->trainPlace(train, tile, place, heading, &length))
        return false;
    stopRiding();
    camera->setPozT(tile[0], tile[1]);
    if (currentViewMode == ViewMode::Map) {
        camera->setPos(place[0], 0.0f, place[2]);
    } else {
        // Behind its rear, the whole train ahead.
        const float back = 0.5f * length + 35.0f, up = 15.0f + 0.05f * length;
        const float eye[3] = {place[0] - back * heading[0], place[1] + up, place[2] - back * heading[2]};
        camera->setPos(eye[0], eye[1], eye[2]);
        const float look[3] = {place[0] - eye[0], place[1] - eye[1], place[2] - eye[2]};
        const float distance = std::sqrt(look[0] * look[0] + look[1] * look[1] + look[2] * look[2]);
        camera->setPlayerRot(std::atan2(look[0], look[2]), std::asin(look[1] / std::max(distance, 0.001f)));
    }
    camera->check_coords();
    return true;
}

bool RouteEditorGLWidget::rideTrainVehicle(int train, int vehicle, const float *eye, const float *at) {
    Consist *consist = Sim::SimulationController::instance()->consistOf(train);
    if (consist == NULL || !consist->isOnTrack || vehicle < 0 || vehicle >= consist->engItems.size())
        return false;
    consist->cameraVehicle = vehicle;
    cameraFree->setCameraObject(consist);
    const float look[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    const float distance = std::sqrt(look[0] * look[0] + look[1] * look[1] + look[2] * look[2]);
    cameraFree->setPos(eye[0], eye[1], eye[2]);
    cameraFree->setPlayerRot(std::atan2(look[0], look[2]), std::asin(look[1] / std::max(distance, 0.001f)));
    return true;
}

bool RouteEditorGLWidget::rideTrain(int train, RideView view) {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    Consist *consist = simulation->consistOf(train);
    const std::shared_ptr<const Sim::SimulationInput> input = simulation->input();
    const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->viewFrame();
    if (consist == NULL || !consist->isOnTrack || consist->engItems.isEmpty() || input == nullptr
            || train < 0 || size_t(train) >= input->trains.size() || size_t(train) >= frame->trains.size())
        return false;
    const Sim::TrainPlan &plan = input->trains[size_t(train)];
    const bool reversed = frame->trains[size_t(train)].state.reversed;
    // It rides the leading vehicle (cab, front) or the trailing one (back)
    // as the train runs, the first or the last as the consist lists them; at
    // that vehicle's front (as listed), which faces the way the train runs
    // unless it runs reversed.
    const bool front = view != RideView::Back;
    const int last = consist->engItems.size() - 1;
    const int vehicle = front != reversed ? 0 : last;
    const float length = size_t(vehicle) < plan.vehicles.size() ? plan.vehicles[size_t(vehicle)].length : 0.0f;
    consist->cameraVehicle = vehicle;
    // In the 3D cab at its leading end, where it has one: the shape's front
    // leads unless it is flipped or the train runs reversed.
    const Eng *eng = consist->engItems[vehicle].engPointer;
    const bool cab3D = view == RideView::Cab && eng != NULL
            && consist->rideCab3D(vehicle, (eng->flip != 0) != reversed);
    if (!cab3D)
        consist->leaveCab3D();
    // The consist camera's own place: at the head in the cab, else at the
    // first vehicle's cab window, looking ahead.
    cameraFree->setCameraObject(consist);
    if (cab3D || (view == RideView::Cab && !reversed))
        return true;
    // In the vehicle's own frame (CameraFree's, riding it): its front at the
    // origin, along it towards its front -z; `ahead` the way the train runs.
    const float ahead = reversed ? 1.0f : -1.0f;
    float eye[3], at[3];
    if (view == RideView::Cab) {
        // Reversed: at the cab window of its leading end (the last
        // vehicle's rear as listed), looking the way it runs.
        const float lead = reversed ? length : 0.0f;
        eye[0] = 0.0f;
        eye[1] = 3.0f;
        eye[2] = ahead * lead;
        at[0] = 0.0f;
        at[1] = 3.0f;
        at[2] = ahead * (lead + 100.0f);
    } else if (front) {
        // Ahead of its leading end and to its side, looking back at it.
        const float lead = reversed ? length : 0.0f;
        eye[0] = 8.0f;
        eye[1] = 4.0f;
        eye[2] = ahead * (lead + 18.0f);
        at[0] = 0.0f;
        at[1] = 2.0f;
        at[2] = ahead * (lead - 8.0f);
    } else {
        // Behind its trailing end and above, the whole train ahead.
        const float trail = reversed ? 0.0f : length;
        eye[0] = 0.0f;
        eye[1] = 15.0f + 0.05f * plan.length;
        eye[2] = -ahead * (trail + 35.0f);
        at[0] = 0.0f;
        at[1] = 0.0f;
        at[2] = -ahead * trail + ahead * 0.5f * plan.length;
    }
    const float look[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    const float distance = std::sqrt(look[0] * look[0] + look[1] * look[1] + look[2] * look[2]);
    cameraFree->setPos(eye[0], eye[1], eye[2]);
    cameraFree->setPlayerRot(std::atan2(look[0], look[2]), std::asin(look[1] / std::max(distance, 0.001f)));
    return true;
}

void RouteEditorGLWidget::enableTool(QString name) {
    // A tool the view mode cannot use stays off (map mode, task editor 04).
    if (!tools->allowed(name, currentViewMode))
        return;
    // Nor one changing the activity while a simulation exists.
    if (ActivityNotifier::editingLocked() && editsActivity(name))
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
    // A panel button may choose the tool's left-click action ("tool:action").
    if (next != nullptr && !EditorTool::actionOf(name).isEmpty())
        next->setDefaultAction(EditorTool::actionOf(name));
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
    // Off a train it rides, or the place would be relative to the train.
    releaseCameraObject();
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
        // An MSTS activity, else an Open Rails timetable set by its file
        // (task simulation 08).
        Activity *set = NULL;
        for (int setId : route->timetableSetId) {
            Activity *candidate = ActLib::TimetableSets.value(setId);
            if (candidate != NULL && candidate->name.compare(activity, Qt::CaseInsensitive) == 0)
                set = candidate;
        }
        if (set != NULL)
            route->activitySelected(set);
        else if (id >= 0 && ActLib::Act[id] != NULL)
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

void RouteEditorGLWidget::setDiagnosticAITrains(const QString &trains) {
    Activity *activity = route != NULL ? route->getCurrentActivity() : NULL;
    if (activity == NULL || trains.isEmpty())
        return;
    for (const QString &part : trains.split(',', Qt::SkipEmptyParts)) {
        bool number = false;
        const int index = part.trimmed().toInt(&number);
        if (number)
            activity->shownAITrain = index;
        else if (part.trimmed() == "all")
            activity->showAllAITrains = true;
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
    terrainMap->invalidate();
    activityMap->invalidate();
    mapLabelsInvalid = true;
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

QVector3D RouteEditorGLWidget::probePointer(const QPoint &devicePoint) {
    mousex = devicePoint.x() / Game::PixelRatio;
    mousey = devicePoint.y() / Game::PixelRatio;
    pointerReadTime = 0;
    pointerReadExact = true;
    grabFramebuffer();
    pointerReadExact = false;
    return QVector3D(aktPointerPos[0], aktPointerPos[1], aktPointerPos[2]);
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
    // The map shows what is selected.
    if (trackItemMap)
        trackItemMap->rebuild();
    if (activityMap)
        activityMap->invalidate();
    mapLabelsInvalid = true;
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
    if (!Undo::UndoLast() && ActivityNotifier::editingLocked() && Undo::LastChangesActivity())
        QMessageBox::information(this,
            //% "Undo"
            qtTrId("route.editor.glwidget.undo.title"),
            //% "The last change is to the activity: it can be undone when the simulation stops."
            qtTrId("route.editor.glwidget.undo.activity.locked"));
    // Undone path edits show on the map's next draw.
    if (activityMap)
        activityMap->invalidate();
    update();
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
        EditorTool *tool = activeTool();
        // A tool with actions heads them with its own sections.
        if (tool == nullptr || tool->actions(*this).empty()) {
            QString toolName = tool != nullptr && !tool->title().isEmpty() ? tool->title()
                                                                           : EditorTool::idOf(toolEnabled);
            toolName[0] = toolName[0].toUpper();
            menu.addSection(toolName);
        }
        if (tool != nullptr)
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
