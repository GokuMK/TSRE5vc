/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <routeEditor/TrkWindow.h>
#include <routeEditor/TerrainLodProfileDialog.h>
#include <QDebug>
#include <QString>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsPixmapItem>
#include <tsre/world/Trk.h>
#include <tsre/gui/GuiFunct.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>
#include <tsre/Game.h>
#include <QImage>

TrkWindow::TrkWindow(Mode mode) : QDialog(), mode(mode) {
    setWindowFlags(Qt::WindowStaysOnTopHint);
    //QPushButton *loadButton = new QPushButton("Load", this);
    //QImage myImage(800, 800, QImage::Format_RGB888);
    //myImage->load("F:/2.png");
    imageGraphic.setFixedSize(640,450);
    imageLoad.setFixedSize(640,450);
    //imageLabel->setContentsMargins(0,0,0,0);
    //imageLabel->setPixmap(QPixmap::fromImage(myImage));
    QVBoxLayout *mainLayout = new QVBoxLayout;
    QHBoxLayout *main2Layout = new QHBoxLayout;
    mainLayout->addItem(main2Layout);
    mainLayout->addWidget(&description);
    
    QVBoxLayout *tab1 = new QVBoxLayout;
    QGridLayout *settings = new QGridLayout;
    settings->setSpacing(2);
    settings->setContentsMargins(3,0,1,0);    
    int row = 0;
    idName.setMinimumWidth(200);
    
    settings->addWidget(GuiFunct::newTQLabel(
        //% "Route names"
        qtTrId("route.editor.trk.window.label.route.names")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Route ID: "
        qtTrId("route.editor.trk.window.label.route.id")), row++, 0);
    settings->addWidget(new QLabel(
        //% "File names: "
        qtTrId("route.editor.trk.window.label.file.names")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Display name: "
        qtTrId("route.editor.trk.window.label.display.name")), row++, 0);
    settings->addWidget(GuiFunct::newTQLabel(
        //% "Electricity"
        qtTrId("route.editor.trk.window.label.electricity")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Electrified: "
        qtTrId("route.editor.trk.window.label.electrified")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Wire Height: "
        qtTrId("route.editor.trk.window.label.wire.height")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Max Voltage: "
        qtTrId("route.editor.trk.window.label.max.voltage")), row++, 0);
    settings->addWidget(GuiFunct::newTQLabel(
        //% "Start Tile"
        qtTrId("route.editor.trk.window.label.start.tile")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Tile X: "
        qtTrId("route.editor.trk.window.label.tile.x")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Tile Z: "
        qtTrId("route.editor.trk.window.label.tile.z")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Position X: "
        qtTrId("route.editor.trk.window.label.position.x")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Position Z: "
        qtTrId("route.editor.trk.window.label.position.z")), row++, 0);
    settings->addWidget(GuiFunct::newTQLabel(
        //% "Other"
        qtTrId("route.editor.trk.window.label.other")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Speed Limit (km/h): "
        qtTrId("route.editor.trk.window.label.speed.limit.km.h")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Restricted Speed (km/h): "
        qtTrId("route.editor.trk.window.label.restricted.speed.km.h")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Speed in Miles: "
        qtTrId("route.editor.trk.window.label.speed.in.miles")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Terrain Error Scale: "
        qtTrId("route.editor.trk.window.label.terrain.error.scale")), row++, 0);
    settings->addWidget(new QLabel(
        //% "Terrain mesh LOD: "
        qtTrId("route.editor.trk.window.label.terrain.mesh.lod")), row++, 0);
    settings->addWidget(GuiFunct::newTQLabel(
        //% "Environment"
        qtTrId("route.editor.trk.window.label.environment")), row++, 0);
    settings->addWidget(&envName, row++, 0);
    QObject::connect(&envName, SIGNAL(textActivated(QString)), this, SLOT(envNameEnabled(QString)));
    envName.setStyleSheet("combobox-popup: 0;");
    settings->addWidget(GuiFunct::newTQLabel(
        //% "Description"
        qtTrId("route.editor.trk.window.label.description")), row++, 0);
    row = 0;
    row++;
    settings->addWidget(&idName, row++, 1);
    idName.setEnabled(false);
    settings->addWidget(&routeName, row++, 1);
    routeName.setEnabled(false);
    settings->addWidget(&displayName, row++, 1);
    row++;
    settings->addWidget(&electrified, row++, 1);
    electrified.setStyleSheet("combobox-popup: 0;");
    electrified.addItem(
        //% "No"
        qtTrId("route.editor.trk.window.item.no"), 0);
    electrified.addItem(
        //% "Yes"
        qtTrId("route.editor.trk.window.item.yes"), 1);
    settings->addWidget(&overheadWireHeight, row++, 1);
    overheadWireHeight.setRange(0, 10000);
    overheadWireHeight.setSingleStep(0.5);
    settings->addWidget(&maxLineVoltage, row++, 1);
    maxLineVoltage.setRange(0, 100000);
    maxLineVoltage.setSingleStep(100);
    row++;
    settings->addWidget(&startTileX, row++, 1);
    settings->addWidget(&startTileZ, row++, 1);
    settings->addWidget(&startpX, row++, 1);
    settings->addWidget(&startpZ, row++, 1);
    row++;
    settings->addWidget(&speedLimit, row++, 1);
    speedLimit.setRange(0, 1000);
    speedLimit.setSingleStep(5);
    settings->addWidget(&tempRestrictedSpeed, row++, 1);
    tempRestrictedSpeed.setRange(0, 1000);
    tempRestrictedSpeed.setSingleStep(5);
    settings->addWidget(&milepostUnitsKilometers, row++, 1);
    milepostUnitsKilometers.setStyleSheet("combobox-popup: 0;");
    milepostUnitsKilometers.addItem(
        //% "No"
        qtTrId("route.editor.trk.window.item.no.2"), 0);
    milepostUnitsKilometers.addItem(
        //% "Yes"
        qtTrId("route.editor.trk.window.item.yes.2"), 1);
    settings->addWidget(&terrainErrorScale, row++, 1);
    terrainErrorScale.setRange(0, 8);
    terrainErrorScale.setSingleStep(0.1);
    QWidget *terrainLodWidget = new QWidget;
    QVBoxLayout *terrainLodLayout = new QVBoxLayout(terrainLodWidget);
    terrainLodLayout->setContentsMargins(0, 0, 0, 0);
    terrainLodSummary.setWordWrap(true);
    terrainLodEdit.setText(
        //% "Edit terrain LOD profile..."
        qtTrId("route.editor.trk.window.text.edit.terrain.lod.profile"));
    terrainLodLayout->addWidget(&terrainLodSummary);
    terrainLodLayout->addWidget(&terrainLodEdit);
    settings->addWidget(terrainLodWidget, row++, 1);
    QObject::connect(&terrainLodEdit, &QPushButton::released,
                     this, &TrkWindow::terrainLodEnabled);
    row++;
    settings->addWidget(&envValue, row++, 1);
    tab1->addItem(settings);
    QVBoxLayout *tab2 = new QVBoxLayout;
    QHBoxLayout *ibuttons = new QHBoxLayout;
    ibuttons->addWidget(&iList);
    iList.setStyleSheet("combobox-popup: 0;");
    iList.addItem(
        //% "Load Image"
        qtTrId("route.editor.trk.window.item.load.image"), 0);
    iList.addItem(
        //% "Details Image"
        qtTrId("route.editor.trk.window.item.details.image"), 1);
    iList.setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    ibuttons->addWidget(&iCopy);
    iCopy.setText(
        //% " Copy "
        qtTrId("route.editor.trk.window.text.copy"));
    iPaste.setText(
        //% " Paste "
        qtTrId("route.editor.trk.window.text.paste"));
    ibuttons->addWidget(&iPaste);    
    tab2->addItem(ibuttons);
    tab2->addWidget(&imageLoad);
    //settings->addWidget(GuiFunct::newTQLabel("Details Image"), row++, 0);
    //tab2->addWidget(&imageGraphic);
    main2Layout->addItem(tab1);
    main2Layout->addItem(tab2);
    description.setMinimumHeight(200);
    //mainLayout->addWidget(loadButton);
    //mainLayout->addWidget(imageLabel);
    ibuttons = new QHBoxLayout;
    QPushButton *bok = new QPushButton(
        //% "Apply"
        qtTrId("route.editor.trk.window.button.apply"));
    QObject::connect(bok, SIGNAL(released()), this, SLOT(bokEnabled()));
    QPushButton *bcancel = new QPushButton(
        mode == Mode::NewRouteTemplate
            //% "Skip"
            ? qtTrId("route.editor.trk.window.button.skip")
            //% "Discard"
            : qtTrId("route.editor.trk.window.button.discard"));
    QObject::connect(bcancel, SIGNAL(released()), this, SLOT(bcancelEnabled()));
    ibuttons->addWidget(bok);
    ibuttons->addWidget(bcancel);
    mainLayout->addItem(ibuttons);
    mainLayout->setContentsMargins(1,1,1,1);
    this->setLayout(mainLayout);
    
    //QObject::connect(loadButton, SIGNAL(released()),
    //                  this, SLOT(load()));
}

int TrkWindow::exec() {
    if(this->trk == NULL)
        return 0;
    this->setWindowTitle(
        //% "Route settings"
        qtTrId("route.editor.trk.window.title.route.settings"));
    
    this->idName.setText(trk->idName);
    this->routeName.setText(trk->routeName);
    this->displayName.setText(trk->displayName);
    this->maxLineVoltage.setValue(trk->maxLineVoltage);
    this->speedLimit.setValue(trk->speedLimit*3.6);
    this->tempRestrictedSpeed.setValue(trk->tempRestrictedSpeed*3.6);
    this->overheadWireHeight.setValue(trk->overheadWireHeight);
    if(trk->electrified){
        this->electrified.setCurrentIndex(1);
    }else{
        this->electrified.setCurrentText(0);
    }
    
    if(trk->milepostUnitsKilometers){
        this->milepostUnitsKilometers.setCurrentIndex(0);
    }else{
        this->milepostUnitsKilometers.setCurrentIndex(1);
    }
    
    this->startTileX.setText(QString::number(trk->startTileX));
    this->startTileZ.setText(QString::number(trk->startTileZ));
    this->startpX.setText(QString::number(trk->startpX));
    this->startpZ.setText(QString::number(trk->startpZ));
    
    this->terrainErrorScale.setValue(trk->terrainErrorScale);
    pendingTerrainLodLevels = trk->terrainLodLevels;
    terrainLodSummary.setText(
                TerrainLodProfileDialog::summary(pendingTerrainLodLevels));
    
    QString txt = trk->description;
    txt.replace("\\n","\n");
    this->description.setPlainText(txt);
    for (auto it = trk->environment.begin(); it != trk->environment.end(); ++it)
        this->envName.addItem(QString::fromStdString((*it).first));
    this->envValue.setText(trk->environment[this->envName.itemText(0).toStdString()]);
    
    Texture * tex1 = NULL;
    const auto texture = TexLib::mtex.find(trk->imageLoadId);
    if(texture != TexLib::mtex.end() && texture->second != NULL)
        if(texture->second->loaded){
            tex1 = texture->second;
            unsigned char * out = tex1->getImageData(640,450);
            if(tex1->bytesPerPixel == 3)
                imageLoad.setPixmap(QPixmap::fromImage(QImage(out,640,450,QImage::Format_RGB888)));
            if(tex1->bytesPerPixel == 4)
                imageLoad.setPixmap(QPixmap::fromImage(QImage(out,640,450,QImage::Format_RGBA8888)));   
            delete[] out;
    }
    //int imageLoadId = TexLib::addTex(Game::root+"/ROUTES/"+idName+"/"+imageLoad);
    
    
    
    return QDialog::exec();
} 

void TrkWindow::envNameEnabled(QString item){
    this->envValue.setText(trk->environment[item.toStdString()]);
}

void TrkWindow::terrainLodEnabled() {
    TerrainLodProfileDialog dialog(this);
    dialog.setLevels(pendingTerrainLodLevels);
    if (dialog.exec() != QDialog::Accepted)
        return;
    pendingTerrainLodLevels = dialog.levels();
    terrainLodSummary.setText(
                TerrainLodProfileDialog::summary(pendingTerrainLodLevels));
}

void TrkWindow::bokEnabled(){
    trk->setModified(true);
    trk->displayName = displayName.text();
    trk->startTileX = startTileX.text().toInt();
    trk->startTileZ = startTileZ.text().toInt();
    trk->startpX = startpX.text().toFloat();
    trk->startpZ = startpZ.text().toFloat();
    trk->description = description.toPlainText();
    trk->description.replace("\n","\\n");
    
    trk->electrified = electrified.currentIndex();
    trk->overheadWireHeight = overheadWireHeight.value();
    trk->maxLineVoltage = maxLineVoltage.value();
    if(milepostUnitsKilometers.currentIndex() == 1)
        trk->milepostUnitsKilometers = false;
    else
        trk->milepostUnitsKilometers = true;
    
    trk->tempRestrictedSpeed = tempRestrictedSpeed.value()/3.6;
    trk->speedLimit = speedLimit.value()/3.6;
    trk->terrainLodLevels = pendingTerrainLodLevels;
    accept();
}

void TrkWindow::bcancelEnabled(){
    reject();
}

TrkWindow::~TrkWindow() {
}

