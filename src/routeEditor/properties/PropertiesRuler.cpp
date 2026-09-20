/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PropertiesRuler.h"
#include <tsre/world/objects/RulerObj.h>
#include <tsre/Undo.h>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include <tsre/procedural/ProceduralShape.h>
#include <tsre/procedural/ShapeTemplates.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <QSignalBlocker>

PropertiesRuler::PropertiesRuler() {
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    infoLabel = new QLabel(
        //% "Ruler:"
        qtTrId("route.editor.properties.ruler.label.info.label"));
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
        qtTrId("route.editor.properties.ruler.label.ui.d"),&this->uid);
    vlist->addRow(
        //% "Tile X:"
        qtTrId("route.editor.properties.ruler.label.tile.x"),&this->tX);
    vlist->addRow(
        //% "Tile Z:"
        qtTrId("route.editor.properties.ruler.label.tile.z"),&this->tY);
    vbox->addItem(vlist);
    QLabel *label = new QLabel(
        //% "Game Length:"
        qtTrId("route.editor.properties.ruler.label.label"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);

    vlist->addRow(
        //% "Meters: "
        qtTrId("route.editor.properties.ruler.label.meters"),&this->lengthM);
    vbox->addItem(vlist);
    
    label = new QLabel(
        //% "Geo Length:"
        qtTrId("route.editor.properties.ruler.label.label.2"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    vlist = new QFormLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);

    vlist->addRow(
        //% "Meters: "
        qtTrId("route.editor.properties.ruler.label.meters.2"),&this->lengthGM);
    vbox->addItem(vlist);
    
    
    label = new QLabel(
        //% "Average Elevation:"
        qtTrId("route.editor.properties.ruler.label.label.3"));
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
        qtTrId("route.editor.properties.ruler.label.value"),&this->elevType);
    elevType.addItem(
        //% "Permille ‰"
        qtTrId("route.editor.properties.ruler.item.permille"), "permille");
    elevType.addItem(
        //% "Percent %"
        qtTrId("route.editor.properties.ruler.item.percent"), "percent");
    elevType.addItem(
        //% "1 in 'X' m"
        qtTrId("route.editor.properties.ruler.item.1.in.x.m"), "oneInX");
    elevType.addItem(
        //% "Angle º"
        qtTrId("route.editor.properties.ruler.item.angle"), "angle");
    elevType.setStyleSheet("combobox-popup: 0;");
    QObject::connect(&elevType, SIGNAL(currentTextChanged(QString)),
                      this, SLOT(elevTypeEdited(QString)));
    
    elevPromLabel.setText("‰");
    vlist->addRow(&elevPromLabel,&elevProm);
    //oneInXm
    elev1inXmLabel.setText(
        //% "1 in 'x' m"
        qtTrId("route.editor.properties.ruler.text.1.in.x.m"));
    vlist->addRow(&elev1inXmLabel,&elev1inXm);
    //º
    elevProgLabel.setText(
        //% "º"
        qtTrId("route.editor.properties.ruler.text.value"));
    vlist->addRow(&elevProgLabel,&elevProg);
    //%
    elevPropLabel.setText("%");
    vlist->addRow(&elevPropLabel,&elevProp);
    hideElevBoxes();
    elevType.setCurrentIndex(Settings::enumIndex("core.track.defaultGradeFormat"));
    ElevTypeName = elevType.currentData().toString();
    showElevBox(ElevTypeName);
    vbox->addItem(vlist);
    
    
    label = new QLabel(
        //% "Default Settings:"
        qtTrId("route.editor.properties.ruler.label.label.4"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    checkboxTwoPoint.setText(
        //% "Only Two-Point Ruler"
        qtTrId("route.editor.properties.ruler.text.only.two.point.ruler"));
    checkboxTwoPoint.setChecked(false);
    vbox->addWidget(&checkboxTwoPoint);
    QObject::connect(&checkboxTwoPoint, SIGNAL(stateChanged(int)),
                      this, SLOT(checkboxTwoPointEdited(int)));
    checkboxDrawPoints.setText(
        //% "Render points"
        qtTrId("route.editor.properties.ruler.text.render.points"));
    checkboxDrawPoints.setChecked(false);
    vbox->addWidget(&checkboxDrawPoints);
    QObject::connect(&checkboxDrawPoints, SIGNAL(stateChanged(int)),
                      this, SLOT(checkboxDrawPointsEdited(int)));
    label = new QLabel(
        //% "Experimental:"
        qtTrId("route.editor.properties.ruler.label.label.5"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    vbox->addWidget(label);
    QPushButton *button = new QPushButton(
        //% "Create Road Paths"
        qtTrId("route.editor.properties.ruler.button.button"));
    vbox->addWidget(button);
    QObject::connect(button, SIGNAL(released()),
                      this, SLOT(createRoadPathsEdited()));
    button = new QPushButton(
        //% "Remove Road Paths"
        qtTrId("route.editor.properties.ruler.button.button.2"));
    vbox->addWidget(button);
    QObject::connect(button, SIGNAL(released()),
                      this, SLOT(removeRoadPathsEdited()));
    
    label = new QLabel(
        //% "Shape Template:"
        qtTrId("route.editor.properties.ruler.label.label.6"));
    //label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
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
        //% "NOT SET disables the procedural Ruler shape; DEFAULT explicitly requests the default procedural template."
        qtTrId("route.editor.properties.ruler.tooltip.not.set.disables.procedural.ruler.shape.default"));
    QObject::connect(&eTemplate, SIGNAL(currentTextChanged(QString)),
                      this, SLOT(eTemplateEdited(QString)));
    button = new QPushButton(
        //% "Add Shape"
        qtTrId("route.editor.properties.ruler.button.button.3"));
    vbox->addWidget(button);
    QObject::connect(button, SIGNAL(released()),
                      this, SLOT(addShapeEdited()));
    vbox->addStretch(1);
    this->setLayout(vbox);
}

PropertiesRuler::~PropertiesRuler() {
}

void PropertiesRuler::elevTypeEdited(QString val){
    Q_UNUSED(val);
    val = elevType.currentData().toString();
    hideElevBoxes();
    showElevBox(val);
    ElevTypeName = val;
}

void PropertiesRuler::eTemplateEdited(QString val){
    if(worldObj == NULL){
        return;
    }
    Q_UNUSED(val);
    val = eTemplate.currentData().toString();
    Undo::SinglePushWorldObjData(worldObj);
    worldObj->setTemplate(val);
    Undo::StateEnd();
}

void PropertiesRuler::refreshTemplateList(){
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

    // Route-local ORTS profiles are the most specific definitions, so show
    // them before application-level TSRE templates.
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

void PropertiesRuler::updateTemplateValue(){
    if(worldObj == NULL)
        return;
    QString name = worldObj->getTemplate();
    const QSignalBlocker blocker(&eTemplate);
    if(eTemplate.findData(name) < 0)
        eTemplate.addItem(name, name);
    eTemplate.setCurrentIndex(eTemplate.findData(name));
}

void PropertiesRuler::showObj(GameObj* obj){
    if(obj == NULL){
        infoLabel->setText(
            //% "NULL"
            qtTrId("route.editor.properties.ruler.text.null"));
        return;
    }
    worldObj = (WorldObj*)obj;
    RulerObj* robj = (RulerObj*)obj;
    refreshTemplateList();
    this->uid.setText(QString::number(robj->UiD, 10));
    this->tX.setText(QString::number(robj->x, 10));
    this->tY.setText(QString::number(-robj->y, 10));
    lengthM.setText(QString::number(robj->getLength(), 'G', 4));
    lengthGM.setText(QString::number(robj->getGeoLength(), 'G', 4));
    
    updateTemplateValue();
    
    elevType.setCurrentIndex(qMax(0, elevType.findData(ElevTypeName)));
    float elev = sin(robj->getElevation())*1000;
    float oneInXm = 0.0;
    float prog = qRadiansToDegrees(qAtan(elev/1000.0));
    float prop = elev/10.0;
    oneInXm = 1000.0/elev;
    this->elevProm.setText(QString::number(elev));
    this->elevProg.setText(QString::number(prog));
    this->elevProp.setText(QString::number(prop));
    this->elev1inXm.setText(QString::number(oneInXm));
}

void PropertiesRuler::updateObj(GameObj* obj){
    if(obj == NULL){
        return;
    }
    worldObj = (WorldObj*)obj;
    RulerObj* robj = (RulerObj*)obj;

    if(!lengthM.hasFocus())
        lengthM.setText(QString::number(robj->getLength(), 'G', 4));
    if(!lengthGM.hasFocus())
        lengthGM.setText(QString::number(robj->getGeoLength(), 'G', 4));
    updateTemplateValue();
    
    float elev = sin(robj->getElevation())*1000;
    float oneInXm = 0.0;
    float prog = qRadiansToDegrees(qAtan(elev/1000.0));
    float prop = elev/10.0;
    oneInXm = 1000.0/elev;
    if(!this->elevProm.hasFocus() && !this->elev1inXm.hasFocus() && !this->elevProg.hasFocus() && !this->elevProp.hasFocus()){
        this->elevProm.setText(QString::number(elev));
        this->elevProg.setText(QString::number(prog));
        this->elevProp.setText(QString::number(prop));
        this->elev1inXm.setText(QString::number(oneInXm));
    }
}

void PropertiesRuler::checkboxTwoPointEdited(int val){
    if(worldObj == NULL)
        return;
    RulerObj* robj = (RulerObj*)worldObj;
    if(val == 2){
        robj->TwoPointRuler = true;
    } else {
        robj->TwoPointRuler = false;
    }
}

void PropertiesRuler::checkboxDrawPointsEdited(int val){
    if(worldObj == NULL)
        return;
    RulerObj* robj = (RulerObj*)worldObj;
    if(val == 2){
        robj->DrawPoints = true;
    } else {
        robj->DrawPoints = false;
    }
}

void PropertiesRuler::createRoadPathsEdited(){
    if(worldObj == NULL)
        return;
    RulerObj* robj = (RulerObj*)worldObj;
    //Undo::SinglePushWorldObjData(worldObj);
    robj->createRoadPaths();
}

void PropertiesRuler::addShapeEdited(){
    if(worldObj == NULL)
        return;
    RulerObj* robj = (RulerObj*)worldObj;
    //Undo::SinglePushWorldObjData(worldObj);
    robj->enableShape();
}

void PropertiesRuler::removeRoadPathsEdited(){
    if(worldObj == NULL)
        return;
    RulerObj* robj = (RulerObj*)worldObj;
    //Undo::SinglePushWorldObjData(worldObj);
    robj->removeRoadPaths();
}
bool PropertiesRuler::support(GameObj* obj){
    if(obj == NULL)
        return false;
    if(obj->typeObj != GameObj::worldobj)
        return false;
    if(((WorldObj*)obj)->type == "ruler")
        return true;
    return false;
}

void PropertiesRuler::showElevBox(QString val){
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
}

void PropertiesRuler::hideElevBoxes(){
    elevProm.hide();
    elevProg.hide();
    elevProp.hide();
    elev1inXm.hide();
    elevPromLabel.hide();
    elevProgLabel.hide();
    elevPropLabel.hide();
    elev1inXmLabel.hide();
}
