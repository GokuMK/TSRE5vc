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
class GLUU;

class OpenGL3Renderer : public Renderer {
public:
    OpenGL3Renderer();
    virtual ~OpenGL3Renderer();
    void renderPasses(RenderPass first, RenderPass last) override;
    void renderShadowCasters(float range, int statsSlot) override;
    void renderFrame() override;
    void resetFrame() override;
    void pushItem(RenderItem *r, float* mvmatrix) override;
    void pushPacket(RenderItem *packet, quint32 selectionId = 0,
                    SubmitOrder order = SUBMIT_GROUPED) override;
    void pushPackets(const QVector<RenderItem*> &packets, quint32 selectionId = 0) override;
    void pushItemsVNTA(QVector<RenderItem*>& r, float* mvmatrix) override;
    void pushItemVNTA(RenderItem *r, float* mvmatrix) override;

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
    const float *frameMatrix(quint32 index) const;
    RenderPass routePass(const RenderItem *packet, SubmitOrder order) const;
    bool castsShadow(const RenderItem *packet) const;
    void instanceOrigin(const DrawInstance &instance, float *origin) const;
    void queueInstance(RenderItem *packet, const float *matrix, quint32 selectionId,
                       SubmitOrder order, bool owned);
    void sortByTexture(std::vector<DrawInstance> &instances);
    void sortBackToFront(std::vector<DrawInstance> &instances);
    void drawOrdered(GLUU *gluu, const std::vector<DrawInstance> &instances, int pass);
    void drawGrouped(GLUU *gluu, const std::vector<DrawInstance> &instances, int pass);
    void consumePass(PassQueue &queue);
    void clearQueues();

    // Frame storage is cleared, not freed, so steady frames do not allocate.
    PassQueue passes[PASS_COUNT];
    std::vector<float> frameMatrices;
    std::vector<quint32> sortOrder;
    std::vector<RenderItem*> ownedItems;
    quint32 nextOrder = 0;
    QOpenGLFunctions *f = nullptr;
};

#endif /* OPENGL3RENDERER_H */
