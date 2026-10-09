/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/fileFunctions/ContentPath.h>
#include <shapeViewer/ShapeViewerWindow.h>
#include <shapeViewer/ShapeTexturesWindow.h>
#include <shapeViewer/ShapeHierarchyWindow.h>
#include <shapeViewer/ShapeViewerNavigatorWidget.h>
#include <shapeViewer/EngInfoWidget.h>
#include <shapeViewer/ConInfoWidget.h>
#include <shapeViewer/ShapeInfoWidget.h>
#include <QDebug>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/Consist.h>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include <shapeViewer/ShapeViewerGLWidget.h>
#include <tsre/camera/CameraFree.h>
#include <tsre/camera/CameraConsist.h>
#include <tsre/camera/CameraRot.h>
#include <tsre/gui/GuiFunct.h>
#include <routeEditor/AboutWindow.h>
#include <tsre/math3d/GLMatrix.h>
#include <QVector>
#include <shapeViewer/ShapeTextureInfo.h>
#include <shapeViewer/ShapeHierarchyInfo.h>
#include <shapeViewer/ContentHierarchyInfo.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/SFileComplex.h>

namespace {
struct ShapeSaveSelection {
    QString path;
    int format = -1;      // -1 preserve, 0 Unicode, 1 binary
    int compression = -1; // -1 preserve, 0 uncompressed, 1 compressed
};

bool chooseShapeSave(QWidget *parent, const QString &sourcePath,
                     SFileComplex::Format sourceFormat, bool sourceCompressed,
                     ShapeSaveSelection &selection) {
    QDialog dialog(parent);
    //% "Save Shape As"
    dialog.setWindowTitle(qtTrId("shape.viewer.save.save.shape.as"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *pathLayout = new QHBoxLayout;
    auto *path = new QLineEdit(QDir::toNativeSeparators(sourcePath), &dialog);
    //% "Browse…"
    auto *browse = new QPushButton(qtTrId("shape.viewer.save.browse"), &dialog);
    pathLayout->addWidget(path, 1);
    pathLayout->addWidget(browse);
    //% "Output file:"
    form->addRow(qtTrId("shape.viewer.save.output.file.label"), pathLayout);

    auto *format = new QComboBox(&dialog);
    //% "Preserve (%1)"
    format->addItem(qtTrId("shape.viewer.save.preserve")
                        .arg(sourceFormat == SFileComplex::Format::Binary ?
                                 //% "Binary"
                                 qtTrId("shape.viewer.save.binary") :
                                 //% "Unicode"
                                 qtTrId("shape.viewer.save.unicode")), -1);
    //% "Unicode text"
    format->addItem(qtTrId("shape.viewer.save.unicode.text"), 0);
    //% "Binary"
    format->addItem(qtTrId("shape.viewer.save.binary"), 1);
    //% "Format:"
    form->addRow(qtTrId("shape.viewer.save.format.label"), format);

    auto *compression = new QComboBox(&dialog);
    //% "Preserve (%1)"
    compression->addItem(qtTrId("shape.viewer.save.preserve")
                             .arg(sourceCompressed ?
                                  //% "Compressed"
                                  qtTrId("shape.viewer.save.compressed") :
                                  //% "Uncompressed"
                                  qtTrId("shape.viewer.save.uncompressed")), -1);
    //% "Uncompressed"
    compression->addItem(qtTrId("shape.viewer.save.uncompressed"), 0);
    //% "Compressed"
    compression->addItem(qtTrId("shape.viewer.save.compressed"), 1);
    //% "Compression:"
    form->addRow(qtTrId("shape.viewer.save.compression.label"), compression);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&dialog, path] {
        //% "Save Shape As"
        QFileDialog picker(&dialog, qtTrId("shape.viewer.save.save.shape.as"), path->text(),
                           //% "MSTS shapes"
                           qtTrId("shape.viewer.save.msts.shapes") + QStringLiteral(" (*.s)"));
        picker.setAcceptMode(QFileDialog::AcceptSave);
        picker.setDefaultSuffix("s");
        picker.setFileMode(QFileDialog::AnyFile);
        picker.setOption(QFileDialog::DontConfirmOverwrite, true);
        if (picker.exec() == QDialog::Accepted && !picker.selectedFiles().isEmpty())
            path->setText(QDir::toNativeSeparators(picker.selectedFiles().front()));
    });
    path->selectAll();
    path->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return false;

    selection.path = QDir::fromNativeSeparators(path->text().trimmed());
    if (selection.path.isEmpty()) {
        //% "Save Shape"
        QMessageBox::warning(parent, qtTrId("shape.viewer.save.save.shape"),
                             //% "Choose an output filename."
                             qtTrId("shape.viewer.save.choose.an.output.filename"));
        return false;
    }
    if (QFileInfo(selection.path).suffix().isEmpty())
        selection.path += ".s";
    if (QFileInfo(selection.path).suffix().compare("s", Qt::CaseInsensitive) != 0) {
        //% "Save Shape"
        QMessageBox::warning(parent, qtTrId("shape.viewer.save.save.shape"),
                             //% "MSTS shape files must use the .s extension."
                             qtTrId("shape.viewer.save.msts.shape.files.must.use.the.s"));
        return false;
    }
    if (QFileInfo::exists(selection.path) &&
        //% "Replace Shape"
        QMessageBox::question(parent, qtTrId("shape.viewer.save.replace.shape"),
                              //% "%1 already exists. Replace it?"
                              qtTrId("shape.viewer.save.already.exists.replace.it")
                                  .arg(QDir::toNativeSeparators(selection.path)),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
            QMessageBox::Yes)
        return false;
    selection.format = format->currentData().toInt();
    selection.compression = compression->currentData().toInt();
    return true;
}
} // namespace

ShapeViewerWindow::ShapeViewerWindow() : QMainWindow() {
    Game::shadowsEnabled = 0;
    Game::fogDensity = 0;
    Vec3::set((float*)Game::sunLightDirection,-1.0,0.0,0.0);
    aboutWindow = new AboutWindow(this);
    englib = new EngLib();
    Game::currentEngLib = englib;

    navigatorWidget = new ShapeViewerNavigatorWidget(this);
    // The standalone viewer owns an editable, preservation-complete document.
    // Embedded preview widgets retain the configured/default backend.
    glShapeWidget = new ShapeViewerGLWidget(this, ShapeLib::MstsBackend::Complex);
    const QVariant shapeBackground = Settings::variant(
                "core.interface.shapeBackground", SettingType::Color);
    if (shapeBackground.isValid()) {
        const QColor color(shapeBackground.toString());
        glShapeWidget->setBackgroundGlColor(color.redF(), color.greenF(), color.blueF());
    }

    hierarchyWindow = new ShapeHierarchyWindow(this);
    texturesWindow = new ShapeTexturesWindow(this);
    engInfo = new EngInfoWidget(this);
    conInfo = new ConInfoWidget(this);
    shapeInfo = new ShapeInfoWidget(this);
    engInfo->hide();
    conInfo->hide();
    shapeInfo->hide();
        
    engCamera = new CameraRot();
    engCamera->setPos(0,2.5,0);
    engCamera->setPlayerRot(M_PI/2.0,0);
    
    glShapeWidget->setCamera(engCamera);
    glShapeWidget->setMode("rot");
    glShapeWidget->setMinimumSize(100, 100);
    
    // MAIN WINDOW
    QWidget* main = new QWidget();
    QHBoxLayout *mbox = new QHBoxLayout;
    mbox->setSpacing(2);
    mbox->setContentsMargins(1,1,1,1);
    mbox->addWidget(navigatorWidget);
    connect(navigatorWidget, SIGNAL(dirFilesSelected(QString)), this, SLOT(dirFilesSelected(QString)));
    connect(navigatorWidget, SIGNAL(contentHierarchySelected(int)), this, SLOT(contentHierarchySelected(int)));
    QVBoxLayout *itemLayout = new QVBoxLayout;
    itemLayout->addWidget(glShapeWidget);
    itemLayout->addWidget(conInfo);
    itemLayout->addWidget(engInfo);
    itemLayout->addWidget(shapeInfo);
    mbox->addItem(itemLayout);

    main->setLayout(mbox);
    this->setCentralWidget(main);
    //% "%1 %2 Shape Viewer"
    setWindowTitle(qtTrId("shape.viewer.title")
                   .arg(Game::AppName, Game::AppVersion));

    // MENU
    fileMenu = menuBar()->addMenu(
        //% "&File"
        qtTrId("shape.viewer.shape.viewer.window.menu.file.menu"));
    fNew = new QAction(
        //% "&Open"
        qtTrId("shape.viewer.shape.viewer.window.action.f.new"), this);
    fileMenu->addAction(fNew);
    QObject::connect(fNew, SIGNAL(triggered(bool)), this, SLOT(openFileEnabled()));
    //% "&Save"
    fSave = new QAction(qtTrId("shape.viewer.shape.viewer.window.action.f.save"), this);
    fSave->setShortcut(QKeySequence::Save);
    fSave->setEnabled(false);
    fileMenu->addAction(fSave);
    QObject::connect(fSave, SIGNAL(triggered(bool)), this, SLOT(saveFileEnabled()));
    //% "Save &As…"
    fSaveAs = new QAction(qtTrId("shape.viewer.shape.viewer.window.action.f.save.as"), this);
    fSaveAs->setShortcut(QKeySequence::SaveAs);
    fSaveAs->setEnabled(false);
    fileMenu->addAction(fSaveAs);
    QObject::connect(fSaveAs, SIGNAL(triggered(bool)), this, SLOT(saveFileAsEnabled()));
    fReload = new QAction(
        //% "&Reload"
        qtTrId("shape.viewer.shape.viewer.window.action.f.reload"), this);
    fileMenu->addAction(fReload);
    QObject::connect(fReload, SIGNAL(triggered(bool)), this, SLOT(reloadFileEnabled()));
    fExit = new QAction(
        //% "&Exit"
        qtTrId("shape.viewer.shape.viewer.window.action.f.exit"), this);
    fileMenu->addAction(fExit);
    QObject::connect(fExit, SIGNAL(triggered(bool)), this, SLOT(close()));
    
    viewMenu = menuBar()->addMenu(
        //% "&View"
        qtTrId("shape.viewer.shape.viewer.window.menu.view.menu"));
    vHierarchyView = GuiFunct::newMenuCheckAction(
        //% "&Shape Hierarchy"
        qtTrId("shape.viewer.shape.viewer.window.action.v.hierarchy.view"), this, false);
    viewMenu->addAction(vHierarchyView);
    QObject::connect(vHierarchyView, SIGNAL(triggered(bool)), this, SLOT(viewHierarchySelected(bool)));
    vTexturesView = GuiFunct::newMenuCheckAction(
        //% "&Shape Textures"
        qtTrId("shape.viewer.shape.viewer.window.action.v.textures.view"), this, false);
    viewMenu->addAction(vTexturesView);
    QObject::connect(vTexturesView, SIGNAL(triggered(bool)), this, SLOT(viewTexturesSelected(bool)));
    
    view3dMenu = menuBar()->addMenu(
        //% "&3D View"
        qtTrId("shape.viewer.shape.viewer.window.menu.view3d.menu"));
    vResetShapeView = new QAction(
        //% "&Reset"
        qtTrId("shape.viewer.shape.viewer.window.action.v.reset.shape.view"), this);
    view3dMenu->addAction(vResetShapeView);
    QObject::connect(vResetShapeView, SIGNAL(triggered()), this, SLOT(vResetShapeViewSelected()));
    vGetImgShapeView = new QAction(
        //% "&Copy Image"
        qtTrId("shape.viewer.shape.viewer.window.action.v.get.img.shape.view"), this);
    view3dMenu->addAction(vGetImgShapeView);
    QObject::connect(vGetImgShapeView, SIGNAL(triggered()), this, SLOT(vGetImgShapeViewSelected()));
    vSaveImgShapeView = new QAction(
        //% "&Save Image"
        qtTrId("shape.viewer.shape.viewer.window.action.v.save.img.shape.view"), this);
    view3dMenu->addAction(vSaveImgShapeView);
    QObject::connect(vSaveImgShapeView, SIGNAL(triggered()), this, SLOT(vSaveImgShapeViewSelected()));    
    vSetColorShapeView = new QAction(
        //% "&Set Color"
        qtTrId("shape.viewer.shape.viewer.window.action.v.set.color.shape.view"), this);
    view3dMenu->addAction(vSetColorShapeView);
    QObject::connect(vSetColorShapeView, SIGNAL(triggered()), this, SLOT(vSetColorShapeViewSelected()));
    
    helpMenu = menuBar()->addMenu(
        //% "&Help"
        qtTrId("shape.viewer.shape.viewer.window.menu.help.menu"));
    aboutAction = new QAction(
        //% "&About"
        qtTrId("shape.viewer.shape.viewer.window.action.about.action"), this);
    QObject::connect(aboutAction, SIGNAL(triggered()), this, SLOT(about()));
    helpMenu->addAction(aboutAction);
    
    resize(1280, 800);
    
    //texturesWindow->show();
    //hierarchyWindow->show();
}

void ShapeViewerWindow::viewHierarchySelected(bool show){
    if(show) hierarchyWindow->show();
    else hierarchyWindow->hide();
}

void ShapeViewerWindow::viewTexturesSelected(bool show){
    if(show) texturesWindow->show();
    else texturesWindow->hide();
}

void ShapeViewerWindow::vSetColorShapeViewSelected(){
    QColor color = QColorDialog::getColor(Qt::black, this,
        //% "Shape View Color"
        qtTrId("shape.viewer.shape.viewer.window.dialog.title.color"),  QColorDialog::DontUseNativeDialog);
    glShapeWidget->setBackgroundGlColor((float)color.redF(), (float)color.greenF(), (float)color.blueF());
}


void ShapeViewerWindow::copyImgShapeView(){
    if(glShapeWidget->screenShot != NULL)
        //QApplication::clipboard()->setImage((glShapeWidget->screenShot->mirrored(false, true)), QClipboard::Clipboard);
        QApplication::clipboard()->setImage((glShapeWidget->screenShot->flipped(Qt::Vertical)), QClipboard::Clipboard);
}

void ShapeViewerWindow::saveImgShapeView(){
    if(glShapeWidget->screenShot != NULL){
        QImage img = glShapeWidget->screenShot->flipped(Qt::Vertical);
        QString path = QFileDialog::getSaveFileName(this,
            //% "Save File"
            qtTrId("shape.viewer.shape.viewer.window.dialog.title.path"), "./",
            //% "Images (*.png *.jpg)"
            qtTrId("shape.viewer.shape.viewer.window.dialog.filter.path"));
        qDebug() << path;
        if(path.length() < 1) return;
        QFile file(path);
        if(file.open(QIODevice::WriteOnly))
            img.save(&file);
        else
            qDebug() << "Cannot save image to file!";
    }
}

void ShapeViewerWindow::vGetImgShapeViewSelected(){
    glShapeWidget->getImg();
    QTimer::singleShot(500, this, SLOT(copyImgShapeView()));
}

void ShapeViewerWindow::vSaveImgShapeViewSelected(){
    glShapeWidget->getImg();
    QTimer::singleShot(500, this, SLOT(saveImgShapeView()));
}

void ShapeViewerWindow::vResetShapeViewSelected(){
    if(currentItemType == "shape"){
        glShapeWidget->resetRot();
    }
    if(currentItemType == "eng"){
        if(currentEng == NULL)
            return;
        float pos = -currentEng->sizez-1;
        if(pos > -15) pos = -15;
        engCamera->setPos(pos,2.5,0);
        engCamera->setPlayerRot(M_PI/2.0,0);
        glShapeWidget->resetRot();
    }
    if(currentItemType == "con"){
        if(currentCon == NULL)
            return;
        if(currentCon->engItems.size() < 1)
            return;
        float pos = -currentCon->conLength;
        if(pos > -15) pos = -15;
        engCamera->setPos(pos,2.5,pos/2.0);
        engCamera->setPlayerRot(M_PI/2.0,0);
        glShapeWidget->resetRot();
    }
}

ShapeViewerWindow::~ShapeViewerWindow() {
}

void ShapeViewerWindow::reloadFileEnabled(){
    if(currentItemType == "shape"){
        if(currentShape == NULL)
            return;
        currentShape->reload();
    }
    if(currentItemType == "eng"){
        if(currentEng == NULL)
            return;
        currentEng->reload();
    }
    if(currentItemType == "con"){
        if(currentCon == NULL)
            return;
    }
}

void ShapeViewerWindow::updateShapeSaveActions() {
    const bool editable = currentItemType == "shape" &&
                          dynamic_cast<SFileComplex *>(currentShape) != nullptr;
    fSave->setEnabled(editable);
    fSaveAs->setEnabled(editable);
}

void ShapeViewerWindow::saveFileEnabled() {
    auto *shape = dynamic_cast<SFileComplex *>(currentShape);
    if (!shape)
        return;
    if (!shape->isLoaded() && !shape->loadData()) {
        //% "Save Shape"
        QMessageBox::critical(this, qtTrId("shape.viewer.save.save.shape"),
                              //% "The shape could not be loaded completely:\n%1"
                              qtTrId("shape.viewer.save.the.shape.could.not.be.loaded.completely")
                                  .arg(shape->diagnostics().join("\n")));
        return;
    }
    SFileComplex::Format format;
    bool compressed = false;
    if (!shape->storageFormat(format, compressed)) {
        //% "Save Shape"
        QMessageBox::critical(this, qtTrId("shape.viewer.save.save.shape"),
                              //% "Saving requires a complete shape document."
                              qtTrId("shape.viewer.save.saving.requires.a.complete.shape.document"));
        return;
    }
    QString error;
    if (!shape->save(shape->getPathId(), format, compressed, &error)) {
        //% "Save Shape"
        QMessageBox::critical(this, qtTrId("shape.viewer.save.save.shape"), error);
        return;
    }
    //% "Saved %1"
    statusBar()->showMessage(qtTrId("shape.viewer.save.saved").arg(QDir::toNativeSeparators(shape->getPathId())),
                             5000);
}

void ShapeViewerWindow::saveFileAsEnabled() {
    auto *shape = dynamic_cast<SFileComplex *>(currentShape);
    if (!shape)
        return;
    if (!shape->isLoaded() && !shape->loadData()) {
        //% "Save Shape As"
        QMessageBox::critical(this, qtTrId("shape.viewer.save.save.shape.as"),
                              //% "The shape could not be loaded completely:\n%1"
                              qtTrId("shape.viewer.save.the.shape.could.not.be.loaded.completely")
                                  .arg(shape->diagnostics().join("\n")));
        return;
    }
    SFileComplex::Format sourceFormat;
    bool sourceCompressed = false;
    if (!shape->storageFormat(sourceFormat, sourceCompressed)) {
        //% "Save Shape As"
        QMessageBox::critical(this, qtTrId("shape.viewer.save.save.shape.as"),
                              //% "Saving requires a complete shape document."
                              qtTrId("shape.viewer.save.saving.requires.a.complete.shape.document"));
        return;
    }
    ShapeSaveSelection selection;
    if (!chooseShapeSave(this, shape->getPathId(), sourceFormat, sourceCompressed, selection))
        return;
    const auto format = selection.format < 0
                            ? sourceFormat
                            : (selection.format == 1 ? SFileComplex::Format::Binary
                                                     : SFileComplex::Format::Text);
    const bool compressed = selection.compression < 0
                                ? sourceCompressed : selection.compression == 1;
    QString error;
    if (!shape->save(selection.path, format, compressed, &error)) {
        //% "Save Shape As"
        QMessageBox::critical(this, qtTrId("shape.viewer.save.save.shape.as"), error);
        return;
    }

    // Rebind the viewer to the saved document. If the target was already in
    // this viewer's cache, reload it so the cache observes the replacement.
    loadFile(selection.path);
    if (auto *saved = dynamic_cast<SFileComplex *>(currentShape))
        saved->reload();
    //% "Saved %1"
    statusBar()->showMessage(qtTrId("shape.viewer.save.saved").arg(QDir::toNativeSeparators(selection.path)),
                             5000);
}

void ShapeViewerWindow::openFileEnabled(){
    QFileDialog fd;
    QString path = "";
    path = ContentPath::normalize(path);
    fd.setDirectory(path);
    fd.setFileMode(QFileDialog::ExistingFiles);
    int result = fd.exec();
    QString filename;
    if (!result) 
        return;
    if(fd.selectedFiles().size() < 1)
        return;
    
    filename = fd.selectedFiles()[0];
    //filename.split()
    texturesWindow->clearLists();
    hierarchyWindow->clearLists();
    navigatorWidget->listDirectoryFiles(filename);
    
    loadFile(filename);
}

void ShapeViewerWindow::loadFile(QString path){
    path.replace("\\", "/");
    path = ContentPath::normalize(path);
    currentShape = NULL;
    updateShapeSaveActions();
    QString dir = path.section("/",0,-2);
    qDebug() << dir;
    QString filename = path.section("/",-1,-1);
    qDebug() << filename;
    
    if(filename.endsWith(".s", Qt::CaseInsensitive)
            || filename.endsWith(".gltf", Qt::CaseInsensitive)
            || filename.endsWith(".glb", Qt::CaseInsensitive)){
        currentItemType = "shape";
        glShapeWidget->showShape(path, ShapeViewerGLWidget::textureDirectory(path), &currentShape);
    }
    if(filename.endsWith(".eng", Qt::CaseInsensitive) || filename.endsWith(".wag", Qt::CaseInsensitive)){
        int idx = Game::currentEngLib->addEng(dir, filename);
        qDebug() << "eng id "<< idx;
        Eng *eng = Game::currentEngLib->eng[idx];
        float pos = -eng->sizez-1;
        if(pos > -15) pos = -15;
        engCamera->setPos(pos,2.5,0);
        engCamera->setPlayerRot(M_PI/2.0,0);
        currentItemType = "eng";
        glShapeWidget->showEng(eng);
        currentEng = eng;
    }
    if(filename.endsWith(".con", Qt::CaseInsensitive)){
        if(!dir.contains("/TRAINS/CONSISTS", Qt::CaseInsensitive))
            return;
        QString gameRoot = dir.left(dir.indexOf("/TRAINS/CONSISTS", 0, Qt::CaseInsensitive));
        Game::root = gameRoot;
        int cid = ConLib::addCon(dir, filename);
        Consist* con = ConLib::con[cid];
        if(con == NULL)
            return;
        if(con->engItems.size() < 1)
            return;
        int idx = con->engItems[0].eng;
        Eng *eng = Game::currentEngLib->eng[idx];
        float pos = -con->conLength;// -eng->sizez-1;
        if(pos > -15) pos = -15;
        engCamera->setPos(pos,2.5,pos/2.0);
        engCamera->setPlayerRot(M_PI/2.0,0);
        currentItemType = "con";
        glShapeWidget->showConSimple(cid);
        currentCon = con;
    }
    updateShapeSaveActions();
    
    //List textures:
    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateTextureInfo()));
    connect(this, SIGNAL(stopUpdateTimer()), timer, SLOT(stop()));
    timer->start(100);
}

void ShapeViewerWindow::dirFilesSelected(QString file){
    loadFile(file);
}

void ShapeViewerWindow::contentHierarchySelected(int id){
    qDebug() << currentContent.size()<<id;
    
    if(currentContent.size() < id + 1)
        return;
    qDebug() << currentContent[id]->name;
    
    if(currentShape != NULL)
        currentShape->setCurrentDistanceLevel(0,-1);
    
    currentItemType = currentContent[id]->type;
    if(currentItemType == "shape"){
        currentItemType = "shape";
        currentShape = currentContent[id]->shape;
        currentShape->setCurrentDistanceLevel(0, currentContent[id]->distanceLevelId);
        glShapeWidget->showShape(currentShape);
    }
    if(currentItemType == "eng"){
        currentShape = NULL;
        Eng *eng = currentContent[id]->eng;
        float pos = -eng->sizez-1;
        if(pos > -15) pos = -15;
        engCamera->setPos(pos,2.5,0);
        engCamera->setPlayerRot(M_PI/2.0,0);
        currentItemType = "eng";
        glShapeWidget->showEng(eng);
        currentEng = eng;
    }
    if(currentItemType == "con"){
        currentShape = NULL;
        updateShapeSaveActions();
        Consist* con = currentContent[id]->con;
        if(con == NULL)
            return;
        if(con->engItems.size() < 1)
            return;
        float pos = -con->conLength;
        if(pos > -15) pos = -15;
        engCamera->setPos(pos,2.5,pos/2.0);
        engCamera->setPlayerRot(M_PI/2.0,0);
        currentItemType = "con";
        glShapeWidget->showConSimple(con);
        currentCon = con;
    }

    updateShapeSaveActions();
    updateTextureInfo(false);
}

void ShapeViewerWindow::updateTextureInfo(){
    updateTextureInfo(true);
}

void ShapeViewerWindow::updateTextureInfo(bool refreshContentList){
    
    QHash<int, ShapeTextureInfo*> textureInfo;
    glShapeWidget->fillCurrentShapeTextureInfo(textureInfo);
    foreach(ShapeTextureInfo* i, textureInfo){
        if(i->loading)
            return;
    }
    
    engInfo->hide();
    conInfo->hide();
    shapeInfo->hide();
    
    emit stopUpdateTimer();
    texturesWindow->setTextureList(textureInfo);
        
    ShapeHierarchyInfo* hierarchyInfo = new ShapeHierarchyInfo();
    glShapeWidget->fillCurrentShapeHierarchyInfo(hierarchyInfo);
    hierarchyWindow->setHierarchyList(hierarchyInfo);
    hierarchyWindow->currentShape = currentShape;

    if(refreshContentList){
        currentContent.clear();
        glShapeWidget->fillCurrentContentHierarchyInfo(currentContent);
        navigatorWidget->listHierarchy(currentContent);
    }
    
    if(currentItemType == "shape"){
        //shapeInfo->show();
    }
    if(currentItemType == "eng"){
        engInfo->show();
        engInfo->setInfo(currentEng);
    }
    if(currentItemType == "con"){
        //conInfo->show();
    }
}

void ShapeViewerWindow::about(){
    aboutWindow->show();
}
