/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <shapeViewer/EngInfoWidget.h>
#include <tsre/Game.h>
#include <tsre/trains/Eng.h>
#include <tsre/units/Units.h>

EngInfoWidget::EngInfoWidget(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    connect(Units::Notifier::instance(), &Units::Notifier::changed, this, [this] { setInfo(shown); });
    
    QGridLayout *engInfoForm = new QGridLayout;
    engInfoForm->setSpacing(2);
    engInfoForm->setContentsMargins(1,1,1,1);    
    engInfoForm->addWidget(new QLabel(
        //% "Name:"
        qtTrId("shape.viewer.eng.info.widget.label.name")),0,0);
    engInfoForm->addWidget(new QLabel(
        //% "File Name:"
        qtTrId("shape.viewer.eng.info.widget.label.file.name")),1,0);
    engInfoForm->addWidget(new QLabel(
        //% "Dir Name:"
        qtTrId("shape.viewer.eng.info.widget.label.dir.name")),2,0);
    engInfoForm->addWidget(new QLabel(
        //% "Shape:"
        qtTrId("shape.viewer.eng.info.widget.label.shape")),3,0);
    engInfoForm->addWidget(new QLabel(
        //% "Type:"
        qtTrId("shape.viewer.eng.info.widget.label.type")),0,2);
    engInfoForm->addWidget(new QLabel(
        //% "Brakes:"
        qtTrId("shape.viewer.eng.info.widget.label.brakes")),1,2);
    engInfoForm->addWidget(new QLabel(
        //% "Couplings:"
        qtTrId("shape.viewer.eng.info.widget.label.couplings")),2,2);
    engInfoForm->addWidget(new QLabel(
        //% "Size:"
        qtTrId("shape.viewer.eng.info.widget.label.size")),3,2);
    engInfoForm->addWidget(new QLabel(
        //% "Mass:"
        qtTrId("shape.viewer.eng.info.widget.label.mass")),0,4);
    engInfoForm->addWidget(new QLabel(
        //% "Max. Speed:"
        qtTrId("shape.viewer.eng.info.widget.label.max.speed")),1,4);
    engInfoForm->addWidget(new QLabel(
        //% "Max. Force:"
        qtTrId("shape.viewer.eng.info.widget.label.max.force")),2,4);
    engInfoForm->addWidget(new QLabel(
        //% "Max. Power:"
        qtTrId("shape.viewer.eng.info.widget.label.max.power")),3,4);
    
    engInfoForm->addWidget(&eName,0,1);
    engInfoForm->addWidget(&eFileName,1,1);
    engInfoForm->addWidget(&eDirName,2,1);
    engInfoForm->addWidget(&eShape,3,1);
    engInfoForm->addWidget(&eType,0,3);
    engInfoForm->addWidget(&eBrakes,1,3);
    engInfoForm->addWidget(&eCouplings,2,3);
    engInfoForm->addWidget(&eSize,3,3);
    engInfoForm->addWidget(&eMass,0,5);
    engInfoForm->addWidget(&eMaxSpeed,1,5);
    engInfoForm->addWidget(&eMaxForce,2,5);
    engInfoForm->addWidget(&eMaxPower,3,5);
    eMass.setMaximumWidth(70);
    eMaxSpeed.setMaximumWidth(70);
    eMaxForce.setMaximumWidth(70);
    eMaxPower.setMaximumWidth(70);
    setLayout(engInfoForm);
}

EngInfoWidget::~EngInfoWidget() {
}

void EngInfoWidget::setInfo(Eng* eng){
    if(eng == NULL)
        return;
    shown = eng;
    using Units::Quantity;

    eName.setText(eng->displayName);
    eFileName.setText(eng->name);
    eDirName.setText(eng->path.split("/").last());
    QString ttype = eng->type;
    if(eng->engType.length() > 1)
        ttype += " ( "+eng->engType+" )";
    eType.setText(ttype);
    //eBrakes;
    //eCouplings;
    eMass.setText(Units::format(eng->massKg, Quantity::Mass));
    if(eng->wagonTypeId >= 4){
        eMaxSpeed.setText(Units::format(eng->maxSpeedMps, Quantity::Speed, 0));
        eMaxForce.setText(Units::format(eng->forceN, Quantity::Force));
        eMaxPower.setText(Units::format(eng->powerW, Quantity::Power));
    } else {
        eMaxSpeed.setText("--");
        eMaxForce.setText("--");
        eMaxPower.setText("--");
    }
    eShape.setText(eng->shape.name);
    eSize.setText(Units::formatNumber(eng->sizex, Quantity::Length) + " "
                  + Units::formatNumber(eng->sizey, Quantity::Length) + " "
                  + Units::format(eng->sizez, Quantity::Length));
    eCouplings.setText(eng->getCouplingsName());
    eBrakes.setText(eng->brakeSystemType);
}