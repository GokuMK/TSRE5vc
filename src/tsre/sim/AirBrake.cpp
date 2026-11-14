#include <tsre/sim/AirBrake.h>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tsre/maszyna/MaszynaBrake.h>
#include <tsre/maszyna/MaszynaHamulce.h>
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
    const QString system = d.brakeSystem().toLower();
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
    insensitivity = positive(d.number("wagon(ortsbrakeinsensitivity"), 0.07 * Psi);

    // The emergency reservoir: Open Rails' default volume, charging rate and
    // volume against the auxiliary's (the file's multiplier where the
    // auxiliary's volume comes from it).
    emergencyReservoir_ = equipment.contains("emergency_brake_reservoir");
    const double emergencyVolume = positive(d.number("wagon(emergencyrescapacity"), 0.07);
    emergencyRatio = emergencyVolume / auxiliaryVolume;
    emergencyChargingRate = positive(d.number("wagon(emergencyreschargingrate"), 1.684 * Psi);

    // The shoes (Open Rails' MSTSWagon): a known kind takes its own maximum
    // force where the file gives one; their number, else 4 to an axle.
    static const std::pair<const char *, Shoe> kinds[] = {
            {"Cast_Iron_P6", Shoe::CastIronP6}, {"Cast_Iron_P10", Shoe::CastIronP10},
            {"Disc_Pads", Shoe::DiscPads},       {"High_Friction_Composite", Shoe::HighFrictionComposite},
            {"User_Defined", Shoe::UserDefined}};
    const QString kind = d.text("wagon(ortsbrakeshoetype").trimmed();
    for (const auto &[name, value] : kinds)
        if (kind == QLatin1String(name))
            shoe_ = value;
    maxShoeForce = std::max(0.0, d.number("wagon(ortsmaxbrakeshoeforce"));
    if (maxShoeForce > 0.0 && shoe_ != Shoe::Unknown)
        maxBrakeForce = maxShoeForce;
    shoes = d.number("wagon(ortsnumbercarbrakeshoes");
    if (shoes <= 0.0) {
        double axles = positive(d.number("wagon(ortsnumberaxles"), d.number("wagon(numwheels"));
        axles = positive(axles, 4.0);
        if (!d.engType.isEmpty())
            axles += positive(d.number("engine(ortsnumberdriveaxles"), d.number("engine(numwheels"));
        shoes = 4.0 * axles;
    }
    // Its friction curve: pairs of km/h and friction.
    for (int i = 0;; i += 2) {
        const double v = d.number("wagon(ortsbrakeshoefriction", i, std::nan(""));
        const double f = d.number("wagon(ortsbrakeshoefriction", i + 1, std::nan(""));
        if (std::isnan(v) || std::isnan(f))
            break;
        frictionSpeed.push_back(v);
        frictionValue.push_back(f);
    }
    if (frictionSpeed.size() < 2)
        frictionSpeed.clear(), frictionValue.clear();
}

void AirBrake::charge(double pipe) {
    auxiliary_ = control = pipe;
    emergency_ = emergencyReservoir_ ? pipe : 0.0;
    cylinder_ = 0.0;
    state_ = State::Release;
    emergencyClock = 0.0;
    emergencyTimedOut = false;
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

void AirBrake::stepState(double pipe) {
    const State previous = state_;
    // Open Rails' emergency valve with an emergency reservoir (its legacy
    // one, the content giving no actuation rate): tripped with the pipe at
    // 0.75 of the pressure the cylinder would equalise at from that
    // reservoir.
    const bool tripped = emergencyReservoir_ && pipe <= 0.75 * emergency_ * ratio / (ratio + 1.0);
    const double applied = control - pipe;
    switch (valve_) {
    case Valve::Distributor: {
        const double target = applied * ratio;
        if (tripped) {
            state_ = State::Emergency;
        } else if (state_ != State::Emergency
                   && target > cylinder_ + (state_ == State::Apply ? 0.0 : sensitivity * ratio)) {
            if (previous != State::Release || applied > initialThreshold)
                state_ = State::Apply;
        } else if (target < cylinder_ - (state_ == State::Release ? 0.0 : sensitivity * ratio)
                   || applied < initialThreshold) {
            state_ = State::Release;
        } else if (state_ != State::Emergency || pipe > auxiliary_) {
            state_ = State::Lap;
        }
        break;
    }
    case Valve::TripleValve:
        if (tripped) {
            state_ = State::Emergency;
        } else if (state_ != State::Emergency
                   && pipe < auxiliary_ - (state_ == State::Apply ? 0.0 : sensitivity)) {
            if (previous != State::Release || pipe < auxiliary_ - initialThreshold)
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
    // Open Rails' dump valve timer starts with the emergency.
    if (state_ != State::Emergency || previous != State::Emergency) {
        emergencyClock = 0.0;
        emergencyTimedOut = false;
    }
}

void AirBrake::stepValve(double dt, double &pipe, double *mainPipe) {
    stepState(pipe);
    const double applied = control - pipe;
    // The pressure its cylinder goes to: in an emergency the most it takes;
    // a distributor's from the pipe's fall, a triple valve's as high as the
    // reservoir gives.
    double threshold = 0.0;
    if (state_ == State::Emergency) {
        threshold = maxCylinder;
    } else {
        if (valve_ == Valve::Distributor) {
            threshold = std::max(applied * ratio, 0.0);
            if (threshold < initialThreshold * ratio)
                threshold = 0.0;
        } else if (state_ != State::Release) {
            threshold = maxCylinder;
        }
        threshold = std::min({threshold, serviceMaxCylinder, maxCylinder});
    }

    const double reservoirToPipe = auxiliaryVolume / pipeVolume_;
    if (state_ == State::Apply || state_ == State::Emergency) {
        // Air from the reservoir into the cylinder, slower near either's
        // end (Open Rails', so the valve does not flicker); at the full
        // rate in an emergency.
        double dp = dt * (state_ == State::Emergency ? applicationRate : serviceApplicationRate);
        if (state_ != State::Emergency && pipe > auxiliary_ - Psi)
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
        if (state_ == State::Emergency && emergencyReservoir_) {
            if (emergencyTimedOut && pipe > 20.0 * Psi) {
                // Open Rails' accelerated emergency release, once its dump
                // valve's time is out and the pipe is recharging: the
                // cylinder's air back into the reservoir, the reservoir's
                // into the pipe.
                double dq = dt * releaseRate;
                if (cylinder_ - dq < auxiliary_ + dq / ratio)
                    dq = std::max((cylinder_ - auxiliary_) * ratio / (1.0 + ratio), 0.0);
                cylinder_ -= dq;
                auxiliary_ += dq / ratio;
                dq = dt * chargingRate;
                if (auxiliary_ - dq < pipe + dq * reservoirToPipe)
                    dq = std::max((auxiliary_ - pipe) / (1.0 + reservoirToPipe), 0.0);
                auxiliary_ -= dq;
                pipe += dq * reservoirToPipe;
            } else {
                // The emergency reservoir's air into the auxiliary, so the
                // cylinder fills higher.
                double dq = dt * applicationRate;
                if (emergency_ - dq < auxiliary_ + dq * emergencyRatio)
                    dq = (emergency_ - auxiliary_) / (1.0 + emergencyRatio);
                emergency_ -= dq;
                auxiliary_ += dq * emergencyRatio;
            }
        }
        if (state_ == State::Emergency) {
            emergencyClock += dt;
            if (emergencyClock > 120.0)
                emergencyTimedOut = true;
        }
    }
    if (state_ == State::Release && valve_ != Valve::None) {
        if (valve_ == Valve::Distributor) {
            if (control < pipe)
                control = pipe;
            else if (control < pipe + Psi)
                // Open Rails' overcharge elimination.
                control = std::max(control - dt * insensitivity, pipe);
        }
        // Open Rails' insensitivity: a little air back from the reservoir
        // keeps small pipe changes from working the valve.
        if (auxiliary_ > pipe) {
            double dp = dt * insensitivity;
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
    stepEmergencyReservoir(dt, pipe);
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
        double dp = dt * insensitivity / 6.0 * (pipe > auxiliary_ ? 1.0 : pipe < auxiliary_ ? -1.0 : 0.0);
        if (std::fabs(auxiliary_ - pipe) < 0.1 * Psi)
            dp *= std::fabs(auxiliary_ - pipe) / (0.1 * Psi);
        auxiliary_ += dp;
        pipe -= dp * reservoirToPipe;
    }
    pipe = std::max(pipe, 0.0);
}

void AirBrake::stepEmergencyReservoir(double dt, double &pipe) {
    if (!emergencyReservoir_)
        return;
    // Released: its air recharges the auxiliary reservoir up to the pipe
    // (Open Rails' quick recharge of older valves, its default).
    if (state_ == State::Release && emergency_ > pipe) {
        double dp = dt * chargingRate;
        if (auxiliary_ + dp > emergency_ - dp / emergencyRatio)
            dp = (emergency_ - auxiliary_) * emergencyRatio / (1.0 + emergencyRatio);
        if (pipe < auxiliary_ + dp)
            dp = pipe - auxiliary_;
        dp = std::max(dp, 0.0);
        auxiliary_ += dp;
        emergency_ -= dp / emergencyRatio;
    }
    // It charges from the auxiliary.
    if (auxiliary_ > emergency_ && (valve_ == Valve::Distributor || state_ == State::Release)) {
        double dp = dt * emergencyChargingRate;
        if (emergency_ + dp > auxiliary_ - dp * emergencyRatio)
            dp = (auxiliary_ - emergency_) / (1.0 + emergencyRatio);
        emergency_ += dp;
        auxiliary_ -= dp * emergencyRatio;
    }
}

void AirBrake::independent(double dt, double pressure, double bailOff) {
    independent_ = std::max(0.0, pressure);
    if (bailOff > 0.0) {
        cylinder_ = std::max(0.0, cylinder_ - bailOff * dt);
        if (valve_ == Valve::Distributor)
            control = 0.0;
    }
}

void AirBrake::values(const std::function<void(const char *, double)> &out) const {
    out("auxiliaryReservoir", auxiliary_);
    if (valve_ == Valve::Distributor)
        out("controlReservoir", control);
    if (emergencyReservoir_)
        out("emergencyReservoir", emergency_);
}

std::unique_ptr<VehicleBrake> MakeVehicleBrake(const VehicleDefinition &definition) {
    // A .fiz vehicle with a valve MaSzyna's port has: MaSzyna's brake (task
    // driving 10); else, and every ENG/WAG vehicle, Open Rails' after.
    if (std::unique_ptr<MaszynaBrake> maszyna = MaszynaBrake::make(definition))
        return maszyna;
    return std::make_unique<AirBrake>(definition);
}

double AirBrake::force(double speed) const {
    if (maxBrakeForce <= 0.0 || reference <= spring)
        return 0.0;
    const double shoeForce = maxBrakeForce * std::max((cylinder() - spring) / (reference - spring), 0.0);
    return shoeForce * shoeFriction(speed, shoeForce);
}

double AirBrake::shoeFriction(double speed, double shoeForce) const {
    const double kph = std::fabs(speed) * 3.6;
    // Karwatzki's friction of a known shoe (Open Rails' constants after
    // Wende), by the force on each shoe (kN, at most 20).
    const auto karwatzki = [&](double k1, double k2, double k3, double k4, double k5) {
        const double kn = std::min(shoeForce / 1000.0 / shoes, 20.0);
        return k1 * ((kn + k2) / (kn + k3)) * ((kph + k4) / (kph + k5));
    };
    // The file's curve at a speed, straight beyond its ends (Open Rails'
    // Interpolator).
    const auto curve = [this](double at) {
        const size_t n = frictionSpeed.size();
        size_t i = 0;
        if (at >= frictionSpeed[n - 2])
            i = n - 2;
        else
            while (i + 2 < n && at > frictionSpeed[i + 1])
                ++i;
        const double span = frictionSpeed[i + 1] - frictionSpeed[i];
        if (span == 0.0)
            return frictionValue[i];
        return frictionValue[i] + (frictionValue[i + 1] - frictionValue[i]) * (at - frictionSpeed[i]) / span;
    };
    switch (shoe_) {
    case Shoe::CastIronP6:
        return karwatzki(0.024, 62.5, 12.5, 100.0, 20.0);
    case Shoe::CastIronP10:
        return karwatzki(0.05, 62.5, 31.25, 100.0, 20.0);
    case Shoe::HighFrictionComposite:
        return karwatzki(0.055, 200.0, 50.0, 150.0, 75.0);
    case Shoe::DiscPads:
        return karwatzki(0.385, -24.5, -27.2, 39.5, 33.0);
    case Shoe::UserDefined:
        return frictionSpeed.empty() ? 0.0 : curve(kph);
    case Shoe::Unknown:
        break;
    }
    // Older stock, its MaxBrakeForce at a stand: the file's curve against
    // its value at 0, else Curtius-Kniffler's against a friction of 0.2; at
    // most 1.
    if (!frictionSpeed.empty()) {
        const double atStand = curve(0.0);
        return atStand != 0.0 ? std::clamp(curve(kph) / atStand, 0.01, 1.0) : 1.0;
    }
    return std::clamp((7.6 / (kph + 17.5) + 0.07) / 0.2, 0.01, 1.0);
}

void BrakePipe::step(double dt) {
    const size_t n = pressure.size();
    if (n < 2 || dt <= 0.0)
        return;
    // V_i (p_i' - p_i) / dt = g_(i-1) (p_(i-1)' - p_i') + g_i (p_(i+1)' - p_i'),
    // g_i the flow between i and i + 1: Open Rails' V_i V_(i+1) / (V_i +
    // V_(i+1)) / T, or MaSzyna's, below.
    std::vector<double> g(n - 1), upper(n), rhs(n);
    for (size_t i = 0; i + 1 < n; ++i) {
        if (!joined.empty() && !joined[i])
            g[i] = 0.0;
        else if (!laws.empty() && laws[i] == VehicleBrake::PipeLaw::Maszyna)
            g[i] = maszynaConductance(i);
        else
            g[i] = volume[i] * volume[i + 1] / (volume[i] + volume[i + 1]) / timeFactor;
    }
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

double BrakePipe::maszynaConductance(size_t i) const {
    using MaszynaHamulce::PF;
    using MaszynaHamulce::SpO;
    // Each length's orifice, else one from its volume: a 1" pipe of
    // (L + 0.5) SpO litres, S = SpO / (1 + 0.015 / SpO L).
    const auto orifice = [this](size_t k) {
        if (k < nozzle.size() && nozzle[k] > 0.0)
            return nozzle[k];
        const double length = std::max(volume[k] * 1000.0 / SpO - 0.5, 0.0);
        return SpO / (1.0 + 0.015 / SpO * length);
    };
    // GetDVc: each of the two vehicles moves 0.5 dt PF(p, p', its S) into
    // its own pipe from the other's: together PF at the mean orifice (PF is
    // linear in S), bar l/s. As a conductance at the step's start: PF over
    // the pressures' difference, at a vanishing difference PF's eased limit
    // 788 S sqrt(PH - DP) (DP 0.25 bar), l/s per bar; m3/s here (bar l to
    // Pa m3: 100, bar to Pa: 1e5).
    const double S = 0.5 * (orifice(i) + orifice(i + 1));
    const double p1 = pressure[i] / 1e5, p2 = pressure[i + 1] / 1e5;
    const double difference = p2 - p1;
    const double litres = std::fabs(difference) > 1e-9
            ? PF(p1, p2, S) / difference
            : 788.0 * S * std::sqrt(std::max(p1, p2) + 1.0 - 0.25);
    return litres * 1e-3;
}

void BrakePipe::reverse() {
    std::reverse(volume.begin(), volume.end());
    std::reverse(pressure.begin(), pressure.end());
    std::reverse(joined.begin(), joined.end());
    std::reverse(laws.begin(), laws.end());
    std::reverse(nozzle.begin(), nozzle.end());
}

bool EpBrakes(const VehicleDefinition &d) {
    const QString system = d.brakeSystem().toLower();
    return system == "ep" || system == "ecp" || system == "ep_single_pipe";
}

bool TwinPipe(const VehicleDefinition &d) {
    const QString system = d.brakeSystem().toLower();
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
