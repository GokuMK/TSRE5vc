#include <tsre/tests/ActivityExportTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <tsre/Game.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityEvent.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/PathTrack.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Traffic.h>
#include <tsre/world/Route.h>

namespace {

using Outcome = ActivityEvent::Outcome;

// The activity read again from its file, to be saved under another name.
Activity *variant(const QString &folder, const QString &file, const QString &name, const QString &title) {
    Activity *activity = new Activity(folder + "/" + file, folder, file);
    if (activity->loaded != 1 || activity->header == nullptr)
        return nullptr;
    activity->nameid = name;
    activity->name = name + ".act";
    activity->pathid = folder + "/" + activity->name;
    activity->setDisplayName(title);
    return activity;
}

Outcome *outcome(ActivityEvent *event, Outcome::OutcomeType type) {
    Outcome *added = event->outcomes.value(event->newOutcome());
    added->setToNewType(type);
    return added;
}

}

int TsreTests::runActivityExportSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:activity-export] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:activity-export] FAIL" << name; }
    };
    if (!QFileInfo::exists(Game::root + "/TSRE_SCRATCH")) {
        qWarning() << "activity-export writes into the route: the game root must be a scratch copy with a file TSRE_SCRATCH";
        return 1;
    }
    const QDir sourceRoute(Game::root + "/ROUTES/" + Game::route);
    const auto databases = sourceRoute.entryInfoList({"*.tdb"}, QDir::Files);
    if (databases.size() != 1) {
        qWarning() << "activity-export requires a route with one TDB (--game-root, --route)";
        return 1;
    }
    // The files of an earlier run go first (their names would be taken).
    for (const QString &dir : {"ACTIVITIES", "TRAFFIC"})
        for (const QString &entry : QDir(sourceRoute.filePath(dir)).entryList({"tsre_*"}, QDir::Files))
            QFile::remove(sourceRoute.filePath(dir) + "/" + entry);
    const QScopedValueRollback<QString> routeName(Game::routeName, databases.first().completeBaseName());
    const QScopedValueRollback<bool> write(Game::writeEnabled, true);
    TSectionDAT sections(false, false);
    if (QFileInfo::exists(sourceRoute.filePath("tsection.dat")))
        sections.loadRoute(false);
    TDB database(&sections, false);
    database.loadTdb();
    if (!database.loaded)
        return 1;
    const QScopedValueRollback<TDB*> trackDB(Game::trackDB, &database);
    EngLib engines;
    const QScopedValueRollback<EngLib*> engineLibrary(Game::currentEngLib,
            Game::currentEngLib != nullptr ? Game::currentEngLib : &engines);
    Route *content = new Route();
    content->loadServices();
    content->loadTraffic();
    content->loadPaths();
    content->loadActivities();

    const QString folder = sourceRoute.filePath("ACTIVITIES");
    QString base = qEnvironmentVariable("TSRE_EXPORT_ACTIVITY", "lclsrvce");
    for (const QString &entry : QDir(folder).entryList(QDir::Files))
        if (entry.compare(base + ".act", Qt::CaseInsensitive) == 0)
            base = entry;
    const QString routePath = sourceRoute.absolutePath();

    // Resaved unchanged.
    if (Activity *a = variant(folder, base, "tsre_resaved", "TSRE resaved")) {
        a->save();
        check(QFileInfo::exists(a->pathid) && !a->isUnSaved(), "tsre_resaved written");
    } else {
        check(false, "the activity " + base + " loads");
    }

    // Open Rails' additions, as the windows make them.
    if (Activity *a = variant(folder, base, "tsre_additions", "TSRE Open Rails additions")) {
        a->setOrtsHornAtCrossigns(true);
        a->setOrtsHornPattern("US");
        a->setFuelSand(80);
        float start[4] = {0, 0, 0, 0};
        Service *player = a->playerService();
        Path *playerPath = player != nullptr ? ActLib::GetPathByName(player->pathId, routePath) : nullptr;
        if (playerPath != nullptr)
            playerPath->getStartPositionTXZ(start);
        const ActivityServiceDefinition *ai = a->traffic != nullptr && !a->traffic->service.isEmpty()
                ? &a->traffic->service[0] : nullptr;

        ActivityEvent *weather = a->newEvent(ActivityEvent::CategoryTime);
        weather->setName("TSRE weather");
        weather->setTime(60);
        Outcome *change = outcome(weather, Outcome::TypeORTSWeatherChange);
        change->setWeather(Outcome::Overcast, 0.8f, 60);
        change->setWeather(Outcome::Fog, 800, 120);
        change->setWeather(Outcome::Intensity, 0.01f, 60);
        change->setWeather(Outcome::Liquidity, 1, 60);
        outcome(weather, Outcome::TypeDisplayMessage)->setMessage("TSRE: the weather changes.");

        ActivityEvent *sound = a->newEvent(ActivityEvent::CategoryTime);
        sound->setName("TSRE sound");
        sound->setTime(90);
        Outcome *everywhere = outcome(sound, Outcome::TypeORTSActSoundFile);
        everywhere->setSoundFileName("gen_uk_ind1.wav");
        everywhere->setSoundType("Everywhere");
        Outcome *placed = outcome(sound, Outcome::TypeORTSActSoundFile);
        placed->setSoundFileName("gen_uk_ind1.wav");
        placed->setSoundType("Location");
        placed->setSoundLocation(true, int(start[0]), int(start[1]), start[2], 10, start[3]);

        ActivityEvent *triggered = a->newEvent(ActivityEvent::CategoryLocation);
        triggered->setName("TSRE triggered by an AI train");
        triggered->setLocation(int(start[0]), int(start[1]), start[2], start[3]);
        triggered->setLocationRadius(30);
        triggered->setReversable(true);
        triggered->setAutoContinue(10);
        if (ai != nullptr)
            triggered->setTriggeringTrain(ai->name, ai->time);
        outcome(triggered, Outcome::TypeDisplayMessage)->setMessage("TSRE: an AI train passed.");

        ActivityEvent *restart = a->newEvent(ActivityEvent::CategoryTime);
        restart->setName("TSRE restart");
        restart->setTime(120);
        if (ai != nullptr) {
            Outcome *again = outcome(restart, Outcome::TypeORTSRestartWaitingTrain);
            again->setRestartTrain(ai->name, ai->time);
            again->setRestartDelay(60);
            again->setRestartWaitingPoint(31500);
        }
        a->save();
        check(QFileInfo::exists(a->pathid) && !a->isUnSaved(), "tsre_additions written");
        if (verbose)
            qInfo().noquote() << a->toText();
    }

    // Its own traffic pattern, a stop moved, an AI train taken out.
    if (Activity *a = variant(folder, base, "tsre_own_traffic", "TSRE own traffic")) {
        Traffic *copy = ActivityTrains::CopyPattern(a, "tsre_own_traffic");
        bool moved = false;
        if (copy != nullptr)
            for (ActivityTimetable *run : copy->service)
                if (run != nullptr && run->departTime.size() > 1 && !moved) {
                    run->setDepart(0, run->departTime[0] + 30);
                    moved = true;
                }
        if (a->traffic != nullptr && a->traffic->service.size() > 1)
            a->removeAITrain(a->traffic->service.size() - 1);
        check(copy != nullptr && moved && copy->isModified(), "an own traffic pattern with a stop moved");
        if (copy != nullptr)
            copy->save();
        a->save();
        check(QFileInfo::exists(a->pathid) && !a->isUnSaved(), "tsre_own_traffic written");
    }

    // Waiting points of Open Rails' kinds on the first AI train's path,
    // saved over the scratch copy of the path.
    if (Activity *a = variant(folder, base, "tsre_waits", "TSRE waiting points")) {
        Service *service = a->traffic != nullptr && !a->traffic->service.isEmpty()
                ? ActLib::GetServiceByName(a->traffic->service[0].name, routePath) : nullptr;
        Path *path = service != nullptr ? ActLib::GetPathByName(service->pathId, routePath) : nullptr;
        int added = 0;
        if (path != nullptr) {
            QVector<PathTrack::Node> nodes = path->nodes();
            const QVector<PathTrack::Leg> legs = path->legs();
            // TSRE_EXPORT_WAIT: the waiting point's value (31500, until
            // 15:00, by default: the one the additions' restart matches);
            // 0 saves the path as read.
            const int value = qEnvironmentVariableIntValue("TSRE_EXPORT_WAIT") != 0 || !qEnvironmentVariableIsSet("TSRE_EXPORT_WAIT")
                    ? (qEnvironmentVariableIsSet("TSRE_EXPORT_WAIT") ? qEnvironmentVariableIntValue("TSRE_EXPORT_WAIT") : 31500) : 0;
            if (value == 0) {
                path->apply(nodes);
                check(path->save(), "the AI path saved as read");
                added = 1;
            }
            for (int k = 0; value != 0 && k < legs.size(); ++k) {
                const PathTrack::Leg &leg = legs[k];
                if (leg.broken || std::fabs(leg.to - leg.from) < 200)
                    continue;
                const PathTrack::Result result = PathTrack::waitAt(&database, nodes,
                        {leg.track, (leg.from + leg.to) / 2}, value);
                if (!result.ok)
                    continue;
                nodes = result.nodes;
                ++added;
                break;
            }
            if (added > 0 && value != 0) {
                path->apply(nodes);
                check(path->save(), "the AI path saved with a waiting point");
            }
        }
        check(added > 0, "a waiting point added on the AI path");
        a->save();
        check(QFileInfo::exists(a->pathid) && !a->isUnSaved(), "tsre_waits written");
    }

    qInfo().noquote() << QString("activity-export: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
