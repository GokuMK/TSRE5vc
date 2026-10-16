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

class ComplexShape;
class RenderItem;
class RenderQueue;
class SignalShape;

// The lights of a signal's heads in their default draw state (task 26), or
// in the draw states a running simulation gives them (task simulation 02): a
// disc per light at its sigcfg.dat position on the head. On the QRhi
// renderer with local lights on the discs are emissive and glow through the
// bloom; otherwise they are plain discs in the light's colour.
class SignalLights {
public:
    // Where the heads' draw states come from while a simulation runs.
    class StateSource {
    public:
        virtual ~StateSource() = default;
        // Changes whenever a draw state it gives may have.
        virtual unsigned long long stateSerial() const = 0;
        // The draw state's name (lower case) the head of a TDB item shows;
        // empty for its default.
        virtual QString drawStateOf(int item) const = 0;
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
    // items: each sub-object's TDB item (32).
    void push(RenderQueue &queue, ComplexShape *shape, const SignalShape *signalShape,
              unsigned int heads, const int *items, quint32 selectionId);

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
    // The draw states built (by sub-object; empty the default) and the
    // source's serial they were asked at.
    QVector<QString> states;
    unsigned long long statesSerial = 0;
    bool emissive = false;
    QVector<RenderItem *> packets;
    void build(ComplexShape *shape, const SignalShape *signalShape, unsigned int heads);
    void makePackets();
    void retirePackets();
};

#endif
