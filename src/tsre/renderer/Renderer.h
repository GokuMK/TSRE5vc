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
//   stay valid while the packet is alive; a null msMatrix means identity.
// - A packet's fields may be refreshed when it is submitted (for example a
//   texture that finished loading), but must not change between submissions
//   in the same frame. Producers that draw one object with different
//   materials in a frame use one packet per material; frameNumber() tells
//   them when a new frame starts.
class Renderer {
public:
    enum RenderMode {
        RENDER_DEFAULT = 0,
        RENDER_SELECTION = 1,
        RENDER_SHADOWMAP = 2
    };
    // Grouped packets are batched by texture; ordered ones keep submission
    // order with frame-owned items (overlays, decals, helper geometry).
    enum SubmitOrder {
        SUBMIT_GROUPED = 0,
        SUBMIT_ORDERED = 1
    };
    // Passes draw in this order. The renderer routes each submission from the
    // packet surface, the submission order and the current layer: terrain
    // packets go to PASS_TERRAIN; ordered scene work to PASS_OPAQUE before its
    // batched packets; grouped packets by surface; the overlay layer to
    // PASS_OVERLAY.
    enum RenderPass {
        PASS_TERRAIN = 0,
        PASS_OPAQUE,
        PASS_ALPHA_TEST,
        PASS_BLENDED,
        PASS_OVERLAY,
        PASS_COUNT
    };
    enum Layer {
        LAYER_SCENE = 0,
        LAYER_OVERLAY = 1
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
    virtual void pushPacket(RenderItem *packet, quint32 selectionId = 0,
                            SubmitOrder order = SUBMIT_GROUPED);
    virtual void pushPackets(const QVector<RenderItem*> &packets, quint32 selectionId = 0);
    // Compatibility wrappers for pushPackets().
    virtual void pushItemVNTA(RenderItem *r, float* mvmatrix);
    virtual void pushItemsVNTA(QVector<RenderItem*> &r, float* mvmatrix);

    void mvPushMatrix();
    void mvPopMatrix();
    // Layer for following submissions; resetFrame() returns to LAYER_SCENE.
    void setLayer(Layer layer);
    Layer layer() const;
    // Camera position in the submission space, for back-to-front sorting.
    void setViewPosition(const float *position);
    // Draws and consumes queued work of passes first..last; later passes stay
    // queued. Use it where direct drawing must happen between passes.
    virtual void renderPasses(RenderPass first, RenderPass last);
    // Draws all remaining passes and ends the frame's submissions.
    virtual void renderFrame();
    // Starts a frame: drops queued work and rebalances the matrix stack.
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
    void deleteFrameMatrices();
    void resetMatrixStack();
    Layer currentLayer = LAYER_SCENE;
    float viewPosition[3] = {0, 0, 0};

private:
    float ownMvMatrix[16];
    QVector<std::array<float, 16>> matrixStack;
    int matrixStackDepth = 0;
};

#endif /* RENDERER_H */
