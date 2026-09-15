/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PropertiesActivityObject.h"
#include <tsre/Game.h>
#include <tsre/trains/ActivityObject.h>
#include <tsre/trains/Activity.h>

PropertiesActivityObject::PropertiesActivityObject() {
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    infoLabel = new QLabel(
        //% "ActivityObject:"
        qtTrId("route.editor.properties.activity.object.label.info.label"));
    infoLabel->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    infoLabel->setContentsMargins(3,0,0,0);
    vbox->addWidget(infoLabel);
    
    QFormLayout *vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    vlist->addRow(
        //% "Type:"
        qtTrId("route.editor.properties.activity.object.label.type"),&eObjectType);
    vlist->addRow(
        //% "Id:"
        qtTrId("route.editor.properties.activity.object.label.id"),&eId);
    vlist->addRow(
        //% "eId:"
        qtTrId("route.editor.properties.activity.object.label.e.id"),&eEid);
    eObjectType.setDisabled(true);
    eId.setDisabled(true);
    eEid.setDisabled(true);
    vbox->addItem(vlist);
    
    QPushButton *bDelete = new QPushButton(
        //% "Delete"
        qtTrId("route.editor.properties.activity.object.button.b.delete"));
    QObject::connect(bDelete, SIGNAL(released()), this, SLOT(bDeleteEnabled()));
    vbox->addWidget(bDelete);
    
    QLabel *label = new QLabel(
        //% "Owned by:"
        qtTrId("route.editor.properties.activity.object.label.label"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vbox->addWidget(&eActivityName);
    
    vbox->addStretch(1);
    this->setLayout(vbox);
}

PropertiesActivityObject::~PropertiesActivityObject() {
}

void PropertiesActivityObject::showObj(GameObj* obj){
    if(obj == NULL){
        infoLabel->setText(
            //% "NULL"
            qtTrId("route.editor.properties.activity.object.text.null"));
        return;
    }
    actObj = (ActivityObject*)obj;
    
    infoLabel->setText(
        //% "Object: ActivityObject"
        qtTrId("route.editor.properties.activity.object.text.object.activity.object"));
    eObjectType.setText(actObj->objectType);
    eId.setText(QString::number(actObj->getId()));
    eEid.setText(QString::number(actObj->getSelectedElementId()));
    eActivityName.setText(actObj->getParentName());

}

void PropertiesActivityObject::updateObj(GameObj* obj){
    if(obj == NULL){
        return;
    }
    actObj = (ActivityObject*)obj;

}

void PropertiesActivityObject::bDeleteEnabled(){
    if(actObj == NULL){
        return;
    }
    actObj->remove();
    emit sendMsg(QString("unselect"));
}

bool PropertiesActivityObject::support(GameObj* obj){
    if(obj == NULL)
        return false;
    if(obj->typeObj == GameObj::activityobj)
        return true;
    return false;
}
