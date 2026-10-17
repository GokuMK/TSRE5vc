/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef CARLIGHTS_H
#define CARLIGHTS_H

#include <tsre/renderer/MeshHandle.h>
#include <tsre/trains/EngLights.h>
#include <QVector>
#include <QtGlobal>
#include <vector>

class RenderItem;
class RenderQueue;

// The lamps of a car (task renderer 34): a lens for each lit glow light, at
// its position on the car facing the way it shines, the lenses of one
// colour merged into one mesh. On the QRhi renderer with local lights on
// they are emissive, glow through the bloom and bring glow splats for the
// distance (one per cluster of lenses, not one per LED dot); otherwise they
// are plain discs in the light's colour.
class CarLights {
public:
    CarLights() = default;
    CarLights(const CarLights &) = delete;
    CarLights &operator=(const CarLights &) = delete;
    ~CarLights();
    // Submits the lamps in the car shape's space (the queue's transform is
    // the car's); built again when the lights, the train's state or the
    // material path change.
    void push(RenderQueue &queue, const EngLights &lights, const EngLights::Train &train, quint32 selectionId);

    // A lens in the car shape's drawing space.
    struct Lens {
        float position[3];
        float direction[3];
        float radius;
        quint32 colour;
    };
    // The lenses of the lit glow lights (their first state).
    static std::vector<Lens> lenses(const EngLights &lights, const EngLights::Train &train);

private:
    const EngLights *builtLights = nullptr;
    EngLights::Train builtTrain;
    bool built = false;
    bool emissive = false;
    QVector<RenderItem *> packets;
    QVector<MeshHandle> meshes;
    void build(const EngLights &lights, const EngLights::Train &train);
    void release();
};

#endif
