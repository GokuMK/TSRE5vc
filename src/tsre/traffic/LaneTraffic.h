/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef LANETRAFFIC_H
#define LANETRAFFIC_H

#include <vector>

// Cars on one road lane (task traffic 01): they enter at its start and
// leave past its end, each keeping behind the car in front by the
// Intelligent Driver Model (IDM), and stop before the lane's closed stop
// points (level crossings). No road database here: places are metres along
// the lane from its start, the way cars drive.
class LaneTraffic {
public:
    // IDM: the most a car speeds up, how hard it likes to brake, the gap it
    // keeps standing and its time gap moving. A car needing more than
    // HardBraking to stop at a stop point that has just closed goes through
    // (Open Rails: a car within a second of it). Cars stop StopBefore short
    // of a stop point (Open Rails' RoadCarSpawner.StopDistance). Steps are
    // cut into pieces of at most MaxStep.
    static constexpr double MaxAcceleration = 1.5;
    static constexpr double ComfortBraking = 2.5;
    static constexpr double MinimumGap = 2.0;
    static constexpr double Headway = 1.2;
    static constexpr double HardBraking = 4.0;
    static constexpr double StopBefore = 10.0;
    static constexpr double MaxStep = 0.1;
    // Brake lights (2026-11-13): on once a car slows by more than
    // BrakeLightsOn (m/s²) or stands, off once it slows by less than
    // BrakeLightsOff (a driver lifting off slows without them).
    static constexpr double BrakeLightsOn = 1.0;
    static constexpr double BrakeLightsOff = 0.5;

    struct Car {
        int id = 0;
        double length = 4.5;
        // m/s.
        double desired = 15.0;
        double speed = 0.0;
        // Its middle, metres from the lane's start.
        double position = 0.0;
        // Its brake lights on (BrakeLightsOn).
        bool braking = false;
        double front() const { return position + length / 2.0; }
        double rear() const { return position - length / 2.0; }
    };
    // A place cars do not pass while it is closed (where the first rail of
    // a level crossing is), metres from the start.
    struct Stop {
        double at = 0.0;
        bool closed = false;
    };

    // The lane's length: cars leave once their rear is past it.
    double length = 0.0;
    // Front first (the furthest along), as no car passes another.
    std::vector<Car> cars;
    // In order along the lane.
    std::vector<Stop> stops;

    // Room at the start for a car this long: the last car's rear is that
    // far in, with the gap behind it, and no closed stop point's line is
    // short of its front.
    bool roomFor(double carLength) const;
    // A car enters with its rear at the start, at its desired speed or as
    // fast as it may to keep behind the car in front.
    void enter(Car car);
    // Moves every car on by `seconds`; the cars that left (from the front)
    // are removed: how many.
    int step(double seconds);

private:
    // The acceleration IDM gives a car `gap` metres behind something moving
    // at `leadSpeed`.
    static double idm(const Car &car, double gap, double leadSpeed);
    void stepOnce(double dt);
};

#endif
