// Task explore 02: runs started from the Explore window; task explore 04:
// walking runs.

#include "ExploreStartTestSuite.h"
#include "TokenTestSupport.h"

#include <explore/ExploreMenu.h>
#include <explore/ExplorePanel.h>
#include <explore/ExploreRun.h>
#include <explore/ExploreWindow.h>
#include <routeEditor/RouteEditorGLWidget.h>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>
#include <tsre/characters/Cast.h>
#include <tsre/characters/Character.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/tdb/StationPlatforms.h>
#include <tsre/tdb/TDB.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/Consist.h>

#include <QApplication>
#include <QDir>
#include <QKeyEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {

bool settleUntil(const std::function<bool()> &done, int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        if (done())
            return true;
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return done();
}

// How far a place (the editor's tile and the place in it) is from the
// nearest platform track of a station (metres, flat), and how high over
// that track's rail; a large distance when none.
void fromPlatform(const QString &station, const int *tile, const float *feet, double &away, double &above) {
    away = 1e9;
    above = 0.0;
    if (Game::trackDB == nullptr)
        return;
    const double x = double(tile[0]) * 2048.0 + feet[0], z = double(tile[1]) * 2048.0 + feet[2];
    for (StationPlatforms::Platform &p : StationPlatforms::read(*Game::trackDB)) {
        if (p.station.compare(station, Qt::CaseInsensitive) != 0)
            continue;
        StationPlatforms::sample(*Game::trackDB, p, 2.0);
        for (size_t i = 0; i + 2 < p.points.size(); i += 3) {
            // MSTS's z north; the editor's south.
            const double d = std::hypot(p.points[i] - x, -p.points[i + 2] - z);
            if (d < away) {
                away = d;
                above = double(feet[1]) - (p.points[i + 1] + 0.325);
            }
        }
    }
}

}

int TsreTests::runExploreStartSuite(bool verbose) {
    TokenTest::Suite test{"[tests:explore-start]", verbose};
    const QString root = QDir::cleanPath(Game::root);
    const QString route = Game::route.trimmed();
    if (route.isEmpty() || !QFileInfo(root + "/ROUTES/" + route).isDir()) {
        qInfo().noquote() << "[tests:explore-start] needs --game-root and --route (EUROPE1 or PRUDNIK): nothing checked";
        return test.finish();
    }
    SettingsManager &settings = SettingsManager::instance();
    QTemporaryDir temporary;
    Explore::Panel::setRememberedFile(temporary.filePath("explore.json"));
    settings.setSessionValue("core.explore.resolution", "1024x768");
    settings.setSessionValue("core.explore.fillScreen", false);
    Explore::Window *window = new Explore::Window(route);
    bool exited = false;
    window->setExitHandler([&exited] { exited = true; });
    window->open();
    RouteEditorGLWidget *view = window->view();
    settleUntil([window] { return window->menu() != nullptr && window->menu()->panel() != nullptr; }, 10000);
    Explore::Menu *menu = window->menu();
    Explore::Run *run = window->run();
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    test.check(menu != nullptr && menu->usable() && menu->panel() != nullptr, "the menu and the Explore window loaded");
    if (menu == nullptr || menu->panel() == nullptr) {
        delete window;
        Explore::Panel::setRememberedFile(QString());
        return test.finish();
    }
    menu->open(Explore::Menu::Page::Explore);
    Explore::Panel *panel = menu->panel();
    test.check(panel->chooseRoute(route), "the route chosen: " + route);

    // A run started through the model: driven from its start, the camera
    // riding it; then the game menu's Main menu back.
    const auto runOnce = [&](const QString &label, const std::function<void()> &check) {
        QElapsedTimer timer;
        timer.start();
        test.check(panel->ready() && panel->go(), label + ": Explore");
        settleUntil([run] { return run->state() == Explore::Run::State::Loading; }, 2000);
        test.check(!menu->shown(), label + ": the menu hidden, the loading screen shown");
        settleUntil([run] { return run->state() != Explore::Run::State::Loading; }, 300000);
        test.check(run->state() == Explore::Run::State::Running && view->showsRoute(),
                   QString("%1: the route loaded, the run on (%2 s)").arg(label).arg(timer.elapsed() / 1000.0, 0, 'f', 1));
        if (run->state() != Explore::Run::State::Running)
            return;
        // The run's time every 30 s until its train is driven (5 minutes at
        // most: a large timetable's first frame on a slow machine).
        for (int waited = 0; waited < 300000; waited += 30000) {
            if (settleUntil([&] { return run->onTrack(); }, 30000))
                break;
            const std::shared_ptr<const Sim::SimulationFrame> now = simulation->frame();
            qInfo().noquote() << QString("[tests:explore-start] %1: waiting, the run at %2 s")
                                         .arg(label).arg(now != nullptr ? now->time : -1.0, 0, 'f', 1);
        }
        const int driven = simulation->driven();
        const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->frame();
        qInfo().noquote() << QString("[tests:explore-start] %1: the run at %2 s, %3, the player's train %4, driven %5")
                                     .arg(label).arg(frame != nullptr ? frame->time : -1.0, 0, 'f', 1)
                                     .arg(simulation->runs() ? "running" : "not running")
                                     .arg(simulation->trainOf(true, -1)).arg(driven);
        test.check(driven >= 0 && simulation->trainOf(true, -1) == driven,
                   label + ": the player's train driven");
        test.check(driven >= 0 && view->riddenObject() == simulation->consistOf(driven),
                   label + ": the camera riding it");
        test.check(run->onTrack(), label + ": the loading screen gone with the train on the track");
        check();
        test.check(run->showGameMenu() && run->gameMenuShown(), label + ": Esc's game menu");
        test.check(run->click("main-menu"), label + ": Main menu");
        settleUntil([&] { return run->state() == Explore::Run::State::None && menu->shown(); }, 10000);
        test.check(run->state() == Explore::Run::State::None && menu->shown() && !view->showsRoute()
                           && !simulation->exists(),
                   label + ": back to the menu, the run stopped, the route left");
    };

    // A walking run (task explore 04): on foot on a platform of the station,
    // no train driven, the run playing from its start; Esc's game menu in
    // one press, Resume, then Main menu back.
    const auto walkOnce = [&](const QString &label, const std::function<void()> &check) {
        QElapsedTimer timer;
        timer.start();
        const Explore::RunChoice chosen = panel->choice();
        test.check(panel->ready() && panel->go(), label + ": Explore on foot at " + chosen.station);
        settleUntil([run] { return run->state() == Explore::Run::State::Loading; }, 2000);
        settleUntil([run] { return run->state() != Explore::Run::State::Loading; }, 300000);
        test.check(run->state() == Explore::Run::State::Running && view->showsRoute(),
                   QString("%1: the route loaded, the run on (%2 s)").arg(label).arg(timer.elapsed() / 1000.0, 0, 'f', 1));
        if (run->state() != Explore::Run::State::Running) {
            settleUntil([&] { return menu->shown(); }, 2000);
            return;
        }
        for (int waited = 0; waited < 300000; waited += 30000) {
            if (settleUntil([&] { return run->onTrack() || run->state() != Explore::Run::State::Running; }, 30000))
                break;
            const std::shared_ptr<const Sim::SimulationFrame> now = simulation->frame();
            qInfo().noquote() << QString("[tests:explore-start] %1: waiting, the run at %2 s")
                                         .arg(label).arg(now != nullptr ? now->time : -1.0, 0, 'f', 1);
        }
        test.check(run->onTrack(), QString("%1: on foot, the loading screen gone (%2 s)")
                                           .arg(label).arg(timer.elapsed() / 1000.0, 0, 'f', 1));
        if (!run->onTrack()) {
            if (run->state() == Explore::Run::State::Running && run->showGameMenu())
                run->click("main-menu");
            settleUntil([&] { return run->state() == Explore::Run::State::None && menu->shown(); }, 10000);
            return;
        }
        Characters::Character *walker = Characters::Cast::instance().player();
        const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->frame();
        test.check(walker != nullptr && view->riddenObject() == walker && simulation->driven() < 0,
                   label + ": the character the camera's, no train driven");
        if (walker != nullptr) {
            double away = 0.0, above = 0.0;
            fromPlatform(chosen.station, walker->tile(), walker->feet(), away, above);
            test.check(away < 6.0 && above > 0.1 && above < 1.9,
                       QString("%1: on a platform of %2 (%3 m from its track, %4 m over the rail)")
                               .arg(label, chosen.station).arg(away, 0, 'f', 1).arg(above, 0, 'f', 2));
        }
        qInfo().noquote() << QString("[tests:explore-start] %1: the run at %2 s, %3")
                                     .arg(label).arg(frame != nullptr ? frame->time : -1.0, 0, 'f', 1)
                                     .arg(simulation->runs() ? "running" : "not running");
        test.check(simulation->runs(), label + ": the run plays");
        check();
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(view, &esc);
        settleUntil([run] { return run->gameMenuShown(); }, 2000);
        test.check(run->gameMenuShown(), label + ": Esc's game menu in one press");
        // Enter at once: Resume (live, 2026-11-21: the pointer resting in
        // the view's middle chose Exit, and Enter quit).
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(view, &enter);
        QKeyEvent enterUp(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(view, &enterUp);
        settleUntil([run] { return !run->gameMenuShown(); }, 2000);
        test.check(!run->gameMenuShown() && run->state() == Explore::Run::State::Running && !exited,
                   label + ": Enter on it: Resume, the run on");
        test.check(run->showGameMenu() && run->click("main-menu"), label + ": Main menu");
        settleUntil([&] { return run->state() == Explore::Run::State::None && menu->shown(); }, 10000);
        test.check(run->state() == Explore::Run::State::None && menu->shown() && !simulation->exists()
                           && Characters::Cast::instance().count() == 0,
                   label + ": back to the menu, the run and its character gone");
    };
    // A walk that cannot start: back to the window with why.
    const auto walkFails = [&](const QString &label) {
        test.check(panel->ready() && panel->go(), label + ": Explore");
        settleUntil([run] { return run->state() == Explore::Run::State::Loading; }, 2000);
        settleUntil([&] { return run->state() == Explore::Run::State::None && menu->shown(); }, 300000);
        test.check(run->state() == Explore::Run::State::None && menu->shown() && !simulation->exists(),
                   label + ": back to the window, nothing running");
    };

    if (route.compare("EUROPE1", Qt::CaseInsensitive) == 0) {
        test.check(panel->chooseRun("lclsrvce.act"), "lclsrvce chosen");
        runOnce("lclsrvce", [&] {
            Activity *activity = run->activity();
            test.check(activity != nullptr && activity->name.compare("lclsrvce.act", Qt::CaseInsensitive) == 0,
                       "lclsrvce: the route's own activity");
        });
        // Exploring on a player path, on a winter day.
        test.check(panel->chooseRun("explore"), "exploring chosen");
        settleUntil([panel] { return panel->consistsRead(); }, 60000);
        test.check(panel->consistsRead(), "the consists read");
        test.check(panel->choosePath("Local Service.pat"), "the path Local Service");
        test.check(panel->chooseConsist("Local Service.con") || panel->ready(), "a consist");
        panel->setDay(QDate(2026, 1, 15));
        panel->setTime(10 * 3600 + 30 * 60);
        panel->setWeather(1);
        runOnce("exploring", [&] {
            Activity *activity = run->activity();
            test.check(activity != nullptr && activity->header != nullptr && activity->header->season == 3
                               && activity->header->weather == 1,
                       "exploring: winter and snow in its header");
            const std::shared_ptr<const Sim::SimulationInput> input = simulation->input();
            test.check(input != nullptr && input->air.date == QDate(2026, 1, 15) && input->air.snow,
                       QString("exploring: the run's first day the day chosen (%1)")
                               .arg(input != nullptr ? input->air.date.toString("yyyy-MM-dd") : QString()));
            test.check(input != nullptr && input->startTime == 10 * 3600 + 30 * 60,
                       QString("exploring: it starts at 10:30 (%1)").arg(input != nullptr ? input->startTime : -1));
            test.check(Game::season == "WinterSnow", "exploring: the content's season " + Game::season);
        });

        // Walking (task explore 04). lclsrvce on foot at its second stop:
        // the run from 5 minutes before the player's train is due there.
        test.check(panel->chooseRun("lclsrvce.act"), "walking: lclsrvce chosen");
        panel->setWalk(true);
        settleUntil([panel] { return panel->stationsRead(); }, 30000);
        const QStringList stops = panel->stationNames();
        test.check(stops.size() >= 2 && panel->chooseStation(stops.at(1)), "its second stop chosen: " + stops.value(1));
        const Explore::RunChoice second = panel->choice();
        walkOnce("lclsrvce on foot", [&] {
            const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->frame();
            const std::shared_ptr<const Sim::SimulationInput> input = simulation->input();
            test.check(frame != nullptr && input != nullptr && second.time > input->startTime
                               && frame->time >= second.time - 1.0 && frame->time < second.time + 60.0,
                       QString("lclsrvce on foot: the run went on to %1 (at %2, the activity from %3)")
                               .arg(second.time).arg(frame != nullptr ? frame->time : -1.0)
                               .arg(input != nullptr ? input->startTime : -1));
            test.check(input != nullptr && simulation->trainOf(true, -1) >= 0,
                       "lclsrvce on foot: its player's train in the run, its own driver");
        });
        // Exploring with the train on its path: it calls at that stop.
        test.check(panel->chooseRun("explore"), "walking: exploring chosen");
        settleUntil([panel] { return panel->stationsRead(); }, 30000);
        panel->setExploreTrain(true);
        test.check(panel->choosePath("Local Service.pat") && panel->chooseStation(second.station),
                   "the path Local Service, on foot at " + second.station);
        panel->setTime(9 * 3600);
        walkOnce("exploring on foot", [&] {
            const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->frame();
            const std::shared_ptr<const Sim::SimulationInput> input = simulation->input();
            const int player = simulation->trainOf(true, -1);
            const bool stops = input != nullptr && player >= 0 && player < int(input->trains.size())
                    && !input->trains[size_t(player)].stops.empty();
            test.check(stops && frame != nullptr && frame->time > 9 * 3600,
                       QString("exploring on foot: the train stops on its path (%1 stops), the run on to %2 s")
                               .arg(stops ? int(input->trains[size_t(player)].stops.size()) : 0)
                               .arg(frame != nullptr ? frame->time : -1.0, 0, 'f', 0));
        });
        // A station the path does not pass: back to the window.
        const QStringList all = panel->stationNames();
        test.check(all.contains("Carlisle") && panel->chooseStation("Carlisle"), "on foot at Carlisle");
        // ShrtPass: Horton in Ribblesdale to Dent.
        test.check(panel->choosePath("ShrtPass.pat"), "the path ShrtPass (Horton in Ribblesdale to Dent)");
        walkFails("exploring off the path");
        // Exploring with no train: the route on foot.
        panel->setExploreTrain(false);
        test.check(panel->chooseStation("Settle"), "no train, on foot at Settle");
        panel->setTime(10 * 3600);
        walkOnce("no train", [&] {
            const std::shared_ptr<const Sim::SimulationInput> input = simulation->input();
            test.check(input != nullptr && input->trains.empty() && input->startTime == 10 * 3600,
                       "no train: a run with no trains from 10:00");
        });
        panel->setWalk(false);
    } else if (route.compare("PRUDNIK", Qt::CaseInsensitive) == 0) {
        test.check(panel->chooseRun("PSK_2026.timetable_or"), "PSK_2026 chosen");
        const QStringList trains = panel->trainNames();
        test.check(!trains.isEmpty() && panel->chooseTrain(trains.first()), "a train chosen: " + trains.value(0));
        qInfo().noquote() << "[tests:explore-start] PSK_2026's train:" << trains.value(0);
        runOnce("PSK_2026", [&] {
            Activity *activity = run->activity();
            test.check(activity != nullptr && activity->isTimetables() && activity->playerTrain == trains.value(0),
                       "PSK_2026: the set, the train chosen the player's");
        });

        // Walking (task explore 04): 0518_R46403 at Raszowa, 5 minutes
        // before it comes; then no train at Prudnik.
        panel->setWalk(true);
        test.check(panel->chooseTrain("0518_R46403_R-KK-O") && panel->chooseStation("Raszowa"),
                   "on foot at Raszowa for 0518_R46403");
        const Explore::RunChoice raszowa = panel->choice();
        walkOnce("Raszowa on foot", [&] {
            Activity *activity = run->activity();
            const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->frame();
            test.check(activity != nullptr && activity->playerTrain.isEmpty() && simulation->trainOf(true, -1) < 0,
                       "Raszowa on foot: no player's train");
            test.check(frame != nullptr && frame->time >= raszowa.time - 1.0 && frame->time < raszowa.time + 60.0,
                       QString("Raszowa on foot: the run from %1 (at %2)").arg(raszowa.time)
                               .arg(frame != nullptr ? frame->time : -1.0, 0, 'f', 0));
            // The train comes where the character stands, in front of it
            // (live, 2026-11-21: none came into the view).
            const std::shared_ptr<const Sim::SimulationInput> input = simulation->input();
            int train = -1;
            for (const Sim::TrainPlan &plan : input != nullptr ? input->trains : std::vector<Sim::TrainPlan>())
                if (plan.timetableTrain && plan.service == "0518_R46403_R-KK-O")
                    train = plan.id;
            const double due = raszowa.time + 300.0;
            QString error;
            simulation->runToSeconds(due + 10.0, &error);
            settleUntil([&] {
                const std::shared_ptr<const Sim::SimulationFrame> f = simulation->frame();
                return f != nullptr && f->time >= due + 9.0 && !simulation->runs();
            }, 120000);
            // The latest frame (offscreen, the view's pinned one may be the
            // run's start).
            simulation->unpinViewFrame();
            simulation->placeViewTrains();
            Characters::Character *walker = Characters::Cast::instance().player();
            int tile[2];
            float at[3], heading[3], length = 0.0f;
            const bool placed = train >= 0 && walker != nullptr && simulation->trainPlace(train, tile, at, heading, &length);
            double away = 1e9, facing = 180.0;
            if (placed) {
                const double fx = walker->feet()[0] + 2048.0 * (walker->tile()[0] - tile[0]);
                const double fz = walker->feet()[2] + 2048.0 * (walker->tile()[1] - tile[1]);
                // The nearest point of the train's line.
                const double hx = heading[0], hz = heading[2], hl = std::hypot(hx, hz);
                const double ux = hl > 0 ? hx / hl : 0.0, uz = hl > 0 ? hz / hl : 1.0;
                const double along = std::clamp((fx - at[0]) * ux + (fz - at[2]) * uz, -length / 2.0, length / 2.0);
                const double nx = at[0] + ux * along - fx, nz = at[2] + uz * along - fz;
                away = std::hypot(nx, nz);
                const double yaw = walker->facing();
                const double dot = (std::sin(yaw) * nx + std::cos(yaw) * nz) / std::max(1e-6, away);
                facing = std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / M_PI;
            }
            if (placed)
                qInfo().noquote() << QString("[tests:explore-start] the train's middle tile %1 %2 (%3, %4), the character's "
                                             "tile %5 %6 (%7, %8)")
                                             .arg(tile[0]).arg(tile[1]).arg(double(at[0]), 0, 'f', 1).arg(double(at[2]), 0, 'f', 1)
                                             .arg(walker->tile()[0]).arg(walker->tile()[1])
                                             .arg(double(walker->feet()[0]), 0, 'f', 1).arg(double(walker->feet()[2]), 0, 'f', 1);
            const std::shared_ptr<const Sim::SimulationFrame> after = simulation->frame();
            qInfo().noquote() << QString("[tests:explore-start] Raszowa on foot: at %1 s the train %2 (%3), %4 m from "
                                         "the character, %5 degrees off its look")
                                         .arg(after != nullptr ? after->time : -1.0, 0, 'f', 0).arg(train)
                                         .arg(simulation->stateText(train)).arg(away, 0, 'f', 1).arg(facing, 0, 'f', 0);
            test.check(placed && away < 6.0 && facing < 45.0,
                       QString("Raszowa on foot: 0518_R46403 at its stop %1 m from the character, %2 degrees off its look")
                               .arg(away, 0, 'f', 1).arg(facing, 0, 'f', 0));
        });
        panel->chooseNoTrain();
        test.check(panel->chooseStation("Prudnik"), "no train, on foot at Prudnik");
        panel->setTime(8 * 3600);
        walkOnce("Prudnik on foot", [&] {
            const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->frame();
            test.check(frame != nullptr && frame->time >= 8 * 3600 - 1.0, "Prudnik on foot: the run from 08:00");
        });
        panel->setWalk(false);
    } else {
        qInfo().noquote() << "[tests:explore-start] runs only on EUROPE1 or PRUDNIK:" << route;
    }

    delete window;
    settings.clearSessionValue("core.explore.resolution");
    settings.clearSessionValue("core.explore.fillScreen");
    settings.clearSessionValue("core.startup.season");
    Explore::Panel::setRememberedFile(QString());
    Game::exploreMode = false;
    return test.finish();
}
