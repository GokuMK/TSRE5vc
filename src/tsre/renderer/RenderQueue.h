/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RENDERQUEUE_H
#define RENDERQUEUE_H

#include <QVector>
#include <QtGlobal>
#include <array>

class RenderItem;

// What producers see while a frame is gathered. Objects submit their packets
// to the queue passed to their pushRenderItems() functions; they never draw
// and never hold on to the queue.
//
// Submission contract:
// - submit() borrows persistent packets owned by the producer. The queue
//   records an instance (packet, current transform, selection ID) and never
//   modifies the packet. Producers release packets with
//   Renderer::retirePacket(), never with delete, so queued instances stay
//   valid until the frame is drawn.
// - submitFrameItem() takes ownership of an item built for this frame only
//   (a shared item is copied) and deletes it after drawing.
// - The transform is copied at submission; producers may change it right
//   afterwards. A packet's msMatrix pointer must stay valid while the packet
//   is alive (frameMatrix() gives one valid for the frame); a null msMatrix
//   means identity.
// - A packet's fields may be refreshed when it is submitted (for example a
//   texture that finished loading), but must not change between submissions
//   in the same frame. Producers that draw one object with different
//   materials in a frame use one packet per material; Renderer::frameNumber()
//   tells them when a new frame starts.
// - Retirement protects the RenderItem only. A producer must not destroy or
//   rebuild a queued packet's VAO, VBO or textures before the frame ends;
//   change geometry before submitting it.
class RenderQueue {
public:
    enum RenderMode {
        RENDER_DEFAULT = 0,
        RENDER_SELECTION = 1
    };
    // Grouped packets are batched by texture; ordered ones keep submission
    // order with frame-owned items (overlays, decals, helper geometry).
    enum SubmitOrder {
        SUBMIT_GROUPED = 0,
        SUBMIT_ORDERED = 1
    };
    // What the following submissions belong to. Each layer except the scene
    // draws in its own pass; see Renderer::RenderPass.
    enum Layer {
        LAYER_SCENE = 0,
        LAYER_SKY,
        LAYER_DISTANT,
        LAYER_OVERLAY,
        LAYER_WATER,
        LAYER_UI
    };

    RenderQueue();
    RenderQueue(const RenderQueue &orig) = delete;
    RenderQueue &operator=(const RenderQueue &orig) = delete;
    virtual ~RenderQueue();

    // Current model-view transform, changed in place with the Mat4 functions.
    // The pointer stays valid across pushTransform()/popTransform().
    float *transform() { return mvMatrix; }
    const float *transform() const { return mvMatrix; }
    void pushTransform();
    void popTransform();

    virtual void submit(RenderItem *packet, quint32 selectionId = 0,
                        SubmitOrder order = SUBMIT_GROUPED) = 0;
    virtual void submit(const QVector<RenderItem*> &packets, quint32 selectionId = 0) = 0;
    virtual void submitFrameItem(RenderItem *item) = 0;
    // Copy of a 4x4 matrix that stays valid until the frame is drawn, for a
    // frame item's msMatrix.
    float *frameMatrix(const float *matrix);

    // Layer for following submissions; a new frame starts in LAYER_SCENE.
    void setLayer(Layer layer);
    Layer layer() const;
    // Whether following scene submissions may cast shadows; a new frame turns
    // it back on. Only lit triangle meshes in the scene and overlay layers
    // cast, excluding terrain and terrain decals.
    void setShadowCasting(bool cast);
    bool shadowCastingEnabled() const { return shadowCasting; }

protected:
    void resetQueueState();
    void deleteFrameMatrices();

    float *mvMatrix = nullptr;
    Layer currentLayer = LAYER_SCENE;
    bool shadowCasting = true;

private:
    float ownMvMatrix[16];
    QVector<std::array<float, 16>> matrixStack;
    int matrixStackDepth = 0;
    QVector<float*> frameMatrices;
};

#endif /* RENDERQUEUE_H */
