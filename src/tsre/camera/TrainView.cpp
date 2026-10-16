#include <tsre/camera/TrainView.h>

#include <algorithm>
#include <cmath>

namespace TrainView {

namespace {

// From the eye towards `at`.
void lookAt(Pose &pose, const float at[3]) {
    const float look[3] = {at[0] - pose.eye[0], at[1] - pose.eye[1], at[2] - pose.eye[2]};
    const float distance = std::sqrt(look[0] * look[0] + look[1] * look[1] + look[2] * look[2]);
    pose.yaw = std::atan2(look[0], look[2]);
    pose.pitch = std::asin(look[1] / std::max(distance, 0.001f));
}

}

Pose Place(End end, const float front[3], const float back[3], const float heading[3], float length) {
    Pose pose;
    if (end == End::Back) {
        // Behind its rear, the whole train ahead.
        const float middle[3] = {0.5f * (front[0] + back[0]), 0.5f * (front[1] + back[1]), 0.5f * (front[2] + back[2])};
        const float behind = 0.5f * length + 35.0f, up = 15.0f + 0.05f * length;
        pose.eye[0] = middle[0] - behind * heading[0];
        pose.eye[1] = middle[1] + up;
        pose.eye[2] = middle[2] - behind * heading[2];
        lookAt(pose, middle);
        return pose;
    }
    // Ahead of its front and to its side, its first vehicle in view.
    const float ahead = 18.0f, side = 8.0f, up = 4.0f;
    pose.eye[0] = front[0] + ahead * heading[0] + side * heading[2];
    pose.eye[1] = front[1] + up;
    pose.eye[2] = front[2] + ahead * heading[2] - side * heading[0];
    const float first[3] = {front[0] - 8.0f * heading[0], front[1] + 2.0f, front[2] - 8.0f * heading[2]};
    lookAt(pose, first);
    return pose;
}

float Yaw(const float heading[3]) {
    return std::atan2(heading[0], heading[2]);
}

void Carry(float eye[3], float &yaw, const float was[3], const float now[3], float turned) {
    const float x = eye[0] - was[0], z = eye[2] - was[2];
    const float c = std::cos(turned), s = std::sin(turned);
    eye[0] = now[0] + x * c + z * s;
    eye[1] += now[1] - was[1];
    eye[2] = now[2] - x * s + z * c;
    yaw += turned;
}

}
