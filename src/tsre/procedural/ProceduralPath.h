/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef PROCEDURALPATH_H
#define PROCEDURALPATH_H

struct ProceduralPathTransform {
    // Rotation from the ordinary flat track-local path into the local
    // space of the yaw-only object matrix used to draw a baked path.
    float rotation[4] = {0, 0, 0, 1};
    bool enabled = false;
    bool uprightCrossSections = false;
};

class ProceduralPath {
public:
    static ProceduralPathTransform bakedObjectTransform(
            const float *objectQuaternion, float tdbYaw);
    static bool quaternionFromBasis(float *rotation,
            const float *right, const float *up, const float *forward);
};

#endif
