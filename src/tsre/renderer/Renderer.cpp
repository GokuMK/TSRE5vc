/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/renderer/Renderer.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/math3d/GLMatrix.h>

#include <algorithm>

namespace {

QVector<RenderItem*> &retiredPackets() {
    static QVector<RenderItem*> packets;
    return packets;
}

}

int Renderer::queuedPackets = 0;
quint64 Renderer::currentFrame = 0;

Renderer::Renderer() {
    Mat4::identity(ownMvMatrix);
    mvMatrix = ownMvMatrix;
}

Renderer::~Renderer() {
    deleteFrameMatrices();
}

void Renderer::pushItem(RenderItem* r, float* mvmatrix){
    Q_UNUSED(mvmatrix);
    if(r != NULL && !r->shared)
        delete r;
}

// Renderers without native packet support receive the older calls.
void Renderer::pushPacket(RenderItem *packet, quint32 selectionId, SubmitOrder order){
    if(packet == NULL)
        return;
    if(selectionId == 0 && order == SUBMIT_GROUPED){
        pushItemVNTA(packet, mvMatrix);
        return;
    }
    RenderItem *item = new RenderItem(*packet);
    item->shared = false;
    if(selectionId != 0){
        item->setSelectionId(selectionId);
        item->lineWidth = 0;
    }
    pushItem(item, mvMatrix);
}

void Renderer::pushPackets(const QVector<RenderItem*> &packets, quint32 selectionId){
    if(selectionId == 0){
        QVector<RenderItem*> borrowed = packets;
        pushItemsVNTA(borrowed, mvMatrix);
        return;
    }
    for(RenderItem *packet : packets){
        if(packet == NULL)
            continue;
        RenderItem *selectionItem = new RenderItem(*packet);
        selectionItem->shared = false;
        selectionItem->setSelectionId(selectionId);
        selectionItem->lineWidth = 0;
        pushItem(selectionItem, mvMatrix);
    }
}

void Renderer::pushItemsVNTA(QVector<RenderItem*>& r, float* mvmatrix){
    Q_UNUSED(r);
    Q_UNUSED(mvmatrix);
}

void Renderer::pushItemVNTA(RenderItem* r, float* mvmatrix){
    Q_UNUSED(r);
    Q_UNUSED(mvmatrix);
}

void Renderer::mvPushMatrix() {
    if (matrixStackDepth == matrixStack.size())
        matrixStack.push_back({});
    std::copy(mvMatrix, mvMatrix + 16, matrixStack[matrixStackDepth].begin());
    matrixStackDepth++;
}

void Renderer::mvPopMatrix() {
    if (matrixStackDepth == 0)
        return;
    matrixStackDepth--;
    std::copy(matrixStack[matrixStackDepth].begin(),
              matrixStack[matrixStackDepth].end(), mvMatrix);
}

// A frame may flush several times; the matrix stack carries across flushes.
void Renderer::renderFrame(){
    deleteFrameMatrices();
    releaseRetiredPackets();
}

void Renderer::resetFrame(){
    currentFrame++;
    resetMatrixStack();
    deleteFrameMatrices();
    releaseRetiredPackets();
}

void Renderer::resetMatrixStack(){
    matrixStackDepth = 0;
}

void Renderer::deleteFrameMatrices(){
    for(float *matrix : mvMatrixDelete)
        delete[] matrix;
    mvMatrixDelete.clear();
}

void Renderer::retirePacket(RenderItem *packet){
    if(packet == NULL)
        return;
    if(queuedPackets == 0){
        delete packet;
        return;
    }
    retiredPackets().push_back(packet);
}

quint64 Renderer::frameNumber(){
    return currentFrame;
}

int Renderer::pendingRetiredPackets(){
    return retiredPackets().size();
}

void Renderer::releaseRetiredPackets(){
    if(queuedPackets != 0)
        return;
    QVector<RenderItem*> &packets = retiredPackets();
    for(RenderItem *packet : packets)
        delete packet;
    packets.clear();
}
