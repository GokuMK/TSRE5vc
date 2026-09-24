/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PropertiesDyntrack.h"
#include <tsre/world/objects/DynTrackObj.h>
#include <tsre/math3d/Flex.h>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/Undo.h>
#include <tsre/procedural/ProceduralShape.h>
#include <tsre/procedural/ShapeTemplates.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <QSignalBlocker>

PropertiesDyntrack::PropertiesDyntrack() {
    buttonTools["FlexTool"] = new QPushButton(
        //% "Flex"
        qtTrId("route.editor.properties.dyntrack.button.flex"), this);
    QMapIterator<QString, QPushButton*> i(buttonTools);
    while (i.hasNext()) {
        i.next();
        i.value()->setCheckable(true);
    }
    
    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    
    infoLabel = new QLabel(
        //% "DynTrack:"
        qtTrId("route.editor.properties.dyntrack.label.info.label"));
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
    this->eDatabase.setReadOnly(true);
    vlist->addRow(
        //% "UiD:"
        qtTrId("route.editor.properties.dyntrack.label.ui.d"),&this->uid);
    vlist->addRow(
        //% "Tile X:"
        qtTrId("route.editor.properties.dyntrack.label.tile.x"),&this->tX);
    vlist->addRow(
        //% "Tile Z:"
        qtTrId("route.editor.properties.dyntrack.label.tile.z"),&this->tY);
    vlist->addRow(
        //% "TrackShape:"
        qtTrId("route.editor.properties.dyntrack.label.track.shape"),&this->eSectionIdx);
    vlist->addRow(
        //% "Database:"
        qtTrId("route.editor.properties.dyntrack.label.database"),&this->eDatabase);
    vbox->addItem(vlist);

    QLabel *templateLabel = new QLabel(
        //% "Shape Template:"
        qtTrId("route.editor.properties.dyntrack.label.template.label"));
    templateLabel->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    templateLabel->setContentsMargins(3,0,0,0);
    vbox->addWidget(templateLabel);
    vbox->addWidget(&eTemplate);
    vbox->addWidget(&eTemplateSubtype);
    eTemplate.setStyleSheet("combobox-popup: 0;");
    eTemplateSubtype.setStyleSheet("combobox-popup: 0;");
    eTemplateSubtype.setEnabled(false);
    eTemplateSubtype.setToolTip(
        //% "Select a route profile subtype."
        qtTrId("route.editor.properties.profile.tooltip.route.profile.subtype"));
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
        //% "NOT SET uses the hardcoded shape in Enabled mode; DEFAULT explicitly requests the default procedural template."
        qtTrId("route.editor.properties.dyntrack.tooltip.not.set.uses.hardcoded.shape.in.enabled"));
    QObject::connect(&eTemplate, SIGNAL(currentTextChanged(QString)),
                     this, SLOT(eTemplateEdited(QString)));
    QObject::connect(&eTemplateSubtype, SIGNAL(currentTextChanged(QString)),
                     this, SLOT(eTemplateSubtypeEdited(QString)));
    
    QLabel * label2 = new QLabel(
        //% "Sections:"
        qtTrId("route.editor.properties.dyntrack.label.label2"));
    label2->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label2->setContentsMargins(3,0,0,0);
    vbox->addWidget(label2);
    
    
    this->chSect[0].setText(
        //% "First Straight section"
        qtTrId("route.editor.properties.dyntrack.text.first.straight.section"));
    this->chSect[0].setChecked(true);
    //this->chSect[0].setEnabled(false);// .setCheckable(false);
    vbox->addWidget(&chSect[0]);
    vSect[0].setSpacing(2);
    vSect[0].setContentsMargins(3,0,3,0);
    vSect[0].addRow(
        //% "Length:"
        qtTrId("route.editor.properties.dyntrack.label.length"),&this->sSectA[0]);
    wSect[0].setLayout(&vSect[0]);
    vbox->addWidget(&wSect[0]);
    
    this->chSect[1].setText(
        //% "First curve"
        qtTrId("route.editor.properties.dyntrack.text.first.curve"));
    vbox->addWidget(&chSect[1]);
    vSect[1].setSpacing(2);
    vSect[1].setContentsMargins(3,0,3,0);
    vSect[1].addRow(
        //% "Angle:"
        qtTrId("route.editor.properties.dyntrack.label.angle"),&this->sSectA[1]);
    vSect[1].addRow(
        //% "Radius:"
        qtTrId("route.editor.properties.dyntrack.label.radius"),&this->sSectR[1]);
    wSect[1].setLayout(&vSect[1]);
    vbox->addWidget(&wSect[1]);
    
    
    this->chSect[2].setText(
        //% "Second Straight section"
        qtTrId("route.editor.properties.dyntrack.text.second.straight.section"));
    vbox->addWidget(&chSect[2]);
    vSect[2].setSpacing(2);
    vSect[2].setContentsMargins(3,0,3,0);
    vSect[2].addRow(
        //% "Length:"
        qtTrId("route.editor.properties.dyntrack.label.length.2"),&this->sSectA[2]);
    wSect[2].setLayout(&vSect[2]);
    vbox->addWidget(&wSect[2]);
    
    this->chSect[3].setText(
        //% "Second curve"
        qtTrId("route.editor.properties.dyntrack.text.second.curve"));
    vbox->addWidget(&chSect[3]);
    vSect[3].setSpacing(2);
    vSect[3].setContentsMargins(3,0,3,0);
    vSect[3].addRow(
        //% "Angle:"
        qtTrId("route.editor.properties.dyntrack.label.angle.2"),&this->sSectA[3]);
    vSect[3].addRow(
        //% "Radius:"
        qtTrId("route.editor.properties.dyntrack.label.radius.2"),&this->sSectR[3]);
    wSect[3].setLayout(&vSect[3]);
    vbox->addWidget(&wSect[3]);
    
    
    this->chSect[4].setText(
        //% "Third Straight section"
        qtTrId("route.editor.properties.dyntrack.text.third.straight.section"));
    vbox->addWidget(&chSect[4]);
    vSect[4].setSpacing(2);
    vSect[4].setContentsMargins(3,0,3,0);
    vSect[4].addRow(
        //% "Length:"
        qtTrId("route.editor.properties.dyntrack.label.length.3"),&this->sSectA[4]);
    wSect[4].setLayout(&vSect[4]);
    vbox->addWidget(&wSect[4]);
    
    vbox->addWidget(buttonTools["FlexTool"]);
    
    QLabel *label = new QLabel(
        //% "Elevation:"
        qtTrId("route.editor.properties.dyntrack.label.label"));
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
        qtTrId("route.editor.properties.dyntrack.label.value"),&this->elevType);
    elevType.addItem(
        //% "Permille ‰"
        qtTrId("route.editor.properties.dyntrack.item.permille"), "permille");
    elevType.addItem(
        //% "Percent %"
        qtTrId("route.editor.properties.dyntrack.item.percent"), "percent");
    elevType.addItem(
        //% "1 in 'X' m"
        qtTrId("route.editor.properties.dyntrack.item.1.in.x.m"), "oneInX");
    elevType.addItem(
        //% "Angle º"
        qtTrId("route.editor.properties.dyntrack.item.angle"), "angle");
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
        qtTrId("route.editor.properties.dyntrack.text.1.in.x.m"));
    vlist->addRow(&elev1inXmLabel,&elev1inXm);
    elev1inXm.setValidator(doubleValidator);
    QObject::connect(&elev1inXm, SIGNAL(textEdited(QString)), this, SLOT(elev1inXmEnabled(QString)));
    //º
    elevProgLabel.setText(
        //% "º"
        qtTrId("route.editor.properties.dyntrack.text.value"));
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
        qtTrId("route.editor.properties.dyntrack.label.step"),&elevStep);
    elevStep.setValidator(doubleValidator);
    QObject::connect(&elevStep, SIGNAL(textEdited(QString)), this, SLOT(elevStepEnabled(QString)));
    hideElevBoxes();
    elevType.setCurrentIndex(Settings::enumIndex("core.track.defaultGradeFormat"));
    ElevTypeName = elevType.currentData().toString();
    showElevBox(ElevTypeName);
    vbox->addItem(vlist);
    
    vbox->addStretch(1);
    this->setLayout(vbox);
    
    for(int i=0; i<5; i++){
        if(i%2 == 0){
            sSectA[i].setDecimals(2);
            sSectA[i].setMinimum(0);
            sSectA[i].setMaximum(250);
            sSectA[i].setSingleStep(1.0);
        } else {
            sSectA[i].setDecimals(3);
            sSectA[i].setMinimum(-3.14);
            sSectA[i].setMaximum(3.14);
            sSectA[i].setSingleStep(0.01);
        }
        sSectR[i].setDecimals(2);
        sSectR[i].setMinimum(15);
        sSectR[i].setMaximum(5000);
        sSectR[i].setSingleStep(1.0);
        sSectA[i].setKeyboardTracking(false);
        sSectR[i].setKeyboardTracking(false);
    }
    
    for(int i = 0; i < 5; i++){
        dyntrackChSect.setMapping(&chSect[i], i);
        connect(&chSect[i], SIGNAL(clicked()), &dyntrackChSect, SLOT(map()));
    }
    
    QObject::connect(&dyntrackChSect, SIGNAL(mappedInt(int)),
            this, SLOT(chSectEnabled(int)));
    
    for(int i = 0; i < 5; i++){
        dyntrackSect.setMapping(&sSectR[i], i);
        connect(&sSectR[i], SIGNAL(valueChanged(double)), &dyntrackSect, SLOT(map()));
        dyntrackSect.setMapping(&sSectA[i], i);
        connect(&sSectA[i], SIGNAL(valueChanged(double)), &dyntrackSect, SLOT(map()));
    }
    
    QObject::connect(&dyntrackSect, SIGNAL(mappedInt(int)),
            this, SLOT(sSectEnabled(int)));
    
    QObject::connect(buttonTools["FlexTool"], SIGNAL(released()),
                      this, SLOT(flexEnabled()));
    
}

PropertiesDyntrack::~PropertiesDyntrack() {
}

void PropertiesDyntrack::msg(QString name, QString val){
    if(name == "toolEnabled"){
        QMapIterator<QString, QPushButton*> i(buttonTools);
        while (i.hasNext()) {
            i.next();
            if(i.value() == NULL)
                continue;
            i.value()->blockSignals(true);
            i.value()->setChecked(false);
        }
        if(buttonTools[val] != NULL)
            buttonTools[val]->setChecked(true);
        i.toFront();
        while (i.hasNext()) {
            i.next();
            if(i.value() == NULL)
                continue;
            i.value()->blockSignals(false);
        }
    }
}

void PropertiesDyntrack::showObj(GameObj* obj){
    if(obj == NULL){
        infoLabel->setText(
            //% "NULL"
            qtTrId("route.editor.properties.dyntrack.text.null"));
        return;
    }
    worldObj = (WorldObj*)obj;
    dobj = (DynTrackObj*)obj;
    refreshTemplateList();
    //% "Object: %1"
    this->infoLabel->setText(qtTrId("route.properties.dynamic.track.object.type")
                             .arg(dobj->type));
    
    this->uid.setText(QString::number(dobj->UiD, 10));
    this->tX.setText(QString::number(dobj->x, 10));
    this->tY.setText(QString::number(-dobj->y, 10));
    if(dobj->sectionIdx < 0)
        this->eSectionIdx.setText("");
    else
        this->eSectionIdx.setText(QString::number(dobj->sectionIdx, 10));
    this->eDatabase.setText(dobj->isRoad()
            ?
              //% "Road (RDB)"
              qtTrId("route.properties.dyntrack.database.road")
            :
              //% "Rail (TDB)"
              qtTrId("route.properties.dyntrack.database.rail"));
    
    updateSectionValues();
    updateTemplateValue();
    
    ///////////
    elevType.setCurrentIndex(qMax(0, elevType.findData(ElevTypeName)));
    
    float * q = dobj->qDirection;
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
    
    //this->carNumber.setText(QString::number(pobj->getCarNumber(),10));
    //this->carSpeed.setText(QString::number(pobj->getCarSpeed(),10));
}


void PropertiesDyntrack::updateObj(GameObj* obj){
    if(obj == NULL){
        return;
    }
    dobj = (DynTrackObj*)obj;
    this->eDatabase.setText(dobj->isRoad()
            ?
              //% "Road (RDB)"
              qtTrId("route.properties.dyntrack.database.road")
            :
              //% "Rail (TDB)"
              qtTrId("route.properties.dyntrack.database.rail"));
    float * q = dobj->qDirection;
    float vect[3];
    vect[0] = 0; vect[1] = 0; vect [2] = 1000.0;
    Vec3::transformQuat(vect, vect, q);
    vect[1] = -vect[1];
     
    float oneInXm = 0.0;
    oneInXm = 1000.0/vect[1];
    float prog = qRadiansToDegrees(qAtan(vect[1]/1000.0));
    float prop = vect[1]/10.0;
       
    if(!this->elevProm.hasFocus() && !this->elev1inXm.hasFocus() && !this->elevProg.hasFocus() && !this->elevProp.hasFocus()){
        this->elevProm.setText(QString::number(vect[1]));
        this->elevProg.setText(QString::number(prog));
        this->elevProp.setText(QString::number(prop));
        this->elev1inXm.setText(QString::number(oneInXm));
    }

    updateSectionValues();
    updateTemplateValue();
}

void PropertiesDyntrack::refreshTemplateList(){
    const QSignalBlocker blocker(&eTemplate);
    const QString previousValue = dobj != NULL
            ? dobj->getTemplate() : selectedTemplateValue();

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
    const OrtsTrackProfile::ObjectType profileType = dobj != NULL && dobj->isRoad()
            ? OrtsTrackProfile::ObjectType::Road
            : OrtsTrackProfile::ObjectType::Track;

    // Route-local ORTS profile families are the most specific definitions, so
    // show them before application-level TSRE templates. Their exact role is
    // selected in the subtype combo below.
    addRouteProfileFamilies(profileType);

    if(ProceduralShape::ShapeTemplateFile != NULL){
        QMapIterator<QString, ShapeTemplate*> iterator(
                ProceduralShape::ShapeTemplateFile->templates);
        while(iterator.hasNext()){
            iterator.next();
            if(iterator.value() == NULL)
                continue;
            const ShapeTemplate::TemplateType expectedType =
                    profileType == OrtsTrackProfile::ObjectType::Road
                    ? ShapeTemplate::ROAD : ShapeTemplate::TRACK;
            if(iterator.value()->type != expectedType)
                continue;
            const QString name = iterator.value()->name;
            // A route profile with the same ID overrides the global template.
            if(OrtsTrackProfileCatalog::hasFamily(name, profileType)
                    || OrtsTrackProfileCatalog::find(
                        name, profileType) != nullptr)
                continue;
            if(eTemplate.findData(name) < 0)
                eTemplate.addItem(name, name);
        }
    }

    selectTemplateValue(previousValue, profileType);
}

void PropertiesDyntrack::updateTemplateValue(){
    if(dobj == NULL)
        return;
    if(selectedTemplateValue() == dobj->getTemplate())
        return;
    const OrtsTrackProfile::ObjectType profileType = dobj->isRoad()
            ? OrtsTrackProfile::ObjectType::Road
            : OrtsTrackProfile::ObjectType::Track;
    selectTemplateValue(dobj->getTemplate(), profileType);
}

void PropertiesDyntrack::eTemplateEdited(QString val){
    if(dobj == NULL)
        return;
    Q_UNUSED(val);
    const OrtsTrackProfile::ObjectType profileType = dobj->isRoad()
            ? OrtsTrackProfile::ObjectType::Road
            : OrtsTrackProfile::ObjectType::Track;
    refreshTemplateSubtype(profileType);
    val = selectedTemplateValue();
    Undo::SinglePushWorldObjData(worldObj);
    dobj->setTemplate(val);
}

void PropertiesDyntrack::eTemplateSubtypeEdited(QString val){
    if(dobj == NULL)
        return;
    Q_UNUSED(val);
    Undo::SinglePushWorldObjData(worldObj);
    dobj->setTemplate(selectedTemplateValue());
}

void PropertiesDyntrack::updateSectionValues(){
    if(dobj == NULL || dobj->sections == NULL)
        return;

    for(int i = 0; i < 5; i++){
        const bool enabled = dobj->sections[i].sectIdx <= 1000000;
        const QSignalBlocker checkBlocker(&chSect[i]);
        chSect[i].setChecked(enabled);
        wSect[i].setVisible(enabled);
        if(!enabled)
            continue;

        const QSignalBlocker angleBlocker(&sSectA[i]);
        const QSignalBlocker radiusBlocker(&sSectR[i]);

        // Live Flex can validly exceed the panel's normal manual-edit ranges.
        // Expand only as needed so the displayed value is never clamped.
        if(dobj->sections[i].a > sSectA[i].maximum())
            sSectA[i].setMaximum(dobj->sections[i].a);
        if(dobj->sections[i].r > sSectR[i].maximum())
            sSectR[i].setMaximum(dobj->sections[i].r);

        if(!sSectA[i].hasFocus())
            sSectA[i].setValue(dobj->sections[i].a);
        if(!sSectR[i].hasFocus())
            sSectR[i].setValue(dobj->sections[i].r);
    }
}

void PropertiesDyntrack::chSectEnabled(int idx){
    qDebug() << "chSectEnabled";
    if(dobj == NULL){
        infoLabel->setText(
            //% "NULL"
            qtTrId("route.editor.properties.dyntrack.text.null.2"));
        return;
    }
    
    bool state = this->chSect[idx].isChecked();
    Undo::SinglePushWorldObjData(worldObj);
    
    if(state){
        this->wSect[idx].show();
        dobj->sections[idx].sectIdx = 0;
        if(idx%2 == 0){
            dobj->sections[idx].a = 2;
            dobj->sections[idx].r = 0;
        } else {
            dobj->sections[idx].a = 0.1;
            dobj->sections[idx].r = 100;
        }
        this->sSectA[idx].blockSignals(true);
        this->sSectR[idx].blockSignals(true);
        this->sSectA[idx].setValue(dobj->sections[idx].a);
        this->sSectR[idx].setValue(dobj->sections[idx].r);
        this->sSectA[idx].blockSignals(false);
        this->sSectR[idx].blockSignals(false);
    } else {
        this->wSect[idx].hide();
        dobj->sections[idx].sectIdx = 4294967295;
    }
    dobj->modified = true;
    dobj->deleteVBO();
}

void PropertiesDyntrack::sSectEnabled(int idx){
    qDebug() << "sSectEnabled";
    if(dobj == NULL){
        infoLabel->setText(
            //% "NULL"
            qtTrId("route.editor.properties.dyntrack.text.null.3"));
        return;
    }
    if(idx%2 == 1 && fabs(this->sSectA[idx].value()) < 0.0001 )
        return;

    Undo::SinglePushWorldObjData(worldObj);
    dobj->sections[idx].a = this->sSectA[idx].value();
    if(idx%2 == 1)
        dobj->sections[idx].r = this->sSectR[idx].value();
    dobj->modified = true;
    dobj->box.loaded = false;
    dobj->deleteVBO();
}

bool PropertiesDyntrack::support(GameObj* obj){
    if(obj == NULL)
        return false;
    if(obj->typeObj != GameObj::worldobj)
        return false;
    if(((WorldObj*)obj)->type == "dyntrack")
        return true;
    return false;
}

void PropertiesDyntrack::flexEnabled(){
    emit enableTool("FlexTool");
    
}

void PropertiesDyntrack::flexData(int x, int z, float* p){
    emit enableTool("");
    if(dobj == NULL) return;
    
    qDebug() << "flex1";
    
    float p1[3];
    float p2[3];
    p1[0] = dobj->position[0];
    p1[1] = dobj->position[1];
    p1[2] = dobj->position[2];
    p2[0] = p[0];
    p2[1] = p[1];
    p2[2] = p[2];
    
    float dyntrackData[10] = {0};
    float elev = 0;

    TDB *database = dobj->isRoad() ? Game::roadDB : Game::trackDB;
    bool success = Flex::AutoFlex(
            database, dobj->x, dobj->y, (float*)p1,
            x, z, (float*)p2, (float*)dyntrackData, elev);
    qDebug() << "flex2" << elev;
    if(!success){
        this->showObj(dobj);
        emit enableTool("selectTool");
        return;
    }

    Undo::SinglePushWorldObjData(worldObj);
    dobj->set("dyntrackdata", (float*)dyntrackData);
    dobj->setElevation(elev);
    this->showObj(dobj);
    emit enableTool("selectTool");
}

void PropertiesDyntrack::elevTypeEdited(QString val){
    Q_UNUSED(val);
    val = elevType.currentData().toString();
    hideElevBoxes();
    showElevBox(val);
    ElevTypeName = val;
}

void PropertiesDyntrack::showElevBox(QString val){
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

void PropertiesDyntrack::hideElevBoxes(){
    elevProm.hide();
    elevProg.hide();
    elevProp.hide();
    elev1inXm.hide();
    elevPromLabel.hide();
    elevProgLabel.hide();
    elevPropLabel.hide();
    elev1inXmLabel.hide();
}

void PropertiesDyntrack::setStepValue(float step){
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

float PropertiesDyntrack::getStepValue(float step){
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

void PropertiesDyntrack::elevPromEnabled(QString val){
    if(dobj == NULL){
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
    dobj->setElevation(prom);
}

void PropertiesDyntrack::elevStepEnabled(QString val){
    if(dobj == NULL)
        return;

    bool ok = false;
    float f = val.toFloat(&ok);
    if(!ok)
        return;
    
    f = getStepValue(f);
    
    Game::DefaultMoveStep = f;
    emit setMoveStep(f);
}

void PropertiesDyntrack::elev1inXmEnabled(QString val){
    if(dobj == NULL){
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
    dobj->setElevation(prom);
}

void PropertiesDyntrack::elevProgEnabled(QString val){
    if(dobj == NULL){
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
    dobj->setElevation(prom);
}
 
void PropertiesDyntrack::elevPropEnabled(QString val){
    if(dobj == NULL){
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
    dobj->setElevation(prom);
}
