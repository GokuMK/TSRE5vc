/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/procedural/ProceduralPath.h>

#include <tsre/math3d/GLMatrix.h>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool ProceduralPath::quaternionFromBasis(float *rotation,
        const float *right, const float *up, const float *forward) {
    if(rotation == nullptr || right == nullptr || up == nullptr
            || forward == nullptr)
        return false;

    // Quat::fromMat3 consumes the basis in row order. Mat4 stores the same
    // axes as columns, so copying a Mat4-style flattened basis here would
    // transpose the rotation and reverse curve yaw for native templates.
    float rotationMatrix[9] = {
        right[0], up[0], forward[0],
        right[1], up[1], forward[1],
        right[2], up[2], forward[2]
    };
    Quat::fromMat3(rotation, rotationMatrix);
    const float length = std::sqrt(
            rotation[0] * rotation[0]
            + rotation[1] * rotation[1]
            + rotation[2] * rotation[2]
            + rotation[3] * rotation[3]);
    if(!std::isfinite(length) || length <= 1e-6f)
        return false;
    for(int i = 0; i < 4; i++)
        rotation[i] /= length;
    return true;
}

ProceduralPathTransform ProceduralPath::bakedObjectTransform(
        const float *objectQuaternion, float tdbYaw) {
    ProceduralPathTransform result;
    if(objectQuaternion == nullptr || !std::isfinite(tdbYaw))
        return result;
    for(int i = 0; i < 4; i++)
        if(!std::isfinite(objectQuaternion[i]))
            return result;

    float yawQuaternion[4];
    Quat::fill(yawQuaternion);
    Quat::rotateY(yawQuaternion, yawQuaternion, -tdbYaw);

    // A track object's normal world matrix contains its complete quaternion
    // and a final MSTS/TSRE basis flip. Procedural geometry is drawn with the
    // corresponding yaw-only matrix, so bake the residual into the path:
    //
    //   yawFinal * bakedRotation == fullFinal
    float basisFlip[4];
    Quat::fill(basisFlip);
    Quat::rotateY(basisFlip, basisFlip, -(float)M_PI);

    float fullDirection[4];
    Quat::copy(fullDirection, const_cast<float*>(objectQuaternion));
    float fullFinal[4];
    float yawFinal[4];
    Quat::multiply(fullFinal, fullDirection, basisFlip);
    Quat::multiply(yawFinal, yawQuaternion, basisFlip);

    float inverseYawFinal[4];
    Quat::invert(inverseYawFinal, yawFinal);
    Quat::multiply(result.rotation, inverseYawFinal, fullFinal);
    const float length = std::sqrt(
            result.rotation[0] * result.rotation[0]
            + result.rotation[1] * result.rotation[1]
            + result.rotation[2] * result.rotation[2]
            + result.rotation[3] * result.rotation[3]);
    if(!std::isfinite(length) || length <= 1e-6f)
        return ProceduralPathTransform();
    for(float &component : result.rotation)
        component /= length;
    result.enabled = true;
    result.uprightCrossSections = true;
    return result;
}
