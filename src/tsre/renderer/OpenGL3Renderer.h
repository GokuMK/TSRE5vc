/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */


#ifndef OPENGL3RENDERER_H
#define OPENGL3RENDERER_H

#include <tsre/renderer/Renderer.h>
#include <vector>

class QOpenGLFunctions;
class QOpenGLContext;
class GLUU;
class Shader;

class OpenGL3Renderer : public Renderer {
public:
    OpenGL3Renderer();
    virtual ~OpenGL3Renderer();
    void renderPasses(RenderPass first, RenderPass last) override;
    void renderShadowCasters(float range, int statsSlot,
                             const float *viewProjection = nullptr) override;
    void renderFrame() override;
    void resetFrame() override;
    using RenderQueue::submit;
    void submit(RenderItem *packet, quint32 selectionId = 0,
                SubmitOrder order = SUBMIT_GROUPED) override;
    void submit(const QVector<RenderItem*> &packets, quint32 selectionId = 0) override;
    void submitFrameItem(RenderItem *item) override;

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

private:
    // Ordered work is drawn first, in submission order; grouped packets after.
    struct PassQueue {
        std::vector<DrawInstance> ordered;
        std::vector<DrawInstance> grouped;
    };

    quint32 captureMatrix(const float *matrix);
    const float *instanceMatrix(quint32 index) const;
    RenderPass routePass(const RenderItem *packet, SubmitOrder order) const;
    bool castsShadow(const RenderItem *packet) const;
    void instanceOrigin(const DrawInstance &instance, float *origin) const;
    // Whether an instance's bounds can be inside the frustum; counts culls.
    bool visible(const DrawInstance &instance, const Frustum &frustum) const;
    void queueInstance(RenderItem *packet, const float *matrix, quint32 selectionId,
                       SubmitOrder order, bool owned);
    void sortByTexture(std::vector<DrawInstance> &instances);
    void sortBackToFront(std::vector<DrawInstance> &instances);
    void drawOrdered(GLUU *gluu, Shader *base, const std::vector<DrawInstance> &instances,
                     int pass);
    void drawGrouped(GLUU *gluu, Shader *base, const std::vector<DrawInstance> &instances,
                     int pass);
    void consumePass(PassQueue &queue);
    void clearQueues();
    // Uploads instanceUpload to the instance buffer texture on unit 8.
    bool uploadInstances();
    void releaseInstanceBuffer();

    // A run of instances of one packet in a grouped pass. base >= 0 marks an
    // instanced draw of count instances starting at that buffer row.
    struct GroupPlan {
        size_t begin = 0;
        size_t end = 0;
        int visible = 0;
        int base = -1;
    };
    std::vector<GroupPlan> groupPlans;
    std::vector<char> instanceVisible;
    std::vector<float> instanceUpload;
    unsigned int instanceBuffer = 0;
    unsigned int instanceTexture = 0;
    QOpenGLContext *instanceContext = nullptr;
    int maxInstanceTexels = 0;

    // Frame storage is cleared, not freed, so steady frames do not allocate.
    PassQueue passes[PASS_COUNT];
    std::vector<float> instanceMatrices;
    std::vector<quint32> sortOrder;
    std::vector<RenderItem*> ownedItems;
    quint32 nextOrder = 0;
    QOpenGLFunctions *f = nullptr;
};

#endif /* OPENGL3RENDERER_H */
