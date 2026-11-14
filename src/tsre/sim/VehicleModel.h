#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>
#include <memory>
#include <variant>
#include <vector>
#include <tsre/sound/CarSoundInput.h>
#include <tsre/trains/CabControls.h>
#include <tsre/trains/CarExhaust.h>

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
    // The outside air's temperature (°C) where its train is (task driving
    // 06).
    double airTemperature = 15.0;
    // The brake pipe at it: its pressure (Pa) and its volume there (m³);
    // its brake cylinder's pressure (Pa).
    double brakePipe = 0.0;
    double brakePipeVolume = 0.0;
    double brakeCylinder = 0.0;
    // The main reservoir pipe at it (Pa; its volume there is the brake
    // pipe's), 0 for a vehicle without one (single pipe).
    double mainPipe = 0.0;
    // The train's steam heating pipe at it (Pa gauge, task driving 06), and
    // the steam the pipe took from it (kg/s) where it feeds the pipe.
    double heatingPipe = 0.0;
    double heatingSteam = 0.0;
    // Its cab is the one driven (commands without a vehicle go to it).
    bool cab = false;
    // The way its driver faces (task driving 08), the way its train runs: 1
    // toward its front, -1 toward its rear (Open Rails' rear cab): the
    // driver's left and right (doors, windows) on its own sides.
    int cabFacing = 1;
    // A loco no unit works (task driving 10): outside its train's control
    // group (another family's, its control lines cut, or cut out in its
    // consist). It starts completely powered off, not failed: a crew may
    // switch it on.
    bool offline = false;
    // Its own brake's pressure the train brake asks of an ED brake (Pa,
    // MaSzyna's Hamulec->GetEDBCP, task driving 10 step 6); -1 while the
    // host gives none.
    double edBrakePressure = -1.0;
    // The train's power lines at it (task driving 10, step 8; V): what the
    // other vehicles put on its high voltage line, on its heating cable
    // (the high voltage a heating source sends along), on its 110 V and
    // 24 V lines, through the joints that carry each (MaSzyna's
    // GetTrainsetVoltage); 0 where none reaches it.
    double trainHighVoltage = 0.0;
    double trainHeatingVoltage = 0.0;
    double train110V = 0.0;
    double train24V = 0.0;
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
    // Its ED brake's share of its own air brake's (0 to 1, MaSzyna's
    // Hamulec->SetED: the air let off as ED brakes; task driving 10 step 6).
    double edBrake = 0.0;
    // The pressure it holds on the train's steam heating pipe (Pa gauge, 0
    // none): its valve on its boiler, its heating boiler. The highest feeds.
    double heatingSupply = 0.0;
    // What it puts on the train's power lines (task driving 10, step 8;
    // V, 0 nothing): its own high voltage (its pantographs', or its train
    // heating's source while it heats), and whether that goes onto heating
    // cables too (its heating on: MaSzyna's power export over a heating
    // coupling); its converter's 110 V, its battery's 24 V.
    double highVoltage = 0.0;
    bool heatingCable = false;
    double supply110V = 0.0;
    double supply24V = 0.0;
    // Its own electric heaters work (1) or not (0): the model's heating
    // (MaSzyna's Heating flag); -1 where the model does not say, the host
    // then heating it whenever a high voltage reaches it.
    int heating = -1;
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
    // Its 3D cab's controls' values (task driving 08), one per control of
    // `controls` in its units, NaN where it has none, the train going at
    // `speed` (m/s); false where the model gives none. `stops`, when asked:
    // the stops its script gives them (task maszyna 04), null for none.
    virtual bool cabValues(const CabControls &controls, double speed, std::vector<double> &values,
                           std::shared_ptr<const CabStops> *stops = nullptr) {
        (void)controls;
        (void)speed;
        (void)values;
        (void)stops;
        return false;
    }
};

// A driven vehicle's sound input from its values (task sound 02): its
// brakeCylinder (Pa), the host's tractiveEffort and tractivePower, and what
// its script publishes for Open Rails' sound controls: variable1 (engine 1;
// variable1_2... the others), variable2, variable2Booster, variable3,
// engineRpm, enginePower (W), engineTorque (N·m) (engineRpm2... the other
// engines), backPressure (Pa). Missing values are 0.
CarSoundInput carSoundInput(const QHash<QString, MessageValue> &values);

// A vehicle's exhaust from its values (task driving 05): for each of
// `kinds` (its definition's exhaustKinds), what its model publishes as
// fx.<kind>.flow (m³/s), .speed (m/s), .temperature (°C), .steam and .soot
// (0 to 1), .beats (a second), .color (ARGB). Missing values are 0: off.
CarExhaust carExhaust(const QHash<QString, MessageValue> &values, const QStringList &kinds);

// The model of a vehicle without a script (task driving 02): an engine gets
// a stand-in force (throttle × MaxForce up to MaxPower / v, below
// MaxVelocity) and brake valve until its kind has a script; any other
// vehicle, a wagon (no force; its brakes are the host's air brake).
std::unique_ptr<VehicleModel> MakeVehicleModel(const VehicleDefinition &definition);

}
