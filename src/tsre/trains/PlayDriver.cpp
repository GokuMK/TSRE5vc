/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/trains/PlayDriver.h>

#include <algorithm>
#include <cmath>

PlayDriver::PlayDriver(float topSpeed, float period) : top(std::max(topSpeed, 1.0f)), period(std::max(period, 10.0f)) {
}

void PlayDriver::run(float seconds) {
    time = std::fmod(time + std::max(seconds, 0.0f), double(period));
    currentSpeed = top * float(std::max(0.0, std::sin(2.0 * M_PI * time / period)));
    derive(seconds);
}

void PlayDriver::follow(float speed, float seconds) {
    currentSpeed = speed;
    derive(seconds);
}

void PlayDriver::derive(float seconds) {
    if (seconds > 0.0f) {
        // Smoothed over a second: the frames' steps are uneven.
        const float measured = (currentSpeed - lastSpeed) / seconds;
        const float k = std::min(seconds, 1.0f);
        currentAcceleration += (measured - currentAcceleration) * k;
    }
    lastSpeed = currentSpeed;
    // The sine's steepest rise and fall.
    const float peak = top * 2.0f * float(M_PI) / period;
    const float share = currentAcceleration / peak;
    if (std::fabs(currentSpeed) < 0.05f) {
        // Standing: brakes on, throttle shut (it opens as it starts).
        currentThrottle = share > 0.02f ? std::clamp(0.3f + 0.7f * share, 0.0f, 1.0f) : 0.0f;
        currentBrake = currentThrottle > 0.0f ? 0.0f : 1.0f;
    } else if (share >= -0.02f) {
        // Pulling, a third of the throttle to hold the speed.
        currentThrottle = std::clamp(0.3f + 0.7f * share, 0.0f, 1.0f);
        currentBrake = 0.0f;
    } else {
        currentThrottle = 0.0f;
        currentBrake = std::clamp(-share, 0.0f, 1.0f);
    }
}

float TrainMotion::curveForce(float massKg, float speed, float radius) {
    // Standing wheels do not rub.
    if (radius <= 0.0f || massKg <= 0.0f || std::fabs(speed) < 0.1f)
        return 0.0f;
    constexpr float g = 9.81f;
    const float base = g * massKg * RailFriction * (TrackGauge + RigidWheelBase) / (2.0f * radius);
    const float perTonne = 1.5f * speed * speed / radius;
    return base + g * massKg / 1000.0f * perTonne;
}

float TrainMotion::angleOfAttack(float radius) {
    if (radius <= 0.0f)
        return 0.0f;
    return std::asin(std::min(1.0f, 2.0f * RigidWheelBase / radius)) * 1000.0f;
}

float TrainMotion::wheelRpm(float speed, float wheelRadius) {
    if (wheelRadius <= 0.0f)
        return 0.0f;
    return std::fabs(speed) / (2.0f * float(M_PI) * wheelRadius) * 60.0f;
}
