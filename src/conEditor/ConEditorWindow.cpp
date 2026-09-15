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
#include <conEditor/ConEditorWindow.h>
#include <QDebug>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/Consist.h>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include <conEditor/EngListWidget.h>
#include <conEditor/ConListWidget.h>
#include <shapeViewer/ShapeViewerGLWidget.h>
#include <tsre/camera/CameraFree.h>
#include <tsre/camera/CameraConsist.h>
#include <tsre/camera/CameraRot.h>
#include <tsre/gui/GuiFunct.h>
#include <conEditor/ConUnitsWidget.h>
#include <routeEditor/AboutWindow.h>
#include <conEditor/OverwriteDialog.h>
#include <tsre/gui/UnsavedDialog.h>
#include <tsre/gui/ChooseFileDialog.h>
#include <conEditor/RandomConsist.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/math3d/GLMatrix.h>
#include <QVector>

ConEditorWindow::ConEditorWindow() : QMainWindow() {
    Game::shadowsEnabled = 0;
    Game::fogDensity = 0;
    Vec3::set((float*)Game::sunLightDirection,-1.0,0.0,0.0);
    aboutWindow = new AboutWindow(this);
    englib = new EngLib();
    englib->loadAll(Game::root, true);
    Game::currentEngLib = englib;
    ConLib::loadAll(Game::root, true);
    ActLib::LoadAllAct(Game::root, true);
    randomConsist = new RandomConsist(this);
    glShapeWidget = new ShapeViewerGLWidget(this);
    const QVariant shapeBackground = Settings::variant(
                "core.interface.shapeBackground", SettingType::Color);
    if (shapeBackground.isValid()) {
        const QColor color(shapeBackground.toString());
        glShapeWidget->setBackgroundGlColor(color.redF(), color.greenF(), color.blueF());
    }
    //glShapeWidget->currentEngLib = englib;
    glConWidget = new ShapeViewerGLWidget(this);
    const QVariant consistBackground = Settings::variant(
                "core.interface.consistBackground", SettingType::Color);
    if (consistBackground.isValid()) {
        const QColor color(consistBackground.toString());
        glConWidget->setBackgroundGlColor(color.redF(), color.greenF(), color.blueF());
    }

    conCamera = new CameraConsist();
    conCamera->setPos(-100,2.5,42);
    conCamera->setPlayerRot(M_PI/2.0,0);
    engCamera = new CameraRot();
    engCamera->setPos(0,2.5,0);
    engCamera->setPlayerRot(M_PI/2.0,0);
    
    cDurability.setDecimals(2);
    cDurability.setMinimum(0);
    cDurability.setMaximum(2);
    cDurability.setSingleStep(0.05);
    
    glConWidget->setCamera(conCamera);
    glShapeWidget->setCamera(engCamera);
    glShapeWidget->setMode("rot");
    //qDebug()<<"aaa";
    eng1 = new EngListWidget();
    //eng1->englib = englib;
    eng1->fillEngList();
    eng2 = new EngListWidget();
    //eng2->englib = englib;
    eng2->fillEngList();
    units = new ConUnitsWidget();
    //units->englib = englib;
    con1 = new ConListWidget();
    //con1->englib = englib;
    con1->fillConList();
    //qDebug()<<"aaa";
    conSlider = new QScrollBar(Qt::Horizontal);
    conSlider->setMaximum(0);
    conSlider->setMinimum(0);
    
    QWidget* main = new QWidget();
    
    QVBoxLayout *mbox = new QVBoxLayout;
    mbox->setSpacing(2);
    mbox->setContentsMargins(1,1,1,1);
    QGridLayout *vbox = new QGridLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    vbox->addWidget(con1,0,0);
    vbox->addWidget(units,0,1);
    vbox->addWidget(eng1,0,2);
    vbox->addWidget(eng2,0,3);
    engInfo = new QWidget(this);
    QVBoxLayout *engInfoLayout = new QVBoxLayout;
    engInfoLayout->addWidget(glShapeWidget);
    glShapeWidget->setMinimumSize(100, 100);
    QGridLayout *engInfoForm = new QGridLayout;
    engInfoForm->setSpacing(2);
    engInfoForm->setContentsMargins(1,1,1,1);    
    engInfoForm->addWidget(new QLabel(
        //% "Name:"
        qtTrId("con.editor.con.editor.window.label.name")),0,0);
    engInfoForm->addWidget(new QLabel(
        //% "File Name:"
        qtTrId("con.editor.con.editor.window.label.file.name")),1,0);
    engInfoForm->addWidget(new QLabel(
        //% "Dir Name:"
        qtTrId("con.editor.con.editor.window.label.dir.name")),2,0);
    engInfoForm->addWidget(new QLabel(
        //% "Shape:"
        qtTrId("con.editor.con.editor.window.label.shape")),3,0);
    engInfoForm->addWidget(new QLabel(
        //% "Type:"
        qtTrId("con.editor.con.editor.window.label.type")),0,2);
    engInfoForm->addWidget(new QLabel(
        //% "Brakes:"
        qtTrId("con.editor.con.editor.window.label.brakes")),1,2);
    engInfoForm->addWidget(new QLabel(
        //% "Couplings:"
        qtTrId("con.editor.con.editor.window.label.couplings")),2,2);
    engInfoForm->addWidget(new QLabel(
        //% "Size:"
        qtTrId("con.editor.con.editor.window.label.size")),3,2);
    engInfoForm->addWidget(new QLabel(
        //% "Mass:"
        qtTrId("con.editor.con.editor.window.label.mass")),0,4);
    engInfoForm->addWidget(new QLabel(
        //% "Max. Speed:"
        qtTrId("con.editor.con.editor.window.label.max.speed")),1,4);
    engInfoForm->addWidget(new QLabel(
        //% "Max. Force:"
        qtTrId("con.editor.con.editor.window.label.max.force")),2,4);
    engInfoForm->addWidget(new QLabel(
        //% "Max. Power:"
        qtTrId("con.editor.con.editor.window.label.max.power")),3,4);
    
    engInfoForm->addWidget(&eName,0,1);
    engInfoForm->addWidget(&eFileName,1,1);
    engInfoForm->addWidget(&eDirName,2,1);
    engInfoForm->addWidget(&eShape,3,1);
    engInfoForm->addWidget(&eType,0,3);
    engInfoForm->addWidget(&eBrakes,1,3);
    engInfoForm->addWidget(&eCouplings,2,3);
    engInfoForm->addWidget(&eSize,3,3);
    engInfoForm->addWidget(&eMass,0,5);
    engInfoForm->addWidget(&eMaxSpeed,1,5);
    engInfoForm->addWidget(&eMaxForce,2,5);
    engInfoForm->addWidget(&eMaxPower,3,5);
    eMass.setMaximumWidth(70);
    eMaxSpeed.setMaximumWidth(70);
    eMaxForce.setMaximumWidth(70);
    eMaxPower.setMaximumWidth(70);
    engInfoLayout->addItem(engInfoForm);
    engSetsWidget = new QWidget(this);
    QGridLayout *engSetsWidgetForm = new QGridLayout;
    engSetsWidgetForm->setSpacing(0);
    engSetsWidgetForm->setContentsMargins(0,0,0,0);    
    engSetsWidget->setLayout(engSetsWidgetForm);
    QLabel *engSetsLabel = GuiFunct::newTQLabel(
        //% "Eng Sets Detected:"
        qtTrId("con.editor.con.editor.window.label.eng.sets.label"));
    QPushButton *engSetShowButton = new QPushButton(
        //% "Show"
        qtTrId("con.editor.con.editor.window.button.eng.set.show.button"));
    engSetShowButton->setFixedWidth(60);
    QPushButton *engSetHideButton = new QPushButton(
        //% "Hide"
        qtTrId("con.editor.con.editor.window.button.eng.set.hide.button"));
    engSetHideButton->setFixedWidth(60);
    QPushButton *engSetAddButton = new QPushButton(
        //% "Add to Consist"
        qtTrId("con.editor.con.editor.window.button.eng.set.add.button"));
    engSetAddButton->setFixedWidth(120);
    QPushButton *engSetAddFlipButton = new QPushButton(
        //% "Flip and add to Consist"
        qtTrId("con.editor.con.editor.window.button.eng.set.add.flip.button"));
    engSetAddFlipButton->setFixedWidth(135);
    engSetsList.setFixedWidth(250);
    engSetsWidgetForm->addWidget(engSetsLabel,0,0);
    engSetsWidgetForm->addWidget(&engSetsList,0,1);
    engSetsList.setStyleSheet("combobox-popup: 0;");
    engSetsWidgetForm->addWidget(engSetShowButton,0,2);
    engSetsWidgetForm->addWidget(engSetHideButton,0,3);
    engSetsWidgetForm->addWidget(engSetAddButton,0,4);
    engSetsWidgetForm->addWidget(engSetAddFlipButton,0,5);
    engInfoLayout->addWidget(engSetsWidget);
    engInfoLayout->setSpacing(0);
    engInfoLayout->setContentsMargins(0,0,0,0);
    engInfo->setLayout(engInfoLayout);
    vbox->addWidget(engInfo,0,4);
    //vbox->addStretch(1);
    mbox->addItem(vbox);
    conInfo = new QWidget(this);
    QGridLayout *conInfoForm = new QGridLayout;
    conInfoForm->setSpacing(2);
    conInfoForm->setContentsMargins(1,1,1,1);    
    conInfoForm->addWidget(new QLabel(
        //% "File Name:"
        qtTrId("con.editor.con.editor.window.label.file.name.2")),0,0);
    conInfoForm->addWidget(new QLabel(
        //% "Display Name:"
        qtTrId("con.editor.con.editor.window.label.display.name")),1,0);
    conInfoForm->addWidget(new QLabel(
        //% "Total Mass:"
        qtTrId("con.editor.con.editor.window.label.total.mass")),0,2);
    conInfoForm->addWidget(new QLabel(
        //% "Length:"
        qtTrId("con.editor.con.editor.window.label.length")),1,2);
    conInfoForm->addWidget(new QLabel(
        //% "Eng Mass:"
        qtTrId("con.editor.con.editor.window.label.eng.mass")),0,4);
    conInfoForm->addWidget(new QLabel(
        //% "Wag Mass:"
        qtTrId("con.editor.con.editor.window.label.wag.mass")),1,4);
    conInfoForm->addWidget(new QLabel(
        //% "Units:"
        qtTrId("con.editor.con.editor.window.label.units")),0,6);
    conInfoForm->addWidget(new QLabel(
        //% "Durability:"
        qtTrId("con.editor.con.editor.window.label.durability")),1,6);
    conInfoForm->addWidget(&cFileName,0,1);
    conInfoForm->addWidget(&cDisplayName,1,1);
    conInfoForm->addWidget(&cMass,0,3);
    conInfoForm->addWidget(&cLength,1,3);
    conInfoForm->addWidget(&cEmass,0,5);
    conInfoForm->addWidget(&cWmass,1,5);
    conInfoForm->addWidget(&cUnits,0,7);
    conInfoForm->addWidget(&cDurability,1,7);
    cMass.setFixedWidth(100);
    cEmass.setFixedWidth(100);
    cWmass.setFixedWidth(100);
    cLength.setFixedWidth(100);
    cUnits.setFixedWidth(100);
    cDurability.setFixedWidth(100);
    conInfo->setLayout(conInfoForm);
    mbox->addWidget(conInfo);
    mbox->addWidget(glConWidget);
    mbox->addWidget(conSlider);
    //glConWidget->setFixedHeight(150);
    glConWidget->setMinimumSize(1000, 100);
    QSizePolicy policy(glConWidget->sizePolicy());
    policy.setHeightForWidth(true);
    glConWidget->setSizePolicy(policy);
    
    main->setLayout(mbox);
    this->setCentralWidget(main);
    
    //% "%1 %2 Consist Editor   [ %3 ]"
    setWindowTitle(qtTrId("con.editor.title.root")
                   .arg(Game::AppName, Game::AppVersion, Game::root));
    fileMenu = menuBar()->addMenu(
        //% "&File"
        qtTrId("con.editor.con.editor.window.menu.file.menu"));
    fNew = new QAction(
        //% "&New"
        qtTrId("con.editor.con.editor.window.action.f.new"), this);
    fileMenu->addAction(fNew);
    QObject::connect(fNew, SIGNAL(triggered(bool)), this, SLOT(newConsist()));
    fSave = new QAction(
        //% "&Save"
        qtTrId("con.editor.con.editor.window.action.f.save"), this);
    fileMenu->addAction(fSave);
    QObject::connect(fSave, SIGNAL(triggered(bool)), this, SLOT(save()));
    fExit = new QAction(
        //% "&Exit"
        qtTrId("con.editor.con.editor.window.action.f.exit"), this);
    fileMenu->addAction(fExit);
    QObject::connect(fExit, SIGNAL(triggered(bool)), this, SLOT(close()));
    consistMenu = menuBar()->addMenu(
        //% "&Consist"
        qtTrId("con.editor.con.editor.window.menu.consist.menu"));
    cReverse = new QAction(
        //% "&Reverse"
        qtTrId("con.editor.con.editor.window.action.c.reverse"), this);
    consistMenu->addAction(cReverse);
    QObject::connect(cReverse, SIGNAL(triggered(bool)), this, SLOT(cReverseSelected()));
    cClone = new QAction(
        //% "&Clone"
        qtTrId("con.editor.con.editor.window.action.c.clone"), this);
    consistMenu->addAction(cClone);
    QObject::connect(cClone, SIGNAL(triggered(bool)), this, SLOT(cCloneSelected()));
    cDelete = new QAction(
        //% "&Delete"
        qtTrId("con.editor.con.editor.window.action.c.delete"), this);
    consistMenu->addAction(cDelete);
    QObject::connect(cDelete, SIGNAL(triggered(bool)), this, SLOT(cDeleteSelected()));
    cOpenInExtEditor = new QAction(
        //% "&Open in external editor"
        qtTrId("con.editor.con.editor.window.action.c.open.in.ext.editor"), this);
    consistMenu->addAction(cOpenInExtEditor);
    QObject::connect(cOpenInExtEditor, SIGNAL(triggered(bool)), this, SLOT(cOpenInExternalEditor()));
    cSaveAsEngSet = new QAction(
        //% "&Save as Eng Set"
        qtTrId("con.editor.con.editor.window.action.c.save.as.eng.set"), this);
    consistMenu->addAction(cSaveAsEngSet);
    QObject::connect(cSaveAsEngSet, SIGNAL(triggered()), this, SLOT(cSaveAsEngSetSelected()));
    engMenu = menuBar()->addMenu(
        //% "&Eng"
        qtTrId("con.editor.con.editor.window.menu.eng.menu"));
    eFindCons = new QAction(
        //% "&Find Consists"
        qtTrId("con.editor.con.editor.window.action.e.find.cons"), this);
    engMenu->addAction(eFindCons);
    QObject::connect(eFindCons, SIGNAL(triggered(bool)), this, SLOT(eFindConsistsByEng()));
    eOpenInExtEditor = new QAction(
        //% "&Open in external editor"
        qtTrId("con.editor.con.editor.window.action.e.open.in.ext.editor"), this);
    engMenu->addAction(eOpenInExtEditor);
    QObject::connect(eOpenInExtEditor, SIGNAL(triggered(bool)), this, SLOT(eOpenInExternalEditor()));
    eOpenLegacyInExtEditor = new QAction(
        //% "&Open legacy ENG in ext. editor"
        qtTrId("con.editor.con.editor.window.action.e.open.legacy.in.ext.editor"), this);
    engMenu->addAction(eOpenLegacyInExtEditor);
    QObject::connect(eOpenLegacyInExtEditor, SIGNAL(triggered(bool)), this, SLOT(eOpenLegacyInExternalEditor()));
    eReload = new QAction(
        //% "&Reload Shape"
        qtTrId("con.editor.con.editor.window.action.e.reload"), this);
    engMenu->addAction(eReload);
    QObject::connect(eReload, SIGNAL(triggered(bool)), this, SLOT(eReloadEnabled()));
    replaceMenu = menuBar()->addMenu(
        //% "&Replace"
        qtTrId("con.editor.con.editor.window.menu.replace.menu"));
    QAction *replaceOne = new QAction(
        //% "&Only selected Unit"
        qtTrId("con.editor.con.editor.window.action.replace.one"), this);
    QObject::connect(replaceOne, SIGNAL(triggered(bool)), this, SLOT(replaceOneEnabled()));
    replaceMenu->addAction(replaceOne);
    QAction *replaceAll = new QAction(
        //% "&All units in selected Consist"
        qtTrId("con.editor.con.editor.window.action.replace.all"), this);
    QObject::connect(replaceAll, SIGNAL(triggered(bool)), this, SLOT(replaceAllEnabled()));
    replaceMenu->addAction(replaceAll);
    QAction *replaceAllAll = new QAction(
        //% "&All units in all Consists"
        qtTrId("con.editor.con.editor.window.action.replace.all.all"), this);
    QObject::connect(replaceAllAll, SIGNAL(triggered(bool)), this, SLOT(replaceAllAllEnabled()));
    replaceMenu->addAction(replaceAllAll);
    viewMenu = menuBar()->addMenu(
        //% "&View"
        qtTrId("con.editor.con.editor.window.menu.view.menu"));
    vConList = GuiFunct::newMenuCheckAction(
        //% "&Consist List"
        qtTrId("con.editor.con.editor.window.action.v.con.list"), this);
    viewMenu->addAction(vConList);
    QObject::connect(vConList, SIGNAL(triggered(bool)), this, SLOT(viewConList(bool)));
    vEngList1 = GuiFunct::newMenuCheckAction(
        //% "&Eng List 1"
        qtTrId("con.editor.con.editor.window.action.v.eng.list1"), this);
    viewMenu->addAction(vEngList1);
    QObject::connect(vEngList1, SIGNAL(triggered(bool)), this, SLOT(viewEngList1(bool)));
    vEngList2 = GuiFunct::newMenuCheckAction(
        //% "&Eng List 2"
        qtTrId("con.editor.con.editor.window.action.v.eng.list2"), this);
    viewMenu->addAction(vEngList2);
    QObject::connect(vEngList2, SIGNAL(triggered(bool)), this, SLOT(viewEngList2(bool)));
    vConUnits = GuiFunct::newMenuCheckAction(
        //% "&Consist Units"
        qtTrId("con.editor.con.editor.window.action.v.con.units"), this);
    viewMenu->addAction(vConUnits);
    QObject::connect(vConUnits, SIGNAL(triggered(bool)), this, SLOT(viewConUnits(bool)));
    vEngView = GuiFunct::newMenuCheckAction(
        //% "&Eng View"
        qtTrId("con.editor.con.editor.window.action.v.eng.view"), this);
    viewMenu->addAction(vEngView);
    QObject::connect(vEngView, SIGNAL(triggered(bool)), this, SLOT(viewEngView(bool)));
    vConView = GuiFunct::newMenuCheckAction(
        //% "&Con View"
        qtTrId("con.editor.con.editor.window.action.v.con.view"), this);
    viewMenu->addAction(vConView);
    QObject::connect(vConView, SIGNAL(triggered(bool)), this, SLOT(viewConView(bool)));
    view3dMenu = menuBar()->addMenu(
        //% "&3D View"
        qtTrId("con.editor.con.editor.window.menu.view3d.menu"));
    vResetShapeView = new QAction(
        //% "&Shape View: Reset"
        qtTrId("con.editor.con.editor.window.action.v.reset.shape.view"), this);
    view3dMenu->addAction(vResetShapeView);
    QObject::connect(vResetShapeView, SIGNAL(triggered()), this, SLOT(vResetShapeViewSelected()));
    vGetImgShapeView = new QAction(
        //% "&Shape View: Copy Image"
        qtTrId("con.editor.con.editor.window.action.v.get.img.shape.view"), this);
    view3dMenu->addAction(vGetImgShapeView);
    QObject::connect(vGetImgShapeView, SIGNAL(triggered()), this, SLOT(vGetImgShapeViewSelected()));
    vSaveImgShapeView = new QAction(
        //% "&Shape View: Save Image"
        qtTrId("con.editor.con.editor.window.action.v.save.img.shape.view"), this);
    view3dMenu->addAction(vSaveImgShapeView);
    QObject::connect(vSaveImgShapeView, SIGNAL(triggered()), this, SLOT(vSaveImgShapeViewSelected()));    
    vSetColorShapeView = new QAction(
        //% "&Shape View: Set Color"
        qtTrId("con.editor.con.editor.window.action.v.set.color.shape.view"), this);
    view3dMenu->addAction(vSetColorShapeView);
    QObject::connect(vSetColorShapeView, SIGNAL(triggered()), this, SLOT(vSetColorShapeViewSelected()));
    vSetColorConView = new QAction(
        //% "&Con View: Set Color"
        qtTrId("con.editor.con.editor.window.action.v.set.color.con.view"), this);
    view3dMenu->addAction(vSetColorConView);
    QObject::connect(vSetColorConView, SIGNAL(triggered()), this, SLOT(vSetColorConViewSelected()));
    settingsMenu = menuBar()->addMenu(
        //% "&Settings"
        qtTrId("con.editor.con.editor.window.menu.settings.menu"));
    sLoadEngSetsByDefault = GuiFunct::newMenuCheckAction(
        //% "&Auto load Eng Sets"
        qtTrId("con.editor.con.editor.window.action.s.load.eng.sets.by.default"), this);
    QObject::connect(sLoadEngSetsByDefault, SIGNAL(triggered(bool)), this, SLOT(sLoadEngSetsByDefaultSelected(bool)));
    settingsMenu->addAction(sLoadEngSetsByDefault);
    sRefreshEngList = new QAction(
        //% "&Refresh Eng Data"
        qtTrId("con.editor.con.editor.window.action.s.refresh.eng.list"), this);
    QObject::connect(sRefreshEngList, SIGNAL(triggered()), this, SLOT(sRefreshEngListSelected()));
    settingsMenu->addAction(sRefreshEngList);
    sForceReloadEngList = new QAction(
        //% "&Force Reload Eng Data"
        qtTrId("con.editor.con.editor.window.action.s.force.reload.eng.list"), this);
    QObject::connect(sForceReloadEngList, SIGNAL(triggered()), this, SLOT(sForceReloadEngListSelected()));
    settingsMenu->addAction(sForceReloadEngList);
    helpMenu = menuBar()->addMenu(
        //% "&Help"
        qtTrId("con.editor.con.editor.window.menu.help.menu"));
    aboutAction = new QAction(
        //% "&About"
        qtTrId("con.editor.con.editor.window.action.about.action"), this);
    QObject::connect(aboutAction, SIGNAL(triggered()), this, SLOT(about()));
    helpMenu->addAction(aboutAction);
    
    QObject::connect(eng1, SIGNAL(engListSelected(int)),
                      this, SLOT(engListSelected(int)));
    QObject::connect(eng2, SIGNAL(engListSelected(int)),
                      this, SLOT(engListSelected(int)));
    
    QObject::connect(eng1, SIGNAL(addToConSelected(int, int, int)),
                      this, SLOT(addToConSelected(int, int, int)));
    QObject::connect(eng2, SIGNAL(addToConSelected(int, int, int)),
                      this, SLOT(addToConSelected(int, int, int)));
    
    QObject::connect(randomConsist, SIGNAL(addToConSelected(int, int, int)),
                      this, SLOT(addToConSelected(int, int, int)));
    
    QObject::connect(eng1, SIGNAL(addToRandomConsist(int)),
                      this, SLOT(addToRandomConsist(int)));
    QObject::connect(eng2, SIGNAL(addToRandomConsist(int)),
                      this, SLOT(addToRandomConsist(int)));
    
    QObject::connect(con1, SIGNAL(conListSelected(int)),
                      this, SLOT(conListSelected(int)));
    
    QObject::connect(con1, SIGNAL(conListSelected(int,int)),
                      this, SLOT(conListSelected(int,int)));
    
    QObject::connect(this, SIGNAL(showEng(QString, QString)),
                      glShapeWidget, SLOT(showEng(QString, QString))); 
    
    QObject::connect(this, SIGNAL(showEng(Eng*)),
                      glShapeWidget, SLOT(showEng(Eng*))); 
    
    QObject::connect(this, SIGNAL(showEngSet(int)),
                      glShapeWidget, SLOT(showEngSet(int))); 
    
    QObject::connect(this, SIGNAL(showCon(int)),
                      glConWidget, SLOT(showCon(int))); 
    QObject::connect(this, SIGNAL(showCon(int, int)),
                      glConWidget, SLOT(showCon(int, int))); 
    
    QObject::connect(conSlider, SIGNAL(valueChanged(int)),
                      this, SLOT(conSliderValueChanged(int))); 
    
    QObject::connect(units, SIGNAL(selected(int)),
                      this, SLOT(conUnitSelected(int))); 
    
    QObject::connect(glConWidget, SIGNAL(selected(int)),
                      this, SLOT(conUnitSelected(int))); 
    
    QObject::connect(glConWidget, SIGNAL(refreshItem()),
                      this, SLOT(refreshCurrentCon())); 
    
    QObject::connect(units, SIGNAL(refreshItem()),
                      this, SLOT(refreshCurrentCon())); 
    
    QObject::connect(&cFileName, SIGNAL(textEdited(QString)),
                      this, SLOT(cFileNameSelected(QString))); 
    
    QObject::connect(&cDisplayName, SIGNAL(textEdited(QString)),
                      this, SLOT(cDisplayNameSelected(QString))); 
    
    QObject::connect(&cDurability, SIGNAL(editingFinished()),
                      this, SLOT(cDurabilitySelected())); 

    QObject::connect(&engSetsList, SIGNAL(textActivated(QString)),
                      this, SLOT(engSetShowSet(QString)));
    
    QObject::connect(engSetShowButton, SIGNAL(released()),
        this, SLOT(engSetShowSelected()));
    QObject::connect(engSetHideButton, SIGNAL(released()),
        this, SLOT(engSetHideSelected()));
    QObject::connect(engSetAddButton, SIGNAL(released()),
        this, SLOT(engSetAddSelected()));
    QObject::connect(engSetAddFlipButton, SIGNAL(released()),
        this, SLOT(engSetFlipAndAddSelected()));
    
    const QString windowLayout = Settings::string("core.interface.consistWindowLayout");
    if(!windowLayout.contains("C"))
        vConList->trigger();
    if(!windowLayout.contains("1"))
        vEngList1->trigger();
    if(!windowLayout.contains("2"))
        vEngList2->trigger();
    if(!windowLayout.contains("U"))
        vConUnits->trigger();
}

ConEditorWindow::~ConEditorWindow() {
}

void ConEditorWindow::vSetColorConViewSelected(){
    QColor color = QColorDialog::getColor(Qt::black, this,
        //% "Shape View Color"
        qtTrId("con.editor.con.editor.window.dialog.title.color"),  QColorDialog::DontUseNativeDialog);
    glConWidget->setBackgroundGlColor((float)color.redF(), (float)color.greenF(), (float)color.blueF());
}

void ConEditorWindow::vSetColorShapeViewSelected(){
    QColor color = QColorDialog::getColor(Qt::black, this,
        //% "Shape View Color"
        qtTrId("con.editor.con.editor.window.dialog.title.color.2"),  QColorDialog::DontUseNativeDialog);
    glShapeWidget->setBackgroundGlColor((float)color.redF(), (float)color.greenF(), (float)color.blueF());
}

void ConEditorWindow::eFindConsistsByEng(){
    if(currentEng == NULL) return;
    int eid = englib->getEngByPathid(currentEng->pathid);
    if(eid < 0) return;
    con1->findConsistsByEng(eid);
}

void ConEditorWindow::cOpenInExternalEditor(){
    if(currentCon == NULL) return;
    QFileInfo fileInfo(currentCon->pathid);
    qDebug() << currentCon->pathid;
    if(fileInfo.exists())
        QDesktopServices::openUrl(QUrl::fromLocalFile(currentCon->pathid));
}

void ConEditorWindow::eOpenLegacyInExternalEditor(){
    if(currentEng == NULL) return;
    QFileInfo fileInfo(currentEng->pathid);
    if(fileInfo.exists())
        QDesktopServices::openUrl(QUrl::fromLocalFile(currentEng->pathid));
}

void ConEditorWindow::eReloadEnabled(){
    if(currentEng == NULL) return;
    Game::currentShapeLib = glShapeWidget->currentShapeLib;
    currentEng->reload();
}

void ConEditorWindow::eOpenInExternalEditor(){
    if(currentEng == NULL) return;
    if(currentEng->filePaths.size() == 1){
        QFileInfo fileInfo(currentEng->filePaths[0]);
        if(fileInfo.exists())
            QDesktopServices::openUrl(QUrl::fromLocalFile(currentEng->filePaths[0]));
    } else {
        ChooseFileDialog chooseFileDialog;
        chooseFileDialog.setMsg(
            //% "This ENG contains more than one file:"
            qtTrId("con.editor.con.editor.window.message.this.eng.contains.more.than.one.file"));
        chooseFileDialog.setWindowTitle(
            //% "Choose file:"
            qtTrId("con.editor.con.editor.window.title.choose.file"));
        for(int i = 0; i < currentEng->filePaths.size(); i++){
            chooseFileDialog.items.addItem(""+currentEng->filePaths[i]);
        }
        chooseFileDialog.exec();
        if(chooseFileDialog.changed == 1){
            QFileInfo fileInfo(currentEng->filePaths[chooseFileDialog.items.currentRow()]);
            if(fileInfo.exists())
                QDesktopServices::openUrl(QUrl::fromLocalFile(currentEng->filePaths[chooseFileDialog.items.currentRow()]));
        }
    }
}

void ConEditorWindow::copyImgShapeView(){
    if(glShapeWidget->screenShot != NULL)
        //QApplication::clipboard()->setImage((glShapeWidget->screenShot->mirrored(false, true)), QClipboard::Clipboard);
        QApplication::clipboard()->setImage((glShapeWidget->screenShot->flipped(Qt::Vertical)), QClipboard::Clipboard);
}

void ConEditorWindow::saveImgShapeView(){
    if(glShapeWidget->screenShot != NULL){
        //QImage img = glShapeWidget->screenShot->mirrored(false, true);
        QImage img = glShapeWidget->screenShot->flipped(Qt::Vertical);
        QString path = QFileDialog::getSaveFileName(this,
            //% "Save File"
            qtTrId("con.editor.con.editor.window.dialog.title.path"), "./",
            //% "Images (*.png *.jpg)"
            qtTrId("con.editor.con.editor.window.dialog.filter.path"));
        qDebug() << path;
        if(path.length() < 1) return;
        QFile file(path);
        if(file.open(QIODevice::WriteOnly))
            img.save(&file);
        else
            qDebug() << "Cannot save image to file!";
    }
}

void ConEditorWindow::vGetImgShapeViewSelected(){
    if(currentEng == NULL) return;
    glShapeWidget->getImg();
    QTimer::singleShot(500, this, SLOT(copyImgShapeView()));
}/**/

void ConEditorWindow::vSaveImgShapeViewSelected(){
    if(currentEng == NULL) return;
    glShapeWidget->getImg();
    QTimer::singleShot(500, this, SLOT(saveImgShapeView()));
}/**/

void ConEditorWindow::vResetShapeViewSelected(){
    if(currentEng == NULL) return;
    float pos = -currentEng->sizez-1;
    if(pos > -15) pos = -15;
    engCamera->setPos(pos,2.5,0);
    engCamera->setPlayerRot(M_PI/2.0,0);
    glShapeWidget->resetRot();
}

void ConEditorWindow::save(){
    if(con1->isActivity())
        saveCurrentActivity();
    else
        saveCurrentConsist();
}

void ConEditorWindow::saveCurrentActivity(){
    int id = con1->getCurrentActivityId();
    if(ActLib::Act[id] == NULL)
        return;
    ActLib::Act[id]->save();
}

void ConEditorWindow::saveCurrentConsist(){
    if(currentCon == NULL) return;
    if(currentCon->isNewConsist()){
        OverwriteDialog owerwriteDialog;
        //% "Overwrite \"%1\"?"
        owerwriteDialog.setWindowTitle(
                    qtTrId("con.editor.overwrite.title").arg(currentCon->conName));
        //owerwriteDialog.name.setText(currentCon->conName);
        QString spath;
        do {
            spath = currentCon->path + "/" + currentCon->name;
            spath = ContentPath::normalize(spath);
            qDebug() << spath;
            QFile file(spath);
            if(!file.exists())
                break;
            owerwriteDialog.exec();
            if(owerwriteDialog.changed == 0)
                return;
            if(owerwriteDialog.changed == 1)
                break;
            //currentCon->conName = owerwriteDialog.name.text();
            //currentCon->name = currentCon->conName + ".con";
            //cFileName.setText(currentCon->conName);
        } while(owerwriteDialog.changed == 2);
    }
    Game::currentEngLib = englib;
    currentCon->save();
}

void ConEditorWindow::newConsist(){
    Game::currentEngLib = englib;
    con1->newConsist();
}

void ConEditorWindow::cCloneSelected(){
    Game::currentEngLib = englib;
    if(currentCon == NULL) return;
    con1->newConsist(currentCon);
}

void ConEditorWindow::cDeleteSelected(){
    Game::currentEngLib = englib;
    if(currentCon == NULL) return;
    con1->deleteCurrentCon();
    showCon(-1);
}


void ConEditorWindow::about(){
    aboutWindow->show();
}

void ConEditorWindow::cDurabilitySelected(){
    if(currentCon == NULL) return;
    currentCon->setDurability(cDurability.value());
}

void ConEditorWindow::cFileNameSelected(QString n){
    if(currentCon == NULL) return;
    if(!currentCon->isNewConsist()){
        cFileName.setText(currentCon->conName);
        return;
    }
    currentCon->conName = n;
    if(currentCon->displayName == "")
        currentCon->showName = n;
    currentCon->name = n+".con";
    con1->updateCurrentCon();
}

void ConEditorWindow::cDisplayNameSelected(QString n){
    if(currentCon == NULL) return;
    currentCon->setDisplayName(n);
    con1->updateCurrentCon();
}

void ConEditorWindow::cReverseSelected(){
    if(currentCon == NULL) return;
    Game::currentEngLib = englib;
    currentCon->reverse();
    refreshCurrentCon();
}

void ConEditorWindow::conUnitSelected(int uid){
    if(currentCon == NULL) return;
    currentCon->select(uid);
    setCurrentEng(currentCon->engItems[uid].eng);
}

void ConEditorWindow::viewConList(bool show){
    if(show) con1->show();
    else con1->hide();
}
void ConEditorWindow::viewEngList1(bool show){
    if(show) eng1->show();
    else eng1->hide();
}
void ConEditorWindow::viewEngList2(bool show){
    if(show) eng2->show();
    else eng2->hide();
}
void ConEditorWindow::viewConUnits(bool show){
    if(show) units->show();
    else units->hide();
}
void ConEditorWindow::viewEngView(bool show){
    if(show) engInfo->show();
    else engInfo->hide();
}
void ConEditorWindow::viewConView(bool show){
    if(show) glConWidget->show();
    else glConWidget->hide();
    if(show) conInfo->show();
    else conInfo->hide();
    if(show) conSlider->show();
    else conSlider->hide();
}

void ConEditorWindow::setCurrentEng(int id, int engSetId){
    currentEng = englib->eng[id];
    qDebug() << currentEng->engName;

    engSets.clear();
    con1->getEngSets(currentEng, engSets);
    if(engSets.size() > 0){
        this->engSetsWidget->show();
        engSetsList.clear();
        for(int i = 0; i < engSets.size(); i++){
            engSetsList.addItem(ConLib::con[engSets[i]]->showName, i);
        }
    } else {
        this->engSetsWidget->hide();
        this->engSetsList.clear();
        engSetId = -1;
    }
    
    fillCurrentEng(engSetId);
}

void ConEditorWindow::fillCurrentEng(int engSetId){
    if(currentEng == NULL)
        return;
    
    float pos = 0;
    
    if(engSetId >= 0 ){
        pos = -ConLib::con[engSets[engSetId]]->conLength - 1;
        if(pos > -15) pos = -15;
        emit showEngSet(engSets[engSetId]);
    } else {
        pos = -currentEng->sizez-1;
        if(pos > -15) pos = -15;
        emit showEng(currentEng);
    }
    engCamera->setPos(pos,2.5,0);
    //engCamera->setPos(-30,2.5,0);
    engCamera->setPlayerRot(M_PI/2.0,0);
    
    eName.setText(currentEng->displayName);
    eFileName.setText(currentEng->name);
    eDirName.setText(currentEng->path.split("/").last());
    QString ttype = currentEng->type;
    if(currentEng->engType.length() > 1)
        ttype += " ( "+currentEng->engType+" )";
    eType.setText(ttype);
    //eBrakes;
    //eCouplings;
    eMass.setText(QString::number(currentEng->mass) + " t");
    if(currentEng->wagonTypeId >= 4){
        eMaxSpeed.setText(QString::number((int)currentEng->maxSpeed) + " km/h");
        eMaxForce.setText(QString::number((int)currentEng->maxForce / 1000.0) + " kN");
        eMaxPower.setText(QString::number((int)currentEng->maxPower ) + " kW");
    } else {
        eMaxSpeed.setText("--");
        eMaxForce.setText("--");
        eMaxPower.setText("--");
    }
    eShape.setText(currentEng->shape.name);
    eSize.setText(QString::number(currentEng->sizex)+" "+QString::number(currentEng->sizey)+" "+QString::number(currentEng->sizez)+" ");
    eCouplings.setText(currentEng->getCouplingsName());
    eBrakes.setText(currentEng->brakeSystemType);
}

void ConEditorWindow::engSetAddSelected(){
    if(currentCon == NULL) return;
    int cid = engSetsList.currentIndex();
    if(cid > engSets.size()) return;
    cid = engSets[cid];
    if(currentCon == ConLib::con[cid])
        return;
    
    for(int i = 0; i < ConLib::con[cid]->engItems.size(); i++)
        currentCon->appendEngItem(ConLib::con[cid]->engItems[i].eng, 2, ConLib::con[cid]->engItems[i].flip);
    refreshCurrentCon();
    conSlider->setValue(currentCon->engItems.size()-2);
}

void ConEditorWindow::engSetFlipAndAddSelected(){
    if(currentCon == NULL) return;
    int cid = engSetsList.currentIndex();
    if(cid > engSets.size()) return;
    cid = engSets[cid];
    
    for(int i = ConLib::con[cid]->engItems.size() - 1; i >= 0 ; i--){
        currentCon->appendEngItem(ConLib::con[cid]->engItems[i].eng, 2);
        currentCon->engItems[currentCon->engItems.size()-1].flip = !ConLib::con[cid]->engItems[i].flip;
    }
    refreshCurrentCon();
    conSlider->setValue(currentCon->engItems.size()-2);
}

void ConEditorWindow::engSetHideSelected(){
    this->fillCurrentEng(-1);
}

void ConEditorWindow::engSetShowSelected(){
    this->fillCurrentEng(engSetsList.currentIndex());
}

void ConEditorWindow::engSetShowSet(QString n){
    this->fillCurrentEng(engSetsList.currentIndex());
}

void ConEditorWindow::cSaveAsEngSetSelected(){
    qDebug() << "new eng set";
    if(currentCon == NULL) return;
    if(!currentCon->isNewConsist()){
        QMessageBox msgBox;
        msgBox.setText(
            //% "Consist must be new."
            qtTrId("con.editor.con.editor.window.text.consist.must.be.new"));
        msgBox.exec();
        return;
    }
    QString fileName = currentCon->name.split("#").last().split(".con").first();
    QString engName = currentCon->getFirstEngName();
    
    if(fileName == "")
        fileName = engName;
    else
        fileName = engName + "#" + fileName;
    
    currentCon->conName = fileName;
    currentCon->showName = fileName;
    cFileName.setText(currentCon->conName);
    currentCon->name = fileName+".con";
    
    OverwriteDialog owerwriteDialog;
        //% "Overwrite \"%1\"?"
        owerwriteDialog.setWindowTitle(
                    qtTrId("con.editor.overwrite.title").arg(currentCon->conName));
    
    QString spath;
    spath = currentCon->path + "/" + currentCon->name;
    spath = ContentPath::normalize(spath);
    qDebug() << spath;
    QFile file(spath);
    if(file.exists()){
        owerwriteDialog.exec();
        if(owerwriteDialog.changed == 0)
            return;
    }
    currentCon->save();
    con1->updateCurrentCon();
}

void ConEditorWindow::sLoadEngSetsByDefaultSelected(bool show){
    loadEngSetsByDefault = show;
}

void ConEditorWindow::sRefreshEngListSelected(){
    Game::currentEngLib->removeBroken();
    Game::currentEngLib->loadAll(Game::root);
    ConLib::refreshEngDataAll();
    eng1->fillEngList();
    eng2->fillEngList();
}

void ConEditorWindow::sForceReloadEngListSelected(){
    Game::currentEngLib->removeAll();
    Game::currentEngLib->loadAll(Game::root);
    ConLib::refreshEngDataAll();
    eng1->fillEngList();
    eng2->fillEngList();
}

void ConEditorWindow::engListSelected(int id){
    if(loadEngSetsByDefault)
        setCurrentEng(id, 0);
    else
        setCurrentEng(id, -1);
    //currentEng = englib->eng[id];
    qDebug() << currentEng->engName;
    //float pos = -currentEng->sizez-1;
    //if(pos > -15) pos = -15;
    //engCamera->setPos(pos,2.5,0);
    //engCamera->setPlayerRot(M_PI/2.0,0);

    //emit showEng(englib->eng[id]->path, englib->eng[id]->name);

}

void ConEditorWindow::addToConSelected(int id, int pos, int count){
    if(currentCon == NULL) return;
    Game::currentEngLib = englib;
    for(int i = 0; i < count; i++)
        currentCon->appendEngItem(id, pos);
    refreshCurrentCon();
    if(pos == 0)
        conSlider->setValue(0);
    if(pos == 2)
        conSlider->setValue(currentCon->engItems.size()-2);
    if(pos == 1)
        conSlider->setValue(currentCon->selectedIdx);
}

void ConEditorWindow::conListSelected(int id){
    currentCon = ConLib::con[id];
    qDebug() << currentCon->conName;
    refreshCurrentCon();
    conSlider->setValue(0);
    emit showCon(id);
}

void ConEditorWindow::conListSelected(int aid, int id){
    currentCon = ActLib::Act[aid]->activityObjects[id]->con;
    qDebug() << currentCon->showName;
    refreshCurrentCon();
    conSlider->setValue(0);
    emit showCon(aid, id);
}

void ConEditorWindow::refreshCurrentCon(){
    units->setCon(currentCon);
    conSlider->setMinimum(0);
    conSlider->setMaximum(currentCon->engItems.size());
    if(conSlider->value() > conSlider->maximum())
        conSlider->setValue(conSlider->maximum());
    cFileName.setText(currentCon->conName);
    cDisplayName.setText(currentCon->displayName);
    cMass.setText(QString::number(currentCon->mass) + " t");
    cEmass.setText(QString::number(currentCon->emass) + " t");
    cWmass.setText(QString::number(currentCon->mass - currentCon->emass) + " t");
    cLength.setText(QString::number(currentCon->conLength) + " m");
    cUnits.setText(QString::number(currentCon->engItems.size()));
    cDurability.setValue(currentCon->durability);
}

void ConEditorWindow::conSliderValueChanged(int val){
    if(currentCon == NULL) return;
    if(currentCon->engItems.size() < 1) return;
    if(val > currentCon->engItems.size() - 1)
        val = currentCon->engItems.size() - 1;
    float len = currentCon->engItems[val].conLength;
    conCamera->setPos(-100,2.5,42 + len);
}
//addToRandomConsist
void ConEditorWindow::addToRandomConsist(int id){
    if(englib->eng[id] == NULL) return;
    randomConsist->show();
    new QListWidgetItem ( englib->eng[id]->displayName, &randomConsist->items, id);
}

void ConEditorWindow::replaceOneEnabled(){
    int eid = englib->getEngByPointer(currentEng);
    if(eid < 0)
        return;
    if(currentCon == NULL)
        return;
    currentCon->replaceEngItemSelected(eid);
}

void ConEditorWindow::replaceAllEnabled(){
    int eid = englib->getEngByPointer(currentEng);
    if(eid < 0)
        return;
    if(currentCon == NULL)
        return;
    int oeid = currentCon->getSelectedEngId();
    currentCon->replaceEngItemById(oeid, eid);
}

void ConEditorWindow::replaceAllAllEnabled(){
    int eid = englib->getEngByPointer(currentEng);
    if(eid < 0)
        return;
    if(currentCon == NULL)
        return;
    int oeid = currentCon->getSelectedEngId();
    
    for(int i = 0; i < ConLib::jestcon; i++){
        if(ConLib::con[i] != NULL)
            ConLib::con[i]->replaceEngItemById(oeid, eid);
    }
}
    
void ConEditorWindow::closeEvent( QCloseEvent *event )
{
    QVector<int> unsavedConIds;
    QVector<int> unsavedActIds;
    con1->getUnsaed(unsavedConIds);
    con1->getUnsaedAct(unsavedActIds);
    if(unsavedConIds.size()+unsavedActIds.size() == 0){
        qDebug() << "nic do zapisania";
        event->accept();
        return;
    }
    
    UnsavedDialog unsavedDialog;
    unsavedDialog.setMsg(
        //% "Save changes in consists?"
        qtTrId("con.editor.con.editor.window.message.save.changes.in.consists"));
    unsavedDialog.setWindowTitle(
        //% "Save changes?"
        qtTrId("con.editor.con.editor.window.title.save.changes"));
    for(int i = 0; i < unsavedConIds.size(); i++){
        if(ConLib::con[unsavedConIds[i]] == NULL) continue;
        unsavedDialog.items.addItem("[C] "+ConLib::con[unsavedConIds[i]]->showName);
    }
    for(int i = 0; i < unsavedActIds.size(); i++){
        if(ActLib::Act[unsavedActIds[i]] == NULL) continue;
        unsavedDialog.items.addItem("[A] "+ActLib::Act[unsavedActIds[i]]->header->name);
    }
    unsavedDialog.exec();
    if(unsavedDialog.changed == 0){
        event->ignore();
        return;
    }
    if(unsavedDialog.changed == 2){
        event->accept();
        return;
    }
    
    for(int i = 0; i < unsavedConIds.size(); i++){
        currentCon = ConLib::con[unsavedConIds[i]];
        if(currentCon == NULL) continue;
        if(currentCon->isUnSaved())
            this->saveCurrentConsist();
    }
    for(int i = 0; i < unsavedActIds.size(); i++){
        if(ActLib::Act[unsavedActIds[i]] == NULL) continue;
        if(ActLib::Act[unsavedActIds[i]]->isUnSaved())
            ActLib::Act[unsavedActIds[i]]->save();
    }
    //qDebug() << "aaa2";
    event->accept();
}
