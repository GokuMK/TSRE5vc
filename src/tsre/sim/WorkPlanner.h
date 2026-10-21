#pragma once
#include <QString>
#include <QStringList>
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

// A move: the leading end's way, end B or end A leading.
struct WorkLeg {
    bool leadB = true;
    std::vector<RouteInterval> way;
    double length() const;
};

// A step of a plan: its moves (a reversal between two with different
// leading ends), then what is done there.
struct WorkStep {
    enum Action { None, Couple, Cut };
    std::vector<WorkLeg> legs;
    Action action = None;
    // Couple: the cars taken; Cut: the cars left standing, at end B or A.
    int cars = 0;
    bool atB = true;
    QString what;
};

// A goal from one of the activity's wagon events and how it is done.
struct WorkGoal {
    int event = -1;
    QString name;
    EventPlan::Kind kind = EventPlan::Other;
    int cars = 0;
    bool planned = false;
    // Not planned: why.
    QString why;
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
    // The trains as the work leaves them: the player's, the cuts standing.
    YardTrain train;
    std::vector<YardTrain> cuts;
};

// Plans the work of the input's player's train from where it starts, the
// activity's loose consists where the input has them (its AI trains not
// counted).
WorkPlan PlanWork(const SimulationInput &input);

}
