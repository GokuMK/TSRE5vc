/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <shapeViewer/ShapeInfoWidget.h>
#include <tsre/Game.h>

ShapeInfoWidget::ShapeInfoWidget(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    
    QGridLayout *engInfoForm = new QGridLayout;
    engInfoForm->setSpacing(2);
    engInfoForm->setContentsMargins(1,1,1,1);    
    engInfoForm->addWidget(new QLabel(
        //% "File Name:"
        qtTrId("shape.viewer.shape.info.widget.label.file.name")),0,0);
    engInfoForm->addWidget(new QLabel(
        //% "Shape Type:"
        qtTrId("shape.viewer.shape.info.widget.label.shape.type")),1,0);
    engInfoForm->addWidget(new QLabel(
        //% "SD File:"
        qtTrId("shape.viewer.shape.info.widget.label.sd.file")),2,0);
    engInfoForm->addWidget(new QLabel(
        //% "Poly count:"
        qtTrId("shape.viewer.shape.info.widget.label.poly.count")),0,2);
    engInfoForm->addWidget(new QLabel(
        //% "Dimensions SD file:"
        qtTrId("shape.viewer.shape.info.widget.label.dimensions.sd.file")),1,2);
    engInfoForm->addWidget(new QLabel(
        //% "Dimensions Calculated:"
        qtTrId("shape.viewer.shape.info.widget.label.dimensions.calculated")),2,2);
    
    engInfoForm->addWidget(&sName,0,1);
    engInfoForm->addWidget(&sType,1,1);
    engInfoForm->addWidget(&sSdFile,2,1);
    engInfoForm->addWidget(&sPolyCount,0,3);
    engInfoForm->addWidget(&sDimensionsSd,1,3);
    engInfoForm->addWidget(&sDimensionsCalc,2,3);
    
    setLayout(engInfoForm);
}

ShapeInfoWidget::~ShapeInfoWidget() {
}

