#include <tsre/trains/ActivityTrains.h>

#include <QDir>
#include <QTime>
#include <algorithm>
#include <tsre/Game.h>
#include <tsre/trains/TextSnapshot.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Traffic.h>

namespace {

using ActivityTrains::Train;

QString clock(int seconds) {
    return QTime::fromMSecsSinceStartOfDay((seconds % 86400 + 86400) % 86400 * 1000).toString("HH:mm:ss");
}

QString station(int platformId) {
    if (Game::trackDB != NULL && Game::trackDB->loaded) {
        const auto found = Game::trackDB->trackItems.find(platformId);
        if (found != Game::trackDB->trackItems.end() && found->second != NULL
                && !found->second->stationName.isEmpty())
            return found->second->stationName;
    }
    //% "platform %1"
    return qtTrId("activity.trains.platform").arg(platformId);
}

bool fileInFolder(const QString &folder, const QString &name) {
    for (const QString &entry : QDir(folder).entryList(QDir::Files))
        if (entry.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

// Service, path, consist and stops of the train, and what is missing.
void describe(Train &train, const QString &routePath) {
    Service *service = ActLib::GetServiceByName(train.service, routePath);
    if (service == NULL) {
        //% "The service %1 does not exist."
        train.problems << qtTrId("activity.trains.service.missing").arg(train.service);
    } else {
        train.path = service->pathId;
        train.consist = service->trainConfig;
        if (ActLib::GetPathByName(service->pathId, routePath) == NULL)
            //% "The path %1 does not exist."
            train.problems << qtTrId("activity.trains.path.missing").arg(service->pathId);
        if (service->trainConfig.isEmpty()
                || !fileInFolder(Game::root + "/TRAINS/CONSISTS", service->trainConfig + ".con"))
            //% "The consist %1 does not exist."
            train.problems << qtTrId("activity.trains.consist.missing").arg(service->trainConfig);
    }
    const ActivityTimetable *t = train.timetable;
    if (t == NULL)
        return;
    const int stops = std::min({t->platformStartID.size(), t->arrivalTime.size(), t->departTime.size()});
    if (stops == 0)
        return;
    // The first stop's departure; the last stop's arrival when there are
    // two or more.
    train.firstStop = QString("%1 %2").arg(station(t->platformStartID[0]), clock(t->departTime[0]));
    if (stops > 1)
        train.lastStop = QString("%1 %2").arg(station(t->platformStartID[stops - 1]), clock(t->arrivalTime[stops - 1]));
}

}

Traffic *ActivityTrains::Pattern(Activity *activity) {
    if (activity == NULL || activity->traffic == NULL)
        return NULL;
    return ActLib::GetTrafficByName(activity->traffic->name, ContentPath::parentDirectory(activity->path));
}

QStringList ActivityTrains::OtherUsers(Activity *activity) {
    QStringList users = ActLib::TrafficUsers(Pattern(activity));
    if (activity != NULL)
        users.removeAll(activity->name);
    return users;
}

QVector<Train> ActivityTrains::List(Activity *activity) {
    QVector<Train> trains;
    if (activity == NULL)
        return trains;
    const QString routePath = ContentPath::parentDirectory(activity->path);
    ActivityServiceDefinition *player = activity->playerServiceDefinition;
    if (player != NULL && !player->empty) {
        Train train;
        train.kind = Player;
        train.service = player->name;
        train.timetable = player->trafficDefinition;
        train.time = train.timetable != NULL ? train.timetable->time : 0;
        describe(train, routePath);
        trains.push_back(train);
    }
    Traffic *pattern = Pattern(activity);
    QVector<bool> inActivity(pattern != NULL ? pattern->service.size() : 0, false);
    if (activity->traffic != NULL) {
        for (int i = 0; i < activity->traffic->service.size(); ++i) {
            ActivityServiceDefinition &definition = activity->traffic->service[i];
            Train train;
            train.kind = AI;
            train.service = definition.name;
            train.time = definition.time;
            train.index = i;
            ActivityTimetable *run = pattern != NULL ? pattern->getInstance(definition.name, definition.time) : NULL;
            if (run != NULL) {
                inActivity[pattern->service.indexOf(run)] = true;
                definition.trafficDefinition = run;
                train.timetable = run;
                if (run->name != definition.name)
                    //% "The traffic pattern names it %1: Open Rails matches names exactly and finds no times."
                    train.problems << qtTrId("activity.trains.name.case").arg(run->name);
            } else {
                //% "Not in the traffic pattern: it has no times."
                train.problems << qtTrId("activity.trains.no.times");
            }
            describe(train, routePath);
            trains.push_back(train);
        }
    }
    for (int i = 0; i < inActivity.size(); ++i) {
        const ActivityTimetable *run = pattern->service[i];
        if (inActivity[i] || run == NULL || run->empty)
            continue;
        Train train;
        train.kind = PatternOnly;
        train.service = run->name;
        train.time = run->time;
        train.index = i;
        train.timetable = pattern->service[i];
        describe(train, routePath);
        trains.push_back(train);
    }
    std::stable_sort(trains.begin(), trains.end(), [](const Train &a, const Train &b) {
        if (a.time != b.time)
            return a.time < b.time;
        return a.kind == Player && b.kind != Player;
    });
    return trains;
}

ActivityServiceDefinition *ActivityTrains::Definition(Activity *activity, const Train &train) {
    if (activity == NULL)
        return NULL;
    if (train.kind == Player)
        return activity->playerServiceDefinition;
    if (train.kind != AI || activity->traffic == NULL || train.index < 0
            || train.index >= activity->traffic->service.size())
        return NULL;
    ActivityServiceDefinition *definition = &activity->traffic->service[train.index];
    if (definition->trafficDefinition == NULL) {
        Traffic *pattern = Pattern(activity);
        if (pattern != NULL)
            definition->trafficDefinition = pattern->getInstance(definition->name, definition->time);
    }
    return definition;
}

Traffic *ActivityTrains::CopyPattern(Activity *activity, const QString &name) {
    Traffic *pattern = Pattern(activity);
    if (pattern == NULL || name.isEmpty() || ActLib::TrafficExists(name, ContentPath::parentDirectory(activity->path)))
        return NULL;
    Traffic *copy = ActLib::Traffics.value(ActLib::AddTraffic(pattern->path, name + ".trf", true));
    if (copy == NULL)
        return NULL;
    TextSnapshot text;
    text.text = pattern->toText();
    text.unsaved = true;
    copy->restore(text);
    copy->displayName = name;
    activity->setTrafficName(copy->nameId);
    return copy;
}

bool ActivityTrains::StartPosition(Activity *activity, const Train &train, float *txz) {
    if (activity == NULL)
        return false;
    Service *service = ActLib::GetServiceByName(train.service, ContentPath::parentDirectory(activity->path));
    Path *path = service != NULL ? ActLib::GetPathByName(service->pathId, ContentPath::parentDirectory(activity->path)) : NULL;
    if (path == NULL || path->nodes().isEmpty())
        return false;
    path->getStartPositionTXZ(txz);
    return true;
}
