#pragma once
#include <QObject>
#include <QStringList>
#include <QString>
#include <unordered_map>
#include <vector>
#include <tsre/sim/SimulationTexts.h>

class Activity;

namespace Sim {

// An activity event in the last run of its activity (task simulation 06).
struct EventResult {
    // It was in the run (the input had it).
    bool inRun = false;
    // Its state when the run was last seen (EventState), and the train it
    // looked at when it fired, by name ("" none).
    EventState state;
    QString by;
    // Not fired: why (WhyNotFired); its name and the texts MSTS shows at
    // the activity's end (TextToDisplayOnCompletionIfTriggered and
    // IfNotTriggered: the one for how it went), for Check Activity.
    QString why;
    QString name;
    QString completion;
};

// What the last run of an activity did with its events: Play's (as it
// runs) or Simulate's.
struct EventResults {
    // By the event's place in the activity's list.
    std::vector<EventResult> events;
    // Up to when it ran (seconds after midnight), and whether to its end
    // (every train finished or settled).
    double time = 0.0;
    bool complete = false;
    ActivityEnd end = ActivityEnd::None;
    double endedAt = -1.0;
    // What the run's input noted on the trains it made
    // (SimulationInput::warnings): trains not simulated, stops not found,
    // trains driven simply (Check Activity lists them).
    QStringList warnings;
};

// The last run's results of each activity, kept for the session (main
// thread). They go out of date once the activity is edited.
class EventResultStore : public QObject {
    Q_OBJECT
public:
    static EventResultStore *instance();
    // A run's events (the input's list) as they are now, from `time` on.
    // The cars where the run left them (task 07), else as the input has them.
    void record(Activity *activity, const SimulationInput &input, const std::vector<EventState> &states,
                ActivityEnd end, double endedAt, double time, bool complete, const CarsNow *cars = nullptr);
    // A run's input made (its start, again from the start): what it could
    // not make, for any activity (a timetable set has no events).
    void recordInput(Activity *activity, const QStringList &warnings);
    // The run went on to `time` (nothing else changed): no signal.
    void advance(const Activity *activity, double time, bool complete);
    // The activity was edited: its results are from before.
    void outdate(const Activity *activity);
    // The last run's results, null none; `current` whether the activity is
    // as it was then.
    const EventResults *of(const Activity *activity, bool *current = nullptr) const;
    // Changes whenever any results do (the map builds again).
    unsigned long long version() const { return changes; }
    // Forgotten (tests).
    void forgetAll();

signals:
    void changed(const Activity *activity);

private:
    EventResultStore();
    struct Entry {
        EventResults results;
        bool current = true;
    };
    std::unordered_map<const Activity *, Entry> entries;
    unsigned long long changes = 0;
};

}
