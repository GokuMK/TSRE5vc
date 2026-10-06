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
#include <tsre/trains/EngLib.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/texture/TexLib.h>
#include <shapeViewer/ShapeTextureInfo.h>
#include <tsre/renderer/OpenGL3Renderer.h>

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

    camera->update(fps);
    update();
    
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
    if(renderItem == 4 && complexShape != NULL){
        complexShape->fillShapeHierarchyInfo(info);
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
        if(renderItem == 3 && con != nullptr)
            renderFrame(true);
    }
    // Always finish a pick with a visible frame in the widget framebuffer.
    renderFrame(false);
}

void ShapeViewerGLWidget::renderFrame(bool selectionPass) {
    Game::currentShapeLib = currentShapeLib;
    if(renderer == nullptr || !renderer->programsReady())
        return;
    const int selectionHeight = qRound(height() * devicePixelRatioF());
    if(selectionPass){
        const qreal pixelRatio = devicePixelRatioF();
        if(!renderer->beginSelection(qRound(width() * pixelRatio), selectionHeight))
            return;
    } else {
        renderer->clear(true, true, backgroundGlColor);
    }
    renderer->useProgram(selectionPass ? Renderer::PROGRAM_SELECTION : Renderer::PROGRAM_MAIN);
    // Zero is the background; wagon indices start at one.
    const quint32 selectionId = selectionPass ? 1 : 0;

    // disable shadows temporaily
    int shadowsState = Game::shadowsEnabled;
    Game::shadowsEnabled = 0;
    
    renderer->clear(false, false, backgroundGlColor);
    
    float aspect = float(this->width()) / float(this->height());
    float* lookAt = camera->getMatrix();
    const float zNear = renderItem == 4 ? nearPlane : 0.2f;
    Mat4::perspective(gluu->pMatrix, camera->fov*M_PI/180*(1/aspect), aspect, zNear, Game::objectLod);
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
    if(renderItem == 4 && complexShape != NULL){
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
        if(id > 0 && id <= static_cast<quint32>(con->engItems.size())){
            const int index = static_cast<int>(id - 1);
            con->select(index);
            emit selected(index);
        }
    }
    
    if(getImage && !selectionPass){
        qDebug() << "get image";
        if(screenShot != NULL)
            delete screenShot;
        qDebug() << "new image";
        screenShot = NULL;
        getImage = false;
        int* viewport = new int[4];
        renderer->viewport(viewport);
        unsigned char* winZ = new unsigned char[viewport[2]*viewport[3]*4];
        renderer->readColor(0, 0, viewport[2], viewport[3], winZ);
        screenShot = new QImage(winZ, viewport[2], viewport[3], QImage::Format_RGBA8888 );
        
    }
    
    renderer->releaseProgram();
    Game::shadowsEnabled = shadowsState;
}

void ShapeViewerGLWidget::renderGathered(quint32 selectionId) {
    renderer->resetFrame();
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
    if(renderItem == 4 && complexShape != NULL)
        complexShape->pushRenderItem(queue);
    renderer->renderFrame();
}

void ShapeViewerGLWidget::getImg() {
    getImage = true;
    return;
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
    
    if ((event->button()) == Qt::RightButton) {
        showContextMenu(event->pos());
    }
}

void ShapeViewerGLWidget::mouseMoveEvent(QMouseEvent *event) {
    mousex = event->position().x()*Game::PixelRatio;
    mousey = event->position().y()*Game::PixelRatio;
    
    if(mode == "rot"){
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

void ShapeViewerGLWidget::setModelRotation(float yaw){
    rotY = M_PI + yaw;
    rotZ = 0;
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
    if(texPath.length() > 0)
        shapeId = currentShapeLib->addShape(path, texPath);
    else
        shapeId = currentShapeLib->addShape(path);
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
