/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef EXPLOREWAIT_H
#define EXPLOREWAIT_H

#include <QPair>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

class Activity;
class RouteEditorGLWidget;
class TDB;

namespace Rml {
class Context;
class ElementDocument;
}

namespace Sim {
struct SimulationInput;
struct SimulationFrame;
struct TrainPlan;
}

namespace Ui {
class Screen;
}

namespace Explore {

// A train's stop a wait waits for (task explore 06).
struct WaitDue {
    // The simulation's train and its stop (in its plan), -1 none.
    int train = -1;
    int stop = -1;
    // Its booked arrival there (seconds after the run's first midnight);
    // its departure for a train starting there; -1 none.
    int booked = -1;
    bool starts = false;
    // How late it is now (s), as the platform screens count it.
    double late = 0.0;
    QString station;
    // The train as the platform screens name it, where it goes ("R 46405
    // to Opole Gl").
    QString label;
    bool valid() const { return train >= 0; }
};

// The goals' arithmetic (the checks use it too). Times are the run's
// seconds (after its first day's midnight).
namespace WaitGoals {
// How long before the train arrives a wait ends (s): a train heading for its
// stop brakes into it, so its last `d` metres at `v` take about 2d/v (the
// wait ends when that is Ahead; live 2026-11-27: at d/v it ended 50 s
// early); within Creep of the stop at any speed. How far from a platform
// line's track a player stands at it (m, flat).
constexpr double Ahead = 30.0;
constexpr double Creep = 30.0;
constexpr double PlatformReach = 10.0;
// How a train is named: its timetable's comment by its name (empty none).
using Names = std::function<QString(const QString &service)>;
// The next train stopping at one of the platform lines (their two track
// items) whose arrival there is more than Ahead away; a train starting
// there, by its departure. The earliest by its booked time and how late it
// is now. None: !valid().
WaitDue nextTrain(const QVector<QPair<int, int>> &lines, const TDB &tdb, const Sim::SimulationInput &in,
                  const Sim::SimulationFrame *frame, double now, const Names &names);
// The next stop of `train` (where it stands: the one after) more than
// Ahead away.
WaitDue nextStop(int train, const TDB &tdb, const Sim::SimulationInput &in, const Sim::SimulationFrame *frame,
                 double now, const Names &names);
// The run's seconds before a wait for `due` ends: 0 when it is Ahead from
// its arrival by its distance and speed (2d/v), within Creep of the stop,
// there, past it or gone; its booked time plus how late it is, less Ahead,
// before it heads for the stop; a train starting there, when it is on the
// track or two minutes before it leaves. A guess until the train comes:
// read again after each step.
double secondsLeft(const WaitDue &due, const Sim::SimulationInput &in, const Sim::SimulationFrame *frame,
                   double now);
// How far the run may go in one step without passing the wait's end
// however the train runs (it may run faster than now, and arrive early, but
// not leave a stop early): heading for the stop, until at its top speed it
// could be Ahead from it (2d/v); before that, until it
// may leave the stop before (its booked departure), plus that run at its
// top speed; a train starting there, until its start. At least
// StepLeast.
double safeStep(const WaitDue &due, const Sim::SimulationInput &in, const Sim::SimulationFrame *frame, double now);
constexpr double StepLeast = 5.0;
// A train's top speed when its plan has none (m/s, 160 km/h).
constexpr double FastestUnknown = 44.5;
// A train's name as the platform screens show it: its category and
// number, else its name field, else its timetable's comment, else its
// service's name.
QString trainName(const Sim::TrainPlan &plan, const Names &names);
}

// Explore mode's wait (task explore 06): T while walking (or the game
// menu's Wait) opens appdata/<version>/ui/explore-wait.rml, its data model
// "wait": an analog clock of the run's time, the day, a time to wait (5
// minutes to 12 hours), Wait; Next train standing at a platform, Next stop
// in a train's car; Cancel. Waiting: the run as fast as it can in steps
// (each ends where it was asked to, the goal read again), the world's time
// following it (a time-lapse) and the sound faded out; Stop, Skip (the
// world not drawn). A run the view does not control (a server's) cannot go
// faster: the dialog stays open counting down at its pace.
class Wait {
public:
    enum class Goal { None, Time, Train, Stop };

    // Makes the data model "wait" in `context` (before its document loads).
    Wait(Rml::Context &context, RouteEditorGLWidget &view);
    ~Wait();
    bool usable() const;
    // The dialog's document; `closed` called when it closes (the run takes
    // the pointer again).
    void setDocument(Rml::ElementDocument *document, std::function<void()> closed);
    // The run's activity (played on after a wait) and how its trains are
    // named.
    void setRun(Activity *activity, WaitGoals::Names names);
    // Shown as a menu on `screen`, Wait focused; false when it cannot be.
    bool open(Ui::Screen &screen);
    void close();
    bool shown() const;
    // Each frame while the run is on: the clock, the rows, the wait's steps,
    // the sound's fade.
    void frame();
    // Waiting for a goal (the dialog's buttons); false when there is none.
    bool start(Goal goal);
    // The wait ended where the run is; the run plays on.
    void stop();
    // The rest of the wait with the world not drawn.
    void skip();
    // Everything back as before a wait (the run left).
    void reset();
    bool waiting() const;
    bool skipping() const;
    // Waiting faster than the run's pace (a run this view controls).
    bool fast() const;
    Goal goal() const;
    // The time to wait (minutes).
    int minutes() const;
    void setMinutes(int minutes);
    // What a wait for a train waits for; the run's time a wait for a time
    // ends at (-1 none).
    const WaitDue &due() const;
    double end() const;
    // The checks: the rows shown (at a platform, in a train), an element's
    // text, a click.
    bool atPlatform() const;
    bool inTrain() const;
    QString text(const QString &id) const;
    bool click(const QString &id);
    // The checks: waits as on a server's run (at its pace).
    static void setServerPace(bool on);

    static constexpr int MinutesLeast = 5;
    static constexpr int MinutesMost = 12 * 60;

private:
    struct Data;
    std::unique_ptr<Data> d;
};

}

#endif // EXPLOREWAIT_H
