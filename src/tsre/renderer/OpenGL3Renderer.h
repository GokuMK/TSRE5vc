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

class OpenGL3Renderer : public Renderer {
public:
    OpenGL3Renderer();
    virtual ~OpenGL3Renderer();
    void renderFrame() override;
    void resetFrame() override;
    void pushItem(RenderItem *r, float* mvmatrix) override;
    void pushPacket(RenderItem *packet, quint32 selectionId = 0,
                    SubmitOrder order = SUBMIT_GROUPED) override;
    void pushPackets(const QVector<RenderItem*> &packets, quint32 selectionId = 0) override;
    void pushItemsVNTA(QVector<RenderItem*>& r, float* mvmatrix) override;
    void pushItemVNTA(RenderItem *r, float* mvmatrix) override;

    // Packets are grouped by texture and packet for fewer state changes;
    // false draws them in submission order.
    bool groupByTexture = true;

    // Ordered work queued since the last flush, in submission order.
    int queuedItemCount() const { return static_cast<int>(orderedItems.size()); }
    const RenderItem *queuedItem(int index) const { return orderedItems[index].packet; }
    quint32 queuedSelectionId(int index) const { return orderedItems[index].selectionId; }

    // One queued draw of a packet for this frame.
    struct DrawInstance {
        RenderItem *packet = nullptr;
        quint32 matrix = 0;
        quint32 selectionId = 0;
        quint32 order = 0;
        quint32 textureRank = 0;
        quint32 packetRank = 0;
        quint8 category = 0;
    };

private:
    quint32 captureMatrix(const float *matrix);
    const float *frameMatrix(quint32 index) const;
    void queuePacket(RenderItem *packet, const float *matrix, quint32 selectionId,
                     SubmitOrder order = SUBMIT_GROUPED);
    void sortPackets();
    void clearQueues();

    // Frame storage is cleared, not freed, so steady frames do not allocate.
    std::vector<float> frameMatrices;
    std::vector<DrawInstance> orderedItems;
    std::vector<DrawInstance> packets;
    std::vector<quint32> packetOrder;
    std::vector<RenderItem*> ownedItems;
    // Borrowed packets in orderedItems, for retirement accounting.
    int orderedPackets = 0;
    quint32 nextOrder = 0;
    QOpenGLFunctions *f = nullptr;
};

#endif /* OPENGL3RENDERER_H */
