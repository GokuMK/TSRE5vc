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

#include <QVector>
#include <QtGlobal>
#include <array>

class RenderItem;

// Collects a frame's draw work from producers and submits it in renderFrame().
//
// Submission contract:
// - pushPackets() borrows persistent packets owned by the producer. The
//   renderer records an instance (packet, current mvMatrix, selection ID) and
//   never modifies the packet. Producers release packets with retirePacket(),
//   never with delete, so queued instances stay valid until the next flush.
// - pushItem() takes ownership of a frame-owned item (a shared item is
//   copied) and deletes it after the flush. It is the compatibility path for
//   producers that still build items every frame.
// - mvMatrix values are copied at submission; producers may change or reuse
//   their matrices immediately afterwards. A packet's msMatrix pointer must
//   stay valid while the packet is alive.
class Renderer {
public:
    enum RenderMode {
        RENDER_DEFAULT = 0,
        RENDER_SELECTION = 1,
        RENDER_SHADOWMAP = 2
    };
    float* objStrMatrix = NULL;
    // Current model-view matrix. Producers transform it in place; the pointer
    // stays stable across mvPushMatrix()/mvPopMatrix().
    float* mvMatrix = NULL;
    // Producer-allocated matrices deleted at the end of the frame.
    QVector<float*> mvMatrixDelete;
    Renderer();
    Renderer(const Renderer& orig) = delete;
    Renderer& operator=(const Renderer& orig) = delete;
    virtual ~Renderer();

    virtual void pushItem(RenderItem *r, float* mvmatrix);
    virtual void pushPackets(const QVector<RenderItem*> &packets, quint32 selectionId = 0);
    // Compatibility wrappers for pushPackets().
    virtual void pushItemVNTA(RenderItem *r, float* mvmatrix);
    virtual void pushItemsVNTA(QVector<RenderItem*> &r, float* mvmatrix);

    void mvPushMatrix();
    void mvPopMatrix();
    virtual void renderFrame();
    // Drops queued work without drawing it.
    virtual void resetFrame();

    // Deletes a producer-owned packet once no renderer can still draw it.
    static void retirePacket(RenderItem *packet);
    static int pendingRetiredPackets();

protected:
    // Borrowed packets queued in all renderers; retirement waits for zero.
    static int queuedPackets;
    static void releaseRetiredPackets();
    void deleteFrameMatrices();
    void resetMatrixStack();

private:
    float ownMvMatrix[16];
    QVector<std::array<float, 16>> matrixStack;
    int matrixStackDepth = 0;
};

#endif /* RENDERER_H */
