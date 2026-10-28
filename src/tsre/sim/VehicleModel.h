#pragma once
#include <QHash>
#include <QString>
#include <memory>
#include <variant>
#include <vector>

struct VehicleDefinition;

namespace Sim {

// A message (task driving 02): a name and a value, both free. The engine
// reads the few names of its own parts and passes the rest on, so a script
// and its cab can talk of what TSRE knows nothing about
// (CAB_AMBIENT_LIGHT = disable). Written as text "name = value".
using MessageValue = std::variant<bool, double, QString>;
struct Message {
    QString name;
    MessageValue value = 0.0;

    // The value as a number (true 1, false 0, a text read as a number), as
    // text, as a flag (non-zero, "on", "up", "close", "true", "yes").
    double number(double otherwise = 0.0) const;
    QString text() const;
    bool flag() const;
    // "name = value"; and back (false for text that is not one).
    QString toText() const;
    static bool fromText(const QString &text, Message &out);
};

// What a vehicle's model is given each step, in the vehicle's own frame:
// + toward its front as built (the host turns it for a flipped vehicle and
// for a train running backwards).
struct VehicleInputs {
    double dt = 0.0;
    // m/s.
    double speed = 0.0;
    // The sine of the track's slope, + uphill toward its front.
    double gradient = 0.0;
    // The most force its driven wheels hold (N): its weight on them × the
    // rail's adhesion.
    double adhesionLimit = 0.0;
    // The line's voltage (V), the route's.
    double lineVoltage = 0.0;
    // Its cab is the one driven (commands without a vehicle go to it).
    bool cab = false;
};

// What a vehicle's model gives each step.
struct VehicleOutputs {
    // N, + toward its front.
    double motiveForce = 0.0;
    // N >= 0, against its motion (its brakes).
    double brakeForce = 0.0;
    // Values that hold, by name: what its cab and the UI show, sent on when
    // they change.
    QHash<QString, MessageValue> values;
    // Messages it sends this step, in order (sound triggers as "sound").
    std::vector<Message> events;
};

// A vehicle's model: its forces and state from its controls and the world.
class VehicleModel {
public:
    virtual ~VehicleModel() = default;
    virtual void step(const VehicleInputs &in, VehicleOutputs &out) = 0;
    // A message for it (a control, a command, a name of a script's own).
    virtual void command(const Message &message) { (void)message; }
    // It drives the train (an engine): commands without a vehicle go to it.
    virtual bool drives() const { return false; }
};

// The model of a vehicle without a script (task driving 02): an engine gets
// a stand-in force (throttle × MaxForce up to MaxPower / v, below
// MaxVelocity) until its kind has a script; any other vehicle, a wagon (no
// force; its brakes come with the brake pipe).
std::unique_ptr<VehicleModel> MakeVehicleModel(const VehicleDefinition &definition);

}
