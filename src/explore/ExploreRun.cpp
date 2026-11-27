/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <explore/ExploreLists.h>
#include <explore/ExploreRun.h>
#include <explore/ExploreWait.h>
#include <explore/ExploreWalk.h>
#include <routeEditor/RouteEditorGLWidget.h>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>
#include <tsre/environment/WorldClock.h>
#include <tsre/script/ScriptState.h>
#include <tsre/characters/RouteCollision.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/SimulationSession.h>
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
#include <QHash>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <algorithm>
#include <atomic>

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
    // The wait (task explore 06): its model and its dialog.
    std::unique_ptr<Wait> wait;
    Rml::ElementDocument *waitDocument = nullptr;
    // A timetable's trains' comments by name (the wait names trains by
    // them), read on a worker when a run starts.
    struct Names {
        std::atomic<bool> done{false};
        QHash<QString, QString> comments;
    };
    std::shared_ptr<Names> names;
    bool documentsTried = false;
    std::vector<std::unique_ptr<Listener>> listeners;
    std::vector<std::pair<Rml::Element *, std::pair<Rml::EventId, Listener *>>> attached;
    // The game menu's items follow the mouse only once it moves (a walking
    // character's pointer rests in the view's middle, over the menu: Exit
    // was chosen by it, and Enter quit).
    bool hoverArmed = false;
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
    // Walking (task explore 04): the run going on as fast as it can to its
    // start (Run to), then the character placed on the platform; the
    // loading screen stays until it stands there, then the run plays.
    enum class Walking { None, RunningUp, Placing } walking = Walking::None;
    double runUpTo = -1.0;
    WalkStart walkStart;
    std::unique_ptr<Characters::RouteCollision> probe;
    QElapsedTimer placing;

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
    view.setExploreWaitKey(nullptr);
    for (const auto &[element, listener] : d->attached)
        element->RemoveEventListener(listener.first, listener.second);
    if (d->screen != nullptr)
        d->screen->setMenuHandler(nullptr);
    if (d->wait != nullptr) {
        d->wait->reset();
        d->wait->setDocument(nullptr, nullptr);
    }
    if (d->loading != nullptr)
        d->loading->Close();
    if (d->game != nullptr)
        d->game->Close();
    if (d->waitDocument != nullptr)
        d->waitDocument->Close();
    // The documents go at the context's next update; the wait's model after
    // them.
    if (d->wait != nullptr && d->screen != nullptr && d->screen->context() != nullptr)
        d->screen->context()->Update();
    d->wait.reset();
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
    // The wait's model first: its document binds it as it loads.
    d->wait = std::make_unique<Wait>(*context, view);
    {
        Script::State::Deadline deadline(runtime->lua());
        d->loading = context->LoadDocument((runtime->folder() + "/loading.rml").toStdString());
        d->game = context->LoadDocument((runtime->folder() + "/explore-game.rml").toStdString());
        if (d->wait->usable())
            d->waitDocument = context->LoadDocument((runtime->folder() + "/explore-wait.rml").toStdString());
    }
    if (d->waitDocument != nullptr) {
        // Closed: the walking character's pointer again (not under the
        // game menu).
        d->wait->setDocument(d->waitDocument, [this] {
            if (d->game == nullptr || !d->game->IsVisible())
                view.exploreResumeWalking();
        });
    } else {
        qWarning().noquote() << "[explore] the wait's document did not load:" << runtime->folder();
    }
    if (d->loading == nullptr || d->game == nullptr) {
        qWarning().noquote() << "[explore] the run's documents did not load:" << runtime->folder();
        return false;
    }
    Rml::ElementList buttons;
    d->game->GetElementsByTagName(buttons, "button");
    for (Rml::Element *button : buttons)
        d->listen(button, Rml::EventId::Mouseover, [this, button](Rml::Event &) {
            if (d->hoverArmed)
                button->Focus();
        });
    d->listen(d->game, Rml::EventId::Mousemove, [this](Rml::Event &) { d->hoverArmed = true; });
    d->listen(d->game->GetElementById("resume"), Rml::EventId::Click, [this](Rml::Event &) {
        if (d->game->IsVisible())
            d->game->Hide();
        // Walking: the pointer taken again (task explore 04).
        view.exploreResumeWalking();
    });
    // The wait in its place (task explore 06).
    d->listen(d->game->GetElementById("wait"), Rml::EventId::Click, [this](Rml::Event &) {
        if (d->game->IsVisible())
            d->game->Hide();
        if (!openWait())
            view.exploreResumeWalking();
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
    // Esc with the view's keys: the game menu, while a run is on; T while
    // walking, the wait.
    d->screen->setMenuHandler([this] { return showGameMenu(); });
    view.setExploreWaitKey([this] { return openWait(); });
    return true;
}

void Run::frame(Ui::Screen &screen) {
    d->screen = &screen;
    loadDocuments();
    if (d->wait != nullptr && onTrack())
        d->wait->frame();
    if (now == State::Running && d->walking != Data::Walking::None) {
        walkFrame();
        return;
    }
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
    d->walking = Data::Walking::None;
    d->probe.reset();
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
    // Its text outside ?: (lupdate misses a //% inside one).
    QString text;
    if (seconds >= 0)
        //% "Preparing the run: %1 s"
        text = qtTrId("ui.explore.preparing").arg(seconds);
    if (Rml::Element *status = d->loading->GetElementById("loading-status"))
        status->SetInnerRML(Rml::StringUtilities::EncodeRml(text.toStdString()));
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
    // A timetable's trains' comments for the wait's names (task explore 06).
    d->names = std::make_shared<Data::Names>();
    if (chosen.kind == RunChoice::Kind::Timetable) {
        QThreadPool::globalInstance()->start([names = d->names, root = Game::root, route = chosen.route,
                                              file = chosen.file] {
            for (const Lists::TrainInfo &train : Lists::timetableTrains(root, route, file))
                if (!train.comment.trimmed().isEmpty())
                    names->comments.insert(train.name, train.comment.trimmed());
            names->done = true;
        });
    } else {
        d->names->done = true;
    }
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
    if (chosen.walk) {
        if (!startWalking(activity, error)) {
            fail(error);
            return;
        }
        running = activity;
        now = State::Running;
        d->waiting.start();
        d->shownSeconds = -1;
        setLoadingStatus(0);
        qInfo().noquote() << QString("Explore: %1 on %2 started on foot at %3: the route in %4 ms, the run in %5 ms")
                                     .arg(activity->header != nullptr ? activity->header->name : activity->name,
                                          chosen.route, chosen.station)
                                     .arg(routeMs).arg(timer.elapsed() - routeMs);
        return;
    }
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
        // Walking (task explore 04): no player's train, each its own
        // driver; the train chosen is found by its name.
        set->playerTrain = chosen.walk ? QString() : chosen.train;
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
    // Walking with no train (task explore 04): the activity alone.
    if (chosen.walk && chosen.consist.isEmpty()) {
        const QString activityName = freeName("tsre-explore", [&](const QString &name) {
            return ActLib::GetAct(activities, name + ".act") >= 0 || QFile::exists(activities + "/" + name + ".act");
        });
        Activity *activity = ActLib::Act.value(ActLib::AddAct(activities, activityName + ".act", true));
        if (activity == nullptr) {
            why = qtTrId("ui.explore.error.explore");
            return nullptr;
        }
        Trk *trk = Game::currentRoute != nullptr ? Game::currentRoute->getTrk() : nullptr;
        activity->init(trk != nullptr ? trk->idName : chosen.route, qtTrId("ui.explore.explore.route"));
        activity->header->startTime[0] = chosen.time / 3600;
        activity->header->startTime[1] = chosen.time / 60 % 60;
        activity->header->startTime[2] = chosen.time % 60;
        activity->header->season = chosen.season;
        activity->header->weather = chosen.weather;
        return activity;
    }
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
    // Driven, no stops (the player stops where they like); walking (task
    // explore 04), a stop at every platform of its path, as the editor's new
    // service has them: the character boards it and gets off elsewhere.
    if (!chosen.walk)
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

bool Run::startWalking(Activity *activity, QString &why) {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    Sim::RunOptions options = Sim::RunOptions::Current(false);
    options.trainless = chosen.kind == RunChoice::Kind::Explore && chosen.consist.isEmpty();
    double startAt = chosen.time;
    // Exploring with a train: when it gets to the station, from a run of
    // it alone first; the walk 5 minutes before.
    if (chosen.kind == RunChoice::Kind::Explore && !options.trainless) {
        QElapsedTimer timer;
        timer.start();
        QString error;
        const std::shared_ptr<const Sim::SimulationInput> in = Sim::MakeRunInput(activity, options, &error);
        const double arrival = in != nullptr && Game::trackDB != nullptr
                ? walkArrival(in, *Game::trackDB, walkTrain(*in, QString()), chosen.station) : -1.0;
        qInfo().noquote() << QString("Explore: the train at %1 at %2 (found in %3 ms)")
                                     .arg(chosen.station).arg(arrival).arg(timer.elapsed());
        if (arrival < 0.0) {
            //% "The train does not call at %1 on this path."
            why = in == nullptr ? error : qtTrId("ui.explore.error.walk.path").arg(chosen.station);
            return false;
        }
        startAt = arrival - 300.0;
    }
    if (!simulation->start(activity, options, &why))
        return false;
    const std::shared_ptr<const Sim::SimulationInput> in = simulation->input();
    if (in == nullptr || Game::trackDB == nullptr) {
        why = qtTrId("ui.explore.error.explore");
        return false;
    }
    // The platform from the run's stops; the camera there now, so its
    // tiles load while the run goes on to its start.
    const int train = walkTrain(*in, chosen.kind == RunChoice::Kind::Timetable ? chosen.train : QString());
    d->walkStart = WalkStart();
    if (!walkPlatform(*in, *Game::trackDB, chosen.station, train, d->walkStart)) {
        //% "%1 has no platform in the route's track database."
        why = qtTrId("ui.explore.error.walk.platform").arg(chosen.station);
        return false;
    }
    qInfo().noquote() << QString("Explore: walking on %1 at %2 (%3)")
                                 .arg(d->walkStart.platformName, d->walkStart.station)
                                 .arg(d->walkStart.why == WalkStart::Why::TrainStop ? "the train's stop"
                                      : d->walkStart.why == WalkStart::Why::Busiest ? "the busiest platform"
                                                                                     : "the longest platform");
    view.jumpTo(d->walkStart.tile[0], d->walkStart.tile[1], d->walkStart.at[0], d->walkStart.at[1],
                d->walkStart.at[2]);
    d->probe = std::make_unique<Characters::RouteCollision>();
    // Before the run's start, no earlier than it (its first train's, an
    // activity's own).
    d->runUpTo = std::max(startAt, double(in->startTime));
    if (d->runUpTo > in->startTime + 0.5) {
        if (!simulation->runTo(activity, int(std::lround(d->runUpTo)), &why))
            return false;
        d->walking = Data::Walking::RunningUp;
    } else {
        d->walking = Data::Walking::Placing;
        d->placing.start();
    }
    return true;
}

void Run::walkFrame() {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    setLoadingStatus(int(d->waiting.elapsed() / 1000));
    if (!simulation->exists()) {
        fail(qtTrId("ui.explore.error.explore"));
        return;
    }
    if (d->walking == Data::Walking::RunningUp) {
        const std::shared_ptr<const Sim::SimulationFrame> frame = simulation->frame();
        if (simulation->runs() || frame == nullptr || frame->time < d->runUpTo - 1.0)
            return;
        qInfo().noquote() << QString("Explore: the run at its start %1 after %2 ms")
                                     .arg(frame->time).arg(d->waiting.elapsed());
        d->walking = Data::Walking::Placing;
        d->placing.start();
    }
    // The platform's objects read (30 s at most), the character on it.
    view.exploreUseShapes();
    int tile[2];
    float at[3], yaw = 0.0f;
    const bool patient = d->placing.elapsed() < 30000;
    const WalkPlace found = walkPlace(d->walkStart, *d->probe, patient, tile, at, yaw);
    if (found == WalkPlace::Waiting || (found == WalkPlace::None && patient))
        return;
    if (found == WalkPlace::None || !view.exploreWalkFrom(tile, at, yaw)) {
        if (patient)
            return;
        //% "No place to stand at %1."
        fail(qtTrId("ui.explore.error.walk.place").arg(chosen.station));
        return;
    }
    d->walking = Data::Walking::None;
    d->probe.reset();
    QString error;
    if (!simulation->play(running, &error)) {
        fail(error);
        return;
    }
    qInfo().noquote() << QString("Explore: walking at %1 %2 ms after the start").arg(chosen.station)
                                 .arg(d->waiting.elapsed());
    hideLoading();
}

void Run::leave() {
    if (now == State::None)
        return;
    if (d->wait != nullptr)
        d->wait->reset();
    d->waitingForTrain = false;
    d->walking = Data::Walking::None;
    d->probe.reset();
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
    return now == State::Running && !d->waitingForTrain && d->walking == Data::Walking::None;
}

bool Run::showGameMenu() {
    if (now != State::Running || d->screen == nullptr || d->game == nullptr)
        return false;
    d->hoverArmed = false;
    // Wait only while walking (task explore 06).
    if (Rml::Element *wait = d->game->GetElementById("wait")) {
        if (canWait())
            wait->RemoveProperty("display");
        else
            wait->SetProperty("display", "none");
    }
    d->screen->showMenu(d->game);
    // Resume first, whatever the pointer rests over.
    if (Rml::Element *resume = d->game->GetElementById("resume"))
        resume->Focus(true);
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

bool Run::canWait() const {
    return onTrack() && d->wait != nullptr && d->waitDocument != nullptr && view.exploreWalker() != nullptr
            && Sim::SimulationController::instance()->driven() < 0;
}

bool Run::openWait() {
    if (!canWait() || d->screen == nullptr)
        return false;
    if (d->wait->shown())
        return true;
    std::shared_ptr<Data::Names> names = d->names;
    d->wait->setRun(running, [names](const QString &service) {
        return names != nullptr && names->done ? names->comments.value(service) : QString();
    });
    return d->wait->open(*d->screen);
}

Wait *Run::wait() const {
    return d->wait.get();
}

}
