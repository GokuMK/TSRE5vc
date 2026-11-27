/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <explore/ExploreWait.h>
#include <routeEditor/RouteEditorGLWidget.h>
#include <tsre/Game.h>
#include <tsre/characters/Character.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sound/SoundEngine.h>
#include <tsre/tdb/StationPlatforms.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/ui/UiRouteDisplays.h>
#include <tsre/ui/UiScreen.h>

#include <RmlUi/Core.h>
#include <QDate>
#include <QDebug>
#include <QElapsedTimer>
#include <QLocale>
#include <algorithm>
#include <cmath>

namespace Explore {

namespace {

// An RmlUi event handled by a function.
class Listener : public Rml::EventListener {
public:
    explicit Listener(std::function<void(Rml::Event &)> handler) : handler(std::move(handler)) {}
    void ProcessEvent(Rml::Event &event) override { handler(event); }

private:
    std::function<void(Rml::Event &)> handler;
};

bool serverPace = false;

// TSRE_WAIT_LOG: each step of a wait in the log (its goal read: the train's
// next stop, its path part against the stop's, metres, speed).
bool stepLog() {
    static const bool on = qEnvironmentVariableIsSet("TSRE_WAIT_LOG");
    return on;
}

// The rows read again this often while the dialog is open (ms).
constexpr qint64 RowsEvery = 500;
// The sound faded out or back in this long (s); the view dimmed this long
// before the world is no longer drawn (ms).
constexpr float FadeSeconds = 0.3f;
constexpr qint64 DimMs = 350;
// A wait's step at most this long (the goal read again after it).
constexpr double StepMost = 1800.0;

QString clockText(double seconds) {
    const int s = int(std::floor(std::fmod(std::fmod(seconds, 86400.0) + 86400.0, 86400.0)));
    return QString("%1:%2").arg(s / 3600, 2, 10, QLatin1Char('0')).arg(s / 60 % 60, 2, 10, QLatin1Char('0'));
}

// A time span: 0:30, 1:05, 12:00 (hours and minutes); under an hour with
// its seconds when `seconds` (4:32).
QString spanText(double span, bool seconds) {
    const int s = std::max(0, int(std::lround(span)));
    if (seconds && s < 3600)
        return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    const int m = (s + 59) / 60;
    return QString("%1:%2").arg(m / 60).arg(m % 60, 2, 10, QLatin1Char('0'));
}

const TRitem *itemOf(const TDB &tdb, int id) {
    const auto found = tdb.trackItems.find(id);
    return found != tdb.trackItems.end() ? found->second : nullptr;
}

QString stationOf(const TDB &tdb, int id) {
    const TRitem *item = itemOf(tdb, id);
    return item != nullptr ? item->stationName.trimmed() : QString();
}

// A due's row: its name, its booked time and how late it is ("R 46405 to
// Opole Gl  05:52 +2").
QString dueText(const QString &what, const WaitDue &due) {
    QString text = what;
    if (due.booked >= 0)
        text += "  " + clockText(due.booked);
    const int minutes = int(due.late / 60.0);
    if (minutes > 0)
        text += QString(" +%1").arg(minutes);
    return text;
}

}

namespace WaitGoals {

QString trainName(const Sim::TrainPlan &plan, const Names &names) {
    const QString category = plan.fields[TrainFields::Category].trimmed();
    const QString number = plan.fields[TrainFields::Number].trimmed();
    QString name = (category + " " + number).trimmed();
    if (name.isEmpty())
        name = plan.fields[TrainFields::Name].trimmed();
    if (name.isEmpty() && names)
        name = names(plan.service).trimmed();
    if (name.isEmpty())
        name = plan.service;
    return name;
}

namespace {

// A train at a stop as a due: its booked time there, how late it is, its
// name and where it goes.
WaitDue dueOf(int train, int stop, const TDB &tdb, const Sim::SimulationInput &in, const Sim::SimulationFrame *frame,
              double now, const Names &names) {
    const Sim::TrainPlan &plan = in.trains[size_t(train)];
    const Sim::StopPlan &at = plan.stops[size_t(stop)];
    const Sim::TrainState *state =
            frame != nullptr && size_t(train) < frame->trains.size() ? &frame->trains[size_t(train)].state : nullptr;
    WaitDue due;
    due.train = train;
    due.stop = stop;
    // It arrives here (not where it starts, as the boards have it); else
    // it starts here, by its departure.
    const bool arrives = at.arrival >= 0 && (stop > 0 || at.arrival > plan.startTime);
    due.starts = !arrives && stop == 0;
    due.booked = arrives ? at.arrival : due.starts ? at.departure : -1;
    due.late = Ui::RouteDisplays::progress(plan, state, now).late;
    due.station = stationOf(tdb, at.platformItem);
    QString destination = plan.fields[TrainFields::Destination].trimmed();
    if (destination.isEmpty() && !plan.stops.empty())
        destination = stationOf(tdb, plan.stops.back().platformItem);
    const QString name = trainName(plan, names);
    // It ends here: no "to".
    if (destination.isEmpty() || size_t(stop) + 1 == plan.stops.size())
        due.label = name;
    else
        //% "%1 to %2"
        due.label = qtTrId("ui.explore.wait.train.to").arg(name, destination);
    return due;
}

bool gone(const Sim::TrainState *state) {
    return state != nullptr && (state->status == Sim::TrainStatus::Finished || state->joinedTo >= 0);
}

}

double secondsLeft(const WaitDue &due, const Sim::SimulationInput &in, const Sim::SimulationFrame *frame,
                   double now) {
    if (due.train < 0 || size_t(due.train) >= in.trains.size())
        return 0.0;
    const Sim::TrainPlan &plan = in.trains[size_t(due.train)];
    if (due.stop < 0 || size_t(due.stop) >= plan.stops.size())
        return 0.0;
    const Sim::StopPlan &stop = plan.stops[size_t(due.stop)];
    const Sim::TrainState *state =
            frame != nullptr && size_t(due.train) < frame->trains.size() ? &frame->trains[size_t(due.train)].state
                                                                          : nullptr;
    const Ui::RouteDisplays::Progress progress = Ui::RouteDisplays::progress(plan, state, now);
    // Nothing to wait for: gone, there, past it.
    if (gone(state))
        return 0.0;
    const bool onTrack = state != nullptr && state->status != Sim::TrainStatus::Waiting;
    if (onTrack && (progress.at > due.stop || (progress.at == due.stop && progress.standing)))
        return 0.0;
    if (due.starts) {
        // On the track there, or two minutes before it leaves.
        if (onTrack)
            return 0.0;
        return due.booked >= 0 ? due.booked - 120.0 - now : 60.0;
    }
    // Heading for it: by its distance and speed.
    const bool heading = onTrack && progress.at == due.stop;
    if (heading && stop.part == state->part) {
        const double metres = stop.front - state->front;
        if (metres >= 0.0) {
            const double speed = std::fabs(double(state->speed));
            if (metres <= Creep || (speed > 1.0 && 2.0 * metres / speed <= Ahead))
                return 0.0;
            // At its speed, at least 30 km/h (a train held or starting).
            return std::max(StepLeast, metres / std::max(speed, 8.0) - Ahead);
        }
    }
    // Before it heads for it: its booked time and how late it is now; its
    // booked time gone with the train not near, later than it shows yet.
    // Heading for it where its distance is not known (another part of its
    // path): by that time alone.
    if (due.booked < 0)
        return 60.0;
    const double left = double(due.booked) + progress.late - Ahead - now;
    return left > 0.0 || heading ? left : 30.0;
}

double safeStep(const WaitDue &due, const Sim::SimulationInput &in, const Sim::SimulationFrame *frame, double now) {
    if (due.train < 0 || size_t(due.train) >= in.trains.size())
        return StepLeast;
    const Sim::TrainPlan &plan = in.trains[size_t(due.train)];
    if (due.stop < 0 || size_t(due.stop) >= plan.stops.size())
        return StepLeast;
    const Sim::StopPlan &stop = plan.stops[size_t(due.stop)];
    const Sim::TrainState *state =
            frame != nullptr && size_t(due.train) < frame->trains.size() ? &frame->trains[size_t(due.train)].state
                                                                          : nullptr;
    const Ui::RouteDisplays::Progress progress = Ui::RouteDisplays::progress(plan, state, now);
    const bool onTrack = state != nullptr && state->status != Sim::TrainStatus::Waiting;
    const double fastest = std::max(plan.maxSpeed > 0.0f ? double(plan.maxSpeed) : FastestUnknown,
                                    state != nullptr ? std::fabs(double(state->speed)) : 0.0);
    // Within these metres of the stop the wait ends at the latest.
    const double near = std::max(Creep, fastest * Ahead / 2.0);
    double safe = 0.0;
    if (due.starts) {
        // It appears at its start; two minutes before it leaves at the latest.
        safe = std::min(double(plan.startTime), due.booked >= 0 ? due.booked - 120.0 : 1e18) - now;
    } else if (onTrack && progress.at == due.stop) {
        // Heading for it: at its top speed from where it is.
        if (stop.part == state->part)
            safe = (stop.front - state->front - near) / fastest;
    } else if (due.stop > 0) {
        // Not yet. Each bound holds, so the largest: from where it is at
        // its top speed; it leaves the stop before no earlier than booked,
        // then runs to this one at its top speed at most; not on the track
        // before its start.
        if (onTrack && stop.part == state->part)
            safe = (stop.front - state->front - near) / fastest;
        const Sim::StopPlan &before = plan.stops[size_t(due.stop - 1)];
        const double leaves = before.departure >= 0 ? before.departure - now : 0.0;
        const double run = before.part == stop.part ? (stop.front - before.front - near) / fastest : 0.0;
        safe = std::max(safe, std::max(0.0, leaves) + std::max(0.0, run));
        if (!onTrack)
            safe = std::max(safe, double(plan.startTime) - now);
    } else if (due.booked >= 0) {
        // It comes onto the route here: by its booked time, two minutes
        // short.
        safe = due.booked - Ahead - 120.0 - now;
    }
    return std::max(StepLeast, safe);
}

WaitDue nextTrain(const QVector<QPair<int, int>> &lines, const TDB &tdb, const Sim::SimulationInput &in,
                  const Sim::SimulationFrame *frame, double now, const Names &names) {
    WaitDue best;
    double bestTime = 1e18;
    const auto here = [&lines](int item) {
        if (item < 0)
            return false;
        for (const QPair<int, int> &line : lines)
            if (item == line.first || item == line.second)
                return true;
        return false;
    };
    for (size_t t = 0; t < in.trains.size(); ++t) {
        const Sim::TrainPlan &plan = in.trains[t];
        const Sim::TrainState *state = frame != nullptr && t < frame->trains.size() ? &frame->trains[t].state : nullptr;
        if (plan.stops.empty() || gone(state))
            continue;
        for (size_t k = 0; k < plan.stops.size(); ++k) {
            if (!here(plan.stops[k].platformItem))
                continue;
            const WaitDue due = dueOf(int(t), int(k), tdb, in, frame, now, names);
            if (due.booked < 0 && !due.starts)
                continue;
            // Not within Ahead of it, not there, not past it.
            const double left = secondsLeft(due, in, frame, now);
            if (left <= 0.0)
                continue;
            const double when = due.booked >= 0 ? due.booked + due.late : now + left + Ahead;
            if (when < bestTime) {
                bestTime = when;
                best = due;
            }
            // Its first call here still to come.
            break;
        }
    }
    return best;
}

WaitDue nextStop(int train, const TDB &tdb, const Sim::SimulationInput &in, const Sim::SimulationFrame *frame,
                 double now, const Names &names) {
    if (train < 0 || size_t(train) >= in.trains.size())
        return WaitDue();
    const Sim::TrainPlan &plan = in.trains[size_t(train)];
    const Sim::TrainState *state =
            frame != nullptr && size_t(train) < frame->trains.size() ? &frame->trains[size_t(train)].state : nullptr;
    if (state == nullptr || gone(state) || state->status == Sim::TrainStatus::Waiting)
        return WaitDue();
    const Ui::RouteDisplays::Progress progress = Ui::RouteDisplays::progress(plan, state, now);
    int from = progress.standing ? progress.at + 1 : progress.at;
    for (int k = std::max(0, from); k < int(plan.stops.size()); ++k) {
        const WaitDue due = dueOf(train, k, tdb, in, frame, now, names);
        if (secondsLeft(due, in, frame, now) > 0.0)
            return due;
    }
    return WaitDue();
}

}

struct Wait::Data {
    Rml::Context &context;
    RouteEditorGLWidget &view;
    Rml::DataModelHandle handle;
    bool made = false;
    Rml::ElementDocument *document = nullptr;
    std::function<void()> closed;
    Activity *activity = nullptr;
    WaitGoals::Names names;
    std::vector<std::unique_ptr<Listener>> listeners;
    struct Attached {
        Rml::Element *element;
        Rml::EventId event;
        Listener *listener;
        bool capture;
    };
    std::vector<Attached> attached;

    // The route's platform lines, read once for its track database.
    struct Line {
        int start = -1;
        int end = -1;
        std::vector<double> points;
    };
    const TDB *linesOf = nullptr;
    int linesItems = -1;
    std::vector<Line> lines;

    int minutes = 30;
    Goal goal = Goal::None;
    bool fast = false;
    // The run's time a wait for a time ends at; when the wait began.
    double endTime = -1.0;
    double began = -1.0;
    WaitDue due;
    // The step asked for (the run's time), -1 none.
    double stepTarget = -1.0;
    bool skipping = false;
    QElapsedTimer dimming;
    // The rows: standing at a platform (its lines), in a train's car.
    QVector<QPair<int, int>> platformLines;
    bool inCar = false;
    WaitDue trainDue;
    WaitDue stopDue;
    QElapsedTimer rowsRead;
    float volume = 1.0f;
    QElapsedTimer fadeClock;
    QString focusWanted;

    // The model.
    float mHours = 0.0f;
    float mMinutes = 0.0f;
    float mSeconds = 0.0f;
    Rml::String mClock, mDay, mLength, mUntil, mWaitingText, mLeftText, mTrainText, mStopText;
    bool mWaiting = false;
    bool mAtPlatform = false;
    bool mTrainNone = false;
    bool mInTrain = false;
    bool mCanSkip = false;
    bool mSkipping = false;

    Data(Rml::Context &context, RouteEditorGLWidget &view) : context(context), view(view) {}

    void listen(Rml::Element *element, Rml::EventId event, std::function<void(Rml::Event &)> handler,
                bool capture = false) {
        if (element == nullptr)
            return;
        listeners.push_back(std::make_unique<Listener>(std::move(handler)));
        element->AddEventListener(event, listeners.back().get(), capture);
        attached.push_back({element, event, listeners.back().get(), capture});
    }
    void unlisten() {
        for (const Attached &a : attached)
            a.element->RemoveEventListener(a.event, a.listener, a.capture);
        attached.clear();
        listeners.clear();
    }

    static Rml::String rml(const QString &text) { return text.toStdString(); }

    void readLines(TDB &tdb) {
        if (linesOf == &tdb && linesItems == tdb.iTRitems)
            return;
        linesOf = &tdb;
        linesItems = tdb.iTRitems;
        lines.clear();
        for (StationPlatforms::Platform &platform : StationPlatforms::read(tdb)) {
            StationPlatforms::sample(tdb, platform, 2.0);
            lines.push_back({platform.start, platform.end, std::move(platform.points)});
        }
    }

    // The platform lines a character stands at: on foot within
    // PlatformReach of a line's track (flat), not far above or under it
    // (a bridge, an underpass).
    QVector<QPair<int, int>> linesAt(const Characters::Character &walker) {
        QVector<QPair<int, int>> found;
        if (Game::trackDB == nullptr || walker.inCar())
            return found;
        readLines(*Game::trackDB);
        const double x = double(walker.tile()[0]) * 2048.0 + walker.feet()[0];
        const double z = double(walker.tile()[1]) * 2048.0 + walker.feet()[2];
        for (const Line &line : lines) {
            bool near = false;
            for (size_t i = 0; i + 2 < line.points.size() && !near; i += 3) {
                // MSTS's z north; the editor's south.
                const double flat = std::hypot(line.points[i] - x, -line.points[i + 2] - z);
                const double above = double(walker.feet()[1]) - (line.points[i + 1] + 0.325);
                near = flat <= WaitGoals::PlatformReach && above > -2.0 && above < 6.0;
            }
            if (near)
                found.push_back({line.start, line.end});
        }
        return found;
    }

    // The rows from where the character is and the run's time.
    void readRows(const Sim::SimulationInput &in, const Sim::SimulationFrame *frame, double now) {
        rowsRead.start();
        Characters::Character *walker = view.exploreWalker();
        platformLines = walker != nullptr ? linesAt(*walker) : QVector<QPair<int, int>>();
        inCar = walker != nullptr && walker->inCar();
        trainDue = WaitDue();
        stopDue = WaitDue();
        if (Game::trackDB == nullptr)
            return;
        if (!platformLines.isEmpty())
            trainDue = WaitGoals::nextTrain(platformLines, *Game::trackDB, in, frame, now, names);
        if (inCar)
            stopDue = WaitGoals::nextStop(walker->train(), *Game::trackDB, in, frame, now, names);
    }

    double left(const Sim::SimulationInput &in, const Sim::SimulationFrame *frame, double now) const {
        if (goal == Goal::Time)
            return endTime - now;
        return WaitGoals::secondsLeft(due, in, frame, now);
    }

    // The model from the run's time and the wait.
    void show(const Sim::SimulationInput &in, const Sim::SimulationFrame *frame, double now) {
        const double day = std::fmod(std::fmod(now, 86400.0) + 86400.0, 86400.0);
        mSeconds = float(std::fmod(day, 60.0));
        mMinutes = float(std::fmod(std::floor(day / 60.0), 60.0));
        mHours = float(std::fmod(std::floor(day / 3600.0), 12.0));
        mClock = rml(clockText(now));
        const QDate date = in.air.date.isValid() ? in.air.date.addDays(qint64(std::floor(now / 86400.0))) : QDate();
        mDay = rml(date.isValid() ? QLocale().toString(date, "dddd d MMMM") : QString());
        mLength = rml(spanText(minutes * 60.0, false));
        //% "until %1"
        mUntil = rml(qtTrId("ui.explore.wait.until").arg(clockText(now + minutes * 60.0)));
        mWaiting = goal != Goal::None;
        mAtPlatform = !platformLines.isEmpty();
        mTrainNone = !trainDue.valid();
        //% "No train today"
        mTrainText = rml(trainDue.valid() ? dueText(trainDue.label, trainDue) : qtTrId("ui.explore.wait.no.train"));
        mInTrain = inCar && stopDue.valid();
        mStopText = rml(stopDue.valid() ? dueText(stopDue.station, stopDue) : QString());
        mCanSkip = mWaiting && fast && !skipping;
        mSkipping = skipping;
        if (mWaiting) {
            QString text;
            if (goal == Goal::Time)
                //% "Waiting until %1"
                text = qtTrId("ui.explore.wait.waiting.until").arg(clockText(endTime));
            else
                //% "Waiting for %1"
                text = qtTrId("ui.explore.wait.waiting.for").arg(goal == Goal::Train ? due.label : due.station);
            mWaitingText = rml(text);
            //% "%1 left"
            mLeftText = rml(qtTrId("ui.explore.wait.left").arg(spanText(std::max(0.0, left(in, frame, now)), true)));
        }
        if (made)
            handle.DirtyAllVariables();
    }

    void focusLater(const QString &id) { focusWanted = id; }
    void takeFocus() {
        if (focusWanted.isEmpty() || document == nullptr || !document->IsVisible())
            return;
        Rml::Element *element = document->GetElementById(focusWanted.toStdString());
        if (element != nullptr && element->IsVisible(true)) {
            element->Focus(true);
            focusWanted.clear();
        }
    }

    // The sound towards silence while waiting fast, back after.
    void fadeSound() {
        const float seconds = fadeClock.isValid() ? float(fadeClock.restart()) / 1000.0f : 0.0f;
        if (!fadeClock.isValid())
            fadeClock.start();
        const float target = goal != Goal::None && fast ? 0.0f : 1.0f;
        if (volume == target)
            return;
        const float step = std::min(1.0f, seconds / FadeSeconds);
        volume = target > volume ? std::min(target, volume + step) : std::max(target, volume - step);
        SoundEngine::get().setVolume(volume);
    }
};

void Wait::setServerPace(bool on) {
    serverPace = on;
}

Wait::Wait(Rml::Context &context, RouteEditorGLWidget &view) : d(new Data(context, view)) {
    Rml::DataModelConstructor constructor = context.CreateDataModel("wait");
    if (!constructor)
        return;
    constructor.Bind("hours", &d->mHours);
    constructor.Bind("minutes", &d->mMinutes);
    constructor.Bind("seconds", &d->mSeconds);
    constructor.Bind("clock", &d->mClock);
    constructor.Bind("day", &d->mDay);
    constructor.Bind("length", &d->mLength);
    constructor.Bind("until", &d->mUntil);
    constructor.Bind("waiting", &d->mWaiting);
    constructor.Bind("waitingText", &d->mWaitingText);
    constructor.Bind("leftText", &d->mLeftText);
    constructor.Bind("atPlatform", &d->mAtPlatform);
    constructor.Bind("trainNone", &d->mTrainNone);
    constructor.Bind("trainText", &d->mTrainText);
    constructor.Bind("inTrain", &d->mInTrain);
    constructor.Bind("stopText", &d->mStopText);
    constructor.Bind("canSkip", &d->mCanSkip);
    constructor.Bind("skipping", &d->mSkipping);
    const auto change = [this](int by) {
        if (d->goal == Goal::None)
            setMinutes(d->minutes + by);
    };
    constructor.BindEventCallback("less", [change](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        change(-MinutesLeast);
    });
    constructor.BindEventCallback("more", [change](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        change(MinutesLeast);
    });
    constructor.BindEventCallback("waitTime", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        start(Goal::Time);
    });
    constructor.BindEventCallback("waitTrain", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        start(Goal::Train);
    });
    constructor.BindEventCallback("waitStop", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        start(Goal::Stop);
    });
    constructor.BindEventCallback("cancel", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        close();
    });
    constructor.BindEventCallback("stopWait", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        stop();
    });
    constructor.BindEventCallback("skip", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        skip();
    });
    d->handle = constructor.GetModelHandle();
    d->made = true;
}

Wait::~Wait() {
    reset();
    d->unlisten();
    if (d->made)
        d->context.RemoveDataModel("wait");
}

bool Wait::usable() const {
    return d->made;
}

void Wait::setDocument(Rml::ElementDocument *document, std::function<void()> closed) {
    d->unlisten();
    d->document = document;
    d->closed = std::move(closed);
    if (document == nullptr)
        return;
    // Esc and T: Stop while waiting, else Cancel; Left and Right the time
    // to wait (Shift: an hour). In the capture phase, before RmlUi's own
    // (the screen would hide the document on an Esc it did not use).
    d->listen(document, Rml::EventId::Keydown, [this](Rml::Event &event) {
        const int key = event.GetParameter<int>("key_identifier", 0);
        if (key == Rml::Input::KI_ESCAPE || key == Rml::Input::KI_T) {
            event.StopPropagation();
            if (d->goal != Goal::None)
                stop();
            else
                close();
            return;
        }
        if ((key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT) && d->goal == Goal::None) {
            event.StopPropagation();
            const int by = event.GetParameter<int>("shift_key", 0) != 0 ? 60 : MinutesLeast;
            setMinutes(d->minutes + (key == Rml::Input::KI_RIGHT ? by : -by));
        }
    }, true);
}

void Wait::setRun(Activity *activity, WaitGoals::Names names) {
    d->activity = activity;
    d->names = std::move(names);
}

bool Wait::open(Ui::Screen &screen) {
    if (d->document == nullptr || d->activity == nullptr)
        return false;
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->frame();
    const std::shared_ptr<const Sim::SimulationInput> in = simulation->input();
    if (frame == nullptr || in == nullptr)
        return false;
    d->readRows(*in, frame.get(), frame->time);
    d->show(*in, frame.get(), frame->time);
    screen.showMenu(d->document);
    d->focusLater("wait-go");
    d->takeFocus();
    qInfo().noquote() << QString("Explore: the wait at %1: %2, %3")
                                 .arg(clockText(frame->time))
                                 .arg(d->platformLines.isEmpty() ? "not at a platform"
                                                                 : QString("at %1 platform lines, next train %2")
                                                                           .arg(d->platformLines.size())
                                                                           .arg(d->trainDue.valid() ? d->trainDue.label : "none"))
                                 .arg(d->inCar ? QString("in a car, next stop %1").arg(d->stopDue.valid() ? d->stopDue.station : "none")
                                               : "on foot");
    return true;
}

void Wait::close() {
    if (d->goal != Goal::None)
        stop();
    if (d->document != nullptr && d->document->IsVisible()) {
        d->document->Hide();
        if (d->closed)
            d->closed();
    }
}

bool Wait::shown() const {
    return d->document != nullptr && d->document->IsVisible();
}

void Wait::frame() {
    d->fadeSound();
    if (!shown() && d->goal == Goal::None)
        return;
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->frame();
    const std::shared_ptr<const Sim::SimulationInput> in = simulation->input();
    if (frame == nullptr || in == nullptr) {
        reset();
        return;
    }
    const double now = frame->time;
    if (d->goal == Goal::None) {
        if (!d->rowsRead.isValid() || d->rowsRead.elapsed() >= RowsEvery)
            d->readRows(*in, frame.get(), now);
    } else if (!d->fast) {
        // A server's run: at its pace.
        if (d->left(*in, frame.get(), now) <= 0.0) {
            stop();
            return;
        }
    } else {
        if (d->skipping && !d->view.exploreWorldHidden() && d->dimming.elapsed() >= DimMs)
            d->view.setExploreWorldHidden(true);
        // A step running: until the run stands where it was asked to.
        if (d->stepTarget >= 0.0) {
            if (simulation->runs() || now < d->stepTarget - 0.5)
                return d->show(*in, frame.get(), now);
            d->stepTarget = -1.0;
        }
        const double left = d->left(*in, frame.get(), now);
        if (left <= 0.5 || now - d->began > MinutesMost * 60.0) {
            stop();
            return;
        }
        // A time in one step; a train in steps it cannot pass its end in,
        // the goal read again after each.
        const double step = d->goal == Goal::Time
                ? left
                : std::min(WaitGoals::safeStep(d->due, *in, frame.get(), now), StepMost);
        d->stepTarget = now + step;
        if (stepLog()) {
            QString train;
            if (d->due.valid() && size_t(d->due.train) < frame->trains.size()
                    && size_t(d->due.train) < in->trains.size()) {
                const Sim::TrainState &state = frame->trains[size_t(d->due.train)].state;
                const Sim::TrainPlan &plan = in->trains[size_t(d->due.train)];
                const Sim::StopPlan *stop = size_t(d->due.stop) < plan.stops.size() ? &plan.stops[size_t(d->due.stop)] : nullptr;
                train = QString(", train %1 status %2 next stop %3 (waited for %4), part %5 (the stop's %6), front %7 "
                                "(the stop's %8), %9 m/s")
                                .arg(d->due.train).arg(int(state.status)).arg(state.nextStop).arg(d->due.stop)
                                .arg(state.part).arg(stop != nullptr ? stop->part : -1)
                                .arg(state.front, 0, 'f', 0).arg(stop != nullptr ? stop->front : -1.0, 0, 'f', 0)
                                .arg(double(state.speed), 0, 'f', 1);
            }
            qInfo().noquote() << QString("Explore: wait step at %1 s: %2 s left, to %3 s%4")
                                         .arg(now, 0, 'f', 1).arg(left, 0, 'f', 1).arg(d->stepTarget, 0, 'f', 1).arg(train);
        }
        QString error;
        if (!simulation->runToSeconds(d->stepTarget, &error)) {
            qWarning().noquote() << "Explore: the wait could not run on:" << error;
            stop();
            return;
        }
    }
    d->show(*in, frame.get(), now);
    d->takeFocus();
}

bool Wait::start(Goal goal) {
    if (d->goal != Goal::None || goal == Goal::None || d->activity == nullptr)
        return false;
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->frame();
    const std::shared_ptr<const Sim::SimulationInput> in = simulation->input();
    if (frame == nullptr || in == nullptr)
        return false;
    const double now = frame->time;
    d->readRows(*in, frame.get(), now);
    d->due = WaitDue();
    d->endTime = -1.0;
    if (goal == Goal::Time) {
        d->endTime = now + d->minutes * 60.0;
    } else {
        d->due = goal == Goal::Train ? d->trainDue : d->stopDue;
        if (!d->due.valid() || (goal == Goal::Train && d->platformLines.isEmpty()) || (goal == Goal::Stop && !d->inCar))
            return false;
    }
    d->goal = goal;
    d->fast = simulation->controlsRun() && !serverPace;
    d->began = now;
    d->stepTarget = -1.0;
    d->skipping = false;
    if (d->fast)
        simulation->setWorldFollowsFastRuns(true);
    qInfo().noquote() << QString("Explore: waiting at %1 %2%3")
                                 .arg(clockText(now))
                                 .arg(goal == Goal::Time ? QString("until %1").arg(clockText(d->endTime))
                                      : QString("for %1 at %2 (booked %3, %4 s late)")
                                                .arg(d->due.label, d->due.station)
                                                .arg(d->due.booked >= 0 ? clockText(d->due.booked) : QString("-"))
                                                .arg(int(d->due.late)))
                                 .arg(d->fast ? "" : " at the run's pace");
    d->show(*in, frame.get(), now);
    d->focusLater("wait-halt");
    return true;
}

void Wait::stop() {
    if (d->goal == Goal::None)
        return;
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->frame();
    const bool wasFast = d->fast;
    d->goal = Goal::None;
    d->stepTarget = -1.0;
    d->skipping = false;
    d->view.setExploreWorldHidden(false);
    if (wasFast) {
        simulation->setWorldFollowsFastRuns(false);
        QString error;
        if (simulation->exists() && !simulation->play(d->activity, &error))
            qWarning().noquote() << "Explore: the run did not play on after the wait:" << error;
    }
    qInfo().noquote() << QString("Explore: the wait ended at %1 (%2 s of the run)")
                                 .arg(frame != nullptr ? clockText(frame->time) : QString("-"))
                                 .arg(frame != nullptr ? frame->time : -1.0, 0, 'f', 1);
    close();
}

void Wait::skip() {
    if (d->goal == Goal::None || !d->fast || d->skipping)
        return;
    d->skipping = true;
    d->dimming.start();
}

void Wait::reset() {
    if (d->goal != Goal::None) {
        d->goal = Goal::None;
        d->stepTarget = -1.0;
        if (d->fast)
            Sim::SimulationController::instance()->setWorldFollowsFastRuns(false);
    }
    d->skipping = false;
    d->view.setExploreWorldHidden(false);
    if (d->volume != 1.0f) {
        d->volume = 1.0f;
        SoundEngine::get().setVolume(1.0f);
    }
    if (d->document != nullptr && d->document->IsVisible())
        d->document->Hide();
}

bool Wait::waiting() const {
    return d->goal != Goal::None;
}

bool Wait::skipping() const {
    return d->skipping;
}

bool Wait::fast() const {
    return d->goal != Goal::None && d->fast;
}

Wait::Goal Wait::goal() const {
    return d->goal;
}

int Wait::minutes() const {
    return d->minutes;
}

void Wait::setMinutes(int minutes) {
    d->minutes = std::clamp(minutes, int(MinutesLeast), int(MinutesMost));
}

const WaitDue &Wait::due() const {
    return d->due;
}

double Wait::end() const {
    return d->endTime;
}

bool Wait::atPlatform() const {
    return !d->platformLines.isEmpty();
}

bool Wait::inTrain() const {
    return d->inCar && d->stopDue.valid();
}

QString Wait::text(const QString &id) const {
    if (d->document == nullptr)
        return QString();
    Rml::Element *element = d->document->GetElementById(id.toStdString());
    return element != nullptr ? QString::fromStdString(element->GetInnerRML()).trimmed() : QString();
}

bool Wait::click(const QString &id) {
    if (d->document == nullptr)
        return false;
    Rml::Element *element = d->document->GetElementById(id.toStdString());
    if (element == nullptr)
        return false;
    element->Focus();
    element->Click();
    return true;
}

}
