/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PropertiesPickup.h"
#include <tsre/world/objects/PickupObj.h>
#include <tsre/gui/GuiFunct.h>
#include <tsre/Game.h>

PropertiesPickup::PropertiesPickup() {
    QDoubleValidator* doubleValidator = new QDoubleValidator(-1000000, 1000000, 6, this);
    doubleValidator->setNotation(QDoubleValidator::StandardNotation);

    cPickupType.addItem(
        //% "Freight - grain"
        qtTrId("route.editor.properties.pickup.item.freight.grain"));
    cPickupType.addItem(
        //% "Freight - coal"
        qtTrId("route.editor.properties.pickup.item.freight.coal"));
    cPickupType.addItem(
        //% "Freight - gravel"
        qtTrId("route.editor.properties.pickup.item.freight.gravel"));
    cPickupType.addItem(
        //% "Freight - sand"
        qtTrId("route.editor.properties.pickup.item.freight.sand"));
    cPickupType.addItem(
        //% "Fuel - water"
        qtTrId("route.editor.properties.pickup.item.fuel.water"));
    cPickupType.addItem(
        //% "Fuel - coal"
        qtTrId("route.editor.properties.pickup.item.fuel.coal"));
    cPickupType.addItem(
        //% "Fuel - diesel"
        qtTrId("route.editor.properties.pickup.item.fuel.diesel"));
    cPickupType.addItem(
        //% "Special - mail"
        qtTrId("route.editor.properties.pickup.item.special.mail"));
    cPickupType.setStyleSheet("combobox-popup: 0;");
    QObject::connect(&cPickupType, SIGNAL(currentIndexChanged(int)),
            this, SLOT(cPickupTypeEdited(int)));

    cAnimType.addItem(
        //% "Activated animation only"
        qtTrId("route.editor.properties.pickup.item.activated.animation.only"));
    cAnimType.addItem(
        //% "Proximity animation only"
        qtTrId("route.editor.properties.pickup.item.proximity.animation.only"));
    cAnimType.addItem(
        //% "Activated effects only"
        qtTrId("route.editor.properties.pickup.item.activated.effects.only"));
    cAnimType.addItem(
        //% "Activated anim. and effects"
        qtTrId("route.editor.properties.pickup.item.activated.anim.effects"));
    cAnimType.addItem(
        //% "Proximity anim. and effects"
        qtTrId("route.editor.properties.pickup.item.proximity.anim.effects"));
    cAnimType.addItem(
        //% "On empty animation only"
        qtTrId("route.editor.properties.pickup.item.on.empty.animation.only"));
    cAnimType.addItem(
        //% "Fuel hose only"
        qtTrId("route.editor.properties.pickup.item.fuel.hose.only"));
    cAnimType.setStyleSheet("combobox-popup: 0;");
    QObject::connect(&cAnimType, SIGNAL(currentIndexChanged(int)),
            this, SLOT(cAnimTypeEdited(int)));

    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0, 1, 1, 1);
    infoLabel = new QLabel(
        //% "Pickup:"
        qtTrId("route.editor.properties.pickup.label.info.label"));
    infoLabel->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    infoLabel->setContentsMargins(3, 0, 0, 0);
    vbox->addWidget(infoLabel);

    QLabel *label = new QLabel(
        //% "Filename:"
        qtTrId("route.editor.properties.pickup.label.label"));
    label->setContentsMargins(3, 0, 0, 0);
    vbox->addWidget(label);
    this->fileName.setDisabled(true);
    this->fileName.setAlignment(Qt::AlignCenter);
    vbox->addWidget(&this->fileName);
    label = new QLabel(
        //% "Filename - broken:"
        qtTrId("route.editor.properties.pickup.label.label.2"));
    label->setContentsMargins(3, 0, 0, 0);
    vbox->addWidget(label);
    this->eBrokenFileName.setDisabled(true);
    this->eBrokenFileName.setAlignment(Qt::AlignCenter);
    vbox->addWidget(&this->eBrokenFileName);
    label = new QLabel(
        //% "Properties:"
        qtTrId("route.editor.properties.pickup.label.label.3"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3, 0, 0, 0);
    vbox->addWidget(label);
    QFormLayout *vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3, 0, 3, 0);
    label = new QLabel(
        //% "Type:"
        qtTrId("route.editor.properties.pickup.label.label.4"));
    label->setContentsMargins(3, 0, 0, 0);
    vbox->addWidget(label);
    vbox->addWidget(&cPickupType);
    vlist->addRow(
        //% "Capacity:"
        qtTrId("route.editor.properties.pickup.label.capacity"), &eCapacity);
    eCapacity.setValidator(doubleValidator);
    QObject::connect(&eCapacity, SIGNAL(textEdited(QString)), this, SLOT(eCapacityEnabled(QString)));

    vlist->addRow(
        //% "Content:"
        qtTrId("route.editor.properties.pickup.label.content"), &eContent);
    eContent.setValidator(doubleValidator);
    QObject::connect(&eContent, SIGNAL(textEdited(QString)), this, SLOT(eContentEnabled(QString)));

    vlist->addRow(
        //% "Fill rate:"
        qtTrId("route.editor.properties.pickup.label.fill.rate"), &eFill);
    eFill.setValidator(doubleValidator);
    QObject::connect(&eFill, SIGNAL(textEdited(QString)), this, SLOT(eFillEnabled(QString)));

    vlist->addRow(
        //% "Speed min:"
        qtTrId("route.editor.properties.pickup.label.speed.min"), &eSpeedMin);
    eSpeedMin.setValidator(doubleValidator);
    QObject::connect(&eSpeedMin, SIGNAL(textEdited(QString)), this, SLOT(eSpeedMinEnabled(QString)));

    vlist->addRow(
        //% "Speed max:"
        qtTrId("route.editor.properties.pickup.label.speed.max"), &eSpeedMax);
    eSpeedMax.setValidator(doubleValidator);
    QObject::connect(&eSpeedMax, SIGNAL(textEdited(QString)), this, SLOT(eSpeedMaxEnabled(QString)));

    vbox->addItem(vlist);
    label = new QLabel(
        //% "Anim type:"
        qtTrId("route.editor.properties.pickup.label.label.5"));
    label->setContentsMargins(3, 0, 0, 0);
    vbox->addWidget(label);
    vbox->addWidget(&cAnimType);
    vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3, 0, 3, 0);
    vlist->addRow(
        //% "Anim lenght:"
        qtTrId("route.editor.properties.pickup.label.anim.lenght"), &eAnimLength);
    eAnimLength.setValidator(doubleValidator);
    QObject::connect(&eAnimLength, SIGNAL(textEdited(QString)), this, SLOT(eAnimLengthEnabled(QString)));

    vbox->addItem(vlist);
    vbox->addWidget(&chInfinite);
    vbox->addWidget(&chBroken);
    chInfinite.setText(
        //% "Infinite capacity"
        qtTrId("route.editor.properties.pickup.text.infinite.capacity"));
    QObject::connect(&chInfinite, SIGNAL(stateChanged(int)),
            this, SLOT(chInfiniteEnabled(int)));
    chBroken.setText(
        //% "Broken by default"
        qtTrId("route.editor.properties.pickup.text.broken.by.default"));
    QObject::connect(&chBroken, SIGNAL(stateChanged(int)),
            this, SLOT(chBrokenEnabled(int)));
    vbox->addStretch(1);
    this->setLayout(vbox);

}

PropertiesPickup::~PropertiesPickup() {
}

void PropertiesPickup::showObj(GameObj* obj) {
    if (obj == NULL) {
        infoLabel->setText(
            //% "NULL"
            qtTrId("route.editor.properties.pickup.text.null"));
        return;
    }
    worldObj = (WorldObj*)obj;
    pobj = (PickupObj*) obj;

    //% "Object: %1"
    this->infoLabel->setText(qtTrId("route.properties.pickup.object.type")
                             .arg(pobj->type));
    this->fileName.setText(pobj->fileName);
    this->eBrokenFileName.setText(pobj->fileName.split('.')[0] + "_d.s");
    cPickupType.blockSignals(true);
    cPickupType.setCurrentIndex(pobj->getTypeId());
    cPickupType.blockSignals(false);
    eCapacity.setText(QString::number(pobj->getCapacity()));
    eContent.setText(QString::number(pobj->getPickupContent()));
    eFill.setText(QString::number(pobj->getFillRate()));
    eSpeedMin.setText(QString::number(pobj->getSpeedMin()));
    eSpeedMax.setText(QString::number(pobj->getSpeedMax()));
    cAnimType.blockSignals(true);
    cAnimType.setCurrentIndex(pobj->getAnimTypeId());
    cAnimType.blockSignals(false);
    eAnimLength.setText(QString::number(pobj->getAnimLength()));
    this->chInfinite.blockSignals(true);
    chInfinite.setChecked((pobj->isInfinite()));
    this->chInfinite.blockSignals(false);
    this->chBroken.blockSignals(true);
    this->chBroken.setChecked(pobj->isBroken());
    this->chBroken.blockSignals(false);
}

void PropertiesPickup::cPickupTypeEdited(int val) {
    if (pobj == NULL) {
        return;
    }
    Undo::SinglePushWorldObjData(worldObj);
    pobj->setTypeId(val);
}

void PropertiesPickup::cAnimTypeEdited(int val) {
    if (pobj == NULL) {
        return;
    }
    Undo::SinglePushWorldObjData(worldObj);
    pobj->setAnimTypeId(val);
}

void PropertiesPickup::eCapacityEnabled(QString val){
    if (pobj == NULL) {
        return;
    }
    bool ok = false;
    float fval = val.toFloat(&ok);
    if(ok){
        Undo::SinglePushWorldObjData(worldObj);
        pobj->setCapacity(fval);
    }
}

void PropertiesPickup::eContentEnabled(QString val){
    if (pobj == NULL) {
        return;
    }
    bool ok = false;
    float fval = val.toFloat(&ok);
    if(ok){
        Undo::StateBegin();
        Undo::PushGameObjData(worldObj);
        Undo::PushTrackDB(Game::trackDB);
        pobj->setPickupContent(fval);
        Undo::StateEnd();
    }
}

void PropertiesPickup::eFillEnabled(QString val){
    if (pobj == NULL) {
        return;
    }
    bool ok = false;
    float fval = val.toFloat(&ok);
    if(ok){
        Undo::SinglePushWorldObjData(worldObj);
        pobj->setFillRate(fval);
    }
}

void PropertiesPickup::eSpeedMinEnabled(QString val){
    if (pobj == NULL) {
        return;
    }
    bool ok = false;
    float fval = val.toFloat(&ok);
    if(ok){
        Undo::SinglePushWorldObjData(worldObj);
        pobj->setSpeedMin(fval);
    }
}

void PropertiesPickup::eSpeedMaxEnabled(QString val){
    if (pobj == NULL) {
        return;
    }
    bool ok = false;
    float fval = val.toFloat(&ok);
    if(ok){
        Undo::SinglePushWorldObjData(worldObj);
        pobj->setSpeedMax(fval);
    }
}

void PropertiesPickup::eAnimLengthEnabled(QString val){
    if (pobj == NULL) {
        return;
    }
    bool ok = false;
    float fval = val.toFloat(&ok);
    if(ok){
        Undo::SinglePushWorldObjData(worldObj);
        pobj->setAnimLength(fval);
    }
}

void PropertiesPickup::chInfiniteEnabled(int val){
    if (pobj == NULL) {
        return;
    }
    Undo::SinglePushWorldObjData(worldObj);
    if(val == 2){
        pobj->setInfinite(true);
    } else {
        pobj->setInfinite(false);
    }
}

void PropertiesPickup::chBrokenEnabled(int val){
    if (pobj == NULL) {
        return;
    }
    Undo::SinglePushWorldObjData(worldObj);
    if(val == 2){
        pobj->setBroken(true);
    } else {
        pobj->setBroken(false);
    }
}

bool PropertiesPickup::support(GameObj* obj) {
    if (obj == NULL)
        return false;
    if(obj->typeObj != GameObj::worldobj)
        return false;
    if(((WorldObj*)obj)->type == "pickup")
        return true;
    return false;
}