/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/traffic/LaneTraffic.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// The gap IDM never divides by less than (cars touching).
constexpr double SmallestGap = 0.1;
// A car stopping at a stop point may stand this far past its line (creep).
constexpr double LineSlack = 0.5;
// IDM only creeps toward where it stops: a car this close to its line, or
// to the minimum gap behind a standing car, and this slow, stands.
constexpr double StandWithin = 0.25;
constexpr double StandBelow = 0.5;
// A car brakes for a stop line once standing there takes this share of its
// comfortable braking.
constexpr double BrakeFrom = 0.9;

}

bool LaneTraffic::roomFor(double carLength) const {
    // A car entering past the line of a closed stop point would drive
    // through it: it waits until the crossing opens.
    for (const Stop &stop : stops)
        if (stop.closed && stop.at - StopBefore < carLength)
            return false;
    if (cars.empty())
        return true;
    return cars.back().rear() >= carLength + MinimumGap;
}

void LaneTraffic::enter(Car car) {
    car.position = car.length / 2.0;
    car.speed = car.desired;
    if (!cars.empty()) {
        // No faster than it can still stop behind the car in front.
        const Car &lead = cars.back();
        const double gap = std::max(0.0, lead.rear() - car.front() - MinimumGap);
        car.speed = std::min(car.speed, lead.speed + std::sqrt(2.0 * ComfortBraking * gap));
    }
    for (const Stop &stop : stops)
        if (stop.closed && stop.at - StopBefore > car.front()) {
            const double gap = std::max(0.0, stop.at - StopBefore - car.front());
            car.speed = std::min(car.speed, std::sqrt(2.0 * ComfortBraking * gap));
            break;
        }
    cars.push_back(car);
}

double LaneTraffic::idm(const Car &car, double gap, double leadSpeed) {
    const double v = car.speed;
    const double v0 = std::max(0.1, car.desired);
    const double wanted = MinimumGap + std::max(0.0, v * Headway + v * (v - leadSpeed) / (2.0 * std::sqrt(MaxAcceleration * ComfortBraking)));
    const double s = std::max(SmallestGap, gap);
    const double free = std::pow(v / v0, 4.0);
    return MaxAcceleration * (1.0 - free - (wanted / s) * (wanted / s));
}

int LaneTraffic::step(double seconds) {
    if (seconds <= 0.0)
        return 0;
    const int pieces = std::max(1, int(std::ceil(seconds / MaxStep - 1e-9)));
    const double dt = seconds / pieces;
    for (int k = 0; k < pieces; ++k)
        stepOnce(dt);
    int left = 0;
    while (left < int(cars.size()) && cars[size_t(left)].rear() > length)
        ++left;
    cars.erase(cars.begin(), cars.begin() + left);
    return left;
}

void LaneTraffic::stepOnce(double dt) {
    const double none = std::numeric_limits<double>::infinity();
    // Front to back: each car follows the car in front where that car is
    // after this step.
    for (size_t i = 0; i < cars.size(); ++i) {
        Car &car = cars[i];
        double accel = idm(car, none, car.speed);
        // How far its front may go: behind the car in front (by a hair), and
        // to the line of a closed stop point it stops at.
        double behindLead = none;
        double leadSpeed = 0.0;
        if (i > 0) {
            const Car &lead = cars[i - 1];
            accel = std::min(accel, idm(car, lead.rear() - car.front(), lead.speed));
            behindLead = lead.rear() - SmallestGap;
            leadSpeed = lead.speed;
        }
        double atLine = none;
        // The first closed stop point ahead it can still stop at, its line
        // StopBefore short of it: braking evenly to stand at the line once
        // that takes most of a comfortable stop (IDM alone brakes harder
        // than it likes at the end of a stop).
        for (const Stop &stop : stops) {
            if (!stop.closed)
                continue;
            const double line = stop.at - StopBefore;
            const double gap = line - car.front();
            if (gap < -LineSlack)
                continue;
            // Too late to stop (a moving car on its line is too): it goes
            // through.
            const double needed = car.speed * car.speed / (2.0 * std::max(gap, 0.01));
            if (needed > HardBraking)
                continue;
            if (needed >= BrakeFrom * ComfortBraking)
                accel = std::min(accel, -needed);
            atLine = std::max(line, car.front());
            break;
        }
        accel = std::max(accel, -3.0 * HardBraking);
        double speed = car.speed + accel * dt;
        double moved;
        if (speed < 0.0) {
            // It stops within the step.
            moved = accel < 0.0 ? car.speed * car.speed / (-2.0 * accel) : 0.0;
            speed = 0.0;
        } else {
            moved = (car.speed + speed) / 2.0 * dt;
        }
        double front = car.front() + std::max(0.0, moved);
        if (front > atLine && atLine <= behindLead) {
            front = atLine;
            speed = 0.0;
        } else if (front > behindLead) {
            front = std::max(car.front(), behindLead);
            speed = std::min(speed, leadSpeed);
        } else if (speed < StandBelow && atLine <= behindLead && atLine - front < StandWithin) {
            front = std::max(car.front(), atLine);
            speed = 0.0;
        } else if (speed < StandBelow && i > 0 && leadSpeed == 0.0
                   && cars[i - 1].rear() - front < MinimumGap + StandWithin) {
            speed = 0.0;
        }
        car.position = front - car.length / 2.0;
        const double slowing = (car.speed - speed) / dt;
        if (speed == 0.0 || slowing > BrakeLightsOn)
            car.braking = true;
        else if (slowing < BrakeLightsOff)
            car.braking = false;
        car.speed = speed;
    }
}
