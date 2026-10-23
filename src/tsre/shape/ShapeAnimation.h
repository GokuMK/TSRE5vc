/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SHAPEANIMATION_H
#define SHAPEANIMATION_H

#include <array>
#include <vector>

// A shape's keyframe animation (task shapes 12), as Open Rails poses MSTS
// shapes (PoseableShape.AnimateOneMatrix): one channel per matrix (part),
// its rotation keys taking the place of the matrix's rotation and its
// position keys of its translation, each between the two keys around the
// frame (slerp, lerp; TCB parameters left out, as Open Rails leaves them);
// before the first key the first, after the last the last. A matrix with
// neither keeps its rest transform.
namespace ShapeAnimation {

struct PositionKey {
    float frame = 0.0f;
    float position[3] = {0.0f, 0.0f, 0.0f};
};

// A unit quaternion (x, y, z, w) in TSRE's shape space (fromMsts).
struct RotationKey {
    float frame = 0.0f;
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct Channel {
    std::vector<PositionKey> positions;
    std::vector<RotationKey> rotations;
    bool empty() const { return positions.empty() && rotations.empty(); }
    // The last key's frame, 0 without keys.
    float lastFrame() const;
};

// An MSTS key's quaternion (x, y, z, w as written) in TSRE's shape space:
// the matrices are read as written, so the rotation is its conjugate (the
// rest matrix of every keyed stock part agrees with its frame 0 key).
void fromMsts(const float msts[4], float out[4]);

// The matrix posed at the frame: `rest` (4x4, column major) with what the
// channel has replaced.
void pose(const Channel &channel, float frame, const float rest[16], float out[16]);

// Keys sorted by frame (stable), as the evaluation expects them.
void sortKeys(Channel &channel);

// A shape state's pose (ComplexShape::setPartFrame, setPartTransform): each
// part at rest, at a frame of its keys, or at a transform of its own
// (relative to its parent). `posed` while any part is.
struct Pose {
    struct Part {
        enum Mode : unsigned char { Rest, Frame, Transform };
        Mode mode = Rest;
        float frame = 0.0f;
        std::array<float, 16> local{};
    };
    std::vector<Part> parts;
    bool posed = false;
    // Changes whenever a part's pose does (shapes keep what they built for
    // it until then).
    unsigned int version = 0;
    void setFrame(int part, float frame);
    // Null: back to rest.
    void setTransform(int part, const float *local);
    void clear();
    // The part's transform relative to its parent: its own, its keys at its
    // frame, its keys at `loopFrame` when the state loops and it has keys
    // (`channel`, null for none), else `rest`.
    void local(int part, const Channel *channel, bool looping, float loopFrame, const float rest[16],
               float out[16]) const;
};

}

#endif
