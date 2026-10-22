#include <tsre/tests/SimulationTestSuite.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTime>
#include <QTreeWidget>
#include <cmath>
#include <functional>
// Complete before the windows' signals name them.
#include <tsre/GameObj.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/world/Route.h>
#include <routeEditor/ActivityTools.h>
#include <routeEditor/activity/ActivityEdit.h>
#include <routeEditor/activity/ActivityEventProperties.h>
#include <routeEditor/activity/ActivityCheckWindow.h>
#include <routeEditor/activity/ActivityEventWindow.h>
#include <routeEditor/activity/ActivityTimetableProperties.h>
#include <routeEditor/activity/ActivityTimetableWindow.h>
#include <routeEditor/properties/PropertiesConsist.h>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/environment/WorldClock.h>
#include <tsre/map/MapFeatures.h>
#include <tsre/map/MapLabelLayer.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/EventResults.h>
#include <tsre/sim/SimulationHost.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/TimetableCalculation.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityEvent.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/PathTrack.h>
#include <tsre/trains/Traffic.h>
#include <tsre/world/TerrainLib.h>

namespace {

using FramePointer = std::shared_ptr<const Sim::SimulationFrame>;

// A frame meeting `ready`, waited for at most `ms` (the host's thread runs
// on its own; `poll` runs between looks).
FramePointer waitFor(const std::function<FramePointer()> &frame, const std::function<bool(const Sim::SimulationFrame &)> &ready,
                     int ms, const std::function<void()> &poll = nullptr) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        if (poll)
            poll();
        FramePointer now = frame();
        if (now != nullptr && ready(*now))
            return now;
        QThread::msleep(1);
    }
    return nullptr;
}

bool writeSimis(const QString &path, const QString &text) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QString crlf = text;
    crlf.replace("\n", "\r\n");
    file.write("\xff\xfe", 2);
    file.write(reinterpret_cast<const char *>(crlf.utf16()), crlf.size() * 2);
    return true;
}

// A route of two 2 km lines (tests/TrackLayoutSupport), each with a
// platform from 1000 to 1200 m ("Middle", "Middle 2"); the paths Main and
// Second from 100 to 1900 m on them; the services S and S2 of an engine and
// a carriage stopping there; the activity starting at 08:00 with the
// player's S and an AI S2 at 08:01 (on its own line: trains hold the track
// they use, task 02).
const char *activityText = R"(SIMISA@@@@@@@@@@JINX0a0t______

Tr_Activity (
	Serial ( 1 )
	Tr_Activity_Header (
		RouteID ( R )
		Name ( "Simulated" )
		StartTime ( 8 0 0 )
		Season ( 1 )
		Weather ( 0 )
		PathID ( "Main" )
		StartingSpeed ( 0 )
		Duration ( 1 0 )
		Difficulty ( 1 )
	)
	Tr_Activity_File (
		Player_Service_Definition ( S
			Player_Traffic_Definition ( 28800
				ArrivalTime ( 28900 )
				DepartTime ( 28960 )
				SkipCount ( 0 )
				DistanceDownPath ( 900 )
				PlatformStartID ( 101 )
			)
			UiD ( 0 )
			Efficiency ( 1 )
			SkipCount ( 0 )
			DistanceDownPath ( 900 )
			PlatformStartID ( 101 )
		)
		NextServiceUID ( 2 )
		NextActivityObjectUID ( 32768 )
		Traffic_Definition ( T
			Service_Definition ( S2 28860
				UiD ( 1 )
			)
		)
	)
)
)";

const char *serviceText = R"(SIMISA@@@@@@@@@@JINX0v0t______

Service_Definition (
	Serial ( 1 )
	Name ( "%1" )
	Train_Config ( Two )
	PathID ( "%2" )
	MaxWheelAcceleration ( 0 )
	Efficiency ( 1 )
	TimeTable (
		StartingSpeed ( 0 )
		EndingSpeed ( 0 )
		StartInWorld ( 0 )
		EndInWorld ( 0 )
		Efficiency ( 1 )
		SkipCount ( 0 )
		DistanceDownPath ( 900 )
		PlatformStartID ( %3 )
	)
)
)";

const char *trafficText = R"(SIMISA@@@@@@@@@@JINX0f0t______

Traffic_Definition ( T
	Serial ( 1 )
	Service_Definition ( S2 28860
		ArrivalTime ( 29000 )
		DepartTime ( 29060 )
		SkipCount ( 0 )
		DistanceDownPath ( 900 )
		PlatformStartID ( 201 )
	)
)
)";

const char *pathText = R"(SIMISA@@@@@@@@@@JINX0P0t______

Serial ( 1 )
TrackPDPs (
	TrackPDP ( 0 0 100 0 0 2 0 )
	TrackPDP ( 0 0 1900 0 0 2 0 )
)
TrackPath (
	TrPathName ( %1 )
	Name ( "%1" )
	TrPathStart ( "A" )
	TrPathEnd ( "B" )
	TrPathNodes ( 2
		TrPathNode ( 00000000 1 4294967295 0 )
		TrPathNode ( 00000000 4294967295 4294967295 1 )
	)
)
)";

const char *consistText = R"(SIMISA@@@@@@@@@@JINX0D0t______

Train (
	TrainCfg ( "Two"
		Name ( "Two" )
		Serial ( 1 )
		MaxVelocity ( 20 0.5 )
		NextWagonUID ( 2 )
		Durability ( 1 )
		Engine (
			UiD ( 0 )
			EngineData ( loco TEST )
		)
		Wagon (
			UiD ( 1 )
			WagonData ( car TEST )
		)
	)
)
)";

const char *vehicleText = R"(SIMISA@@@@@@@@@@JINX0D0t______

Wagon ( %1
	Type ( %2 )
	Size ( 3m 4m 20m )
	WagonShape ( missing.s )
)
)";

}

int TsreTests::runSimulationHostSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:simulation-host] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:simulation-host] FAIL" << name; }
    };

    // Two trains on a 2 km line, the second starting a minute after the
    // first (no signals yet: nothing keeps them apart).
    TestTrack::Layout straight;
    straight.end(1, 10);
    straight.track(10, 1, 2, 200);
    straight.end(2, 10);
    TDB *line = &straight.db;
    const QScopedValueRollback<TDB *> database(Game::trackDB, line);
    const PathTrack::Result run = PathTrack::startAt(line, {10, 100.0f});
    const Sim::TrainRoute route = Sim::BuildRoute(line, run.nodes, PathTrack::legs(line, run.nodes), 31.0);
    const int t0 = 8 * 3600;
    auto input = std::make_shared<Sim::SimulationInput>();
    input->startTime = t0;
    input->runId = 7;
    input->geometry = std::make_shared<const Sim::TrackGeometry>(line);
    for (int start : {t0 + 10, t0 + 60}) {
        Sim::TrainPlan p;
        p.id = int(input->trains.size());
        p.vehicles = {Sim::Vehicle{20.0f, false, 4}, Sim::Vehicle{20.0f, false, 1}};
        p.length = 40.0f;
        p.maxSpeed = 20.0f;
        Sim::SetRates(p);
        p.startTime = start;
        p.route = route;
        p.startFront = route.pathStart + 10.0;
        input->trains.push_back(p);
    }
    // Activity events (task simulation 06): a time event activating a
    // location event, location events each train triggers; the host fires
    // them at the times the core alone does (the fixed step).
    {
        const auto event = [&](Sim::EventPlan::Kind kind, int level) {
            Sim::EventPlan e;
            e.kind = kind;
            e.activation = level;
            e.index = int(input->events.size());
            e.name = QString("E%1").arg(e.index);
            return e;
        };
        Sim::EventPlan time = event(Sim::EventPlan::Time, 1);
        time.time = 30;
        time.activate = {2};
        input->events.push_back(time);
        for (const auto &[train, distance, level] : {std::make_tuple(0, 600.0f, 1), std::make_tuple(0, 1100.0f, 0),
                                                     std::make_tuple(1, 300.0f, 1)}) {
            Sim::EventPlan place = event(Sim::EventPlan::Location, level);
            place.places = {Sim::TrackPlace{10, distance}};
            place.radius = 5.0f;
            place.trigger = train;
            input->events.push_back(place);
        }
    }
    const std::shared_ptr<const Sim::SimulationInput> shared = input;

    QThread thread;
    Sim::SimulationHost *host = new Sim::SimulationHost();
    host->moveToThread(&thread);
    thread.start();
    unsigned long long seq = 0;
    const auto send = [&](const std::function<void(Sim::SimulationHost *, unsigned long long)> &command) {
        const unsigned long long s = ++seq;
        QMetaObject::invokeMethod(host, [host, command, s] { command(host, s); }, Qt::QueuedConnection);
        return s;
    };
    const auto latest = [&] { return host->frame(); };
    const auto acknowledged = [](unsigned long long s) {
        return [s](const Sim::SimulationFrame &f) { return f.acknowledged >= s; };
    };
    std::vector<Sim::TrainEvent> events;
    const auto takeEvents = [&] {
        for (const Sim::TrainEvent &e : host->takeEvents())
            events.push_back(e);
    };

    unsigned long long s = send([&](Sim::SimulationHost *h, unsigned long long n) { h->start(shared, n); });
    FramePointer f = waitFor(latest, acknowledged(s), 5000);
    check(f && f->runId == 7 && f->time == t0 && f->mode == Sim::RunMode::Paused && f->trains.size() == 2
              && f->trains[0].state.status == Sim::TrainStatus::Waiting,
          "start: a frame at the activity's start, paused, both trains waiting");
    QThread::msleep(50);
    check(host->frame()->time == t0 && host->frame()->steps == 0, "paused, the time stays");

    // Run to a time: there and paused, the same events as the core run
    // synchronously in 0.1 s steps.
    const double target = t0 + 100;
    s = send([target](Sim::SimulationHost *h, unsigned long long n) { h->runTo(target, n); });
    f = waitFor(latest, [&](const Sim::SimulationFrame &x) { return x.acknowledged >= s && x.mode == Sim::RunMode::Paused; },
                10000);
    check(f && std::fabs(f->time - target) < 1e-6, QString("run to 08:01:40: there and paused (%1)").arg(f ? f->time - t0 : -1));
    check(f && f->trains[0].fronts.size() == 2 && f->trains[1].fronts.size() == 2
              && f->trains[0].state.status == Sim::TrainStatus::Running,
          "both trains on the track, two vehicles each");
    QThread::msleep(50);
    check(f && host->frame()->time == f->time, "after a run to a time, the time stays");
    check(host->frame() == host->frame(), "the latest frame is kept until the next");
    takeEvents();
    {
        Sim::SimulationCore core(shared);
        std::vector<Sim::TrainEvent> expected;
        while (core.time() < target - 1e-6) {
            core.step(std::min(0.1, target - core.time()));
            for (const Sim::TrainEvent &e : core.takeEvents())
                expected.push_back(e);
        }
        bool same = expected.size() == events.size() && !events.empty();
        bool ordered = true;
        for (size_t i = 0; i < events.size(); ++i) {
            if (i > 0 && events[i].time < events[i - 1].time)
                ordered = false;
            if (same && (events[i].kind != expected[i].kind || events[i].train != expected[i].train
                         || events[i].index != expected[i].index || std::fabs(events[i].time - expected[i].time) > 1e-6))
                same = false;
        }
        int fired = 0;
        for (const Sim::TrainEvent &e : events)
            fired += e.kind == Sim::TrainEvent::EventFired ? 1 : 0;
        check(ordered, "events come in order");
        check(same && fired == 4, QString("the thread's run gives the synchronous core's events (%1 and %2), the "
                                          "activity's %3 among them at the same times")
                                      .arg(events.size()).arg(expected.size()).arg(fired));
    }

    // The track edited while the simulation runs: its copy stays.
    {
        Sim::TrackPose before, after;
        shared->geometry->pose(10, 300.0f, before);
        TRnode *node = line->trackNodes[10];
        for (int i = 0; i < node->iTrv; ++i)
            node->trVectorSection[i].x += 500.0f;
        float drawn[7];
        line->getDrawPositionOnTrNode(drawn, 10, 300.0f);
        shared->geometry->pose(10, 300.0f, after);
        check(before.x == after.x && before.z == after.z && std::fabs(drawn[0] - before.x) > 400.0f,
              "the live track edited, the simulation's copy is as it was");
        for (int i = 0; i < node->iTrv; ++i)
            node->trVectorSection[i].x -= 500.0f;
    }

    // As fast as possible; paused.
    send([](Sim::SimulationHost *h, unsigned long long n) { h->setSpeed(0.0, n); });
    s = send([](Sim::SimulationHost *h, unsigned long long n) { h->play(n); });
    f = waitFor(latest, [&](const Sim::SimulationFrame &x) { return x.acknowledged >= s && x.time > t0 + 300; }, 10000);
    check(f != nullptr, "as fast as possible it runs on");
    s = send([](Sim::SimulationHost *h, unsigned long long n) { h->pause(n); });
    f = waitFor(latest, acknowledged(s), 5000);
    const double pausedAt = f ? f->time : -1;
    QThread::msleep(60);
    check(f && f->mode == Sim::RunMode::Paused && host->frame()->time == pausedAt && f->finished,
          "pause is acknowledged and holds the time (both trains have finished)");
    s = send([](Sim::SimulationHost *h, unsigned long long n) { h->runTo(8 * 3600 + 60, n); });
    f = waitFor(latest, acknowledged(s), 5000);
    check(f && f->time == pausedAt && f->mode == Sim::RunMode::Paused, "a run to a time passed stays paused");

    // At a pace: real time, and 64×.
    const auto rateAt = [&](double speed) {
        send([speed](Sim::SimulationHost *h, unsigned long long n) { h->setSpeed(speed, n); });
        const unsigned long long playing = send([](Sim::SimulationHost *h, unsigned long long n) { h->play(n); });
        FramePointer first = waitFor(latest, acknowledged(playing), 5000);
        QElapsedTimer wall;
        wall.start();
        QThread::msleep(400);
        FramePointer last = host->frame();
        const double rate = first && last ? (last->time - first->time) / (wall.nsecsElapsed() * 1e-9) : -1;
        const unsigned long long paused = send([](Sim::SimulationHost *h, unsigned long long n) { h->pause(n); });
        waitFor(latest, acknowledged(paused), 5000);
        return rate;
    };
    const double realTime = rateAt(1.0);
    check(realTime > 0.5 && realTime < 1.5, QString("at 1× the simulation keeps to the clock (%1×)").arg(realTime));
    const double fast = rateAt(64.0);
    check(fast > 24.0 && fast < 96.0, QString("at 64× (%1×)").arg(fast));

    // Stop with work queued: the thread ends, the host comes back.
    send([](Sim::SimulationHost *h, unsigned long long n) { h->setSpeed(0.0, n); });
    send([](Sim::SimulationHost *h, unsigned long long n) { h->play(n); });
    for (int i = 0; i < 1000; ++i)
        send([](Sim::SimulationHost *h, unsigned long long n) { h->setSpeed(0.0, n); });
    QThread *mainThread = QThread::currentThread();
    s = send([mainThread](Sim::SimulationHost *h, unsigned long long n) { h->stop(n, mainThread); });
    const bool ended = thread.wait(5000);
    check(ended && host->thread() == mainThread && host->frame()->acknowledged == s && host->frame()->trains.empty(),
          "stop with commands queued: they are taken, the thread ends, the host is handed back");
    delete host;

    qInfo().noquote() << QString("simulation-host: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}

int TsreTests::runSimulationUiSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:simulation-ui] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:simulation-ui] FAIL" << name; }
    };

    QTemporaryDir temp;
    const QString routePath = temp.path() + "/ROUTES/R";
    for (const QString &dir : {"ACTIVITIES", "SERVICES", "TRAFFIC", "PATHS"})
        QDir().mkpath(routePath + "/" + dir);
    check(writeSimis(routePath + "/ACTIVITIES/Simulated.act", activityText)
          && writeSimis(routePath + "/SERVICES/S.srv", QString(serviceText).arg("S", "Main").arg(101))
          && writeSimis(routePath + "/SERVICES/S2.srv", QString(serviceText).arg("S2", "Second").arg(201))
          && writeSimis(routePath + "/TRAFFIC/T.trf", trafficText)
          && writeSimis(routePath + "/PATHS/Main.pat", QString(pathText).arg("Main"))
          && writeSimis(routePath + "/PATHS/Second.pat", QString(pathText).arg("Second"))
          && writeSimis(temp.path() + "/TRAINS/CONSISTS/Two.con", consistText)
          && writeSimis(temp.path() + "/TRAINS/TRAINSET/TEST/loco.eng", QString(vehicleText).arg("loco", "Engine"))
          && writeSimis(temp.path() + "/TRAINS/TRAINSET/TEST/car.wag", QString(vehicleText).arg("car", "Carriage")),
          "write the test route");

    TestTrack::Layout layout;
    layout.end(1, 10);
    layout.track(10, 1, 2, 200);
    layout.end(2, 10);
    layout.end(3, 30);
    layout.track(30, 3, 4, 200);
    layout.end(4, 30);
    // A line through a junction, for a deadlock at the end.
    layout.end(5, 50);
    layout.track(50, 5, 6, 100);
    layout.junction(6, 50, 51, 52);
    layout.track(51, 6, 7, 100);
    layout.end(7, 51);
    layout.track(52, 6, 8, 100);
    layout.end(8, 52);
    TestTrack::platform(layout, 10, 101, 1000.0f, 1200.0f, "Middle");
    TestTrack::platform(layout, 30, 201, 1000.0f, 1200.0f, "Middle 2");
    const QScopedValueRollback<QString> root(Game::root, temp.path());
    const QScopedValueRollback<QString> routeName(Game::route, "R");
    const QScopedValueRollback<TDB *> trackDB(Game::trackDB, &layout.db);
    // The editor places consists on the terrain: none here (height 0).
    TerrainLib terrain;
    const QScopedValueRollback<TerrainLib *> flat(Game::terrainLib, &terrain);
    const QScopedValueRollback<float> hours(Game::timeOfDayHours, 12.0f);
    const QScopedValueRollback<QString> day(Game::timeOfDayDate, "2026-10-15");
    EngLib engines;
    const QScopedValueRollback<EngLib *> engineLibrary(Game::currentEngLib, &engines);
    const int firstAct = ActLib::jestact, firstService = ActLib::jestservice;
    const int firstTraffic = ActLib::jesttraffic, firstPath = ActLib::jestpath;
    const bool routeListed = ActLib::route.contains("R");

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
    Activity *activity = route->activityId.isEmpty() ? nullptr : ActLib::Act.value(route->activityId[0]);
    check(main != nullptr && !main->isBroken() && second != nullptr && !second->isBroken() && activity != nullptr
              && activity->loaded == 1, "the activity loads, its paths on the test lines");
    if (activity == nullptr || activity->loaded != 1) {
        qInfo().noquote() << QString("simulation-ui: %1 passed, %2 failed").arg(passed).arg(failed + 1);
        return 1;
    }

    ActivityTools tools("ActivityTools");
    ActivityEventWindow events(nullptr);
    ActivityTimetableWindow timetable(nullptr);
    QObject::connect(&tools, &ActivityTools::showEvents, &events, &ActivityEventWindow::showEvents);
    QObject::connect(&tools, &ActivityTools::showTimetable, &timetable, &ActivityTimetableWindow::showTimetable);
    tools.show();
    events.show();
    timetable.show();
    tools.routeLoaded(route);
    QComboBox *activities = nullptr;
    for (QComboBox *combo : tools.findChildren<QComboBox *>())
        if (combo->findText(activity->header->name) >= 0)
            activities = combo;
    if (activities) {
        activities->setCurrentIndex(activities->findText(activity->header->name));
        emit activities->textActivated(activities->currentText());
    }
    check(route->getCurrentActivity() == activity, "the activity is chosen");

    QPushButton *play = tools.findChild<QPushButton *>("simulationPlay");
    QPushButton *pause = tools.findChild<QPushButton *>("simulationPause");
    QPushButton *stop = tools.findChild<QPushButton *>("simulationStop");
    QPushButton *runTo = tools.findChild<QPushButton *>("simulationRunTo");
    QComboBox *speed = tools.findChild<QComboBox *>("simulationSpeed");
    QTimeEdit *runToTime = tools.findChild<QTimeEdit *>("simulationRunToTime");
    QTreeWidget *rows = timetable.findChild<QTreeWidget *>("timetableTrains");
    QPushButton *follow = timetable.findChild<QPushButton *>("timetableFollow");
    QPushButton *add = timetable.findChild<QPushButton *>("timetableAdd");
    QListWidget *trainEvents = timetable.findChild<QListWidget *>("timetableEvents");
    check(play && pause && stop && runTo && speed && runToTime && rows && follow && add && trainEvents,
          "the panel and the Timetable window have the simulation's controls");
    if (!(play && pause && stop && runTo && speed && runToTime && rows && follow && add && trainEvents)) {
        qInfo().noquote() << QString("simulation-ui: %1 passed, %2 failed").arg(passed).arg(failed);
        return 1;
    }
    check(play->isEnabled() && !pause->isEnabled() && !stop->isEnabled() && rows->isColumnHidden(2),
          "before: Play only, no state column");

    // An activity edit before: undo keeps it for after the simulation.
    Undo::Clear();
    {
        ActivityEdit edit(activity);
        activity->setDisplayName("Simulated, renamed");
    }
    Undo::StateEnd();

    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const auto frame = [&] { return simulation->frame(); };
    const auto pump = [&] {
        QApplication::processEvents();
        simulation->poll();
    };
    speed->setCurrentIndex(speed->findData(0.0));
    emit speed->activated(speed->currentIndex());
    // Played with the Timetable window closed: opened later, it shows the
    // simulation's states.
    timetable.hide();
    play->click();
    check(simulation->exists() && simulation->shows(activity) && simulation->input()->trains.size() == 2,
          "Play: a simulation of the activity, the player's train and the AI train");
    check(simulation->input()->trains[0].length > 39.0f && simulation->input()->trains[0].vehicles[0].type >= 4,
          "the trains have their vehicles: an engine and a carriage, 40 m");
    check(std::fabs(Game::timeOfDayHours - 8.0f) < 1e-4f, "the world's time is set to the activity's start");
    check(ActivityNotifier::editingLocked() && !activities->isEnabled() && !add->isEnabled()
              && !events.eventProperties->isEnabled() && !timetable.timetableProperties->isEnabled(),
          "activity editing is off: the choice, the windows' editors");
    check(!Undo::UndoLast() && activity->header->name == "Simulated, renamed", "undo leaves the activity's step");

    FramePointer f = waitFor(frame, [&](const Sim::SimulationFrame &x) { return x.time > 8 * 3600 + 200; }, 10000, pump);
    pump();
    check(f && simulation->runs() && pause->isEnabled() && !play->isEnabled() && stop->isEnabled(),
          "it runs as fast as possible: Pause and Stop on");
    pause->click();
    f = waitFor(frame, [&](const Sim::SimulationFrame &x) {
            return x.acknowledged >= simulation->lastCommand(); }, 5000, pump);
    pump();
    check(f && !simulation->runs() && play->isEnabled() && f->mode == Sim::RunMode::Paused, "paused");
    // The panel: the trains' states, and the run's notes on its trains only
    // counted, linked to Check Activity (which lists them).
    QLabel *panelStatus = tools.findChild<QLabel *>("simulationStatus");
    QLabel *notes = tools.findChild<QLabel *>("simulationWarnings");
    check(panelStatus != nullptr && notes != nullptr && !panelStatus->text().contains("driven simply") && notes->isVisible()
              && notes->text().startsWith("2 ") && notes->text().contains("<a href"),
          "the panel counts the run's notes, linked: " + (notes ? notes->text() : QString()));
    if (qEnvironmentVariableIsSet("TSRE_ACTIVITY_UI_SHOTS"))
        tools.grab().save(qEnvironmentVariable("TSRE_ACTIVITY_UI_SHOTS") + "/simulation-panel.png");

    // The Timetable window, opened now: states, the chosen train's events,
    // Follow.
    timetable.show();
    timetable.showTimetable(activity);
    pump();
    check(!rows->isColumnHidden(2) && rows->topLevelItemCount() == 2 && !rows->topLevelItem(0)->text(2).isEmpty(),
          "opened while it runs: each train's state: " + (rows->topLevelItemCount() ? rows->topLevelItem(0)->text(2) : QString()));
    rows->setCurrentItem(rows->topLevelItem(0));
    rows->topLevelItem(0)->setSelected(true);
    pump();
    QStringList listed;
    for (int i = 0; i < trainEvents->count(); ++i)
        listed << trainEvents->item(i)->text();
    // Texts are their ids without translations: the stop by its plan.
    bool arrived = false;
    for (const Sim::TrainEvent &event : simulation->events())
        arrived = arrived || (event.train == simulation->trainOf(true, -1) && event.kind == Sim::TrainEvent::Arrived
                              && simulation->input()->trains[0].stops.at(0).platform == "Middle");
    check(simulation->chosen() == simulation->trainOf(true, -1) && listed.size() >= 3 && arrived,
          "the player's train chosen: its events, the stop at Middle: " + listed.join(" | "));
    check(follow->isEnabled(), "Follow is on offer");
    follow->click();
    check(simulation->followed() == simulation->chosen() && follow->isChecked(), "Follow: the camera follows the chosen train");
    int tile[2];
    float place[3], heading[3];
    check(simulation->trainPlace(simulation->followed(), tile, place, heading) && heading[0] > 0.9f,
          "the followed train's place, heading along the line");
    // The AI train has left at its path's end; the player's stands at it.
    MapFeatures features;
    simulation->getMapFeatures(features, 0, 0);
    check(simulation->frame()->trains[1].state.status == Sim::TrainStatus::Finished
              && features.vehicles.size() == size_t(2 * MapFeatures::VehicleFloats),
          QString("the map has the vehicles of the train on the track (%1)").arg(features.vehicles.size() / MapFeatures::VehicleFloats));
    // The trains as the editor's consists (copies placed from the frames):
    // the player's on the track where the frame has it, the simulation's
    // speed, its ids; the AI train, gone at its path's end, off the track.
    {
        const int player = simulation->trainOf(true, -1);
        Consist *consist = simulation->consistOf(player);
        const Sim::TrainFrame &placed = simulation->frame()->trains[size_t(player)];
        bool where = consist != nullptr && consist->isOnTrack && consist->isSimulated()
                && consist->engItems.size() == int(placed.fronts.size()) && consist->engItems[0].engPointer != nullptr;
        if (where) {
            const float *at = consist->engItems[0].engPointer->getCurrentPositionOnTrack();
            const Sim::TrackPose &a = placed.fronts[0];
            where = at != nullptr && std::fabs(at[0] - a.x) < 0.01f && std::fabs(at[1] - a.y) < 0.01f
                    && std::fabs(at[2] - a.z) < 0.01f && int(at[5]) == a.tileX && int(at[6]) == a.tileZ;
        }
        Consist *gone = simulation->consistOf(1);
        check(where && std::fabs(consist->getTrainSpeed() - placed.state.speed) < 1e-6
                  && simulation->trainOfObject(consist) == player
                  && simulation->trainOfService(simulation->serviceId(player)) == player
                  && (gone == nullptr || !gone->isOnTrack),
              "the trains as consists: the player's placed where the frame has it, at its speed, picked by its id; "
              "the AI train off the track");
        float throttle = -1.0f, brake = -1.0f;
        bool controls = true;
        const auto controlled = [&](float speed, float acceleration, float wantThrottle, float wantBrake) {
            Sim::SimulationController::SoundControls(speed, acceleration, 0.5f, 1.0f, throttle, brake);
            controls = controls && std::fabs(throttle - wantThrottle) < 1e-4f && std::fabs(brake - wantBrake) < 1e-4f;
        };
        controlled(0.0f, 0.0f, 0.0f, 1.0f);      // standing, braked
        controlled(0.0f, 0.25f, 0.65f, 0.0f);    // starting at half its acceleration
        controlled(20.0f, 0.0f, 0.3f, 0.0f);     // holding its speed
        controlled(20.0f, 0.5f, 1.0f, 0.0f);     // full acceleration
        controlled(20.0f, -0.5f, 0.0f, 0.5f);    // braking at half its deceleration
        check(controls, "a train's sounds: throttle and brake from its speed and acceleration");
        // The Consist properties' Show Border: the outlines drawn or not,
        // for every consist.
        PropertiesConsist properties;
        properties.showObj(consist);
        QCheckBox *border = properties.findChild<QCheckBox *>("consistShowBorder");
        const bool shown = border != nullptr && border->isChecked() && Consist::showBorder;
        if (border != nullptr)
            border->setChecked(false);
        const bool hidden = !Consist::showBorder;
        Consist::showBorder = true;
        check(shown && hidden, "the Consist properties' Show Border: the outlines drawn at first, not when unchecked");
    }

    // Run to an earlier time: from the start again.
    runToTime->setTime(QTime(8, 1, 30));
    runTo->click();
    f = waitFor(frame, [&](const Sim::SimulationFrame &x) {
            return x.acknowledged >= simulation->lastCommand() && x.mode == Sim::RunMode::Paused; }, 10000, pump);
    pump();
    check(f && std::fabs(f->time - (8 * 3600 + 90)) < 1e-6 && f->trains[1].state.status == Sim::TrainStatus::Running,
          QString("Run to 08:01:30, before the time shown: reached from the start (%1)").arg(f ? f->time : -1));

    // A view's tick pins the frame it shows (the camera riding a train and
    // the train drawn from the same one): the consists stay where it has
    // them while newer frames come, and go on with the next tick's.
    {
        const auto placedAt = [&](const Sim::SimulationFrame &x, int train) {
            Consist *consist = simulation->consistOf(train);
            if (consist == nullptr || !consist->isOnTrack || consist->engItems.isEmpty()
                    || consist->engItems[0].engPointer == nullptr)
                return false;
            const float *at = consist->engItems[0].engPointer->getCurrentPositionOnTrack();
            const Sim::TrackPose &a = x.trains[size_t(train)].fronts[0];
            return at != nullptr && std::fabs(at[0] - a.x) < 0.01f && std::fabs(at[2] - a.z) < 0.01f
                    && int(at[5]) == a.tileX && int(at[6]) == a.tileZ;
        };
        simulation->pinViewFrame();
        const std::shared_ptr<const Sim::SimulationFrame> pinned = simulation->viewFrame();
        const bool before = placedAt(*pinned, 1);
        runToTime->setTime(QTime(8, 2, 0));
        runTo->click();
        f = waitFor(frame, [&](const Sim::SimulationFrame &x) {
                return x.acknowledged >= simulation->lastCommand() && x.mode == Sim::RunMode::Paused; }, 10000, pump);
        pump();
        const bool moved = f && f->trains[1].state.status == Sim::TrainStatus::Running && !placedAt(*f, 1);
        const bool stayed = simulation->viewFrame() == pinned && placedAt(*pinned, 1);
        simulation->pinViewFrame();
        const bool next = simulation->viewFrame() == f && placedAt(*f, 1);
        simulation->unpinViewFrame();
        check(before && moved && stayed && next,
              "the frame a view pins: its trains stay there as newer frames come, the next tick's moves them on");
        runToTime->setTime(QTime(8, 1, 30));
        runTo->click();
        f = waitFor(frame, [&](const Sim::SimulationFrame &x) {
                return x.acknowledged >= simulation->lastCommand() && x.mode == Sim::RunMode::Paused; }, 10000, pump);
        pump();
    }

    // The world's clock is the simulation's; a run as fast as possible
    // leaves it until it pauses; past midnight, the next day.
    WorldClock *clock = WorldClock::instance();
    clock->tick(0.0);
    check(clock->source() == simulation && std::fabs(clock->hours() - (8 + 1.5 / 60)) < 1e-6
              && clock->date() == "2026-10-15" && clock->rate() == 0.0 && !clock->running()
              && std::fabs(Game::timeOfDayHours - (8 + 1.5 / 60)) < 1e-4,
          QString("the world's clock is the simulation's, paused (%1 %2)").arg(clock->hours()).arg(clock->date()));
    play->click();
    f = waitFor(frame, [&](const Sim::SimulationFrame &x) { return x.time > 8 * 3600 + 1800; }, 10000, pump);
    check(f && std::fabs(clock->hours() - (8 + 1.5 / 60)) < 1e-6 && clock->rate() == 0.0,
          "as fast as possible the world keeps its time");
    runToTime->setTime(QTime(7, 59, 0));
    runTo->click();
    // As fast as possible the run can pass that time before the host takes
    // the command (a day of this activity takes well under a second): the
    // controller then runs to it from the start again, a second command.
    const auto paused = [&](const Sim::SimulationFrame &x) {
        return x.acknowledged >= simulation->lastCommand() && x.mode == Sim::RunMode::Paused; };
    f = waitFor(frame, paused, 20000, pump);
    pump();
    f = waitFor(frame, paused, 20000, pump);
    pump();
    // A day of 0.1 s steps from the start ends a few microseconds off.
    check(f && std::fabs(f->time - (86400 + 7 * 3600 + 59 * 60)) < 1e-4 && std::fabs(clock->hours() - (7 + 59.0 / 60)) < 1e-6
              && clock->date() == "2026-10-16",
          QString("Run to 07:59, before the start: the next day; the world's clock there (%1 %2)").arg(clock->hours()).arg(clock->date()));

    // The clock's controls (the environment window's) drive the simulation.
    clock->setSpeed(5.0);
    pump();
    check(simulation->speed() == 5.0 && clock->speed() == 5.0 && speed->currentData().toDouble() == 5.0,
          "a speed set on the clock is the simulation's, shown in the panel");
    clock->setRunning(true);
    f = waitFor(frame, [&](const Sim::SimulationFrame &x) {
            return x.acknowledged >= simulation->lastCommand() && x.mode == Sim::RunMode::Running; }, 5000, pump);
    pump();
    check(f && simulation->runs() && clock->running() && clock->rate() == 5.0, "Resume on the clock plays the simulation");
    clock->setTime(9.0);
    f = waitFor(frame, [&](const Sim::SimulationFrame &x) {
            return x.acknowledged >= simulation->lastCommand() && x.mode == Sim::RunMode::Running
                   && x.time >= 86400 + 9 * 3600; }, 20000, pump);
    pump();
    check(f && clock->running() && std::fabs(clock->hours() - 9.0) < 0.01 && clock->date() == "2026-10-16",
          QString("the clock set to 09:00: the simulation runs there on the day shown and on after (%1 %2)")
              .arg(clock->hours()).arg(clock->date()));
    clock->setRunning(false);
    f = waitFor(frame, [&](const Sim::SimulationFrame &x) {
            return x.acknowledged >= simulation->lastCommand() && x.mode == Sim::RunMode::Paused; }, 5000, pump);
    pump();
    check(f && !simulation->runs() && !clock->running(), "Stop on the clock pauses the simulation");
    const double lastHours = clock->hours();

    // The consists as a view drawing the trains has them.
    const int ridden = simulation->trainOf(true, -1);
    const bool made = simulation->consistOf(ridden) != nullptr;
    int removing = 0;
    const QMetaObject::Connection removal = QObject::connect(simulation, &Sim::SimulationController::trainsRemoving,
                                                          [&] { removing += simulation->consistOf(ridden) != nullptr ? 1 : 0; });
    stop->click();
    pump();
    QObject::disconnect(removal);
    check(made && removing == 1 && simulation->consistOf(ridden) == nullptr,
          "Stop: the trains' consists go, the editor told while they are there");
    check(!simulation->exists() && !ActivityNotifier::editingLocked() && activities->isEnabled()
              && timetable.timetableProperties->isEnabled() && rows->isColumnHidden(2) && !follow->isChecked() && simulation->followed() == -1,
          "Stop: no simulation, editing on, the state column and Follow gone");
    check(clock->source() == nullptr && std::fabs(Game::timeOfDayHours - lastHours) < 1e-3 && clock->speed() == 5.0
              && Game::timeOfDayDate == "2026-10-16",
          "after Stop the editor's clock goes on from the simulation's time, date and speed");
    clock->setSpeed(1.0);
    check(Undo::UndoLast() && activity->header->name == "Simulated", "after Stop the activity's step undoes");
    Undo::Clear();

    // Simulate (task 04): the activity run as fast as possible, its times
    // written. The file books the player's S to leave Middle at 08:02:40, the
    // AI's S2 Middle 2 at 08:04:20: departures that hold them, rounded up
    // to 30 s.
    QPushButton *simulate = timetable.findChild<QPushButton *>("timetableSimulate");
    QComboBox *roundTo = timetable.findChild<QComboBox *>("timetableRoundTo");
    QComboBox *timesFor = timetable.findChild<QComboBox *>("timetableTimesFor");
    QCheckBox *fromScratch = timetable.findChild<QCheckBox *>("timetableFromScratch");
    QLabel *lastRun = timetable.findChild<QLabel *>("timetableLastRun");
    QTableWidget *stopsTable = timetable.findChild<QTableWidget *>("timetableStops");
    bool oldCalculate = false;
    for (QPushButton *button : timetable.findChildren<QPushButton *>())
        oldCalculate = oldCalculate || button->text() == "Calculate"
                || button->text() == "route.editor.activity.timetable.properties.button.b.calculate";
    check(simulate && roundTo && timesFor && fromScratch && lastRun && stopsTable && !oldCalculate,
          "Simulate's controls in the Timetable window; the old Calculate gone");
    if (simulate && roundTo && timesFor && fromScratch && lastRun && stopsTable) {
        Sim::TimetableCalculation::ForgetAll();
        timetable.showTimetable(activity);
        const auto playerTimes = [&]() -> ActivityTimetable * {
            return activity->playerServiceDefinition != nullptr ? activity->playerServiceDefinition->trafficDefinition
                                                                : nullptr;
        };
        const auto aiTimes = [&]() -> ActivityTimetable * {
            Traffic *pattern = ActivityTrains::Pattern(activity);
            return pattern != nullptr ? pattern->getInstance("S2", 28860) : nullptr;
        };
        const auto times = [&] {
            ActivityTimetable *p = playerTimes(), *a = aiTimes();
            return p != nullptr && a != nullptr && !p->arrivalTime.isEmpty() && !a->arrivalTime.isEmpty()
                ? std::vector<int>{p->arrivalTime[0], p->departTime[0], a->arrivalTime[0], a->departTime[0]}
                : std::vector<int>{};
        };
        const auto text = [](const std::vector<int> &t) {
            QStringList parts;
            for (int v : t)
                parts << QString::number(v);
            return parts.join(" ");
        };
        const auto onStep = [](int t) { return t % 30 == 0; };
        check(simulate->isEnabled() && timesFor->isEnabled() && timesFor->currentIndex() == 0
                  && roundTo->currentData().toInt() == 30 && times().size() == 4,
              "Simulate on offer: all trains, rounded to 30 s");
        simulate->click();
        const std::vector<int> first = times();
        check(first.size() == 4 && onStep(first[0]) && first[0] > 28800 && first[0] < 28980 && first[1] == 28980
                  && onStep(first[2]) && first[2] > 28860 && first[3] == 29070 && lastRun->isVisible(),
              QString("both trains' times written: the file's departures hold them, rounded up (%1)").arg(text(first)));
        simulate->click();
        check(times() == first && lastRun->text().contains(qtTrId("route.editor.activity.timetable.window.summary.unchanged")),
              QString("simulated again: nothing changed (%1)").arg(lastRun->text()));
        fromScratch->setChecked(true);
        simulate->click();
        fromScratch->setChecked(false);
        const std::vector<int> scratch = times();
        check(scratch.size() == 4 && scratch[0] == first[0] && onStep(scratch[1]) && scratch[1] < 28980
                  && scratch[1] >= scratch[0] + 20 && onStep(scratch[3]) && scratch[3] < 29070 && scratch[3] >= scratch[2] + 20,
              QString("From scratch: every departure calculated again, after boarding (%1)").arg(text(scratch)));
        check(Undo::UndoLast() && times() == first, "one undo brings the times before back");
        simulate->click();
        check(times() == first, "the file's departures, back, hold the trains again");

        // The user sets the player's departure two minutes later in the
        // stops' table: kept on the next Simulate, the change shown.
        for (int i = 0; i < rows->topLevelItemCount(); ++i)
            if (rows->topLevelItem(i)->text(1).contains("(player)")
                    || rows->topLevelItem(i)->text(1).contains(activity->playerServiceDefinition->name + " ")) {
                rows->setCurrentItem(rows->topLevelItem(i));
                rows->topLevelItem(i)->setSelected(true);
            }
        QApplication::processEvents();
        const QString later = QTime::fromMSecsSinceStartOfDay((first[1] + 120) * 1000).toString("HH:mm:ss");
        if (stopsTable->rowCount() > 0 && stopsTable->item(0, 2) != nullptr)
            stopsTable->item(0, 2)->setText(later);
        QApplication::processEvents();
        check(playerTimes() != nullptr && playerTimes()->departTime[0] == first[1] + 120 && stopsTable->item(0, 2) != nullptr
                  && stopsTable->item(0, 2)->font().bold(),
              QString("a departure set in the table: %1, shown in bold").arg(later));
        simulate->click();
        QApplication::processEvents();
        const std::vector<int> kept = times();
        check(kept.size() == 4 && kept[1] == first[1] + 120 && kept[0] == first[0] && kept[3] == first[3]
                  && stopsTable->item(0, 3) != nullptr && stopsTable->item(0, 3)->text() == "0:00 / +2:00"
                  && stopsTable->item(0, 2)->font().bold(),
              QString("Simulate keeps it, the change shown: \"%1\"").arg(stopsTable->item(0, 3) != nullptr ? stopsTable->item(0, 3)->text() : QString()));
        // TSRE_UI_SHOTS: the window as it is now, for a look at its layout.
        if (qEnvironmentVariableIsSet("TSRE_UI_SHOTS"))
            timetable.grab().save(qEnvironmentVariable("TSRE_UI_SHOTS") + "/timetable-simulated.png");

        // Another activity naming the same traffic pattern: the player's
        // times alone are written.
        QString other = QString(activityText).replace("\"Simulated\"", "\"Other\"");
        const int otherId = writeSimis(routePath + "/ACTIVITIES/Other.act", other)
                ? ActLib::AddAct(routePath + "/ACTIVITIES", "Other.act") : -1;
        timetable.showTimetable(activity);
        const bool shared = !ActivityTrains::OtherUsers(activity).isEmpty();
        check(shared && !timesFor->isEnabled() && timesFor->currentIndex() == 1,
              "a traffic pattern shared with another activity: the player's times alone");
        if (ActivityTimetable *p = playerTimes())
            p->setDepart(0, first[1] + 300);
        simulate->click();
        const std::vector<int> alone = times();
        check(alone.size() == 4 && alone[1] == first[1] + 300 && alone[2] == kept[2] && alone[3] == kept[3]
                  && lastRun->text().contains("Other"),
              QString("Simulate writes the player's times, the AI train's stay (%1)").arg(lastRun->text()));
        if (otherId >= 0)
            ActLib::Act[otherId] = nullptr;
        timetable.showTimetable(activity);
        check(timesFor->isEnabled() && timesFor->currentIndex() == 0, "the pattern its own again: all trains, as chosen");

        // A timetable MSAE never calculated (every stop at the train's
        // start): its times are no bookings, Simulate's shown as new.
        Sim::TimetableCalculation::ForgetAll();
        if (ActivityTimetable *a = aiTimes())
            for (int r = 0; r < a->arrivalTime.size() && r < a->departTime.size(); ++r) {
                a->arrivalTime[r] = 28860;
                a->departTime[r] = 28860;
            }
        simulate->click();
        const Sim::TimetableTrain *placeholder = Sim::TimetableCalculation::LastTrain(activity, false, "S2", 28860);
        check(placeholder != nullptr && !placeholder->stops.empty() && placeholder->stops[0].reached
                  && placeholder->stops[0].arrivalBefore < 0 && placeholder->stops[0].departureBefore < 0
                  && !placeholder->stops[0].kept,
              "an uncalculated timetable (every stop at the train's start): Simulate's times shown as new, none kept");
        Undo::Clear();
    }

    // Where trains stand at their start (task simulation 06, decided
    // 2026-10-21): their rear at the path's start, the whole train ahead, as
    // Open Rails places them; the editor shows the player's train where the
    // simulation starts it.
    {
        QString error;
        check(simulation->runTo(activity, 8 * 3600 + 1, &error), "a run to a second after the start: " + error);
        f = waitFor(frame, [&](const Sim::SimulationFrame &x) {
                return x.acknowledged >= simulation->lastCommand() && x.mode == Sim::RunMode::Paused; }, 10000, pump);
        pump();
        const Sim::TrainPlan &first = simulation->input()->trains[0];
        MapFeatures shown, simulated;
        if (Service *service = activity->playerService())
            service->getMapFeatures(shown, 0, 0);
        if (Consist *train = simulation->consistOf(simulation->trainOf(true, -1)))
            train->getMapFeatures(simulated, 0, 0);
        const bool both = shown.vehicles.size() >= MapFeatures::VehicleFloats && simulated.vehicles.size() >= MapFeatures::VehicleFloats;
        // The test track's 10 m pieces lie side by side, not end to end: a
        // place on the track is within one piece of another a metre along it
        // (the old placement, the first car across the start, is 30 m off).
        const float apart = both ? std::max(std::fabs(shown.vehicles[0] - simulated.vehicles[0]),
                                            std::fabs(shown.vehicles[1] - simulated.vehicles[1])) : -1.0f;
        check(f && first.player && std::fabs(first.startFront - (first.route.pathStart + first.length)) < 1e-6
                  && both && apart <= 11.0f,
              QString("the player's train starts with its rear at its path's start; the editor shows it there (%1 m apart "
                      "on this track after a second)").arg(apart));
        stop->click();
        pump();
    }

    // Activity events (task simulation 06): a time event, a location event
    // the player's train passes (both with messages), one on the other line
    // and one not active. Run to 08:03: the Event window shows when each
    // fired, the Timetable window lists them with the player's train, the
    // map the latest message beside it; an edit puts the results out of
    // date, Simulate brings them back.
    {
        const auto messageOf = [](ActivityEvent *event, const QString &text) {
            ActivityEvent::Outcome *outcome = event->outcomes[event->newOutcome()];
            outcome->setToNewType(ActivityEvent::Outcome::TypeDisplayMessage);
            outcome->setMessage(text);
        };
        const auto placeAt = [&](ActivityEvent *event, int track, float distance) {
            float at[7];
            layout.db.getDrawPositionOnTrNode(at, track, distance);
            event->setLocation(int(at[5]), int(at[6]), at[0], at[2]);
            event->setLocationRadius(10);
        };
        ActivityEvent *half = activity->newEvent(ActivityEvent::CategoryTime);
        half->setName("Half a minute");
        half->setTime(30);
        messageOf(half, "Thirty seconds in");
        ActivityEvent *marker = activity->newEvent(ActivityEvent::CategoryLocation);
        marker->setName("Marker");
        placeAt(marker, 10, 700.0f);
        messageOf(marker, "Passing the marker");
        ActivityEvent *elsewhere = activity->newEvent(ActivityEvent::CategoryLocation);
        elsewhere->setName("Elsewhere");
        placeAt(elsewhere, 30, 700.0f);
        ActivityEvent *sleeping = activity->newEvent(ActivityEvent::CategoryTime);
        sleeping->setName("Sleeping");
        sleeping->setActivationLevel(0);
        events.showEvents(activity);
        runToTime->setTime(QTime(8, 3, 0));
        runTo->click();
        f = waitFor(frame, [&](const Sim::SimulationFrame &x) {
                return x.acknowledged >= simulation->lastCommand() && x.mode == Sim::RunMode::Paused; }, 20000, pump);
        pump();
        bool current = false;
        const Sim::EventResults *results = Sim::EventResultStore::instance()->of(activity, &current);
        const auto resultOf = [&](ActivityEvent *event) -> const Sim::EventResult * {
            const int index = activity->event.indexOf(event);
            return results != nullptr && index >= 0 && size_t(index) < results->events.size() ? &results->events[size_t(index)] : nullptr;
        };
        check(f && results != nullptr && current && resultOf(half) && resultOf(half)->state.fired == 1
                  && std::fabs(resultOf(half)->state.firedAt - (8 * 3600 + 30)) < 1e-6
                  && resultOf(marker) && resultOf(marker)->state.fired == 1 && resultOf(marker)->by.contains("S")
                  && resultOf(elsewhere) && resultOf(elsewhere)->state.fired == 0
                  && resultOf(sleeping) && resultOf(sleeping)->state.fired == 0,
              "Run to 08:03: the time and passed location events fired, the others not");
        const auto rowOf = [&](const char *list, const QString &name) -> QListWidgetItem * {
            QListWidget *shown = events.findChild<QListWidget *>(list);
            for (int i = 0; shown != nullptr && i < shown->count(); ++i)
                if (shown->item(i)->text().endsWith(name))
                    return shown->item(i);
            return nullptr;
        };
        QLabel *eventRun = events.findChild<QLabel *>("eventLastRun");
        QListWidgetItem *markerRow = rowOf("eventLocationList", "Marker");
        QListWidgetItem *elsewhereRow = rowOf("eventLocationList", "Elsewhere");
        check(markerRow != nullptr && markerRow->text().startsWith("08:00:") && elsewhereRow != nullptr
                  && elsewhereRow->text() == "Elsewhere" && elsewhereRow->data(Qt::ForegroundRole).isValid()
                  && eventRun != nullptr && !eventRun->isHidden() && eventRun->text().contains("2 of 4"),
              QString("the Event window: when each fired, grey those that did not, the run under the lists (%1; %2)")
                  .arg(markerRow != nullptr ? markerRow->text() : QString("-"), eventRun != nullptr ? eventRun->text() : QString("-")));
        // The player's train is the first row.
        if (rows->topLevelItemCount() > 0) {
            rows->setCurrentItem(rows->topLevelItem(0));
            rows->topLevelItem(0)->setSelected(true);
        }
        pump();
        QStringList listed;
        for (int i = 0; i < trainEvents->count(); ++i)
            listed << trainEvents->item(i)->text();
        check(simulation->chosen() == simulation->trainOf(true, -1) && listed.join("\n").contains("Passing the marker")
                  && listed.join("\n").contains("Thirty seconds in"),
              "the Timetable window lists the activity's events with the player's train: " + listed.filter("event").join(" | "));
        // Why the others did not fire (section 7), in the tooltip and in
        // Check Activity's last run.
        check(elsewhereRow != nullptr && elsewhereRow->toolTip().contains("does not pass its point")
                  && rowOf("eventTimeList", "Sleeping") != nullptr
                  && rowOf("eventTimeList", "Sleeping")->toolTip().contains("Never active"),
              QString("why not fired: \"%1\"").arg(elsewhereRow != nullptr ? elsewhereRow->toolTip() : QString("-")));
        {
            ActivityCheckWindow checker(nullptr);
            checker.check(activity);
            QTreeWidget *found = checker.findChild<QTreeWidget *>();
            QStringList run;
            for (int i = 0; found != nullptr && i < found->topLevelItemCount(); ++i)
                if (found->topLevelItem(i)->text(0) == "Last run")
                    run << found->topLevelItem(i)->text(1);
            // (And the notes on the trains made: driven simply, no mass.)
            check(run.size() == 5 && run.filter("did not fire").size() == 2 && run.filter("up to 08:03:00").size() == 1
                      && run.filter("driven simply").size() == 2,
                  "Check Activity's last run: the trains' notes, the run up to 08:03, the two events not fired: "
                      + run.join(" | "));
        }
        std::vector<MapLabel> labels;
        simulation->messageLabels(labels);
        check(labels.size() == 1 && labels[0].text.contains("Marker") && labels[0].kind == MapLabelKind::Event,
              QString("the map shows the latest message beside the player's train (%1)")
                  .arg(labels.empty() ? QString("none") : labels[0].text));
        stop->click();
        pump();
        {
            ActivityEdit edit(activity);
            half->setTime(31);
        }
        Undo::StateEnd();
        Sim::EventResultStore::instance()->of(activity, &current);
        markerRow = rowOf("eventLocationList", "Marker");
        check(!current && markerRow != nullptr && markerRow->font().italic() && eventRun->text().contains("edited"),
              "an edit puts the last run's results out of date: shown so");
        if (simulate != nullptr)
            simulate->click();
        QApplication::processEvents();
        results = Sim::EventResultStore::instance()->of(activity, &current);
        markerRow = rowOf("eventLocationList", "Marker");
        check(current && results != nullptr && results->complete && markerRow != nullptr && !markerRow->font().italic()
                  && resultOf(half) && std::fabs(resultOf(half)->state.firedAt - (8 * 3600 + 31)) < 1e-6,
              "Simulate records the events' results for the activity as it is");
        for (ActivityEvent *added : {half, marker, elsewhere, sleeping})
            activity->deleteEvent(added);
        events.showEvents(activity);
        Undo::Clear();
    }

    // A deadlock: the two trains either side of a junction, each for the
    // other's side, both on the track from 08:00. The user is told which
    // trains; the run goes on. (Each reverses just past the junction,
    // short of where the other stands 10 m from it: a train standing on its
    // path's part would be coupled to, task 07, as Open Rails' AI does.)
    if (main != nullptr && second != nullptr) {
        PathTrack::Result made = PathTrack::startAt(&layout.db, {50, 500.0f});
        made = PathTrack::reverseAt(&layout.db, made.nodes, {51, 5.0f});
        main->apply(PathTrack::endAt(&layout.db, made.nodes, {50, 200.0f}).nodes);
        made = PathTrack::turnStart(&layout.db, PathTrack::startAt(&layout.db, {51, 450.0f}).nodes);
        made = PathTrack::reverseAt(&layout.db, made.nodes, {50, 995.0f});
        second->apply(PathTrack::endAt(&layout.db, made.nodes, {52, 900.0f}).nodes);
    }
    for (int id = firstTraffic; id < ActLib::jesttraffic; ++id)
        if (Traffic *traffic = ActLib::Traffics.value(id))
            traffic->setInstanceTime(traffic->getInstance("S2", 28860), 28800);
    clock->setSpeed(0.0);
    play->click();
    f = waitFor(frame, [&](const Sim::SimulationFrame &x) {
            return x.trains.size() == 2 && x.trains[0].state.deadlocked && x.trains[1].state.deadlocked; }, 20000, pump);
    pump();
    QMessageBox *told = tools.findChild<QMessageBox *>();
    QLabel *status = tools.findChild<QLabel *>("simulationStatus");
    check(f && told != nullptr && told->isVisible() && !told->isModal()
              && told->text().contains(simulation->trainName(0)) && told->text().contains(simulation->trainName(1))
              && simulation->runs() && status != nullptr && status->text().contains(simulation->trainName(1)),
          QString("a deadlock: a message names both trains, the status line too, the run goes on (%1)")
              .arg(told != nullptr ? told->text() : QString("no message")));
    check(f && simulation->stateText(0).contains(simulation->trainName(1))
              && simulation->stateText(1).contains(simulation->trainName(0)),
          QString("each waits for the other: \"%1\", \"%2\"").arg(simulation->stateText(0), simulation->stateText(1)));
    stop->click();
    pump();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(tools.findChild<QMessageBox *>() == nullptr, "Stop closes the message");
    clock->setSpeed(1.0);
    Undo::Clear();
    if (simulate != nullptr && lastRun != nullptr) {
        // Simulate where the trains end in a deadlock: it says which did not
        // finish and why.
        simulate->click();
        QApplication::processEvents();
        check(lastRun->text().count('\n') >= 3 && lastRun->text().contains("S2"),
              QString("Simulate in a deadlock: the trains that did not finish named (%1)").arg(lastRun->text()));
        Undo::Clear();
    }

    // Task 07: a loose consist ahead of the player's train on its line. The
    // simulation draws it as a standing train in place of the activity's
    // object; the player's train takes it, its consist made again with the
    // cars; the Timetable window lists the coupling; Stop shows the
    // activity's object again.
    if (main != nullptr && second != nullptr)
        for (auto [path, track] : {std::make_pair(main, 10), std::make_pair(second, 30)}) {
            PathTrack::Result made = PathTrack::startAt(&layout.db, {track, 100.0f});
            path->apply(PathTrack::endAt(&layout.db, made.nodes, {track, 1900.0f}).nodes);
        }
    activity->editorConListSelected = temp.path() + "/TRAINS/CONSISTS/Two.con";
    float loosePlace[2] = {10.0f, 1500.0f};
    const int objectsBefore = activity->activityObjects.size();
    activity->newLooseConsist(loosePlace);
    ActivityObject *loose = activity->activityObjects.size() > objectsBefore ? activity->activityObjects.back() : nullptr;
    check(loose != nullptr && loose->con != nullptr && activity->showsLooseConsists(), "a loose consist on the player's line");
    Undo::Clear();
    clock->setSpeed(0.0);
    play->click();
    f = waitFor(frame, [&](const Sim::SimulationFrame &x) { return x.standing != nullptr && !x.trains.empty(); }, 10000, pump);
    pump();
    Consist *standingCopy = simulation->standingConsist(0);
    check(f && f->standing->size() == 1 && (*f->standing)[0].vehicles.size() == 2 && (*f->standing)[0].fronts.size() == 2
              && standingCopy != nullptr && standingCopy->isOnTrack && standingCopy->engItems.size() == 2
              && standingCopy->engItems[0].engPointer != nullptr && !activity->showsLooseConsists(),
          "simulated: the loose consist a standing train, drawn in place of the activity's object");
    f = waitFor(frame, [&](const Sim::SimulationFrame &x) {
            return !x.trains.empty() && x.trains[0].vehicles != nullptr && x.trains[0].vehicles->size() == 4; }, 20000, pump);
    pump();
    Consist *taken = simulation->consistOf(simulation->trainOf(true, -1));
    check(f && f->trains[0].carsVersion > 0 && (*f->standing)[0].gone && simulation->standingConsist(0) == nullptr
              && taken != nullptr && taken->engItems.size() == 4 && taken->engItems[3].engPointer != nullptr,
          QString("the player's train takes it: its consist made again with its %1 vehicles, the standing train gone")
              .arg(taken != nullptr ? taken->engItems.size() : 0));
    rows->setCurrentItem(rows->topLevelItem(0));
    rows->topLevelItem(0)->setSelected(true);
    pump();
    QStringList coupledListed;
    for (int i = 0; i < trainEvents->count(); ++i)
        coupledListed << trainEvents->item(i)->text();
    check(coupledListed.filter("coupled to 2 cars").size() == 1,
          "the Timetable window lists the coupling: " + coupledListed.join(" | "));
    stop->click();
    pump();
    clock->setSpeed(1.0);
    check(!simulation->exists() && activity->showsLooseConsists(), "Stop: the activity's loose consist drawn again");
    if (loose != nullptr) {
        activity->activityObjects.removeOne(loose);
        delete loose;
    }
    Undo::Clear();

    for (QWidget *window : QList<QWidget *>{&tools, &events, &timetable})
        window->hide();
    for (int id = firstAct; id < ActLib::jestact; ++id) ActLib::Act[id] = nullptr;
    for (int id = firstService; id < ActLib::jestservice; ++id) ActLib::Services[id] = nullptr;
    for (int id = firstTraffic; id < ActLib::jesttraffic; ++id) ActLib::Traffics[id] = nullptr;
    for (int id = firstPath; id < ActLib::jestpath; ++id) ActLib::Paths[id] = nullptr;
    if (!routeListed)
        ActLib::route.remove("R");

    qInfo().noquote() << QString("simulation-ui: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
