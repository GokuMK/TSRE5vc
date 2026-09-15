/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <shapeViewer/ConInfoWidget.h>
#include <tsre/Game.h>

ConInfoWidget::ConInfoWidget(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    
    QGridLayout *engInfoForm = new QGridLayout;
    engInfoForm->setSpacing(2);
    engInfoForm->setContentsMargins(1,1,1,1);    
    engInfoForm->addWidget(new QLabel(
        //% "Name:"
        qtTrId("shape.viewer.con.info.widget.label.name")),0,0);
    engInfoForm->addWidget(new QLabel(
        //% "File Name:"
        qtTrId("shape.viewer.con.info.widget.label.file.name")),1,0);
    engInfoForm->addWidget(new QLabel(
        //% "Dir Name:"
        qtTrId("shape.viewer.con.info.widget.label.dir.name")),2,0);
    engInfoForm->addWidget(new QLabel(
        //% "Shape:"
        qtTrId("shape.viewer.con.info.widget.label.shape")),3,0);
    engInfoForm->addWidget(new QLabel(
        //% "Type:"
        qtTrId("shape.viewer.con.info.widget.label.type")),0,2);
    engInfoForm->addWidget(new QLabel(
        //% "Brakes:"
        qtTrId("shape.viewer.con.info.widget.label.brakes")),1,2);
    engInfoForm->addWidget(new QLabel(
        //% "Couplings:"
        qtTrId("shape.viewer.con.info.widget.label.couplings")),2,2);
    engInfoForm->addWidget(new QLabel(
        //% "Size:"
        qtTrId("shape.viewer.con.info.widget.label.size")),3,2);
    engInfoForm->addWidget(new QLabel(
        //% "Mass:"
        qtTrId("shape.viewer.con.info.widget.label.mass")),0,4);
    engInfoForm->addWidget(new QLabel(
        //% "Max. Speed:"
        qtTrId("shape.viewer.con.info.widget.label.max.speed")),1,4);
    engInfoForm->addWidget(new QLabel(
        //% "Max. Force:"
        qtTrId("shape.viewer.con.info.widget.label.max.force")),2,4);
    engInfoForm->addWidget(new QLabel(
        //% "Max. Power:"
        qtTrId("shape.viewer.con.info.widget.label.max.power")),3,4);

    setLayout(engInfoForm);
}

ConInfoWidget::~ConInfoWidget() {
}

