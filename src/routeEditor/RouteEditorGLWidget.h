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

#include <QVector3D>
#include <QWidget>
#include <tsre/renderer/RenderSurface.h>
#include <vector>
#include <QOpenGLFunctions>
//#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QMatrix4x4>
#include <QBasicTimer>
#include <QElapsedTimer>
#include <tsre/camera/CameraFree.h>
#include <tsre/camera/CameraConsist.h>
#include <tsre/world/objects/WorldObj.h>
#include <tsre/world/objects/GroupObj.h>
#include <tsre/ogl/Pointer3d.h>
#include <tsre/world/Ref.h>
#include <unordered_map>
#include <memory>
#include "tools/ToolContext.h"
#include <tsre/map/MapLayers.h>
#include <tsre/map/MapPalette.h>

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
class ToolRegistry;
class CameraMap;
class TrackMapLayer;
class TrackItemMapLayer;
class ActivityMapLayer;
class MapSelection;
class TerrainMapLayer;
class MapOverlayFade;
class OsmMapLayer;
class ImageryMapLayer;
class QLabel;
class MapLabelLayer;
class Coords;

QT_FORWARD_DECLARE_CLASS(QOpenGLShaderProgram)

class RouteEditorGLWidget : public QWidget, public RenderSurfaceClient,
        protected QOpenGLFunctions, private ToolContext
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

    // The camera as a core.startup.camera value.
    QString cameraSetting() const;
    // Renderer parity harness hooks (and the startup camera).
    void setDiagnosticView(int tileX, int tileZ, float x, float y, float z,
                           float rotX, float rotY);
    void diagnosticView(int &tileX, int &tileZ, float *pos,
                        float &rotX, float &rotY) const;
    // Map mode centred on a ground point, at a scale, with a compass
    // bearing (degrees, 0: north) at the top of the screen.
    void setDiagnosticMapView(int tileX, int tileZ, float x, float z,
                              float metresPerPixel, float bearingDegrees);
    // Selects an activity and a path by file name, as the activity tools
    // do; empty names leave them as they are.
    void setDiagnosticActivity(const QString &activity, const QString &path);
    // The editor's tools, for the tool panels to know which work in a view
    // mode.
    const ToolRegistry *toolRegistry() const { return tools.get(); }
    // Renders one selection pass and reads the IDs at device-pixel points
    // without applying a selection.
    QVector<quint32> probeSelectionIds(const QVector<QPoint> &devicePoints);
    // Renders a frame with the mouse at a device-pixel point and returns
    // the 3D pointer position read from the depth there.
    QVector3D probePointer(const QPoint &devicePoint);
    // Stops simulation updates (traffic, animation) so separate processes
    // render the same scene. Content loading continues.
    void setSimulationPaused(bool paused);
    Route *currentRoute() const override { return route; }
    // The view mode (task editor 04): the 3D scene, or the map from straight
    // above. Switching moves to the pointer's place in the other mode; tools
    // the new mode does not support are put aside until the mode returns.
    void setViewMode(ViewMode mode);
    // Shows or hides a layer of the map mode (the Map menu).
    void setMapLayerVisible(MapLayer layer, bool visible);
    // Before Map > OSM Data is switched on: checks the OSM directory and the route's
    // geographic reference (telling the user what is missing) and offers to convert the
    // downloads covering the view. False when the layer cannot be shown.
    bool prepareOsmLayer();
    // Before Map > Imagery is switched on: checks the route's geographic reference and
    // the imagery catalogue's world source. False when the layer cannot be shown.
    bool prepareImageryLayer();
    // A map layer is still building on a worker thread (captures wait for it).
    bool mapLayersBusy() const;

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
    void toggleViewMode();
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
    void reloadRefFile();
    void reloadTrackProfiles();
    void refreshMarkerList();
    void setCameraObject(GameObj* obj);
    void setMoveStep(float val);
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
    // Time of day (Game::timeOfDayEnabled): puts the sun where it stands over
    // the camera and lights the scene for it; off, restores the fixed light.
    void applyTimeOfDay();
    struct TimeOfDayState {
        bool saved = false;
        bool applied = false;
        // The fixed light, restored when time of day is switched off.
        float sunDirection[3];
        float sky[4], fog[4], diffuse[4], ambient[4];
        // Latitude and longitude of the camera's tile.
        int tileX = 0, tileZ = 0;
        bool located = false;
        double latitude = 50.0, longitude = 0.0;
        // True bearing (degrees clockwise from north) of the world's -z axis:
        // the projection's grid north turns away from true north.
        double gridNorth = 0.0;
    } timeOfDay;
    // Direction towards the shadow-casting sun, and whether it casts shadows.
    float shadowSunDirection[3] = {-1.0f, 1.5f, 1.0f};
    bool sunCastsShadows = true;
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
    // ToolContext: what the tools reach (tools/ToolContext.h).
    QWidget *view() override { return this; }
    ViewMode viewMode() const override { return currentViewMode; }
    int tileX() const override;
    int tileZ() const override;
    float *pointer() override { return aktPointerPos; }
    float cameraHeading() const override;
    Brush *brush() override { return defaultPaintBrush; }
    GameObj *selected() const override { return selectedObj; }
    void select(GameObj *object) override { setSelectedObj(object); }
    void setLastSelected(GameObj *object) override { lastSelectedObj = object; }
    void requestSelectionPass() override { selection = true; }
    bool pointerOnTrack(int &tileX, int &tileZ, float *position) override;
    bool prepareTerrainEdit(bool procedural) override;
    bool prepareTerrainTile() override;
    ObjectEdit objectEdit() const override;
    void setObjectEdit(ObjectEdit edit) override;
    bool pointerSticksToTerrain() const override { return stickPointerToTerrain; }
    void setPointerSticksToTerrain(bool terrainOnly) override { stickPointerToTerrain = terrainOnly; }
    bool shiftDown() const override { return keyShiftEnabled; }
    bool controlDown() const override { return keyControlEnabled; }
    float keyMoveStep() const override { return moveStep; }
    void resetKeyMoveStep() override { selectToolresetMoveStep(); }
    float *placementRotation() override { return placeRot; }
    float placementElevation() const override { return placeElev; }
    bool autoAddToTrackDb() const override { return autoAddToTDB; }
    void resetPlacementRotation() override { selectToolresetRot(); }
    void rememberPlacement() override;
    void terrainToSelected() override { setTerrainToObj(); }
    void selectedPositionToTerrain() override { adjustObjPositionToTerrainMenu(); }
    void selectedRotationToTerrain() override { adjustObjRotationToTerrainMenu(); }
    void pickPlacementFromSelected() override { pickObjForPlacement(); }
    void pickPlacementRotation() override { pickObjRotForPlacement(); }
    void pickPlacementRotationAndElevation() override { pickObjRotElevForPlacement(); }
    bool placeContinuousFlex(float *rotation) override;
    bool placeContinuousRulerPoint(const float *rotation) override;
    void startTelepole(TelepoleObj *telepole) override { beginLiveTelepole(telepole); }
    void activateTool(const QString &id) override { enableTool(id); }
    void message(const QString &name) override { emit sendMsg(name); }
    void message(const QString &name, const QString &value) override { emit sendMsg(name, value); }
    void sendFlexData() override;
    void reportTextureId(int textureId) override { emit setBrushTextureId(textureId); }
    void reportMaterialPicked() override { emit terrainMaterialPicked(); }
    void openMapTileWindow(Terrain *terrain) override;
    void openImageryWindow(Terrain *terrain) override;
    // The active tool's object; null for no tool or a name without one.
    EditorTool *activeTool() const;
    // Draws the map mode's frame.
    void paintMap();
    // The map's selection pass: the layers' selectable shapes with 3D's IDs,
    // read and applied as in 3D.
    void paintMapSelection(int width, int height);
    // The pointer in map mode: the ground under the mouse, on the map
    // plane (height 0; the map loads no terrain).
    void updateMapPointer();

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
    // GPU time of a recent frame where the renderer measures it, else < 0.
    float gpuMsDisplay = -1.0f;
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
    // The first view, and the view after a camera jump, are shown with their
    // shapes loaded (loadWholeView()); after a jump the wait ends after
    // limitMs and the rest loads while the view is shown. The camera's
    // position in the last frame, in metres, tells a jump.
    bool wholeViewPending = true;
    double lastViewPosition[2] = {0.0, 0.0};
    void loadWholeView(const char *reason, qint64 limitMs);
    // From a whole view's start to the next frame: the time it was shown.
    QElapsedTimer wholeViewShown;
    const char *wholeViewReason = nullptr;
    // Its phases, from its start: loaded, frame gathered, frame drawn.
    qint64 wholeViewMarks[3] = {0, 0, 0};
    QVector<quint32> selectionProbeResults;
    GameObj* selectedObj = NULL;
    GameObj* lastSelectedObj = NULL;
    WorldObj* copyPasteObj = NULL;
    GroupObj* groupObj = NULL;
    GroupObj* copyPasteGroupObj = NULL;
    Pointer3d* pointer3d;
    float lastPointerPos[3];
    float aktPointerPos[3];
    // When the pointer last read the depth (it reads at most every 50 ms).
    unsigned long long pointerReadTime = 0;
    // The next pointer read waits for this frame's depth (probePointer);
    // otherwise it takes the latest completed read.
    bool pointerReadExact = false;
    bool mouseLPressed = false;
    bool mouseRPressed = false;
    bool mouseClick = false;
    QString toolEnabled = "";
    std::unique_ptr<ToolRegistry> tools;
    ViewMode currentViewMode = ViewMode::Scene3D;
    CameraMap *cameraMap = NULL;
    std::unique_ptr<TrackMapLayer> trackMap;
    std::unique_ptr<TrackItemMapLayer> trackItemMap;
    std::unique_ptr<ActivityMapLayer> activityMap;
    std::unique_ptr<TerrainMapLayer> terrainMap;
    std::unique_ptr<MapOverlayFade> mapFade;
    std::unique_ptr<OsmMapLayer> osmMap;
    // Map > Imagery: the imagery catalogue's world tile source, with its attribution
    // in the bottom right corner while shown.
    std::unique_ptr<ImageryMapLayer> imageryMap;
    QLabel *imageryAttribution = NULL;
    void showImageryAttribution(bool show);
    // Map labels: the marker set (Map > Markers), stations, platforms and sidings (Map >
    // Track Objects), location events (Map > Activity), placed together. Rebuilt when a
    // source or its size changes, or when edits invalidate the map's items.
    std::unique_ptr<MapLabelLayer> mapLabels;
    std::vector<const void *> mapLabelSources;
    bool mapLabelsInvalid = true;
    void updateMapLabels();
    // Where a press began in map mode: a left click that moves the map no
    // more than this goes to the active tool.
    static constexpr float MapClickPixels = 4.0f;
    QPointF mapPressPos;
    // A left press on the map that picked the selected activity object:
    // the drag moves it instead of the map.
    bool mapDraggingObject = false;
    // A left press with a tool that edits by dragging (painting): the drag
    // goes to the tool, the right button moves the map.
    bool mapEditing = false;
    std::unique_ptr<MapSelection> mapSelection;
    // The ID of the last selection applied, to know a press is on it.
    quint32 appliedSelectionId = 0;
    OglObj *mapPointer = NULL;
    MapPalette mapPalette;
    MapLayers mapLayers;
    // The setting value mapPalette was read for.
    QString mapPaletteSetting;
    // The tool active when map mode began, for the return to 3D.
    QString toolBeforeMap;
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
