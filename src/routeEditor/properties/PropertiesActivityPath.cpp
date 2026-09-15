/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PropertiesActivityPath.h"
#include <tsre/trains/Path.h>
#include <tsre/Game.h>

PropertiesActivityPath::PropertiesActivityPath() {
QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    
    infoLabel = new QLabel(
        //% "Path:"
        qtTrId("route.editor.properties.activity.path.label.info.label"));
    infoLabel->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    infoLabel->setContentsMargins(3,0,0,0);
    vbox->addWidget(infoLabel);
    //QFormLayout *vlist = new QFormLayout;
    vbox->addWidget(new QLabel(
        //% "File Name"
        qtTrId("route.editor.properties.activity.path.label.file.name")));
    vbox->addWidget(&this->ePathFName);
    vbox->addWidget(new QLabel(
        //% "Display Name"
        qtTrId("route.editor.properties.activity.path.label.display.name")));
    vbox->addWidget(&this->eName);
    vbox->addWidget(new QLabel(
        //% "Start location"
        qtTrId("route.editor.properties.activity.path.label.start.location")));
    vbox->addWidget(&this->ePathStart);
    vbox->addWidget(new QLabel(
        //% "End Location"
        qtTrId("route.editor.properties.activity.path.label.end.location")));
    vbox->addWidget(&this->ePathEnd);
    
    QLabel *label = new QLabel(
        //% "Main route nodes:"
        qtTrId("route.editor.properties.activity.path.label.label"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    
    vbox->addWidget(&nodeList);
    //vlist->setSpacing(2);
    //vlist->setContentsMargins(3,0,3,0);
    //vbox->addItem(vlist);

    vbox->addStretch(1);
    this->setLayout(vbox);
}

PropertiesActivityPath::~PropertiesActivityPath() {
}

void PropertiesActivityPath::showObj(GameObj* obj){
    if(obj == NULL){
        infoLabel->setText(
            //% "NULL"
            qtTrId("route.editor.properties.activity.path.text.null"));
        return;
    }
    pathObj = (Path*)obj;
    this->infoLabel->setText(
        //% "Object: Path"
        qtTrId("route.editor.properties.activity.path.text.object.path"));
    this->ePathFName.setText(pathObj->trPathName);
    this->eName.setText(pathObj->displayName);
    this->ePathStart.setText(pathObj->trPathStart);
    this->ePathEnd.setText(pathObj->trPathEnd);
}

bool PropertiesActivityPath::support(GameObj* obj){
    if(obj == NULL)
        return false;
    if(obj->typeObj == GameObj::activitypath)
        return true;
    return false;
}