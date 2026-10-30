#pragma once
#include <vector>

namespace Sim {

// A driven train's motion (task driving 02): its cars in the order they run,
// the leading one first, each a mass with a speed and a place along its
// route, coupled to the next by a coupler: free play (slack), then a spring
// and a damper. Each step takes the forces on every car and solves the
// couplers and the speeds together, implicitly (backward Euler: one
// tridiagonal system along the train), so stiff couplers stay stable at the
// step; a rigid coupler is the stiff limit. Brakes and resistance act as
// friction: they slow a moving car without turning it round, and hold a
// standing one up to their force. Plain numbers only (no route, no Qt): the
// core places the cars on the track.
class TrainPhysics {
public:
    struct Car {
        // kg; m along the route of its front (the way the train runs); m
        // from its front to the next car's front at rest (its length with
        // the gap at its coupler); m/s along the route (+ the way it runs).
        double mass = 0.0;
        double front = 0.0;
        double length = 0.0;
        double speed = 0.0;
    };
    // The coupler behind a car (between car i and i + 1).
    struct Coupler {
        // Free play each way from rest, m: it moves without force until
        // stretched past `tension` or pushed in past `compression`.
        double tension = 0.0;
        double compression = 0.0;
        // Beyond it: N/m and N/(m/s).
        double stiffness = 1e7;
        double damping = 0.0;
        // The result of the last step: how far it is stretched from rest (m,
        // + stretched, - pushed in) and its force (N, + pulling).
        double extension = 0.0;
        double force = 0.0;
    };
    // The forces on a car for a step.
    struct Forces {
        // N along the route, + the way the train runs: motive force, gravity
        // and whatever else pushes it.
        double drive = 0.0;
        // N >= 0 against its motion: brakes and resistance. A standing car
        // stays standing while the rest of its forces are under this.
        double friction = 0.0;
    };

    std::vector<Car> cars;
    // couplers[i] is behind cars[i]; one fewer than the cars.
    std::vector<Coupler> couplers;

    // The couplers' damping where a file gives none: a damping ratio of
    // `ratio` for the two cars' masses (1: a long train's slack run-in dies
    // out within about a minute and a half).
    static double defaultDamping(double stiffness, double massA, double massB, double ratio = 1.0);
    // Cars placed one behind the other at rest from the first's front (its
    // couplers at rest).
    void placeFrom(double front);
    // One step of `dt` seconds with these forces (one per car).
    void step(double dt, const std::vector<Forces> &forces);

    // A car's speed counts as standing below this (m/s).
    static constexpr double Standing = 1e-3;
};

}
