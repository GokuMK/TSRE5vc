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

namespace Explore {

namespace {

// A list's row: a name, a note after it, or a heading between rows.
struct Row {
    Rml::String name;
    Rml::String note;
    bool heading = false;
};

// A timetable train's row in the table.
struct TrainRow {
    Rml::String name;
    Rml::String start;
    Rml::String from;
    Rml::String to;
    Rml::String category;
    Rml::String number;
    Rml::String line;
    Rml::String engine;
    bool drivable = false;
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
    struct ConsistJob {
        std::atomic<bool> done{false};
        QVector<Lists::ConsistInfo> list;
        qint64 ms = 0;
    };
    std::shared_ptr<ConsistJob> consistJob;
    QVector<Lists::ConsistInfo> consists;
    bool consistsDone = false;
    // The routes' stations (task explore 04), read on a worker for walking
    // in an activity or exploring, kept by route folder.
    struct StationsJob {
        std::atomic<bool> done{false};
        Lists::RouteStations stations;
        qint64 ms = 0;
    };
    QHash<QString, std::shared_ptr<StationsJob>> stationsJobs;
    QHash<QString, Lists::RouteStations> routeStations;
    // A timetable set's station rows.
    QStringList setStations;

    // The runs' list: what each row is.
    enum class RunKind { Heading, Explore, Activity, Timetable };
    struct RunEntry {
        RunKind kind = RunKind::Heading;
        int index = -1;
        QString name;
    };
    QVector<RunEntry> runEntries;

    // The choices.
    int route = -1;
    int run = -1;
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
    // Walking (task explore 04): the station; exploring with its train or
    // none.
    bool walk = false;
    QString station;
    bool exploreTrain = true;

    // The picker: what it chooses, its values by row.
    QString picking;
    QStringList pickerValues;
    // An element to focus once the model made it: by id, or the picker's
    // chosen row.
    QString focusWanted;
    int focusTries = 0;

    // Remembered in the profile (explore.json).
    QJsonObject remembered;

    // The model's variables.
    Rml::Vector<Row> mRoutes, mRuns, mOptions;
    Rml::Vector<TrainRow> mTrains;
    int mRoute = -1, mRun = -1, mTrain = -1, mOption = -1;
    Rml::String mKind, mRouteName, mRouteDescription, mPicture;
    Rml::String mStart, mHeading, mLoco, mConsist, mTime, mDay, mSeason, mWeather;
    Rml::String mActDescription, mActBriefing, mActStart, mActSeason, mActWeather, mActDuration, mActDifficulty,
            mActConsist, mActPath;
    Rml::String mFilter, mSortColumn, mPicking, mPickerTitle, mStatus, mStation;
    bool mSortUp = true, mDrivableOnly = true, mReady = false, mConsistsLoading = false;
    bool mWalk = false, mStationsLoading = false, mNoTrain = false, mExploreTrain = true;
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
    const RunEntry *chosenRun() const {
        return run >= 0 && run < runEntries.size() ? &runEntries[run] : nullptr;
    }
    RunKind runKind() const {
        const RunEntry *entry = chosenRun();
        return entry != nullptr ? entry->kind : RunKind::Heading;
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
    QStringList starts() const {
        QStringList list;
        for (const Lists::PathInfo &p : paths)
            if (!list.contains(p.start))
                list << p.start;
        return list;
    }
    QVector<const Lists::PathInfo *> headings() const {
        QVector<const Lists::PathInfo *> list;
        for (const Lists::PathInfo &p : paths)
            if (p.start == startName)
                list << &p;
        return list;
    }
    QStringList locos() const {
        QStringList list;
        for (const Lists::ConsistInfo &c : consists)
            if (!list.contains(c.engineName))
                list << c.engineName;
        return list;
    }
    QVector<const Lists::ConsistInfo *> locoConsists() const {
        QVector<const Lists::ConsistInfo *> list;
        for (const Lists::ConsistInfo &c : consists)
            if (c.engineName == loco)
                list << &c;
        return list;
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
        switch (runKind()) {
        case RunKind::Timetable:
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
        case RunKind::Activity: {
            const Lists::RouteStations *route = stationsOfRoute();
            const RunEntry *entry = chosenRun();
            if (route == nullptr || entry == nullptr)
                break;
            const Lists::ActivityInfo &a = activities[entry->index];
            for (int i = 0; i < a.playerStops.size(); ++i) {
                const Lists::ActivityInfo::Stop &stop = a.playerStops[i];
                const QString name = route->ofItem.value(stop.platform);
                if (name.isEmpty() || has(name))
                    continue;
                const int calls = i == 0 && stop.departure >= 0 ? stop.departure
                                : stop.arrival >= 0 ? stop.arrival : stop.departure;
                list.push_back({name, timesNote(stop.arrival, stop.departure, -1),
                                calls >= 0 ? std::max(a.startTime, calls - Before) : a.startTime});
            }
            for (const QString &name : route->names)
                if (!has(name))
                    list.push_back({name, {}, a.startTime});
            break;
        }
        case RunKind::Explore:
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
            if (chosenStation(list) == nullptr && runKind() == RunKind::Explore && chosenPath() != nullptr)
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
        return walk && (runKind() == RunKind::Activity || runKind() == RunKind::Explore);
    }
    // Exploring with its train: walking may go without one.
    bool exploresWithTrain() const {
        return !walk || exploreTrain;
    }

    bool ready() const {
        const bool stationReady = !walk || chosenStation(stationChoices()) != nullptr;
        switch (runKind()) {
        case RunKind::Explore:
            return stationReady && (!exploresWithTrain() || (chosenPath() != nullptr && chosenConsist() != nullptr));
        case RunKind::Activity:
            return stationReady;
        case RunKind::Timetable: {
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
        refresh();
    }

    // The keys on to a column once the model made it (Enter on a route goes
    // to its runs, on a run to its details).
    void focusLater(const QString &what) {
        focusWanted = what;
        focusTries = 0;
    }

    void chooseRoute(int index) {
        if (index < 0 || index >= routes.size())
            return;
        route = index;
        run = -1;
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
        runEntries.clear();
        //% "Explore the route"
        runEntries.push_back({RunKind::Explore, -1, qtTrId("ui.explore.explore.route")});
        if (!activities.isEmpty())
            //% "Activities"
            runEntries.push_back({RunKind::Heading, -1, qtTrId("ui.explore.activities")});
        for (int i = 0; i < activities.size(); ++i)
            runEntries.push_back({RunKind::Activity, i, activities[i].name});
        if (!sets.isEmpty())
            //% "Timetables"
            runEntries.push_back({RunKind::Heading, -1, qtTrId("ui.explore.timetables")});
        for (int i = 0; i < sets.size(); ++i)
            runEntries.push_back({RunKind::Timetable, i, sets[i].name});
        // The remembered path's start, else the first one.
        const QString keptPath = remembered.value("path").toString();
        path.clear();
        for (const Lists::PathInfo &p : paths)
            if (p.file == keptPath)
                path = p.file;
        if (path.isEmpty() && !paths.isEmpty())
            path = paths.first().file;
        startName = chosenPath() != nullptr ? chosenPath()->start : QString();
        setStations.clear();
        // The remembered run of this route.
        if (remembered.value("route").toString() == r.folder) {
            const QString kept = remembered.value("run").toString();
            for (int i = 0; i < runEntries.size(); ++i) {
                const RunEntry &e = runEntries[i];
                if ((e.kind == RunKind::Explore && kept == "explore")
                        || (e.kind == RunKind::Activity && kept == "activity:" + activities[e.index].file)
                        || (e.kind == RunKind::Timetable && kept == "timetable:" + sets[e.index].file))
                    chooseRun(i);
            }
        }
        refresh();
    }

    void chooseRun(int index) {
        if (index < 0 || index >= runEntries.size() || runEntries[index].kind == RunKind::Heading)
            return;
        run = index;
        status.clear();
        const RunEntry &entry = runEntries[index];
        if (entry.kind == RunKind::Explore)
            readConsists();
        if (entry.kind == RunKind::Timetable) {
            QElapsedTimer timer;
            timer.start();
            trains = Lists::timetableTrains(root, chosenRoute()->folder, sets[entry.index].file, &setStations);
            qInfo().noquote() << QString("Explore: %1: %2 trains, %3 stations read in %4 ms")
                                         .arg(sets[entry.index].file).arg(trains.size()).arg(setStations.size())
                                         .arg(timer.elapsed());
            train = remembered.value("trains").toObject().value(sets[entry.index].file).toString();
            if (chosenTrain() == nullptr)
                train.clear();
            filterTrains();
        }
        if (needsRouteStations())
            readRouteStations();
        if (walk)
            chooseStationFor(true);
        refresh();
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
                    && !t.to.contains(text, Qt::CaseInsensitive))
                continue;
            shownTrains << i;
        }
        if (!sortColumn.isEmpty()) {
            QCollator collator;
            collator.setNumericMode(true);
            collator.setCaseSensitivity(Qt::CaseInsensitive);
            const auto key = [this](const Lists::TrainInfo &t) -> QString {
                if (sortColumn == "start")
                    return QString("%1").arg(t.startTime < 0 ? 999999 : t.startTime, 6, 10, QChar('0'));
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

    // The picker for a choice: its title, rows and the row chosen now.
    void openPicker(const QString &what) {
        QVector<Row> rows;
        QStringList values;
        int chosen = -1;
        QString title;
        if (what == "start") {
            //% "Start at"
            title = qtTrId("ui.explore.start.at");
            // Walking: no train at all first (task explore 04).
            if (walk) {
                if (!exploreTrain)
                    chosen = 0;
                //% "No train"
                rows.push_back({rml(qtTrId("ui.explore.no.train")), {}, false});
                values << QString();
            }
            for (const QString &s : starts()) {
                if (s == startName && exploresWithTrain())
                    chosen = values.size();
                rows.push_back({rml(s), {}, false});
                values << s;
            }
        } else if (what == "station") {
            //% "Station"
            title = qtTrId("ui.explore.station");
            for (const StationChoice &c : stationChoices()) {
                if (c.name.compare(station, Qt::CaseInsensitive) == 0)
                    chosen = values.size();
                rows.push_back({rml(c.name), rml(c.note), false});
                values << c.name;
            }
        } else if (what == "heading") {
            //% "Heading to"
            title = qtTrId("ui.explore.heading.to");
            for (const Lists::PathInfo *p : headings()) {
                if (p->file == path)
                    chosen = values.size();
                rows.push_back({rml(p->end), rml(p->name), false});
                values << p->file;
            }
        } else if (what == "loco") {
            //% "Locomotive"
            title = qtTrId("ui.explore.locomotive");
            for (const QString &l : locos()) {
                if (l == loco)
                    chosen = values.size();
                rows.push_back({rml(l), {}, false});
                values << l;
            }
        } else if (what == "consist") {
            //% "Consist"
            title = qtTrId("ui.explore.consist");
            for (const Lists::ConsistInfo *c : locoConsists()) {
                if (c->file == consist)
                    chosen = values.size();
                rows.push_back({rml(c->name), {}, false});
                values << c->file;
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
        focusWanted = "picker";
        focusTries = 0;
        refresh();
    }

    void pickOption(int index) {
        if (picking.isEmpty() || index < 0 || index >= pickerValues.size())
            return;
        const QString value = pickerValues[index];
        if (picking == "start" && value.isEmpty()) {
            exploreTrain = false;
        } else if (picking == "start") {
            exploreTrain = true;
            startName = value;
            // Its first path.
            const QVector<const Lists::PathInfo *> list = headings();
            path = list.isEmpty() ? QString() : list.first()->file;
        } else if (picking == "station") {
            station = value;
            chooseStationFor(true);
        } else if (picking == "heading") {
            path = value;
        } else if (picking == "loco") {
            loco = value;
            const QVector<const Lists::ConsistInfo *> list = locoConsists();
            consist = list.isEmpty() ? QString() : list.first()->file;
        } else if (picking == "consist") {
            consist = value;
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
        closePicker();
    }

    void closePicker() {
        if (picking.isEmpty())
            return;
        focusWanted = "pick-" + picking;
        focusTries = 0;
        picking.clear();
        pickerValues.clear();
        mOptions.clear();
        mOption = -1;
        refresh();
    }

    // The model's variables from the choices.
    void refresh() {
        mRoutes.clear();
        for (const Lists::RouteInfo &r : routes)
            mRoutes.push_back({rml(r.name), {}, false});
        mRoute = route;
        mRuns.clear();
        for (const RunEntry &e : runEntries)
            mRuns.push_back({rml(e.name), {}, e.kind == RunKind::Heading});
        mRun = run;
        const Lists::RouteInfo *r = chosenRoute();
        mRouteName = r != nullptr ? rml(r->name) : Rml::String();
        mRouteDescription = r != nullptr ? rml(r->description) : Rml::String();
        mPicture = r != nullptr ? rml(r->picture) : Rml::String();
        switch (runKind()) {
        case RunKind::Explore: mKind = "explore"; break;
        case RunKind::Activity: mKind = "activity"; break;
        case RunKind::Timetable: mKind = "timetable"; break;
        default: mKind.clear(); break;
        }
        const Lists::PathInfo *p = chosenPath();
        mStart = exploresWithTrain() ? rml(startName) : rml(qtTrId("ui.explore.no.train"));
        mHeading = p != nullptr ? rml(p->end) : Rml::String();
        mWalk = walk;
        mExploreTrain = exploresWithTrain();
        mNoTrain = walk && runKind() == RunKind::Timetable && chosenTrain() == nullptr;
        mStationsLoading = needsRouteStations() && stationsOfRoute() == nullptr;
        {
            const QVector<StationChoice> list = stationChoices();
            const StationChoice *c = chosenStation(list);
            mStation = c != nullptr ? rml(c->note.isEmpty() ? c->name : c->name + "  " + c->note) : Rml::String();
        }
        mLoco = rml(loco);
        const Lists::ConsistInfo *c = chosenConsist();
        mConsist = c != nullptr ? rml(c->name) : Rml::String();
        mConsistsLoading = runKind() == RunKind::Explore && !consistsDone;
        mTime = rml(clock(time));
        mDay = rml(dayText(day));
        mSeason = rml(seasonName(Lists::seasonOf(day, latitude())));
        mWeather = rml(weatherName(weather));
        const RunEntry *entry = chosenRun();
        if (entry != nullptr && entry->kind == RunKind::Activity) {
            const Lists::ActivityInfo &a = activities[entry->index];
            mActDescription = rml(a.description);
            mActBriefing = rml(a.briefing);
            mActStart = rml(clock(a.startTime));
            mActSeason = rml(seasonName(a.season));
            mActWeather = rml(weatherName(a.weather));
            mActDuration = a.duration > 0 ? rml(clock(a.duration * 60)) : Rml::String();
            mActDifficulty = rml(difficultyName(a.difficulty));
            mActConsist = rml(a.consist);
            mActPath = rml(a.path);
        }
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
            mTrains.push_back({rml(t.name), rml(clock(t.startTime)), rml(t.from), rml(t.to), rml(t.category),
                               rml(t.number), rml(t.line), rml(t.engine), t.drivable});
        }
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
        if (!e->IsVisible(true) || e->IsClassSet("later"))
            return false;
        const Rml::String tag = e->GetTagName();
        return tag == "button" || tag == "input";
    }

    // A column's element to focus: its chosen item, else its first control;
    // null when it has none shown.
    Rml::Element *columnTarget(const QString &column) const {
        if (document == nullptr)
            return nullptr;
        if (column == "go")
            return document->GetElementById("go");
        const char *id = column == "routes" ? "routes-column" : column == "runs" ? "runs-column" : "details-column";
        Rml::Element *parent = document->GetElementById(id);
        if (parent == nullptr)
            return nullptr;
        Rml::ElementList chosen;
        parent->GetElementsByClassName(chosen, "chosen");
        for (Rml::Element *e : chosen)
            if (shownFocusable(e))
                return e;
        // A timetable's trains first, then a button: a text field would take
        // the arrows (the filter).
        Rml::ElementList rows;
        parent->QuerySelectorAll(rows, "#trains button");
        for (Rml::Element *e : rows)
            if (shownFocusable(e))
                return e;
        Rml::ElementList all;
        parent->QuerySelectorAll(all, "button, input");
        // Drive or Walk (task explore 04) after the choices: Up reaches it.
        Rml::Element *field = nullptr, *mode = nullptr;
        for (Rml::Element *e : all)
            if (shownFocusable(e)) {
                Rml::Element *row = e->GetParentNode();
                if (row != nullptr && row->IsClassSet("modes")) {
                    if (mode == nullptr)
                        mode = e;
                    continue;
                }
                if (e->GetTagName() != "input")
                    return e;
                if (field == nullptr)
                    field = e;
            }
        return field != nullptr ? field : mode;
    }

    QString columnOf(Rml::Element *focus) const {
        for (Rml::Element *e = focus; e != nullptr; e = e->GetParentNode()) {
            const Rml::String id = e->GetId();
            if (id == "routes-column")
                return "routes";
            if (id == "runs-column")
                return "runs";
            if (id == "details-column")
                return "details";
            if (id == "go")
                return "go";
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
        row.RegisterMember("from", &TrainRow::from);
        row.RegisterMember("to", &TrainRow::to);
        row.RegisterMember("category", &TrainRow::category);
        row.RegisterMember("number", &TrainRow::number);
        row.RegisterMember("line", &TrainRow::line);
        row.RegisterMember("engine", &TrainRow::engine);
        row.RegisterMember("drivable", &TrainRow::drivable);
    }
    constructor.RegisterArray<Rml::Vector<TrainRow>>();
    Data &m = *d;
    constructor.Bind("routes", &m.mRoutes);
    constructor.Bind("runs", &m.mRuns);
    constructor.Bind("options", &m.mOptions);
    constructor.Bind("trains", &m.mTrains);
    constructor.Bind("route", &m.mRoute);
    constructor.Bind("run", &m.mRun);
    constructor.Bind("train", &m.mTrain);
    constructor.Bind("option", &m.mOption);
    constructor.Bind("kind", &m.mKind);
    constructor.Bind("routeName", &m.mRouteName);
    constructor.Bind("routeDescription", &m.mRouteDescription);
    constructor.Bind("picture", &m.mPicture);
    constructor.Bind("startName", &m.mStart);
    constructor.Bind("headingName", &m.mHeading);
    constructor.Bind("locoName", &m.mLoco);
    constructor.Bind("consistName", &m.mConsist);
    constructor.Bind("consistsLoading", &m.mConsistsLoading);
    constructor.Bind("time", &m.mTime);
    constructor.Bind("day", &m.mDay);
    constructor.Bind("season", &m.mSeason);
    constructor.Bind("weather", &m.mWeather);
    constructor.Bind("actDescription", &m.mActDescription);
    constructor.Bind("actBriefing", &m.mActBriefing);
    constructor.Bind("actStart", &m.mActStart);
    constructor.Bind("actSeason", &m.mActSeason);
    constructor.Bind("actWeather", &m.mActWeather);
    constructor.Bind("actDuration", &m.mActDuration);
    constructor.Bind("actDifficulty", &m.mActDifficulty);
    constructor.Bind("actConsist", &m.mActConsist);
    constructor.Bind("actPath", &m.mActPath);
    constructor.Bind("sortColumn", &m.mSortColumn);
    constructor.Bind("sortUp", &m.mSortUp);
    constructor.Bind("drivableOnly", &m.mDrivableOnly);
    constructor.Bind("hasCategory", &m.mHasCategory);
    constructor.Bind("hasNumber", &m.mHasNumber);
    constructor.Bind("hasLine", &m.mHasLine);
    constructor.Bind("picking", &m.mPicking);
    constructor.Bind("pickerTitle", &m.mPickerTitle);
    constructor.Bind("ready", &m.mReady);
    constructor.Bind("status", &m.mStatus);
    constructor.Bind("walk", &m.mWalk);
    constructor.Bind("stationName", &m.mStation);
    constructor.Bind("stationsLoading", &m.mStationsLoading);
    constructor.Bind("noTrain", &m.mNoTrain);
    constructor.Bind("exploreTrain", &m.mExploreTrain);
    // The filter typed: the list follows each letter.
    constructor.BindFunc("filter", [this](Rml::Variant &value) { value = d->mFilter; },
                         [this](const Rml::Variant &value) { setFilter(QString::fromStdString(value.Get<Rml::String>())); });
    const auto index = [](const Rml::VariantList &arguments) {
        return arguments.empty() ? -1 : arguments[0].Get<int>(-1);
    };
    constructor.BindEventCallback("chooseRoute", [this, index](Rml::DataModelHandle, Rml::Event &,
                                                               const Rml::VariantList &arguments) {
        d->chooseRoute(index(arguments));
        d->focusLater("column:runs");
    });
    constructor.BindEventCallback("chooseRun", [this, index](Rml::DataModelHandle, Rml::Event &,
                                                             const Rml::VariantList &arguments) {
        d->chooseRun(index(arguments));
        d->focusLater("column:details");
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
            // On to Explore, as Enter on a route goes to its runs.
            if (d->ready())
                d->focusLater("column:go");
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
    if (d->focusWanted.startsWith("column:")) {
        element = d->columnTarget(d->focusWanted.mid(7));
    } else if (d->focusWanted == "picker") {
        Rml::Element *list = d->document->GetElementById("picker-options");
        element = d->shownButton(list, std::max(0, d->mOption));
    } else {
        // A choice's button: the one shown (each run's details have theirs).
        Rml::ElementList found;
        d->document->QuerySelectorAll(found, ("#" + d->focusWanted).toStdString());
        for (Rml::Element *e : found)
            if (e->IsVisible(true))
                element = e;
    }
    if (element != nullptr) {
        d->focus(element);
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
    for (const Data::RunEntry &e : d->runEntries)
        if (e.kind != Data::RunKind::Heading)
            names << e.name;
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
    for (int i = 0; i < d->runEntries.size(); ++i) {
        const Data::RunEntry &e = d->runEntries[i];
        const QString file = e.kind == Data::RunKind::Activity ? d->activities[e.index].file
                           : e.kind == Data::RunKind::Timetable ? d->sets[e.index].file : QString();
        if (e.kind != Data::RunKind::Heading && (e.name == name || file.compare(name, Qt::CaseInsensitive) == 0
                                                 || (e.kind == Data::RunKind::Explore && name == "explore"))) {
            d->chooseRun(i);
            return true;
        }
    }
    return false;
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
    const Data::RunEntry *entry = d->chosenRun();
    if (r == nullptr || entry == nullptr)
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
    switch (entry->kind) {
    case Data::RunKind::Explore:
        choice.kind = RunChoice::Kind::Explore;
        if (d->exploresWithTrain()) {
            choice.path = d->path;
            choice.consist = d->consist;
        }
        choice.time = d->time;
        break;
    case Data::RunKind::Activity: {
        const Lists::ActivityInfo &a = d->activities[entry->index];
        choice.kind = RunChoice::Kind::Activity;
        choice.file = a.file;
        // Walking: from the time chosen, no earlier than its start.
        choice.time = d->walk ? std::max(a.startTime, d->time) : a.startTime;
        // Its own season and weather.
        choice.season = a.season >= 0 ? a.season : choice.season;
        choice.weather = a.weather >= 0 ? a.weather : 0;
        break;
    }
    case Data::RunKind::Timetable:
        choice.kind = RunChoice::Kind::Timetable;
        choice.file = d->sets[entry->index].file;
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
    // A text field takes the arrows; buttons side by side (a pair, the
    // table's head, the filter's row) take them to each other.
    if (focus->GetTagName() == "input")
        return false;
    Rml::Element *parent = focus->GetParentNode();
    if (parent != nullptr && (parent->IsClassSet("pair") || parent->IsClassSet("table-head") || parent->IsClassSet("tools"))) {
        Rml::Element *next = right ? focus->GetNextSibling() : focus->GetPreviousSibling();
        while (next != nullptr && !Data::shownFocusable(next))
            next = right ? next->GetNextSibling() : next->GetPreviousSibling();
        if (next != nullptr)
            return false;
    }
    // Right from the trains' table: the day and weather under it first.
    if (right) {
        Rml::Element *trains = d->document->GetElementById("trains");
        bool inTable = false;
        for (Rml::Element *e = focus; e != nullptr && !inTable; e = e->GetParentNode())
            inTable = e == trains;
        if (inTable) {
            Rml::ElementList under;
            d->document->QuerySelectorAll(under, ".timetable .under button");
            for (Rml::Element *e : under)
                if (Data::shownFocusable(e)) {
                    e->Focus(true);
                    return true;
                }
        }
    }
    static const QStringList order = {"routes", "runs", "details", "go"};
    const int at = order.indexOf(d->columnOf(focus));
    if (at < 0 || (!right && at == 0))
        return false;
    for (int i = at + (right ? 1 : -1); i >= 0 && i < order.size(); i += right ? 1 : -1)
        if (Rml::Element *target = d->columnTarget(order[i])) {
            target->Focus(true);
            target->ScrollIntoView(Rml::ScrollAlignment::Nearest);
            return true;
        }
    // Nothing that way: the keys stay.
    return true;
}

Rml::Element *Panel::entry() const {
    if (d->document == nullptr)
        return nullptr;
    return d->shownButton(d->document->GetElementById("routes"), std::max(0, d->route));
}

}
