/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PropertiesConsist.h"
#include <tsre/Game.h>
#include <tsre/trains/Consist.h>
#include <tsre/sim/SimulationController.h>

PropertiesConsist::PropertiesConsist() {
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    infoLabel = new QLabel(
        //% "ActivityConsist:"
        qtTrId("route.editor.properties.consist.label.info.label"));
    infoLabel->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    infoLabel->setContentsMargins(3,0,0,0);
    vbox->addWidget(infoLabel);
    
    QFormLayout *vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    vlist->addRow(
        //% "Speed:"
        qtTrId("route.editor.properties.consist.label.speed"),&eSpeed);
    QObject::connect(&eSpeed, SIGNAL(textEdited(QString)),
                      this, SLOT(eSpeedEnabled(QString)));
    //vlist->addRow("Id:",&eId);
    //vlist->addRow("eId:",&eEid);
    //eObjectType.setDisabled(true);
    //eId.setDisabled(true);
    //eEid.setDisabled(true);
    vbox->addItem(vlist);
    state.setWordWrap(true);
    state.setContentsMargins(3, 0, 3, 0);
    state.setObjectName("consistSimulationState");
    state.hide();
    vbox->addWidget(&state);
    
    QPushButton *bCam = new QPushButton(
        //% "Get Camera View"
        qtTrId("route.editor.properties.consist.button.b.cam"));
    QObject::connect(bCam, SIGNAL(released()), this, SLOT(bCamEnabled()));
    vbox->addWidget(bCam);
    QPushButton *bRelease = new QPushButton(
        //% "Release Camera"
        qtTrId("route.editor.properties.consist.button.release.camera"));
    bRelease->setToolTip(
        //% "The camera leaves the train it rides and stays where it is."
        qtTrId("route.editor.properties.consist.button.release.camera.tooltip"));
    QObject::connect(bRelease, SIGNAL(released()), this, SLOT(bReleaseEnabled()));
    vbox->addWidget(bRelease);
    showBorder.setText(
        //% "Show Border"
        qtTrId("route.editor.properties.consist.option.show.border"));
    showBorder.setToolTip(
        //% "The red outline of the selected vehicle, and of the whole train the simulation has chosen; for every consist."
        qtTrId("route.editor.properties.consist.option.show.border.tooltip"));
    showBorder.setObjectName("consistShowBorder");
    showBorder.setChecked(Consist::showBorder);
    QObject::connect(&showBorder, SIGNAL(toggled(bool)), this, SLOT(showBorderEnabled(bool)));
    vbox->addWidget(&showBorder);
    
        
    vbox->addStretch(1);
    this->setLayout(vbox);
}

PropertiesConsist::~PropertiesConsist() {
}

void PropertiesConsist::showObj(GameObj* obj){
    if(obj == NULL){
        infoLabel->setText(
            //% "NULL"
            qtTrId("route.editor.properties.consist.text.null"));
        return;
    }
    conObj = (Consist*)obj;
    
    infoLabel->setText(
        //% "Object: Activity Consist"
        qtTrId("route.editor.properties.consist.text.object.activity.consist"));
    eSpeed.setText(QString::number(floor(conObj->getTrainSpeed()*3.6)));
    showBorder.setChecked(Consist::showBorder);
    showSimulated();
}

// A train of the simulation: named, its speed the simulation's (not set
// here), what it is doing.
void PropertiesConsist::showSimulated(){
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const int train = conObj != NULL ? simulation->trainOfObject(conObj) : -1;
    eSpeed.setReadOnly(train >= 0);
    state.setVisible(train >= 0);
    if(train < 0)
        return;
    infoLabel->setText(
        //% "Simulated train: %1"
        qtTrId("route.editor.properties.consist.text.simulated.train").arg(simulation->trainName(train)));
    state.setText(simulation->stateText(train));
}

void PropertiesConsist::updateObj(GameObj* obj){
    if(obj == NULL){
        return;
    }
    conObj = (Consist*)obj;
    if(!eSpeed.hasFocus())
        eSpeed.setText(QString::number(floor(conObj->getTrainSpeed()*3.6)));
    showSimulated();
    //conObj = (Consist*)obj;
}


bool PropertiesConsist::support(GameObj* obj){
    if(obj == NULL)
        return false;
    if(obj->typeObj == GameObj::consistobj)
        return true;
    return false;
}

void PropertiesConsist::bCamEnabled(){
    emit cameraObject((GameObj*)conObj);
}

void PropertiesConsist::bReleaseEnabled(){
    emit releaseCamera();
}

void PropertiesConsist::showBorderEnabled(bool val){
    Consist::showBorder = val;
}

void PropertiesConsist::eSpeedEnabled(QString val){
    if(conObj == NULL)
        return;
    bool ok = false;
    float speed = val.toFloat(&ok);
    if(ok)
        conObj->setTrainSpeed(speed/3.6);
}