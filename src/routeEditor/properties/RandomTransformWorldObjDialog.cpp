/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "RandomTransformWorldObjDialog.h"

RandomTransformWorldObjDialog::RandomTransformWorldObjDialog() {
    transformation = new Ref::RandomTransformation();
    this->setWindowTitle(
        //% "Random Transform"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.title.random.transform"));
    QPushButton* ok = new QPushButton(
        //% "OK"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.button.ok"));
    QPushButton* cancel = new QPushButton(
        //% "Cancel"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.button.cancel"));
    connect(ok, SIGNAL (released()), this, SLOT (ok()));
    connect(cancel, SIGNAL (released()), this, SLOT (cancel()));

    QGridLayout *vlist = new QGridLayout;
    vlist->setSpacing(2);
    vlist->setContentsMargins(3,0,3,0);
    int row = 0;

    vlist->addWidget(new QLabel(
        //% "Translation Range:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.translation.range")),row++, 0, 1, 4);
    
    vlist->addWidget(new QLabel(
        //% "Min. X:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.min.x")),row, 0);
    vlist->addWidget(&this->tbX,row, 1);
    this->tbX.setText("0");
    vlist->addWidget(new QLabel(
        //% "Max. X:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.max.x")),row, 2);
    vlist->addWidget(&this->teX,row++, 3);
    this->teX.setText("0");

    vlist->addWidget(new QLabel(
        //% "Min. Y:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.min.y")),row, 0);
    vlist->addWidget(&this->tbY,row, 1);
    this->tbY.setText("0");
    vlist->addWidget(new QLabel(
        //% "Max. Y:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.max.y")),row, 2);
    vlist->addWidget(&this->teY,row++, 3);
    this->teY.setText("0");
    
    vlist->addWidget(new QLabel(
        //% "Min. Z:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.min.z")),row, 0);
    vlist->addWidget(&this->tbZ,row, 1);
    this->tbZ.setText("0");
    vlist->addWidget(new QLabel(
        //% "Max. Z:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.max.z")),row, 2);
    vlist->addWidget(&this->teZ,row++, 3);
    this->teZ.setText("0");
    
    vlist->addWidget(new QLabel(
        //% "Rotation Range:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.rotation.range")),row++, 0, 1, 4);
    
    vlist->addWidget(new QLabel(
        //% "Min. X deg:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.min.x.deg")),row, 0);
    vlist->addWidget(&this->rbX,row, 1);
    this->rbX.setText("0");
    vlist->addWidget(new QLabel(
        //% "Max. X deg:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.max.x.deg")),row, 2);
    vlist->addWidget(&this->reX,row++, 3);
    this->reX.setText("0");

    vlist->addWidget(new QLabel(
        //% "Min. Y deg:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.min.y.deg")),row, 0);
    vlist->addWidget(&this->rbY,row, 1);
    this->rbY.setText("0");
    vlist->addWidget(new QLabel(
        //% "Max. Y deg:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.max.y.deg")),row, 2);
    vlist->addWidget(&this->reY,row++, 3);
    this->reY.setText("0");
    
    vlist->addWidget(new QLabel(
        //% "Min. Z deg:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.min.z.deg")),row, 0);
    vlist->addWidget(&this->rbZ,row, 1);
    this->rbZ.setText("0");
    vlist->addWidget(new QLabel(
        //% "Max. Z deg:"
        qtTrId("route.editor.properties.random.transform.world.obj.dialog.label.max.z.deg")),row, 2);
    vlist->addWidget(&this->reZ,row++, 3);
    this->reZ.setText("0");
    
    vlist->addWidget(ok,row, 0);
    vlist->addWidget(cancel,row++, 1);

    vlist->setContentsMargins(1,1,1,1);
    this->setLayout(vlist);
}

RandomTransformWorldObjDialog::~RandomTransformWorldObjDialog() {
}

Ref::RandomTransformation* RandomTransformWorldObjDialog::getTransform(){
    return transformation;
}


void RandomTransformWorldObjDialog::cancel(){
    this->isOk = false;
    this->close();
}
void RandomTransformWorldObjDialog::ok(){
    this->isOk = true;

    transformation->rbX = this->rbX.text().toFloat();
    transformation->reX = this->reX.text().toFloat();
    transformation->rbY = this->rbY.text().toFloat();
    transformation->reY = this->reY.text().toFloat();    
    transformation->rbZ = this->rbZ.text().toFloat();
    transformation->reZ = this->reZ.text().toFloat();    
    
    transformation->tbX = this->tbX.text().toFloat();
    transformation->teX = this->teX.text().toFloat();
    transformation->tbY = this->tbY.text().toFloat();
    transformation->teY = this->teY.text().toFloat();    
    transformation->tbZ = this->tbZ.text().toFloat();
    transformation->teZ = this->teZ.text().toFloat();  
    
    this->close();
}