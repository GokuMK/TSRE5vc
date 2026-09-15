/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PropertiesStatic.h"
#include <tsre/world/objects/WorldObj.h>
#include <tsre/world/objects/StaticObj.h>
#include <tsre/fileFunctions/ParserX.h>
#include <routeEditor/properties/EditFileNameDialog.h>
#include <tsre/Game.h>

PropertiesStatic::PropertiesStatic(){
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    infoLabel = new QLabel(
        //% "Static:"
        qtTrId("route.editor.properties.static.label.info.label"));
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
        qtTrId("route.editor.properties.static.label.ui.d"),&this->uid);
    vlist->addRow(
        //% "Tile X:"
        qtTrId("route.editor.properties.static.label.tile.x"),&this->tX);
    vlist->addRow(
        //% "Tile Z:"
        qtTrId("route.editor.properties.static.label.tile.z"),&this->tY);
    vbox->addItem(vlist);
    QLabel * label;
    label = new QLabel(
        //% "FileName:"
        qtTrId("route.editor.properties.static.label.label"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    this->fileName.setDisabled(true);
    this->fileName.setAlignment(Qt::AlignCenter);
    vbox->addWidget(&this->fileName);
    QGridLayout *filenameList = new QGridLayout;
    filenameList->setSpacing(2);
    filenameList->setContentsMargins(0,0,0,0);    
    QPushButton *copyF = new QPushButton(
        //% "Copy"
        qtTrId("route.editor.properties.static.button.copy.f"), this);
    QObject::connect(copyF, SIGNAL(released()),
                      this, SLOT(copyFileNameEnabled()));
    QPushButton *editF = new QPushButton(
        //% "Edit"
        qtTrId("route.editor.properties.static.button.edit.f"), this);
    QObject::connect(editF, SIGNAL(released()),
                      this, SLOT(editFileNameEnabled()));
    filenameList->addWidget(copyF, 0, 0);
    filenameList->addWidget(editF, 0, 1);
    vbox->addItem(filenameList);
    
    label = new QLabel(
        //% "Position & Rotation:"
        qtTrId("route.editor.properties.static.label.label.2"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    vlist->addRow(
        //% "X:"
        qtTrId("route.editor.properties.static.label.x"),&this->posX);
    QDoubleValidator* doubleValidator = new QDoubleValidator(-1500, 1500, 6, this); 
    doubleValidator->setNotation(QDoubleValidator::StandardNotation);
    this->posX.setValidator(doubleValidator);
    QObject::connect(&this->posX, SIGNAL(textEdited(QString)), this, SLOT(editPositionEnabled(QString)));
    vlist->addRow(
        //% "Y:"
        qtTrId("route.editor.properties.static.label.y"),&this->posY);
    this->posY.setValidator(doubleValidator);
    QObject::connect(&this->posY, SIGNAL(textEdited(QString)), this, SLOT(editPositionEnabled(QString)));
    vlist->addRow(
        //% "Z:"
        qtTrId("route.editor.properties.static.label.z"),&this->posZ);
    this->posZ.setValidator(doubleValidator);
    QObject::connect(&this->posZ, SIGNAL(textEdited(QString)), this, SLOT(editPositionEnabled(QString)));
    this->quat.setDisabled(true);
    this->quat.setAlignment(Qt::AlignCenter);
    vlist->addRow(
        //% "Rot:"
        qtTrId("route.editor.properties.static.label.rot"),&this->quat);
    vbox->addItem(vlist);
    QGridLayout *posRotList = new QGridLayout;
    posRotList->setSpacing(2);
    posRotList->setContentsMargins(0,0,0,0);    

    QPushButton *copyPos = new QPushButton(
        //% "Copy Pos"
        qtTrId("route.editor.properties.static.button.copy.pos"), this);
    QObject::connect(copyPos, SIGNAL(released()),
                      this, SLOT(copyPEnabled()));
    QPushButton *pastePos = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.static.button.paste.pos"), this);
    QObject::connect(pastePos, SIGNAL(released()),
                      this, SLOT(pastePEnabled()));
    QPushButton *copyQrot = new QPushButton(
        //% "Copy Rot"
        qtTrId("route.editor.properties.static.button.copy.qrot"), this);
    QObject::connect(copyQrot, SIGNAL(released()),
                      this, SLOT(copyREnabled()));
    QPushButton *pasteQrot = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.static.button.paste.qrot"), this);
    QObject::connect(pasteQrot, SIGNAL(released()),
                      this, SLOT(pasteREnabled()));
    QPushButton *copyPosRot = new QPushButton(
        //% "Copy Pos+Rot"
        qtTrId("route.editor.properties.static.button.copy.pos.rot"), this);
    QObject::connect(copyPosRot, SIGNAL(released()),
                      this, SLOT(copyPREnabled()));
    QPushButton *pastePosRot = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.static.button.paste.pos.rot"), this);
    QObject::connect(pastePosRot, SIGNAL(released()),
                      this, SLOT(pastePREnabled()));
    QPushButton *resetQrot = new QPushButton(
        //% "Reset Rot"
        qtTrId("route.editor.properties.static.button.reset.qrot"), this);
    QObject::connect(resetQrot, SIGNAL(released()),
                      this, SLOT(resetRotEnabled()));
    QPushButton *qRot90 = new QPushButton(
        //% "Rot Y 90°"
        qtTrId("route.editor.properties.static.button.q.rot90"), this);
    QObject::connect(qRot90, SIGNAL(released()),
                      this, SLOT(rotYEnabled()));
    QPushButton *transform = new QPushButton(
        //% "Transform ..."
        qtTrId("route.editor.properties.static.button.transform"), this);
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
        qtTrId("route.editor.properties.static.label.label.3"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    this->defaultDetailLevel.setDisabled(true);
    this->defaultDetailLevel.setAlignment(Qt::AlignCenter);
    this->enableCustomDetailLevel.setText(
        //% "Custom"
        qtTrId("route.editor.properties.static.text.custom"));
    QCheckBox* defaultDetailLevelLabel = new QCheckBox(
        //% "Default"
        qtTrId("route.editor.properties.static.option.default.detail.level.label"), this);
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
        qtTrId("route.editor.properties.static.label.label.4"));
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
        qtTrId("route.editor.properties.static.button.copy.flags"), this);
    QObject::connect(copyFlags, SIGNAL(released()),
                      this, SLOT(copyFEnabled()));
    QPushButton *pasteFlags = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.static.button.paste.flags"), this);
    QObject::connect(pasteFlags, SIGNAL(released()),
                      this, SLOT(pasteFEnabled()));
    flagslView->addWidget(copyFlags,0,0);
    flagslView->addWidget(pasteFlags,0,1);
    vbox->addItem(flagslView);
    checkboxAnim.setText(
        //% "Animate Object"
        qtTrId("route.editor.properties.static.text.animate.object"));
    checkboxTerrain.setText(
        //% "Terrain Object"
        qtTrId("route.editor.properties.static.text.terrain.object"));
    vbox->addWidget(&checkboxAnim);
    QObject::connect(&checkboxAnim, SIGNAL(stateChanged(int)),
                      this, SLOT(checkboxAnimEdited(int)));
    vbox->addWidget(&checkboxTerrain);
    QObject::connect(&checkboxTerrain, SIGNAL(stateChanged(int)),
                      this, SLOT(checkboxTerrainEdited(int)));
    cShadowType.addItem(
        //% "No Shadow"
        qtTrId("route.editor.properties.static.item.no.shadow"));
    cShadowType.addItem(
        //% "Round Shadow"
        qtTrId("route.editor.properties.static.item.round.shadow"));
    cShadowType.addItem(
        //% "Rect. Shadow"
        qtTrId("route.editor.properties.static.item.rect.shadow"));
    cShadowType.addItem(
        //% "Treeline Shadow"
        qtTrId("route.editor.properties.static.item.treeline.shadow"));
    cShadowType.addItem(
        //% "Dynamic Shadow"
        qtTrId("route.editor.properties.static.item.dynamic.shadow"));
    cShadowType.setStyleSheet("combobox-popup: 0;");
    vbox->addWidget(&cShadowType);
    QObject::connect(&cShadowType, SIGNAL(currentIndexChanged(int)),
                      this, SLOT(cShadowTypeEdited(int)));

    label = new QLabel(
        //% "MSTS Collision:"
        qtTrId("route.editor.properties.static.label.label.5"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vbox->addWidget(&eCollisionFlags);
    eCollisionFlags.setDisabled(true);
    eCollisionFlags.setAlignment(Qt::AlignCenter);
    cCollisionType.addItem(
        //% "Disabled"
        qtTrId("route.editor.properties.static.item.disabled"));
    cCollisionType.addItem(
        //% "Immovable"
        qtTrId("route.editor.properties.static.item.immovable"));
    cCollisionType.addItem(
        //% "Buffer"
        qtTrId("route.editor.properties.static.item.buffer"));
    cCollisionType.setStyleSheet("combobox-popup: 0;");
    vbox->addWidget(&cCollisionType);
    QObject::connect(&cCollisionType, SIGNAL(currentIndexChanged(int)),
                      this, SLOT(cCollisionTypeEdited(int)));
    QPushButton *resetFlags = new QPushButton(
        //% "Remove Collisions"
        qtTrId("route.editor.properties.static.button.reset.flags"), this);
    QObject::connect(resetFlags, SIGNAL(released()),
                      this, SLOT(removeCollisionsEnabled()));
    vbox->addWidget(resetFlags);
    label = new QLabel(
        //% "Position & Rotation:"
        qtTrId("route.editor.properties.static.label.label.6"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    QPushButton *reload = new QPushButton(
        //% "Reload"
        qtTrId("route.editor.properties.static.button.reload"), this);
    QObject::connect(reload, SIGNAL(released()),
                      this, SLOT(reloadEnabled()));
    vbox->addWidget(reload);
    vbox->addStretch(1);
    this->setLayout(vbox);
    
}

PropertiesStatic::~PropertiesStatic() {
}

void PropertiesStatic::showObj(GameObj* obj){
    if(obj == NULL){
        infoLabel->setText(
            //% "NULL"
            qtTrId("route.editor.properties.static.text.null"));
        return;
    }
    worldObj = (WorldObj*)obj;
    
    //% "Object: %1"
    this->infoLabel->setText(qtTrId("route.properties.static.object.type")
                             .arg(worldObj->type));
    this->fileName.setText(worldObj->fileName);
    
    this->uid.setText(QString::number(worldObj->UiD, 10));
    this->tX.setText(QString::number(worldObj->x, 10));
    this->tY.setText(QString::number(-worldObj->y, 10));
    this->posX.setText(QString::number(worldObj->position[0], 'G', 6));
    this->posY.setText(QString::number(worldObj->position[1], 'G', 6));
    this->posZ.setText(QString::number(-worldObj->position[2], 'G', 6));
    this->quat.setText(
            QString::number(worldObj->qDirection[0], 'G', 4) + " " +
            QString::number(worldObj->qDirection[1], 'G', 4) + " " +
            QString::number(-worldObj->qDirection[2], 'G', 4) + " " +
            QString::number(worldObj->qDirection[3], 'G', 4)
            );
    
    defaultDetailLevel.setText(QString::number(worldObj->getDefaultDetailLevel()));
    enableCustomDetailLevel.blockSignals(true);
    if(worldObj->customDetailLevelEnabled()){
        enableCustomDetailLevel.setChecked(true);
        customDetailLevel.setText(QString::number(worldObj->getCustomDetailLevel()));
        customDetailLevel.setEnabled(true);
    } else {
        enableCustomDetailLevel.setChecked(false);
        customDetailLevel.setText("");
        customDetailLevel.setEnabled(false);
    }
    enableCustomDetailLevel.blockSignals(false);
    
    this->flags.setText(ParserX::MakeFlagsString(worldObj->staticFlags));
    this->checkboxAnim.blockSignals(true);
    this->checkboxTerrain.blockSignals(true);
    this->cShadowType.blockSignals(true);
    this->checkboxAnim.setChecked(worldObj->isAnimated());
    this->checkboxTerrain.setChecked(worldObj->isTerrainObj());
    this->cShadowType.setCurrentIndex((int)worldObj->getShadowType());
    this->checkboxAnim.blockSignals(false);
    this->checkboxTerrain.blockSignals(false);
    this->cShadowType.blockSignals(false);
    
    if(worldObj->type == "collideobject"){
        eCollisionFlags.setText(QString::number(worldObj->getCollisionFlags()));
    } else {
        eCollisionFlags.setText("");
    }
    int collisionType = worldObj->getCollisionType();
    this->cCollisionType.blockSignals(true);
    this->cCollisionType.setCurrentIndex(collisionType);
    this->cCollisionType.blockSignals(false);
}

void PropertiesStatic::updateObj(GameObj* obj){
    if(obj == NULL){
        return;
    }
    worldObj = (WorldObj*)obj;
    if(!posX.hasFocus() && !posY.hasFocus() && !posZ.hasFocus() && !quat.hasFocus()){
        this->uid.setText(QString::number(worldObj->UiD, 10));
        this->tX.setText(QString::number(worldObj->x, 10));
        this->tY.setText(QString::number(-worldObj->y, 10));
        this->posX.setText(QString::number(worldObj->position[0], 'G', 6));
        this->posY.setText(QString::number(worldObj->position[1], 'G', 6));
        this->posZ.setText(QString::number(-worldObj->position[2], 'G', 6));
        this->quat.setText(
                QString::number(worldObj->qDirection[0], 'G', 4) + " " +
                QString::number(worldObj->qDirection[1], 'G', 4) + " " +
                QString::number(-worldObj->qDirection[2], 'G', 4) + " " +
                QString::number(worldObj->qDirection[3], 'G', 4)
                );
    }
    
    if(worldObj->type == "collideobject"){
        eCollisionFlags.setText(QString::number(worldObj->getCollisionFlags()));
    } else {
        eCollisionFlags.setText("");
    }
}

bool PropertiesStatic::support(GameObj* obj){
    if(obj == NULL)
        return false;
    if(obj->typeObj != GameObj::worldobj)
        return false;
    if(((WorldObj*)obj)->type == "static")
        return true;
    if(((WorldObj*)obj)->type == "gantry")
        return true;
    if(((WorldObj*)obj)->type == "collideobject")
        return true;
    return false;
}

void PropertiesStatic::editPositionEnabled(QString val){
    if(worldObj == NULL)
        return;
    StaticObj* staticObj = (StaticObj*) worldObj;
    float pos[3];
    bool ok = false;
    pos[0] = this->posX.text().toFloat(&ok);
    if(!ok) return;
    pos[1] = this->posY.text().toFloat(&ok);
    if(!ok) return;
    pos[2] = -this->posZ.text().toFloat(&ok);
    if(!ok) return;
    
    Undo::SinglePushWorldObjData(worldObj);
    staticObj->setPosition((float*)pos);
    staticObj->modified = true;
    staticObj->setMartix();
}

void PropertiesStatic::enableCustomDetailLevelEnabled(int val){
    if(worldObj == NULL)
        return;
    StaticObj* staticObj = (StaticObj*) worldObj;
    Undo::SinglePushWorldObjData(worldObj);
    if(val == 2){
        customDetailLevel.setEnabled(true);
        customDetailLevel.setText("0");
        staticObj->setCustomDetailLevel(0);
    } else {
        customDetailLevel.setEnabled(false);
        customDetailLevel.setText("");
        staticObj->setCustomDetailLevel(-1);
    }
}

void PropertiesStatic::customDetailLevelEdited(QString val){
    if(worldObj == NULL)
        return;
    StaticObj* staticObj = (StaticObj*) worldObj;
    bool ok = false;
    int level = val.toInt(&ok);
    //qDebug() << "aaaaaaaaaa";
    if(ok){
        Undo::SinglePushWorldObjData(worldObj);
        staticObj->setCustomDetailLevel(level);
    }
}

void PropertiesStatic::checkboxAnimEdited(int val){
    if(worldObj == NULL)
        return;
    Undo::SinglePushWorldObjData(worldObj);
    if(val == 2){
        worldObj->setAnimated(true);
    } else {
        worldObj->setAnimated(false);
    }
    this->flags.setText(ParserX::MakeFlagsString(worldObj->staticFlags));
}

void PropertiesStatic::checkboxTerrainEdited(int val){
    if(worldObj == NULL)
        return;
    Undo::SinglePushWorldObjData(worldObj);
    if(val == 2){
        worldObj->setTerrainObj(true);
    } else {
        worldObj->setTerrainObj(false);
    }
    this->flags.setText(ParserX::MakeFlagsString(worldObj->staticFlags));
}

void PropertiesStatic::cShadowTypeEdited(int val){
    if(worldObj == NULL)
        return;
    Undo::SinglePushWorldObjData(worldObj);
    worldObj->setShadowType((WorldObj::ShadowType)val);
    this->flags.setText(ParserX::MakeFlagsString(worldObj->staticFlags));
}

void PropertiesStatic::cCollisionTypeEdited(int val){
    if(worldObj == NULL)
        return;
    Undo::SinglePushWorldObjData(worldObj);
    worldObj->setCollisionType(val-1);
}

void PropertiesStatic::removeCollisionsEnabled(){
    if(worldObj == NULL)
        return;
    Undo::SinglePushWorldObjData(worldObj);
    StaticObj* staticObj = (StaticObj*) worldObj;
    staticObj->removeCollisions();
}

void PropertiesStatic::reloadEnabled(){
    if(worldObj == NULL)
        return;
    Undo::SinglePushWorldObjData(worldObj);
    worldObj->reload();
}

void PropertiesStatic::editFileNameEnabled(){
    if(worldObj == NULL)
        return;
    EditFileNameDialog eWindow;
    eWindow.name.setText(worldObj->fileName);
    eWindow.exec();
    //qDebug() << waterWindow->changed;
    if(eWindow.isOk){
        Undo::SinglePushWorldObjData(worldObj);
        worldObj->fileName = eWindow.name.text();
        worldObj->position[2] = -worldObj->position[2];
        worldObj->qDirection[2] = -worldObj->qDirection[2];
        worldObj->load(worldObj->x, worldObj->y);
        worldObj->modified = true;
    }
}