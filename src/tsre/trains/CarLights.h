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
#include <tsre/shape/ShapeLights.h>
#include <tsre/trains/EngLights.h>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <vector>

class ComplexShape;
class RenderQueue;

// The lamps of a car (task renderer 34): a lens for each lit glow light, at
// its position on the car facing the way it shines, the lenses of one
// colour merged into one mesh. On the QRhi renderer with local lights on
// they are emissive, glow through the bloom and bring glow splats for the
// distance (one per cluster of lenses, not one per LED dot), and the lit
// cones are spot lights of the light grid (headlight beams lighting the
// track and the mist); otherwise the lamps are plain discs in the light's
// colour and there are no beams. ENG lights may also switch lights of the
// car's shape (task shapes 14: switchShapeLights).
class CarLights {
public:
    CarLights() = default;
    CarLights(const CarLights &) = delete;
    CarLights &operator=(const CarLights &) = delete;
    ~CarLights();
    // Submits the lamps in the car shape's space (the queue's transform is
    // the car's); built again when the lights, the train's state or the
    // material path change. halfLength: half the car's length (its ENG
    // Size), where its beams start at the latest. Without `lenses` the
    // beams alone (seen from its 3D cab).
    void push(RenderQueue &queue, const EngLights &lights, const EngLights::Train &train, float halfLength,
              quint32 selectionId, bool lenses = true);

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

    // The car shape's lights its ENG lights bind (task shapes 14): an ENG
    // light whose ShapeHierarchy names a light of the shape (its bindName,
    // ShapeLights::bound) switches it in the car's shape state, set before
    // the shape draws: on while any light binding it is on (fades and
    // flashing come with task renderer 34's step 4), off otherwise. The
    // reversed copies of cones bind nothing: each end is bound on its own.
    // An ENG binding a cone gives no derived beams (push), whatever its
    // other cones. `file`: the ENG's, for its author's messages.
    void switchShapeLights(ComplexShape &shape, unsigned int stateId, const EngLights &lights,
                           const EngLights::Train &train, const QString &file);
    // How far its lit bound lights reach (their Range, else the grid's
    // longest); 0 with none lit: asked for while the shape draws.
    float shapeLightReach() const { return boundReach; }

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
    // The binding found for a shape (its lights' data, read with at() only:
    // a non-const [] would detach it) and ENG: per ENG light the shape light
    // it switches, -1 none.
    const ComplexShape *bindShape = nullptr;
    const EngLights *bindLights = nullptr;
    QVector<ShapeLights::Light> bindData;
    QVector<int> binding;
    bool anyBound = false;
    bool conesBound = false;
    bool builtConesBound = false;
    float boundReach = 0.0f;
    void bind(const ComplexShape &shape, const EngLights &lights, const QString &file);
    void build(const EngLights &lights, const EngLights::Train &train, float halfLength);
    void release();
};

#endif
