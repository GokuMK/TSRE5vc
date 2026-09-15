/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PropertiesTrackObj.h"
#include <tsre/world/objects/WorldObj.h>
#include <tsre/world/objects/TrackObj.h>
#include <math.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/fileFunctions/ParserX.h>
#include <routeEditor/properties/EditFileNameDialog.h>
#include <tsre/Undo.h>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include <tsre/world/Route.h>
#include <tsre/procedural/ProceduralShape.h>
#include <tsre/procedural/ShapeTemplates.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <QSignalBlocker>

PropertiesTrackObj::PropertiesTrackObj(){
    QLabel *label;
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    infoLabel = new QLabel(
        //% "TrackObj:"
        qtTrId("route.editor.properties.track.obj.label.info.label"));
    infoLabel->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    infoLabel->setContentsMargins(3,0,0,0);
    vbox->addWidget(infoLabel);
    QFormLayout *vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    this->uid.setDisabled(true);
    this->tX.setDisabled(true);
    this->tY.setDisabled(true);
    this->eSectionIdx.setDisabled(true);
    vlist->addRow(
        //% "UiD:"
        qtTrId("route.editor.properties.track.obj.label.ui.d"),&this->uid);
    vlist->addRow(
        //% "Tile X:"
        qtTrId("route.editor.properties.track.obj.label.tile.x"),&this->tX);
    vlist->addRow(
        //% "Tile Z:"
        qtTrId("route.editor.properties.track.obj.label.tile.z"),&this->tY);
    vlist->addRow(
        //% "Id:"
        qtTrId("route.editor.properties.track.obj.label.id"),&this->eSectionIdx);
    vlist->addRow(
        //% "Name:"
        qtTrId("route.editor.properties.track.obj.label.name"),&this->fileName);
    vbox->addItem(vlist);
    this->fileName.setDisabled(true);
    this->fileName.setAlignment(Qt::AlignCenter);
    QGridLayout *filenameList = new QGridLayout;
    filenameList->setSpacing(2);
    filenameList->setContentsMargins(0,0,0,0);    
    QPushButton *copyF = new QPushButton(
        //% "Copy Name"
        qtTrId("route.editor.properties.track.obj.button.copy.f"), this);
    QObject::connect(copyF, SIGNAL(released()),
                      this, SLOT(copyFileNameEnabled()));
    QPushButton *editF = new QPushButton(
        //% "Edit"
        qtTrId("route.editor.properties.track.obj.button.edit.f"), this);
    QObject::connect(editF, SIGNAL(released()),
                      this, SLOT(editFileNameEnabled()));
    filenameList->addWidget(copyF, 0, 0);
    filenameList->addWidget(editF, 0, 1);
    vbox->addItem(filenameList);
    
    label = new QLabel(
        //% "Shape Template:"
        qtTrId("route.editor.properties.track.obj.label.label"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vbox->addWidget(&eTemplate);
    eTemplate.setStyleSheet("combobox-popup: 0;");
    eTemplate.addItem(
        //% "NOT SET"
        qtTrId("common.value.not.set"), QString());
    eTemplate.addItem(
        //% "DEFAULT"
        qtTrId("common.value.default"), "DEFAULT");
    eTemplate.addItem(
        //% "DISABLED"
        qtTrId("common.value.disabled"), "DISABLED");
    eTemplate.setToolTip(
        //% "NOT SET uses the static shape in Enabled mode; DEFAULT explicitly requests the default procedural template."
        qtTrId("route.editor.properties.track.obj.tooltip.not.set.uses.static.shape.in.enabled"));
    refreshTemplateList();
    QObject::connect(&eTemplate, SIGNAL(currentTextChanged(QString)),
                      this, SLOT(eTemplateEdited(QString)));
    
    label = new QLabel(
        //% "Position & Rotation:"
        qtTrId("route.editor.properties.track.obj.label.label.2"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    vlist->addRow(
        //% "X:"
        qtTrId("route.editor.properties.track.obj.label.x"),&this->posX);
    vlist->addRow(
        //% "Y:"
        qtTrId("route.editor.properties.track.obj.label.y"),&this->posY);
    vlist->addRow(
        //% "Z:"
        qtTrId("route.editor.properties.track.obj.label.z"),&this->posZ);
    this->quat.setDisabled(true);
    this->quat.setAlignment(Qt::AlignCenter);
    vlist->addRow(
        //% "Rot:"
        qtTrId("route.editor.properties.track.obj.label.rot"),&this->quat);
    vbox->addItem(vlist);
    QGridLayout *posRotList = new QGridLayout;
    posRotList->setSpacing(2);
    posRotList->setContentsMargins(0,0,0,0);    

    QPushButton *copyPos = new QPushButton(
        //% "Copy Pos"
        qtTrId("route.editor.properties.track.obj.button.copy.pos"), this);
    QObject::connect(copyPos, SIGNAL(released()),
                      this, SLOT(copyPEnabled()));
    QPushButton *pastePos = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.track.obj.button.paste.pos"), this);
    QObject::connect(pastePos, SIGNAL(released()),
                      this, SLOT(pastePEnabled()));
    QPushButton *copyQrot = new QPushButton(
        //% "Copy Rot"
        qtTrId("route.editor.properties.track.obj.button.copy.qrot"), this);
    QObject::connect(copyQrot, SIGNAL(released()),
                      this, SLOT(copyREnabled()));
    QPushButton *pasteQrot = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.track.obj.button.paste.qrot"), this);
    QObject::connect(pasteQrot, SIGNAL(released()),
                      this, SLOT(pasteREnabled()));
    QPushButton *copyPosRot = new QPushButton(
        //% "Copy Pos+Rot"
        qtTrId("route.editor.properties.track.obj.button.copy.pos.rot"), this);
    QObject::connect(copyPosRot, SIGNAL(released()),
                      this, SLOT(copyPREnabled()));
    QPushButton *pastePosRot = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.track.obj.button.paste.pos.rot"), this);
    QObject::connect(pastePosRot, SIGNAL(released()),
                      this, SLOT(pastePREnabled()));
    QPushButton *resetQrot = new QPushButton(
        //% "Reset Rot"
        qtTrId("route.editor.properties.track.obj.button.reset.qrot"), this);
    QObject::connect(resetQrot, SIGNAL(released()),
                      this, SLOT(resetRotEnabled()));
    QPushButton *qRot90 = new QPushButton(
        //% "Rot Y 90°"
        qtTrId("route.editor.properties.track.obj.button.q.rot90"), this);
    QObject::connect(qRot90, SIGNAL(released()),
                      this, SLOT(rotYEnabled()));
    QPushButton *transform = new QPushButton(
        //% "Transform ..."
        qtTrId("route.editor.properties.track.obj.button.transform"), this);
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
        qtTrId("route.editor.properties.track.obj.label.label.3"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    this->defaultDetailLevel.setDisabled(true);
    this->defaultDetailLevel.setAlignment(Qt::AlignCenter);
    this->enableCustomDetailLevel.setText(
        //% "Custom"
        qtTrId("route.editor.properties.track.obj.text.custom"));
    QCheckBox* defaultDetailLevelLabel = new QCheckBox(
        //% "Default"
        qtTrId("route.editor.properties.track.obj.option.default.detail.level.label"), this);
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
        qtTrId("route.editor.properties.track.obj.label.label.4"));
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
        qtTrId("route.editor.properties.track.obj.button.copy.flags"), this);
    QObject::connect(copyFlags, SIGNAL(released()),
                      this, SLOT(copyFEnabled()));
    QPushButton *pasteFlags = new QPushButton(
        //% "Paste"
        qtTrId("route.editor.properties.track.obj.button.paste.flags"), this);
    QObject::connect(pasteFlags, SIGNAL(released()),
                      this, SLOT(pasteFEnabled()));
    flagslView->addWidget(copyFlags,0,0);
    flagslView->addWidget(pasteFlags,0,1);
    vbox->addItem(flagslView);
    
    label = new QLabel(
        //% "Elevation:"
        qtTrId("route.editor.properties.track.obj.label.label.5"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vlist = new QFormLayout;
    vlist->setSpacing(0);
    vlist->setContentsMargins(0,0,0,0);
    QDoubleValidator* doubleValidator = new QDoubleValidator(-10000, 10000, 6, this); 
    doubleValidator->setNotation(QDoubleValidator::StandardNotation);
    QDoubleValidator* doubleValidator1 = new QDoubleValidator(-1000, 1000, 6, this); 
    doubleValidator1->setNotation(QDoubleValidator::StandardNotation);
    
    //‰
    vlist->addRow(
        //% "Value: "
        qtTrId("route.editor.properties.track.obj.label.value"),&this->elevType);
    elevType.addItem(
        //% "Permille ‰"
        qtTrId("route.editor.properties.track.obj.item.permille"), "permille");
    elevType.addItem(
        //% "Percent %"
        qtTrId("route.editor.properties.track.obj.item.percent"), "percent");
    elevType.addItem(
        //% "1 in 'X' m"
        qtTrId("route.editor.properties.track.obj.item.1.in.x.m"), "oneInX");
    elevType.addItem(
        //% "Angle º"
        qtTrId("route.editor.properties.track.obj.item.angle"), "angle");
    elevType.setStyleSheet("combobox-popup: 0;");
    QObject::connect(&elevType, SIGNAL(currentTextChanged(QString)),
                      this, SLOT(elevTypeEdited(QString)));
    
    elevPromLabel.setText("‰");
    vlist->addRow(&elevPromLabel,&elevProm);
    elevProm.setValidator(doubleValidator1);
    QObject::connect(&elevProm, SIGNAL(textEdited(QString)), this, SLOT(elevPromEnabled(QString)));
    //oneInXm
    elev1inXmLabel.setText(
        //% "1 in 'x' m"
        qtTrId("route.editor.properties.track.obj.text.1.in.x.m"));
    vlist->addRow(&elev1inXmLabel,&elev1inXm);
    elev1inXm.setValidator(doubleValidator);
    QObject::connect(&elev1inXm, SIGNAL(textEdited(QString)), this, SLOT(elev1inXmEnabled(QString)));
    //º
    elevProgLabel.setText(
        //% "º"
        qtTrId("route.editor.properties.track.obj.text.value"));
    vlist->addRow(&elevProgLabel,&elevProg);
    elevProg.setValidator(doubleValidator1);
    QObject::connect(&elevProg, SIGNAL(textEdited(QString)), this, SLOT(elevProgEnabled(QString)));
    //%
    elevPropLabel.setText("%");
    vlist->addRow(&elevPropLabel,&elevProp);
    elevProp.setValidator(doubleValidator1);
    QObject::connect(&elevProp, SIGNAL(textEdited(QString)), this, SLOT(elevPropEnabled(QString)));
    vlist->addRow(
        //% "Step:"
        qtTrId("route.editor.properties.track.obj.label.step"),&elevStep);
    elevStep.setValidator(doubleValidator);
    QObject::connect(&elevStep, SIGNAL(textEdited(QString)), this, SLOT(elevStepEnabled(QString)));
    hideElevBoxes();
    elevType.setCurrentIndex(Settings::enumIndex("core.track.defaultGradeFormat"));
    ElevTypeName = elevType.currentData().toString();
    showElevBox(ElevTypeName);
    vbox->addItem(vlist);
    
    
    label = new QLabel(
        //% "MSTS Collision:"
        qtTrId("route.editor.properties.track.obj.label.label.6"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vbox->addWidget(&eCollisionFlags);
    eCollisionFlags.setDisabled(true);
    eCollisionFlags.setAlignment(Qt::AlignCenter);
    cCollisionType.addItem(
        //% "Disabled"
        qtTrId("route.editor.properties.track.obj.item.disabled.2"));
    cCollisionType.addItem(
        //% "Immovable"
        qtTrId("route.editor.properties.track.obj.item.immovable"));
    cCollisionType.addItem(
        //% "Buffer"
        qtTrId("route.editor.properties.track.obj.item.buffer"));
    cCollisionType.setStyleSheet("combobox-popup: 0;");
    vbox->addWidget(&cCollisionType);
    QObject::connect(&cCollisionType, SIGNAL(currentIndexChanged(int)),
                      this, SLOT(cCollisionTypeEdited(int)));
    //QPushButton *resetFlags = new QPushButton("Reset Flags", this);
    //QObject::connect(resetFlags, SIGNAL(released()),
    //                  this, SLOT(copyFEnabled()));
    //vbox->addWidget(resetFlags);
    label = new QLabel(
        //% "Advanced:"
        qtTrId("route.editor.properties.track.obj.label.label.7"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    
    QPushButton *hacks = new QPushButton(
        //% "Hacks"
        qtTrId("route.editor.properties.track.obj.button.hacks"), this);
    QObject::connect(hacks, SIGNAL(released()),
                      this, SLOT(hacksButtonEnabled()));
    vbox->addWidget(hacks);
    
    vbox->addStretch(1);
    this->setLayout(vbox);
    
    QObject::connect(copyF, SIGNAL(released()),
                      this, SLOT(copyFileNameEnabled()));
}

void PropertiesTrackObj::eTemplateEdited(QString val){
    if(trackObj == NULL){
        return;
    }
    Q_UNUSED(val);
    val = eTemplate.currentData().toString();
    Undo::SinglePushWorldObjData(worldObj);
    trackObj->setTemplate(val);
    Undo::StateEnd();
}

void PropertiesTrackObj::elevTypeEdited(QString val){
    Q_UNUSED(val);
    val = elevType.currentData().toString();
    hideElevBoxes();
    showElevBox(val);
    ElevTypeName = val;
}

void PropertiesTrackObj::showElevBox(QString val){
    if(val == "permille"){
        elevProm.show();
        elevPromLabel.show();
    }
    if(val == "percent"){
        elevProp.show();
        elevPropLabel.show();
    }
    if(val == "oneInX"){
        elev1inXm.show();
        elev1inXmLabel.show();
    }
    if(val == "angle"){
        elevProg.show();
        elevProgLabel.show();
    }    
    setStepValue(Game::DefaultMoveStep);
}

void PropertiesTrackObj::hideElevBoxes(){
    elevProm.hide();
    elevProg.hide();
    elevProp.hide();
    elev1inXm.hide();
    elevPromLabel.hide();
    elevProgLabel.hide();
    elevPropLabel.hide();
    elev1inXmLabel.hide();
}

PropertiesTrackObj::~PropertiesTrackObj() {
}

void PropertiesTrackObj::fixJNodePosnEnabled(){
    if(trackObj == NULL){
        return;
    }
    Undo::SinglePushWorldObjData(worldObj);
    trackObj->fillJNodePosn();
    Undo::StateEnd();
}

void PropertiesTrackObj::hacksButtonEnabled(){
    if(trackObj == NULL){
        return;
    }
    
    QDialog d;
    d.setMinimumWidth(400);
    d.setWindowTitle(
        //% "TrackObj Hacks"
        qtTrId("route.editor.properties.track.obj.title.track.obj.hacks"));
    QVBoxLayout *vbox = new QVBoxLayout;
    QLabel *label = new QLabel(
        //% "These functions will damage your route if you don't know what you are doing."
        qtTrId("route.editor.properties.track.obj.label.label.8"));
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    label->setWordWrap(true);
    
    QPushButton *fixJNodePosn = new QPushButton(
        //% "Fix JNodePosn"
        qtTrId("route.editor.properties.track.obj.button.fix.jnode.posn"), this);
    QObject::connect(fixJNodePosn, SIGNAL(released()),
                      this, SLOT(fixJNodePosnEnabled()));
    vbox->addWidget(fixJNodePosn);
    
    QPushButton *haxRemoveTDBVector = new QPushButton(
        //% "Remove TDB Vector ( remove TrItems first )"
        qtTrId("route.editor.properties.track.obj.button.hax.remove.tdbvector"), this);
    QObject::connect(haxRemoveTDBVector, SIGNAL(released()),
                      this, SLOT(haxRemoveTDBVectorEnabled()));
    vbox->addWidget(haxRemoveTDBVector);
    
    QPushButton *haxRemoveTDBTree = new QPushButton(
        //% "Remove TDB Tree ( remove TrItems first; max 1000 nodes )"
        qtTrId("route.editor.properties.track.obj.button.hax.remove.tdbtree"), this);
    QObject::connect(haxRemoveTDBTree, SIGNAL(released()),
                      this, SLOT(haxRemoveTDBTreeEnabled()));
    vbox->addWidget(haxRemoveTDBTree);
    
    QPushButton *haxElevTDBVector = new QPushButton(
        //% "[Fix sElev] Don't click me!"
        qtTrId("route.editor.properties.track.obj.button.hax.elev.tdbvector"), this);
    QObject::connect(haxElevTDBVector, SIGNAL(released()),
                      this, SLOT(haxElevTDBVectorEnabled()));
    vbox->addWidget(haxElevTDBVector);

    vbox->setSpacing(2);
    vbox->setContentsMargins(3,3,3,3);
    vbox->addStretch(1);
    d.setLayout(vbox);
    d.exec();
}

void PropertiesTrackObj::haxElevTDBVectorEnabled(){
    if(trackObj == NULL)
        return;
    if(Game::currentRoute == NULL)
        return;
    Game::currentRoute->fixTDBVectorElevation(trackObj);
}

void PropertiesTrackObj::haxRemoveTDBVectorEnabled(){
    if(trackObj == NULL)
        return;
    if(Game::currentRoute == NULL)
        return;
    Game::currentRoute->deleteTDBVector(trackObj);
}

void PropertiesTrackObj::haxRemoveTDBTreeEnabled(){
    if(trackObj == NULL)
        return;
    if(Game::currentRoute == NULL)
        return;
    
    Game::currentRoute->deleteTDBTree(trackObj);
}

void PropertiesTrackObj::showObj(GameObj* obj){
    if(obj == NULL){
        infoLabel->setText(
            //% "NULL"
            qtTrId("route.editor.properties.track.obj.text.null"));
        return;
    }
    worldObj = (WorldObj*)obj;
    trackObj = (TrackObj*) obj;
    refreshTemplateList();
    
    //% "Object: %1"
    this->infoLabel->setText(qtTrId("route.properties.track.object.type")
                             .arg(trackObj->type));
    this->fileName.setText(trackObj->fileName);
    
    this->uid.setText(QString::number(trackObj->UiD, 10));
    this->tX.setText(QString::number(trackObj->x, 10));
    this->tY.setText(QString::number(-trackObj->y, 10));
    this->eSectionIdx.setText(QString::number(trackObj->sectionIdx, 10));
    this->posX.setText(QString::number(trackObj->position[0], 'G', 6));
    this->posY.setText(QString::number(trackObj->position[1], 'G', 6));
    this->posZ.setText(QString::number(-trackObj->position[2], 'G', 6));
    this->quat.setText(
            QString::number(trackObj->qDirection[0], 'G', 4) + " " +
            QString::number(trackObj->qDirection[1], 'G', 4) + " " +
            QString::number(-trackObj->qDirection[2], 'G', 4) + " " +
            QString::number(trackObj->qDirection[3], 'G', 4)
            );
    
    defaultDetailLevel.setText(QString::number(trackObj->getDefaultDetailLevel()));
    enableCustomDetailLevel.blockSignals(true);
    if(trackObj->customDetailLevelEnabled()){
        enableCustomDetailLevel.setChecked(true);
        customDetailLevel.setText(QString::number(trackObj->getCustomDetailLevel()));
        customDetailLevel.setEnabled(true);
    } else {
        enableCustomDetailLevel.setChecked(false);
        customDetailLevel.setText("");
        customDetailLevel.setEnabled(false);
    }
    enableCustomDetailLevel.blockSignals(false);
    
    this->flags.setText(ParserX::MakeFlagsString(trackObj->staticFlags));
    
    ///////////
    elevType.setCurrentIndex(qMax(0, elevType.findData(ElevTypeName)));
    
    TrackObj* track = (TrackObj*)obj;
    float * q = track->qDirection;
    float vect[3];
    vect[0] = 0; vect[1] = 0; vect [2] = 1000.0;
    Vec3::transformQuat(vect, vect, q);
    vect[1] = -vect[1];
     
    float oneInXm = 0.0;
    float prog = qRadiansToDegrees(qAtan(vect[1]/1000.0));
    float prop = vect[1]/10.0;

    //if(vect[1] > 0)
        oneInXm = 1000.0/vect[1];
    this->elevProm.setText(QString::number(vect[1]));
    this->elevProg.setText(QString::number(prog));
    this->elevProp.setText(QString::number(prop));
    this->elev1inXm.setText(QString::number(oneInXm));
    setStepValue(Game::DefaultMoveStep);
    
    /*float pitch = asin(2*(q[0]*q[2] - q[1]*q[3]));
    
    if(vect[2] < 0)
        pitch = M_PI - pitch;
    if(vect[2] == 0 && vect[0] < 0)
        pitch = M_PI/2;
    if(vect[2] == 0 && vect[0] > 0)
        pitch = -M_PI/2;*/

    //float elev = tan((vect[1]/10.0));
    //qe[1] = pitch;
    //qe[2] = 0;
    eCollisionFlags.setText(QString::number(worldObj->getCollisionFlags()));
    int collisionType = worldObj->getCollisionType();
    this->cCollisionType.blockSignals(true);
    this->cCollisionType.setCurrentIndex(collisionType);
    this->cCollisionType.blockSignals(false);
    
    updateTemplateValue();
}

void PropertiesTrackObj::refreshTemplateList(){
    const QSignalBlocker blocker(&eTemplate);
    const QString previousValue = eTemplate.currentData().toString();

    eTemplate.clear();
    eTemplate.addItem(
        //% "NOT SET"
        qtTrId("common.value.not.set"), QString());
    eTemplate.addItem(
        //% "DEFAULT"
        qtTrId("common.value.default"), "DEFAULT");
    eTemplate.addItem(
        //% "DISABLED"
        qtTrId("common.value.disabled"), "DISABLED");

    ProceduralShape::Load();
    OrtsTrackProfileCatalog::load(Game::root + "/ROUTES/" + Game::route);

    // Route-local ORTS profiles override application-level TSRE templates and
    // are shown first so the most relevant choices are easiest to find.
    for(const QString &profileId : OrtsTrackProfileCatalog::profileIds())
        if(eTemplate.findData(profileId) < 0)
            eTemplate.addItem(profileId, profileId);

    if(ProceduralShape::ShapeTemplateFile != NULL){
        QMapIterator<QString, ShapeTemplate*> iterator(
                ProceduralShape::ShapeTemplateFile->templates);
        while(iterator.hasNext()){
            iterator.next();
            if(iterator.value() == NULL)
                continue;
            const QString name = iterator.value()->name;
            if(OrtsTrackProfileCatalog::find(name) != nullptr)
                continue;
            if(eTemplate.findData(name) < 0)
                eTemplate.addItem(name, name);
        }
    }

    if(!previousValue.isEmpty()
            && eTemplate.findData(previousValue) < 0)
        eTemplate.addItem(previousValue, previousValue);
    if(!previousValue.isEmpty())
        eTemplate.setCurrentIndex(eTemplate.findData(previousValue));
}

void PropertiesTrackObj::updateTemplateValue(){
    if(trackObj == NULL)
        return;
    QString name = trackObj->getTemplate();
    const QSignalBlocker blocker(&eTemplate);
    if(eTemplate.findData(name) < 0)
        eTemplate.addItem(name, name);
    eTemplate.setCurrentIndex(eTemplate.findData(name));
}

void PropertiesTrackObj::setStepValue(float step){
    if(elevType.currentIndex() == 0)
        step = step * 100;
    if(elevType.currentIndex() == 1)
        step = step * 10;
    if(elevType.currentIndex() == 2)
        step = 10.0/step;
    if(elevType.currentIndex() == 3)
        step = qRadiansToDegrees(qAtan(step/10.0));
    
    elevStep.setText(QString::number(step));
}

float PropertiesTrackObj::getStepValue(float step){
    if(elevType.currentIndex() == 0)
        return step / 100;
    if(elevType.currentIndex() == 1)
        return step / 10;
    if(elevType.currentIndex() == 2)
        return 10.0/step;
    if(elevType.currentIndex() == 3)
        return qTan(qDegreesToRadians(step))*10.0;
    return 0;
}

void PropertiesTrackObj::updateObj(GameObj* obj){
    if(obj == NULL){
        return;
    }
    TrackObj* track = (TrackObj*)obj;
    float * q = track->qDirection;
     float vect[3];
    vect[0] = 0; vect[1] = 0; vect [2] = 1000.0;
    Vec3::transformQuat(vect, vect, q);
    vect[1] = -vect[1];
     
    float oneInXm = 0.0;
    oneInXm = 1000.0/vect[1];
    float prog = qRadiansToDegrees(qAtan(vect[1]/1000.0));
    float prop = vect[1]/10.0;
       
    if(!posX.hasFocus() && !posY.hasFocus() && !posZ.hasFocus() && !quat.hasFocus()){
        this->uid.setText(QString::number(trackObj->UiD, 10));
        this->tX.setText(QString::number(trackObj->x, 10));
        this->tY.setText(QString::number(-trackObj->y, 10));
        this->posX.setText(QString::number(trackObj->position[0], 'G', 6));
        this->posY.setText(QString::number(trackObj->position[1], 'G', 6));
        this->posZ.setText(QString::number(-trackObj->position[2], 'G', 6));
        this->quat.setText(
                QString::number(trackObj->qDirection[0], 'G', 4) + " " +
                QString::number(trackObj->qDirection[1], 'G', 4) + " " +
                QString::number(-trackObj->qDirection[2], 'G', 4) + " " +
                QString::number(trackObj->qDirection[3], 'G', 4)
                );
    }
    if(!this->elevProm.hasFocus() && !this->elev1inXm.hasFocus() && !this->elevProg.hasFocus() && !this->elevProp.hasFocus()){
        this->elevProm.setText(QString::number(vect[1]));
        this->elevProg.setText(QString::number(prog));
        this->elevProp.setText(QString::number(prop));
        this->elev1inXm.setText(QString::number(oneInXm));
    }
    eCollisionFlags.setText(QString::number(worldObj->getCollisionFlags()));
    updateTemplateValue();
}

void PropertiesTrackObj::elevPromEnabled(QString val){
    if(trackObj == NULL){
        return;
    }
    bool ok = false;
    //prom
    float prom = val.toFloat(&ok);
    if(!ok) return;
    if(fabs(prom) > Game::trackElevationMaxPm + 0.000001) {   
        this->elevProm.setText(QString::number(Game::trackElevationMaxPm));
        return;
    }
    //oneInXm
    float oneInXm = 1000.0/prom;
    qDebug () << "oneInXm" << oneInXm;
    qDebug () << "Game::trackElevationMaxPm" << Game::trackElevationMaxPm;
    this->elev1inXm.setText(QString::number(oneInXm));
    //prog 
    float prog = qRadiansToDegrees(qAtan(prom/1000.0));
    qDebug () << "prog" << prog;
    this->elevProg.setText(QString::number(prog));
    //prop 
    float prop = prom/10.0;
    qDebug () << "prop" << prop;
    this->elevProp.setText(QString::number(prop));  
    
    Undo::SinglePushWorldObjData(worldObj);
    trackObj->setElevation(prom);
}

void PropertiesTrackObj::elevStepEnabled(QString val){
    if(trackObj == NULL)
        return;

    bool ok = false;
    float f = val.toFloat(&ok);
    if(!ok)
        return;
    
    f = getStepValue(f);
    
    Game::DefaultMoveStep = f;
    emit setMoveStep(f);
}

void PropertiesTrackObj::elev1inXmEnabled(QString val){
    if(trackObj == NULL){
        return;
    }
    bool ok = false;
    //oneInXm
    float oneInXm = val.toFloat(&ok);
    if(!ok) return;
    //qDebug () << "oneInXm" << oneInXm;
    //prom
    float prom = 1000.0/oneInXm;
    if(fabs(prom) > Game::trackElevationMaxPm + 0.000001) { 
        return;
    }
    
    qDebug () << "Game::trackElevationMaxPm: " << Game::trackElevationMaxPm;
    this->elevProm.setText(QString::number(prom));  
    //prop 
    float prop = prom/10.0;
    qDebug () << "prop" << prop;
    this->elevProp.setText(QString::number(prop));
    //prog 
    float prog = qRadiansToDegrees(qAtan(prom/1000.0));
    qDebug () << "prog" << prog;
    this->elevProg.setText(QString::number(prog));
    
    Undo::SinglePushWorldObjData(worldObj);
    trackObj->setElevation(prom);
}

void PropertiesTrackObj::elevProgEnabled(QString val){
    if(trackObj == NULL){
         return;
    }
    bool ok = false;
    //prog
    float prog = val.toFloat(&ok);
    if(!ok) return;
    if(fabs(prog) > qRadiansToDegrees(qAtan(Game::trackElevationMaxPm/1000.0))+ 0.000001) {   
        this->elevProg.setText(QString::number(qRadiansToDegrees(qAtan(Game::trackElevationMaxPm/1000.0))));
        return;
    }
    //prop 
    float prop = qTan(qDegreesToRadians(prog))*100.0;
    qDebug () << "prop" << prop;
    qDebug () << "prog" << prog;
    this->elevProp.setText(QString::number(prop));
    //prom
    float prom = prop*10.0;
    qDebug () << "prom" << prom;
    this->elevProm.setText(QString::number(prom));
    //oneInXm
    float oneInXm = 1000.0/prom;
    qDebug () << "oneInXm" << oneInXm;
    this->elev1inXm.setText(QString::number(oneInXm));
     
    Undo::SinglePushWorldObjData(worldObj);
    trackObj->setElevation(prom);
}
 
void PropertiesTrackObj::elevPropEnabled(QString val){
    if(trackObj == NULL){
        return;
    }
    bool ok = false;
    //prop
    float prop = val.toFloat(&ok);
    if(!ok) return;
    if(fabs(prop) > (Game::trackElevationMaxPm/10.0)+ 0.000001)
    {    this->elevProp.setText(QString::number(Game::trackElevationMaxPm/10.0));
        return;}
    //prom    
    float prom = prop*10.0;
    qDebug () << "prop" << prop;
    qDebug () << "prom" << prom;
    qDebug () << "Game::trackElevationMaxPm/10.0: " << Game::trackElevationMaxPm/10.0;
    this->elevProm.setText(QString::number(prom));
    //prog 
    float prog = qRadiansToDegrees(qAtan(prom/1000.0));
    qDebug () << "prog" << prog;
    this->elevProg.setText(QString::number(prog));
    //oneInXm
    float oneInXm = 1000.0/prom;
    qDebug () << "oneInXm" << oneInXm;
    this->elev1inXm.setText(QString::number(oneInXm));
    
    Undo::SinglePushWorldObjData(worldObj);
    trackObj->setElevation(prom);
}

bool PropertiesTrackObj::support(GameObj* obj){
    if(obj == NULL)
        return false;
    if(obj->typeObj != GameObj::worldobj)
        return false;
    if(((WorldObj*)obj)->type == "trackobj")
        return true;
    return false;
}

void PropertiesTrackObj::cCollisionTypeEdited(int val){
    if(worldObj == NULL)
        return;
    Undo::SinglePushWorldObjData(worldObj);
    worldObj->setCollisionType(val-1);
}

void PropertiesTrackObj::editFileNameEnabled(){
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

void PropertiesTrackObj::enableCustomDetailLevelEnabled(int val){
    if(worldObj == NULL)
        return;
    TrackObj* tObj = (TrackObj*) worldObj;
    Undo::SinglePushWorldObjData(worldObj);
    if(val == 2){
        customDetailLevel.setEnabled(true);
        customDetailLevel.setText("0");
        tObj->setCustomDetailLevel(0);
    } else {
        customDetailLevel.setEnabled(false);
        customDetailLevel.setText("");
        tObj->setCustomDetailLevel(-1);
    }
}

void PropertiesTrackObj::customDetailLevelEdited(QString val){
    if(worldObj == NULL)
        return;
    TrackObj* tObj = (TrackObj*) worldObj;
    bool ok = false;
    int level = val.toInt(&ok);
    //qDebug() << "aaaaaaaaaa";
    if(ok){
        Undo::SinglePushWorldObjData(worldObj);
        tObj->setCustomDetailLevel(level);
    }
}
