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
#include <tsre/renderer/RenderItem.h>
#include <tsre/trains/EngLights.h>
#include <QVector>
#include <QtGlobal>
#include <vector>

class RenderQueue;

// The lamps of a car (task renderer 34): a lens for each lit glow light, at
// its position on the car facing the way it shines, the lenses of one
// colour merged into one mesh. On the QRhi renderer with local lights on
// they are emissive, glow through the bloom and bring glow splats for the
// distance (one per cluster of lenses, not one per LED dot), and the lit
// cones are spot lights of the light grid (headlight beams lighting the
// track and the mist); otherwise the lamps are plain discs in the light's
// colour and there are no beams.
class CarLights {
public:
    CarLights() = default;
    CarLights(const CarLights &) = delete;
    CarLights &operator=(const CarLights &) = delete;
    ~CarLights();
    // Submits the lamps in the car shape's space (the queue's transform is
    // the car's); built again when the lights, the train's state or the
    // material path change. halfLength: half the car's length (its ENG
    // Size), where its beams start at the latest.
    void push(RenderQueue &queue, const EngLights &lights, const EngLights::Train &train, float halfLength,
              quint32 selectionId);

    // A lens in the car shape's drawing space.
    struct Lens {
        float position[3];
        float direction[3];
        float radius;
        quint32 colour;
    };
    // The lenses of the lit glow lights (their first state).
    static std::vector<Lens> lenses(const EngLights &lights, const EngLights::Train &train);
    // The beams of the first lit cone (its first state), as Open Rails takes
    // one cone a car for its headlight, in the car shape's drawing space:
    // one out of each of the car's lit head lamps at the end it shines
    // from, the cone's strength shared; without such lamps the cone's own,
    // from the car's end when it is placed beyond it (halfLength, 0 for
    // none).
    static std::vector<RenderItem::Light> beams(const EngLights &lights, const EngLights::Train &train,
                                                float halfLength);

private:
    const EngLights *builtLights = nullptr;
    EngLights::Train builtTrain;
    float builtHalfLength = 0.0f;
    bool built = false;
    bool emissive = false;
    QVector<RenderItem *> packets;
    QVector<MeshHandle> meshes;
    // The beams, on a packet without a mesh: gathered with the lights, never
    // drawn, so a car out of view still lights the track ahead of it.
    RenderItem *beamPacket = nullptr;
    float beamReach = 0.0f;
    void build(const EngLights &lights, const EngLights::Train &train, float halfLength);
    void release();
};

#endif
