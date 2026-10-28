#include <tsre/sim/VehicleModel.h>
#include <tsre/sim/AirBrake.h>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <tsre/trains/VehicleDefinition.h>

namespace Sim {

double Message::number(double otherwise) const {
    if (const bool *b = std::get_if<bool>(&value))
        return *b ? 1.0 : 0.0;
    if (const double *d = std::get_if<double>(&value))
        return *d;
    bool ok = false;
    const double read = std::get<QString>(value).toDouble(&ok);
    return ok ? read : otherwise;
}

QString Message::text() const {
    if (const bool *b = std::get_if<bool>(&value))
        return *b ? QStringLiteral("true") : QStringLiteral("false");
    if (const double *d = std::get_if<double>(&value))
        return QString::number(*d, 'g', 15);
    return std::get<QString>(value);
}

bool Message::flag() const {
    if (const bool *b = std::get_if<bool>(&value))
        return *b;
    if (const double *d = std::get_if<double>(&value))
        return *d != 0.0;
    static const QStringList on{"on", "up", "close", "closed", "true", "yes", "pressed"};
    return on.contains(std::get<QString>(value).trimmed(), Qt::CaseInsensitive);
}

QString Message::toText() const {
    return name + " = " + text();
}

bool Message::fromText(const QString &text, Message &out) {
    const qsizetype equals = text.indexOf(u'=');
    if (equals <= 0)
        return false;
    out.name = text.left(equals).trimmed();
    const QString value = text.mid(equals + 1).trimmed();
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (ok)
        out.value = number;
    else if (value == "true" || value == "false")
        out.value = value == "true";
    else
        out.value = value;
    return !out.name.isEmpty();
}

namespace {

// A control's new value from a message: a number, or "+"/"-" a step.
double controlValue(const Message &message, double now, double step, double lowest, double highest) {
    const QString text = message.text().trimmed();
    if (text == "+")
        return std::min(highest, now + step);
    if (text == "-")
        return std::max(lowest, now - step);
    return std::clamp(message.number(now), lowest, highest);
}

// A wagon: no force of its own yet (its brakes are the host's).
class Wagon : public VehicleModel {
public:
    void step(const VehicleInputs &, VehicleOutputs &out) override {
        out.motiveForce = 0.0;
        out.brakeForce = 0.0;
    }
};

// An engine until its kind has a script (task driving 02): its throttle
// gives throttle × MaxForce, at most MaxPower × throttle / v and nothing
// above MaxVelocity (Open Rails' fade over its last 0.05 m/s), the way its
// direction sets, within the rail's adhesion. Its train brake works its
// brake valve (no notches) and its emergency vents the pipe. Driving the
// train (its cab driven), it sends its throttle and direction to the other
// units on the control line, and the EP wire's demand where its brake is EP.
class StandInEngine : public VehicleModel {
public:
    explicit StandInEngine(const VehicleDefinition &d)
        : maxForce(d.forceN), maxPower(d.powerW), maxSpeed(d.maxSpeedMps), valve(d), ep(EpBrakes(d)),
          twin(TwinPipe(d)), mainPipe(MainPipePressure(d)) {}
    bool drives() const override { return true; }
    void command(const Message &m) override {
        if (m.name == "throttle") {
            throttle = controlValue(m, throttle, 0.1, 0.0, 1.0);
            pending.push_back({"throttle", throttle});
        } else if (m.name == "direction") {
            direction = int(std::lround(controlValue(m, direction, 1.0, -1.0, 1.0)));
            pending.push_back({"direction", double(direction)});
        } else if (m.name == "trainBrake") {
            brake = controlValue(m, brake, 0.1, 0.0, 1.0);
        } else if (m.name == "emergency") {
            emergency = m.flag();
        }
    }
    void step(const VehicleInputs &in, VehicleOutputs &out) override {
        double force = 0.0;
        if (direction != 0 && throttle > 0.0) {
            const double along = in.speed * direction;
            force = throttle * maxForce;
            if (maxSpeed > 0.0) {
                if (along >= maxSpeed)
                    force = 0.0;
                else if (along > maxSpeed - 0.05)
                    force *= (maxSpeed - along) / 0.05;
            }
            const double power = maxPower * throttle;
            const double v = std::fabs(in.speed);
            if (power > 0.0 && force * v > power)
                force = power / v;
            if (in.adhesionLimit > 0.0)
                force = std::min(force, in.adhesionLimit);
        }
        out.motiveForce = direction * force;
        out.brakeForce = 0.0;
        out.brakePipeAir = 0.0;
        // A twin pipe engine keeps the main reservoir pipe at its pressure
        // (its main reservoir endless).
        if (twin && in.mainPipe < mainPipe)
            out.mainPipeAir = (mainPipe - in.mainPipe) * in.brakePipeVolume;
        if (in.cab) {
            out.brakePipeAir = valve.step(in.dt, brake, emergency, in.brakePipe, in.brakePipeVolume);
            out.line = std::move(pending);
            const double demand = emergency ? 1.0 : brake;
            if (ep && demand != sentEp) {
                out.line.push_back({"epBrake", demand});
                sentEp = demand;
            }
        }
        pending.clear();
        out.values.insert("throttle", throttle);
        out.values.insert("trainBrake", brake);
        out.values.insert("emergency", emergency);
        out.values.insert("direction", double(direction));
        out.values.insert("force", out.motiveForce);
        out.values.insert("equalisingReservoir", valve.equalising);
        out.values.insert("lineVoltage", in.lineVoltage);
    }

private:
    double maxForce, maxPower, maxSpeed;
    BrakeValve valve;
    bool ep;
    bool twin;
    double mainPipe;
    double throttle = 0.0, brake = 0.0;
    int direction = 0;
    bool emergency = false;
    // Commands for the control line, sent at its next step if it drives.
    std::vector<Message> pending;
    double sentEp = -1.0;
};

} // namespace

std::unique_ptr<VehicleModel> MakeVehicleModel(const VehicleDefinition &definition) {
    if (definition.wagonTypeId >= 4)
        return std::make_unique<StandInEngine>(definition);
    return std::make_unique<Wagon>();
}

}
