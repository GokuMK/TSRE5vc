/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PropertiesTransfer.h"
#include <tsre/world/objects/WorldObj.h>
#include <tsre/texture/TexLib.h>
#include <tsre/world/objects/TransferObj.h>
#include <routeEditor/properties/EditFileNameDialog.h>
#include <tsre/Game.h>

PropertiesTransfer::PropertiesTransfer() {
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    infoLabel = new QLabel(
        //% "Transfer:"
        qtTrId("route.editor.properties.transfer.label.info.label"));
    infoLabel->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    infoLabel->setContentsMargins(3,0,0,0);
    
    texPreview = new QPixmap(150,150);
    texPreview->fill(Qt::gray);
    texPreviewLabel = new QLabel("");
    texPreviewLabel->setContentsMargins(0,0,0,0);
    texPreviewLabel->setPixmap(*texPreview);
    
    vbox->addWidget(infoLabel);
    QFormLayout *vlistt = new QFormLayout;
    vlistt->setSpacing(2);
    vlistt->setContentsMargins(3,0,3,0);
    this->tX.setDisabled(true);
    this->tY.setDisabled(true);
    vlistt->addRow(
        //% "Tile X:"
        qtTrId("route.editor.properties.transfer.label.tile.x"),&this->tX);
    vlistt->addRow(
        //% "Tile Z:"
        qtTrId("route.editor.properties.transfer.label.tile.z"),&this->tY);
    vbox->addItem(vlistt);
    
    QLabel * label0 = new QLabel(
        //% "Texture:"
        qtTrId("route.editor.properties.transfer.label.label0"));
    label0->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label0->setContentsMargins(3,0,0,0);
    vbox->addWidget(label0);
    QLabel * label1 = new QLabel(
        //% "FileName:"
        qtTrId("route.editor.properties.transfer.label.label1"));
    label1->setContentsMargins(3,0,0,0);
    vbox->addWidget(label1);
    this->fileName.setDisabled(true);
    this->fileName.setAlignment(Qt::AlignCenter);
    vbox->addWidget(&this->fileName);
    QPushButton *copyF = new QPushButton(
        //% "Copy"
        qtTrId("route.editor.properties.transfer.button.copy.f"), this);
    QObject::connect(copyF, SIGNAL(released()),
                      this, SLOT(copyFileNameEnabled()));
    QPushButton *editF = new QPushButton(
        //% "Edit"
        qtTrId("route.editor.properties.transfer.button.edit.f"), this);
    QObject::connect(editF, SIGNAL(released()),
                      this, SLOT(editFileNameEnabled()));
    QGridLayout *filenameList = new QGridLayout;
    filenameList->setSpacing(2);
    filenameList->setContentsMargins(0,0,0,0);
    filenameList->addWidget(copyF, 0, 0);
    filenameList->addWidget(editF, 0, 1);
    vbox->addItem(filenameList);
    vbox->addWidget(texPreviewLabel);
    vbox->setAlignment(texPreviewLabel, Qt::AlignHCenter);
    QPushButton *texLoad = new QPushButton(
        //% "Load Texture"
        qtTrId("route.editor.properties.transfer.button.tex.load"), this);
    QPushButton *texPick = new QPushButton(
        //% "Pick This"
        qtTrId("route.editor.properties.transfer.button.tex.pick"), this);
    QPushButton *texPut = new QPushButton(
        //% "Put Here"
        qtTrId("route.editor.properties.transfer.button.tex.put"), this);
    texLoad->setDisabled(true);
    texPick->setDisabled(true);
    texPut->setDisabled(true);
    vbox->addWidget(texLoad);
    vbox->addWidget(texPick);
    vbox->addWidget(texPut);
    
    QLabel * label = new QLabel(
        //% "Size:"
        qtTrId("route.editor.properties.transfer.label.label"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    QFormLayout *vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    vlist->addRow(
        //% "Width:"
        qtTrId("route.editor.properties.transfer.label.width"),&this->sizeX);
    vlist->addRow(
        //% "Height:"
        qtTrId("route.editor.properties.transfer.label.height"),&this->sizeY);
    vbox->addItem(vlist);
    QDoubleValidator* doubleValidator = new QDoubleValidator(0, 999, 2, this); 
    doubleValidator->setNotation(QDoubleValidator::StandardNotation);
    sizeX.setValidator(doubleValidator);
    QObject::connect(&sizeX, SIGNAL(textEdited(QString)),
                      this, SLOT(sizeEnabled(QString)));
    sizeY.setValidator(doubleValidator);
    QObject::connect(&sizeY, SIGNAL(textEdited(QString)),
                      this, SLOT(sizeEnabled(QString)));
    
    label = new QLabel(
        //% "Position & Rotation:"
        qtTrId("route.editor.properties.transfer.label.label.2"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    vlist->addRow(
        //% "X:"
        qtTrId("route.editor.properties.transfer.label.x"),&this->posX);
    vlist->addRow(
        //% "Y:"
        qtTrId("route.editor.properties.transfer.label.y"),&this->posY);
    vlist->addRow(
        //% "Z:"
        qtTrId("route.editor.properties.transfer.label.z"),&this->posZ);
    this->quat.setDisabled(true);
    this->quat.setAlignment(Qt::AlignCenter);
    vlist->addRow(
        //% "Rot:"
        qtTrId("route.editor.properties.transfer.label.rot"),&this->quat);
    vbox->addItem(vlist);
    QGridLayout *posRotList = new QGridLayout;
    posRotList->setSpacing(2);
    posRotList->setContentsMargins(0,0,0,0);    

    QPushButton *copyPos = new QPushButton(
        //% "Copy Pos"
        qtTrId("route.editor.properties.transfer.button.copy.pos"), this);
    QObject::connect(copyPos, SIGNAL(released()),
                      this, SLOT(copyPEnabled()));
    QPushButton *pastePos = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.transfer.button.paste.pos"), this);
    QObject::connect(pastePos, SIGNAL(released()),
                      this, SLOT(pastePEnabled()));
    QPushButton *copyQrot = new QPushButton(
        //% "Copy Rot"
        qtTrId("route.editor.properties.transfer.button.copy.qrot"), this);
    QObject::connect(copyQrot, SIGNAL(released()),
                      this, SLOT(copyREnabled()));
    QPushButton *pasteQrot = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.transfer.button.paste.qrot"), this);
    QObject::connect(pasteQrot, SIGNAL(released()),
                      this, SLOT(pasteREnabled()));
    QPushButton *copyPosRot = new QPushButton(
        //% "Copy Pos+Rot"
        qtTrId("route.editor.properties.transfer.button.copy.pos.rot"), this);
    QObject::connect(copyPosRot, SIGNAL(released()),
                      this, SLOT(copyPREnabled()));
    QPushButton *pastePosRot = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.transfer.button.paste.pos.rot"), this);
    QObject::connect(pastePosRot, SIGNAL(released()),
                      this, SLOT(pastePREnabled()));
    QPushButton *resetQrot = new QPushButton(
        //% "Reset Rot"
        qtTrId("route.editor.properties.transfer.button.reset.qrot"), this);
    QObject::connect(resetQrot, SIGNAL(released()),
                      this, SLOT(resetRotEnabled()));
    QPushButton *qRot90 = new QPushButton(
        //% "Rot Y 90°"
        qtTrId("route.editor.properties.transfer.button.q.rot90"), this);
    QObject::connect(qRot90, SIGNAL(released()),
                      this, SLOT(rotYEnabled()));
    QPushButton *transform = new QPushButton(
        //% "Transform ..."
        qtTrId("route.editor.properties.transfer.button.transform"), this);
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
        
    vbox->addStretch(1);
    this->setLayout(vbox);
}

PropertiesTransfer::~PropertiesTransfer() {
}

void PropertiesTransfer::showObj(GameObj* obj){
    if(obj == NULL){
        infoLabel->setText(
            //% "NULL"
            qtTrId("route.editor.properties.transfer.text.null"));
        return;
    }
    worldObj = (WorldObj*)obj;
    transferObj = (TransferObj*)obj;

    TransferObj* tobj = (TransferObj*)obj;
        
    //% "Object: %1"
    this->infoLabel->setText(qtTrId("route.properties.transfer.object.type")
                             .arg(transferObj->type));
    this->fileName.setText(tobj->texture);
        
    this->tX.setText(QString::number(transferObj->x, 10));
    this->tY.setText(QString::number(-transferObj->y, 10));
    this->sizeX.setText(QString::number(tobj->width, 'G', 4));
    this->sizeY.setText(QString::number(tobj->height, 'G', 4));
    this->posX.setText(QString::number(transferObj->position[0], 'G', 6));
    this->posY.setText(QString::number(transferObj->position[1], 'G', 6));
    this->posZ.setText(QString::number(-transferObj->position[2], 'G', 6));
    this->quat.setText(
            QString::number(transferObj->qDirection[0], 'G', 4) + " " +
            QString::number(transferObj->qDirection[1], 'G', 4) + " " +
            QString::number(-transferObj->qDirection[2], 'G', 4) + " " +
            QString::number(transferObj->qDirection[3], 'G', 4)
            );
    

    Texture* tex = TexLib::mtex[tobj->getTexId()];
    if(tex == NULL) 
        return;
    
    unsigned char * out = tex->getImageData(128,128);
    if(tex->bytesPerPixel == 3)
        texPreviewLabel->setPixmap(QPixmap::fromImage(QImage(out,128,128,QImage::Format_RGB888)));
    if(tex->bytesPerPixel == 4)
        texPreviewLabel->setPixmap(QPixmap::fromImage(QImage(out,128,128,QImage::Format_RGBA8888)));
}

void PropertiesTransfer::sizeEnabled(QString val){
    if(transferObj == NULL)
        return;
    bool ok;
    sizeX.text().toFloat(&ok);
    if(!ok) return;
    if(sizeX.text().toFloat() <= 0) return;
    sizeY.text().toFloat(&ok);
    if(!ok) return;
    if(sizeY.text().toFloat() <= 0) return;
    Undo::SinglePushWorldObjData(worldObj);
    transferObj->set("width", sizeX.text().toFloat());
    transferObj->set("height", sizeY.text().toFloat());
    transferObj->modified = true;
    transferObj->deleteVBO();
}

void PropertiesTransfer::editFileNameEnabled(){
    if(transferObj == NULL)
        return;
    EditFileNameDialog eWindow;
    eWindow.name.setText(transferObj->texture);
    eWindow.exec();
    if(eWindow.isOk){
        Undo::SinglePushWorldObjData(worldObj);
        transferObj->set("filename", eWindow.name.text());
        transferObj->modified = true;
        fileName.setText(transferObj->texture);
    }
}

bool PropertiesTransfer::support(GameObj* obj){
    if(obj == NULL)
        return false;
    if(obj->typeObj != GameObj::worldobj)
        return false;
    if(((WorldObj*)obj)->type == "transfer")
        return true;
    return false;
}
