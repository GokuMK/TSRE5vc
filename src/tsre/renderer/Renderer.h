/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RENDERER_H
#define RENDERER_H

#include <tsre/renderer/RenderQueue.h>

// A render queue that also draws: the frame owner gathers producers into it,
// then draws the queued work pass by pass. Producers only see RenderQueue.
class Renderer : public RenderQueue {
public:
    // Passes draw in this order. The renderer routes each submission from the
    // current layer, the packet surface and the submission order. In the
    // scene layer terrain packets go to PASS_TERRAIN, ordered work to
    // PASS_OPAQUE before its batched packets, and grouped packets by surface.
    // Every other layer maps to its own pass. The frame sets the projection
    // and clears depth between sky, distant and scene passes.
    enum RenderPass {
        PASS_SKY = 0,
        PASS_DISTANT,
        PASS_TERRAIN,
        PASS_OPAQUE,
        PASS_ALPHA_TEST,
        PASS_BLENDED,
        PASS_OVERLAY,
        PASS_WATER,
        PASS_UI,
        PASS_COUNT
    };

    Renderer();
    virtual ~Renderer();

    // Camera position in the submission space, for back-to-front sorting and
    // shadow caster range.
    void setViewPosition(const float *position);
    // Draws queued shadow casters with the bound shader without consuming
    // them, skipping instances whose origin lies farther than range from the
    // view position on the ground plane. statsSlot labels the draws.
    virtual void renderShadowCasters(float range, int statsSlot) = 0;
    // Draws and consumes queued work of passes first..last; later passes stay
    // queued. Use it where direct drawing must happen between passes.
    virtual void renderPasses(RenderPass first, RenderPass last) = 0;
    // Draws all remaining passes and ends the frame's submissions.
    virtual void renderFrame();
    // Starts a frame: drops queued work and rebalances the transform stack.
    virtual void resetFrame();
    // Increments at every resetFrame() of any renderer.
    static quint64 frameNumber();

    // Deletes a producer-owned packet once no renderer can still draw it.
    static void retirePacket(RenderItem *packet);
    static int pendingRetiredPackets();

protected:
    // Borrowed packets queued in all renderers; retirement waits for zero.
    static int queuedPackets;
    static quint64 currentFrame;
    static void releaseRetiredPackets();
    float viewPosition[3] = {0, 0, 0};
};

#endif /* RENDERER_H */
