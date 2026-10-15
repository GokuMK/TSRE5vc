#include <tsre/tests/SimulationTestSuite.h>

#include <QApplication>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <QThread>
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
#include <routeEditor/activity/ActivityEventWindow.h>
#include <routeEditor/activity/ActivityTimetableProperties.h>
#include <routeEditor/activity/ActivityTimetableWindow.h>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/map/MapFeatures.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/SimulationHost.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/PathTrack.h>

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

// A route of one 2 km line (tests/TrackLayoutSupport), its platform
// "Middle" from 1000 to 1200 m; the path Main from 100 to 1900 m; the
// service S of an engine and a carriage stopping there; the activity
// starting at 08:00 with the player's S and an AI S at 08:01.
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
			Service_Definition ( S 28860
				UiD ( 1 )
			)
		)
	)
)
)";

const char *serviceText = R"(SIMISA@@@@@@@@@@JINX0v0t______

Service_Definition (
	Serial ( 1 )
	Name ( "S" )
	Train_Config ( Two )
	PathID ( "Main" )
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
		PlatformStartID ( 101 )
	)
)
)";

const char *trafficText = R"(SIMISA@@@@@@@@@@JINX0f0t______

Traffic_Definition ( T
	Serial ( 1 )
	Service_Definition ( S 28860
		ArrivalTime ( 29000 )
		DepartTime ( 29060 )
		SkipCount ( 0 )
		DistanceDownPath ( 900 )
		PlatformStartID ( 101 )
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
	TrPathName ( Main )
	Name ( "Main" )
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
        check(ordered, "events come in order");
        check(same, QString("the thread's run gives the synchronous core's events (%1 and %2)")
                        .arg(events.size()).arg(expected.size()));
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
          && writeSimis(routePath + "/SERVICES/S.srv", serviceText)
          && writeSimis(routePath + "/TRAFFIC/T.trf", trafficText)
          && writeSimis(routePath + "/PATHS/Main.pat", pathText)
          && writeSimis(temp.path() + "/TRAINS/CONSISTS/Two.con", consistText)
          && writeSimis(temp.path() + "/TRAINS/TRAINSET/TEST/loco.eng", QString(vehicleText).arg("loco", "Engine"))
          && writeSimis(temp.path() + "/TRAINS/TRAINSET/TEST/car.wag", QString(vehicleText).arg("car", "Carriage")),
          "write the test route");

    TestTrack::Layout layout;
    layout.end(1, 10);
    layout.track(10, 1, 2, 200);
    layout.end(2, 10);
    TestTrack::platform(layout, 10, 101, 1000.0f, 1200.0f, "Middle");
    const QScopedValueRollback<QString> root(Game::root, temp.path());
    const QScopedValueRollback<QString> routeName(Game::route, "R");
    const QScopedValueRollback<TDB *> trackDB(Game::trackDB, &layout.db);
    const QScopedValueRollback<float> hours(Game::timeOfDayHours, 12.0f);
    EngLib engines;
    const QScopedValueRollback<EngLib *> engineLibrary(Game::currentEngLib, &engines);
    const int firstAct = ActLib::jestact, firstService = ActLib::jestservice;
    const int firstTraffic = ActLib::jesttraffic, firstPath = ActLib::jestpath;
    const bool routeListed = ActLib::route.contains("R");

    Route *route = new Route();
    route->loadPaths();
    Path *main = ActLib::GetPathByName("Main", routePath);
    if (main != nullptr) {
        PathTrack::Result made = PathTrack::startAt(&layout.db, {10, 100.0f});
        made = PathTrack::endAt(&layout.db, made.nodes, {10, 1900.0f});
        main->apply(made.nodes);
    }
    route->loadServices();
    route->loadTraffic();
    route->loadActivities();
    Activity *activity = route->activityId.isEmpty() ? nullptr : ActLib::Act.value(route->activityId[0]);
    check(main != nullptr && !main->isBroken() && activity != nullptr && activity->loaded == 1,
          "the activity loads, its path on the test line");
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

    // The Timetable window: states, the chosen train's events, Follow.
    check(!rows->isColumnHidden(2) && rows->topLevelItemCount() == 2 && !rows->topLevelItem(0)->text(2).isEmpty(),
          "each train's state: " + (rows->topLevelItemCount() ? rows->topLevelItem(0)->text(2) : QString()));
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

    // Run to an earlier time: from the start again.
    runToTime->setTime(QTime(8, 1, 30));
    runTo->click();
    f = waitFor(frame, [&](const Sim::SimulationFrame &x) {
            return x.acknowledged >= simulation->lastCommand() && x.mode == Sim::RunMode::Paused; }, 10000, pump);
    pump();
    check(f && std::fabs(f->time - (8 * 3600 + 90)) < 1e-6 && f->trains[1].state.status == Sim::TrainStatus::Running,
          QString("Run to 08:01:30, before the time shown: reached from the start (%1)").arg(f ? f->time : -1));

    stop->click();
    pump();
    check(!simulation->exists() && !ActivityNotifier::editingLocked() && activities->isEnabled()
              && timetable.timetableProperties->isEnabled() && rows->isColumnHidden(2) && !follow->isChecked() && simulation->followed() == -1,
          "Stop: no simulation, editing on, the state column and Follow gone");
    check(Undo::UndoLast() && activity->header->name == "Simulated", "after Stop the activity's step undoes");
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
