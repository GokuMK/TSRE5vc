/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef QUEUERENDERER_H
#define QUEUERENDERER_H

#include <tsre/renderer/LightGrid.h>
#include <tsre/renderer/Renderer.h>
#include <vector>

// The backend-neutral part of a renderer: it keeps the submitted work of a
// frame in passes, culls and sorts it, and plans instanced draws and shadow
// casters. Backends draw what it plans.
class QueueRenderer : public Renderer {
public:
    void resetFrame() override;
    using RenderQueue::submit;
    void submit(RenderItem *packet, quint32 selectionId = 0,
                SubmitOrder order = SUBMIT_GROUPED) override;
    void submit(const QVector<RenderItem*> &packets, quint32 selectionId = 0) override;
    void submitFrameItem(RenderItem *item) override;
    void visibleBounds(RenderPass pass, const float *viewProjection,
                       std::vector<float> &spheres) const override;

    // Opaque, alpha-test and overlay packets are grouped by texture and
    // packet for fewer state changes; false draws them in submission order.
    bool groupByTexture = true;

    // Ordered work queued in a pass since it was last drawn.
    int queuedItemCount(RenderPass pass = PASS_OPAQUE) const {
        return static_cast<int>(passes[pass].ordered.size());
    }
    const RenderItem *queuedItem(int index, RenderPass pass = PASS_OPAQUE) const {
        return passes[pass].ordered[index].packet;
    }
    quint32 queuedSelectionId(int index, RenderPass pass = PASS_OPAQUE) const {
        return passes[pass].ordered[index].selectionId;
    }
    // Grouped packets queued in a pass since it was last drawn.
    int queuedGroupedCount(RenderPass pass) const {
        return static_cast<int>(passes[pass].grouped.size());
    }

    // One queued draw of a packet for this frame.
    struct DrawInstance {
        RenderItem *packet = nullptr;
        quint32 matrix = 0;
        quint32 selectionId = 0;
        quint32 order = 0;
        quint32 textureRank = 0;
        quint32 packetRank = 0;
        float distance = 0.0f;
        quint8 category = 0;
        bool owned = false;
        bool castsShadow = false;
    };

protected:
    // Ordered work is drawn first, in submission order; grouped packets after.
    struct PassQueue {
        std::vector<DrawInstance> ordered;
        std::vector<DrawInstance> grouped;
    };
    // A run of instances of one packet. base >= 0 marks an instanced draw of
    // `visible` instances starting at that row of instanceUpload.
    struct GroupPlan {
        size_t begin = 0;
        size_t end = 0;
        int visible = 0;
        int base = -1;
    };

    // Sort key grouping packets of one texture.
    static quint64 textureKey(const RenderItem *item);
    static bool hasMesh(const RenderItem *item);
    quint32 captureMatrix(const float *matrix);
    const float *instanceMatrix(quint32 index) const;
    RenderPass routePass(const RenderItem *packet, SubmitOrder order) const;
    bool castsShadow(const RenderItem *packet) const;
    void instanceOrigin(const DrawInstance &instance, float *origin) const;
    // An instance's bounding sphere in submission space; false without bounds.
    bool instanceBounds(const DrawInstance &instance, float *center, float &radius) const;
    // Whether an instance's bounds can be inside the frustum; counts culls.
    bool visible(const DrawInstance &instance, const Frustum &frustum) const;
    void queueInstance(RenderItem *packet, const float *matrix, quint32 selectionId,
                       SubmitOrder order, bool owned);
    void sortByTexture(std::vector<DrawInstance> &instances);
    void sortBackToFront(std::vector<DrawInstance> &instances);
    void consumePass(PassQueue &queue);
    void clearQueues();

    // Whether instance rows first..first+count fit the backend's instance
    // storage.
    virtual bool instanceRowsFit(int first, int count) = 0;
    // Plans a grouped pass: culls each instance once (instanceVisible) and
    // gives runs of two or more visible instances of a packet with one
    // selection ID an instanced draw (groupPlans, instanceUpload).
    void planGroups(const std::vector<DrawInstance> &instances);
    // Shadow casters within range on the ground plane (measured to their
    // bounds) and inside the light view, grouped by packet: shadowCasters,
    // groupPlans and instanceUpload.
    void planShadowCasters(float range, const float *viewProjection);
    // The lights of every queued instance in submission space, punctual
    // lights scaled by exposure and emissive ones by emissiveGain, with
    // ranges derived where missing (task 21). emittersOnly keeps only the
    // emissive ones, also those too dim to light anything (their glow,
    // task 24), with signal lights scaled by signalGain (task 26); signal
    // lights are left out otherwise.
    void gatherLights(std::vector<LightGrid::Light> &lights, float exposure,
                      float emissiveGain, bool emittersOnly = false, float signalGain = 0.0f) const;

    std::vector<GroupPlan> groupPlans;
    std::vector<const DrawInstance *> shadowCasters;
    std::vector<char> instanceVisible;
    std::vector<float> instanceUpload;
    // Frame storage is cleared, not freed, so steady frames do not allocate.
    PassQueue passes[PASS_COUNT];
    std::vector<float> instanceMatrices;
    std::vector<quint32> sortOrder;
    std::vector<RenderItem*> ownedItems;
    quint32 nextOrder = 0;
};

#endif /* QUEUERENDERER_H */
