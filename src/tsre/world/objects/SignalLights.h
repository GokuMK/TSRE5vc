/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SIGNALLIGHTS_H
#define SIGNALLIGHTS_H

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <tsre/tdb/SigCfg.h>

class ComplexShape;
class RenderItem;
class RenderQueue;
class SignalShape;

// The lights of a signal's heads in their default draw state (task 26), or
// in the draw states a running simulation gives them (task simulation 02): a
// light per sigcfg.dat light at its place on the head, drawn as Open Rails
// draws it: its light texture (the light's ORTSSignalLightTex, else its
// type's SignalLightTex) on a square of its radius, cut out by the texture's
// alpha and tinted by its colour; a disc of its radius where the texture
// cannot be had (or until it is loaded). On the QRhi renderer with local
// lights on they are emissive and glow through the bloom; otherwise unlit in
// the light's colour. A light texture's light is seen from the front only,
// a little in front of the head, the more so the farther it is (the depth
// buffer's step there).
class SignalLights {
public:
    // Where the heads' draw states come from while a simulation runs.
    class StateSource {
    public:
        virtual ~StateSource() = default;
        // Changes whenever a draw state it gives may have.
        virtual unsigned long long stateSerial() const = 0;
        // The draw state the head of a TDB item shows: its number in the
        // type (SignalDrawState's, as a script sets draw_state; several
        // may share a name, so not by name); -1 for its default.
        virtual int drawStateOf(int item) const = 0;
    };
    // Null: every head in its default draw state.
    static void setStateSource(const StateSource *source);

    SignalLights() = default;
    SignalLights(const SignalLights &) = delete;
    SignalLights &operator=(const SignalLights &) = delete;
    ~SignalLights();
    // Forgets the lights; they are built again once the shape is loaded.
    void invalidate();
    // Submits the lights in the signal shape's space (the queue's transform
    // is the object's). heads: bit per sub-object whose lights are shown;
    // items: each sub-object's TDB item (32); distance: the signal's from
    // the camera (m).
    void push(RenderQueue &queue, ComplexShape *shape, const SignalShape *signalShape,
              unsigned int heads, const int *items, quint32 selectionId, float distance);

    // How far in front of its head a light sits, seen from that far (m).
    static float frontOffset(float distance);
    // A light texture's UV rectangle as the transform of a square's texture
    // coordinates (DiscMesh's, 0 to 1), laid as Open Rails lays it: two rows
    // of a 2 x 3 matrix (RenderItem::Pbr::UvTransform).
    static void lightTextureUv(const SigCfg::LightTexture &texture, float *rows);

private:
    struct Light {
        // The head's matrix, and the light's place on it (sigcfg.dat).
        float head[16];
        float position[3];
        float radius = 0.0f;
        // Square (or disc) of radius 1 to the shape's space, at the front
        // offset of the last push.
        float matrix[16];
        // LightsTab colour, display values.
        float color[3];
        // Its light texture (TexLib; -1: a disc) and the transform of the
        // square's texture coordinates to the texture's UV rectangle, laid
        // as Open Rails lays it (mirrored along x).
        int texture = -1;
        float uv[6] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        // A flashing light (DrawLight's FLASHING): lit the first `flashOn`
        // seconds of every `flashCycle` of the world clock (its type's
        // SigFlashDuration, Open Rails' 1 s and 1 s by default).
        bool flashing = false;
        float flashOn = 1.0f;
        float flashCycle = 2.0f;
    };
    QVector<Light> lights;
    bool built = false;
    unsigned int builtHeads = 0;
    // The draw states built (by sub-object, their numbers; -1 the default)
    // and the source's serial they were asked at.
    QVector<int> states;
    unsigned long long statesSerial = 0;
    bool emissive = false;
    // The packets draw the light textures (each loaded; discs until then).
    bool textured = false;
    // The front offset the matrices have.
    float offset = -1.0f;
    QVector<RenderItem *> packets;
    void build(ComplexShape *shape, const SignalShape *signalShape, unsigned int heads);
    bool texturesReady() const;
    void place(float frontOffset);
    void makePackets();
    void retirePackets();
};

#endif
