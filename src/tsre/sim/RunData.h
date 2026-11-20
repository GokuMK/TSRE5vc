#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>

class Activity;

namespace Sim {

// What a run is made of besides the route, TRAINS and TSRE's own files
// (task server 05): an activity or an Open Rails timetable set, written as
// the editor's Save writes them (their toText), by path under the route's
// folder ("ACTIVITIES/x.act", "SERVICES/y.srv", "TRAFFIC/z.trf",
// "PATHS/p.pat", "ACTIVITIES/OPENRAILS/t.timetable_or"). A view in another
// process reads them with the editor's own loaders and makes the same input
// (checked by its digest).
struct RunData {
    // The activity's or the set's file.
    QString activity;
    bool timetables = false;
    QHash<QString, QByteArray> files;

    bool empty() const { return activity.isEmpty(); }
    // For a message, and back; false (why in `error`) for bytes that are no
    // run data.
    QByteArray encode() const;
    static bool Decode(const QByteArray &bytes, RunData &out, QString *error = nullptr);
};

// An activity's run data from the editor's objects now: the activity, the
// services of the trains it lists and their paths, and its traffic; or a
// timetable set's timetables (a group's list too) and its trains' paths.
// Empty when the activity is none.
RunData GatherRunData(Activity *activity);

// A run's data loaded with the editor's own loaders, its files read from
// the route files under `routeFolder` (a client: the server's, put in
// RouteFiles): its paths, services and traffic, then the activity or the
// set, into ActLib (which keeps what it has of the same file). The activity
// or set, null when it does not load.
Activity *LoadRunData(const RunData &data, const QString &routeFolder);

// The path of `file` under the folder `folder` (forward slashes), "" when it
// is not under it.
QString PathUnder(const QString &folder, const QString &file);

}
