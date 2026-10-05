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
    // View-projection the following passes are culled against: an instance
    // whose bounds lie outside it is skipped (clip position = viewProjection
    // * transform * msMatrix * vertex). Null draws everything; a new frame
    // starts without culling. Packets without bounds are never culled.
    void setCullView(const float *viewProjection);
    // Limits of a secondary view such as an environment map face: instances
    // other than terrain whose bounds lie farther than maxDistance from the
    // view position, or subtend less than minAngularRadius (radius over
    // distance), are skipped. Null removes them; a new frame starts without.
    struct ViewLimits {
        float maxDistance = 0.0f;
        float minAngularRadius = 0.0f;
    };
    void setViewLimits(const ViewLimits *limits);
    // Draws queued shadow casters with the bound shader without consuming
    // them, skipping instances whose origin lies farther than range from the
    // view position on the ground plane, and, with a light view-projection,
    // instances outside it. statsSlot labels the draws.
    virtual void renderShadowCasters(float range, int statsSlot,
                                     const float *viewProjection = nullptr) = 0;
    // Draws and consumes queued work of passes first..last; later passes stay
    // queued. Use it where direct drawing must happen between passes.
    virtual void renderPasses(RenderPass first, RenderPass last) = 0;
    // Draws queued work of passes first..last without consuming it, for an
    // extra view of the same frame (environment map faces).
    virtual void renderPassesRetained(RenderPass first, RenderPass last) = 0;
    // Bounds centre (submission space) of the queued instance of a pass that
    // is nearest to the view position and inside the view-projection; false
    // when there is none. Instances without bounds are not considered.
    virtual bool nearestVisible(RenderPass pass, const float *viewProjection,
                                float *center) const = 0;
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

    // Normalised clip planes of a view-projection; disabled draws everything.
    struct Frustum {
        float planes[6][4];
        bool enabled = false;
    };
    static Frustum frustumOf(const float *viewProjection);
    // Whether a sphere in submission space can be inside the frustum.
    static bool intersects(const Frustum &frustum, const float *center, float radius);
    Frustum cullFrustum;
    ViewLimits viewLimits;
    bool viewLimitsEnabled = false;
};

#endif /* RENDERER_H */
