// Task explore 06: the wait in walking runs.

#include "ExploreWaitTestSuite.h"
#include "TokenTestSupport.h"

#include <explore/ExploreMenu.h>
#include <explore/ExplorePanel.h>
#include <explore/ExploreRun.h>
#include <explore/ExploreWait.h>
#include <explore/ExploreWindow.h>
#include <routeEditor/RouteEditorGLWidget.h>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>
#include <tsre/characters/Cast.h>
#include <tsre/characters/Character.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/ui/UiRouteDisplays.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QKeyEvent>
#include <QRegularExpression>
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

void settle(int ms) {
    settleUntil([] { return false; }, ms);
}

// A key pressed and let go on the view, as a keyboard would.
void key(QWidget *view, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString &text = QString()) {
    QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
    QApplication::sendEvent(view, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
    QApplication::sendEvent(view, &release);
    settle(60);
}

QString clockText(double seconds) {
    const int s = int(std::floor(std::fmod(std::fmod(seconds, 86400.0) + 86400.0, 86400.0)));
    return QString("%1:%2").arg(s / 3600, 2, 10, QLatin1Char('0')).arg(s / 60 % 60, 2, 10, QLatin1Char('0'));
}

double runTime() {
    const std::shared_ptr<const Sim::SimulationFrame> frame = Sim::SimulationController::instance()->frame();
    return frame != nullptr ? frame->time : -1.0;
}

// Where a train is to a stop of its plan: metres to it on its way (-1 not
// known), there, past it, its speed; its booked arrival there and how late
// it is now.
struct Near {
    double metres = -1.0;
    bool there = false;
    bool past = false;
    double speed = 0.0;
    double booked = -1.0;
    double late = 0.0;
    double now = -1.0;
};

Near nearOf(int train, int stop) {
    Near n;
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->frame();
    const std::shared_ptr<const Sim::SimulationInput> in = simulation->input();
    if (frame == nullptr || in == nullptr || train < 0 || size_t(train) >= in->trains.size()
            || size_t(train) >= frame->trains.size())
        return n;
    const Sim::TrainPlan &plan = in->trains[size_t(train)];
    if (stop < 0 || size_t(stop) >= plan.stops.size())
        return n;
    const Sim::TrainState &state = frame->trains[size_t(train)].state;
    const Ui::RouteDisplays::Progress progress = Ui::RouteDisplays::progress(plan, &state, frame->time);
    n.now = frame->time;
    n.there = progress.at == stop && progress.standing;
    n.past = progress.at > stop;
    n.speed = std::fabs(double(state.speed));
    n.booked = plan.stops[size_t(stop)].arrival;
    n.late = progress.late;
    if (progress.at == stop && plan.stops[size_t(stop)].part == state.part)
        n.metres = plan.stops[size_t(stop)].front - state.front;
    return n;
}

// The wait ended about 30 s before the train arrives: it is not there yet,
// and braking into the stop it needs 30 s (2d/v) or a step (5 s) less; by
// its booked time and delay within 45 s where its distance is not known.
bool endedBefore(const Near &n) {
    if (n.there || n.past || n.now < 0.0)
        return false;
    if (n.metres >= 0.0)
        return n.metres <= Explore::WaitGoals::Creep
                || (n.speed > 0.5 && 2.0 * n.metres / n.speed <= Explore::WaitGoals::Ahead
                    && 2.0 * n.metres / n.speed >= Explore::WaitGoals::Ahead - 2.0 * Explore::WaitGoals::StepLeast);
    return n.booked >= 0.0 && n.booked + n.late - n.now <= 45.0;
}

QString nearText(const Near &n) {
    return QString("at %1, %2 m from the stop at %3 km/h (2d/v %4 s), booked %5 (+%6 s)%7%8")
            .arg(clockText(n.now)).arg(n.metres, 0, 'f', 0).arg(n.speed * 3.6, 0, 'f', 0)
            .arg(n.speed > 0.5 ? 2.0 * n.metres / n.speed : -1.0, 0, 'f', 0)
            .arg(n.booked >= 0 ? clockText(n.booked) : QString("-")).arg(n.late, 0, 'f', 0)
            .arg(n.there ? ", there" : "").arg(n.past ? ", past it" : "");
}

int trainNamed(const QString &service) {
    const std::shared_ptr<const Sim::SimulationInput> in = Sim::SimulationController::instance()->input();
    if (in == nullptr)
        return -1;
    for (size_t t = 0; t < in->trains.size(); ++t)
        if (in->trains[t].service == service)
            return int(t);
    return -1;
}

}

int TsreTests::runExploreWaitSuite(bool verbose) {
    TokenTest::Suite test{"[tests:explore-wait]", verbose};
    const QString root = QDir::cleanPath(Game::root);
    const QString route = Game::route.trimmed();
    if (route.isEmpty() || !QFileInfo(root + "/ROUTES/" + route).isDir()) {
        qInfo().noquote() << "[tests:explore-wait] needs --game-root and --route (EUROPE1 or PRUDNIK): nothing checked";
        return test.finish();
    }
    SettingsManager &settings = SettingsManager::instance();
    QTemporaryDir temporary;
    Explore::Panel::setRememberedFile(temporary.filePath("explore.json"));
    settings.setSessionValue("core.explore.resolution", "1280x720");
    settings.setSessionValue("core.explore.fillScreen", false);
    Explore::Window *window = new Explore::Window(route);
    window->setExitHandler([] {});
    window->open();
    RouteEditorGLWidget *view = window->view();
    settleUntil([window] { return window->menu() != nullptr && window->menu()->panel() != nullptr; }, 10000);
    Explore::Menu *menu = window->menu();
    Explore::Run *run = window->run();
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    test.check(menu != nullptr && menu->panel() != nullptr, "the menu and the Explore window loaded");
    if (menu == nullptr || menu->panel() == nullptr) {
        delete window;
        Explore::Panel::setRememberedFile(QString());
        return test.finish();
    }
    menu->open(Explore::Menu::Page::Explore);
    Explore::Panel *panel = menu->panel();
    test.check(panel->chooseRoute(route), "the route chosen: " + route);

    // A walking run started, on foot on its platform; false when it did
    // not get there.
    const auto walk = [&](const QString &label) {
        QElapsedTimer timer;
        timer.start();
        test.check(panel->ready() && panel->go(), label + ": Explore on foot at " + panel->choice().station);
        settleUntil([run] { return run->state() == Explore::Run::State::Loading; }, 2000);
        settleUntil([run] { return run->state() != Explore::Run::State::Loading; }, 300000);
        settleUntil([&] { return run->onTrack() || run->state() != Explore::Run::State::Running; }, 300000);
        test.check(run->onTrack() && run->wait() != nullptr,
                   QString("%1: on foot (%2 s)").arg(label).arg(timer.elapsed() / 1000.0, 0, 'f', 1));
        return run->onTrack() && run->wait() != nullptr;
    };
    const auto back = [&](const QString &label) {
        if (run->state() == Explore::Run::State::Running && run->showGameMenu())
            run->click("main-menu");
        settleUntil([&] { return run->state() == Explore::Run::State::None && menu->shown(); }, 10000);
        test.check(run->state() == Explore::Run::State::None, label + ": back to the menu");
    };
    // The wait running to its end (2 minutes at most here).
    const auto waitEnd = [&](Explore::Wait *wait) {
        QElapsedTimer timer;
        timer.start();
        settleUntil([wait] { return !wait->waiting(); }, 120000);
        return timer.elapsed();
    };
    // T: the dialog.
    const auto openByT = [&](Explore::Wait *wait, const QString &label) {
        key(view, Qt::Key_T, Qt::NoModifier, "t");
        settleUntil([wait] { return wait->shown(); }, 2000);
        settle(150);
        test.check(wait->shown(), label + ": T opens the wait");
    };

    if (route.compare("PRUDNIK", Qt::CaseInsensitive) == 0) {
        test.check(panel->chooseRun("PSK_2026.timetable_or"), "PSK_2026 chosen");
        panel->setWalk(true);
        test.check(panel->chooseTrain("0518_R46403_R-KK-O") && panel->chooseStation("Raszowa"),
                   "on foot at Raszowa for 0518_R46403");
        if (walk("Raszowa")) {
            Explore::Wait *wait = run->wait();
            const int train = trainNamed("0518_R46403_R-KK-O");
            test.check(train >= 0, "0518_R46403 in the run");
            // The dialog: the run's clock, at a platform, the next train.
            openByT(wait, "Raszowa");
            const double opened = runTime();
            const QString shownTime = wait->text("wait-time");
            test.check(shownTime == clockText(opened) || shownTime == clockText(opened - 60.0),
                       QString("the clock at the run's time: %1 (the run at %2)").arg(shownTime, clockText(opened)));
            test.check(!wait->text("wait-day").isEmpty(), "the day: " + wait->text("wait-day"));
            test.check(wait->atPlatform() && !wait->inTrain(), "at a platform, not in a train");
            const QString trainText = wait->text("wait-train-text");
            test.check(trainText.contains("46403"), "Next train: " + trainText);
            key(view, Qt::Key_Escape);
            test.check(!wait->shown() && !wait->waiting() && simulation->runs(), "Esc: closed, the run on");
            // The game menu's Wait.
            test.check(run->showGameMenu() && run->canWait(), "the game menu with Wait (walking)");
            test.check(run->click("wait"), "the game menu's Wait clicked");
            settleUntil([wait] { return wait->shown(); }, 2000);
            test.check(wait->shown() && !run->gameMenuShown(), "the wait instead of the game menu");
            key(view, Qt::Key_T, Qt::NoModifier, "t");
            test.check(!wait->shown(), "T again: closed");

            // Next train: the run as fast as it can to 30 s before it comes.
            openByT(wait, "Raszowa");
            test.check(wait->click("wait-train") && wait->waiting() && wait->fast()
                               && wait->goal() == Explore::Wait::Goal::Train,
                       "Next train: waiting, fast");
            test.check(wait->due().train == train, QString("for 0518_R46403 (train %1)").arg(wait->due().train));
            test.check(simulation->worldFollowsFastRuns(), "the world's time following the fast run");
            const qint64 trainMs = waitEnd(wait);
            const Near atRaszowa = nearOf(train, wait->due().stop);
            test.check(!wait->waiting() && !wait->shown() && endedBefore(atRaszowa),
                       QString("the wait ended before 0518_R46403 came (%1; %2 ms)").arg(nearText(atRaszowa)).arg(trainMs));
            test.check(simulation->runs() && !simulation->worldFollowsFastRuns(),
                       "the run playing on, the world's time as before");

            // The train comes; the character boards it.
            const int raszowaStop = wait->due().stop;
            for (int i = 0; i < 60 && !nearOf(train, raszowaStop).there; ++i) {
                const double at = runTime();
                simulation->runToSeconds(at + 5.0);
                settleUntil([&] { return runTime() >= at + 4.5 && !simulation->runs(); }, 10000);
            }
            test.check(nearOf(train, raszowaStop).there, "0518_R46403 at Raszowa");
            simulation->unpinViewFrame();
            simulation->placeViewTrains();
            Characters::Character *walker = Characters::Cast::instance().player();
            int tile[2];
            float centre[3], heading[3], length = 0.0f;
            Characters::Character::Place place;
            bool boarded = false;
            if (walker != nullptr && simulation->trainPlace(train, tile, centre, heading, &length)) {
                centre[1] += 2.0f;
                if (Characters::Character::placeNear(tile, centre, place) && place.train == train) {
                    walker->start(place, walker->facing(), 0.0f);
                    boarded = walker->inCar() && walker->train() == train;
                }
            }
            test.check(boarded, "the character in one of its cars");
            simulation->play(run->activity());
            settle(300);
            if (boarded) {
                openByT(wait, "in the train");
                test.check(wait->inTrain() && !wait->atPlatform(), "in a train, not at a platform");
                const QString stopText = wait->text("wait-stop-text");
                test.check(!stopText.isEmpty(), "Next stop: " + stopText);
                test.check(wait->click("wait-stop") && wait->waiting() && wait->goal() == Explore::Wait::Goal::Stop,
                           "Next stop: waiting");
                const int nextStop = wait->due().stop;
                test.check(nextStop > raszowaStop, QString("its stop after Raszowa (%1)").arg(wait->due().station));
                const qint64 stopMs = waitEnd(wait);
                const Near next = nearOf(train, nextStop);
                test.check(!wait->waiting() && endedBefore(next),
                           QString("the wait ended before %1 (%2; %3 ms)").arg(wait->due().station, nearText(next)).arg(stopMs));
                test.check(walker->inCar() && walker->train() == train, "the character still in its car");
            }

            // A time: Left and Right, Enter on Wait; the run to it exactly.
            openByT(wait, "a time");
            wait->setMinutes(30);
            settle(100);
            key(view, Qt::Key_Right);
            settle(100);
            test.check(wait->minutes() == 35 && wait->text("wait-length") == "0:35",
                       "Right: 5 minutes more (" + wait->text("wait-length") + ")");
            key(view, Qt::Key_Left);
            key(view, Qt::Key_Right, Qt::ShiftModifier);
            test.check(wait->minutes() == 90, QString("Left, Shift and Right: 1:30 (%1 minutes)").arg(wait->minutes()));
            key(view, Qt::Key_Left, Qt::ShiftModifier);
            const double start = runTime();
            key(view, Qt::Key_Return);
            test.check(wait->waiting() && wait->goal() == Explore::Wait::Goal::Time, "Enter on Wait: waiting");
            const double end = wait->end();
            test.check(std::fabs(end - (start + 1800.0)) < 2.0,
                       QString("until %1 (from %2)").arg(clockText(end), clockText(start)));
            const qint64 timeMs = waitEnd(wait);
            const double after = runTime();
            test.check(!wait->waiting() && after >= end - 0.5 && after < end + 5.0,
                       QString("30 minutes on: the run at %1 s, the end %2 s (%3 ms)").arg(after, 0, 'f', 1)
                               .arg(end, 0, 'f', 1).arg(timeMs));
            test.check(simulation->runs() && simulation->speed() == 1.0,
                       QString("the run on at 1x (%1)").arg(simulation->speed()));

            // Stopped on the way (the longest wait, Esc as soon as the run
            // is ten minutes on: an offscreen run goes hours a second).
            openByT(wait, "twelve hours");
            wait->setMinutes(Explore::Wait::MinutesMost);
            const double from = runTime();
            test.check(wait->click("wait-go") && wait->waiting(), "twelve hours: waiting");
            settleUntil([&] { return runTime() >= from + 600.0 || !wait->waiting(); }, 60000);
            key(view, Qt::Key_Escape);
            const double stopped = runTime();
            test.check(!wait->waiting() && !wait->shown() && stopped < from + 12 * 3600.0 - 600.0 && simulation->runs(),
                       QString("Esc: stopped at %1 (from %2), the run on").arg(clockText(stopped), clockText(from)));

            // Skipped: the world not drawn until the end.
            openByT(wait, "skipped");
            wait->setMinutes(60);
            test.check(wait->click("wait-go") && wait->click("wait-skip") && wait->skipping(), "an hour, skipped");
            settleUntil([&] { return view->exploreWorldHidden() || !wait->waiting(); }, 3000);
            test.check(view->exploreWorldHidden() || !wait->waiting(), "the world not drawn");
            waitEnd(wait);
            test.check(!wait->waiting() && !view->exploreWorldHidden(), "the end: the world drawn again");

            // A server's run: at its pace, the dialog open counting down.
            Explore::Wait::setServerPace(true);
            openByT(wait, "a server's pace");
            wait->setMinutes(5);
            const double paced = runTime();
            test.check(wait->click("wait-go") && wait->waiting() && !wait->fast(), "at the run's pace");
            QElapsedTimer real;
            real.start();
            settle(2000);
            const double passed = runTime() - paced;
            test.check(wait->shown() && wait->waiting() && passed < real.elapsed() / 1000.0 * 3.0 + 1.0
                               && !wait->text("wait-left").isEmpty() && !simulation->worldFollowsFastRuns(),
                       QString("the dialog open, %1 s of the run in %2 ms, %3")
                               .arg(passed, 0, 'f', 1).arg(real.elapsed()).arg(wait->text("wait-left")));
            test.check(wait->text("wait-skip").isEmpty() || !wait->skipping(), "no Skip");
            key(view, Qt::Key_T, Qt::NoModifier, "t");
            test.check(!wait->waiting() && !wait->shown(), "T: stopped and closed");
            Explore::Wait::setServerPace(false);
        }
        back("Raszowa");

        // Exploring with no train (no trains at all): Next train greyed.
        test.check(panel->chooseRun("explore"), "exploring chosen");
        settleUntil([panel] { return panel->stationsRead(); }, 30000);
        panel->setExploreTrain(false);
        test.check(panel->chooseStation("Prudnik"), "no train, on foot at Prudnik");
        panel->setTime(8 * 3600);
        if (walk("Prudnik")) {
            Explore::Wait *wait = run->wait();
            openByT(wait, "Prudnik");
            test.check(wait->atPlatform(), "at a platform");
            test.check(wait->text("wait-train-text") == qtTrId("ui.explore.wait.no.train"),
                       "Next train: " + wait->text("wait-train-text"));
            test.check(!wait->click("wait-train") || !wait->waiting(), "nothing to wait for");
            key(view, Qt::Key_Escape);
        }
        back("Prudnik");
        panel->setWalk(false);
    } else if (route.compare("EUROPE1", Qt::CaseInsensitive) == 0) {
        test.check(panel->chooseRun("lclsrvce.act"), "lclsrvce chosen");
        panel->setWalk(true);
        settleUntil([panel] { return panel->stationsRead(); }, 30000);
        const QStringList stops = panel->stationNames();
        test.check(stops.size() >= 2 && panel->chooseStation(stops.at(1)), "its second stop: " + stops.value(1));
        if (walk("lclsrvce")) {
            Explore::Wait *wait = run->wait();
            openByT(wait, "lclsrvce");
            test.check(wait->atPlatform(), "at a platform");
            const QString trainText = wait->text("wait-train-text");
            test.check(!trainText.isEmpty() && trainText != qtTrId("ui.explore.wait.no.train"), "Next train: " + trainText);
            if (wait->click("wait-train") && wait->waiting()) {
                const int train = wait->due().train;
                const int stop = wait->due().stop;
                const qint64 ms = waitEnd(wait);
                const Near n = nearOf(train, stop);
                test.check(!wait->waiting() && endedBefore(n),
                           QString("the wait ended before it came (%1; %2 ms)").arg(nearText(n)).arg(ms));
            } else {
                test.check(false, "Next train: waiting");
            }
            openByT(wait, "a time");
            wait->setMinutes(15);
            const double start = runTime();
            test.check(wait->click("wait-go") && wait->waiting(), "15 minutes: waiting");
            waitEnd(wait);
            const double after = runTime();
            test.check(!wait->waiting() && after >= start + 900.0 - 1.0 && after < start + 900.0 + 5.0,
                       QString("15 minutes on (%1 s)").arg(after - start, 0, 'f', 1));
        }
        back("lclsrvce");
        panel->setWalk(false);
    } else {
        qInfo().noquote() << "[tests:explore-wait] runs only on EUROPE1 or PRUDNIK:" << route;
    }

    delete window;
    settings.clearSessionValue("core.explore.resolution");
    settings.clearSessionValue("core.explore.fillScreen");
    settings.clearSessionValue("core.startup.season");
    Explore::Panel::setRememberedFile(QString());
    Game::exploreMode = false;
    return test.finish();
}
