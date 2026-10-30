#include <tsre/sim/TrainPhysics.h>
#include <algorithm>
#include <cmath>

namespace Sim {

namespace {

// Solves a tridiagonal system in place (Thomas' algorithm): lower[i] x[i-1]
// + diag[i] x[i] + upper[i] x[i+1] = rhs[i]; the result in rhs.
void solveTridiagonal(std::vector<double> &lower, std::vector<double> &diag, std::vector<double> &upper,
                      std::vector<double> &rhs) {
    const size_t n = diag.size();
    for (size_t i = 1; i < n; ++i) {
        const double w = lower[i] / diag[i - 1];
        diag[i] -= w * upper[i - 1];
        rhs[i] -= w * rhs[i - 1];
    }
    rhs[n - 1] /= diag[n - 1];
    for (size_t i = n - 1; i-- > 0;)
        rhs[i] = (rhs[i] - upper[i] * rhs[i + 1]) / diag[i];
}

int signOf(double v) { return v > 0.0 ? 1 : v < 0.0 ? -1 : 0; }

} // namespace

double TrainPhysics::defaultDamping(double stiffness, double massA, double massB, double ratio) {
    const double reduced = massA > 0.0 && massB > 0.0 ? massA * massB / (massA + massB) : std::max(massA, massB);
    return 2.0 * ratio * std::sqrt(std::max(0.0, stiffness * reduced));
}

void TrainPhysics::placeFrom(double front) {
    for (size_t i = 0; i < cars.size(); ++i)
        cars[i].front = i == 0 ? front : cars[i - 1].front - cars[i - 1].length;
    for (Coupler &c : couplers) {
        c.extension = 0.0;
        c.force = 0.0;
    }
}

void TrainPhysics::step(double dt, const std::vector<Forces> &forces) {
    const size_t n = cars.size();
    if (n == 0 || dt <= 0.0 || forces.size() < n)
        return;
    // Each car either slides (its friction against `against`, the way it
    // moves) or stands (held: its speed 0 at the step's end). A moving car
    // slides the way it moves; a standing one stands until the rest of its
    // forces beat its friction.
    std::vector<int> against(n, 0);
    std::vector<bool> held(n, false);
    for (size_t i = 0; i < n; ++i) {
        against[i] = std::fabs(cars[i].speed) > Standing ? signOf(cars[i].speed) : 0;
        held[i] = against[i] == 0;
    }
    // Each coupler: free (0), stretched (+1) or pushed in (-1) at the
    // step's end, first guessed from the speeds now.
    std::vector<double> extension(n > 0 ? n - 1 : 0);
    for (size_t i = 0; i + 1 < n; ++i)
        extension[i] = (cars[i].front - cars[i].length) - cars[i + 1].front;
    std::vector<double> next(n);
    for (size_t i = 0; i < n; ++i)
        next[i] = cars[i].speed;
    std::vector<double> a(n > 0 ? n - 1 : 0), b(n > 0 ? n - 1 : 0);
    std::vector<double> lower(n), diag(n), upper(n), rhs(n);
    for (int round = 0; round < 12; ++round) {
        // The couplers engaged at the step's end, their force linear in the
        // speeds there: F = a + b (v_i - v_i+1).
        for (size_t i = 0; i + 1 < n; ++i) {
            const Coupler &c = couplers[i];
            const double end = extension[i] + dt * (next[i] - next[i + 1]);
            a[i] = b[i] = 0.0;
            if (end > c.tension) {
                a[i] = c.stiffness * (extension[i] - c.tension);
                b[i] = c.stiffness * dt + c.damping;
            } else if (end < -c.compression) {
                a[i] = c.stiffness * (extension[i] + c.compression);
                b[i] = c.stiffness * dt + c.damping;
            }
        }
        // m (v' - v) / dt = drive - friction - F_i + F_i-1.
        for (size_t i = 0; i < n; ++i) {
            const double m = std::max(cars[i].mass, 1.0);
            lower[i] = upper[i] = 0.0;
            if (held[i]) {
                diag[i] = 1.0;
                rhs[i] = 0.0;
                continue;
            }
            diag[i] = m / dt;
            rhs[i] = m * cars[i].speed / dt + forces[i].drive - forces[i].friction * against[i];
            if (i + 1 < n) {
                diag[i] += b[i];
                upper[i] = -b[i];
                rhs[i] -= a[i];
            }
            if (i > 0) {
                diag[i] += b[i - 1];
                lower[i] = -b[i - 1];
                rhs[i] += a[i - 1];
            }
        }
        solveTridiagonal(lower, diag, upper, rhs);
        bool changed = false;
        for (size_t i = 0; i < n; ++i) {
            const double v = rhs[i];
            const double m = std::max(cars[i].mass, 1.0);
            if (held[i]) {
                // What holding it takes: the force that would move it.
                double push = forces[i].drive - m * (0.0 - cars[i].speed) / dt;
                if (i + 1 < n)
                    push -= a[i] + b[i] * (0.0 - rhs[i + 1]);
                if (i > 0)
                    push += a[i - 1] + b[i - 1] * (rhs[i - 1] - 0.0);
                if (std::fabs(push) > forces[i].friction) {
                    held[i] = false;
                    against[i] = signOf(push);
                    changed = true;
                }
            } else if (against[i] != 0 && signOf(v) != against[i]) {
                // Its friction would turn it round: it stops.
                held[i] = true;
                changed = true;
            }
            next[i] = held[i] ? 0.0 : v;
        }
        if (!changed)
            break;
    }
    for (size_t i = 0; i < n; ++i) {
        cars[i].speed = held[i] ? 0.0 : rhs[i];
        cars[i].front += dt * cars[i].speed;
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        couplers[i].extension = (cars[i].front - cars[i].length) - cars[i + 1].front;
        couplers[i].force = a[i] + b[i] * (cars[i].speed - cars[i + 1].speed);
    }
}

}
