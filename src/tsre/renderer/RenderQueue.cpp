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

void RenderQueue::setShadowOnly(bool only) {
    shadowOnly = only;
}

void RenderQueue::setShadowDynamic(bool dynamic) {
    shadowDynamic = dynamic;
}

void RenderQueue::addCarBox(const void *car, const float *model, const float *centre, const float *half, int inner,
                            bool detail) {
    CarBox box;
    box.car = car;
    box.inner = inner;
    box.detail = detail;
    float world[3];
    for (int r = 0; r < 3; ++r)
        world[r] = model[r] * centre[0] + model[4 + r] * centre[1] + model[8 + r] * centre[2] + model[12 + r];
    float radius = 0.0f;
    for (int a = 0; a < 3; ++a) {
        float axis[3] = {model[a * 4], model[a * 4 + 1], model[a * 4 + 2]};
        const float scale = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
        if (scale <= 0.0f || half[a] <= 0.0f)
            return;
        const float extent = half[a] * scale;
        for (int k = 0; k < 3; ++k)
            box.toBox[a * 4 + k] = axis[k] / (scale * extent);
        box.toBox[a * 4 + 3] = -(axis[0] * world[0] + axis[1] * world[1] + axis[2] * world[2]) / (scale * extent);
        for (int k = 0; k < 3; ++k)
            box.toWorld[a * 4 + k] = axis[k] / scale * extent;
        radius += extent * extent;
    }
    // A mirrored placement turned back (the box is the same): its faces
    // keep their winding (the lamps' tiles draw back faces).
    const float *c = box.toWorld;
    const float det = c[0] * (c[5] * c[10] - c[6] * c[9]) - c[4] * (c[1] * c[10] - c[2] * c[9])
            + c[8] * (c[1] * c[6] - c[2] * c[5]);
    if (det < 0.0f)
        for (int k = 0; k < 3; ++k)
            box.toWorld[k] = -box.toWorld[k];
    box.toWorld[12] = world[0];
    box.toWorld[13] = world[1];
    box.toWorld[14] = world[2];
    box.toWorld[15] = 1.0f;
    std::copy(world, world + 3, box.sphere);
    box.sphere[3] = std::sqrt(radius);
    carBoxList.push_back(box);
}

void RenderQueue::setAnimated(bool value) {
    animated = value;
}

void RenderQueue::setLightReach(float metres) {
    currentLightReach = metres > 0.0f ? metres : 0.0f;
}

void RenderQueue::noteShapeWaiting() {
    if (shadowCasting && mvMatrix != nullptr)
        waitingShapes.push_back({mvMatrix[12], mvMatrix[13], mvMatrix[14]});
}

void RenderQueue::resetQueueState() {
    waitingShapes.clear();
    currentLayer = LAYER_SCENE;
    shadowCasting = true;
    shadowOnly = false;
    shadowDynamic = false;
    animated = false;
    motionOwner = nullptr;
    motionPart = 0;
    shadowSignatureSum = 0;
    lampOnlySignatureSum = 0;
    currentLampStatic = false;
    currentLampOwner = nullptr;
    carBoxList.clear();
    shadowObjects.clear();
    sceneSignatureSum = 0;
    currentLightReach = 0.0f;
    currentLightsShown = true;
    currentLightsOn = true;
    matrixStackDepth = 0;
}

void RenderQueue::deleteFrameMatrices() {
    for (float *matrix : frameMatrices)
        delete[] matrix;
    frameMatrices.clear();
}
