/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef GLSHAPEWIDGET_H
#define	GLSHAPEWIDGET_H

#include <QWidget>
#include <tsre/renderer/RenderSurface.h>
#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QMatrix4x4>
#include <QBasicTimer>
#include <math.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/trains/CarAnimation.h>

class ComplexShape;
class Eng;
class Consist;
class GLUU;
class Camera;
class EngLib;
class Renderer;
class EnvironmentMap;
class QImage;
class ShapeTextureInfo;
class ShapeHierarchyInfo;
class ContentHierarchyInfo;

QT_FORWARD_DECLARE_CLASS(QOpenGLShaderProgram)

class ShapeViewerGLWidget : public QWidget, public RenderSurfaceClient,
        protected QOpenGLFunctions {
    Q_OBJECT
public:
    ShapeViewerGLWidget(QWidget *parent = 0,
                        ShapeLib::MstsBackend backend = ShapeLib::MstsBackend::ConfiguredDefault);
    virtual ~ShapeViewerGLWidget();
    
    QSize minimumSizeHint() const Q_DECL_OVERRIDE;
    QSize sizeHint() const Q_DECL_OVERRIDE;
    int heightForWidth(int w) const {
        return w/10;
    }
    void setCamera(Camera* cam);
    ShapeLib *currentShapeLib = NULL;
    //EngLib *currentEngLib = NULL;
    QImage *screenShot = NULL;
    void setMode(QString n);
    void resetRot();
    // Turns the shown model by yaw radians from the default view, and tilts
    // it by pitch radians (as dragging with the right button).
    void setModelRotation(float yaw, float pitch = 0.0f);
    // The light the view draws with, whatever the route's sun (task
    // renderer 23): the direction towards the sun in the view's frame (the
    // camera looks along +x, y up) and the sun's and the ambient light's
    // greys. By default a studio light: the sun above, behind the camera and
    // to one side, the ambient light strong enough for the ambient
    // occlusion to show.
    struct Light {
        float sun[3] = {-0.597f, 0.6965f, 0.398f};
        float sunColor = 0.6f;
        float ambient = 0.5f;
    };
    void setLight(const Light &light);
    void getImg();
    void setBackgroundGlColor(float r, float g, float b);
    void fillCurrentShapeHierarchyInfo(ShapeHierarchyInfo *info);
    void fillCurrentShapeTextureInfo(QHash<int, ShapeTextureInfo*> &list);
    void fillCurrentContentHierarchyInfo(QVector<ContentHierarchyInfo*> &list);

    // The render surface does the drawing; these forward to it.
    void update();
    void makeCurrent();
    void doneCurrent();
    QImage grabFramebuffer();
    bool isValid() const { return surface->isValid(); }
    QString graphicsInfo() { return surface->graphicsInfo(); }
    // Picking the shown shape's primitives (task shapes 13, the materials
    // editor): a click (not a drag) picks the primitive under it
    // (primitivePicked; Ctrl adds), and these are drawn highlighted.
    void setPrimitivePicking(bool on);
    bool primitivePicking() const { return pickPrimitives; }
    void setPickedPrimitives(const QVector<int> &primitives);
    const QVector<int> &pickedPrimitives() const { return picked; }
    // The shape shown on its own (Shape Viewer files), else null.
    ComplexShape *shownShape() const { return renderItem == 4 ? complexShape : nullptr; }
public slots:
    void showEng(QString path, QString name);
    void showEng(Eng *e);
    void showEngSet(int id);
    void showCon(int id);
    void showConSimple(Consist *currentCon);
    void showConSimple(int id);
    void showCon(int aid, int id);
    // Texture directory of a shape opened on its own: a route SHAPES
    // directory uses the route's TEXTURES next to it, anything else the
    // shape's own directory. The same rule applies to .s and glTF files.
    static QString textureDirectory(const QString &shapePath);
    void showShape(QString path, QString texPath, ComplexShape **currentShape = NULL);
    void showShape(ComplexShape *currentShape = NULL);
    void cleanup();
    // Animation (task shapes 12): playing (a car's wheels and running gear
    // turning as if it ran at 15 km/h, a shape's own looping animation), a
    // shape's keyed parts at their last frame (else at rest), and a car's
    // parts open or up (CarAnimation's groups).
    void setAnimationPlaying(bool on);
    void setAnimationEnd(bool on);
    // `now`: at once (captures), else moving as Open Rails moves them.
    void setCarAnimation(int group, bool on, bool now = false);
    // Metres a shown car has gone (its wheels and running gear; captures).
    void setAnimationTravelled(double metres) { animationTravelled = metres; }
    void flipConSelected();
    void leftConSelected();
    void rightConSelected();
    void deleteConSelected();
    void copyUnitConSelected();
    void pasteUnitConSelected();
    
signals:
    void selected(int id);
    void refreshItem();
    // A primitive picked in the view (-1: none, the background); `add`:
    // with Ctrl, to add to the picked ones or take it out.
    void primitivePicked(int primitive, bool add);
    
protected:
    void surfaceInitialize() override;
    void surfacePaint() override;
    void surfaceResize(int width, int height) override;
    void surfaceRelease() override;
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void wheelEvent(QWheelEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseReleaseEvent(QMouseEvent* event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void showContextMenu(const QPoint & point);
    void keyPressEvent(QKeyEvent * event) Q_DECL_OVERRIDE;
    void keyReleaseEvent(QKeyEvent * event) Q_DECL_OVERRIDE;
    void timerEvent(QTimerEvent *event) Q_DECL_OVERRIDE;

private:
    RenderSurface *surface = nullptr;
    void renderFrame(bool selectionPass);
    // Draws the current item through this widget's renderer.
    void renderGathered(quint32 selectionId);
    // Owned; draws this widget's frames.
    Renderer *renderer = nullptr;
    // Owned; a procedural warehouse interior for reflections.
    EnvironmentMap *environmentMap = nullptr;
    QPointF selectionPosition;
    void setupVertexAttribs();
    QBasicTimer timer;
    unsigned long long int lastTime;
    unsigned long long int timeNow;
    bool m_core;
    int m_xRot;
    int m_yRot;
    int m_zRot;
    int fps;
    QPointF m_lastPos;
    GLUU* gluu;
    float mousex, mousey;
    bool mousePressed = false;
    bool mouseRPressed = false;
    bool mouseLPressed = false;
    ComplexShape* complexShape = NULL;
    Eng* eng = NULL;
    Consist* con = NULL;
    Camera* camera = NULL;
    int renderItem = 0;
    QString mode = "";
    float rotY = M_PI;
    float rotZ = 0;
    Light light;
    bool selection = false;
    bool pickPrimitives = false;
    bool pickAdd = false;
    QVector<int> picked;
    // Where the left button went down (widget pixels), to tell a click
    // from a drag.
    QPointF pressPosition;
    bool getImage = false;
    float backgroundGlColor[3];
    bool cameraInit = false;
    // Near clip distance, scaled with the shown shape so small ones are not clipped.
    float nearPlane = 0.2f;
    // This frame's projection (without the camera) and depth planes, which
    // the renderer's screen-space effects need (setSceneProjection).
    float sceneProjection[16] = {};
    float sceneNear = 0.2f;
    float sceneFar = 1.0f;
    
    QMap<QString, QAction*> defaultMenuActions;
    bool animationPlaying = false;
    bool animationEnd = false;
    bool carGroups[CarAnimation::GroupCount] = {};
    bool carGroupsNow = false;
    // Metres a shown car has gone while playing.
    double animationTravelled = 0.0;
    // The settings above on what is shown, `seconds` after the last time.
    void animate(float seconds);
    void animateEng(Eng *shown);
};

#endif	/* GLSHAPEWIDGET_H */

