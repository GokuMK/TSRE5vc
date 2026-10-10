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

#include <QVector>
#include <QtGlobal>

class ComplexShape;
class RenderItem;
class RenderQueue;
class SignalShape;

// The lights of a signal's heads in their default draw state (task 26): a
// disc per light at its sigcfg.dat position on the head. On the QRhi
// renderer with local lights on the discs are emissive and glow through the
// bloom; otherwise they are plain discs in the light's colour.
class SignalLights {
public:
    SignalLights() = default;
    SignalLights(const SignalLights &) = delete;
    SignalLights &operator=(const SignalLights &) = delete;
    ~SignalLights();
    // Forgets the lights; they are built again once the shape is loaded.
    void invalidate();
    // Submits the lights in the signal shape's space (the queue's transform
    // is the object's). heads: bit per sub-object whose lights are shown.
    void push(RenderQueue &queue, ComplexShape *shape, const SignalShape *signalShape,
              unsigned int heads, quint32 selectionId);

private:
    struct Light {
        // Disc of radius 1 to the shape's space.
        float matrix[16];
        // LightsTab colour, display values.
        float color[3];
    };
    QVector<Light> lights;
    bool built = false;
    unsigned int builtHeads = 0;
    bool emissive = false;
    QVector<RenderItem *> packets;
    void build(ComplexShape *shape, const SignalShape *signalShape, unsigned int heads);
    void makePackets();
    void retirePackets();
};

#endif
