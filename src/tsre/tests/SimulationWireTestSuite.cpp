#include <tsre/tests/SimulationTestSuite.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <functional>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <memory>
#include <vector>
#include <tsre/Game.h>
#include <tsre/fileFunctions/RouteFiles.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/sim/RunData.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/SimulationSession.h>
#include <tsre/sim/SimulationWire.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/PathTrack.h>
#include <tsre/trains/TrainFields.h>
#include <tsre/world/Route.h>
#include <tsre/world/TerrainLib.h>

namespace {

// What two trains' plans differ in, by field (the first few).
QString PlanDifference(const Sim::TrainPlan &a, const Sim::TrainPlan &b) {
    QStringList out;
    const auto field = [&](const char *name, const QString &x, const QString &y) {
        if (x != y)
            out << QString("%1 %2 / %3").arg(name, x.left(60), y.left(60));
    };
    const auto n = [](double v) { return QString::number(v, 'g', 9); };
    field("service", a.service, b.service);
    field("consist", a.consist, b.consist);
    field("vehicles", QString::number(a.vehicles.size()), QString::number(b.vehicles.size()));
    for (size_t v = 0; v < std::min(a.vehicles.size(), b.vehicles.size()); ++v) {
        field("vehicle file", a.vehicles[v].file, b.vehicles[v].file);
        field("vehicle flip", QString::number(a.vehicles[v].flip), QString::number(b.vehicles[v].flip));
        field("vehicle number", a.vehicles[v].number, b.vehicles[v].number);
    }
    field("length", n(a.length), n(b.length));
    field("startTime", QString::number(a.startTime), QString::number(b.startTime));
    field("createTime", QString::number(a.createTime), QString::number(b.createTime));
    field("formedBy", QString::number(a.formedBy), QString::number(b.formedBy));
    field("staysAtEnd", QString::number(a.staysAtEnd), QString::number(b.staysAtEnd));
    field("maxSpeed", n(a.maxSpeed), n(b.maxSpeed));
    field("startFront", n(a.startFront), n(b.startFront));
    field("parts", QString::number(a.route.parts.size()), QString::number(b.route.parts.size()));
    field("stops", QString::number(a.stops.size()), QString::number(b.stops.size()));
    for (size_t k = 0; k < std::min(a.stops.size(), b.stops.size()); ++k) {
        field("stop platform", a.stops[k].platform, b.stops[k].platform);
        field("stop arrival", QString::number(a.stops[k].arrival), QString::number(b.stops[k].arrival));
        field("stop departure", QString::number(a.stops[k].departure), QString::number(b.stops[k].departure));
        field("stop front", n(a.stops[k].front), n(b.stops[k].front));
        field("stop holdsExit", QString::number(a.stops[k].holdsExit), QString::number(b.stops[k].holdsExit));
        field("stop passengers", QString::number(a.stops[k].passengers), QString::number(b.stops[k].passengers));
    }
    field("waits", QString::number(a.waits.size()), QString::number(b.waits.size()));
    field("follows", QString::number(a.follows.size()), QString::number(b.follows.size()));
    field("extra", QStringList(a.extra.keys()).join(","), QStringList(b.extra.keys()).join(","));
    for (int f = 0; f < TrainFields::Count; ++f)
        field("field", a.fields[f], b.fields[f]);
    field("activatedBy", QString::number(a.activatedBy), QString::number(b.activatedBy));
    field("cruise", n(a.cruiseSpeed), n(b.cruiseSpeed));
    field("factors", n(a.accelerationFactor) + " " + n(a.decelerationFactor),
          n(b.accelerationFactor) + " " + n(b.decelerationFactor));
    return out.isEmpty() ? QString("something else") : out.mid(0, 4).join("; ");
}

using FramePointer = std::shared_ptr<const Sim::SimulationFrame>;

// A run's frames as the host publishes them: the session played at
// `speed`, its trains heard, polled for `ms` of wall time; each new frame
// once. Driven from its start with `driving` (the throttle opened).
std::vector<FramePointer> CollectFrames(Activity *activity, bool driving, double speed, int ms,
                                        std::shared_ptr<const Sim::SimulationInput> *input,
                                        std::vector<Sim::TrainEvent> *events, std::vector<Sim::TrainMessage> *messages) {
    std::vector<FramePointer> frames;
    Sim::SimulationSession session;
    Sim::RunOptions options = Sim::RunOptions::Current(driving);
    QString error;
    if (!session.start(activity, options, &error)) {
        qWarning() << "simulation-wire: no run:" << error;
        return frames;
    }
    *input = session.input();
    std::vector<int> all;
    for (size_t t = 0; t < session.input()->trains.size(); ++t)
        all.push_back(int(t));
    session.hear(all);
    // Drive beside Play: the input's driven train (commands name their
    // train, task server 06).
    const int drivenTrain = session.input()->driven;
    if (driving) {
        session.command(drivenTrain, -1, Sim::Message{"directionIncrease", true});
        session.command(drivenTrain, -1, Sim::Message{"trainBrake", 0.0});
        session.command(drivenTrain, -1, Sim::Message{"throttle", 0.7});
    }
    session.setSpeed(speed);
    session.play();
    QElapsedTimer timer;
    timer.start();
    unsigned long long tick = 0;
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents();
        session.update();
        const FramePointer now = session.frame();
        if (now != nullptr && now->tick != tick && now->runId == session.runId()) {
            tick = now->tick;
            frames.push_back(now);
        }
        if (driving && timer.elapsed() > ms / 2)
            session.command(drivenTrain, -1, Sim::Message{"directionIncrease", false});
        QThread::msleep(2);
    }
    session.update();
    *events = session.events();
    *messages = session.takeMessages();
    session.stop();
    return frames;
}

// The frames sent as a client gets them: the first whole, then changes;
// each decoded the same as its original (the task 04 frame text). Then a
// client that skipped frames (its changes refused, a whole frame taken), one
// joining late, an older frame refused. The bytes' sizes, raw and as task
// 02 compresses them.
struct WireResult {
    int frames = 0, same = 0, changed = 0;
    bool skipRefused = false, skipWhole = false, lateSame = false, olderRefused = false;
    // A client's clock 200 ppm faster than the server's, an hour: how far a
    // frame's time on it is from its arrival (less the network's 5 ms).
    double driftMs = -1;
    qint64 changesBytes = 0, wholeBytes = 0, changesZipped = 0, wholeZipped = 0;
    QString firstDifference;
};

WireResult SendFrames(const std::vector<FramePointer> &frames, std::shared_ptr<const Sim::SimulationInput> input) {
    WireResult r;
    Sim::Wire::FrameEncoder encoder;
    Sim::Wire::FrameDecoder client;
    client.setInput(input);
    std::vector<QByteArray> wholes;
    for (size_t i = 0; i < frames.size(); ++i) {
        encoder.next(frames[i]);
        const QByteArray bytes = i == 0 ? encoder.whole() : encoder.changes();
        const QByteArray whole = encoder.whole();
        wholes.push_back(whole);
        ++r.frames;
        r.changed += i == 0 ? 0 : encoder.changedTrains();
        if (i > 0) {
            r.changesBytes += bytes.size();
            r.changesZipped += qCompress(bytes, 6).size();
        }
        r.wholeBytes += whole.size();
        r.wholeZipped += qCompress(whole, 6).size();
        QString error;
        const FramePointer got = client.decode(bytes, Sim::SteadyNs(), &error);
        if (got != nullptr && TsreTests::SimulationFrameText(*got) == TsreTests::SimulationFrameText(*frames[i]))
            ++r.same;
        else if (r.firstDifference.isEmpty())
            r.firstDifference = QString("frame %1: %2").arg(i).arg(got == nullptr ? error : QString("its text differs"));
    }
    if (frames.size() >= 8) {
        // Skipped frames 3 to 5: frame 6's changes do not follow on.
        Sim::Wire::FrameEncoder again;
        Sim::Wire::FrameDecoder lagging;
        lagging.setInput(input);
        for (size_t i = 0; i <= 6; ++i) {
            again.next(frames[i]);
            const QByteArray bytes = i == 0 ? again.whole() : again.changes();
            if (i <= 2)
                lagging.decode(bytes, Sim::SteadyNs());
            else if (i == 6) {
                r.skipRefused = lagging.decode(bytes, Sim::SteadyNs()) == nullptr;
                const FramePointer whole = lagging.decode(again.whole(), Sim::SteadyNs());
                r.skipWhole = whole != nullptr
                        && TsreTests::SimulationFrameText(*whole) == TsreTests::SimulationFrameText(*frames[6]);
            }
        }
        // A client joining at frame 5; then frame 4 again (older).
        Sim::Wire::FrameDecoder late;
        late.setInput(input);
        const FramePointer joined = late.decode(wholes[5], Sim::SteadyNs());
        r.lateSame = joined != nullptr
                && TsreTests::SimulationFrameText(*joined) == TsreTests::SimulationFrameText(*frames[5]);
        r.olderRefused = late.decode(wholes[4], Sim::SteadyNs()) == nullptr;
    }
    if (!frames.empty()) {
        // Frame 0 again every 10 s of the server's time for an hour, each
        // arriving 5 ms later plus what the fast clock gained (the review:
        // the offset only fell, so its error grew all session).
        Sim::Wire::FrameEncoder stamped;
        Sim::Wire::FrameDecoder drifting;
        drifting.setInput(input);
        const long long start = 1000000000LL;
        for (int i = 0; i < 360; ++i) {
            auto copy = std::make_shared<Sim::SimulationFrame>(*frames[0]);
            copy->tick = frames[0]->tick + unsigned(i) + 1;
            copy->publishedNs = start + i * 10000000000LL;
            stamped.next(copy);
            const long long arrival = copy->publishedNs + 5000000LL + (long long)(200e-6 * double(i) * 1e10);
            const FramePointer got = drifting.decode(stamped.whole(), arrival);
            if (got != nullptr)
                r.driftMs = double(arrival - got->publishedNs) * 1e-6;
        }
    }
    return r;
}

// The run's events and messages, encoded and decoded the same.
bool SameEvents(const std::vector<Sim::TrainEvent> &events) {
    std::vector<Sim::TrainEvent> back;
    if (!Sim::Wire::DecodeEvents(Sim::Wire::EncodeEvents(events), back) || back.size() != events.size())
        return false;
    for (size_t i = 0; i < events.size(); ++i) {
        const Sim::TrainEvent &a = events[i], &b = back[i];
        if (a.time != b.time || a.train != b.train || a.kind != b.kind || a.index != b.index || a.waitFor != b.waitFor
                || a.signal != b.signal || a.travelled != b.travelled || a.count != b.count)
            return false;
    }
    return true;
}

bool SameMessages(const std::vector<Sim::TrainMessage> &messages) {
    std::vector<Sim::TrainMessage> back;
    if (!Sim::Wire::DecodeMessages(Sim::Wire::EncodeMessages(messages), back) || back.size() != messages.size())
        return false;
    for (size_t i = 0; i < messages.size(); ++i)
        if (messages[i].train != back[i].train || messages[i].vehicle != back[i].vehicle
                || messages[i].message.name != back[i].message.name || messages[i].message.value != back[i].message.value)
            return false;
    return true;
}

// The frames of a run played, driven or not, sent and checked.
void CheckWire(Activity *activity, const QString &name, bool driving, double speed, int ms,
               const std::function<void(bool, const QString &)> &check) {
    std::shared_ptr<const Sim::SimulationInput> input;
    std::vector<Sim::TrainEvent> events;
    std::vector<Sim::TrainMessage> messages;
    const std::vector<FramePointer> frames = CollectFrames(activity, driving, speed, ms, &input, &events, &messages);
    const WireResult r = SendFrames(frames, input);
    bool heard = false, driven = false, standing = false;
    for (const FramePointer &f : frames) {
        heard = heard || !f->heard.empty();
        driven = driven || !f->drivenValues.empty();
        standing = standing || (f->standing != nullptr && !f->standing->empty());
    }
    const auto kb = [](qint64 bytes, int count) { return QString::number(count > 0 ? bytes / 1024.0 / count : 0.0, 'f', 2); };
    check(r.frames > 8 && r.same == r.frames,
          QString("%1%2: %3 frames sent (the first whole, then changes; %4 trains changed a frame), each decoded the "
                  "same as its original%5%6%7; changes %8 KB a frame (%9 KB compressed), whole %10 KB (%11 KB)%12")
                  .arg(name, driving ? " driven" : "").arg(r.frames)
                  .arg(r.frames > 1 ? QString::number(double(r.changed) / (r.frames - 1), 'f', 1) : QString("0"))
                  .arg(heard ? ", heard trains" : "", driven ? ", the driven train's values" : "",
                       standing ? ", standing trains" : "")
                  .arg(kb(r.changesBytes, r.frames - 1), kb(r.changesZipped, r.frames - 1), kb(r.wholeBytes, r.frames),
                       kb(r.wholeZipped, r.frames))
                  .arg(r.firstDifference.isEmpty() ? QString() : ": " + r.firstDifference));
    if (r.frames >= 8)
        check(r.skipRefused && r.skipWhole && r.lateSame && r.olderRefused,
              name + ": a client that skipped frames refuses the changes that do not follow on and takes a whole "
                     "frame; one joining late takes a whole frame; an older frame is refused");
    check(r.driftMs >= 0 && r.driftMs < 12,
          QString("%1: a client clock 200 ppm fast for an hour: its frames' times %2 ms from their arrival (the network's 5 in it)")
                  .arg(name).arg(r.driftMs, 0, 'f', 1));
    check(SameEvents(events) && SameMessages(messages),
          QString("%1: its %2 events and %3 messages encoded and decoded the same").arg(name).arg(events.size())
                  .arg(messages.size()));
}

// A run's data written by the editor's writers and read back as a client
// reads it (task server 05): served by RouteFiles under another route
// folder, loaded with the editor's loaders, the input made again. Its
// digest and each train's against the input made from the activity itself;
// what differs in `why`.
bool RoundTrip(Activity *activity, QString &why, int &files, int &trains) {
    const Sim::RunOptions options = Sim::RunOptions::Current();
    QString error;
    const std::shared_ptr<const Sim::SimulationInput> made = Sim::MakeRunInput(activity, options, &error);
    if (made == nullptr) {
        why = "no input: " + error;
        return false;
    }
    const Sim::RunData data = Sim::GatherRunData(activity);
    files = data.files.size();
    trains = int(made->trains.size());
    // The bytes as a message carries them.
    Sim::RunData received;
    if (!Sim::RunData::Decode(data.encode(), received, &error)) {
        why = error;
        return false;
    }
    const QString route = ActivityTrains::RouteDirectory(activity);
    const QString elsewhere = route + "-run05";
    const QString under = Sim::PathUnder(Game::root, elsewhere);
    QHash<QString, QByteArray> served;
    for (auto it = received.files.constBegin(); it != received.files.constEnd(); ++it)
        served.insert(under + "/" + it.key(), it.value());
    RouteFiles::setServed(Game::root, served);
    Activity *again = Sim::LoadRunData(received, elsewhere);
    RouteFiles::clearServed();
    if (again == nullptr) {
        why = "the activity does not read back";
        return false;
    }
    const std::shared_ptr<const Sim::SimulationInput> back = Sim::MakeRunInput(again, options, &error);
    if (back == nullptr) {
        why = "no input read back: " + error;
        return false;
    }
    if (back->digest() == made->digest())
        return true;
    const std::vector<QByteArray> a = made->trainDigests(), b = back->trainDigests();
    QStringList differ;
    QString first;
    for (size_t t = 0; t < std::max(a.size(), b.size()); ++t)
        if (t >= a.size() || t >= b.size() || a[t] != b[t]) {
            differ << (t < made->trains.size() ? made->trains[t].service : QString::number(t));
            if (first.isEmpty() && t < made->trains.size() && t < back->trains.size())
                first = PlanDifference(made->trains[t], back->trains[t]);
        }
    why = differ.isEmpty() ? QString("the trains agree, the rest differs")
                           : QString("%1 trains differ: %2 (the first: %3)")
                                     .arg(differ.size()).arg(differ.mid(0, 5).join(", "), first);
    return false;
}

}

int TsreTests::runSimulationWireSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:simulation-wire] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:simulation-wire] FAIL" << name; }
    };
    const QScopedValueRollback<QString> day(Game::timeOfDayDate, "2026-10-15");
    const QStringList named = qEnvironmentVariable("TSRE_SIM_BASELINE_ACTIVITY").split(',', Qt::SkipEmptyParts);

    if (!named.isEmpty()) {
        // A route's activities and timetable sets (as simulation-session).
        if (!Game::checkRoot(Game::root) || Game::route.trimmed().isEmpty() || !Game::checkRoute(Game::route)) {
            qWarning() << "simulation-wire: TSRE_SIM_BASELINE_ACTIVITY needs --game-root and --route";
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
            for (int id : route->timetableSetId)
                if (ActLib::TimetableSets.value(id) != nullptr
                        && ActLib::TimetableSets.value(id)->name.compare(name, Qt::CaseInsensitive) == 0)
                    activity = ActLib::TimetableSets.value(id);
            for (int id : route->activityId)
                if (activity == nullptr && ActLib::Act.value(id) != nullptr
                        && QFileInfo(ActLib::Act.value(id)->pathid).fileName().compare(name, Qt::CaseInsensitive) == 0)
                    activity = ActLib::Act.value(id);
            check(activity != nullptr, "the activity is there: " + name);
            if (activity == nullptr)
                continue;
            route->activitySelected(activity);
            QString why;
            int files = 0, trains = 0;
            const bool same = RoundTrip(activity, why, files, trains);
            check(same, QString("%1: its run data (%2 files) written by Save's writers and read back as a client "
                                "reads it: the same input, its %3 trains alike%4")
                                .arg(name).arg(files).arg(trains).arg(same ? QString() : ": " + why));
            CheckWire(activity, name, false, activity->isTimetables() ? 64.0 : 16.0, 3000, check);
            if (!activity->isTimetables())
                CheckWire(activity, name, true, 4.0, 3000, check);
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
        if (activity != nullptr) {
            QString why;
            int files = 0, trains = 0;
            const bool same = RoundTrip(activity, why, files, trains);
            check(same && files == 6, QString("its run data (%1 files: the activity, its traffic, two services, "
                                              "two paths) written and read back: the same input, its %2 trains "
                                              "alike%3").arg(files).arg(trains).arg(same ? QString() : ": " + why));
            CheckWire(activity, "Simulated", false, 8.0, 2000, check);
            CheckWire(activity, "Simulated", true, 4.0, 2000, check);
        }
        check(!RouteFiles::serving(), "back to the route's own files");
    }
    qInfo().noquote() << QString("simulation-wire: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
