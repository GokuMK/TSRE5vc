// Task explore 02: runs started from the Explore window.

#include "ExploreStartTestSuite.h"
#include "TokenTestSupport.h"

#include <explore/ExploreMenu.h>
#include <explore/ExplorePanel.h>
#include <explore/ExploreRun.h>
#include <explore/ExploreWindow.h>
#include <routeEditor/RouteEditorGLWidget.h>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/Consist.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
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
