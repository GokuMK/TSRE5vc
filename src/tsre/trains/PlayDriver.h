/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef PLAYDRIVER_H
#define PLAYDRIVER_H

// A stand-in for driving the played train until TSRE simulates one, in one
// of two modes: the speed on a sine, running half the period and standing
// the other half, or a constant speed from the start; throttle and brake
// follow from its rises and falls, so sounds have something to follow.
// follow() takes a speed from elsewhere (the network) and derives the same.
class PlayDriver {
public:
    enum class Mode { Sine, Constant };

    // Top (or constant) speed in m/s, the sine's period in seconds.
    explicit PlayDriver(Mode mode = Mode::Sine, float topSpeed = 25.0f, float period = 160.0f);

    Mode mode() const { return driving; }

    void run(float seconds);
    void follow(float speed, float seconds);

    float speed() const { return currentSpeed; }
    float acceleration() const { return currentAcceleration; }
    // 0 to 1.
    float throttle() const { return currentThrottle; }
    float brake() const { return currentBrake; }

private:
    void derive(float seconds);

    Mode driving;
    float top;
    float period;
    double time = 0.0;
    float currentSpeed = 0.0f;
    float lastSpeed = 0.0f;
    float currentAcceleration = 0.0f;
    float currentThrottle = 0.0f;
    float currentBrake = 1.0f;
};

// What a car's wheels do on the track, for its sounds.
namespace TrainMotion {
// Curve resistance in N (the base friction of the wheelbase in the curve,
// after C. E. Allen 1912, and the speed term after P. N. Astakhov 1966,
// without superelevation): mass in kg, speed in m/s, radius in m (0:
// straight); none standing.
float curveForce(float massKg, float speed, float radius);
// The wheels' angle of attack in the curve, mrad.
float angleOfAttack(float radius);
// Wheel turns a minute.
float wheelRpm(float speed, float wheelRadius);

constexpr float TrackGauge = 1.435f;
constexpr float RigidWheelBase = 1.6764f;
constexpr float RailFriction = 0.35f;
}

#endif
