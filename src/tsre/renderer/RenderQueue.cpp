/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/renderer/RenderQueue.h>
#include <tsre/math3d/GLMatrix.h>

#include <algorithm>

RenderQueue::RenderQueue() {
    Mat4::identity(ownMvMatrix);
    mvMatrix = ownMvMatrix;
}

RenderQueue::~RenderQueue() {
    deleteFrameMatrices();
}

void RenderQueue::pushTransform() {
    if (matrixStackDepth == matrixStack.size())
        matrixStack.push_back({});
    std::copy(mvMatrix, mvMatrix + 16, matrixStack[matrixStackDepth].begin());
    matrixStackDepth++;
}

void RenderQueue::popTransform() {
    if (matrixStackDepth == 0)
        return;
    matrixStackDepth--;
    std::copy(matrixStack[matrixStackDepth].begin(),
              matrixStack[matrixStackDepth].end(), mvMatrix);
}

float *RenderQueue::frameMatrix(const float *matrix) {
    float *copy = new float[16];
    std::copy(matrix, matrix + 16, copy);
    frameMatrices.push_back(copy);
    return copy;
}

void RenderQueue::setLayer(Layer layer) {
    currentLayer = layer;
}

RenderQueue::Layer RenderQueue::layer() const {
    return currentLayer;
}

void RenderQueue::setShadowCasting(bool cast) {
    shadowCasting = cast;
}

void RenderQueue::resetQueueState() {
    currentLayer = LAYER_SCENE;
    shadowCasting = true;
    matrixStackDepth = 0;
}

void RenderQueue::deleteFrameMatrices() {
    for (float *matrix : frameMatrices)
        delete[] matrix;
    frameMatrices.clear();
}
