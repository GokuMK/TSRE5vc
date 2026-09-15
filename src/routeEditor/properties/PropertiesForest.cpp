/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PropertiesForest.h"
#include <tsre/world/objects/WorldObj.h>
#include <tsre/world/objects/ForestObj.h>
#include <tsre/Game.h>

PropertiesForest::PropertiesForest() {
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    infoLabel = new QLabel(
        //% "Forest:"
        qtTrId("route.editor.properties.forest.label.info.label"));
    infoLabel->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    infoLabel->setContentsMargins(3,0,0,0);
    
   
    vbox->addWidget(infoLabel);
    QFormLayout *vlistt = new QFormLayout;
    vlistt->setSpacing(2);
    vlistt->setContentsMargins(3,0,3,0);
    this->tX.setDisabled(true);
    this->tY.setDisabled(true);
    vlistt->addRow(
        //% "Tile X:"
        qtTrId("route.editor.properties.forest.label.tile.x"),&this->tX);
    vlistt->addRow(
        //% "Tile Z:"
        qtTrId("route.editor.properties.forest.label.tile.z"),&this->tY);
    vbox->addItem(vlistt);
    
    QLabel * label = new QLabel(
        //% "Texture:"
        qtTrId("route.editor.properties.forest.label.label"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    QLabel * label1 = new QLabel(
        //% "FileName:"
        qtTrId("route.editor.properties.forest.label.label1"));
    label1->setContentsMargins(3,0,0,0);
    vbox->addWidget(label1);
    this->fileName.setDisabled(true);
    this->fileName.setAlignment(Qt::AlignCenter);
    vbox->addWidget(&this->fileName);
    QPushButton *copyF = new QPushButton(
        //% "Copy FileName"
        qtTrId("route.editor.properties.forest.button.copy.f"), this);
    vbox->addWidget(copyF);
    
    QLabel * label12 = new QLabel(
        //% "Size:"
        qtTrId("route.editor.properties.forest.label.label12"));
    label12->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label12->setContentsMargins(3,0,0,0);
    vbox->addWidget(label12);
    QFormLayout *vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    vlist->addRow(
        //% "Width:"
        qtTrId("route.editor.properties.forest.label.width"),&this->sizeX);
    vlist->addRow(
        //% "Height:"
        qtTrId("route.editor.properties.forest.label.height"),&this->sizeY);
    QDoubleValidator* doubleValidator = new QDoubleValidator(0, 1000, 2, this); 
    doubleValidator->setNotation(QDoubleValidator::StandardNotation);
    sizeX.setValidator(doubleValidator);
    QObject::connect(&sizeX, SIGNAL(textEdited(QString)),
                      this, SLOT(sizeEnabled(QString)));
    sizeY.setValidator(doubleValidator);
    QObject::connect(&sizeY, SIGNAL(textEdited(QString)),
                      this, SLOT(sizeEnabled(QString)));
    
    vlist->addRow(
        //% "Population:"
        qtTrId("route.editor.properties.forest.label.population"),&this->population);
    population.setValidator( new QIntValidator(0, 1000000, this) );
    QObject::connect(&population, SIGNAL(textEdited(QString)),
                      this, SLOT(populationEnabled(QString)));
    
    vlist->addRow(
        //% "Density/KM:"
        qtTrId("route.editor.properties.forest.label.density.km"),&this->densitykm);
    densitykm.setValidator( new QIntValidator(0, 1000000, this) );
    QObject::connect(&densitykm, SIGNAL(textEdited(QString)),
                      this, SLOT(densitykmEnabled(QString)));
    vbox->addItem(vlist);
    
    label = new QLabel(
        //% "Position & Rotation:"
        qtTrId("route.editor.properties.forest.label.label.2"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    vlist->addRow(
        //% "X:"
        qtTrId("route.editor.properties.forest.label.x"),&this->posX);
    vlist->addRow(
        //% "Y:"
        qtTrId("route.editor.properties.forest.label.y"),&this->posY);
    vlist->addRow(
        //% "Z:"
        qtTrId("route.editor.properties.forest.label.z"),&this->posZ);
    this->quat.setDisabled(true);
    this->quat.setAlignment(Qt::AlignCenter);
    vlist->addRow(
        //% "Rot:"
        qtTrId("route.editor.properties.forest.label.rot"),&this->quat);
    vbox->addItem(vlist);
    QGridLayout *posRotList = new QGridLayout;
    posRotList->setSpacing(2);
    posRotList->setContentsMargins(0,0,0,0);    

    QPushButton *copyPos = new QPushButton(
        //% "Copy Pos"
        qtTrId("route.editor.properties.forest.button.copy.pos"), this);
    QObject::connect(copyPos, SIGNAL(released()),
                      this, SLOT(copyPEnabled()));
    QPushButton *pastePos = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.forest.button.paste.pos"), this);
    QObject::connect(pastePos, SIGNAL(released()),
                      this, SLOT(pastePEnabled()));
    QPushButton *copyQrot = new QPushButton(
        //% "Copy Rot"
        qtTrId("route.editor.properties.forest.button.copy.qrot"), this);
    QObject::connect(copyQrot, SIGNAL(released()),
                      this, SLOT(copyREnabled()));
    QPushButton *pasteQrot = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.forest.button.paste.qrot"), this);
    QObject::connect(pasteQrot, SIGNAL(released()),
                      this, SLOT(pasteREnabled()));
    QPushButton *copyPosRot = new QPushButton(
        //% "Copy Pos+Rot"
        qtTrId("route.editor.properties.forest.button.copy.pos.rot"), this);
    QObject::connect(copyPosRot, SIGNAL(released()),
                      this, SLOT(copyPREnabled()));
    QPushButton *pastePosRot = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.forest.button.paste.pos.rot"), this);
    QObject::connect(pastePosRot, SIGNAL(released()),
                      this, SLOT(pastePREnabled()));
    QPushButton *resetQrot = new QPushButton(
        //% "Reset Rot"
        qtTrId("route.editor.properties.forest.button.reset.qrot"), this);
    QObject::connect(resetQrot, SIGNAL(released()),
                      this, SLOT(resetRotEnabled()));
    QPushButton *qRot90 = new QPushButton(
        //% "Rot Y 90°"
        qtTrId("route.editor.properties.forest.button.q.rot90"), this);
    QObject::connect(qRot90, SIGNAL(released()),
                      this, SLOT(rotYEnabled()));
    QPushButton *transform = new QPushButton(
        //% "Transform ..."
        qtTrId("route.editor.properties.forest.button.transform"), this);
    QObject::connect(transform, SIGNAL(released()),
                      this, SLOT(transformEnabled()));
    
    posRotList->addWidget(copyPos, 0, 0);
    posRotList->addWidget(pastePos, 0, 1);
    posRotList->addWidget(copyQrot, 1, 0);
    posRotList->addWidget(pasteQrot, 1, 1);
    posRotList->addWidget(copyPosRot, 2, 0);
    posRotList->addWidget(pastePosRot, 2, 1);
    posRotList->addWidget(resetQrot, 3, 0);
    posRotList->addWidget(qRot90, 3, 1);
    posRotList->addWidget(transform, 4, 0, 1, 2);
    vbox->addItem(posRotList);
    
    label = new QLabel(
        //% "Detail Level:"
        qtTrId("route.editor.properties.forest.label.label.3"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    this->defaultDetailLevel.setDisabled(true);
    this->defaultDetailLevel.setAlignment(Qt::AlignCenter);
    this->enableCustomDetailLevel.setText(
        //% "Custom"
        qtTrId("route.editor.properties.forest.text.custom"));
    QCheckBox* defaultDetailLevelLabel = new QCheckBox(
        //% "Default"
        qtTrId("route.editor.properties.forest.option.default.detail.level.label"), this);
    defaultDetailLevelLabel->setDisabled(true);
    defaultDetailLevelLabel->setChecked(true);
    QObject::connect(&enableCustomDetailLevel, SIGNAL(stateChanged(int)),
                      this, SLOT(enableCustomDetailLevelEnabled(int)));
    this->customDetailLevel.setDisabled(true);
    this->customDetailLevel.setAlignment(Qt::AlignCenter);
    QObject::connect(&customDetailLevel, SIGNAL(textEdited(QString)),
                      this, SLOT(customDetailLevelEdited(QString)));
    QGridLayout *detailLevelView = new QGridLayout;
    detailLevelView->setSpacing(2);
    detailLevelView->setContentsMargins(0,0,0,0);    
    detailLevelView->addWidget(defaultDetailLevelLabel, 0, 0);
    detailLevelView->addWidget(&defaultDetailLevel, 0, 1);
    detailLevelView->addWidget(&enableCustomDetailLevel, 1, 0);
    detailLevelView->addWidget(&customDetailLevel, 1, 1);
    vbox->addItem(detailLevelView);
    
    label = new QLabel(
        //% "Flags:"
        qtTrId("route.editor.properties.forest.label.label.4"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    this->flags.setDisabled(true);
    this->flags.setAlignment(Qt::AlignCenter);
    vbox->addWidget(&this->flags);
    QGridLayout *flagslView = new QGridLayout;
    flagslView->setSpacing(2);
    flagslView->setContentsMargins(0,0,0,0);    
    QPushButton *copyFlags = new QPushButton(
        //% "Copy Flags"
        qtTrId("route.editor.properties.forest.button.copy.flags"), this);
    QObject::connect(copyFlags, SIGNAL(released()),
                      this, SLOT(copyFEnabled()));
    QPushButton *pasteFlags = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.forest.button.paste.flags"), this);
    QObject::connect(pasteFlags, SIGNAL(released()),
                      this, SLOT(pasteFEnabled()));
    flagslView->addWidget(copyFlags,0,0);
    flagslView->addWidget(pasteFlags,0,1);
    vbox->addItem(flagslView);
    
    vbox->addStretch(1);
    this->setLayout(vbox);
}

PropertiesForest::~PropertiesForest() {
}

void PropertiesForest::showObj(GameObj* obj){
    if(obj == NULL){
        infoLabel->setText(
            //% "NULL"
            qtTrId("route.editor.properties.forest.text.null"));
        return;
    }
    worldObj = (WorldObj*)obj;
    forestObj = (ForestObj*)obj;
    ForestObj* tobj = (ForestObj*)obj;
        
    //% "Object: %1"
    this->infoLabel->setText(qtTrId("route.properties.forest.object.type")
                             .arg(forestObj->type));
    this->fileName.setText(tobj->treeTexture);
        
    this->tX.setText(QString::number(forestObj->x, 10));
    this->tY.setText(QString::number(-forestObj->y, 10));
    this->sizeX.setText(QString::number(tobj->areaX, 'G', 4));
    this->sizeY.setText(QString::number(tobj->areaZ, 'G', 4));
    this->population.setText(QString::number((int)tobj->population, 10));
    this->densitykm.setText(QString::number((int)(tobj->population*(1000000.0/(tobj->areaX*tobj->areaZ))), 10));
    this->posX.setText(QString::number(forestObj->position[0], 'G', 6));
    this->posY.setText(QString::number(forestObj->position[1], 'G', 6));
    this->posZ.setText(QString::number(-forestObj->position[2], 'G', 6));
    this->quat.setText(
            QString::number(forestObj->qDirection[0], 'G', 4) + " " +
            QString::number(forestObj->qDirection[1], 'G', 4) + " " +
            QString::number(-forestObj->qDirection[2], 'G', 4) + " " +
            QString::number(forestObj->qDirection[3], 'G', 4)
            );
}

void PropertiesForest::sizeEnabled(QString val){
    if(forestObj == NULL)
        return;
    bool ok;
    sizeX.text().toFloat(&ok);
    if(!ok) return;
    if(sizeX.text().toFloat() <= 0) return;
    sizeY.text().toFloat(&ok);
    if(!ok) return;
    if(sizeY.text().toFloat() <= 0) return;
    Undo::SinglePushWorldObjData(worldObj);
    forestObj->set("areaX", sizeX.text().toFloat());
    forestObj->set("areaZ", sizeY.text().toFloat());
    forestObj->setModified();
    forestObj->deleteVBO();
}

void PropertiesForest::populationEnabled(QString val){
    if(forestObj == NULL)
        return;
    Undo::SinglePushWorldObjData(worldObj);
    forestObj->set("population", population.text().toLongLong());
    this->densitykm.setText(QString::number((int)(forestObj->population*(1000000.0/(forestObj->areaX*forestObj->areaZ))), 10));
    forestObj->setModified();
    forestObj->deleteVBO();
}

void PropertiesForest::densitykmEnabled(QString val){
    if(forestObj == NULL)
        return;
    Undo::SinglePushWorldObjData(worldObj);
    this->population.setText(QString::number((int)(densitykm.text().toUInt()/(1000000.0/(forestObj->areaX*forestObj->areaZ))), 10));
    forestObj->set("population", population.text().toLongLong());
    forestObj->setModified();
    forestObj->deleteVBO();
}

bool PropertiesForest::support(GameObj* obj){
    if(obj == NULL)
        return false;
    if(obj->typeObj != GameObj::worldobj)
        return false;
    if(((WorldObj*)obj)->type == "forest")
        return true;
    return false;
}

void PropertiesForest::enableCustomDetailLevelEnabled(int val){
    if(worldObj == NULL)
        return;
    ForestObj* forestObj = (ForestObj*) worldObj;
    Undo::SinglePushWorldObjData(worldObj);
    if(val == 2){
        customDetailLevel.setEnabled(true);
        customDetailLevel.setText("0");
        forestObj->setCustomDetailLevel(0);
    } else {
        customDetailLevel.setEnabled(false);
        customDetailLevel.setText("");
        forestObj->setCustomDetailLevel(-1);
    }
}

void PropertiesForest::customDetailLevelEdited(QString val){
    if(worldObj == NULL)
        return;
    ForestObj* forestObj = (ForestObj*) worldObj;
    bool ok = false;
    int level = val.toInt(&ok);

    if(ok){
        Undo::SinglePushWorldObjData(worldObj);
        forestObj->setCustomDetailLevel(level);
    }
}