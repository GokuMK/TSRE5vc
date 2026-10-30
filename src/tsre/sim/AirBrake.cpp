#include <tsre/sim/AirBrake.h>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tsre/trains/VehicleDefinition.h>

namespace Sim {

namespace {

// Open Rails' defaults are in psi.
constexpr double Psi = 6894.757293168;
constexpr double Infinite = std::numeric_limits<double>::infinity();

// The first of two keys a file gives (Open Rails reads either), else
// `otherwise`.
double eitherOf(const VehicleDefinition &d, const char *first, const char *second, double otherwise) {
    const double a = d.number(first, 0, -1.0);
    return a > 0.0 ? a : d.number(second, 0, otherwise);
}

// A value or, where the file gives none (or 0), its default.
double positive(double value, double otherwise) {
    return value > 0.0 ? value : otherwise;
}

} // namespace

AirBrake::AirBrake(const VehicleDefinition &d) {
    const QString system = d.brakeSystemType.toLower();
    const QStringList equipment = d.text("wagon(brakeequipmenttype").toLower().remove(u' ').split(u',');
    if (equipment.contains("distributor") || equipment.contains("graduated_release_triple_valve"))
        valve_ = Valve::Distributor;
    else if (equipment.contains("triple_valve") || equipment.contains("distributing_valve"))
        valve_ = Valve::TripleValve;
    else if (system.startsWith("vacuum") || system == "straight_vacuum_single_pipe")
        // Until vacuum brakes have a model of their own.
        valve_ = Valve::TripleValve;
    if (system == "vacuum_piped" || system == "air_piped" || system == "manual_braking")
        valve_ = Valve::None;
    ep_ = EpBrakes(d);
    twin_ = TwinPipe(d);
    const QString charging = d.text("wagon(ortsmainrespipeauxrescharging").trimmed().toLower();
    mainPipeCharging = twin_ && charging != "0" && charging != "false";

    maxBrakeForce = d.number("wagon(maxbrakeforce", 0, 89e3);
    // The cylinder's pressure for MaxBrakeForce, and its limits: none
    // where the file gives none.
    maxCylinder = positive(d.number("wagon(brakecylinderpressureformaxbrakebrakeforce"), Infinite);
    reference = positive(d.number("wagon(ortsbrakeforcereferencepressure"),
                         std::isfinite(maxCylinder) ? maxCylinder : 64.0 * Psi);
    maxCylinder = positive(d.number("wagon(ortsmaxtriplevalvecylinderpressure"), maxCylinder);
    serviceMaxCylinder = positive(d.number("wagon(ortsmaxservicecylinderpressure"), maxCylinder);
    spring = std::max(0.0, d.number("wagon(ortscylinderspringpressure"));
    ratio = positive(d.number("wagon(triplevalveratio"), 2.5);
    releaseRate = eitherOf(d, "wagon(maxreleaserate", "wagon(brakedistributorreleaserate", 1.86 * Psi);
    applicationRate = eitherOf(d, "wagon(maxapplicationrate", "wagon(brakedistributorapplicationrate", 0.9 * Psi);
    serviceApplicationRate = positive(d.number("wagon(ortsmaxserviceapplicationrate"), applicationRate);
    chargingRate = positive(d.number("wagon(maxauxilarychargingrate"), 1.684 * Psi);
    // The auxiliary reservoir: Open Rails' own key, else the emergency
    // reservoir's share.
    auxiliaryVolume = eitherOf(d, "wagon(ortsauxiliaryrescapacity", "wagon(ortsauxilaryrescapacity", 0.0);
    if (auxiliaryVolume <= 0.0)
        auxiliaryVolume = positive(d.number("wagon(emergencyrescapacity"), 0.07)
                / positive(d.number("wagon(emergencyresvolumemultiplier"), 1.4);
    // A 32 mm pipe along it.
    pipeVolume_ = positive(d.number("wagon(brakepipevolume"),
                           0.032 * 0.032 * M_PI / 4.0 * std::max(5.0, 1.0 + double(d.size[2])));
    const bool distributor = valve_ == Valve::Distributor;
    sensitivity = (distributor ? 1.4 : 1.0) * Psi;
    initialThreshold = positive(d.number("wagon(ortsinitialapplicationthreshold"), (distributor ? 2.2 : 1.0) * Psi);
}

void AirBrake::charge(double pipe) {
    auxiliary_ = control = pipe;
    cylinder_ = 0.0;
    state_ = State::Release;
}

void AirBrake::step(double dt, double &pipe, double ep, double *mainPipe) {
    // The EP wire: its demand holds the cylinder from releasing below it
    // and fills it up to it from the reservoir.
    double demanded = 0.0;
    if (ep_ && ep > 0.0)
        demanded = std::min(ep, 1.0) * (std::isfinite(serviceMaxCylinder) ? serviceMaxCylinder : reference);
    const double before = cylinder_;
    stepValve(dt, pipe, twin_ ? mainPipe : nullptr);
    // Its holding wire (-2) keeps the cylinder as it is.
    const bool holding = ep_ && (ep < -1.5 || (demanded > 0.0 && before <= demanded));
    if (holding && cylinder_ < before)
        cylinder_ = before;
    if (demanded > cylinder_) {
        double dp = std::min(dt * serviceApplicationRate, demanded - cylinder_);
        if (auxiliary_ - dp / ratio < cylinder_ + dp)
            dp = (auxiliary_ - cylinder_) * ratio / (1.0 + ratio);
        dp = std::max(dp, 0.0);
        auxiliary_ -= dp / ratio;
        cylinder_ += dp;
    }
}

void AirBrake::stepValve(double dt, double &pipe, double *mainPipe) {
    // Its state from the pipe.
    const double applied = control - pipe;
    switch (valve_) {
    case Valve::Distributor: {
        const double target = applied * ratio;
        if (target > cylinder_ + (state_ == State::Apply ? 0.0 : sensitivity * ratio)) {
            if (state_ != State::Release || applied > initialThreshold)
                state_ = State::Apply;
        } else if (target < cylinder_ - (state_ == State::Release ? 0.0 : sensitivity * ratio)
                   || applied < initialThreshold) {
            state_ = State::Release;
        } else {
            state_ = State::Lap;
        }
        break;
    }
    case Valve::TripleValve:
        if (pipe < auxiliary_ - (state_ == State::Apply ? 0.0 : sensitivity)) {
            if (state_ != State::Release || pipe < auxiliary_ - initialThreshold)
                state_ = State::Apply;
        } else if (pipe > auxiliary_ + (state_ == State::Release ? 0.0 : sensitivity * 1.5)) {
            state_ = State::Release;
        } else if (state_ == State::Apply) {
            state_ = State::Lap;
        }
        break;
    case Valve::None:
        state_ = State::Release;
        break;
    }
    // The pressure its cylinder goes to: a distributor's from the pipe's
    // fall, a triple valve's as high as the reservoir gives.
    double threshold = 0.0;
    if (valve_ == Valve::Distributor) {
        threshold = std::max(applied * ratio, 0.0);
        if (threshold < initialThreshold * ratio)
            threshold = 0.0;
    } else if (state_ != State::Release) {
        threshold = maxCylinder;
    }
    threshold = std::min({threshold, serviceMaxCylinder, maxCylinder});

    if (state_ == State::Apply) {
        // Air from the reservoir into the cylinder, slower near either's
        // end (Open Rails', so the valve does not flicker).
        double dp = dt * serviceApplicationRate;
        if (pipe > auxiliary_ - Psi)
            dp *= 0.1 + 0.9 * (auxiliary_ - pipe) / Psi;
        else if (valve_ == Valve::Distributor && cylinder_ > threshold - Psi)
            dp *= 0.1 + 0.9 * (threshold - cylinder_) / Psi;
        if (auxiliary_ - dp / ratio < cylinder_ + dp)
            dp = (auxiliary_ - cylinder_) * ratio / (1.0 + ratio);
        if (cylinder_ + dp > threshold)
            dp = threshold - cylinder_;
        if (pipe > auxiliary_ - dp / ratio)
            dp = (auxiliary_ - pipe) * ratio;
        dp = std::max(dp, 0.0);
        cylinder_ += dp;
        auxiliary_ -= dp / ratio;
    }
    const double reservoirToPipe = auxiliaryVolume / pipeVolume_;
    if (state_ == State::Release && valve_ != Valve::None) {
        if (valve_ == Valve::Distributor && control < pipe)
            control = pipe;
        // Open Rails' insensitivity: a little air back from the reservoir
        // keeps small pipe changes from working the valve.
        if (auxiliary_ > pipe) {
            double dp = dt * 0.07 * Psi;
            if (auxiliary_ - dp < pipe + dp * reservoirToPipe)
                dp = (auxiliary_ - pipe) / (1.0 + reservoirToPipe);
            auxiliary_ -= dp;
            pipe += dp * reservoirToPipe;
        }
    }
    if (state_ == State::Release && cylinder_ > threshold) {
        double dp = dt * releaseRate;
        if (threshold > 0.0 && cylinder_ < threshold + Psi)
            dp *= 0.1 + 0.9 * (cylinder_ - threshold) / Psi;
        cylinder_ -= std::clamp(dp, 0.0, cylinder_ - threshold);
    }
    // The reservoir charged: a twin pipe distributor's from the main
    // reservoir pipe up to its control reservoir, else from the brake pipe.
    if (mainPipe != nullptr && mainPipeCharging && valve_ == Valve::Distributor && *mainPipe > pipe) {
        if (auxiliary_ < *mainPipe && auxiliary_ < control) {
            double dp = dt * chargingRate;
            if (auxiliary_ + dp > *mainPipe - dp * reservoirToPipe)
                dp = (*mainPipe - auxiliary_) / (1.0 + reservoirToPipe);
            auxiliary_ += dp;
            *mainPipe -= dp * reservoirToPipe;
        }
    } else if (auxiliary_ < pipe && (valve_ == Valve::Distributor || state_ == State::Release)) {
        double dp = dt * chargingRate;
        if (auxiliary_ > pipe - Psi)
            dp *= 0.1 + 0.9 * (pipe - auxiliary_) / Psi;
        if (auxiliary_ + dp > pipe - dp * reservoirToPipe)
            dp = (pipe - auxiliary_) / (1.0 + reservoirToPipe);
        auxiliary_ += dp;
        pipe -= dp * reservoirToPipe;
    } else if (valve_ == Valve::TripleValve) {
        // Its weeping port, very slowly.
        double dp = dt * 0.07 * Psi / 6.0 * (pipe > auxiliary_ ? 1.0 : pipe < auxiliary_ ? -1.0 : 0.0);
        if (std::fabs(auxiliary_ - pipe) < 0.1 * Psi)
            dp *= std::fabs(auxiliary_ - pipe) / (0.1 * Psi);
        auxiliary_ += dp;
        pipe -= dp * reservoirToPipe;
    }
    pipe = std::max(pipe, 0.0);
}

void AirBrake::independent(double dt, double pressure, double bailOff) {
    independent_ = std::max(0.0, pressure);
    if (bailOff > 0.0) {
        cylinder_ = std::max(0.0, cylinder_ - bailOff * dt);
        if (valve_ == Valve::Distributor)
            control = 0.0;
    }
}

double AirBrake::force(double speed) const {
    if (maxBrakeForce <= 0.0 || reference <= spring)
        return 0.0;
    const double share = std::max((cylinder() - spring) / (reference - spring), 0.0);
    // Open Rails' brake shoe friction for stock whose MaxBrakeForce assumed
    // a friction of 0.2 (Curtius-Kniffler): the full force at low speed,
    // less above 20 km/h.
    const double kph = std::fabs(speed) * 3.6;
    const double friction = std::clamp((7.6 / (kph + 17.5) + 0.07) / 0.2, 0.01, 1.0);
    return maxBrakeForce * share * friction;
}

void BrakePipe::step(double dt) {
    const size_t n = pressure.size();
    if (n < 2 || dt <= 0.0)
        return;
    // V_i (p_i' - p_i) / dt = g_(i-1) (p_(i-1)' - p_i') + g_i (p_(i+1)' - p_i'),
    // g_i the flow between i and i + 1: V_i V_(i+1) / (V_i + V_(i+1)) / T.
    std::vector<double> g(n - 1), upper(n), rhs(n);
    for (size_t i = 0; i + 1 < n; ++i)
        g[i] = joined.empty() || joined[i] ? volume[i] * volume[i + 1] / (volume[i] + volume[i + 1]) / timeFactor : 0.0;
    // Thomas: forward elimination, then back.
    double previousUpper = 0.0, previousRhs = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double left = i > 0 ? g[i - 1] : 0.0;
        const double right = i + 1 < n ? g[i] : 0.0;
        const double diagonal = volume[i] / dt + left + right + left * previousUpper;
        upper[i] = -right / diagonal;
        rhs[i] = (volume[i] * pressure[i] / dt + left * previousRhs) / diagonal;
        previousUpper = upper[i];
        previousRhs = rhs[i];
    }
    pressure[n - 1] = rhs[n - 1];
    for (size_t i = n - 1; i-- > 0;)
        pressure[i] = rhs[i] - upper[i] * pressure[i + 1];
}

void BrakePipe::reverse() {
    std::reverse(volume.begin(), volume.end());
    std::reverse(pressure.begin(), pressure.end());
    std::reverse(joined.begin(), joined.end());
}

bool EpBrakes(const VehicleDefinition &d) {
    const QString system = d.brakeSystemType.toLower();
    return system == "ep" || system == "ecp" || system == "ep_single_pipe";
}

bool TwinPipe(const VehicleDefinition &d) {
    const QString system = d.brakeSystemType.toLower();
    return system == "air_twin_pipe" || system == "ep" || system == "ecp";
}

double MainPipePressure(const VehicleDefinition &d) {
    const double reservoir = positive(d.number("engine(airbrakesmainmaxairpressure"), 130.0 * Psi);
    return std::min(reservoir, positive(d.number("engine(airbrakemaxmainrespipepressure"), reservoir + 5.0 * Psi));
}

double BrakeSystemPressure(const VehicleDefinition &d) {
    return positive(d.number("engine(trainbrakescontrollermaxsystempressure"), 90.0 * Psi);
}

double BrakePipeTimeFactor(const VehicleDefinition &d) {
    return positive(d.number("engine(ortsbrakepipetimefactor"), 0.0015);
}

BrakeValve::BrakeValve(const VehicleDefinition &d)
    : maxPressure(BrakeSystemPressure(d)),
      fullServiceDrop(positive(d.number("engine(trainbrakescontrollerfullservicepressuredrop"), 26.0 * Psi)),
      chargingRate(positive(d.number("engine(ortsbrakepipechargingrate"), 21.0 * Psi)),
      emergencyRate(positive(d.number("engine(trainbrakescontrolleremergencyapplicationrate"), 10.0 * Psi)),
      serviceTime(positive(d.number("engine(ortsbrakeservicetimefactor"), 1.009)),
      emergencyTime(positive(d.number("engine(ortsbrakeemergencytimefactor"), 0.1)),
      equalising(maxPressure) {}

double BrakeValve::step(double dt, double position, bool emergency, double pipe, double volume) {
    if (emergency)
        equalising = std::max(0.0, equalising - dt * emergencyRate);
    else
        equalising = maxPressure - fullServiceDrop * std::clamp(position, 0.0, 1.0);
    double dp = 0.0;
    if (emergency) {
        dp = -std::min(dt / emergencyTime, 0.95) * pipe;
    } else if (pipe < equalising) {
        // Charged, slower as it nears the reservoir (Open Rails' feed
        // valve).
        dp = dt * chargingRate;
        const double slowdown = chargingRate / 4.0;
        if (equalising - pipe < slowdown)
            dp *= std::cbrt((equalising - pipe) / slowdown);
        dp = std::min(dp, equalising - pipe);
    } else if (pipe > equalising) {
        // Vented, slower within 5 psi of it.
        dp = std::min(dt / serviceTime, 0.95) * pipe;
        if (equalising > pipe - 5.0 * Psi)
            dp *= std::min(std::sqrt((pipe - equalising) / (5.0 * Psi)), 1.0);
        dp = -std::min(dp, pipe - equalising);
    }
    return dp * volume;
}

}
