/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef GLWIDGET_H
#define GLWIDGET_H

#include <QWidget>
#include <tsre/renderer/RenderSurface.h>
#include <vector>
#include <QOpenGLFunctions>
//#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QMatrix4x4>
#include <QBasicTimer>
#include <tsre/camera/CameraFree.h>
#include <tsre/camera/CameraConsist.h>
#include <tsre/world/objects/WorldObj.h>
#include <tsre/world/objects/GroupObj.h>
#include <tsre/ogl/Pointer3d.h>
#include <tsre/world/Ref.h>
#include <unordered_map>

class Tile;
class Eng;
class GLUU;
class Route;
class Brush;
class PreciseTileCoordinate;
class Coords;
class MapWindow;
class ImageryWindow;
class ShapeLib;
class EngLib;
class QOpenGLFunctions_3_3_Core;
class QAction;
class GuiGlCompass;
class DynTrackObj;
class RulerObj;
class TelepoleObj;
class Renderer;
class EnvironmentMap;
class PlanarReflection;
class RenderQueue;

QT_FORWARD_DECLARE_CLASS(QOpenGLShaderProgram)

class RouteEditorGLWidget : public QWidget, public RenderSurfaceClient,
        protected QOpenGLFunctions
{
    Q_OBJECT

public:
    RouteEditorGLWidget(QWidget *parent = 0);
    ~RouteEditorGLWidget();

    QSize minimumSizeHint() const Q_DECL_OVERRIDE;
    QSize sizeHint() const Q_DECL_OVERRIDE;
    
    bool initRoute();
    void cameraInit();
    void playInit();
    
    void getUnsavedInfo(QVector<QString> &items);

    // Renderer parity harness hooks; not used by the editor UI.
    void setDiagnosticView(int tileX, int tileZ, float x, float y, float z,
                           float rotX, float rotY);
    void diagnosticView(int &tileX, int &tileZ, float *pos,
                        float &rotX, float &rotY) const;
    // Renders one selection pass and reads the IDs at device-pixel points
    // without applying a selection.
    QVector<quint32> probeSelectionIds(const QVector<QPoint> &devicePoints);
    // Stops simulation updates (traffic, animation) so separate processes
    // render the same scene. Content loading continues.
    void setSimulationPaused(bool paused);
    Route *currentRoute() const { return route; }

    // The render surface does the drawing; these forward to it so the view
    // code reads as before.
    void update();
    void makeCurrent();
    void doneCurrent();
    QImage grabFramebuffer();
    bool isValid() const { return surface->isValid(); }
    QString graphicsInfo() { return surface->graphicsInfo(); }
    unsigned int defaultFramebufferObject() const;

public slots:
    void cleanup();
    void enableTool(QString name);
    void setPaintBrush(Brush* brush);
    void jumpTo(PreciseTileCoordinate*);
    void jumpTo(float *posT, float *pos);
    void jumpTo(int X, int Z, float x, float y, float z);
    
    void msg(QString text);
    void msg(QString name, bool val);
    void msg(QString name, int val);
    void msg(QString name, float val);
    void msg(QString name, QString val);
    
    void editCopy();
    void editPaste();
    void editSelect();
    void editFind1x1();
    void editFind3x3();
    void editFind(int radius = 0);
    void editUndo();
    void showTrkEditr();
    void showContextMenu(const QPoint & point);
    void createNewTiles(QMap<int, QPair<int, int>*> list);
    void createNewLoTiles(QMap<int, QPair<int, int>*> list);
    void objectSelected(GameObj* obj);
    void objectSelected(QVector<GameObj*> obj);
    
    void selectToolresetMoveStep();
    void selectToolresetRot();
    void selectToolSelect();
    void selectToolRotate();
    void selectToolTranslate();
    void selectToolScale();
    void toolBrushDirectionUp();
    void toolBrushDirectionDown();
    void putTerrainTexToolSelectRandom();
    void putTerrainTexToolSelectPresent();
    void putTerrainTexToolSelect0();
    void putTerrainTexToolSelect90();
    void putTerrainTexToolSelect180();
    void putTerrainTexToolSelect270();
    void placeToolStickTerrain();
    void placeToolStickAll();
    void reloadRefFile();
    void reloadTrackProfiles();
    void refreshMarkerList();
    void setCameraObject(GameObj* obj);
    void setMoveStep(float val);
    void paintToolObj();
    void paintToolObjSelected();
    void paintToolTDB();
    void paintToolTDBVector();
    void setTerrainToObj();
    void adjustObjPositionToTerrainMenu();
    void adjustObjRotationToTerrainMenu();
    void pickObjForPlacement();
    void pickObjRotForPlacement();
    void pickObjRotElevForPlacement();
    
    void initRoute2();
    
signals:
    void showWindow();
    void routeLoaded(Route * a);
    void itemSelected(Ref::RefItem* pointer);
    void naviInfo(int all, int hidden);
    void posInfo(PreciseTileCoordinate* pos);
    void pointerInfo(float* pos);
    void setToolbox(QString name);
    void setBrushTextureId(int val);
    void terrainMaterialPicked();
    void showProperties(GameObj* obj);
    void updateProperties(GameObj* obj);
    void flexData(int x, int z, float* p);
    void mkrList(QMap<QString, Coords*> list);
    void refreshObjLists();
    
    void sendMsg(QString name);
    void sendMsg(QString name, bool val);
    void sendMsg(QString name, int val);
    void sendMsg(QString name, float val);
    void sendMsg(QString name, QString val);

protected:
    bool eventFilter(QObject *object, QEvent *event);
    void surfaceInitialize() override;
    void surfacePaint() override;
    void surfaceRelease() override;
    void renderShadowMaps();
    // Renders the scheduled environment map faces from the camera position.
    void renderEnvironmentMap();
    // Draws the mirrored scene for shaded water; false when none is needed.
    bool renderWaterReflection();
    // Draws the water pass, counting its samples when the reflection needs
    // to know whether water was on screen (Renderer::measuredSamples).
    void renderWaterPass(bool measure);
    void computeShadowMatrices();
    void handleSelection();
    void applySelection(quint32 selectionId, int cameraTileX, int cameraTileZ);
    void surfaceResize(int width, int height) override;
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseReleaseEvent(QMouseEvent* event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void wheelEvent(QWheelEvent *event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent * event) Q_DECL_OVERRIDE;
    void keyReleaseEvent(QKeyEvent * event) Q_DECL_OVERRIDE;
    void timerEvent(QTimerEvent *event) Q_DECL_OVERRIDE;
    void pushRenderPointer(RenderQueue &queue);
    void updatePointerPosition();
    void readPointerPosition();
    void applyPointerToLiveTools();
    float pointerDisplayY() const;
private:
    bool startLiveFlex(bool reuseUndoState = false, bool deleteOnCancel = false,
            bool initialDirectionFromMouse = false);
    bool updateLiveFlex(int pointerTileX, int pointerTileZ,
            const float *pointerPosition, bool force = false);
    void finishLiveFlex(bool accept, bool keepContinuousTool = false);
    bool placeContinuousFlexTrack(int tileX, int tileZ, float *position,
            float *quaternion, bool initialMousePlacement = false);
    DynTrackObj* placeRawDynTrack(int tileX, int tileZ, float *position, float *quaternion);
    bool createLiveFlexCompanions();
    QString continuousFlexProfileForRole(const QString &role = QString()) const;
    void applyContinuousFlexProfiles();
    void discardLiveFlexCompanions();
    bool updateLiveFlexCompanions(const float *mainSections);
    float effectiveContinuousFlexMinimumRadius() const;
    bool placeContinuousRuler(int tileX, int tileZ,
            const float *position, const float *quaternion);
    bool updateLiveRuler(int pointerTileX, int pointerTileZ,
            const float *pointerPosition, bool force = false);
    bool acceptLiveRulerPoint();
    void finishLiveRuler(bool keepContinuousTool = false);
    bool beginLiveTelepole(TelepoleObj *telepole);
    bool updateLiveTelepole(int pointerTileX, int pointerTileZ,
            const float *pointerPosition, bool force = false);
    void finishLiveTelepole(bool accept);
    static void quantizeContinuousPoint(int &tileX, int &tileZ,
            float *position, float step);
    void paintScene();
    bool canRenderFrame() const;
    void drawEditorFpsHud();
    void setupVertexAttribs();
    void setSelectedObj(GameObj* o);
    QBasicTimer timer;
    unsigned long long int lastTime;
    unsigned long long int timeNow;
    bool m_core;
    int m_xRot;
    int m_yRot;
    int m_zRot;
    int fps;
    int fpsDisplay = 0;
    double fpsDisplayAccumMs = 0.0;
    int fpsDisplayAccumFrames = 0;
    unsigned long long int fpsDisplayLastUpdate = 0;
    QPointF m_lastPos;
    Eng* eng;
    Tile* tile;
    Route* route = NULL;
    GLUU* gluu;
    QOpenGLFunctions_3_3_Core* funcs = 0;
    unsigned int fbo[3];
    // Height of the renderer's selection target while a selection pass
    // draws, 0 otherwise.
    int selectionTargetHeight = 0;
    bool m_transparent;
    Camera* camera = NULL;
    CameraFree* cameraFree = NULL;
    CameraConsist* cameraObj = NULL;
    bool selection = false;
    float mousex, mousey;
    QVector<QPoint> selectionProbePoints;
    bool simulationPaused = false;
    QVector<quint32> selectionProbeResults;
    GameObj* selectedObj = NULL;
    GameObj* lastSelectedObj = NULL;
    WorldObj* copyPasteObj = NULL;
    GroupObj* groupObj = NULL;
    GroupObj* copyPasteGroupObj = NULL;
    Pointer3d* pointer3d;
    float lastPointerPos[3];
    float aktPointerPos[3];
    bool mouseLPressed = false;
    bool mouseRPressed = false;
    bool mouseClick = false;
    QString toolEnabled = "";
    float defaultMoveStep = 0.25;
    float moveStep = 0.25;
    //float moveUltraStep = 2.0;
    float moveMaxStep = 0.25;
    //float moveMinStep = 0.01;
    bool resizeTool = false;
    bool rotateTool = false;
    bool translateTool = false;
    bool liveFlexActive = false;
    bool continuousFlexMode = false;
    bool continuousFlexRoadMode = false;
    bool liveFlexDeleteOnCancel = false;
    bool liveFlexInitialDirectionFromMouse = false;
    bool liveFlexSolutionValid = false;
    DynTrackObj *liveFlexObj = NULL;
    DynTrackObj *lastAcceptedFlexObj = NULL;
    int liveFlexStartTileX = 0;
    int liveFlexStartTileZ = 0;
    float liveFlexStartPosition[3] = {0, 0, 0};
    float liveFlexStartQ[4] = {0, 0, 0, 1};
    float liveFlexOriginalSections[10] = {0};
    bool liveFlexHasLastTarget = false;
    int liveFlexLastTargetTileX = 0;
    int liveFlexLastTargetTileZ = 0;
    int liveFlexLastEndpointId = -2;
    float liveFlexLastTargetPosition[3] = {0, 0, 0};
    unsigned long long liveFlexLastUpdateTime = 0;
    QVector<DynTrackObj*> liveFlexCompanions;
    QVector<float> liveFlexCompanionOffsets;
    bool liveFlexCompanionsValid = true;
    bool continuousFlexLeftEnabled = false;
    bool continuousFlexRightEnabled = false;
    float continuousFlexSeparation = 4.0f;
    float continuousFlexMinimumRadius = 15.0f;
    QString continuousFlexProfile;
    bool continuousRulerMode = false;
    bool liveRulerActive = false;
    bool liveRulerHasCommittedSegment = false;
    bool liveRulerSolutionValid = false;
    RulerObj *liveRulerObj = NULL;
    bool liveRulerHasLastTarget = false;
    int liveRulerLastTargetTileX = 0;
    int liveRulerLastTargetTileZ = 0;
    float liveRulerLastTargetPosition[3] = {0, 0, 0};
    unsigned long long liveRulerLastUpdateTime = 0;
    QString liveRulerDraftTemplate;
    QString liveRulerDraftNodeShape;
    QString continuousRulerProfile;
    QString continuousRulerNodeShape;
    bool liveTelepoleActive = false;
    bool liveTelepoleSolutionValid = false;
    TelepoleObj *liveTelepoleObj = NULL;
    bool liveTelepoleHasLastTarget = false;
    int liveTelepoleLastTargetTileX = 0;
    int liveTelepoleLastTargetTileZ = 0;
    float liveTelepoleLastTargetPosition[3] = {0, 0, 0};
    unsigned long long liveTelepoleLastUpdateTime = 0;
    float continuousPlacementYOffset = 0.0f;
    bool stickPointerToTerrain = true;
    bool autoAddToTDB = true;
    float lastNewObjPos[3];
    float lastNewObjPosT[2];
    float placeRot[4];
    float placeElev = 0;
    long long int lastMousePressTime = 0;
    bool keyControlEnabled = false;
    bool keyShiftEnabled = false;
    bool keyAltEnabled = false;
    // Near, mid and far shadow maps.
    int shadowMapSize = 2048;
    int distantShadowMapSize = 1024;
    Brush* defaultPaintBrush;
    MapWindow* mapWindow;
    ImageryWindow* imageryWindow;
    ShapeLib *currentShapeLib = NULL;
    EngLib *engLib = NULL;
    
    /*struct DefaultMenuActions {
        QAction *undo;
        QAction *copy;
        QAction *paste;
        QAction *find1x1;
        QAction *find3x3;
        QAction *select;
        void init(RouteEditorGLWidget *widget);
    };*/
    //DefaultMenuActions defaultMenuActions;
    QMap<QString, QAction*> defaultMenuActions;
    bool bolckContextMenu = false;
    
    // Owned; draws this widget's frames.
    Renderer *renderer = NULL;
    EnvironmentMap *environmentMap = NULL;
    RenderSurface *surface = NULL;
    PlanarReflection *waterReflection = NULL;
    // Mirror plane of the last reflection (n . p + d = 0).
    float waterReflectionPlane[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    std::vector<float> waterBounds;
    GuiGlCompass * compass = NULL;
    OglObj * compassPointer = NULL;
    
    
    
};

#endif
