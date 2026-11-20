/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <explore/ExploreRun.h>
#include <routeEditor/RouteEditorGLWidget.h>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>
#include <tsre/environment/WorldClock.h>
#include <tsre/script/ScriptState.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/texture/TexLib.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/Service.h>
#include <tsre/ui/UiRuntime.h>
#include <tsre/ui/UiScreen.h>
#include <tsre/world/Route.h>
#include <tsre/world/Trk.h>

#include <RmlUi/Core.h>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTimer>
#include <algorithm>

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

// The content season (core.startup.season) of an MSTS season and weather:
// SpringClear to WinterSnow.
QString contentSeason(int season, int weather) {
    static const char *const seasons[4] = {"Spring", "Summer", "Autumn", "Winter"};
    static const char *const weathers[3] = {"Clear", "Snow", "Rain"};
    return QString(seasons[std::clamp(season, 0, 3)]) + weathers[std::clamp(weather, 0, 2)];
}

// A file name in the route's folder not taken by one loaded or on disk.
QString freeName(const QString &base, const std::function<bool(const QString &)> &taken) {
    for (int i = 0; i < 1000; ++i) {
        const QString name = i == 0 ? base : base + QString::number(i);
        if (!taken(name))
            return name;
    }
    return base;
}

}

struct Run::Data {
    Ui::Screen *screen = nullptr;
    Rml::ElementDocument *loading = nullptr;
    Rml::ElementDocument *game = nullptr;
    bool documentsTried = false;
    std::vector<std::unique_ptr<Listener>> listeners;
    std::vector<std::pair<Rml::Element *, std::pair<Rml::EventId, Listener *>>> attached;
    // The loading screen's picture held while it shows (loaded before the
    // first frame of it).
    int picture = -1;
    int loadingFrames = 0;
    bool loadQueued = false;
    // The route of the last run (another run on it reads it again).
    QString lastRoute;
    // The run started, its train not yet on the track (a timetable's AI
    // trains run up to the player's start first): the loading screen stays.
    bool waitingForTrain = false;
    QElapsedTimer waiting;
    int shownSeconds = -1;

    void listen(Rml::Element *element, Rml::EventId event, std::function<void(Rml::Event &)> handler) {
        if (element == nullptr)
            return;
        listeners.push_back(std::make_unique<Listener>(std::move(handler)));
        element->AddEventListener(event, listeners.back().get());
        attached.push_back({element, {event, listeners.back().get()}});
    }
};

Run::Run(RouteEditorGLWidget &view, std::function<void(const QString &)> menu, std::function<void()> exit)
    : view(view), d(new Data), menuBack(std::move(menu)), exitHandler(std::move(exit)) {
}

Run::~Run() {
    for (const auto &[element, listener] : d->attached)
        element->RemoveEventListener(listener.first, listener.second);
    if (d->screen != nullptr)
        d->screen->setMenuHandler(nullptr);
    if (d->loading != nullptr)
        d->loading->Close();
    if (d->game != nullptr)
        d->game->Close();
    if (d->picture >= 0)
        TexLib::delRef(d->picture);
}

bool Run::loadDocuments() {
    if (d->documentsTried)
        return d->loading != nullptr && d->game != nullptr;
    d->documentsTried = true;
    Rml::Context *context = d->screen != nullptr ? d->screen->context() : nullptr;
    Ui::Runtime *runtime = Ui::Runtime::get();
    if (context == nullptr || runtime == nullptr)
        return false;
    {
        Script::State::Deadline deadline(runtime->lua());
        d->loading = context->LoadDocument((runtime->folder() + "/loading.rml").toStdString());
        d->game = context->LoadDocument((runtime->folder() + "/explore-game.rml").toStdString());
    }
    if (d->loading == nullptr || d->game == nullptr) {
        qWarning().noquote() << "[explore] the run's documents did not load:" << runtime->folder();
        return false;
    }
    Rml::ElementList buttons;
    d->game->GetElementsByTagName(buttons, "button");
    for (Rml::Element *button : buttons)
        d->listen(button, Rml::EventId::Mouseover, [button](Rml::Event &) { button->Focus(); });
    d->listen(d->game->GetElementById("resume"), Rml::EventId::Click, [this](Rml::Event &) {
        if (d->game->IsVisible())
            d->game->Hide();
    });
    // After RmlUi's event: the route is left, or the window closed, under it.
    d->listen(d->game->GetElementById("main-menu"), Rml::EventId::Click, [this](Rml::Event &) {
        QTimer::singleShot(0, this, [this] {
            leave();
            if (menuBack)
                menuBack(QString());
        });
    });
    d->listen(d->game->GetElementById("exit"), Rml::EventId::Click, [this](Rml::Event &) {
        QTimer::singleShot(0, this, [this] {
            if (exitHandler)
                exitHandler();
        });
    });
    // Esc with the view's keys: the game menu, while a run is on.
    d->screen->setMenuHandler([this] { return showGameMenu(); });
    return true;
}

void Run::frame(Ui::Screen &screen) {
    d->screen = &screen;
    loadDocuments();
    if (now == State::Running && d->waitingForTrain) {
        Sim::SimulationController *simulation = Sim::SimulationController::instance();
        if (simulation->driven() >= 0 && view.riddenObject() != nullptr) {
            d->waitingForTrain = false;
            qInfo().noquote() << QString("Explore: the player's train on the track %1 ms after the start")
                                         .arg(d->waiting.elapsed());
            hideLoading();
        } else {
            setLoadingStatus(int(d->waiting.elapsed() / 1000));
        }
        return;
    }
    if (now != State::Loading || d->loadQueued)
        return;
    // The loading screen drawn once before the load holds the GUI thread
    // (the route loads on it, as in the editor).
    if (d->loading != nullptr && !d->loading->IsVisible())
        showLoading();
    if (++d->loadingFrames < 2)
        return;
    d->loadQueued = true;
    QTimer::singleShot(0, this, [this] { load(); });
}

void Run::start(const RunChoice &choice) {
    if (now == State::Loading)
        return;
    if (now == State::Running)
        leave();
    chosen = choice;
    now = State::Loading;
    d->loadingFrames = 0;
    d->loadQueued = false;
    d->waitingForTrain = false;
    d->shownSeconds = 0;
    setLoadingStatus(-1);
    showLoading();
    view.update();
}

void Run::showLoading() {
    if (d->loading == nullptr)
        return;
    if (Rml::Element *name = d->loading->GetElementById("loading-name"))
        name->SetInnerRML(Rml::StringUtilities::EncodeRml(chosen.routeName.toStdString()));
    if (Rml::Element *picture = d->loading->GetElementById("loading-picture")) {
        if (d->picture >= 0) {
            TexLib::delRef(d->picture);
            d->picture = -1;
        }
        if (!chosen.loadingScreen.isEmpty()) {
            // Its pixels in before the first frame: the load after it holds
            // the thread that would take them.
            d->picture = TexLib::addTex(chosen.loadingScreen, false, TexLib::Loading::Background);
            QElapsedTimer waited;
            waited.start();
            while (d->picture >= 0 && !TexLib::loadQueuedNow(d->picture) && waited.elapsed() < 3000)
                QThread::msleep(5);
            picture->SetAttribute("src", chosen.loadingScreen.toStdString());
            picture->RemoveProperty("display");
        } else {
            picture->SetProperty("display", "none");
        }
    }
    d->loading->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void Run::setLoadingStatus(int seconds) {
    if (d->loading == nullptr || seconds == d->shownSeconds)
        return;
    d->shownSeconds = seconds;
    if (Rml::Element *status = d->loading->GetElementById("loading-status"))
        status->SetInnerRML(Rml::StringUtilities::EncodeRml(
                (seconds < 0 ? QString()
                             //% "Preparing the run: %1 s"
                             : qtTrId("ui.explore.preparing").arg(seconds)).toStdString()));
}

void Run::hideLoading() {
    if (d->loading != nullptr && d->loading->IsVisible())
        d->loading->Hide();
    if (d->picture >= 0) {
        TexLib::delRef(d->picture);
        d->picture = -1;
    }
}

void Run::fail(const QString &why) {
    qWarning().noquote() << "Explore: the run did not start:" << why;
    view.exploreLeaveRoute();
    running = nullptr;
    now = State::None;
    hideLoading();
    if (menuBack)
        menuBack(why);
}

void Run::load() {
    QElapsedTimer timer;
    timer.start();
    const bool same = d->lastRoute.compare(chosen.route, Qt::CaseInsensitive) == 0;
    Game::route = chosen.route;
    if (!Game::checkRoute(chosen.route)) {
        //% "The route %1 is not in the game root."
        fail(qtTrId("ui.explore.error.no.route").arg(chosen.route));
        return;
    }
    // The content's season (seasonal textures) from the run's: read when the
    // route loads (Route's snapshot of core.startup.season).
    SettingsManager::instance().setSessionValue("core.startup.season", contentSeason(chosen.season, chosen.weather));
    if (!view.exploreEnterRoute(same) || Game::currentRoute == nullptr || !Game::currentRoute->loaded) {
        //% "The route %1 did not load."
        fail(qtTrId("ui.explore.error.route.load").arg(chosen.routeName));
        return;
    }
    d->lastRoute = chosen.route;
    const qint64 routeMs = timer.elapsed();
    QString why;
    Activity *activity = prepareActivity(why);
    if (activity == nullptr) {
        fail(why);
        return;
    }
    // The route's activity, as the editor's choice of one makes it: its
    // trains drawn (the simulation's), a timetable's vehicles read ahead.
    Game::currentRoute->activitySelected(activity);
    // The run's first day (an activity's moved into its season by the run).
    WorldClock::instance()->setDate(chosen.day.toString("yyyy-MM-dd"));
    QString error;
    if (!Sim::SimulationController::instance()->playDriving(activity, &error)) {
        fail(error);
        return;
    }
    running = activity;
    now = State::Running;
    // The loading screen until the train is on the track.
    d->waitingForTrain = true;
    d->waiting.start();
    d->shownSeconds = -1;
    setLoadingStatus(0);
    qInfo().noquote() << QString("Explore: %1 on %2 started: the route in %3 ms, the run in %4 ms")
                                 .arg(activity->header != nullptr ? activity->header->name : activity->name,
                                      chosen.route)
                                 .arg(routeMs).arg(timer.elapsed() - routeMs);
}

Activity *Run::prepareActivity(QString &why) {
    const QString routePath = Game::root + "/ROUTES/" + chosen.route;
    const QString activities = routePath + "/ACTIVITIES";
    switch (chosen.kind) {
    case RunChoice::Kind::Activity: {
        // The route's own, loaded with it.
        const int id = ActLib::GetAct(activities, chosen.file);
        Activity *activity = id >= 0 ? ActLib::Act.value(id) : nullptr;
        if (activity == nullptr || activity->header == nullptr)
            //% "The activity %1 did not load."
            why = qtTrId("ui.explore.error.activity").arg(chosen.file);
        return activity;
    }
    case RunChoice::Kind::Timetable: {
        const int id = ActLib::AddTimetableSet(activities + "/OPENRAILS", chosen.file);
        Activity *set = ActLib::TimetableSets.value(id);
        if (set == nullptr || set->header == nullptr) {
            //% "The timetable %1 did not load."
            why = qtTrId("ui.explore.error.timetable").arg(chosen.file);
            return nullptr;
        }
        set->playerTrain = chosen.train;
        set->header->season = chosen.season;
        set->header->weather = chosen.weather;
        return set;
    }
    case RunChoice::Kind::Explore:
        break;
    }
    // Exploring: a new activity in memory, as the editor's New activity
    // makes one, its player's service made of the path and the consist, no
    // stops and no traffic. Never saved.
    // The service named after its consist: the train's name on the map and
    // in the simulation's messages.
    const QString services = routePath + "/SERVICES";
    const QString serviceName = freeName(QFileInfo(chosen.consist).completeBaseName(), [&](const QString &name) {
        return ActLib::ServiceExists(name, routePath);
    });
    Service *service = ActLib::Services.value(ActLib::AddService(services, serviceName + ".srv", true));
    const QString activityName = freeName("tsre-explore", [&](const QString &name) {
        return ActLib::GetAct(activities, name + ".act") >= 0 || QFile::exists(activities + "/" + name + ".act");
    });
    Activity *activity = ActLib::Act.value(ActLib::AddAct(activities, activityName + ".act", true));
    if (service == nullptr || activity == nullptr) {
        //% "The run could not be made."
        why = qtTrId("ui.explore.error.explore");
        return nullptr;
    }
    service->setDisplayName(serviceName);
    service->setTrainConfig(QFileInfo(chosen.consist).completeBaseName());
    service->setNewPath(QFileInfo(chosen.path).completeBaseName());
    service->stationStop.clear();
    Trk *trk = Game::currentRoute != nullptr ? Game::currentRoute->getTrk() : nullptr;
    activity->init(trk != nullptr ? trk->idName : chosen.route, qtTrId("ui.explore.explore.route"));
    activity->header->startTime[0] = chosen.time / 3600;
    activity->header->startTime[1] = chosen.time / 60 % 60;
    activity->header->startTime[2] = chosen.time % 60;
    activity->header->season = chosen.season;
    activity->header->weather = chosen.weather;
    activity->createNewPlayerService(service->nameId, chosen.time);
    return activity;
}

void Run::leave() {
    if (now == State::None)
        return;
    d->waitingForTrain = false;
    if (d->game != nullptr && d->game->IsVisible())
        d->game->Hide();
    hideLoading();
    // The route switch stops the simulation and empties the route's
    // libraries.
    view.exploreLeaveRoute();
    running = nullptr;
    now = State::None;
}

bool Run::onTrack() const {
    return now == State::Running && !d->waitingForTrain;
}

bool Run::showGameMenu() {
    if (now != State::Running || d->screen == nullptr || d->game == nullptr)
        return false;
    d->screen->showMenu(d->game);
    return true;
}

bool Run::gameMenuShown() const {
    return d->game != nullptr && d->game->IsVisible();
}

bool Run::click(const QString &id) {
    if (d->game == nullptr)
        return false;
    Rml::Element *element = d->game->GetElementById(id.toStdString());
    if (element == nullptr)
        return false;
    element->Click();
    return true;
}

}
