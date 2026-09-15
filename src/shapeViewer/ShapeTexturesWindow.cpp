/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <shapeViewer/ShapeTexturesWindow.h>
#include <tsre/Game.h>
#include <shapeViewer/ShapeTextureInfo.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>
#include <tsre/gui/ClickableLabel.h>

ShapeTexturesWindow::ShapeTexturesWindow(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::WindowType::Tool);
    setWindowTitle(
        //% "Shape Textures"
        qtTrId("shape.viewer.shape.textures.window.title.shape.textures"));
    
    texPreview = new QPixmap(192,192);
    texPreview->fill(Qt::gray);
    texPreviewLabel = new ClickableLabel("");
    texPreviewLabel->setContentsMargins(0,0,0,0);
    texPreviewLabel->setPixmap(*texPreview);
    
    QHBoxLayout *vbox = new QHBoxLayout;
    vbox->setSpacing(2);
    vbox->setContentsMargins(0,1,1,1);
    
    QLabel *label = new QLabel(
        //% "Textures:"
        qtTrId("shape.viewer.shape.textures.window.label.label"));
    label->setStyleSheet(QString("QLabel { color : ")+Game::StyleMainLabel+"; }");
    label->setContentsMargins(3,0,0,0);
    //vbox->addWidget(label);

    QStringList list;
    //% "Show:"
    list.append(qtTrId("shape.textures.header.show"));
    //% "Texture Name:"
    list.append(qtTrId("shape.textures.header.name"));
    //% "Loaded:"
    list.append(qtTrId("shape.textures.header.loaded"));
    //% "Resolution:"
    list.append(qtTrId("shape.textures.header.resolution"));
    //% "Format:"
    list.append(qtTrId("shape.textures.header.format"));
    textureList.setColumnCount(5);
    textureList.setHeaderLabels(list);
    textureList.setRootIsDecorated(false);
    textureList.header()->resizeSection(0,40);
    textureList.header()->resizeSection(1,200);    
    textureList.header()->resizeSection(2,50);    
    textureList.header()->resizeSection(3,60);    
    textureList.header()->resizeSection(4,150);    
    vbox->addWidget(&textureList);
    vbox->addWidget(texPreviewLabel);
    QObject::connect(&textureList, SIGNAL(itemChanged(QTreeWidgetItem*, int)),
                      this, SLOT(textureListChanged(QTreeWidgetItem*, int)));
    QObject::connect(&textureList, SIGNAL(itemClicked(QTreeWidgetItem*, int)),
                      this, SLOT(textureListSelected(QTreeWidgetItem*, int)));
    QObject::connect(texPreviewLabel, SIGNAL(clicked()),
                      this, SLOT(saveImg()));
    
    this->setLayout(vbox);
    this->resize(720,200);
    

}

ShapeTexturesWindow::~ShapeTexturesWindow() {
}

void ShapeTexturesWindow::textureListSelected(QTreeWidgetItem* item, int id){
    qDebug() << id;
    qDebug() << item->type();
    currentTex = TexLib::mtex[item->type()];
    if(currentTex == NULL)
        return;
    int res = 192;
    unsigned char * out;
    out = currentTex->getImageData(res,res);
    if(currentTex->bytesPerPixel == 3)
        texPreviewLabel->setPixmap(QPixmap::fromImage(QImage(out,res,res,QImage::Format_RGB888)));
    if(currentTex->bytesPerPixel == 4)
        texPreviewLabel->setPixmap(QPixmap::fromImage(QImage(out,res,res,QImage::Format_RGBA8888)));   
}

void ShapeTexturesWindow::textureListChanged(QTreeWidgetItem* item, int id){
    qDebug() << id;
    if(item->checkState(0) == Qt::Checked){
        qDebug() << item->type();
        TexLib::enableTexture(item->type());
    }else{
        qDebug() << item->type();
        TexLib::disableTexture(item->type());
    }
}

void ShapeTexturesWindow::clearLists(){
    textureList.clear();
}

void ShapeTexturesWindow::saveImg(){
    if(currentTex == NULL)
        return;
    
    QString path = QFileDialog::getSaveFileName(this,
        //% "Save File"
        qtTrId("shape.viewer.shape.textures.window.dialog.title.path"), "./",
        //% "Images (*.png *.bmp *.tga)"
        qtTrId("shape.viewer.shape.textures.window.dialog.filter.path"));
    qDebug() << path;
    if(path.length() < 1) 
        return;
    
    unsigned char * out = currentTex->getImageData(currentTex->width,currentTex->height);

    if(currentTex->bytesPerPixel == 3){
        QPixmap::fromImage(QImage(out,currentTex->width,currentTex->height,QImage::Format_RGB888)).save(path);
    }
    if(currentTex->bytesPerPixel == 4){
        QPixmap::fromImage(QImage(out,currentTex->width,currentTex->height,QImage::Format_RGBA8888)).save(path);   
    }

}

void ShapeTexturesWindow::setTextureList(QHash<int, ShapeTextureInfo*> textureInfoList){
    textureList.clear();
    QStringList list;
    QList<QTreeWidgetItem *> items;
    
    //textures = textureInfoList;
    
    int count = -1;
    foreach(ShapeTextureInfo* i, textureInfoList){
        count++;
        list.clear();
        list.append(QString(""));
        list.append(i->textureName);
        list.append(i->loaded);
        list.append(i->resolution);
        list.append(i->format);
        QTreeWidgetItem *item = new QTreeWidgetItem((QTreeWidget*)0, list, i->textureId );
        item->setCheckState(0, Qt::Unchecked);
        item->setCheckState(0, Qt::Checked);
        items.append(item);
    }
    textureList.blockSignals(true);
    textureList.insertTopLevelItems(0, items);
    textureList.blockSignals(false);
}