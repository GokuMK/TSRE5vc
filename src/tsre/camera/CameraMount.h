#pragma once
#include <cmath>

// Where a camera riding an object sits (GameObj::getCameraMount; a 3D cab's
// head, the user's 2026-10-23): its start in the object's riding frame (as
// getCameraPosition places it: the object's place at the origin, -z ahead,
// x to the right, y up; metres) and where it looks there (as
// Camera::playerRot: round from +z towards +x, up; radians); how far it may
// move from the start on each axis (metres; 0 not on that axis) by a key's
// step (metres), and turn either way (radians; 0 free). `id` changes when
// the object gives another place (another cab): the camera starts there.
struct CameraMount {
    float position[3] = {0.0f, 3.0f, 0.0f};
    float look[2] = {float(M_PI), 0.0f};
    float moveBox[3] = {0.0f, 0.0f, 0.0f};
    float moveStep = 0.0f;
    float turnLimit[2] = {0.0f, 0.0f};
    int id = 0;
};
