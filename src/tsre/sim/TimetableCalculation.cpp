#include <tsre/sim/TimetableCalculation.h>

#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/sim/SimulationTexts.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/world/Route.h>
#include <tsre/world/Trk.h>

namespace Sim {

namespace {

// What the session's runs wrote into a timetable row: the last run's times;
// every departure a run calculated there (one still in the row is the
// calculation's, not the user's); and when the user's departure held the
// train but it left later (late, or the departure off the step), the
// user's (`floor`) and the one written over it (`floorFor`), so the next
// run keeps the user's while the row still has that one.
struct Row {
    int arrival = -1;
    int departure = -1;
    QSet<int> departures;
    int floor = -1;
    int floorFor = -1;
};

struct Memory {
    QHash<QString, QVector<Row>> rows;
    TimetableRun last;
    bool hasLast = false;
};

QHash<QString, Memory> &memories() {
    static QHash<QString, Memory> runs;
    return runs;
}

QString activityKey(const Activity *activity) {
    return activity->pathid.isEmpty() ? activity->path + "/" + activity->name : activity->pathid;
}

QString trainKey(bool player, const QString &service, int time) {
    return player ? QString("player") : service.toLower() + "@" + QString::number(time);
}

// The activity's timetable of an input train, or none.
ActivityTimetable *timetableOf(const QVector<ActivityTrains::Train> &trains, const TrainPlan &plan) {
    for (const ActivityTrains::Train &train : trains)
        if (plan.player ? train.kind == ActivityTrains::Player
                        : train.kind == ActivityTrains::AI && train.service == plan.service && train.time == plan.serviceTime)
            return train.timetable;
    return nullptr;
}

int rowsOf(const ActivityTimetable *timetable) {
    return timetable == nullptr ? 0 : std::min({timetable->platformStartID.size(), timetable->arrivalTime.size(),
                                                timetable->departTime.size()});
}

QString platformName(int platformId) {
    if (Game::trackDB != nullptr) {
        const auto found = Game::trackDB->trackItems.find(platformId);
        if (found != Game::trackDB->trackItems.end() && found->second != nullptr && !found->second->stationName.isEmpty())
            return found->second->stationName;
    }
    return QString::number(platformId);
}

int wrapped(int seconds) {
    return (seconds % 86400 + 86400) % 86400;
}

// A change between two times of day, the shorter way round midnight.
int difference(int now, int before) {
    int d = wrapped(now - before);
    return d > 43200 ? d - 86400 : d;
}

}

int TimetableTrain::changed() const {
    int count = 0;
    for (const TimetableStop &stop : stops) {
        if (!written || !stop.reached || stop.arrival < 0)
            continue;
        if (stop.arrivalBefore < 0 || stop.arrival != stop.arrivalBefore
                || (stop.departure >= 0 && stop.departure != stop.departureBefore))
            ++count;
    }
    return count;
}

int TimetableTrain::largestChange() const {
    int largest = 0;
    for (const TimetableStop &stop : stops) {
        if (!written || !stop.reached)
            continue;
        if (stop.arrival >= 0 && stop.arrivalBefore >= 0)
            largest = std::max(largest, std::abs(difference(stop.arrival, stop.arrivalBefore)));
        if (stop.departure >= 0 && stop.departureBefore >= 0)
            largest = std::max(largest, std::abs(difference(stop.departure, stop.departureBefore)));
    }
    return largest;
}

std::unique_ptr<TimetableCalculation> TimetableCalculation::Prepare(Activity *activity, const TimetableOptions &options,
                                                                   QString *error) {
    if (activity == nullptr || Game::trackDB == nullptr) {
        if (error != nullptr)
            *error =
                //% "Choose an activity first."
                qtTrId("simulation.timetable.error.no.activity");
        return nullptr;
    }
    Route *route = Game::currentRoute;
    const float limit = route != nullptr && route->trk != nullptr ? route->trk->speedLimit : 0.0f;
    const float restricted = route != nullptr && route->trk != nullptr ? route->trk->tempRestrictedSpeed : -1.0f;
    const std::shared_ptr<const SimulationInput> made = MakeInput(activity, Game::trackDB, limit, nullptr, nullptr,
                                                                  restricted);
    if (made->trains.empty()) {
        if (error != nullptr) {
            *error =
                //% "The activity has no train the simulation can run."
                qtTrId("simulation.timetable.error.no.trains");
            if (!made->warnings.isEmpty())
                *error += "\n" + made->warnings.join("\n");
        }
        return nullptr;
    }
    std::unique_ptr<TimetableCalculation> calculation(new TimetableCalculation());
    TimetableRun &run = calculation->outcome;
    run.options = options;
    run.options.step = std::max(0, options.step);
    run.warnings = made->warnings;
    // The AI trains' times are written only into a pattern the activity has
    // to itself.
    run.sharedWith = ActivityTrains::OtherUsers(activity);
    run.options.playerOnly = options.playerOnly || !run.sharedWith.isEmpty() || ActivityTrains::Pattern(activity) == nullptr;

    auto input = std::make_shared<SimulationInput>(*made);
    input->departureStep = run.options.step;
    const Memory &memory = memories()[activityKey(activity)];
    const QVector<ActivityTrains::Train> trains = ActivityTrains::List(activity);
    calculation->trainOf.assign(input->trains.size(), -1);
    calculation->keptStop.resize(input->trains.size());
    for (size_t k = 0; k < input->trains.size(); ++k) {
        TrainPlan &plan = input->trains[k];
        const ActivityTimetable *timetable = timetableOf(trains, plan);
        TimetableTrain train;
        train.player = plan.player;
        train.service = plan.service;
        train.time = plan.player ? plan.startTime : plan.serviceTime;
        train.name = TrainName(*input, int(k));
        train.written = timetable != nullptr && (plan.player || !run.options.playerOnly);
        const int rows = rowsOf(timetable);
        train.stops.resize(size_t(rows));
        for (int r = 0; r < rows; ++r)
            train.stops[size_t(r)].platform = platformName(timetable->platformStartID[r]);
        const auto remembered = memory.rows.constFind(trainKey(plan.player, plan.service, train.time));
        calculation->keptStop[k].assign(plan.stops.size(), false);
        for (size_t i = 0; i < plan.stops.size(); ++i) {
            StopPlan &stop = plan.stops[i];
            if (stop.entry < 0 || stop.entry >= rows)
                continue;
            TimetableStop &row = train.stops[size_t(stop.entry)];
            row.simulated = true;
            // Trains not written keep their booked departures; so do
            // untimed stops (none).
            if (!train.written || stop.departure < 0)
                continue;
            // A departure a run calculated is calculated again; another is
            // the user's (or the file's) and holds the train, as does the
            // user's a run wrote a later one over.
            const Row *had = remembered != memory.rows.constEnd() && stop.entry < remembered->size()
                    ? &(*remembered)[stop.entry] : nullptr;
            if (!run.options.fromScratch && had != nullptr && had->floor >= 0 && stop.departure == had->floorFor)
                stop.departure = had->floor;
            else if (run.options.fromScratch || (had != nullptr && had->departures.contains(stop.departure)))
                stop.departure = -1;
            if (stop.departure >= 0) {
                row.kept = true;
                calculation->keptStop[k][i] = true;
            }
        }
        calculation->trainOf[k] = int(run.trains.size());
        run.trains.push_back(train);
    }
    calculation->in = input;
    return calculation;
}

void TimetableCalculation::run(const std::atomic<bool> *cancel) {
    QElapsedTimer timer;
    timer.start();
    const size_t count = in->trains.size();
    arrived.assign(count, {});
    departed.assign(count, {});
    for (size_t k = 0; k < count; ++k) {
        arrived[k].assign(in->trains[k].stops.size(), -1.0);
        departed[k].assign(in->trains[k].stops.size(), -1.0);
    }
    outcome.from = in->startTime;
    for (const TrainPlan &plan : in->trains)
        outcome.from = std::min(outcome.from, double(plan.startTime));
    // The pre-run happens as the core is made: its events come first.
    SimulationCore core(in);
    const auto take = [&] {
        for (const TrainEvent &event : core.takeEvents()) {
            if (event.train < 0 || size_t(event.train) >= count || event.index < 0
                    || size_t(event.index) >= arrived[size_t(event.train)].size())
                continue;
            if (event.kind == TrainEvent::Arrived && arrived[size_t(event.train)][size_t(event.index)] < 0.0)
                arrived[size_t(event.train)][size_t(event.index)] = event.time;
            else if (event.kind == TrainEvent::Departed && departed[size_t(event.train)][size_t(event.index)] < 0.0)
                departed[size_t(event.train)][size_t(event.index)] = event.time;
        }
    };
    take();
    const double end = in->startTime + 24.0 * 3600.0;
    while (!core.settled() && core.time() < end) {
        if (cancel != nullptr && cancel->load()) {
            outcome.cancelled = true;
            break;
        }
        core.step(10.0);
        take();
    }
    last = core.trains();
    endTime = core.time();
    outcome.to = endTime;
    outcome.ms = timer.elapsed();
}

const TimetableRun &TimetableCalculation::write(Activity *activity) {
    if (outcome.cancelled || activity == nullptr || last.size() != in->trains.size())
        return outcome;
    Memory &memory = memories()[activityKey(activity)];
    const QVector<ActivityTrains::Train> trains = ActivityTrains::List(activity);
    const int step = outcome.options.step;
    // Arrivals down, departures up to the step (they are on it already: the
    // trains waited for them).
    const auto down = [step](double t) {
        return wrapped(step > 0 ? int(std::floor(t / step + 1e-9)) * step : int(std::floor(t + 1e-6)));
    };
    const auto up = [step](double t) {
        return wrapped(step > 0 ? int(std::ceil(t / step - 1e-9)) * step : int(std::ceil(t - 1e-6)));
    };
    for (size_t k = 0; k < in->trains.size(); ++k) {
        const TrainPlan &plan = in->trains[k];
        TimetableTrain &train = outcome.trains[size_t(trainOf[k])];
        const TrainState &state = last[k];
        train.finished = state.status == TrainStatus::Finished || state.stoppedAt == StopKind::End;
        if (!train.finished)
            train.state = StateText(*in, state, endTime, int(k));
        ActivityTimetable *timetable = train.written ? timetableOf(trains, plan) : nullptr;
        const int rows = rowsOf(timetable);
        QVector<Row> *remembered = nullptr;
        if (timetable != nullptr) {
            remembered = &memory.rows[trainKey(plan.player, plan.service, train.time)];
            if (remembered->size() < rows)
                remembered->resize(rows);
        }
        for (size_t i = 0; i < plan.stops.size(); ++i) {
            const int r = plan.stops[i].entry;
            if (r < 0 || size_t(r) >= train.stops.size())
                continue;
            TimetableStop &stop = train.stops[size_t(r)];
            stop.reached = arrived[k][i] >= 0.0;
            if (!stop.reached)
                continue;
            stop.arrival = down(arrived[k][i]);
            stop.departure = departed[k][i] >= 0.0 ? up(departed[k][i]) : -1;
            if (timetable == nullptr || r >= rows)
                continue;
            Row &row = (*remembered)[r];
            const bool timed = timetable->arrivalTime[r] != 0 || timetable->departTime[r] != 0;
            stop.arrivalBefore = row.arrival >= 0 ? row.arrival : timed ? timetable->arrivalTime[r] : -1;
            stop.departureBefore = row.departure >= 0 ? row.departure : timed ? timetable->departTime[r] : -1;
            timetable->setArrival(r, stop.arrival);
            row.arrival = stop.arrival;
            if (stop.departure >= 0) {
                timetable->setDepart(r, stop.departure);
                row.departure = stop.departure;
                const int kept = keptStop[k][i] ? plan.stops[i].departure : -1;
                if (kept < 0)
                    row.departures.insert(stop.departure);
                row.floor = kept >= 0 && kept != stop.departure ? kept : -1;
                row.floorFor = row.floor >= 0 ? stop.departure : -1;
            }
        }
    }
    memory.last = outcome;
    memory.hasLast = true;
    return outcome;
}

const TimetableRun *TimetableCalculation::Last(const Activity *activity) {
    if (activity == nullptr)
        return nullptr;
    const auto found = memories().constFind(activityKey(activity));
    return found != memories().constEnd() && found->hasLast ? &found->last : nullptr;
}

const TimetableTrain *TimetableCalculation::LastTrain(const Activity *activity, bool player, const QString &service,
                                                     int time) {
    const TimetableRun *run = Last(activity);
    if (run == nullptr)
        return nullptr;
    for (const TimetableTrain &train : run->trains)
        if (player ? train.player
                   : !train.player && train.service.compare(service, Qt::CaseInsensitive) == 0 && train.time == time)
            return &train;
    return nullptr;
}

void TimetableCalculation::ForgetAll() {
    memories().clear();
}

}
