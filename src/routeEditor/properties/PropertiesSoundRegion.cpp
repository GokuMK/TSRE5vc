/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PropertiesSoundRegion.h"
#include <tsre/world/objects/SoundRegionObj.h>
#include <tsre/world/SoundList.h>
#include <tsre/Game.h>

PropertiesSoundRegion::PropertiesSoundRegion() {
    
    QDoubleValidator* doubleValidator = new QDoubleValidator(-10000, 10000, 6, this); 
    doubleValidator->setNotation(QDoubleValidator::StandardNotation);
    
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    infoLabel = new QLabel(
        //% "SoundRegion:"
        qtTrId("route.editor.properties.sound.region.label.info.label"));
    infoLabel->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    infoLabel->setContentsMargins(3,0,0,0);
    vbox->addWidget(infoLabel);
    QFormLayout *vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    this->uid.setDisabled(true);
    this->tX.setDisabled(true);
    this->tY.setDisabled(true);
    vlist->addRow(
        //% "UiD:"
        qtTrId("route.editor.properties.sound.region.label.ui.d"),&this->uid);
    vlist->addRow(
        //% "Tile X:"
        qtTrId("route.editor.properties.sound.region.label.tile.x"),&this->tX);
    vlist->addRow(
        //% "Tile Z:"
        qtTrId("route.editor.properties.sound.region.label.tile.z"),&this->tY);
    vbox->addItem(vlist);
    
    QLabel * label2 = new QLabel(
        //% "Position:"
        qtTrId("route.editor.properties.sound.region.label.label2"));
    label2->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label2->setContentsMargins(3,0,0,0);
    vbox->addWidget(label2);
    vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    vlist->addRow(
        //% "X:"
        qtTrId("route.editor.properties.sound.region.label.x"),&this->posX);
    vlist->addRow(
        //% "Y:"
        qtTrId("route.editor.properties.sound.region.label.y"),&this->posY);
    vlist->addRow(
        //% "Z:"
        qtTrId("route.editor.properties.sound.region.label.z"),&this->posZ);
    vbox->addItem(vlist);

    QPushButton *button = new QPushButton(
        //% "Flip"
        qtTrId("route.editor.properties.sound.region.button.button"), this);
    vbox->addWidget(button);
    connect(button, SIGNAL(released()), this, SLOT(flip()));

    QLabel *label = new QLabel(
        //% "Region name:"
        qtTrId("route.editor.properties.sound.region.label.label"));
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vbox->addWidget(&this->sName);
    // Edit
    label = new QLabel(
        //% "Change name:"
        qtTrId("route.editor.properties.sound.region.label.label.2"));
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vbox->addWidget(&this->sources);
    this->sources.setStyleSheet("combobox-popup: 0;");
    QObject::connect(&this->sources, SIGNAL(textActivated(QString)),
        this, SLOT(sourcesListSelected(QString)));
    
    label = new QLabel(
        //% "Track Items:"
        qtTrId("route.editor.properties.sound.region.label.label.3"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    
    QPushButton *bDeleteSelected = new QPushButton(
        //% "Delete Selected"
        qtTrId("route.editor.properties.sound.region.button.b.delete.selected"));
    vbox->addWidget(bDeleteSelected);
    QObject::connect(bDeleteSelected, SIGNAL(released()),
                      this, SLOT(bDeleteSelectedEnabled()));
    QPushButton *bExpandSelected = new QPushButton(
        //% "Expand"
        qtTrId("route.editor.properties.sound.region.button.b.expand.selected"));
    vbox->addWidget(bExpandSelected);
    QObject::connect(bExpandSelected, SIGNAL(released()),
                      this, SLOT(bExpandEnabled()));
    
    label = new QLabel(
        //% "Global settings:"
        qtTrId("route.editor.properties.sound.region.label.label.4"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vbox->addWidget(new QLabel(
        //% "Max placing radius:"
        qtTrId("route.editor.properties.sound.region.label.max.placing.radius")));
    vbox->addWidget(&eMaxPlacingDistance);
    eMaxPlacingDistance.setValidator(doubleValidator);
    QObject::connect(&eMaxPlacingDistance, SIGNAL(textEdited(QString)), this, SLOT(eMaxPlacingDistanceEnabled(QString)));

    
    vbox->addStretch(1);
    this->setLayout(vbox);
    
}

PropertiesSoundRegion::~PropertiesSoundRegion() {
}

void PropertiesSoundRegion::sourcesListSelected(QString val){
    if(sobj == NULL)
        return;
    if(Game::soundList == NULL)
        return;
    Undo::StateBegin();
    Undo::PushGameObjData(worldObj);
    Undo::PushTrackDB(Game::trackDB);
    sobj->set("update_type", Game::soundList->regions[val]->id);
    Undo::StateEnd();
    sobj->modified = true;
    this->sName.setText(val);
}

void PropertiesSoundRegion::flip(){
    if(sobj == NULL)
        return;
    Undo::StateBegin();
    Undo::PushGameObjData(worldObj);
    Undo::PushTrackDB(Game::trackDB);
    sobj->flip();
    Undo::StateEnd();
}

void PropertiesSoundRegion::showObj(GameObj* obj){
    if(obj == NULL){
        infoLabel->setText(
            //% "NULL"
            qtTrId("route.editor.properties.sound.region.text.null"));
        return;
    }
    worldObj = (WorldObj*)obj;
    sobj = (SoundRegionObj*)obj;
    
    //% "Object: %1"
    this->infoLabel->setText(qtTrId("route.properties.sound.region.object.type")
                             .arg(sobj->type));
    
    this->uid.setText(QString::number(sobj->UiD, 10));
    this->tX.setText(QString::number(sobj->x, 10));
    this->tY.setText(QString::number(-sobj->y, 10));
    this->posX.setText(QString::number(sobj->position[0], 'G', 6));
    this->posY.setText(QString::number(sobj->position[1], 'G', 6));
    this->posZ.setText(QString::number(-sobj->position[2], 'G', 6));
    this->quat.setText(
            QString::number(sobj->qDirection[0], 'G', 4) + " " +
            QString::number(sobj->qDirection[1], 'G', 4) + " " +
            QString::number(-sobj->qDirection[2], 'G', 4) + " " +
            QString::number(sobj->qDirection[3], 'G', 4)
            );
    
    this->sources.clear();
    if(Game::soundList != NULL)
        foreach (SoundListItem* it, Game::soundList->regions){
        //for (auto it = Game::soundList->regions.begin(); it != Game::soundList->regions.end(); ++it ){
            if(it->id == sobj->getSoundregionTrackType())
                this->sName.setText(it->name);
            this->sources.addItem(it->name);
        }
        
    this->eMaxPlacingDistance.setText(QString::number(sobj->MaxPlacingDistance));
}

bool PropertiesSoundRegion::support(GameObj* obj){
    if(obj == NULL)
        return false;
    if(obj->typeObj != GameObj::worldobj)
        return false;
    if(((WorldObj*)obj)->type == "soundregion")
        return true;
    return false;
}

void PropertiesSoundRegion::eMaxPlacingDistanceEnabled(QString val){
    if(sobj == NULL){
        return;
    }
    bool ok = false;
    float fval = val.toFloat(&ok);
    if(ok){
        if(fval > 0)
            sobj->MaxPlacingDistance = fval;
    }
}

void PropertiesSoundRegion::bDeleteSelectedEnabled(){
    if(sobj == NULL)
        return;
    Undo::StateBegin();
    Undo::PushGameObjData(worldObj);
    Undo::PushTrackDB(Game::trackDB, false);
    sobj->deleteSelectedTrItem();
    Undo::StateEnd();
}

void PropertiesSoundRegion::bExpandEnabled(){
    if(sobj == NULL)
        return;
    Undo::StateBegin();
    Undo::PushGameObjData(worldObj);
    Undo::PushTrackDB(Game::trackDB, false);
    sobj->expandTrItems();
    Undo::StateEnd();
}