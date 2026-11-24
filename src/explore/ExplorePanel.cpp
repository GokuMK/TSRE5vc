/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <explore/ExplorePanel.h>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>

#include <RmlUi/Core.h>
#include <QCollator>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QThreadPool>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace Explore {

namespace {

// A list's row: a name, a note after it, or a heading between rows (the
// routes, the picker's values).
struct Row {
    Rml::String name;
    Rml::String note;
    bool heading = false;
};

// A timetable train's row in the table.
struct TrainRow {
    Rml::String name;
    Rml::String start;
    Rml::String end;
    Rml::String from;
    Rml::String to;
    Rml::String category;
    Rml::String number;
    Rml::String line;
    Rml::String engine;
    Rml::String stops;
    bool drivable = false;
};

// The activities page's rows (task explore 05).
struct ActivityRow {
    Rml::String name;
    Rml::String difficulty;
    Rml::String duration;
};

// An activity's player's stop.
struct StopRow {
    Rml::String station;
    Rml::String arrival;
    Rml::String departure;
};

// A timetable train's call: a stop, or a pass (dimmed).
struct CallRow {
    Rml::String text;
    bool pass = false;
};

// The explore page's rows: a player path, a consist with its engine.
struct PathRow {
    Rml::String start;
    Rml::String end;
    Rml::String name;
};

struct ConsistRow {
    Rml::String name;
    Rml::String engine;
};

Rml::String rml(const QString &text) {
    return text.toStdString();
}

QString clock(int seconds) {
    if (seconds < 0)
        return QString();
    return QString("%1:%2").arg(seconds / 3600 % 24, 2, 10, QChar('0')).arg(seconds / 60 % 60, 2, 10, QChar('0'));
}

QString seasonName(int season) {
    switch (season) {
    case 0:
        //% "Spring"
        return qtTrId("ui.explore.season.spring");
    case 1:
        //% "Summer"
        return qtTrId("ui.explore.season.summer");
    case 2:
        //% "Autumn"
        return qtTrId("ui.explore.season.autumn");
    case 3:
        //% "Winter"
        return qtTrId("ui.explore.season.winter");
    }
    return QString();
}

// MSTS's weather: 0 clear, 1 snow, 2 rain.
QString weatherName(int weather) {
    switch (weather) {
    case 0:
        //% "Clear"
        return qtTrId("ui.explore.weather.clear");
    case 1:
        //% "Snow"
        return qtTrId("ui.explore.weather.snow");
    case 2:
        //% "Rain"
        return qtTrId("ui.explore.weather.rain");
    }
    return QString();
}

QString difficultyName(int difficulty) {
    switch (difficulty) {
    case 0:
        //% "Easy"
        return qtTrId("ui.explore.difficulty.easy");
    case 1:
        //% "Medium"
        return qtTrId("ui.explore.difficulty.medium");
    case 2:
        //% "Hard"
        return qtTrId("ui.explore.difficulty.hard");
    }
    return QString();
}

// An activity's duration: "2 h 10 min", "45 min".
QString durationText(int minutes) {
    if (minutes <= 0)
        return QString();
    if (minutes < 60)
        //% "%1 min"
        return qtTrId("ui.explore.minutes.only").arg(minutes);
    //% "%1 h %2 min"
    return qtTrId("ui.explore.hours.minutes").arg(minutes / 60).arg(minutes % 60);
}

QString monthName(int month) {
    return QLocale().standaloneMonthName(month, QLocale::LongFormat);
}

QString dayText(const QDate &day) {
    return QString("%1 %2").arg(day.day()).arg(monthName(day.month()));
}

// The clock's date at first: the setting's (2026-06-21 by default).
QDate clockDate() {
    const QDate date = QDate::fromString(Game::timeOfDayDate.trimmed(), "yyyy-MM-dd");
    return date.isValid() ? date : QDate(2026, 6, 21);
}

// The route's facts (task explore 05): its units, electrified or not, its
// speed limit in its units.
QString routeFacts(const Lists::RouteInfo &r) {
    QStringList facts;
    //% "Kilometres"
    facts << (r.metric ? qtTrId("ui.explore.kilometres")
                       //% "Miles"
                       : qtTrId("ui.explore.miles"));
    if (r.electrified && r.lineVoltage >= 1000.0f)
        //% "Electrified, %1 kV"
        facts << qtTrId("ui.explore.electrified.kv").arg(QLocale().toString(double(r.lineVoltage) / 1000.0, 'g', 3));
    else if (r.electrified && r.lineVoltage > 0.0f)
        //% "Electrified, %1 V"
        facts << qtTrId("ui.explore.electrified.v").arg(int(std::lround(r.lineVoltage)));
    else if (r.electrified)
        //% "Electrified"
        facts << qtTrId("ui.explore.electrified");
    else
        //% "Not electrified"
        facts << qtTrId("ui.explore.not.electrified");
    if (r.speedLimit > 0.0f) {
        const long speed = std::lround(r.speedLimit * (r.metric ? 3.6f : 2.2369363f));
        //% "Speed limit %1 km/h"
        facts << (r.metric ? qtTrId("ui.explore.speed.limit.kmh").arg(speed)
                           //% "Speed limit %1 mph"
                           : qtTrId("ui.explore.speed.limit.mph").arg(speed));
    }
    return facts.join(QStringLiteral(" · "));
}

// The weathers as the picker lists them.
constexpr int Weathers[3] = {0, 2, 1};

QString &rememberedOverride() {
    static QString file;
    return file;
}

}

struct Panel::Data {
    Rml::Context &context;
    QString root;
    QString firstRoute;
    std::function<void(const RunChoice &)> start;
    Rml::ElementDocument *document = nullptr;
    Rml::DataModelHandle handle;
    bool made = false;
    bool opened = false;

    // The lists (kept for the session: the routes and the consists).
    QVector<Lists::RouteInfo> routes;
    QVector<Lists::ActivityInfo> activities;
    QVector<Lists::TimetableSetInfo> sets;
    QVector<Lists::PathInfo> paths;
    QVector<Lists::TrainInfo> trains;
    QVector<int> shownTrains;
    // The set the trains are of (route folder and file).
    QString trainsOf;
    struct ConsistJob {
        std::atomic<bool> done{false};
        QVector<Lists::ConsistInfo> list;
        qint64 ms = 0;
    };
    std::shared_ptr<ConsistJob> consistJob;
    QVector<Lists::ConsistInfo> consists;
    bool consistsDone = false;
    // The routes' stations (task explore 04), read on a worker when a route
    // is chosen (its count; walking), kept by route folder.
    struct StationsJob {
        std::atomic<bool> done{false};
        Lists::RouteStations stations;
        qint64 ms = 0;
    };
    QHash<QString, std::shared_ptr<StationsJob>> stationsJobs;
    QHash<QString, Lists::RouteStations> routeStations;
    // A timetable set's station rows.
    QStringList setStations;

    // The pages (task explore 05).
    enum class Page { Route, Explore, Activity, Timetable };
    static QString pageName(Page page) {
        switch (page) {
        case Page::Explore: return "explore";
        case Page::Activity: return "activity";
        case Page::Timetable: return "timetable";
        default: return "route";
        }
    }
    Page page = Page::Route;
    // The page whose button the route page focuses: the one last used on
    // the route (the remembered run's at first).
    Page lastPage = Page::Route;

    // The choices.
    int route = -1;
    int activity = -1;
    int set = -1;
    QString startName;
    QString path;
    QString loco;
    QString consist;
    int time = 12 * 3600;
    QDate day;
    int weather = 0;
    QString train;
    QString sortColumn;
    bool sortUp = true;
    QString filter;
    bool drivableOnly = true;
    QString status;
    // The explore page's lists: their filters and the rows shown.
    QString pathFilter;
    QString consistFilter;
    QVector<int> shownPaths;
    QVector<int> shownConsists;
    // Walking (task explore 04): the station; exploring with its train or
    // none.
    bool walk = false;
    QString station;
    bool exploreTrain = true;

    // The picker: what it chooses, its values by row.
    QString picking;
    QStringList pickerValues;
    // An element to focus once the model made it: by id, a column's
    // ("column:<name>"), or the picker's chosen row.
    QString focusWanted;
    int focusTries = 0;

    // Remembered in the profile (explore.json).
    QJsonObject remembered;

    // The model's variables.
    Rml::Vector<Row> mRoutes, mOptions;
    Rml::Vector<TrainRow> mTrains;
    Rml::Vector<ActivityRow> mActivities;
    Rml::Vector<StopRow> mStops;
    Rml::Vector<CallRow> mCalls;
    Rml::Vector<PathRow> mPaths;
    Rml::Vector<ConsistRow> mConsists;
    int mRoute = -1, mActivity = -1, mTrain = -1, mOption = -1, mPathRow = -1, mConsistRow = -1;
    int mActivityCount = 0, mSetCount = 0;
    Rml::String mPage, mPageTitle, mRouteName, mRouteDescription, mPicture, mRouteFacts, mRouteCounts;
    Rml::String mTime, mDay, mSeason, mWeather;
    Rml::String mActName, mActFacts, mActDescription, mActBriefing, mActTrain, mActPath, mActTraffic,
            mActSeasonWeather;
    Rml::String mSetName, mSetTrains, mTrainComment, mTrainBriefing, mTrainDetails;
    Rml::String mPathNote, mConsistNote;
    Rml::String mFilter, mPathFilter, mConsistFilter, mSortColumn, mPicking, mPickerTitle, mStatus, mStation;
    bool mSortUp = true, mDrivableOnly = true, mReady = false, mConsistsLoading = false, mManySets = false;
    bool mWalk = false, mStationsLoading = false, mNoTrain = false, mExploreTrain = true, mShowTime = false;
    bool mTrainChosen = false;
    // The #tsre columns a set's trains have values in (none: not shown).
    bool mHasCategory = false, mHasNumber = false, mHasLine = false;

    Data(Rml::Context &context) : context(context) {}

    QString rememberedFile() const {
        if (!rememberedOverride().isEmpty())
            return rememberedOverride();
        return QDir(SettingsManager::instance().profileDirectory()).filePath("explore.json");
    }
    void loadRemembered() {
        QFile file(rememberedFile());
        if (file.open(QIODevice::ReadOnly))
            remembered = QJsonDocument::fromJson(file.readAll()).object();
    }
    void saveRemembered() {
        QSaveFile file(rememberedFile());
        if (!file.open(QIODevice::WriteOnly))
            return;
        file.write(QJsonDocument(remembered).toJson());
        file.commit();
    }

    const Lists::RouteInfo *chosenRoute() const {
        return route >= 0 && route < routes.size() ? &routes[route] : nullptr;
    }
    const Lists::ActivityInfo *chosenActivity() const {
        return activity >= 0 && activity < activities.size() ? &activities[activity] : nullptr;
    }
    const Lists::TimetableSetInfo *chosenSet() const {
        return set >= 0 && set < sets.size() ? &sets[set] : nullptr;
    }
    double latitude() const {
        const Lists::RouteInfo *r = chosenRoute();
        return r != nullptr ? r->latitude : 0.0;
    }
    const Lists::PathInfo *chosenPath() const {
        for (const Lists::PathInfo &p : paths)
            if (p.file == path)
                return &p;
        return nullptr;
    }
    const Lists::ConsistInfo *chosenConsist() const {
        for (const Lists::ConsistInfo &c : consists)
            if (c.file == consist)
                return &c;
        return nullptr;
    }
    const Lists::TrainInfo *chosenTrain() const {
        for (const Lists::TrainInfo &t : trains)
            if (t.name == train)
                return &t;
        return nullptr;
    }
    // The run a page starts, as remembered ("explore", "activity:<file>",
    // "timetable:<file>"); empty on the route page.
    QString runKey() const {
        switch (page) {
        case Page::Explore:
            return "explore";
        case Page::Activity:
            return chosenActivity() != nullptr ? "activity:" + chosenActivity()->file : QString();
        case Page::Timetable:
            return chosenSet() != nullptr ? "timetable:" + chosenSet()->file : QString();
        default:
            return QString();
        }
    }
    // The page opened with the run last started on this route.
    bool remembersThisRun() const {
        const Lists::RouteInfo *r = chosenRoute();
        return r != nullptr && remembered.value("route").toString() == r->folder
                && !runKey().isEmpty() && remembered.value("run").toString() == runKey();
    }

    // Walking (task explore 04). The route's stations once read (null
    // while not).
    const Lists::RouteStations *stationsOfRoute() const {
        const Lists::RouteInfo *r = chosenRoute();
        if (r == nullptr)
            return nullptr;
        const auto found = routeStations.constFind(r->folder);
        return found != routeStations.constEnd() ? &*found : nullptr;
    }
    // The stations a walk may start at: a station and its times (a note).
    struct StationChoice {
        QString name;
        QString note;
        // The time walking from there starts (seconds after midnight; -1:
        // the time chosen stays).
        int start = -1;
    };
    static QString timesNote(int arrival, int departure, int passing) {
        if (arrival >= 0 && departure >= 0 && clock(arrival) != clock(departure))
            return clock(arrival) + " - " + clock(departure);
        if (arrival >= 0 || departure >= 0)
            return clock(arrival >= 0 ? arrival : departure);
        //% "passes %1"
        return passing >= 0 ? qtTrId("ui.explore.passes").arg(clock(passing)) : QString();
    }
    // 5 minutes before a train calls (the user, 2026-11-20).
    static constexpr int Before = 300;
    QVector<StationChoice> stationChoices() const {
        QVector<StationChoice> list;
        const auto has = [&list](const QString &name) {
            for (const StationChoice &c : list)
                if (c.name.compare(name, Qt::CaseInsensitive) == 0)
                    return true;
            return false;
        };
        switch (page) {
        case Page::Timetable:
            if (const Lists::TrainInfo *t = chosenTrain()) {
                for (int i = 0; i < t->times.size(); ++i) {
                    const Lists::TrainInfo::Time &time = t->times[i];
                    if (has(time.station))
                        continue;
                    // Its first: before it leaves; else before it comes.
                    const int calls = i == 0 && time.departure >= 0 ? time.departure
                                    : time.arrival >= 0 ? time.arrival
                                    : time.departure >= 0 ? time.departure : time.passing;
                    list.push_back({time.station, timesNote(time.arrival, time.departure, time.passing),
                                    calls >= 0 ? std::max(0, calls - Before) : -1});
                }
            } else {
                for (const QString &name : setStations)
                    list.push_back({name, {}, -1});
            }
            break;
        case Page::Activity: {
            const Lists::RouteStations *route = stationsOfRoute();
            const Lists::ActivityInfo *a = chosenActivity();
            if (route == nullptr || a == nullptr)
                break;
            for (int i = 0; i < a->playerStops.size(); ++i) {
                const Lists::ActivityInfo::Stop &stop = a->playerStops[i];
                const QString name = route->ofItem.value(stop.platform);
                if (name.isEmpty() || has(name))
                    continue;
                const int calls = i == 0 && stop.departure >= 0 ? stop.departure
                                : stop.arrival >= 0 ? stop.arrival : stop.departure;
                list.push_back({name, timesNote(stop.arrival, stop.departure, -1),
                                calls >= 0 ? std::max(a->startTime, calls - Before) : a->startTime});
            }
            for (const QString &name : route->names)
                if (!has(name))
                    list.push_back({name, {}, a->startTime});
            break;
        }
        case Page::Explore:
            if (const Lists::RouteStations *route = stationsOfRoute())
                for (const QString &name : route->names)
                    list.push_back({name, {}, -1});
            break;
        default:
            break;
        }
        return list;
    }
    const StationChoice *chosenStation(const QVector<StationChoice> &list) const {
        for (const StationChoice &c : list)
            if (c.name.compare(station, Qt::CaseInsensitive) == 0)
                return &c;
        return nullptr;
    }
    // The station kept when it is in the list, else the remembered one,
    // else (exploring) the path's start, else the first; its time taken.
    void chooseStationFor(bool takeTime) {
        const QVector<StationChoice> list = stationChoices();
        if (list.isEmpty())
            return;
        if (chosenStation(list) == nullptr) {
            const QString kept = station;
            station = remembered.value("station").toString();
            if (chosenStation(list) == nullptr && page == Page::Explore && chosenPath() != nullptr)
                station = chosenPath()->start;
            if (chosenStation(list) == nullptr)
                station = list.first().name;
            takeTime = takeTime || station != kept;
        }
        const StationChoice *c = chosenStation(list);
        station = c->name;
        if (takeTime && c->start >= 0)
            time = c->start;
    }
    // Reads the route's stations on a worker, once for the session.
    void readRouteStations() {
        const Lists::RouteInfo *r = chosenRoute();
        if (r == nullptr || routeStations.contains(r->folder) || stationsJobs.contains(r->folder))
            return;
        auto job = std::make_shared<StationsJob>();
        stationsJobs.insert(r->folder, job);
        // Never std::thread or std::async (Qt objects on MinGW).
        QThreadPool::globalInstance()->start([job, root = root, folder = r->folder] {
            QElapsedTimer timer;
            timer.start();
            job->stations = Lists::routeStations(root, folder);
            job->ms = timer.elapsed();
            job->done = true;
        });
    }
    void takeRouteStations() {
        bool taken = false;
        for (auto it = stationsJobs.begin(); it != stationsJobs.end();) {
            if (!it.value()->done) {
                ++it;
                continue;
            }
            qInfo().noquote() << QString("Explore: %1: %2 stations (%3 platform items) read in %4 ms")
                                         .arg(it.key()).arg(it.value()->stations.names.size())
                                         .arg(it.value()->stations.ofItem.size()).arg(it.value()->ms);
            routeStations.insert(it.key(), it.value()->stations);
            it = stationsJobs.erase(it);
            taken = true;
        }
        if (taken) {
            if (walk)
                chooseStationFor(false);
            refresh();
        }
    }
    bool needsRouteStations() const {
        return walk && (page == Page::Activity || page == Page::Explore);
    }
    // Exploring with its train: walking may go without one.
    bool exploresWithTrain() const {
        return !walk || exploreTrain;
    }

    bool ready() const {
        const bool stationReady = !walk || chosenStation(stationChoices()) != nullptr;
        switch (page) {
        case Page::Explore:
            return stationReady && (!exploresWithTrain() || (chosenPath() != nullptr && chosenConsist() != nullptr));
        case Page::Activity:
            return stationReady && chosenActivity() != nullptr;
        case Page::Timetable: {
            const Lists::TrainInfo *t = chosenTrain();
            return walk ? stationReady : t != nullptr && t->drivable;
        }
        default:
            return false;
        }
    }

    void setWalk(bool on) {
        walk = on;
        status.clear();
        if (needsRouteStations())
            readRouteStations();
        filterTrains();
        if (walk)
            chooseStationFor(true);
        refresh();
    }

    void readRoutes() {
        QElapsedTimer timer;
        timer.start();
        routes = Lists::routes(root);
        qInfo().noquote() << QString("Explore: %1 routes read in %2 ms").arg(routes.size()).arg(timer.elapsed());
    }

    void readConsists() {
        if (consistJob)
            return;
        consistJob = std::make_shared<ConsistJob>();
        // Never std::thread or std::async (Qt objects on MinGW).
        QThreadPool::globalInstance()->start([job = consistJob, root = root] {
            QElapsedTimer timer;
            timer.start();
            job->list = Lists::drivableConsists(root);
            job->ms = timer.elapsed();
            job->done = true;
        });
    }

    void takeConsists() {
        if (consistsDone || !consistJob || !consistJob->done)
            return;
        consists = consistJob->list;
        consistsDone = true;
        qInfo().noquote() << QString("Explore: %1 consists with an engine read in %2 ms")
                                     .arg(consists.size()).arg(consistJob->ms);
        // The remembered consist, else the first one.
        const QString kept = remembered.value("consist").toString();
        consist.clear();
        for (const Lists::ConsistInfo &c : consists)
            if (c.file == kept)
                consist = c.file;
        if (consist.isEmpty() && !consists.isEmpty())
            consist = consists.first().file;
        if (const Lists::ConsistInfo *c = chosenConsist())
            loco = c->engineName;
        filterConsists();
        refresh();
    }

    // The keys on to a column once the model made it (Enter on a route goes
    // to its page's buttons, on a page's button into the page).
    void focusLater(const QString &what) {
        focusWanted = what;
        focusTries = 0;
    }

    void chooseRoute(int index) {
        if (index < 0 || index >= routes.size())
            return;
        route = index;
        page = Page::Route;
        status.clear();
        const Lists::RouteInfo &r = routes[index];
        QElapsedTimer timer;
        timer.start();
        activities = Lists::activities(root, r);
        sets = Lists::timetableSets(root, r.folder);
        paths = Lists::playerPaths(root, r.folder);
        qInfo().noquote() << QString("Explore: %1: %2 activities, %3 timetable sets, %4 player paths read in %5 ms")
                                     .arg(r.folder).arg(activities.size()).arg(sets.size()).arg(paths.size())
                                     .arg(timer.elapsed());
        activity = activities.isEmpty() ? -1 : 0;
        set = sets.isEmpty() ? -1 : 0;
        lastPage = Page::Route;
        trains.clear();
        shownTrains.clear();
        trainsOf.clear();
        train.clear();
        setStations.clear();
        // The remembered path, else the first one.
        const QString keptPath = remembered.value("path").toString();
        path.clear();
        for (const Lists::PathInfo &p : paths)
            if (p.file == keptPath)
                path = p.file;
        if (path.isEmpty() && !paths.isEmpty())
            path = paths.first().file;
        startName = chosenPath() != nullptr ? chosenPath()->start : QString();
        pathFilter.clear();
        filterPaths();
        // The remembered run of this route: its page's button, its activity
        // or set.
        if (remembered.value("route").toString() == r.folder) {
            const QString kept = remembered.value("run").toString();
            if (kept == "explore")
                lastPage = Page::Explore;
            for (int i = 0; i < activities.size(); ++i)
                if (kept == "activity:" + activities[i].file) {
                    activity = i;
                    lastPage = Page::Activity;
                }
            for (int i = 0; i < sets.size(); ++i)
                if (kept == "timetable:" + sets[i].file) {
                    set = i;
                    lastPage = Page::Timetable;
                }
        }
        // Its stations: their count, and walking.
        readRouteStations();
        refresh();
    }

    // The chosen set's trains, read once while it stays chosen.
    void loadSet() {
        const Lists::RouteInfo *r = chosenRoute();
        const Lists::TimetableSetInfo *s = chosenSet();
        if (r == nullptr || s == nullptr) {
            trains.clear();
            shownTrains.clear();
            trainsOf.clear();
            return;
        }
        const QString key = r->folder + "/" + s->file;
        if (trainsOf == key)
            return;
        QElapsedTimer timer;
        timer.start();
        trains = Lists::timetableTrains(root, r->folder, s->file, &setStations);
        trainsOf = key;
        qInfo().noquote() << QString("Explore: %1: %2 trains, %3 stations read in %4 ms")
                                     .arg(s->file).arg(trains.size()).arg(setStations.size()).arg(timer.elapsed());
        train = remembered.value("trains").toObject().value(s->file).toString();
        if (chosenTrain() == nullptr)
            train.clear();
        filterTrains();
    }

    // A run page (task explore 05); false when the route has none of it.
    bool openPage(Page wanted) {
        if (chosenRoute() == nullptr || wanted == Page::Route)
            return false;
        if ((wanted == Page::Activity && activities.isEmpty()) || (wanted == Page::Timetable && sets.isEmpty()))
            return false;
        page = wanted;
        status.clear();
        // The consists: the explore page's list, an activity's train's
        // engine.
        if (page == Page::Explore || page == Page::Activity)
            readConsists();
        if (page == Page::Activity && activity < 0)
            activity = 0;
        if (page == Page::Timetable) {
            if (set < 0)
                set = 0;
            loadSet();
        }
        if (needsRouteStations())
            readRouteStations();
        if (walk)
            chooseStationFor(true);
        refresh();
        return true;
    }

    // Back to the route page, the button of the page left focused.
    bool back() {
        if (page == Page::Route)
            return false;
        closePicker();
        lastPage = page;
        page = Page::Route;
        status.clear();
        focusLater("open-" + pageName(lastPage));
        refresh();
        return true;
    }

    void filterTrains() {
        shownTrains.clear();
        const QString text = filter.trimmed();
        for (int i = 0; i < trains.size(); ++i) {
            const Lists::TrainInfo &t = trains[i];
            // Walking, any train may be watched or ridden.
            if (drivableOnly && !walk && !t.drivable)
                continue;
            if (!text.isEmpty() && !t.name.contains(text, Qt::CaseInsensitive)
                    && !t.number.contains(text, Qt::CaseInsensitive) && !t.from.contains(text, Qt::CaseInsensitive)
                    && !t.to.contains(text, Qt::CaseInsensitive) && !t.comment.contains(text, Qt::CaseInsensitive))
                continue;
            shownTrains << i;
        }
        if (!sortColumn.isEmpty()) {
            QCollator collator;
            collator.setNumericMode(true);
            collator.setCaseSensitivity(Qt::CaseInsensitive);
            const auto key = [this](const Lists::TrainInfo &t) -> QString {
                const auto time = [](int seconds) {
                    return QString("%1").arg(seconds < 0 ? 999999 : seconds, 6, 10, QChar('0'));
                };
                if (sortColumn == "start")
                    return time(t.startTime);
                if (sortColumn == "end")
                    return time(t.endTime);
                if (sortColumn == "stops")
                    return QString("%1").arg(t.stops, 4, 10, QChar('0'));
                if (sortColumn == "from")
                    return t.from;
                if (sortColumn == "to")
                    return t.to;
                if (sortColumn == "category")
                    return t.category;
                if (sortColumn == "number")
                    return t.number;
                if (sortColumn == "line")
                    return t.line;
                if (sortColumn == "engine")
                    return t.engine;
                return t.name;
            };
            std::stable_sort(shownTrains.begin(), shownTrains.end(), [&](int a, int b) {
                const int c = collator.compare(key(trains[a]), key(trains[b]));
                return sortUp ? c < 0 : c > 0;
            });
        }
    }

    // The explore page's lists by their filters: text in a path's start,
    // end, name or file; in a consist's name, engine or file.
    void filterPaths() {
        shownPaths.clear();
        const QString text = pathFilter.trimmed();
        for (int i = 0; i < paths.size(); ++i) {
            const Lists::PathInfo &p = paths[i];
            if (text.isEmpty() || p.start.contains(text, Qt::CaseInsensitive) || p.end.contains(text, Qt::CaseInsensitive)
                    || p.name.contains(text, Qt::CaseInsensitive) || p.file.contains(text, Qt::CaseInsensitive))
                shownPaths << i;
        }
    }
    void filterConsists() {
        shownConsists.clear();
        const QString text = consistFilter.trimmed();
        for (int i = 0; i < consists.size(); ++i) {
            const Lists::ConsistInfo &c = consists[i];
            if (text.isEmpty() || c.name.contains(text, Qt::CaseInsensitive)
                    || c.engineName.contains(text, Qt::CaseInsensitive) || c.file.contains(text, Qt::CaseInsensitive))
                shownConsists << i;
        }
    }

    // The picker for a choice: its title, rows and the row chosen now.
    void openPicker(const QString &what) {
        QVector<Row> rows;
        QStringList values;
        int chosen = -1;
        QString title;
        if (what == "station") {
            //% "Station"
            title = qtTrId("ui.explore.station");
            for (const StationChoice &c : stationChoices()) {
                if (c.name.compare(station, Qt::CaseInsensitive) == 0)
                    chosen = values.size();
                rows.push_back({rml(c.name), rml(c.note), false});
                values << c.name;
            }
        } else if (what == "set") {
            //% "Timetable"
            title = qtTrId("ui.explore.timetable");
            for (int i = 0; i < sets.size(); ++i) {
                if (i == set)
                    chosen = i;
                rows.push_back({rml(sets[i].name), {}, false});
                values << QString::number(i);
            }
        } else if (what == "hour") {
            //% "Time"
            title = qtTrId("ui.explore.time");
            for (int h = 0; h < 24; ++h) {
                if (h == time / 3600)
                    chosen = h;
                rows.push_back({rml(QString("%1").arg(h, 2, 10, QChar('0'))), {}, false});
                values << QString::number(h);
            }
        } else if (what == "minute") {
            title = qtTrId("ui.explore.time");
            for (int m = 0; m < 60; m += 5) {
                if (m == time / 60 % 60 / 5 * 5)
                    chosen = values.size();
                rows.push_back({rml(QString("%1").arg(m, 2, 10, QChar('0'))), {}, false});
                values << QString::number(m);
            }
        } else if (what == "day") {
            //% "Day"
            title = qtTrId("ui.explore.day");
            for (int n = 1; n <= day.daysInMonth(); ++n) {
                if (n == day.day())
                    chosen = n - 1;
                rows.push_back({rml(QString::number(n)), {}, false});
                values << QString::number(n);
            }
        } else if (what == "month") {
            title = qtTrId("ui.explore.day");
            for (int m = 1; m <= 12; ++m) {
                if (m == day.month())
                    chosen = m - 1;
                rows.push_back({rml(monthName(m)), {}, false});
                values << QString::number(m);
            }
        } else if (what == "weather") {
            //% "Weather"
            title = qtTrId("ui.explore.weather");
            for (int w : Weathers) {
                if (w == weather)
                    chosen = values.size();
                rows.push_back({rml(weatherName(w)), {}, false});
                values << QString::number(w);
            }
        } else {
            return;
        }
        if (values.isEmpty())
            return;
        picking = what;
        pickerValues = values;
        mOptions.assign(rows.begin(), rows.end());
        mOption = chosen;
        mPickerTitle = rml(title);
        focusLater("picker");
        refresh();
    }

    void pickOption(int index) {
        if (picking.isEmpty() || index < 0 || index >= pickerValues.size())
            return;
        const QString value = pickerValues[index];
        if (picking == "station") {
            station = value;
            chooseStationFor(true);
        } else if (picking == "set") {
            set = value.toInt();
            loadSet();
            if (walk)
                chooseStationFor(true);
        } else if (picking == "hour") {
            time = value.toInt() * 3600 + time % 3600;
        } else if (picking == "minute") {
            time = time / 3600 * 3600 + value.toInt() * 60;
        } else if (picking == "day") {
            day = QDate(day.year(), day.month(), value.toInt());
        } else if (picking == "month") {
            const int month = value.toInt();
            day = QDate(day.year(), month, std::min(day.day(), QDate(day.year(), month, 1).daysInMonth()));
        } else if (picking == "weather") {
            weather = value.toInt();
        }
        status.clear();
        closePicker();
    }

    void closePicker() {
        if (picking.isEmpty())
            return;
        focusLater("pick-" + picking);
        picking.clear();
        pickerValues.clear();
        mOptions.clear();
        mOption = -1;
        refresh();
    }

    // An activity's player's train: its consist (with its engine once the
    // consists are read) and its path's ends.
    QString activityTrain(const Lists::ActivityInfo &a) const {
        for (const Lists::ConsistInfo &c : consists)
            if (QFileInfo(c.file).completeBaseName().compare(a.consist, Qt::CaseInsensitive) == 0)
                return c.engineName.isEmpty() || c.engineName == c.name ? c.name : c.name + " (" + c.engineName + ")";
        return a.consist;
    }
    QString activityPath(const Lists::ActivityInfo &a) const {
        for (const Lists::PathInfo &p : paths)
            if (QFileInfo(p.file).completeBaseName().compare(a.path, Qt::CaseInsensitive) == 0
                    || p.name.compare(a.path, Qt::CaseInsensitive) == 0)
                return p.start + " - " + p.end;
        return a.path;
    }

    // The model's variables from the choices.
    void refresh() {
        mRoutes.clear();
        for (const Lists::RouteInfo &r : routes)
            mRoutes.push_back({rml(r.name), {}, false});
        mRoute = route;
        const Lists::RouteInfo *r = chosenRoute();
        mRouteName = r != nullptr ? rml(r->name) : Rml::String();
        mRouteDescription = r != nullptr ? rml(r->description) : Rml::String();
        mPicture = r != nullptr ? rml(r->picture) : Rml::String();
        mRouteFacts = r != nullptr ? rml(routeFacts(*r)) : Rml::String();
        {
            QStringList counts;
            if (r != nullptr) {
                //% "Activities: %1"
                counts << qtTrId("ui.explore.count.activities").arg(activities.size());
                //% "Timetables: %1"
                counts << qtTrId("ui.explore.count.timetables").arg(sets.size());
                //% "Player paths: %1"
                counts << qtTrId("ui.explore.count.paths").arg(paths.size());
                if (const Lists::RouteStations *s = stationsOfRoute())
                    //% "Stations: %1"
                    counts << qtTrId("ui.explore.count.stations").arg(s->names.size());
            }
            mRouteCounts = rml(counts.join(QStringLiteral(" · ")));
        }
        mActivityCount = int(activities.size());
        mSetCount = int(sets.size());
        mPage = rml(pageName(page));
        switch (page) {
        case Page::Explore:
            //% "Explore the route"
            mPageTitle = rml(qtTrId("ui.explore.explore.route"));
            break;
        case Page::Activity:
            //% "Activities"
            mPageTitle = rml(qtTrId("ui.explore.activities"));
            break;
        case Page::Timetable:
            //% "Timetables"
            mPageTitle = rml(qtTrId("ui.explore.timetables"));
            break;
        default:
            mPageTitle.clear();
            break;
        }

        // The activities page.
        mActivities.clear();
        for (const Lists::ActivityInfo &a : activities)
            mActivities.push_back({rml(a.name), rml(difficultyName(a.difficulty)), rml(clock(a.duration > 0 ? a.duration * 60 : -1))});
        mActivity = activity;
        mStops.clear();
        if (const Lists::ActivityInfo *a = chosenActivity()) {
            mActName = rml(a->name);
            QStringList facts;
            for (const QString &fact : {clock(a->startTime), seasonName(a->season), weatherName(a->weather),
                                        difficultyName(a->difficulty), durationText(a->duration)})
                if (!fact.isEmpty())
                    facts << fact;
            mActFacts = rml(facts.join(QStringLiteral(" · ")));
            mActDescription = rml(a->description);
            mActBriefing = rml(a->briefing == a->description ? QString() : a->briefing);
            mActTrain = rml(activityTrain(*a));
            mActPath = rml(activityPath(*a));
            //% "Other trains: %1"
            mActTraffic = rml(qtTrId("ui.explore.other.trains").arg(a->traffic));
            QStringList seasonWeather;
            for (const QString &fact : {seasonName(a->season), weatherName(a->weather)})
                if (!fact.isEmpty())
                    seasonWeather << fact;
            mActSeasonWeather = rml(seasonWeather.join(QStringLiteral(" · ")));
            if (const Lists::RouteStations *s = stationsOfRoute())
                for (const Lists::ActivityInfo::Stop &stop : a->playerStops) {
                    const QString name = s->ofItem.value(stop.platform);
                    if (!name.isEmpty())
                        mStops.push_back({rml(name), rml(clock(stop.arrival)), rml(clock(stop.departure))});
                }
        } else {
            mActName = mActFacts = mActDescription = mActBriefing = mActTrain = mActPath = mActTraffic =
                    mActSeasonWeather = Rml::String();
        }

        // The timetables page.
        const Lists::TimetableSetInfo *s = chosenSet();
        mSetName = s != nullptr ? rml(s->name) : Rml::String();
        mManySets = sets.size() > 1;
        //% "Trains: %1"
        mSetTrains = rml(qtTrId("ui.explore.count.trains").arg(trains.size()));
        mHasCategory = mHasNumber = mHasLine = false;
        for (const Lists::TrainInfo &t : trains) {
            mHasCategory = mHasCategory || !t.category.isEmpty();
            mHasNumber = mHasNumber || !t.number.isEmpty();
            mHasLine = mHasLine || !t.line.isEmpty();
        }
        mTrains.clear();
        mTrain = -1;
        for (int i : shownTrains) {
            const Lists::TrainInfo &t = trains[i];
            if (t.name == train)
                mTrain = int(mTrains.size());
            mTrains.push_back({rml(t.name), rml(clock(t.startTime)), rml(clock(t.endTime)), rml(t.from), rml(t.to),
                               rml(t.category), rml(t.number), rml(t.line), rml(t.engine),
                               rml(t.stops > 0 ? QString::number(t.stops) : QString()), t.drivable});
        }
        mCalls.clear();
        const Lists::TrainInfo *t = chosenTrain();
        mTrainChosen = t != nullptr;
        if (t != nullptr) {
            mTrainComment = rml(t->comment.isEmpty() ? t->name : t->comment);
            mTrainBriefing = rml(t->briefing.isEmpty() || t->briefing == t->comment ? QString() : t->briefing);
            QStringList details;
            if (!t->consist.isEmpty())
                //% "Consist: %1"
                details << qtTrId("ui.explore.consist.is").arg(t->consist);
            if (!t->path.isEmpty())
                //% "Path: %1"
                details << qtTrId("ui.explore.path.is").arg(t->path);
            mTrainDetails = rml(details.join(QStringLiteral(" · ")));
            for (int i = 0; i < t->times.size(); ++i) {
                const Lists::TrainInfo::Time &c = t->times[i];
                const bool pass = c.arrival < 0 && c.departure < 0;
                // A stop's arrival (its first: its departure), a pass's time.
                const int at = pass ? c.passing : i == 0 && c.departure >= 0 ? c.departure
                             : c.arrival >= 0 ? c.arrival : c.departure;
                mCalls.push_back({rml(c.station + " " + clock(at)), pass});
            }
        } else {
            mTrainComment = mTrainBriefing = mTrainDetails = Rml::String();
        }

        // The explore page.
        mPaths.clear();
        mPathRow = -1;
        for (int i : shownPaths) {
            const Lists::PathInfo &p = paths[i];
            if (p.file == path && exploresWithTrain())
                mPathRow = int(mPaths.size());
            mPaths.push_back({rml(p.start), rml(p.end), rml(p.name)});
        }
        const Lists::PathInfo *p = chosenPath();
        mPathNote = p != nullptr ? rml(p->file + ": " + p->start + " - " + p->end
                                       + (p->name != QFileInfo(p->file).completeBaseName() ? "  (" + p->name + ")"
                                                                                            : QString()))
                                 : Rml::String();
        mConsists.clear();
        mConsistRow = -1;
        for (int i : shownConsists) {
            const Lists::ConsistInfo &c = consists[i];
            if (c.file == consist)
                mConsistRow = int(mConsists.size());
            mConsists.push_back({rml(c.name), rml(c.engineName)});
        }
        const Lists::ConsistInfo *c = chosenConsist();
        //% "Cars: %1"
        mConsistNote = c != nullptr ? rml(c->engineName + " · " + qtTrId("ui.explore.cars").arg(c->cars))
                                    : Rml::String();
        mConsistsLoading = page == Page::Explore && !consistsDone;
        mPathFilter = rml(pathFilter);
        mConsistFilter = rml(consistFilter);

        // The bar.
        mWalk = walk;
        mExploreTrain = exploresWithTrain();
        mNoTrain = walk && page == Page::Timetable && chosenTrain() == nullptr;
        mStationsLoading = needsRouteStations() && stationsOfRoute() == nullptr;
        {
            const QVector<StationChoice> list = stationChoices();
            const StationChoice *chosen = chosenStation(list);
            mStation = chosen != nullptr ? rml(chosen->note.isEmpty() ? chosen->name : chosen->name + "  " + chosen->note)
                                         : Rml::String();
        }
        mShowTime = page == Page::Explore || walk;
        mTime = rml(clock(time));
        mDay = rml(dayText(day));
        mSeason = rml(seasonName(Lists::seasonOf(day, latitude())));
        mWeather = rml(weatherName(weather));
        mFilter = rml(filter);
        mSortColumn = rml(sortColumn);
        mSortUp = sortUp;
        mDrivableOnly = drivableOnly;
        mPicking = rml(picking);
        mReady = ready();
        mStatus = rml(status);
        if (handle)
            handle.DirtyAllVariables();
    }

    void focus(Rml::Element *element) {
        if (element != nullptr)
            element->Focus(true);
    }

    static bool shownFocusable(Rml::Element *e) {
        if (!e->IsVisible(true) || e->IsClassSet("later") || e->IsClassSet("disabled"))
            return false;
        for (Rml::Element *p = e->GetParentNode(); p != nullptr; p = p->GetParentNode())
            if (p->IsClassSet("greyed"))
                return false;
        const Rml::String tag = e->GetTagName();
        return tag == "button" || tag == "input";
    }

    // The columns Left and Right cross on the page shown, by their
    // elements' ids.
    QStringList columns() const {
        switch (page) {
        case Page::Activity: return {"activity-list", "bar"};
        case Page::Timetable: return {"trains", "bar"};
        case Page::Explore: return {"path-list", "consist-list", "bar"};
        default: return {"routes", "page-buttons"};
        }
    }

    // The element shown by an id (ids repeat across data-if blocks).
    Rml::Element *shownById(const QString &id) const {
        if (document == nullptr)
            return nullptr;
        Rml::ElementList found;
        document->QuerySelectorAll(found, ("#" + id).toStdString());
        for (Rml::Element *e : found)
            if (e->IsVisible(true))
                return e;
        return nullptr;
    }

    // A column's element to focus: its chosen item, else its first control
    // (a button before a text field); the route page's buttons: the last
    // page's; null when it has none shown.
    Rml::Element *columnTarget(const QString &column) const {
        Rml::Element *parent = shownById(column);
        if (parent == nullptr)
            return nullptr;
        if (column == "page-buttons") {
            Rml::Element *last = shownById("open-" + pageName(lastPage));
            if (last != nullptr && shownFocusable(last))
                return last;
        }
        Rml::ElementList chosen;
        parent->GetElementsByClassName(chosen, "chosen");
        for (Rml::Element *e : chosen)
            if (shownFocusable(e))
                return e;
        Rml::ElementList all;
        parent->QuerySelectorAll(all, "button, input");
        Rml::Element *field = nullptr;
        for (Rml::Element *e : all)
            if (shownFocusable(e)) {
                if (e->GetTagName() != "input")
                    return e;
                if (field == nullptr)
                    field = e;
            }
        return field;
    }

    QString columnOf(Rml::Element *focus) const {
        const QStringList list = columns();
        for (Rml::Element *e = focus; e != nullptr; e = e->GetParentNode()) {
            const QString id = QString::fromStdString(e->GetId());
            if (list.contains(id))
                return id;
        }
        return QString();
    }

    // The n-th button among an element's descendants shown, null for none.
    Rml::Element *shownButton(Rml::Element *parent, int n) const {
        if (parent == nullptr)
            return nullptr;
        Rml::ElementList buttons;
        parent->GetElementsByTagName(buttons, "button");
        int k = 0;
        for (Rml::Element *b : buttons) {
            if (!b->IsVisible(true))
                continue;
            if (k == n)
                return b;
            ++k;
        }
        return nullptr;
    }
};

Panel::Panel(Rml::Context &context, const QString &root, const QString &route,
             std::function<void(const RunChoice &)> start)
    : d(new Data(context)) {
    d->root = root;
    d->firstRoute = route;
    d->start = std::move(start);
    d->day = clockDate();
    d->loadRemembered();
    const QDate kept = QDate::fromString(d->remembered.value("day").toString(), "yyyy-MM-dd");
    if (kept.isValid())
        d->day = kept;
    if (d->remembered.contains("time"))
        d->time = std::clamp(d->remembered.value("time").toInt(), 0, 86399);
    if (d->remembered.contains("weather"))
        d->weather = std::clamp(d->remembered.value("weather").toInt(), 0, 2);
    d->walk = d->remembered.value("walk").toBool();
    d->station = d->remembered.value("station").toString();
    d->exploreTrain = d->remembered.value("exploreTrain").toBool(true);

    Rml::DataModelConstructor constructor = context.CreateDataModel("explore");
    if (!constructor)
        return;
    if (auto row = constructor.RegisterStruct<Row>()) {
        row.RegisterMember("name", &Row::name);
        row.RegisterMember("note", &Row::note);
        row.RegisterMember("heading", &Row::heading);
    }
    constructor.RegisterArray<Rml::Vector<Row>>();
    if (auto row = constructor.RegisterStruct<TrainRow>()) {
        row.RegisterMember("name", &TrainRow::name);
        row.RegisterMember("start", &TrainRow::start);
        row.RegisterMember("end", &TrainRow::end);
        row.RegisterMember("from", &TrainRow::from);
        row.RegisterMember("to", &TrainRow::to);
        row.RegisterMember("category", &TrainRow::category);
        row.RegisterMember("number", &TrainRow::number);
        row.RegisterMember("line", &TrainRow::line);
        row.RegisterMember("engine", &TrainRow::engine);
        row.RegisterMember("stops", &TrainRow::stops);
        row.RegisterMember("drivable", &TrainRow::drivable);
    }
    constructor.RegisterArray<Rml::Vector<TrainRow>>();
    if (auto row = constructor.RegisterStruct<ActivityRow>()) {
        row.RegisterMember("name", &ActivityRow::name);
        row.RegisterMember("difficulty", &ActivityRow::difficulty);
        row.RegisterMember("duration", &ActivityRow::duration);
    }
    constructor.RegisterArray<Rml::Vector<ActivityRow>>();
    if (auto row = constructor.RegisterStruct<StopRow>()) {
        row.RegisterMember("station", &StopRow::station);
        row.RegisterMember("arrival", &StopRow::arrival);
        row.RegisterMember("departure", &StopRow::departure);
    }
    constructor.RegisterArray<Rml::Vector<StopRow>>();
    if (auto row = constructor.RegisterStruct<CallRow>()) {
        row.RegisterMember("text", &CallRow::text);
        row.RegisterMember("pass", &CallRow::pass);
    }
    constructor.RegisterArray<Rml::Vector<CallRow>>();
    if (auto row = constructor.RegisterStruct<PathRow>()) {
        row.RegisterMember("start", &PathRow::start);
        row.RegisterMember("end", &PathRow::end);
        row.RegisterMember("name", &PathRow::name);
    }
    constructor.RegisterArray<Rml::Vector<PathRow>>();
    if (auto row = constructor.RegisterStruct<ConsistRow>()) {
        row.RegisterMember("name", &ConsistRow::name);
        row.RegisterMember("engine", &ConsistRow::engine);
    }
    constructor.RegisterArray<Rml::Vector<ConsistRow>>();
    Data &m = *d;
    // The route page.
    constructor.Bind("routes", &m.mRoutes);
    constructor.Bind("route", &m.mRoute);
    constructor.Bind("routeName", &m.mRouteName);
    constructor.Bind("routeDescription", &m.mRouteDescription);
    constructor.Bind("picture", &m.mPicture);
    constructor.Bind("routeFacts", &m.mRouteFacts);
    constructor.Bind("routeCounts", &m.mRouteCounts);
    constructor.Bind("activityCount", &m.mActivityCount);
    constructor.Bind("setCount", &m.mSetCount);
    constructor.Bind("page", &m.mPage);
    constructor.Bind("pageTitle", &m.mPageTitle);
    // The activities page.
    constructor.Bind("activities", &m.mActivities);
    constructor.Bind("activity", &m.mActivity);
    constructor.Bind("actName", &m.mActName);
    constructor.Bind("actFacts", &m.mActFacts);
    constructor.Bind("actDescription", &m.mActDescription);
    constructor.Bind("actBriefing", &m.mActBriefing);
    constructor.Bind("actTrain", &m.mActTrain);
    constructor.Bind("actPath", &m.mActPath);
    constructor.Bind("actStops", &m.mStops);
    constructor.Bind("actTraffic", &m.mActTraffic);
    constructor.Bind("actSeasonWeather", &m.mActSeasonWeather);
    // The timetables page.
    constructor.Bind("setName", &m.mSetName);
    constructor.Bind("setTrains", &m.mSetTrains);
    constructor.Bind("manySets", &m.mManySets);
    constructor.Bind("trains", &m.mTrains);
    constructor.Bind("train", &m.mTrain);
    constructor.Bind("trainChosen", &m.mTrainChosen);
    constructor.Bind("trainComment", &m.mTrainComment);
    constructor.Bind("trainBriefing", &m.mTrainBriefing);
    constructor.Bind("trainDetails", &m.mTrainDetails);
    constructor.Bind("calls", &m.mCalls);
    constructor.Bind("sortColumn", &m.mSortColumn);
    constructor.Bind("sortUp", &m.mSortUp);
    constructor.Bind("drivableOnly", &m.mDrivableOnly);
    constructor.Bind("hasCategory", &m.mHasCategory);
    constructor.Bind("hasNumber", &m.mHasNumber);
    constructor.Bind("hasLine", &m.mHasLine);
    // The explore page.
    constructor.Bind("paths", &m.mPaths);
    constructor.Bind("pathRow", &m.mPathRow);
    constructor.Bind("pathNote", &m.mPathNote);
    constructor.Bind("consists", &m.mConsists);
    constructor.Bind("consistRow", &m.mConsistRow);
    constructor.Bind("consistNote", &m.mConsistNote);
    constructor.Bind("consistsLoading", &m.mConsistsLoading);
    constructor.Bind("exploreTrain", &m.mExploreTrain);
    // The bar.
    constructor.Bind("walk", &m.mWalk);
    constructor.Bind("stationName", &m.mStation);
    constructor.Bind("stationsLoading", &m.mStationsLoading);
    constructor.Bind("noTrain", &m.mNoTrain);
    constructor.Bind("showTime", &m.mShowTime);
    constructor.Bind("time", &m.mTime);
    constructor.Bind("day", &m.mDay);
    constructor.Bind("season", &m.mSeason);
    constructor.Bind("weather", &m.mWeather);
    constructor.Bind("ready", &m.mReady);
    constructor.Bind("status", &m.mStatus);
    // The picker.
    constructor.Bind("options", &m.mOptions);
    constructor.Bind("option", &m.mOption);
    constructor.Bind("picking", &m.mPicking);
    constructor.Bind("pickerTitle", &m.mPickerTitle);
    // The filters typed: the lists follow each letter.
    constructor.BindFunc("filter", [this](Rml::Variant &value) { value = d->mFilter; },
                         [this](const Rml::Variant &value) { setFilter(QString::fromStdString(value.Get<Rml::String>())); });
    constructor.BindFunc("pathFilter", [this](Rml::Variant &value) { value = d->mPathFilter; },
                         [this](const Rml::Variant &value) {
                             setPathFilter(QString::fromStdString(value.Get<Rml::String>()));
                         });
    constructor.BindFunc("consistFilter", [this](Rml::Variant &value) { value = d->mConsistFilter; },
                         [this](const Rml::Variant &value) {
                             setConsistFilter(QString::fromStdString(value.Get<Rml::String>()));
                         });
    const auto index = [](const Rml::VariantList &arguments) {
        return arguments.empty() ? -1 : arguments[0].Get<int>(-1);
    };
    constructor.BindEventCallback("chooseRoute", [this, index](Rml::DataModelHandle, Rml::Event &,
                                                               const Rml::VariantList &arguments) {
        d->chooseRoute(index(arguments));
        d->focusLater("column:page-buttons");
    });
    // A page's button (task explore 05): into the page; on Start when it
    // opens with the run last started there (one Enter from it).
    constructor.BindEventCallback("openPage", [this](Rml::DataModelHandle, Rml::Event &,
                                                     const Rml::VariantList &arguments) {
        if (arguments.empty() || !openPage(QString::fromStdString(arguments[0].Get<Rml::String>())))
            return;
        d->focusLater(d->remembersThisRun() && d->ready() ? QString("go") : QString("column:*"));
    });
    constructor.BindEventCallback("back", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        d->back();
    });
    constructor.BindEventCallback("chooseActivity", [this, index](Rml::DataModelHandle, Rml::Event &,
                                                                  const Rml::VariantList &arguments) {
        const int row = index(arguments);
        if (row < 0 || row >= d->activities.size())
            return;
        d->activity = row;
        d->status.clear();
        if (d->walk)
            d->chooseStationFor(true);
        d->refresh();
        if (d->ready())
            d->focusLater("go");
    });
    constructor.BindEventCallback("chooseTrain", [this, index](Rml::DataModelHandle, Rml::Event &,
                                                               const Rml::VariantList &arguments) {
        const int row = index(arguments);
        if (row >= 0 && row < d->shownTrains.size()) {
            d->train = d->trains[d->shownTrains[row]].name;
            d->status.clear();
            if (d->walk)
                d->chooseStationFor(true);
            d->refresh();
            // On to Start, as Enter on a route goes to its pages.
            if (d->ready())
                d->focusLater("go");
        }
    });
    // Walking with no train (task explore 04): only watching.
    constructor.BindEventCallback("chooseNoTrain", [this](Rml::DataModelHandle, Rml::Event &,
                                                          const Rml::VariantList &) {
        d->train.clear();
        d->status.clear();
        d->chooseStationFor(false);
        d->refresh();
    });
    constructor.BindEventCallback("choosePathRow", [this, index](Rml::DataModelHandle, Rml::Event &,
                                                                 const Rml::VariantList &arguments) {
        const int row = index(arguments);
        if (row < 0 || row >= d->shownPaths.size())
            return;
        const Lists::PathInfo &p = d->paths[d->shownPaths[row]];
        d->path = p.file;
        d->startName = p.start;
        d->exploreTrain = true;
        d->status.clear();
        if (d->walk)
            d->chooseStationFor(false);
        d->refresh();
        // On to the trains.
        d->focusLater("column:consist-list");
    });
    // Exploring on foot with no train (task explore 04).
    constructor.BindEventCallback("chooseNoPathTrain", [this](Rml::DataModelHandle, Rml::Event &,
                                                              const Rml::VariantList &) {
        d->exploreTrain = false;
        d->status.clear();
        d->refresh();
        if (d->ready())
            d->focusLater("go");
    });
    constructor.BindEventCallback("chooseConsistRow", [this, index](Rml::DataModelHandle, Rml::Event &,
                                                                    const Rml::VariantList &arguments) {
        const int row = index(arguments);
        if (row < 0 || row >= d->shownConsists.size())
            return;
        const Lists::ConsistInfo &c = d->consists[d->shownConsists[row]];
        d->consist = c.file;
        d->loco = c.engineName;
        d->status.clear();
        d->refresh();
        if (d->ready())
            d->focusLater("go");
    });
    constructor.BindEventCallback("setWalk", [this](Rml::DataModelHandle, Rml::Event &,
                                                    const Rml::VariantList &arguments) {
        d->setWalk(!arguments.empty() && arguments[0].Get<bool>(false));
    });
    constructor.BindEventCallback("sortBy", [this](Rml::DataModelHandle, Rml::Event &,
                                                   const Rml::VariantList &arguments) {
        if (!arguments.empty())
            sortTrains(QString::fromStdString(arguments[0].Get<Rml::String>()));
    });
    constructor.BindEventCallback("toggleDrivable", [this](Rml::DataModelHandle, Rml::Event &,
                                                           const Rml::VariantList &) {
        setDrivableOnly(!d->drivableOnly);
    });
    constructor.BindEventCallback("pick", [this](Rml::DataModelHandle, Rml::Event &,
                                                 const Rml::VariantList &arguments) {
        if (!arguments.empty())
            d->openPicker(QString::fromStdString(arguments[0].Get<Rml::String>()));
    });
    constructor.BindEventCallback("pickOption", [this, index](Rml::DataModelHandle, Rml::Event &,
                                                              const Rml::VariantList &arguments) {
        d->pickOption(index(arguments));
    });
    constructor.BindEventCallback("go", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
        go();
    });
    d->handle = constructor.GetModelHandle();
    d->made = true;
}

Panel::~Panel() {
    if (d->made)
        d->context.RemoveDataModel("explore");
}

bool Panel::usable() const {
    return d->made;
}

void Panel::setDocument(Rml::ElementDocument *document) {
    d->document = document;
}

void Panel::open() {
    if (d->opened)
        return;
    d->opened = true;
    d->readRoutes();
    // The Load window's route, else the last one.
    const QString first = !d->firstRoute.isEmpty() ? d->firstRoute : d->remembered.value("route").toString();
    for (int i = 0; i < d->routes.size(); ++i)
        if (d->routes[i].folder.compare(first, Qt::CaseInsensitive) == 0)
            d->chooseRoute(i);
    d->refresh();
}

void Panel::frame() {
    d->takeConsists();
    d->takeRouteStations();
    if (d->focusWanted.isEmpty() || d->document == nullptr)
        return;
    Rml::Element *element = nullptr;
    if (d->focusWanted == "column:*") {
        // The page's first column with something to focus.
        for (const QString &column : d->columns())
            if ((element = d->columnTarget(column)) != nullptr)
                break;
    } else if (d->focusWanted.startsWith("column:")) {
        element = d->columnTarget(d->focusWanted.mid(7));
    } else if (d->focusWanted == "picker") {
        Rml::Element *list = d->document->GetElementById("picker-options");
        element = d->shownButton(list, std::max(0, d->mOption));
    } else {
        // A choice's button, a page's: the one shown.
        element = d->shownById(d->focusWanted);
        if (element != nullptr && !Data::shownFocusable(element))
            element = nullptr;
    }
    if (element != nullptr) {
        d->focus(element);
        element->ScrollIntoView(Rml::ScrollAlignment::Nearest);
        d->focusWanted.clear();
    } else if (++d->focusTries > 5) {
        d->focusWanted.clear();
    }
}

QStringList Panel::routeNames() const {
    QStringList names;
    for (const Lists::RouteInfo &r : d->routes)
        names << r.folder;
    return names;
}

QStringList Panel::runNames() const {
    QStringList names;
    if (d->chosenRoute() == nullptr)
        return names;
    names << qtTrId("ui.explore.explore.route");
    for (const Lists::ActivityInfo &a : d->activities)
        names << a.name;
    for (const Lists::TimetableSetInfo &s : d->sets)
        names << s.name;
    return names;
}

bool Panel::chooseRoute(const QString &name) {
    for (int i = 0; i < d->routes.size(); ++i)
        if (d->routes[i].folder.compare(name, Qt::CaseInsensitive) == 0) {
            d->chooseRoute(i);
            return true;
        }
    return false;
}

bool Panel::chooseRun(const QString &name) {
    if (d->chosenRoute() == nullptr)
        return false;
    if (name == "explore" || name == qtTrId("ui.explore.explore.route"))
        return d->openPage(Data::Page::Explore);
    for (int i = 0; i < d->activities.size(); ++i)
        if (d->activities[i].name == name || d->activities[i].file.compare(name, Qt::CaseInsensitive) == 0) {
            d->activity = i;
            return d->openPage(Data::Page::Activity);
        }
    for (int i = 0; i < d->sets.size(); ++i)
        if (d->sets[i].name == name || d->sets[i].file.compare(name, Qt::CaseInsensitive) == 0) {
            d->set = i;
            return d->openPage(Data::Page::Timetable);
        }
    return false;
}

QString Panel::page() const {
    return Data::pageName(d->page);
}

bool Panel::openPage(const QString &name) {
    if (name == "explore")
        return d->openPage(Data::Page::Explore);
    if (name == "activity")
        return d->openPage(Data::Page::Activity);
    if (name == "timetable")
        return d->openPage(Data::Page::Timetable);
    return false;
}

bool Panel::back() {
    return d->back();
}

void Panel::setPathFilter(const QString &text) {
    if (d->pathFilter == text)
        return;
    d->pathFilter = text;
    d->filterPaths();
    d->refresh();
}

void Panel::setConsistFilter(const QString &text) {
    if (d->consistFilter == text)
        return;
    d->consistFilter = text;
    d->filterConsists();
    d->refresh();
}

QStringList Panel::pathFiles() const {
    QStringList files;
    for (int i : d->shownPaths)
        files << d->paths[i].file;
    return files;
}

QStringList Panel::consistFiles() const {
    QStringList files;
    for (int i : d->shownConsists)
        files << d->consists[i].file;
    return files;
}

QString Panel::text(const QString &id) const {
    Rml::Element *e = d->shownById(id);
    if (e == nullptr)
        return QString();
    // A text field: its value.
    if (e->GetTagName() == "input")
        return QString::fromStdString(e->GetAttribute<Rml::String>("value", ""));
    Rml::String inner;
    e->GetInnerRML(inner);
    return QString::fromStdString(inner);
}

bool Panel::choosePath(const QString &file) {
    for (const Lists::PathInfo &p : d->paths)
        if (p.file.compare(file, Qt::CaseInsensitive) == 0) {
            d->path = p.file;
            d->startName = p.start;
            d->refresh();
            return true;
        }
    return false;
}

bool Panel::chooseConsist(const QString &file) {
    for (const Lists::ConsistInfo &c : d->consists)
        if (c.file.compare(file, Qt::CaseInsensitive) == 0) {
            d->consist = c.file;
            d->loco = c.engineName;
            d->refresh();
            return true;
        }
    return false;
}

bool Panel::chooseTrain(const QString &name) {
    for (int i : d->shownTrains)
        if (d->trains[i].name == name) {
            d->train = name;
            if (d->walk)
                d->chooseStationFor(true);
            d->refresh();
            return true;
        }
    return false;
}

QStringList Panel::trainNames() const {
    QStringList names;
    for (int i : d->shownTrains)
        names << d->trains[i].name;
    return names;
}

void Panel::setFilter(const QString &text) {
    if (d->filter == text)
        return;
    d->filter = text;
    d->filterTrains();
    d->refresh();
}

void Panel::setDrivableOnly(bool on) {
    d->drivableOnly = on;
    d->filterTrains();
    d->refresh();
}

void Panel::sortTrains(const QString &column) {
    if (d->sortColumn == column)
        d->sortUp = !d->sortUp;
    else
        d->sortUp = true;
    d->sortColumn = column;
    d->filterTrains();
    d->refresh();
}

void Panel::setDay(const QDate &day) {
    if (day.isValid())
        d->day = day;
    d->refresh();
}

void Panel::setWeather(int weather) {
    d->weather = std::clamp(weather, 0, 2);
    d->refresh();
}

void Panel::setTime(int seconds) {
    d->time = std::clamp(seconds, 0, 86399);
    d->refresh();
}

bool Panel::consistsRead() const {
    return d->consistsDone;
}

void Panel::setWalk(bool on) {
    d->setWalk(on);
}

QStringList Panel::stationNames() const {
    QStringList names;
    for (const Data::StationChoice &c : d->stationChoices())
        names << c.name;
    return names;
}

bool Panel::chooseStation(const QString &name) {
    for (const Data::StationChoice &c : d->stationChoices())
        if (c.name.compare(name, Qt::CaseInsensitive) == 0) {
            d->station = c.name;
            d->chooseStationFor(true);
            d->refresh();
            return true;
        }
    return false;
}

void Panel::chooseNoTrain() {
    d->train.clear();
    d->chooseStationFor(false);
    d->refresh();
}

void Panel::setExploreTrain(bool on) {
    d->exploreTrain = on;
    d->refresh();
}

bool Panel::stationsRead() const {
    return !d->needsRouteStations() || d->stationsOfRoute() != nullptr;
}

bool Panel::ready() const {
    return d->ready();
}

RunChoice Panel::choice() const {
    RunChoice choice;
    const Lists::RouteInfo *r = d->chosenRoute();
    if (r == nullptr || d->page == Data::Page::Route)
        return choice;
    choice.route = r->folder;
    choice.routeName = r->name;
    choice.loadingScreen = r->loadingScreen;
    choice.day = d->day;
    choice.season = Lists::seasonOf(d->day, r->latitude);
    choice.weather = d->weather;
    choice.walk = d->walk;
    if (d->walk) {
        const QVector<Data::StationChoice> list = d->stationChoices();
        const Data::StationChoice *c = d->chosenStation(list);
        choice.station = c != nullptr ? c->name : QString();
    }
    switch (d->page) {
    case Data::Page::Explore:
        choice.kind = RunChoice::Kind::Explore;
        if (d->exploresWithTrain()) {
            choice.path = d->path;
            choice.consist = d->consist;
        }
        choice.time = d->time;
        break;
    case Data::Page::Activity: {
        const Lists::ActivityInfo *a = d->chosenActivity();
        if (a == nullptr)
            break;
        choice.kind = RunChoice::Kind::Activity;
        choice.file = a->file;
        // Walking: from the time chosen, no earlier than its start.
        choice.time = d->walk ? std::max(a->startTime, d->time) : a->startTime;
        // Its own season and weather.
        choice.season = a->season >= 0 ? a->season : choice.season;
        choice.weather = a->weather >= 0 ? a->weather : 0;
        break;
    }
    case Data::Page::Timetable:
        choice.kind = RunChoice::Kind::Timetable;
        choice.file = d->chosenSet() != nullptr ? d->chosenSet()->file : QString();
        choice.train = d->chosenTrain() != nullptr ? d->train : QString();
        if (d->walk)
            choice.time = d->time;
        break;
    default:
        break;
    }
    return choice;
}

bool Panel::go() {
    if (!d->ready())
        return false;
    const RunChoice run = choice();
    // Remembered for the next time (Open Rails' Menu_Selection).
    d->remembered["route"] = run.route;
    switch (run.kind) {
    case RunChoice::Kind::Explore:
        d->remembered["run"] = "explore";
        if (!run.path.isEmpty()) {
            d->remembered["path"] = run.path;
            d->remembered["consist"] = run.consist;
        }
        d->remembered["time"] = run.time;
        break;
    case RunChoice::Kind::Activity:
        d->remembered["run"] = "activity:" + run.file;
        break;
    case RunChoice::Kind::Timetable: {
        d->remembered["run"] = "timetable:" + run.file;
        QJsonObject trains = d->remembered.value("trains").toObject();
        trains[run.file] = run.train;
        d->remembered["trains"] = trains;
        break;
    }
    }
    d->remembered["day"] = d->day.toString("yyyy-MM-dd");
    d->remembered["weather"] = d->weather;
    d->remembered["walk"] = run.walk;
    if (run.walk) {
        d->remembered["station"] = run.station;
        if (run.kind == RunChoice::Kind::Explore)
            d->remembered["exploreTrain"] = !run.path.isEmpty();
    }
    d->saveRemembered();
    d->status.clear();
    d->refresh();
    if (d->start)
        d->start(run);
    return true;
}

void Panel::setStatus(const QString &text) {
    d->status = text;
    d->refresh();
}

bool Panel::picking() const {
    return !d->picking.isEmpty();
}

void Panel::closePicker() {
    d->closePicker();
}

void Panel::setRememberedFile(const QString &file) {
    rememberedOverride() = file;
}

bool Panel::moveAcross(Rml::Element *focus, bool right) {
    if (focus == nullptr || d->document == nullptr)
        return false;
    // A text field takes the arrows; buttons side by side (a pair, a table's
    // head, a tools row, a bar's row, the route page's buttons) take them to
    // each other.
    if (focus->GetTagName() == "input")
        return false;
    Rml::Element *parent = focus->GetParentNode();
    if (parent != nullptr && (parent->IsClassSet("pair") || parent->IsClassSet("table-head") || parent->IsClassSet("tools")
                              || parent->IsClassSet("bar-row") || parent->IsClassSet("page-buttons"))) {
        Rml::Element *next = right ? focus->GetNextSibling() : focus->GetPreviousSibling();
        while (next != nullptr && !Data::shownFocusable(next))
            next = right ? next->GetNextSibling() : next->GetPreviousSibling();
        if (next != nullptr) {
            // RmlUi's own arrows can jump a row; the sibling is the one.
            next->Focus(true);
            return true;
        }
    }
    const QStringList order = d->columns();
    const int at = order.indexOf(d->columnOf(focus));
    if (at < 0)
        return false;
    if (!right && at == 0) {
        // A run page's first column: back to the route page; the routes'
        // Left goes to the menu's item (their nav-left).
        return d->back();
    }
    for (int i = at + (right ? 1 : -1); i >= 0 && i < order.size(); i += right ? 1 : -1)
        if (Rml::Element *target = d->columnTarget(order[i])) {
            target->Focus(true);
            target->ScrollIntoView(Rml::ScrollAlignment::Nearest);
            return true;
        }
    // Nothing that way: the keys stay; Left with nothing left of it goes
    // back to the route page.
    return right || d->back();
}

bool Panel::moveUpDown(Rml::Element *focus, bool down) {
    if (focus == nullptr || d->document == nullptr)
        return false;
    // A list and the row of its filter above it.
    static const QList<QPair<QString, QString>> lists = {
            {"trains", "trains-tools"}, {"path-list", "path-tools"}, {"consist-list", "consist-tools"}};
    for (const auto &[list, tools] : lists) {
        Rml::Element *listElement = d->shownById(list);
        Rml::Element *toolsElement = d->shownById(tools);
        if (listElement == nullptr || toolsElement == nullptr)
            continue;
        if (!down && focus->GetParentNode() == listElement) {
            // Up from its first row.
            Rml::Element *before = focus->GetPreviousSibling();
            while (before != nullptr && !Data::shownFocusable(before))
                before = before->GetPreviousSibling();
            if (before != nullptr)
                return false;
            Rml::ElementList controls;
            toolsElement->QuerySelectorAll(controls, "button, input");
            // Its filter first (typing), else the row's first control.
            Rml::Element *target = nullptr;
            for (Rml::Element *e : controls)
                if (Data::shownFocusable(e) && (target == nullptr || e->GetTagName() == "input")) {
                    target = e;
                    if (e->GetTagName() == "input")
                        break;
                }
            if (target == nullptr)
                return false;
            target->Focus(true);
            return true;
        }
        if (down) {
            bool inTools = false;
            for (Rml::Element *e = focus; e != nullptr && !inTools; e = e->GetParentNode())
                inTools = e == toolsElement;
            if (!inTools)
                continue;
            if (Rml::Element *target = d->columnTarget(list)) {
                target->Focus(true);
                target->ScrollIntoView(Rml::ScrollAlignment::Nearest);
                return true;
            }
            return false;
        }
    }
    return false;
}

bool Panel::scrollDetails(bool down) {
    Rml::Element *box = d->shownById(d->page == Data::Page::Activity ? "activity-details" : "route-info");
    if (box == nullptr)
        return false;
    const float step = std::max(40.0f, box->GetClientHeight() * 0.8f);
    box->SetScrollTop(std::clamp(box->GetScrollTop() + (down ? step : -step), 0.0f,
                                 std::max(0.0f, box->GetScrollHeight() - box->GetClientHeight())));
    return true;
}

Rml::Element *Panel::entry() const {
    if (d->document == nullptr)
        return nullptr;
    if (d->page != Data::Page::Route) {
        for (const QString &column : d->columns())
            if (Rml::Element *target = d->columnTarget(column))
                return target;
        return nullptr;
    }
    if (d->lastPage != Data::Page::Route)
        if (Rml::Element *button = d->columnTarget("page-buttons"))
            if (button->GetId() == ("open-" + Data::pageName(d->lastPage)).toStdString())
                return button;
    return d->shownButton(d->document->GetElementById("routes"), std::max(0, d->route));
}

}
