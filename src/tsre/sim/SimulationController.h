#pragma once
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <memory>
#include <vector>
#include <tsre/environment/WorldClock.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/world/objects/SignalLights.h>

class Activity;
class QThread;
class RenderQueue;
struct MapFeatures;

namespace Sim {

class SimulationHost;

// The simulation on the main thread (task simulation 01, sections 6 and
// 7): starts it on its own thread with an input made from the activity,
// sends the commands, takes the frames and events, and shows the trains
// (3D, map, the Timetable window's states). While a simulation exists
// (from start until Stop; Pause keeps it) the activity's data is not
// edited (ActivityNotifier::editingLocked), the world's clock is the
// simulation's (WorldClock's source), and so are the signals' lights.
class SimulationController : public QObject, public WorldClock::Source, public SignalLights::StateSource {
    Q_OBJECT
public:
    // Speeds offered, simulated seconds a second; 0 as fast as possible.
    static const std::vector<double> &Speeds();

    static SimulationController *instance();
    ~SimulationController() override;

    bool exists() const { return host != nullptr; }
    // Running or running to a time, as last asked (or as the host ended a
    // run to a time).
    bool runs() const { return exists() && asked != RunMode::Paused; }
    // The activity simulated, or none.
    Activity *activity() const { return current; }
    // A simulation of this activity exists: its trains are drawn where the
    // simulation has them, not at their starts.
    bool shows(const Activity *activity) const { return exists() && activity == current; }

    // A simulation of the activity, its input made from the editor's data
    // now, paused at its start; the world's time of day set to the start.
    // False, with why, when it has no train to run.
    bool start(Activity *activity, QString *error = nullptr);
    // Runs at the speed (starting a simulation of `activity` when none
    // exists); false when one cannot start.
    bool play(Activity *activity, QString *error = nullptr);
    void pause();
    void setSpeed(double speed);
    double speed() const { return pace; }
    // As fast as possible up to a time of day (seconds), then paused there;
    // a time before the simulation's is reached again from the start.
    bool runTo(Activity *activity, int timeOfDay, QString *error = nullptr);
    // A new input from the editor's data, back at the start, running on
    // when it ran.
    void reset();
    void stop();

    std::shared_ptr<const SimulationInput> input() const { return in; }
    // The latest frame, from the host's mailbox (the views draw it each
    // time); an empty one when none exists.
    std::shared_ptr<const SimulationFrame> frame() const;
    // The run's events so far, in order.
    const std::vector<TrainEvent> &events() const { return history; }
    // Milliseconds the input took to make.
    qint64 inputMilliseconds() const { return inputMs; }
    // The sequence number of the last command sent.
    unsigned long long lastCommand() const { return sequence; }
    // Waits until the host has taken command `seq` (tests); false after
    // `ms` milliseconds. The frame and events are taken (poll).
    bool waitFor(unsigned long long seq, int ms);
    // Takes the latest frame and the events (a timer does, about ten times a
    // second).
    void poll();

    // The simulated train of an editor train: the player's, or an AI
    // train's place in the activity's traffic; -1 when it is not simulated.
    int trainOf(bool player, int traffic) const;
    // The train chosen in the Timetable window (outlined), and the one the
    // camera follows; -1 none.
    int chosen() const { return chosenTrain; }
    void choose(int train);
    int followed() const { return followedTrain; }
    void follow(int train);
    // A train's centre and the way it heads (unit, in the camera's space):
    // tile x, tile z (the camera's), x, y, z; its length; false when not on
    // the track.
    bool trainPlace(int train, int *tile, float *position, float *heading, float *length = nullptr) const;

    // The world's clock while a simulation exists (WorldClock::Source): the
    // simulation's time of day and date (days from the world's date at
    // start) and how fast it runs now (0 paused). A run as fast as possible
    // leaves the world at the time it had until the run pauses. Setting the
    // time runs to it on the day shown (from the start again when it is
    // earlier), running on after when it ran.
    void clockNow(double &hours, QString &date, double &rate) const override;
    bool clockRunning() const override { return runs(); }
    double clockSpeed() const override { return speed(); }
    void setClockRunning(bool on) override;
    void setClockSpeed(double secondsPerSecond) override { setSpeed(secondsPerSecond); }
    void setClockTime(double hours) override;
    void setClockDate(const QString &date) override;
    // The heads' draw states for their lights, from the frame taken last.
    unsigned long long stateSerial() const override;
    QString drawStateOf(int item) const override;

    // What a train is doing ("running 72 km/h", "at Settle until 08:31"),
    // and an event as the Timetable window lists it.
    QString stateText(int train) const;
    QString eventText(const TrainEvent &event) const;
    // A train as the Timetable window names it ("Local 09:30"); a section's
    // holder (a train, a standing consist).
    QString trainName(int train) const;
    QString holderName(int holder) const;
    // A signal of the network as the editor names it (its TDB item); why a
    // train waits ("at signal 1234: the track beyond is held by ...").
    QString signalName(int signal) const;
    QString waitText(WaitFor waitFor, int signal, int holder) const;
    QString statusText() const;

    // The simulated trains in 3D, with the ids of the trains at their starts
    // (Activity::pushRenderItems); on the map, a train's vehicles (all
    // trains' for -1).
    void pushRenderItems(RenderQueue &queue, float *playerT, int renderMode);
    void getMapFeatures(MapFeatures &vehicles, int tileX, int tileZ, int train = -1) const;
    // The 3D and map id of a train: SelectionIdCodec::activityService's.
    int serviceId(int train) const;

signals:
    // Started, paused, running, stopped.
    void stateChanged();
    void frameUpdated();
    // Events from `first` on in events().
    void eventsAdded(int first);
    void choiceChanged(int train);
    void followChanged(int train);

private:
    SimulationController();
    SimulationHost *host = nullptr;
    QThread *thread = nullptr;
    Activity *current = nullptr;
    std::shared_ptr<const SimulationInput> in;
    std::shared_ptr<const SimulationFrame> latest;
    std::vector<TrainEvent> history;
    QTimer pollTimer;
    unsigned long long sequence = 0;
    double pace = 1.0;
    RunMode asked = RunMode::Paused;
    int chosenTrain = -1;
    int followedTrain = -1;
    qint64 inputMs = 0;
    // The world's date at start, and the simulation's time it shows (kept
    // while a run as fast as possible goes on).
    QString startDate;
    mutable double worldTime = 0.0;

    // A new input for the current activity; null with why when it has no
    // train.
    std::shared_ptr<const SimulationInput> makeInput(Activity *activity, QString *error);
    void sendStart(std::shared_ptr<const SimulationInput> input);
    // To `target` seconds (after midnight of the start day) as fast as
    // possible, from the start again when it is behind.
    bool runToTime(double target, bool thenRun, QString *error);
};

}
