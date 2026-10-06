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
#include <cmath>

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

void Renderer::renderLayeredView(const LayeredView &view){
    secondaryView = true;
    beginViewBand(view, BAND_SKY);
    renderPassesRetained(PASS_SKY, PASS_SKY);
    beginViewBand(view, BAND_DISTANT);
    renderPassesRetained(PASS_DISTANT, PASS_DISTANT);
    beginViewBand(view, BAND_SCENE);
    renderPassesRetained(PASS_TERRAIN, PASS_BLENDED);
    if(view.water)
        renderPassesRetained(PASS_WATER, PASS_WATER);
    renderPassesRetained(PASS_TRANSMISSION, PASS_TRANSMISSION);
    endView(view);
    secondaryView = false;
}

void Renderer::setViewPosition(const float *position){
    std::copy(position, position + 3, viewPosition);
}

void Renderer::setCullView(const float *viewProjection){
    cullFrustum = frustumOf(viewProjection);
}

void Renderer::setViewLimits(const ViewLimits *limits){
    viewLimitsEnabled = limits != nullptr;
    viewLimits = limits != nullptr ? *limits : ViewLimits();
}

Renderer::Frustum Renderer::frustumOf(const float *m){
    Frustum frustum;
    if(m == nullptr)
        return frustum;
    // Rows of the column-major matrix; planes are row 3 +/- rows 0..2.
    const float r[4][4] = {{m[0], m[4], m[8], m[12]}, {m[1], m[5], m[9], m[13]},
                           {m[2], m[6], m[10], m[14]}, {m[3], m[7], m[11], m[15]}};
    for(int i = 0; i < 6; ++i){
        const float sign = (i % 2) == 0 ? 1.0f : -1.0f;
        float *plane = frustum.planes[i];
        float length = 0.0f;
        for(int c = 0; c < 4; ++c)
            plane[c] = r[3][c] + sign * r[i / 2][c];
        for(int c = 0; c < 3; ++c)
            length += plane[c] * plane[c];
        length = std::sqrt(length);
        if(length <= 0.0f)
            return Frustum();
        for(int c = 0; c < 4; ++c)
            plane[c] /= length;
    }
    frustum.enabled = true;
    return frustum;
}

bool Renderer::intersects(const Frustum &frustum, const float *center, float radius){
    if(!frustum.enabled)
        return true;
    for(const auto &plane : frustum.planes){
        if(plane[0] * center[0] + plane[1] * center[1] + plane[2] * center[2] + plane[3] < -radius)
            return false;
    }
    return true;
}

void Renderer::resetFrame(){
    currentFrame++;
    cullFrustum = Frustum();
    viewLimitsEnabled = false;
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
