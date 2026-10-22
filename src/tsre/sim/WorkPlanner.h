#pragma once
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <vector>
#include <tsre/sim/SimulationInput.h>

namespace Sim {

class TrackNetwork;

// Planning the player's work orders for TSRE's autopilot (task simulation
// 07, part B). Phase B0: a tool on the local content, not run by the core.

// A train or a cut of cars standing: its vehicles from its end A to its end
// B and the track it covers the same way (end A at the first interval's
// `from`).
struct YardTrain {
    QString name;
    std::vector<Vehicle> vehicles;
    std::vector<RouteInterval> track;
    bool loose = false;
    double length() const;
};

// A move: the leading end's way, end B or end A leading; the train's track
// where it starts (from its trailing end to its leading one).
struct WorkLeg {
    bool leadB = true;
    std::vector<RouteInterval> way;
    std::vector<RouteInterval> body;
    double length() const;
};

// A step of a plan: its moves (a reversal between two with different
// leading ends), then what is done there.
struct WorkStep {
    enum Action { None, Couple, Cut };
    std::vector<WorkLeg> legs;
    Action action = None;
    // Couple: the cars taken, and their track from where they are met on;
    // Cut: the cars left standing, at end B or A.
    int cars = 0;
    bool atB = true;
    std::vector<RouteInterval> over;
    QString what;
};

// A goal from one of the activity's wagon events and how it is done.
struct WorkGoal {
    int event = -1;
    QString name;
    EventPlan::Kind kind = EventPlan::Other;
    int cars = 0;
    bool planned = false;
    // Not planned: why; whether because the plan found was impractical
    // (the user, 2026-10-21: work too complicated is skipped, the run's
    // log warning of it).
    QString why;
    bool impractical = false;
    // Its plan sorts (gathers cars from between others).
    bool sorted = false;
    std::vector<WorkStep> steps;
    double metres = 0.0;
    int reversals = 0;
    int couplings = 0;
    int cuts = 0;
};

// The player's train's work: the goals in the order the activity's event
// chain makes them active, each planned from where the one before left the
// cars; then the way back to the path's end.
struct WorkPlan {
    std::vector<WorkGoal> goals;
    QStringList notes;
    // The path's length from where the train starts; metres from where the
    // work ends to the path's end, -1 no way found.
    double path = 0.0;
    double back = -1.0;
    int backReversals = 0;
    std::vector<WorkLeg> backLegs;
    // Where the work leaves the path (route distance on its first part);
    // where the way back comes to it again, the train then running the rest
    // of it: a part and the route distance there (-1 none: back to the
    // path's end); the path's stops, waits and event places skipped.
    double departure = 0.0;
    int rejoinPart = -1;
    double rejoinAt = 0.0;
    int skipped = 0;
    // Or to the place of the event ending the activity in success (the work
    // makes it active; the rest of the path does not pass it), -1 not.
    int endEvent = -1;
    // The trains as the work leaves them: the player's, the cuts standing.
    YardTrain train;
    std::vector<YardTrain> cuts;
    // Stopped by the user before it was done.
    bool cancelled = false;
};

// For a dialog while the work is planned: the wagon events planned so far
// and in all, reported from the planning's thread; set `cancel` to stop it.
struct WorkProgress {
    std::function<void(int done, int total)> report;
    const std::atomic_bool *cancel = nullptr;
};

// Plans the work of the input's player's train from where it starts, the
// activity's loose consists where the input has them (its AI trains not
// counted).
WorkPlan PlanWork(const SimulationInput &input, const WorkProgress *progress = nullptr);

// The player's work orders planned (PlanWork) and given to its train to run
// (task 07, B1 to B5): its path run up to where the work leaves it, the
// work's moves as route parts (a reversal between two), its cuts as waiting
// points, back to the path where the way back meets it. The input's work
// orders say which goals were planned and why not. It reads the input only
// (it may run on another thread); false when cancelled, the input then as
// it was.
bool ApplyWork(SimulationInput &input, const WorkProgress *progress = nullptr);

}
