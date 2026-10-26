/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "TextObj.h"
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/renderer/Renderer.h>

TextObj::TextObj(int val, float s, float sc, int resm) : OglObj() {
    this->text.setNum(val, 10);
    if(s > 0)
        size = s;
    if(sc > 0)
        scale = sc;
    pos[0] = 0;
    pos[1] = 0;
    pos[2] = 0;
    resMult = resm;
}

TextObj::TextObj(QString val, float s, float sc, int resm) : OglObj() {
    this->text = val;
    if(s > 0)
        size = s;
    if(sc > 0)
        scale = sc;
    pos[0] = 0;
    pos[1] = 0;
    pos[2] = 0;
    resMult = resm;
}

TextObj::TextObj() : OglObj() {
    this->text = " ";
    pos[0] = 0;
    pos[1] = 0;
    pos[2] = 0;
}

void TextObj::setColor(int r, int g, int b){
    color.setRgb(r, g, b);
}

void TextObj::setOColor(int r, int g, int b){
    ocolor.setRgb(r, g, b);
    isOutline = true;
}

void TextObj::setFontName(QString val){
    fontName = val;
}

void TextObj::setRotOffset(float val){
    rotOffset = val;
}

namespace {
// The digit texture: the digits 0 to 9 a cell each, DigitCell pixels wide
// and LabelHeight high (a label's texture height, so digits keep the size
// they had painted a label each at 96 dpi). The digits are drawn 32 px high
// whatever the screen: an Arial digit (or a metric-compatible one) advances
// 17.8 px. Painted labels follow the screen's dpi (24 pt): 80 px at 240 dpi,
// cut by their 32 px texture.
constexpr int DigitCell = 18;
constexpr int LabelHeight = 32;
}

bool TextObj::initDigits(){
    if (text.isEmpty() || isOutline || !fontName.isEmpty() || resMult != 1)
        return false;
    for (const QChar c : std::as_const(text))
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return false;
    GLUU* gluu = GLUU::get();
    const float alpha = -gluu->alphaTest;
    // Centred: n cells across, half on each side of the label's place.
    const int n = int(text.size());
    const float cell = float(DigitCell) / LabelHeight * scale;
    const float left = -0.5f * n * cell;
    float *punkty = new float[36 * n];
    int ptr = 0;
    auto vertex = [&](float x, float y, float u, float v) {
        punkty[ptr++] = x;
        punkty[ptr++] = y;
        punkty[ptr++] = 0;
        punkty[ptr++] = u;
        punkty[ptr++] = v;
        punkty[ptr++] = alpha;
    };
    for (int k = 0; k < n; ++k) {
        const int digit = text[k].unicode() - '0';
        const float x0 = left + k * cell, x1 = x0 + cell;
        const float u0 = digit / 10.0f, u1 = (digit + 1) / 10.0f;
        vertex(x0, 1 * scale, u0, 0.0f);
        vertex(x0, 0, u0, 1.0f);
        vertex(x1, 0, u1, 1.0f);
        vertex(x1, 1 * scale, u1, 0.0f);
        vertex(x0, 1 * scale, u0, 0.0f);
        vertex(x1, 0, u1, 1.0f);
    }
    this->setMaterial(new QString(QStringLiteral("0123456789.cells:10.cellw:%1.color:%2.:paintTex")
                                  .arg(DigitCell).arg(color.name())));
    OglObj::init(punkty, ptr, RenderItem::VT, GL_TRIANGLES);
    delete[] punkty;
    isInit = true;
    return true;
}

void TextObj::init(){
    if (initDigits())
        return;
    GLUU* gluu = GLUU::get();
    float *punkty = new float[54];
    int ptr = 0;
    int i = 0;

    float alpha = -gluu->alphaTest;
    
    punkty[ptr++] = (-size / 2)*scale;
    punkty[ptr++] = 1*scale;
    punkty[ptr++] = 0;
    punkty[ptr++] = 0.0;
    punkty[ptr++] = 0.0;
    punkty[ptr++] = alpha;

    punkty[ptr++] = (-size / 2)*scale;
    punkty[ptr++] = 0;
    punkty[ptr++] = 0;
    punkty[ptr++] = 0.0;
    punkty[ptr++] = 1.00;
    punkty[ptr++] = alpha;

    punkty[ptr++] = (size / 2)*scale;
    punkty[ptr++] = 0;
    punkty[ptr++] = 0;
    punkty[ptr++] = 1.0;
    punkty[ptr++] = 1.0;
    punkty[ptr++] = alpha;

    punkty[ptr++] = (size / 2)*scale;
    punkty[ptr++] = 1*scale;
    punkty[ptr++] = 0;
    punkty[ptr++] = 1.0;
    punkty[ptr++] = 0.0;
    punkty[ptr++] = alpha;

    punkty[ptr++] = (-size / 2)*scale;
    punkty[ptr++] = 1*scale;
    punkty[ptr++] = 0;
    punkty[ptr++] = 0.0;
    punkty[ptr++] = 0.0;
    punkty[ptr++] = alpha;

    punkty[ptr++] = (size / 2)*scale;
    punkty[ptr++] = 0;
    punkty[ptr++] = 0;
    punkty[ptr++] = 1.0;
    punkty[ptr++] = 1.0;
    punkty[ptr++] = alpha;
    QString args;
    if(resMult > 1)
        args += ".resm:"+QString::number((int)resMult);
    if(fontName.length() > 0)
        args += ".font:"+fontName;
    if(isOutline)
        args += ".ocolor:"+ocolor.name();
    args += ".size:"+QString::number((int)size);
    args += ".color:"+color.name();
    this->setMaterial(new QString(text+args+".:paintTex"));
    OglObj::init(punkty, ptr, RenderItem::VT, GL_TRIANGLES);
    delete[] punkty;
    isInit = true;
}

TextObj::TextObj(const TextObj& orig) {
}

TextObj::~TextObj() {
}

void TextObj::pushRenderItem(RenderQueue &queue) {
    pushRenderItem(queue, 0);
}

void TextObj::pushRenderItem(RenderQueue &queue, float rot) {
    if(!isInit)
        init();

    queue.pushTransform();
    Mat4::translate(queue.transform(), queue.transform(), pos[0], pos[1], pos[2]);
    Mat4::rotateY(queue.transform(), queue.transform(), rot+rotOffset);

    OglObj::pushRenderItem(queue);

    queue.popTransform();
}

