/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/Skydome.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/Game.h>

Skydome::Skydome() {
    QString resPath = Game::root + "/ROUTES/" + Game::route + "/SHAPES";
    int shape = Game::currentShapeLib->addShape(resPath +"/skydome.s");
    this->shapePointer = Game::currentShapeLib->shape[shape];
    if(this->shapePointer == NULL)
        return;
    loaded = true;
}

Skydome::Skydome(const Skydome& orig) {
}

Skydome::~Skydome() {
}

void Skydome::pushRenderItems(RenderQueue &queue) {
    if (!loaded || shapePointer == NULL)
        return;
    shapePointer->pushRenderItem(queue, 0, 0);
}
