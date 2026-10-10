/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SKYSATELLITES_H
#define SKYSATELLITES_H

#include <tsre/renderer/MeshHandle.h>

class RenderItem;
class RenderQueue;

// The sun and the moon on the sky (task editor 05), MSTS's sky satellites:
// discs in the sky layer, in front of the skydome. The moon shows its phase,
// its lit side towards the sun. On the QRhi renderer both are emissive, so
// the bloom gives the sun its halo; on OpenGL they are plain discs.
// Settings: core.rendering.sky.sun, .moon, .sunSize.
class SkySatellites {
public:
    // Directions point from the camera towards the body, in world space
    // (x east, y up, z south).
    struct State {
        bool sun = true;
        float sunDirection[3] = {0.0f, 1.0f, 0.0f};
        // Degrees above the horizon.
        float sunElevation = 90.0f;
        bool moon = false;
        float moonDirection[3] = {0.0f, 1.0f, 0.0f};
        float moonElevation = 0.0f;
        // Lit share of the moon, 0 (new) to 1 (full).
        float moonFraction = 0.0f;
        // The sky colour behind them (display values).
        float skyColor[3] = {0.0f, 0.0f, 0.0f};
    };

    SkySatellites() = default;
    SkySatellites(const SkySatellites &) = delete;
    SkySatellites &operator=(const SkySatellites &) = delete;
    ~SkySatellites();
    // Submits the discs to the sky layer; the queue's transform must be a
    // translation to the camera.
    void push(RenderQueue &queue, const State &state);

private:
    enum Body { SUN = 0, MOON_DARK, MOON_LIT, BODY_COUNT };
    RenderItem *packets[BODY_COUNT] = {nullptr, nullptr, nullptr};
    float matrices[BODY_COUNT][16];
    // The moon's lit part, rebuilt when the phase or the layout changes.
    MeshHandle litMesh;
    int litVertices = 0;
    float litFraction = -1.0f;
    bool litPbr = false;
    void updateLitMesh(float fraction, bool pbr);
};

#endif
