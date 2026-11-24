/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef EXPLORELISTS_H
#define EXPLORELISTS_H

#include <QDate>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Explore {

// What the Explore window lists (task explore 02), read from the game root
// without a loaded route: nothing goes into ActLib or ConLib (a route's
// load fills them, the route switch clears them). Each reads only the
// blocks it shows.
namespace Lists {

struct RouteInfo {
    // Its folder under ROUTES.
    QString folder;
    // The TRK's DisplayName, else the folder.
    QString name;
    // The TRK's RouteID: an activity names its route by it.
    QString id;
    QString description;
    // The route's picture (the TRK's Graphic) and its loading screen
    // (Trk::loadingScreenFile), absolute paths; empty for none.
    QString picture;
    QString loadingScreen;
    // Its start's latitude (the TRK's projection); the north's when not
    // known.
    double latitude = 0.0;
    bool latitudeKnown = false;
    // Its facts (task explore 05): kilometres or miles, electrified and
    // its line's voltage (V, 0 none given), its speed limit (m/s).
    bool metric = true;
    bool electrified = false;
    float lineVoltage = 0.0f;
    float speedLimit = 0.0f;
};
// The game root's routes (a TRK in the folder, Game::checkRoute's rule),
// sorted by name.
QVector<RouteInfo> routes(const QString &root);

struct ActivityInfo {
    // Its file in ACTIVITIES.
    QString file;
    QString name;
    QString description;
    QString briefing;
    // Seconds after midnight, -1 none.
    int startTime = -1;
    // MSTS's: season 0 spring to 3 winter, weather 0 clear, 1 snow, 2 rain,
    // difficulty 0 easy to 2 hard; -1 none.
    int season = -1;
    int weather = -1;
    int difficulty = -1;
    // Minutes, -1 none.
    int duration = -1;
    // The player's service, and its consist and path as it names them.
    QString service;
    QString consist;
    QString path;
    // The player's timetable (Player_Traffic_Definition): each stop's
    // platform item and its times (seconds after midnight, -1 none), in
    // order (task explore 04: walking from one).
    struct Stop {
        int platform = -1;
        int arrival = -1;
        int departure = -1;
    };
    QVector<Stop> playerStops;
    // Its other trains (its Traffic_Definition's services; task explore
    // 05).
    int traffic = 0;
};
// The route's activities (ACTIVITIES/*.act), those of another route (its
// RouteID) left out, sorted by name.
QVector<ActivityInfo> activities(const QString &root, const RouteInfo &route);

struct TimetableSetInfo {
    // Its file in ACTIVITIES/OPENRAILS.
    QString file;
    // The group's name, else its timetable's description, else the file.
    QString name;
};
// The route's Open Rails timetable sets (*.timetable_or and
// *.timetablelist_or), sorted by name.
QVector<TimetableSetInfo> timetableSets(const QString &root, const QString &routeFolder);

struct TrainInfo {
    QString name;
    // Seconds after midnight, -1 none.
    int startTime = -1;
    // Its first and last stations (the #tsre origin and destination where
    // it has them).
    QString from;
    QString to;
    // The #tsre fields.
    QString category;
    QString number;
    QString line;
    // Its first engine (the .eng's name), empty for none.
    QString engine;
    // An engine of its consists is there to drive it.
    bool drivable = false;
    // Its times at the set's stations, in its order (task explore 04): a
    // stop's arrival and departure, a pass's time (seconds after midnight,
    // -1 none).
    struct Time {
        QString station;
        int arrival = -1;
        int departure = -1;
        int passing = -1;
    };
    QVector<Time> times;
    // Task explore 05: its #comment and #briefing cells, its consists (as
    // written, '+' between them) and path; when it ends (its last call's
    // arrival, else its time there; -1 none) and how many stops it makes
    // (calls that are not passes).
    QString comment;
    QString briefing;
    QString consist;
    QString path;
    int endTime = -1;
    int stops = 0;
};
// A timetable set's trains, static ones left out, in the set's order; its
// station rows' names into `stations` (each once, in order).
QVector<TrainInfo> timetableTrains(const QString &root, const QString &routeFolder, const QString &file,
                                   QStringList *stations = nullptr);

// A route's stations (task explore 04): the platform items of its track
// database (the .tit, else the .tdb), each item's station by its id, and
// the stations' names sorted. Reads only those items: for a worker thread
// (it touches no Qt object and no library).
struct RouteStations {
    QStringList names;
    QHash<int, QString> ofItem;
};
RouteStations routeStations(const QString &root, const QString &routeFolder);

struct PathInfo {
    // Its file in PATHS.
    QString file;
    QString name;
    QString start;
    QString end;
};
// The route's player paths (not flagged 0x20, as Open Rails reads them),
// sorted by start, end and name.
QVector<PathInfo> playerPaths(const QString &root, const QString &routeFolder);

struct ConsistInfo {
    // Its file in TRAINS/CONSISTS.
    QString file;
    QString name;
    // Its first engine there: its file under TRAINSET ("FOLDER/NAME.eng"),
    // and its name (the .eng's, else the file's).
    QString engine;
    QString engineName;
    // Its cars, the engines with them (task explore 05).
    int cars = 0;
};
// The game root's consists led by an engine that is there (Open Rails'
// explore list), sorted by engine and name. Reads every consist and their
// engines: slow on large content, for a worker thread (it touches no Qt
// object and no library).
QVector<ConsistInfo> drivableConsists(const QString &root);

// MSTS's season (0 spring to 3 winter, meteorological) of a day at a
// latitude: the south's half a year on.
int seasonOf(const QDate &day, double latitude);

}

}

#endif // EXPLORELISTS_H
