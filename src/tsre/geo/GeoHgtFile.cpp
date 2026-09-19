/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/geo/GeoHgtFile.h>
#include <tsre/geo/ElevationSource.h>
#include <math.h>
#include <QDebug>
#include <QImage>
#include <QPainter>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>

GeoHgtFile::GeoHgtFile() {
}

GeoHgtFile::~GeoHgtFile() {
    for (int i=0; i<rowSize; ++i) delete[] terrainData[i];
    delete[] terrainData;
}

bool GeoHgtFile::load(int lat, int lon){
    loaded = false;
    QString catalogueError;
    const auto catalog = Elevation::datasets(catalogueError);
    const QString sourceId = Elevation::defaultFileSourceId(catalog);
    const Elevation::Dataset *source = nullptr;
    for (const auto &dataset : catalog) if (dataset.id == sourceId) { source = &dataset; break; }
    if (!source) return false;
    this->pathid = Elevation::findHgtFile(
        Settings::string("core.paths.geoData", SettingType::Directory),*source,lat,lon);
    this->pathid = ContentPath::normalize(pathid);
    qDebug() << this->pathid;
    Elevation::Raster raster;
    QString error;
    if (!Elevation::readHgtFile(pathid,lat,lon,raster,error)) {
        qDebug() <<"HGT not found: "<< this->pathid << error;
        return false;
    }
    for (int i=0; i<rowSize; ++i) delete[] terrainData[i];
    delete[] terrainData;
    this->rowSize = raster.width;
    qDebug() << this->rowSize;
    terrainData = new short int*[rowSize];
    qint64 avg = 0;
    for (int i = 0; i < rowSize; i++) {
        terrainData[i] = new short int[rowSize];
        for (int j = 0; j < rowSize; j++) {
            terrainData[i][j] = short(raster.values[i*rowSize+j]);
            avg += terrainData[i][j];
        }
    }
    qDebug() << avg / (rowSize*rowSize);
    loaded = true;
    return true;
}

bool GeoHgtFile::isLoaded(){
    return loaded;
}

void GeoHgtFile::draw(QImage* &image){
    qDebug() << this->rowSize;
    image = new QImage(this->rowSize, this->rowSize, QImage::Format_RGB888);
    short int val;
    for (int i = 0; i < rowSize; i++) {
        for (int j = 0; j < rowSize; j++) {
            val = this->terrainData[j][i] * 4;
                if(val < 0) val = 0;
                if(val > 255) val = 255;
                image->setPixel(i, j, qRgb(val,val,val));
        }
    }
}

float GeoHgtFile::getHeight(float lat, float lon){
    float latO = lat - floor(lat);
    float lonO = lon - floor(lon);
    int latI = rowSize*latO;
    int lonI = rowSize*lonO;

    //if(latI == 0 || lonI == 0){
    //qDebug() << latI << ":" << lonI;
    //    return this->terrainData[rowSize-latI][lonI];
    //qDebug() << latO << ":" << lonO;
    //} else {
        float tx = (float)rowSize*latO - latI;
        float tz = (float)(rowSize)*lonO - lonI;
        
        if(latI > rowSize - 1)
            latI = rowSize - 1;
        if(lonI > rowSize - 2)
            lonI = rowSize - 2;
        if(latI < 1)
            latI = 1;
        if(lonI < 0)
            lonI = 0;
        
        return 
            this->terrainData[rowSize-latI][lonI+1]*(1.0 - tx)*(tz) +
            this->terrainData[rowSize-latI-1][lonI]*(tx)*(1.0 - tz) +
            this->terrainData[rowSize-latI-1][lonI+1]*(tx)*(tz) +
            this->terrainData[rowSize-latI][lonI]*(1.0 - tx)*(1.0 - tz) +
                0;
    //}
}
