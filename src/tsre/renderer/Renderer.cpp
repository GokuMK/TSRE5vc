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
}

Renderer::~Renderer() {
}

// A frame may flush several times; the transform stack carries across flushes.
void Renderer::renderFrame(){
    deleteFrameMatrices();
    releaseRetiredPackets();
}

void Renderer::setViewPosition(const float *position){
    std::copy(position, position + 3, viewPosition);
}

void Renderer::resetFrame(){
    currentFrame++;
    resetQueueState();
    deleteFrameMatrices();
    releaseRetiredPackets();
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
