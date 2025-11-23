/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/objects/TrWatermarkObj.h>
#include <tsre/shape/SFile.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/math3d/GLMatrix.h>
#include <math.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/Game.h>
#include <QDebug>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

TrWatermarkObj::TrWatermarkObj() {
    this->shape = -1;
    this->UiD = 0;
    this->dstLevel = 0;
    this->loaded = false;
}

TrWatermarkObj::TrWatermarkObj(int level) {
    this->UiD = 0;
    this->dstLevel = level;
    this->shape = -1;
    this->loaded = false;
    this->modified = false;
}

TrWatermarkObj::TrWatermarkObj(const TrWatermarkObj& o) : WorldObj(o) {
}

WorldObj* TrWatermarkObj::clone(){
    return new TrWatermarkObj(*this);
}

TrWatermarkObj::~TrWatermarkObj() {
}

void TrWatermarkObj::load(int x, int y) {
    this->x = x;
    this->y = y;
    this->loaded = true;
    this->skipLevel = 1;
}

void TrWatermarkObj::set(QString sh, FileBuffer* data) {
    return;
}

void TrWatermarkObj::render(GLUU* gluu, float lod, float posx, float posz, float* pos, float* target, float fov, int selectionColor, int renderMode) {
    return;
};

int TrWatermarkObj::getDefaultDetailLevel(){
    // object is deprecated when sorting is enabled
    return -99;
}

void TrWatermarkObj::save(QTextStream* out){
    if (!loaded) return;
    if (Game::deleteTrWatermarks) return;
    
*(out) << "	Tr_Watermark ( "<<this->dstLevel<<" )\n";
}