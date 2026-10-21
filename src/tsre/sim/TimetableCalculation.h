#pragma once
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>
#include <vector>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationTexts.h>

class Activity;

namespace Sim {

// Simulate in the Timetable window (task simulation 04): the activity run
// as fast as possible, its trains' times written into their timetables.
// Nothing is stored beyond the activity's files: what the runs of each
// activity wrote is kept for this session, so a departure a run calculated
// is calculated again on the next run, and any other (the user's, the
// file's) is kept: the train leaves no earlier, as Open Rails takes a
// departure.
struct TimetableOptions {
    // Departures rounded up to it, the train waiting for them; arrivals
    // down (seconds; 0 none).
    int step = 30;
    // The player's times alone (always when other activities share the
    // traffic pattern).
    bool playerOnly = false;
    // Every departure of the trains written calculated again, none kept.
    bool fromScratch = false;
};

// A stop (a timetable row) of a train after a run.
struct TimetableStop {
    QString platform;
    // The simulation has it (its platform is on the train's path); the
    // train reached it.
    bool simulated = false;
    bool reached = false;
    // Its booked departure was kept: the train left no earlier.
    bool kept = false;
    // As written (seconds after midnight; -1 not written), and as compared
    // with: the run before's, else the timetable's before it (-1 none).
    int arrival = -1;
    int departure = -1;
    int arrivalBefore = -1;
    int departureBefore = -1;
};

struct TimetableTrain {
    bool player = false;
    QString service;
    int time = 0;
    // As the Timetable window names it.
    QString name;
    // Its times were written.
    bool written = false;
    // It finished its path or stands at its end; else where it stands and
    // why.
    bool finished = false;
    QString state;
    // By timetable row.
    std::vector<TimetableStop> stops;
    // Its stops whose times changed (or are new); the largest change (s).
    int changed() const;
    int largestChange() const;
};

struct TimetableRun {
    QStringList warnings;
    std::vector<TimetableTrain> trains;
    TimetableOptions options;
    // The player's alone because other activities share the pattern.
    QStringList sharedWith;
    // Simulated from, to (seconds after midnight of the start day); wall
    // time (ms).
    double from = 0.0;
    double to = 0.0;
    qint64 ms = 0;
    bool cancelled = false;
};

class TimetableCalculation {
public:
    // The run of `activity` (main thread); null with why when it has no
    // train to run.
    static std::unique_ptr<TimetableCalculation> Prepare(Activity *activity, const TimetableOptions &options,
                                                        QString *error = nullptr);
    // The simulation to its end: every train finished, at its path's end or
    // in a deadlock, or 24 hours after the start (any thread: it reads the
    // input alone).
    void run(const std::atomic<bool> *cancel = nullptr);
    // The times written into the activity's timetables and kept for the
    // next run (main thread; the caller makes the undo step).
    const TimetableRun &write(Activity *activity);
    const TimetableRun &result() const { return outcome; }
    std::shared_ptr<const SimulationInput> input() const { return in; }

    // The last run written for the activity, and its train (the player's,
    // or an AI train by service and start time); null none.
    static const TimetableRun *Last(const Activity *activity);
    static const TimetableTrain *LastTrain(const Activity *activity, bool player, const QString &service, int time);
    // The session's runs forgotten (tests).
    static void ForgetAll();

private:
    TimetableCalculation() = default;
    std::shared_ptr<const SimulationInput> in;
    TimetableRun outcome;
    // Per input train: its row in `outcome.trains`; per stop its departure
    // kept; when it arrived at and left each stop (-1 not).
    std::vector<int> trainOf;
    std::vector<std::vector<bool>> keptStop;
    std::vector<std::vector<double>> arrived;
    std::vector<std::vector<double>> departed;
    std::vector<TrainState> last;
    double endTime = 0.0;
    // The activity's events at the end (task 06), for EventResultStore.
    std::vector<EventState> eventStates;
    CarsNow carsAtEnd;
    ActivityEnd activityEnd = ActivityEnd::None;
    double activityEndedAt = -1.0;
    bool settled = false;
};

}
