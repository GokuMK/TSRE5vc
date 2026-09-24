/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/texture/MapLib.h>
#include <tsre/geo/MapWindow.h>
#include <QDebug>
#include <QString>
#include <QImage>
#include <QOpenGLShaderProgram>
#include <QColor>

MapLib::MapLib() {
}

void MapLib::run(){
    qDebug() << texture->pathid;
    QString hashName = texture->pathid.split(".")[0];
    int hash = hashName.toInt();
    qDebug() << hash;
    QImage image;
    const auto found=MapWindow::mapTileImages.find(hash);
    if(found==MapWindow::mapTileImages.end() || found->second==nullptr){
        image=QImage(16,16,QImage::Format_RGB888);
        image.fill(QColor(255,0,0));
    }else image=*found->second;

    texture->loaded=false;
    delete[] texture->imageData;
    texture->imageData=nullptr;
    texture->compressedData.clear();
    texture->sourceMipmaps.clear();
    texture->width = image.width();
    texture->height = image.height();
    if(image.format() == QImage::Format_RGBA8888){
        texture->bytesPerPixel = 4;
        texture->type = GL_RGBA;
    }else{
        texture->bytesPerPixel = 3;
        texture->type = GL_RGB;
    }

    const int lineWidth=texture->width*texture->bytesPerPixel;
    texture->imageSize=lineWidth*texture->height;
    texture->bpp=texture->bytesPerPixel*8;
    texture->imageData=new unsigned char[texture->imageSize];
    for(int i=0;i<texture->height;++i)
        memcpy(texture->imageData+i*lineWidth,image.constScanLine(i),lineWidth);
    texture->loaded = true;
    texture->editable = true;
    return;
}
