#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>
#include <memory>
#include <variant>
#include <vector>
#include <tsre/sound/CarSoundInput.h>

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
    // The brake pipe at it: its pressure (Pa) and its volume there (m³);
    // its brake cylinder's pressure (Pa).
    double brakePipe = 0.0;
    double brakePipeVolume = 0.0;
    double brakeCylinder = 0.0;
    // The main reservoir pipe at it (Pa; its volume there is the brake
    // pipe's), 0 for a vehicle without one (single pipe).
    double mainPipe = 0.0;
    // Its cab is the one driven (commands without a vehicle go to it).
    bool cab = false;
};

// What a vehicle's model gives each step.
struct VehicleOutputs {
    // N, + toward its front.
    double motiveForce = 0.0;
    // N >= 0, against its motion: brakes of its own beside its air brake
    // (the host's, on the brake pipe).
    double brakeForce = 0.0;
    // Air its brake valve lets into the brake pipe this step (Pa·m³; - out),
    // and its main reservoir into the main reservoir pipe.
    double brakePipeAir = 0.0;
    double mainPipeAir = 0.0;
    // A loco's independent (engine) brake: the pressure its cylinder gets
    // at least (Pa), and the rate its automatic brake bails off (Pa/s, 0
    // none).
    double independentBrake = 0.0;
    double bailOff = 0.0;
    // Messages to the train's other units this step (the control line): a
    // driving cab's commands. The host passes each to every other vehicle's
    // model; it reads one name itself, "epBrake" (the EP wire's demand).
    std::vector<Message> line;
    // Values that hold, by name: what its cab and the UI show, sent on when
    // they change. The host adds its own parts' (brakePipe, brakeCylinder).
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
    // A message from another unit on the control line (not to be sent on
    // again).
    virtual void fromLine(const Message &message) { command(message); }
    // It drives the train (an engine): commands without a vehicle go to it.
    virtual bool drives() const { return false; }
    // Its state as text, and back (a script's state table); false where it
    // has none to save or the text does not fit.
    virtual bool save(QByteArray &text) { (void)text; return false; }
    virtual bool restore(const QByteArray &text) { (void)text; return false; }
};

// A driven vehicle's sound input from its values (task sound 02): its
// brakeCylinder (Pa), the host's tractiveEffort and tractivePower, and what
// its script publishes for Open Rails' sound controls: variable1 (engine 1;
// variable1_2... the others), variable2, variable2Booster, variable3,
// engineRpm, enginePower (W), engineTorque (N·m) (engineRpm2... the other
// engines), backPressure (Pa). Missing values are 0.
CarSoundInput carSoundInput(const QHash<QString, MessageValue> &values);

// The model of a vehicle without a script (task driving 02): an engine gets
// a stand-in force (throttle × MaxForce up to MaxPower / v, below
// MaxVelocity) and brake valve until its kind has a script; any other
// vehicle, a wagon (no force; its brakes are the host's air brake).
std::unique_ptr<VehicleModel> MakeVehicleModel(const VehicleDefinition &definition);

}
