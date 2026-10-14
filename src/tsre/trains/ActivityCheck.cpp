#include <tsre/trains/ActivityCheck.h>

#include <QDir>
#include <QSet>
#include <QTime>
#include <algorithm>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/trains/ActivityOverlay.h>
#include <QFileInfo>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/ServiceStops.h>
#include <tsre/trains/Traffic.h>

namespace {

// How far a stop may be from where its pass meets its platform item (MSAE's
// distances vary by some metres).
constexpr float StopTolerance = 50.0f;

QString clock(int seconds) {
    return QTime::fromMSecsSinceStartOfDay((seconds % 86400 + 86400) % 86400 * 1000).toString("HH:mm:ss");
}

bool fileInFolder(const QString &folder, const QString &name) {
    for (const QString &entry : QDir(folder).entryList(QDir::Files))
        if (entry.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

const TRitem *trackItem(int id) {
    if (Game::trackDB == NULL || !Game::trackDB->loaded)
        return NULL;
    const auto found = Game::trackDB->trackItems.find(id);
    return found != Game::trackDB->trackItems.end() ? found->second : NULL;
}

class Checker {
public:
    Checker(Activity *activity) : activity(activity),
        routePath(ContentPath::parentDirectory(activity->path)) {}

    QVector<ActivityFinding> run() {
        services();
        instances();
        timetables();
        events();
        consists();
        overlay();
        return findings;
    }

private:
    Activity *activity;
    QString routePath;
    QVector<ActivityFinding> findings;
    QSet<QString> pathsChecked;

    ActivityFinding &add(ActivityFinding::Severity severity, const QString &code, const QString &text) {
        ActivityFinding finding;
        finding.severity = severity;
        finding.code = code;
        finding.text = text;
        findings.push_back(finding);
        return findings.back();
    }
    void target(ActivityFinding &finding, ActivityFinding::Target kind, const QString &name) {
        finding.target = kind;
        finding.name = name;
    }

    // The services the activity runs: the player's, then each AI train's.
    void services() {
        QSet<QString> seen;
        ActivityServiceDefinition *player = activity->playerServiceDefinition;
        if (player != NULL && !player->empty)
            service(player->name, true, seen);
        if (activity->traffic != NULL)
            for (const ActivityServiceDefinition &instance : activity->traffic->service)
                service(instance.name, false, seen);
    }

    void service(const QString &name, bool player, QSet<QString> &seen) {
        if (seen.contains(name.toLower()))
            return;
        seen.insert(name.toLower());
        Service *service = ActLib::GetServiceByName(name, routePath);
        if (service == NULL) {
            ActivityFinding &finding = player
                ? add(ActivityFinding::Error, "player.service.missing",
                      //% "The player service %1 does not exist."
                      qtTrId("activity.check.player.service.missing").arg(name))
                : add(ActivityFinding::Error, "instance.service.missing",
                      //% "The service %1 of an AI train does not exist."
                      qtTrId("activity.check.instance.service.missing").arg(name));
            target(finding, ActivityFinding::Service, name);
            return;
        }
        if (service->trainConfig.isEmpty()) {
            target(add(ActivityFinding::Error, "service.no.consist",
                       //% "The service %1 has no consist."
                       qtTrId("activity.check.service.no.consist").arg(name)),
                   ActivityFinding::Service, name);
        } else if (!fileInFolder(Game::root + "/TRAINS/CONSISTS", service->trainConfig + ".con")) {
            target(add(ActivityFinding::Error, "service.consist.missing",
                       //% "The service %1 names the consist %2, which does not exist."
                       qtTrId("activity.check.service.consist.missing").arg(name, service->trainConfig)),
                   ActivityFinding::Service, name);
        }
        for (const Service::StationStop &stop : service->stationStop)
            if (stop.skipCount > 65535)
                target(add(ActivityFinding::Warning, "skipcount.narrow",
                           //% "%1: a skip count above 65535, more than MSTS keeps."
                           qtTrId("activity.check.skipcount.narrow").arg(name)),
                       ActivityFinding::Service, name);
        if (service->pathId.isEmpty()) {
            target(add(ActivityFinding::Error, "service.no.path",
                       //% "The service %1 has no path."
                       qtTrId("activity.check.service.no.path").arg(name)),
                   ActivityFinding::Service, name);
            return;
        }
        Path *path = ActLib::GetPathByName(service->pathId, routePath);
        if (path == NULL) {
            target(add(ActivityFinding::Error, "service.path.missing",
                       //% "The service %1 names the path %2, which does not exist."
                       qtTrId("activity.check.service.path.missing").arg(name, service->pathId)),
                   ActivityFinding::Service, name);
            return;
        }
        checkPath(path);
        stops(service, path);
    }

    void checkPath(Path *path) {
        if (pathsChecked.contains(path->nameId))
            return;
        pathsChecked.insert(path->nameId);
        path->nodes();
        if (path->droppedPassingPaths() > 0)
            target(add(ActivityFinding::Warning, "path.passing.unjoined",
                       //% "The path %1 has a passing path that does not come back to it; TSRE does not keep it."
                       qtTrId("activity.check.path.passing.unjoined").arg(path->nameId)),
                   ActivityFinding::Path, path->nameId);
        if (Game::trackDB != NULL && Game::trackDB->loaded && path->isBroken())
            target(add(ActivityFinding::Error, "path.broken",
                       //% "The path %1 is broken: a node or the track between nodes is not on the track."
                       qtTrId("activity.check.path.broken").arg(path->nameId)),
                   ActivityFinding::Path, path->nameId);
    }

    // The service's stops: on its path, near where the path passes them,
    // in the order it does.
    void stops(Service *service, Path *path) {
        if (Game::trackDB == NULL || !Game::trackDB->loaded || path->isBroken())
            return;
        path->nodes();
        // Each stop at its pass over its platform (Path::PathObject: the
        // passes as MSTS counts them).
        QVector<Path::PathObject> passes;
        for (const Path::PathObject *pass : path->pathObjects)
            passes.push_back(*pass);
        const QVector<int> at = ServiceStops::Passes(service->stationStop, passes);
        const QString name = service->nameId;
        int previous = -1;
        bool ordered = true;
        for (int i = 0; i < service->stationStop.size(); ++i) {
            const Service::StationStop &stop = service->stationStop[i];
            const TRitem *item = trackItem(stop.platformStartID);
            const QString platform = item != NULL ? item->platformName : QString::number(stop.platformStartID);
            if (at[i] < 0) {
                target(add(ActivityFinding::Warning, "stop.not.on.path",
                           //% "%1: the stop at %2 is not on the path %3."
                           qtTrId("activity.check.stop.not.on.path").arg(name, platform, path->nameId)),
                       ActivityFinding::Service, name);
                continue;
            }
            const Path::PathObject &pass = passes[at[i]];
            float distance = pass.distanceDownPath;
            if (!pass.meets(stop.platformStartID, &distance) && item != NULL && item->platformTrItemData != NULL)
                pass.meets(int(item->platformTrItemData[1]), &distance);
            const float off = std::fabs(distance - stop.distanceDownPath);
            if (off > StopTolerance)
                target(add(ActivityFinding::Warning, "stop.distance",
                           //% "%1: the stop at %2 is %3 m from where the path passes it."
                           qtTrId("activity.check.stop.distance").arg(name, platform).arg(int(off))),
                       ActivityFinding::Service, name);
            if (at[i] < previous)
                ordered = false;
            previous = at[i];
        }
        if (!ordered)
            target(add(ActivityFinding::Warning, "stops.order",
                       //% "%1: the stops are not in the order the path passes them."
                       qtTrId("activity.check.stops.order").arg(name)),
                   ActivityFinding::Service, name);
    }

    // AI trains in the activity and in its traffic pattern, by service and
    // start time.
    void instances() {
        if (activity->traffic == NULL)
            return;
        Traffic *traffic = ActLib::GetTrafficByName(activity->traffic->name, routePath);
        if (traffic == NULL) {
            target(add(ActivityFinding::Error, "traffic.missing",
                       //% "The traffic pattern %1 does not exist."
                       qtTrId("activity.check.traffic.missing").arg(activity->traffic->name)),
                   ActivityFinding::Traffic, activity->traffic->name);
            return;
        }
        for (const ActivityServiceDefinition &instance : activity->traffic->service)
            if (traffic->getInstance(instance.name, instance.time) == NULL)
                // Open Rails runs it without the pattern's timetable.
                target(add(ActivityFinding::Warning, "instance.not.in.traffic",
                           //% "The AI train %1 at %2 is not in the traffic pattern %3: it runs without its timetable."
                           qtTrId("activity.check.instance.not.in.traffic")
                           .arg(instance.name, clock(instance.time), traffic->nameId)),
                       ActivityFinding::Traffic, traffic->nameId);
        for (const ActivityTimetable *instance : traffic->service) {
            if (instance == NULL || instance->empty)
                continue;
            bool found = false;
            for (const ActivityServiceDefinition &own : activity->traffic->service)
                found = found || (own.time == instance->time
                                  && own.name.compare(instance->name, Qt::CaseInsensitive) == 0);
            if (!found)
                target(add(ActivityFinding::Warning, "instance.not.in.activity",
                           //% "The traffic pattern's train %1 at %2 is not in the activity."
                           qtTrId("activity.check.instance.not.in.activity")
                           .arg(instance->name, clock(instance->time))),
                       ActivityFinding::Traffic, traffic->nameId);
        }
    }

    // Stops complete, times in order, the player starting with the activity.
    void timetables() {
        ActivityServiceDefinition *player = activity->playerServiceDefinition;
        if (player != NULL && !player->empty) {
            definition(*player, true);
            if (player->trafficDefinition != NULL && activity->header != NULL) {
                const int start = activity->header->startTime[0] * 3600 + activity->header->startTime[1] * 60
                        + activity->header->startTime[2];
                if (player->trafficDefinition->time != start)
                    add(ActivityFinding::Warning, "player.start",
                        //% "The player's timetable starts at %1, the activity at %2."
                        qtTrId("activity.check.player.start").arg(clock(player->trafficDefinition->time), clock(start)));
                timetable(player->trafficDefinition, player->name, ActivityFinding::Service);
            }
        }
        if (activity->traffic == NULL)
            return;
        for (const ActivityServiceDefinition &instance : activity->traffic->service)
            definition(instance, false);
        Traffic *traffic = ActLib::GetTrafficByName(activity->traffic->name, routePath);
        if (traffic != NULL)
            for (const ActivityTimetable *instance : traffic->service)
                if (instance != NULL && !instance->empty)
                    timetable(instance, instance->name + " " + clock(instance->time), ActivityFinding::Traffic,
                              traffic->nameId);
    }

    void definition(const ActivityServiceDefinition &definition, bool player) {
        const QString name = player ? definition.name : definition.name + " " + clock(definition.time);
        const int stops = std::max({definition.efficiency.size(), definition.skipCount.size(),
                                    definition.distanceDownPath.size(), definition.platformStartId.size()});
        if (definition.efficiency.size() != stops || definition.skipCount.size() != stops
                || definition.distanceDownPath.size() != stops || definition.platformStartId.size() != stops)
            add(ActivityFinding::Error, "stops.incomplete",
                //% "%1: a stop lacks some of its fields."
                qtTrId("activity.check.stops.incomplete").arg(name));
        for (int count : definition.skipCount)
            if (count > 65535)
                add(ActivityFinding::Warning, "skipcount.narrow",
                    qtTrId("activity.check.skipcount.narrow").arg(name));
        // The activity's copy of the stops against the service's.
        Service *service = ActLib::GetServiceByName(definition.name, routePath);
        if (service == NULL)
            return;
        bool same = definition.platformStartId.size() == service->stationStop.size();
        for (int i = 0; same && i < definition.platformStartId.size(); ++i)
            same = definition.platformStartId[i] == service->stationStop[i].platformStartID;
        if (!same)
            target(add(ActivityFinding::Warning, "timetable.stops.differ",
                       //% "%1: the activity's stops differ from the service's; its timetable needs computing again."
                       qtTrId("activity.check.timetable.stops.differ").arg(name)),
                   ActivityFinding::Service, definition.name);
    }

    void timetable(const ActivityTimetable *timetable, const QString &name, ActivityFinding::Target kind,
                   const QString &targetName = QString()) {
        const int stops = std::max({timetable->arrivalTime.size(), timetable->departTime.size(),
                                    timetable->skipCount.size(), timetable->distanceDownPath.size(),
                                    timetable->platformStartID.size()});
        if (timetable->arrivalTime.size() != stops || timetable->departTime.size() != stops
                || timetable->skipCount.size() != stops || timetable->distanceDownPath.size() != stops
                || timetable->platformStartID.size() != stops)
            target(add(ActivityFinding::Error, "stops.incomplete",
                       qtTrId("activity.check.stops.incomplete").arg(name)),
                   kind, targetName.isEmpty() ? name : targetName);
        for (int count : timetable->skipCount)
            if (count > 65535)
                add(ActivityFinding::Warning, "skipcount.narrow", qtTrId("activity.check.skipcount.narrow").arg(name));
        // Stops timed 0 have no time (MSTS writes them so).
        const int times = std::min(timetable->arrivalTime.size(), timetable->departTime.size());
        int left = -1;
        for (int i = 0; i < times; ++i) {
            if (timetable->arrivalTime[i] == 0 && timetable->departTime[i] == 0)
                continue;
            if (timetable->departTime[i] < timetable->arrivalTime[i])
                target(add(ActivityFinding::Warning, "time.depart.before.arrival",
                           //% "%1: at stop %2 it leaves before it arrives."
                           qtTrId("activity.check.time.depart.before.arrival").arg(name).arg(i + 1)),
                       kind, targetName.isEmpty() ? name : targetName);
            if (left >= 0 && timetable->arrivalTime[i] < left)
                target(add(ActivityFinding::Warning, "time.order",
                           //% "%1: it reaches stop %2 before it leaves the stop before."
                           qtTrId("activity.check.time.order").arg(name).arg(i + 1)),
                       kind, targetName.isEmpty() ? name : targetName);
            left = timetable->departTime[i];
        }
    }

    // What events name: stops, sidings, wagons, other events.
    void events() {
        QSet<int> ids;
        for (const ActivityEvent *event : activity->event)
            if (event != NULL)
                ids.insert(event->id);
        ActivityServiceDefinition *player = activity->playerServiceDefinition;
        for (const ActivityEvent *event : activity->event) {
            if (event == NULL)
                continue;
            const QString name = event->name.isEmpty() ? QString::number(event->id) : event->name;
            if (event->eventType == ActivityEvent::EventTypePickupPassengers && event->stationStop != -99999
                    && (player == NULL || event->stationStop < 0
                        || event->stationStop >= player->platformStartId.size()))
                eventTarget(add(ActivityFinding::Error, "event.station.missing",
                                //% "Event %1: the station stop it names is not one of the player's stops."
                                qtTrId("activity.check.event.station.missing").arg(name)), event);
            const bool atSiding = event->eventType == ActivityEvent::EventTypeAssembleTrainAtLocation
                    || event->eventType == ActivityEvent::EventTypeDropoffWagonsAtLocation;
            if (atSiding && Game::trackDB != NULL && Game::trackDB->loaded) {
                const TRitem *item = event->sidingItem >= 0 ? trackItem(event->sidingItem) : NULL;
                if (item == NULL)
                    eventTarget(add(ActivityFinding::Error, "event.siding.missing",
                                    //% "Event %1: the siding it names does not exist."
                                    qtTrId("activity.check.event.siding.missing").arg(name)), event);
            }
            for (const ActivityEvent::WagonList &wagon : event->wagonList)
                if (!wagonExists(wagon.uid))
                    eventTarget(add(ActivityFinding::Error, "event.wagon.missing",
                                    //% "Event %1: the car %2 is in no train of the activity."
                                    qtTrId("activity.check.event.wagon.missing")
                                    .arg(name, QString("%1-%2").arg(wagon.uid / 65536).arg(wagon.uid % 65536))), event);
            for (const ActivityEvent::Outcome *outcome : event->outcomes)
                if (outcome != NULL && outcome->category == ActivityEvent::Outcome::CategoryEvent
                        && !ids.contains(outcome->value.toInt()))
                    eventTarget(add(ActivityFinding::Warning, "event.link.missing",
                                    //% "Event %1: an outcome names event %2, which does not exist."
                                    qtTrId("activity.check.event.link.missing").arg(name).arg(outcome->value.toInt())),
                                event);
            // Open Rails: an AI train triggering the event, or restarted by
            // it at the waiting point of the value given.
            if (!event->triggeringService.isEmpty() && aiTrain(event->triggeringService, event->triggeringTime) == NULL)
                eventTarget(add(ActivityFinding::Error, "event.triggering.missing",
                                //% "Event %1: the AI train %2 that triggers it is not in the activity."
                                qtTrId("activity.check.event.triggering.missing")
                                .arg(name, train(event->triggeringService, event->triggeringTime))), event);
            for (const ActivityEvent::Outcome *outcome : event->outcomes) {
                if (outcome == NULL || outcome->type != ActivityEvent::Outcome::TypeORTSRestartWaitingTrain)
                    continue;
                const ActivityServiceDefinition *restarted = aiTrain(outcome->restartService, outcome->restartTime);
                if (restarted == NULL)
                    eventTarget(add(ActivityFinding::Error, "event.restart.missing",
                                    //% "Event %1: the AI train %2 it restarts is not in the activity."
                                    qtTrId("activity.check.event.restart.missing")
                                    .arg(name, train(outcome->restartService, outcome->restartTime))), event);
                else if (outcome->restartWaitingPoint > 0 && !waitsAt(restarted->name, outcome->restartWaitingPoint))
                    eventTarget(add(ActivityFinding::Warning, "event.restart.waiting.point",
                                    //% "Event %1: the path of the AI train %2 has no waiting point of value %3; Open Rails restarts nothing."
                                    qtTrId("activity.check.event.restart.waiting.point")
                                    .arg(name, restarted->name).arg(outcome->restartWaitingPoint)), event);
            }
        }
    }

    // One of the activity's AI trains: by service, and by start time when
    // one is given.
    const ActivityServiceDefinition *aiTrain(const QString &service, int time) {
        if (service.isEmpty() || activity->traffic == NULL)
            return NULL;
        for (const ActivityServiceDefinition &instance : activity->traffic->service)
            if (instance.name.compare(service, Qt::CaseInsensitive) == 0 && (time < 0 || instance.time == time))
                return &instance;
        return NULL;
    }

    static QString train(const QString &service, int time) {
        return time < 0 ? service : QString("%1 %2").arg(service, clock(time));
    }

    // Whether the service's path has a waiting point of the value; a
    // missing service or path is reported with the service.
    bool waitsAt(const QString &service, int value) {
        Service *found = ActLib::GetServiceByName(service, routePath);
        Path *path = found != NULL ? ActLib::GetPathByName(found->pathId, routePath) : NULL;
        if (path == NULL)
            return true;
        for (const PathTrack::Node &node : path->nodes())
            if (node.type == PathTrack::NodeType::Wait && node.waitTime == value)
                return true;
        return false;
    }

    void eventTarget(ActivityFinding &finding, const ActivityEvent *event) {
        finding.target = ActivityFinding::Event;
        finding.eventId = event->id;
    }

    // A car of a loose consist (by the object's ID) or of the player's train
    // (ID 0).
    bool wagonExists(unsigned int uid) {
        const int train = int(uid / 65536);
        const int car = int(uid % 65536);
        const Consist *consist = NULL;
        if (train == 0) {
            ActivityServiceDefinition *player = activity->playerServiceDefinition;
            Service *service = player != NULL ? ActLib::GetServiceByName(player->name, routePath) : NULL;
            if (service == NULL || service->trainConfig.isEmpty()
                    || !fileInFolder(Game::root + "/TRAINS/CONSISTS", service->trainConfig + ".con"))
                return true; // reported with the service
            consist = ConLib::con[ConLib::addCon(Game::root + "/TRAINS/CONSISTS/", service->trainConfig + ".con")];
        } else {
            for (const ActivityObject *object : activity->activityObjects)
                if (object != NULL && object->id == train)
                    consist = object->con;
        }
        if (consist == NULL)
            return false;
        for (const Consist::EngItem &item : consist->engItems)
            if (item.uid == car)
                return true;
        return false;
    }

    // An Open Rails overlay adds to the activity when Open Rails loads it:
    // noted, with what it names that the activity no longer has.
    void overlay() {
        const QString path = ActivityOverlay::PathOf(activity);
        if (path.isEmpty() || activity->mergedOverlay == path)
            return;
        add(ActivityFinding::Warning, "overlay.present",
            //% "Open Rails adds the overlay %1 to this activity (Merge... in the activity panel puts it in)."
            qtTrId("activity.check.overlay.present").arg(QFileInfo(path).fileName()));
        const ActivityOverlay::Result merge = ActivityOverlay::Merge(activity, path, false);
        if (!merge.readable)
            add(ActivityFinding::Warning, "overlay.unreadable",
                //% "The overlay %1 cannot be read."
                qtTrId("activity.check.overlay.unreadable").arg(QFileInfo(path).fileName()));
        for (const QString &line : merge.skipped)
            add(ActivityFinding::Warning, "overlay.skipped",
                //% "Overlay: %1"
                qtTrId("activity.check.overlay.skipped").arg(line));
    }

    // Loose consists whose cars' files are missing.
    void consists() {
        for (ActivityObject *object : activity->activityObjects)
            if (object != NULL && object->con != NULL && object->con->isBroken()) {
                ActivityFinding &finding = add(ActivityFinding::Error, "consist.cars.missing",
                    //% "The loose consist %1: some of its cars' files are missing."
                    qtTrId("activity.check.consist.cars.missing").arg(object->con->showName));
                finding.target = ActivityFinding::Position;
                std::copy(object->tile, object->tile + 4, finding.position);
            }
    }
};

}

QVector<ActivityFinding> CheckActivity(Activity *activity) {
    if (activity == NULL || activity->loaded != 1)
        return {};
    return Checker(activity).run();
}
