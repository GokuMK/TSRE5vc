#include <tsre/tests/SimulationTestSuite.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QDate>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>
#include <tsre/Game.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/SimulationSession.h>
#include <settings/SettingsAccess.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/PathTrack.h>
#include <tsre/world/Route.h>
#include <tsre/world/TerrainLib.h>

namespace {

using FramePointer = std::shared_ptr<const Sim::SimulationFrame>;

// Numbers as they are, to the last bit.
QString n(double value) {
    return QString::number(value, 'g', 17);
}
QString n(float value) {
    return QString::number(double(value), 'g', 9);
}

QString valueText(const Sim::MessageValue &value) {
    if (std::holds_alternative<QString>(value))
        return "s:" + std::get<QString>(value);
    if (std::holds_alternative<bool>(value))
        return std::get<bool>(value) ? "b:1" : "b:0";
    return "d:" + n(std::get<double>(value));
}

// A vehicle's file under the game root (the test route's is a temporary
// folder).
QString fileText(const QString &path) {
    const QString root = QDir::cleanPath(Game::root);
    const QString file = QDir::cleanPath(path);
    return file.startsWith(root + "/", Qt::CaseInsensitive) ? file.mid(root.size() + 1) : file;
}

void writePose(QTextStream &out, const Sim::TrackPose &p) {
    out << ' ' << p.tileX << ',' << p.tileZ << ',' << n(p.x) << ',' << n(p.y) << ',' << n(p.z) << ',' << n(p.yaw)
        << ',' << n(p.pitch) << ',' << n(p.roll) << ',' << n(p.cant) << ',' << p.node << ',' << n(p.distance) << ','
        << int(p.forward);
}

void writeVehicle(QTextStream &out, const Sim::Vehicle &v) {
    out << "  vehicle " << fileText(v.definition->path) << " flip " << int(v.flip) << " id " << v.id << " number '"
        << v.number << "' tags '" << v.tags << "' lines " << v.lines << " controlOff " << int(v.controlOff) << '\n';
}

void writeValues(QTextStream &out, const char *what, const std::vector<QHash<QString, Sim::MessageValue>> &values) {
    for (size_t v = 0; v < values.size(); ++v) {
        QStringList names = values[v].keys();
        names.sort();
        out << "  " << what << ' ' << v << ':';
        for (const QString &name : names)
            out << ' ' << name << '=' << valueText(values[v].value(name));
        out << '\n';
    }
}

// Everything a frame says of the run, but what a run's place in the
// process gives it (its id, the host's ticks, when it was published, the
// commands taken) and the rate reached.
QString FrameText(const Sim::SimulationFrame &f) {
    QString text;
    QTextStream out(&text);
    out << "time " << n(f.time) << " asked " << n(f.askedTime) << " step " << n(f.step) << " steps " << f.steps
        << " mode " << int(f.mode) << " speed " << n(f.speed) << " target " << n(f.target) << " finished "
        << int(f.finished) << " driven " << f.driven << " end " << int(f.activityEnd) << ' ' << n(f.activityEndedAt)
        << " collision " << n(f.collision.time) << ',' << f.collision.train << ',' << f.collision.other << ','
        << int(f.collision.standing) << ',' << n(f.collision.speed) << '\n';
    for (size_t t = 0; t < f.trains.size(); ++t) {
        const Sim::TrainFrame &train = f.trains[t];
        const Sim::TrainState &s = train.state;
        const Sim::TrainState::Passengers &p = s.passengers;
        out << "train " << t << " status " << int(s.status) << " part " << s.part << " front " << n(s.front)
            << " speed " << n(s.speed) << " acc " << n(s.acceleration) << " reversed " << int(s.reversed)
            << " stopped " << int(s.stoppedAt) << " until " << n(s.until) << " next " << s.nextStop << ','
            << s.nextWait << " travelled " << n(s.travelled) << " authority " << n(s.authority) << " blocked "
            << s.blockedBy << ',' << s.waitSection << ',' << int(s.waitFor) << " signal " << s.atSignal << ' '
            << n(s.signalSpeed) << " deadlocked " << int(s.deadlocked) << " eventStop " << s.eventStop
            << " joined " << s.joinedTo << " atStation " << s.atStation << " people " << p.stop << ','
            << p.waiting << ',' << p.alighting << ',' << p.boarded << ',' << p.alighted << ',' << p.missed << ','
            << int(p.left) << ',' << n(p.arrived) << ',' << p.doors << ',' << n(p.doorsOpenSeconds) << ','
            << p.boardedTotal << ',' << p.alightedTotal << ',' << p.missedTotal << " cars " << train.carsVersion
            << " drawn " << n(train.drawnTravelled) << " pathPart " << train.pathPart << " keeps "
            << int(train.keepsToAuthority) << '\n';
        out << "  path";
        if (train.pathNodes != nullptr)
            for (int node : *train.pathNodes)
                out << ' ' << node;
        out << "\n  fronts";
        for (const Sim::TrackPose &pose : train.fronts)
            writePose(out, pose);
        out << "\n  rears";
        for (const Sim::TrackPose &pose : train.rears)
            writePose(out, pose);
        out << '\n';
        if (train.vehicles != nullptr)
            for (const Sim::Vehicle &v : *train.vehicles)
                writeVehicle(out, v);
    }
    const auto ints = [&out](const char *what, const std::shared_ptr<const std::vector<int>> &list) {
        out << what;
        if (list != nullptr)
            for (int value : *list)
                out << ' ' << value;
        out << '\n';
    };
    ints("aspects", f.aspects);
    ints("drawStates", f.drawStates);
    out << "sections";
    if (f.sectionUses != nullptr)
        for (const Sim::SimulationCore::SectionUse &use : *f.sectionUses)
            out << ' ' << use.holder << (use.occupied ? "o" : "");
    out << "\nstandsOn";
    for (size_t on : f.standsOn)
        out << ' ' << on;
    out << "\ncrossings";
    if (f.levelCrossings != nullptr)
        for (unsigned char closed : *f.levelCrossings)
            out << ' ' << int(closed);
    out << '\n';
    if (f.eventStates != nullptr)
        for (size_t e = 0; e < f.eventStates->size(); ++e) {
            const Sim::EventState &s = (*f.eventStates)[e];
            out << "event " << e << " level " << s.level << " fired " << s.fired << " disabled " << int(s.disabled)
                << " at " << n(s.firedAt) << " by " << s.firedBy << " standIn " << int(s.standIn) << " active "
                << n(s.activeSince) << " passed " << n(s.passedAt) << ',' << int(s.passedActive) << " best "
                << n(s.bestSpeed) << " siding " << int(s.reachedSiding) << '\n';
        }
    if (f.standing != nullptr)
        for (size_t k = 0; k < f.standing->size(); ++k) {
            const Sim::StandingFrame &st = (*f.standing)[k];
            out << "standing " << k << " '" << st.name << "' gone " << int(st.gone) << " loose " << int(st.loose)
                << "\n  fronts";
            for (const Sim::TrackPose &pose : st.fronts)
                writePose(out, pose);
            out << "\n  rears";
            for (const Sim::TrackPose &pose : st.rears)
                writePose(out, pose);
            out << '\n';
            for (const Sim::Vehicle &v : st.vehicles)
                writeVehicle(out, v);
        }
    if (f.drivenValues != nullptr) {
        const Sim::DrivenFrame &d = *f.drivenValues;
        out << "drivenValues train " << d.train << " cab " << d.cab << " controls";
        for (double value : d.cabControls)
            out << ' ' << n(value);
        out << '\n';
        writeValues(out, "values", d.values);
    }
    for (const Sim::HeardFrame &h : f.heard) {
        out << "heard " << h.train << " cab " << h.cab << '\n';
        writeValues(out, "values", h.values);
    }
    out.flush();
    return text;
}

bool writeFile(const QString &path, const QString &text) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(text.toUtf8()) >= 0;
}

QString EventText(const Sim::TrainEvent &e) {
    return QString("at %1 train %2 kind %3 index %4 waitFor %5 signal %6 travelled %7 count %8\n")
            .arg(n(e.time)).arg(e.train).arg(int(e.kind)).arg(e.index).arg(int(e.waitFor)).arg(e.signal)
            .arg(n(e.travelled)).arg(e.count);
}

// Runs through the controller, as the editor's buttons run them, and what
// their frames say at each stop.
struct Recorder {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    QString text;
    size_t eventsWritten = 0;
    QStringList failures;
    // Each stop's frame text, by its name.
    QHash<QString, QString> frames;
    // The commands each frame acknowledged, in the order taken: never fewer
    // within a run.
    unsigned int ackRun = 0;
    unsigned long long ackLast = 0;
    int ackBack = 0;
    // The consist copies' vehicles checked against their files, and those
    // whose Eng is another file's.
    int copiedVehicles = 0;
    QStringList copyProblems;

    void acknowledged(const Sim::SimulationFrame &frame) {
        if (frame.runId != ackRun) {
            ackRun = frame.runId;
            ackLast = 0;
        }
        if (frame.acknowledged < ackLast)
            ++ackBack;
        ackLast = frame.acknowledged;
    }

    // The last command taken and the run paused (a run to a time ends
    // paused); the controller polled meanwhile, which may ask again.
    FramePointer settled(int ms = 600000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            QCoreApplication::processEvents();
            simulation->poll();
            FramePointer now = simulation->frame();
            acknowledged(*now);
            if (simulation->exists() && simulation->input() != nullptr && now->runId == simulation->input()->runId
                    && now->acknowledged >= simulation->lastCommand() && now->mode == Sim::RunMode::Paused)
                return now;
            QThread::msleep(1);
        }
        return nullptr;
    }

    // Each train's consist copy (the view's): every vehicle's Eng the one
    // of the vehicle's file, as the frame names it.
    void checkCopies(const QString &name) {
        FramePointer f = simulation->frame();
        for (size_t t = 0; t < f->trains.size(); ++t) {
            const Sim::TrainFrame &train = f->trains[t];
            Consist *copy = simulation->consistOf(int(t));
            if (copy == nullptr || train.vehicles == nullptr || train.fronts.empty())
                continue;
            for (int v = 0; v < copy->engItems.size() && size_t(v) < train.vehicles->size(); ++v) {
                const Sim::Vehicle &vehicle = (*train.vehicles)[size_t(v)];
                const Eng *eng = copy->engItems[v].engPointer;
                ++copiedVehicles;
                if (vehicle.file.isEmpty() || eng == nullptr
                        || eng->hashid != ContentPath::key(Sim::VehicleFilePath(vehicle.file)))
                    copyProblems << QString("%1: train %2 vehicle %3 (%4) drawn by %5").arg(name).arg(t).arg(v)
                                            .arg(vehicle.file, eng != nullptr ? eng->pathid : QString("none"));
            }
        }
    }

    void write(const QString &name) {
        FramePointer f = settled();
        if (f == nullptr) {
            failures << name + ": the run did not settle";
            return;
        }
        const QString frame = FrameText(*f);
        frames.insert(name, frame);
        text += "== " + name + '\n' + frame;
        const std::vector<Sim::TrainEvent> &events = simulation->events();
        for (size_t e = eventsWritten; e < events.size(); ++e)
            text += EventText(events[e]);
        eventsWritten = events.size();
    }

    bool runTo(double time, const QString &name) {
        QString error;
        if (!simulation->runToSeconds(time, &error)) {
            failures << name + ": " + error;
            return false;
        }
        write(name);
        return true;
    }

    // Started paused, run to fixed times after its start, back to one of
    // them from the start again, reset, stopped.
    void activity(Activity *activity, const QString &name, const std::vector<double> &after) {
        QString error;
        simulation->stop();
        eventsWritten = 0;
        if (!simulation->start(activity, &error)) {
            failures << name + ": " + error;
            return;
        }
        write(name + " start");
        const double start = simulation->input()->startTime;
        for (double seconds : after) {
            runTo(start + seconds, QString("%1 +%2").arg(name).arg(seconds));
            checkCopies(QString("%1 +%2").arg(name).arg(seconds));
        }
        if (after.size() > 1) {
            eventsWritten = 0;
            const double back = after[after.size() / 2];
            runTo(start + back, QString("%1 back to +%2").arg(name).arg(back));
        }
        eventsWritten = 0;
        simulation->reset();
        write(name + " reset");
        simulation->stop();
    }

    // Driven from the start (Drive beside Play): its direction, brake and
    // throttle set at fixed times, as keys would.
    void driving(Activity *activity, const QString &name) {
        QString error;
        simulation->stop();
        eventsWritten = 0;
        if (!simulation->start(activity, &error, true)) {
            failures << name + " driven: " + error;
            return;
        }
        write(name + " driven start");
        const double start = simulation->input()->startTime;
        runTo(start + 5.0, name + " driven +5");
        simulation->command("directionIncrease", true);
        runTo(start + 7.0, name + " driven +7");
        simulation->command("directionIncrease", false);
        simulation->command("trainBrake", 0.0);
        simulation->command("engineBrake", 0.0);
        simulation->command("throttle", 0.6);
        runTo(start + 60.0, name + " driven +60");
        runTo(start + 180.0, name + " driven +180");
        simulation->command("throttle", 0.0);
        simulation->command("trainBrake", 0.5);
        runTo(start + 300.0, name + " driven +300");
        simulation->stop();
    }

    // The same runs as `activity`'s by a session alone, no view (a server's,
    // task server 05): each frame by its name with " session"; false when
    // a run does not settle.
    bool sessionAlone(Activity *activity, const QString &name, const std::vector<double> &after,
                      QByteArray *digest = nullptr) {
        simulation->stop();
        Sim::SimulationSession session;
        QString error;
        if (!session.start(activity, Sim::RunOptions::Current(), &error)) {
            failures << name + " session: " + error;
            return false;
        }
        if (digest != nullptr)
            *digest = session.input()->digest();
        const auto settle = [&](const QString &stop) {
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < 600000) {
                QCoreApplication::processEvents();
                session.update();
                FramePointer now = session.frame();
                if (now != nullptr && now->runId == session.runId() && now->acknowledged >= session.lastCommand()
                        && now->mode == Sim::RunMode::Paused) {
                    frames.insert(stop + " session", FrameText(*now));
                    return true;
                }
                QThread::msleep(1);
            }
            failures << stop + " session: the run did not settle";
            return false;
        };
        bool settledAll = settle(name + " start");
        const double start = session.input()->startTime;
        for (double seconds : after) {
            session.runTo(start + seconds, false, -1, &error);
            settledAll = settle(QString("%1 +%2").arg(name).arg(seconds)) && settledAll;
        }
        session.stop();
        return settledAll;
    }

    // The two frames are the same, but for their events and where the
    // host's last run to a time stopped (the host keeps it over a reset).
    bool same(const QString &a, const QString &b) const {
        static const QRegularExpression target(" target [^ ]+ ");
        return frames.contains(a) && frames.contains(b)
               && QString(frames.value(a)).replace(target, " ") == QString(frames.value(b)).replace(target, " ");
    }
};

}

int TsreTests::runSimulationSessionSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:simulation-session] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:simulation-session] FAIL" << name; }
    };
    // The run's first day, as the world's date gives it (the outside air).
    const QScopedValueRollback<QString> day(Game::timeOfDayDate, "2026-10-15");
    Recorder recorder;
    // A session alone (no view) runs the same; its input made again has the
    // same digest. That digest.
    const auto sessionChecks = [&](Activity *activity, const QString &name, const std::vector<double> &after) {
        QByteArray first;
        bool same = recorder.sessionAlone(activity, name, after, &first);
        same = same && recorder.same(name + " start", name + " start session");
        for (double seconds : after) {
            const QString stop = QString("%1 +%2").arg(name).arg(seconds);
            same = same && recorder.same(stop, stop + " session");
        }
        check(same, name + ": a session alone, no view: the same frames as through the view");
        Sim::SimulationSession again;
        QString error;
        const bool made = again.start(activity, Sim::RunOptions::Current(), &error);
        const QByteArray second = made ? again.input()->digest() : QByteArray();
        again.stop();
        check(!first.isEmpty() && first == second,
              QString("%1: its input made twice, the same digest %2").arg(name, QString(first.toHex().left(16))));
        return first;
    };
    const QStringList named = qEnvironmentVariable("TSRE_SIM_BASELINE_ACTIVITY").split(',', Qt::SkipEmptyParts);

    if (!named.isEmpty()) {
        // A route's activities and timetable sets, as the editor loads them.
        if (!Game::checkRoot(Game::root) || Game::route.trimmed().isEmpty() || !Game::checkRoute(Game::route)) {
            qWarning() << "simulation-session: TSRE_SIM_BASELINE_ACTIVITY needs --game-root and --route";
            return 1;
        }
        Game::gui = false;
        Game::consoleOutput = true;
        if (Game::currentShapeLib == nullptr)
            Game::currentShapeLib = new ShapeLib();
        if (Game::currentEngLib == nullptr)
            Game::currentEngLib = new EngLib();
        Route *route = new Route();
        route->load();
        check(route->loaded, "the route loads: " + Game::route);
        if (!route->loaded)
            return 1;
        for (const QString &wanted : named) {
            const QString name = wanted.trimmed();
            Activity *activity = nullptr;
            for (int id : route->timetableSetId) {
                Activity *set = ActLib::TimetableSets.value(id);
                if (set != nullptr && set->name.compare(name, Qt::CaseInsensitive) == 0)
                    activity = set;
            }
            for (int id : route->activityId) {
                Activity *act = ActLib::Act.value(id);
                if (activity == nullptr && act != nullptr
                        && (act->nameid.compare(name, Qt::CaseInsensitive) == 0
                            || QFileInfo(act->pathid).fileName().compare(name, Qt::CaseInsensitive) == 0))
                    activity = act;
            }
            check(activity != nullptr && activity->loaded == 1, "the activity is there: " + name);
            if (activity == nullptr || activity->loaded != 1)
                continue;
            route->activitySelected(activity);
            const bool timetables = activity->isTimetables();
            // A timetable set's day (Prudnik's runs from the small hours).
            const std::vector<double> after = timetables
                    ? std::vector<double>{600, 1800, 3600, 7200, 14400}
                    : std::vector<double>{60, 300, 900, 1800, 3600, 7200};
            recorder.activity(activity, name, after);
            if (!timetables)
                recorder.driving(activity, name);
            sessionChecks(activity, name, after);
            check(recorder.same(QString("%1 +%2").arg(name).arg(after[after.size() / 2]),
                                QString("%1 back to +%2").arg(name).arg(after[after.size() / 2])),
                  name + ": run back to a time from the start again, the same frame as on the way");
            check(recorder.same(name + " start", name + " reset"), name + ": reset, the same frame as at the start");
        }
    } else {
        // simulation-ui's route.
        QTemporaryDir temp;
        const QString routePath = temp.path() + "/ROUTES/R";
        check(WriteSimulationUiRoute(temp.path()), "write the test route");
        TestTrack::Layout layout;
        MakeSimulationUiLayout(layout);
        const QScopedValueRollback<QString> root(Game::root, temp.path());
        const QScopedValueRollback<QString> routeName(Game::route, "R");
        const QScopedValueRollback<TDB *> trackDB(Game::trackDB, &layout.db);
        TerrainLib terrain;
        const QScopedValueRollback<TerrainLib *> flat(Game::terrainLib, &terrain);
        EngLib engines;
        const QScopedValueRollback<EngLib *> engineLibrary(Game::currentEngLib, &engines);
        Route *route = new Route();
        route->loadPaths();
        Path *main = ActLib::GetPathByName("Main", routePath);
        Path *second = ActLib::GetPathByName("Second", routePath);
        for (auto [path, track] : {std::make_pair(main, 10), std::make_pair(second, 30)})
            if (path != nullptr) {
                PathTrack::Result made = PathTrack::startAt(&layout.db, {track, 100.0f});
                made = PathTrack::endAt(&layout.db, made.nodes, {track, 1900.0f});
                path->apply(made.nodes);
            }
        route->loadServices();
        route->loadTraffic();
        route->loadActivities();
        Activity *activity = nullptr;
        for (int id : route->activityId)
            if (ActLib::Act.value(id) != nullptr && ActLib::Act.value(id)->header != nullptr
                    && ActLib::Act.value(id)->header->name == "Simulated")
                activity = ActLib::Act.value(id);
        check(activity != nullptr && activity->loaded == 1, "the activity loads");
        if (activity != nullptr && activity->loaded == 1) {
            const std::vector<double> after = {30, 60, 120, 300, 600};
            recorder.activity(activity, "Simulated", after);
            recorder.driving(activity, "Simulated");
            check(recorder.same("Simulated +120", "Simulated back to +120"),
                  "run back to a time from the start again: the same frame as on the way");
            check(recorder.same("Simulated start", "Simulated reset"), "reset: the same frame as at the start");
            const QByteArray digest = sessionChecks(activity, "Simulated", after);

            // A run to a time the host has passed when it takes it (asked just
            // before it while running at 128x, 1.28 s a tick): from the start
            // again to it, the same frame as on the way.
            Sim::SimulationController *simulation = recorder.simulation;
            QString error;
            simulation->stop();
            if (simulation->start(activity, &error)) {
                const unsigned int firstRun = simulation->input()->runId;
                const double start = simulation->input()->startTime;
                simulation->setSpeed(128.0);
                simulation->play(activity);
                QElapsedTimer waited;
                waited.start();
                while (simulation->frame()->time < start + 59.0 && waited.elapsed() < 10000) {
                    simulation->poll();
                    QThread::usleep(200);
                }
                simulation->runToSeconds(start + 60.0, &error);
                FramePointer f = recorder.settled();
                const bool again = simulation->input() != nullptr && simulation->input()->runId != firstRun;
                simulation->setSpeed(1.0);
                f = recorder.settled();
                if (f != nullptr)
                    recorder.frames.insert("Simulated passed +60", FrameText(*f));
                check(recorder.same("Simulated +60", "Simulated passed +60"),
                      QString("a run to a time just ahead while running (%1): the same frame as on the way")
                              .arg(again ? "passed: from the start again" : "not passed"));
                simulation->stop();
            } else {
                check(false, "a run to a time while running as fast as possible: " + error);
            }

            // The digest: the same input made in another game root, with
            // another EngLib whose numbers are not these (another vehicle
            // read first), as another process has them; another train's
            // start time, another date: another digest.
            QTemporaryDir elsewhere;
            check(WriteSimulationUiRoute(elsewhere.path())
                  && writeFile(elsewhere.path() + "/TRAINS/TRAINSET/OTHER/other.wag",
                               "SIMISA@@@@@@@@@@JINX0D0t______\n\nWagon ( other\n\tType ( Freight )\n\tSize ( 3m 4m 12m )\n)\n"),
                  "write the test route in another game root");
            TestTrack::Layout layout2;
            MakeSimulationUiLayout(layout2);
            {
                const QScopedValueRollback<QString> root2(Game::root, elsewhere.path());
                const QScopedValueRollback<TDB *> trackDB2(Game::trackDB, &layout2.db);
                EngLib shifted;
                shifted.addEng(elsewhere.path() + "/TRAINS/TRAINSET/OTHER", "other.wag");
                const QScopedValueRollback<EngLib *> engineLibrary2(Game::currentEngLib, &shifted);
                const QString routePath2 = elsewhere.path() + "/ROUTES/R";
                Route *route2 = new Route();
                route2->loadPaths();
                for (const char *name : {"Main", "Second"}) {
                    Path *path = ActLib::GetPathByName(name, routePath2);
                    const int track = QString(name) == "Main" ? 10 : 30;
                    if (path != nullptr) {
                        PathTrack::Result made = PathTrack::startAt(&layout2.db, {track, 100.0f});
                        made = PathTrack::endAt(&layout2.db, made.nodes, {track, 1900.0f});
                        path->apply(made.nodes);
                    }
                }
                route2->loadServices();
                route2->loadTraffic();
                route2->loadActivities();
                Activity *activity2 = nullptr;
                for (int id : route2->activityId)
                    if (ActLib::Act.value(id) != nullptr && ActLib::Act.value(id)->header != nullptr
                            && ActLib::Act.value(id)->header->name == "Simulated")
                        activity2 = ActLib::Act.value(id);
                Sim::SimulationSession session;
                if (activity2 != nullptr && session.start(activity2, Sim::RunOptions::Current(), &error)) {
                    // Each vehicle found by its file in this EngLib, at another
                    // number than in the first.
                    int found = 0, renumbered = 0, vehicles = 0;
                    for (const Sim::TrainPlan &plan : session.input()->trains)
                        for (const Sim::Vehicle &v : plan.vehicles) {
                            ++vehicles;
                            int here = -1;
                            const Eng *eng = Sim::EngOfFile(v.file, &here);
                            found += eng != nullptr && eng->hashid == ContentPath::key(Sim::VehicleFilePath(v.file)) ? 1 : 0;
                            const int there = engines.getEngByPathid(ContentPath::join(temp.path(), v.file));
                            renumbered += here >= 0 && there >= 0 && here != there ? 1 : 0;
                        }
                    check(vehicles > 0 && found == vehicles && renumbered == vehicles,
                          QString("in another game root and EngLib: each vehicle's Eng found by its file (%1 of %2), "
                                  "at another number (%3)").arg(found).arg(vehicles).arg(renumbered));
                    check(session.input()->digest() == digest,
                          "in another game root and EngLib: the same digest");
                    session.stop();
                } else {
                    check(false, "the activity in another game root: " + error);
                }
            }
            Sim::SimulationSession session;
            if (session.start(activity, Sim::RunOptions::Current(), &error)) {
                Sim::SimulationInput edited = *session.input();
                if (edited.trains.size() > 1)
                    edited.trains[1].startTime += 60;
                check(edited.trains.size() > 1 && edited.digest() != digest, "a train's start time edited: another digest");
                session.stop();
            }
            Sim::RunOptions summer = Sim::RunOptions::Current();
            // Inside the activity's season (summer): its first day.
            summer.date = "2026-08-01";
            if (session.start(activity, summer, &error)) {
                check(session.input()->air.date == QDate(2026, 8, 1) && session.input()->digest() != digest,
                      "another date (its outside air): another digest");
                session.stop();
            }
        }
        Sim::SimulationController::instance()->stop();
    }
    // Frames told apart by their run and tick (SameFrame), not by where
    // they are held: a new frame allocated where a freed one was is new, a
    // copy of one is the same.
    {
        auto old = std::make_shared<Sim::SimulationFrame>();
        old->runId = 7;
        old->tick = 41;
        const void *address = old.get();
        old.reset();
        auto next = std::make_shared<Sim::SimulationFrame>();
        next->runId = 7;
        next->tick = 42;
        Sim::SimulationFrame before;
        before.runId = 7;
        before.tick = 41;
        const Sim::SimulationFrame copy = *next;
        check(!Sim::SameFrame(next.get(), &before) && Sim::SameFrame(next.get(), &copy)
                  && !Sim::SameFrame(next.get(), nullptr) && Sim::SameFrame(nullptr, nullptr),
              QString("a new frame where a freed one was (%1): new by its run and tick; a copy the same")
                      .arg(next.get() == address ? "at its very address" : "elsewhere this time"));
    }
    check(recorder.ackBack == 0, QString("the commands acknowledged in order (%1 frames went back)").arg(recorder.ackBack));
    check(recorder.copiedVehicles > 0 && recorder.copyProblems.isEmpty(),
          QString("the view's consist copies: each vehicle drawn by its file's Eng (%1 vehicles)%2")
                  .arg(recorder.copiedVehicles).arg(recorder.copyProblems.isEmpty() ? QString()
                                                                                    : ": " + recorder.copyProblems.mid(0, 3).join("; ")));
    for (const QString &failure : recorder.failures)
        check(false, failure);
    const QByteArray bytes = recorder.text.toUtf8();
    check(!recorder.frames.isEmpty(), QString("%1 frames written, %2 bytes, sha1 %3")
                                            .arg(recorder.frames.size()).arg(bytes.size())
                                            .arg(QString(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex())));
    const QString out = qEnvironmentVariable("TSRE_SIM_BASELINE_OUT");
    if (!out.isEmpty()) {
        QFile file(out);
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size(),
              "the frames written to " + out);
    }
    qInfo().noquote() << QString("simulation-session: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
