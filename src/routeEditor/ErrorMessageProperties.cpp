/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "ErrorMessageProperties.h"
#include <QDebug>
#include <tsre/Game.h>
#include <tsre/ErrorMessage.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/GameObj.h>

ErrorMessageProperties::ErrorMessageProperties(QWidget* parent) : QWidget(parent) {
    setFixedHeight(140);
    //setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(2,2,2,2);
    
    QLabel *label = new QLabel(
        //% "Selected Message:"
        qtTrId("route.editor.error.message.properties.label.label"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    
    QGridLayout *vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,1,0);    
    vlist->setAlignment(Qt::AlignTop);
    vlist->setColumnStretch(1,1);
    int row = 0;
    
    vlist->addWidget(&lMessage,row,0);
    vlist->addWidget(&eMessage,row,1);
    vlist->addWidget(&bSelect,row++,2);
    //% "Fix"
    bFix.setText(qtTrId("route.errors.fix"));
    bFix.setObjectName("errorMessageFix");
    vlist->addWidget(&bFix, 0, 3);
    bFix.hide();
    connect(&bFix, &QPushButton::released, this, &ErrorMessageProperties::fixReleased);
    vlist->addWidget(&lAction,row,0);
    vlist->addWidget(&eAction,row++,1,1,2);
    vlist->addWidget(&lLocation,row,0);
    vlist->addWidget(&eLocation,row,1);
    vlist->addWidget(&bLocation,row++,2);
    lMessage.setText(
        //% "Message:"
        qtTrId("route.editor.error.message.properties.text.message"));
    lMessage.hide();
    eMessage.hide();
    lAction.hide();
    lAction.setText(
        //% "Description:"
        qtTrId("route.editor.error.message.properties.text.description"));
    lAction.setAlignment(Qt::AlignTop);
    eAction.hide();
    lLocation.setText(
        //% "Location:"
        qtTrId("route.editor.error.message.properties.text.location"));
    lLocation.hide();
    eLocation.hide();
    eLocation.setDisabled(true);
    bLocation.hide();
    bSelect.hide();
    
    vbox->addItem(vlist);
    vbox->addStretch(1);
    QObject::connect(&bLocation, SIGNAL(released()), this, SLOT(jumpToLocation()));
    QObject::connect(&bSelect, SIGNAL(released()), this, SLOT(bSelectReleased()));
    
    this->setLayout(vbox);
}

ErrorMessageProperties::~ErrorMessageProperties() {
}

void ErrorMessageProperties::showMessage(ErrorMessage* msg){
    currentMessage = msg;
    lMessage.hide();
    eMessage.hide();
    lAction.hide();
    eAction.hide();
    lLocation.hide();
    bLocation.hide();
    eLocation.hide();
    bSelect.hide();
    bFix.hide();
    if(currentMessage == NULL){
        return;
    }
    if (msg->fix) {
        bFix.show();
        bFix.setEnabled(msg->canFix && msg->canFix());
    }

    bSelect.show();
    lMessage.show();
    eMessage.show();
    eMessage.setText(currentMessage->description);
    if(currentMessage->action.length() > 0){
        lAction.show();
        eAction.show();
        eAction.setPlainText(currentMessage->action);
    }
    
    if(currentMessage->obj != NULL){
        bSelect.setEnabled(true);
        bSelect.setText(
            //% "Select Object"
            qtTrId("route.editor.error.message.properties.text.select.object"));
    } else {
        bSelect.setDisabled(true);
        bSelect.setText(
            //% "NO OBJECT"
            qtTrId("route.editor.error.message.properties.text.no.object"));
    }
    
    if(currentMessage->coords != NULL){
        lLocation.show();
        eLocation.show();
        //% "Tile: %1 %2. Coordinates: %3 %4 %5"
        eLocation.setText(qtTrId("route.error.location.coordinates")
                          .arg(currentMessage->coords->TileX)
                          .arg(currentMessage->coords->TileZ)
                          .arg(currentMessage->coords->wX)
                          .arg(currentMessage->coords->wY)
                          .arg(currentMessage->coords->wZ));
        bLocation.setText(
            //% "Jump"
            qtTrId("route.editor.error.message.properties.text.jump"));
        bLocation.show();
    }
    
    
}

void ErrorMessageProperties::jumpToLocation(){
    emit jumpTo(currentMessage->coords);
}

void ErrorMessageProperties::bSelectReleased(){
    emit selectObject(currentMessage->obj);
}

void ErrorMessageProperties::fixReleased() {
    if (!currentMessage) return;
    QString result;
    if (!currentMessage->applyFix(result))
        currentMessage->action += "\n" + result;
    showMessage(currentMessage);
    emit messageChanged();
}
