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
#include <vector>

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
    // Whether following submissions are drawn into the shadow maps only,
    // never in a view: a coarser level of detail standing in for the view's
    // mesh, which is then submitted without casting (task renderer 37). A new
    // frame turns it off.
    void setShadowOnly(bool only);
    bool shadowOnlyEnabled() const { return shadowOnly; }
    // Whether following submissions are being edited: their shadows are
    // drawn on top of the cached static shadow maps every frame (task
    // renderer 37), and they stay roofs in the sky enclosure maps, which
    // follow them. Overlay and frame items always count as moving. A new
    // frame turns it off.
    void setShadowDynamic(bool dynamic);
    bool shadowDynamicEnabled() const { return shadowDynamic; }
    // Whether following submissions animate (animated objects, car
    // spawners, signals, level crossings, trains): drawn on top of the
    // cached static shadow maps, and no roofs in the cached sky enclosure
    // maps (they would leave their cover behind). A new frame turns it off.
    void setAnimated(bool animated);
    bool animatedEnabled() const { return animated; }
    // What the cached static shadow maps hold (task renderer 37): producers
    // add a hash of each loaded object that casts and is not being edited
    // (its identity, place and last change), in range or not, drawn or not,
    // so edits, additions, deletions and loaded tiles change the sum, and
    // the camera moving or turning and levels of detail switching do not.
    // A new frame starts it at 0.
    void addShadowSignature(quint64 hash) { shadowSignatureSum += hash; }
    quint64 shadowSignature() const { return shadowSignatureSum; }
    // The same of every loaded object, being edited too: what maps of all
    // surfaces hold (the sky enclosure), so an object dragged in the editor
    // redraws them every frame.
    void addSceneSignature(quint64 hash) { sceneSignatureSum += hash; }
    quint64 sceneSignature() const { return sceneSignatureSum; }
    // The reach (m) of the lights of following submissions, set by a placed
    // object that asks for more than the default; 0 for the default. A new
    // frame turns it back to 0.
    void setLightReach(float metres);
    float lightReach() const { return currentLightReach; }
    // A shape asked for here is still loading: with shadow casting on, its
    // place (the transform's) is kept for the frame, so a cached shadow map
    // reaching it knows it misses a caster (task renderer 37). A new frame
    // clears them.
    void noteShapeWaiting();

protected:
    void resetQueueState();
    void deleteFrameMatrices();

    float *mvMatrix = nullptr;
    Layer currentLayer = LAYER_SCENE;
    bool shadowCasting = true;
    bool shadowOnly = false;
    bool shadowDynamic = false;
    bool animated = false;
    quint64 shadowSignatureSum = 0;
    quint64 sceneSignatureSum = 0;
    float currentLightReach = 0.0f;
    std::vector<std::array<float, 3>> waitingShapes;

private:
    float ownMvMatrix[16];
    QVector<std::array<float, 16>> matrixStack;
    int matrixStackDepth = 0;
    QVector<float*> frameMatrices;
};

#endif /* RENDERQUEUE_H */
