#pragma once
#include <QString>
#include <tsre/sim/SimulationCore.h>

namespace Sim {

// Texts about a simulation's trains for the editor, from its input: what a
// train is doing ("running 72 km/h", "at Settle until 08:31") at `time`; a
// train as the Timetable window names it ("Local 09:30"); a section's
// holder (a train, a standing consist); a signal as the editor names it
// (its TDB item); why a train waits ("at signal 1234: the track beyond is
// held by ...").
QString StateText(const SimulationInput &in, const TrainState &state, double time, int train);
QString TrainName(const SimulationInput &in, int train);
// The same name from a service: "service (player)", or the service and its
// start time (seconds after midnight), before a simulation has the train.
QString TrainName(const QString &service, bool player, int time);
QString HolderName(const SimulationInput &in, int holder);
QString SignalName(const SimulationInput &in, int signal);
QString WaitText(const SimulationInput &in, WaitFor waitFor, int signal, int holder);
// Where the cars are when a run ends (task 07: they move as trains couple
// and cut): each train's vehicles by train id, and the standing trains'
// not gone: a loose consist of the activity, or cars a train cut off
// (named after that train).
struct CarsNow {
    std::vector<std::vector<Vehicle>> trains;
    struct Standing {
        QString name;
        bool loose = true;
        std::vector<Vehicle> vehicles;
    };
    std::vector<Standing> standing;
};
CarsNow CarsOf(const SimulationCore &core);
CarsNow CarsOf(const struct SimulationFrame &frame);
// A work order the autopilot does not do (task 07, part B): the input's
// event `event` (named `name`), why.
QString WorkSkippedText(const SimulationInput &in, int event, const QString &name);
// Why the input's event `event` did not fire in a run (task 06, section
// 7), from its state where the run ended and the activity's end; the
// cars where they are then (else where the input has them).
QString WhyNotFired(const SimulationInput &in, int event, const EventState &state, ActivityEnd end, double endedAt,
                    const CarsNow *cars = nullptr);

}
