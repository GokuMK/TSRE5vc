#pragma once

// Where the camera views a simulated train from (the Timetable window's
// Follow; in 3D keys 1 and 2), in the camera's space: positions relative to
// one tile, yaws as CameraFree's (the angle of x over z).
namespace TrainView {

enum class End { Front, Back };

struct Pose {
    float eye[3] = {0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

// The camera's place by an end of a train heading `heading` (a unit vector)
// whose front and back as it runs are at `front` and `back`: ahead of its
// front, to its side and above, looking back at its first vehicle; or
// behind its back and above, the whole train ahead.
Pose Place(End end, const float front[3], const float back[3], const float heading[3], float length);

// The heading's yaw.
float Yaw(const float heading[3]);

// A camera riding an end of the train: moved as that end moved (from `was`
// to `now`, both in the camera's frame) and turned as the train turned
// (`turned` radians), so it keeps its place by that end.
void Carry(float eye[3], float &yaw, const float was[3], const float now[3], float turned);

}
