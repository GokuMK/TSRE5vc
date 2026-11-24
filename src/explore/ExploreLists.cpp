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
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/OrTimetable.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/TrainFields.h>
#include <tsre/world/Trk.h>

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPair>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace Explore {
namespace Lists {

namespace {

// MSTS's text as written in its files: "\\n" a line break, "\\t" a tab.
QString shownText(QString text) {
    text.replace("\\n", "\n").replace("\\t", "\t");
    return text.trimmed();
}

// The file's SIMISA header, read as a token (not a block: skipping it would
// skip the block after it).
bool isHeader(const QString &token) {
    return token.startsWith("simisa@");
}

// A text file read as the editor's loaders read it (UTF-16, an 8-bit one
// converted, the byte order mark skipped); null when it is not there.
std::unique_ptr<FileBuffer> readText(const QString &path) {
    QFile file(ContentPath::normalize(path));
    if (!file.open(QIODevice::ReadOnly))
        return nullptr;
    std::unique_ptr<FileBuffer> data(ReadFile::read(&file));
    if (data == nullptr)
        return nullptr;
    data->toUtf16();
    data->skipBOM();
    return data;
}


// Names sorted as people read them (numbers by value, case ignored).
template <typename T, typename Key>
void sortBy(QVector<T> &items, Key key) {
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(items.begin(), items.end(),
                     [&](const T &a, const T &b) { return collator.compare(key(a), key(b)) < 0; });
}

// A consist file's display name and its first engine (EngineData's name and
// folder), as ConsistFileHasEngine walks it.
struct ConsistRead {
    QString name;
    QString engineName;
    QString engineFolder;
    // Its Engine and Wagon blocks.
    int cars = 0;
};

bool readConsist(const QString &path, ConsistRead &out) {
    std::unique_ptr<FileBuffer> data = readText(path);
    if (data == nullptr)
        return false;
    QString sh;
    while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
        if (isHeader(sh))
            continue;
        if (sh == "train") {
            while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                if (sh == "traincfg") {
                    out.name = ParserX::GetString(data.get());
                    while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                        if (sh == "name") {
                            const QString shown = ParserX::GetStringInside(data.get()).trimmed();
                            if (!shown.isEmpty())
                                out.name = shown;
                        } else if (sh == "wagon") {
                            ++out.cars;
                        } else if (sh == "engine") {
                            ++out.cars;
                            while (out.engineName.isEmpty()
                                   && !(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                                if (sh == "enginedata") {
                                    out.engineName = ParserX::GetString(data.get());
                                    out.engineFolder = ParserX::GetString(data.get());
                                }
                                ParserX::SkipToken(data.get());
                            }
                        }
                        ParserX::SkipToken(data.get());
                    }
                }
                ParserX::SkipToken(data.get());
            }
        }
        ParserX::SkipToken(data.get());
    }
    return true;
}

// The engine's file under TRAINSET ("FOLDER/NAME.eng"; a MaSzyna row's
// type's .mmd), as ConsistFileHasEngine finds it.
QString engineFile(const ConsistRead &consist) {
    if (consist.engineName.isEmpty())
        return QString();
    const QString file = MaszynaVehicles::isRow(consist.engineName)
            ? MaszynaVehicles::typeFile(consist.engineName) : consist.engineName + ".eng";
    return consist.engineFolder + "/" + file;
}

// An .eng file's Engine block's Name; empty for none (MaSzyna's .mmd has
// none: the row's name is shown).
QString engineDisplayName(const QString &path) {
    if (!path.endsWith(".eng", Qt::CaseInsensitive))
        return QString();
    std::unique_ptr<FileBuffer> data = readText(path);
    if (data == nullptr)
        return QString();
    QString sh;
    while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
        if (isHeader(sh))
            continue;
        if (sh == "engine") {
            ParserX::GetString(data.get());
            while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                if (sh == "name")
                    return ParserX::GetStringInside(data.get()).trimmed();
                ParserX::SkipToken(data.get());
            }
            return QString();
        }
        ParserX::SkipToken(data.get());
    }
    return QString();
}

// A path file's name, flags, start and end (TrackPath's), as Path::load
// reads them; false when it cannot be read.
bool readPath(const QString &file, QString &name, unsigned int &flags, QString &start, QString &end) {
    std::unique_ptr<FileBuffer> data = readText(file);
    if (data == nullptr)
        return false;
    QString sh;
    while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
        if (isHeader(sh))
            continue;
        if (sh == "trackpath") {
            while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                if (sh == "name")
                    name = ParserX::GetStringInside(data.get()).trimmed();
                else if (sh == "trpathflags")
                    flags = ParserX::GetHex(data.get());
                else if (sh == "trpathstart")
                    start = ParserX::GetStringInside(data.get()).trimmed();
                else if (sh == "trpathend")
                    end = ParserX::GetStringInside(data.get()).trimmed();
                ParserX::SkipToken(data.get());
            }
        }
        ParserX::SkipToken(data.get());
    }
    return true;
}

double startLatitude(const Trk &trk, bool &known) {
    known = false;
    std::unique_ptr<GeoWorldCoordinateConverter> converter(GeoWorldCoordinateConverter::Create(
            trk.geoProjectionType, trk.geoProjection.has_value() ? &*trk.geoProjection : nullptr));
    if (converter == nullptr)
        return 0.0;
    PreciseTileCoordinate start;
    start.TileX = trk.startTileX;
    start.TileZ = trk.startTileZ;
    start.setWxyz(trk.startpX, 0, -trk.startpZ);
    IghCoordinate internal;
    LatitudeLongitudeCoordinate geographic;
    if (converter->ConvertToInternal(&start, &internal) == nullptr
            || converter->ConvertToLatLon(&internal, &geographic) == nullptr || !std::isfinite(geographic.Latitude))
        return 0.0;
    known = true;
    return geographic.Latitude;
}

}

QVector<RouteInfo> routes(const QString &root) {
    QVector<RouteInfo> list;
    const QString routesFolder = root + "/ROUTES";
    for (const QString &folder : QDir(routesFolder).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = routesFolder + "/" + folder;
        // The TRK named as the folder, else the first one (Game::checkRoute).
        QString trkFile = path + "/" + folder + ".trk";
        if (!QFileInfo(trkFile).isFile()) {
            const QStringList trks = QDir(path).entryList({"*.trk"}, QDir::Files);
            if (trks.isEmpty())
                continue;
            trkFile = path + "/" + trks.first();
        }
        Trk trk;
        trk.load(trkFile);
        RouteInfo route;
        route.folder = folder;
        route.name = trk.displayName.trimmed().isEmpty() ? folder : trk.displayName.trimmed();
        route.id = trk.idName;
        route.description = shownText(trk.description);
        const QString graphic = QString(trk.graphic).replace('\\', '/').trimmed();
        if (!graphic.isEmpty() && !graphic.contains("..")) {
            const QString picture = ContentPath::textureSource(path + "/" + graphic);
            if (QFileInfo(picture).isFile())
                route.picture = picture;
        }
        const QString loading = trk.loadingScreenFile(path);
        if (!loading.isEmpty())
            route.loadingScreen = ContentPath::textureSource(path + "/" + loading);
        route.latitude = startLatitude(trk, route.latitudeKnown);
        route.metric = trk.milepostUnitsKilometers;
        route.electrified = trk.electrified != 0;
        route.lineVoltage = trk.maxLineVoltage;
        route.speedLimit = trk.speedLimit;
        list << route;
    }
    sortBy(list, [](const RouteInfo &r) { return r.name; });
    return list;
}

QVector<ActivityInfo> activities(const QString &root, const RouteInfo &route) {
    QVector<ActivityInfo> list;
    const QString routePath = root + "/ROUTES/" + route.folder;
    const QString folder = routePath + "/ACTIVITIES";
    for (const QString &file : QDir(folder).entryList({"*.act"}, QDir::Files)) {
        std::unique_ptr<FileBuffer> data = readText(folder + "/" + file);
        if (data == nullptr)
            continue;
        ActivityInfo info;
        info.file = file;
        Activity::ActivityHeader header;
        bool hasHeader = false;
        QString sh;
        // Tr_Activity ( Tr_Activity_Header ( ... ) Tr_Activity_File (
        // Player_Service_Definition ( name ... ) ... ) ), as Activity::parse
        // walks it, the rest skipped.
        while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
            if (isHeader(sh))
                continue;
            if (sh == "include") {
                const QString include = ParserX::GetStringInside(data.get());
                ParserX::SkipToken(data.get());
                data->insertFile(folder + "/" + include);
                continue;
            }
            if (sh == "tr_activity") {
                while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                    if (sh == "include") {
                        const QString include = ParserX::GetStringInside(data.get());
                        ParserX::SkipToken(data.get());
                        data->insertFile(folder + "/" + include);
                        continue;
                    }
                    if (sh == "tr_activity_header") {
                        header.load(data.get());
                        hasHeader = true;
                    } else if (sh == "tr_activity_file") {
                        while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                            if (sh == "player_service_definition") {
                                info.service = ParserX::GetStringInside(data.get());
                                // Its timetable (ActivityTimetable::load's
                                // tokens): the stops' times and platforms.
                                while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                                    if (sh == "player_traffic_definition") {
                                        ParserX::GetNumber(data.get());
                                        QVector<int> arrivals, departures, platforms;
                                        while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                                            if (sh == "arrivaltime")
                                                arrivals << int(ParserX::GetNumber(data.get()));
                                            else if (sh == "departtime")
                                                departures << int(ParserX::GetNumber(data.get()));
                                            else if (sh == "platformstartid")
                                                platforms << int(ParserX::GetNumber(data.get()));
                                            ParserX::SkipToken(data.get());
                                        }
                                        for (int i = 0; i < platforms.size(); ++i) {
                                            ActivityInfo::Stop stop;
                                            stop.platform = platforms[i];
                                            stop.arrival = i < arrivals.size() ? arrivals[i] : -1;
                                            stop.departure = i < departures.size() ? departures[i] : -1;
                                            info.playerStops << stop;
                                        }
                                    }
                                    ParserX::SkipToken(data.get());
                                }
                            } else if (sh == "traffic_definition") {
                                // Its services: the other trains.
                                ParserX::GetStringInside(data.get());
                                while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                                    if (sh == "service_definition")
                                        ++info.traffic;
                                    ParserX::SkipToken(data.get());
                                }
                            }
                            ParserX::SkipToken(data.get());
                        }
                    }
                    ParserX::SkipToken(data.get());
                }
            }
            ParserX::SkipToken(data.get());
        }
        if (!hasHeader)
            continue;
        // Another route's (Open Rails reads only its own).
        if (!route.id.isEmpty() && !header.routeid.isEmpty()
                && header.routeid.compare(route.id, Qt::CaseInsensitive) != 0)
            continue;
        info.name = header.name.trimmed().isEmpty() ? QFileInfo(file).completeBaseName() : header.name.trimmed();
        info.description = shownText(header.description);
        info.briefing = shownText(header.briefing);
        info.startTime = header.startTime[0] * 3600 + header.startTime[1] * 60 + header.startTime[2];
        info.season = header.season;
        info.weather = header.weather;
        info.difficulty = header.difficulty;
        info.duration = header.duration[0] * 60 + header.duration[1];
        if (!info.service.isEmpty()) {
            Service service(routePath + "/SERVICES", info.service + ".srv");
            if (service.loaded == 1) {
                info.consist = service.trainConfig;
                info.path = service.pathId;
            }
        }
        list << info;
    }
    sortBy(list, [](const ActivityInfo &a) { return a.name; });
    return list;
}

QVector<TimetableSetInfo> timetableSets(const QString &root, const QString &routeFolder) {
    QVector<TimetableSetInfo> list;
    const QString folder = root + "/ROUTES/" + routeFolder + "/ACTIVITIES/OPENRAILS";
    for (const QString &file :
         QDir(folder).entryList({"*.timetable_or", "*.timetablelist_or"}, QDir::Files)) {
        TimetableSetInfo set;
        set.file = file;
        if (file.contains(".timetablelist", Qt::CaseInsensitive)) {
            OrTimetableGroup group;
            if (group.load(folder + "/" + file))
                set.name = group.name.trimmed();
        } else {
            OrTimetable timetable;
            if (timetable.load(folder + "/" + file))
                set.name = timetable.description.trimmed();
        }
        if (set.name.isEmpty())
            set.name = QFileInfo(file).completeBaseName();
        list << set;
    }
    sortBy(list, [](const TimetableSetInfo &s) { return s.name; });
    return list;
}

QVector<TrainInfo> timetableTrains(const QString &root, const QString &routeFolder, const QString &file,
                                   QStringList *stations) {
    QVector<TrainInfo> list;
    if (stations != nullptr)
        stations->clear();
    const QString folder = root + "/ROUTES/" + routeFolder + "/ACTIVITIES/OPENRAILS";
    QStringList files;
    if (file.contains(".timetablelist", Qt::CaseInsensitive)) {
        OrTimetableGroup group;
        if (group.load(folder + "/" + file))
            files = group.timetables;
    } else {
        files << file;
    }
    const QString consists = root + "/TRAINS/CONSISTS";
    const QString trainsets = root + "/TRAINS/TRAINSET";
    // Each consist read once: a set's trains share them.
    QHash<QString, ConsistRead> read;
    QHash<QString, QString> engineNames;
    const QString paths = root + "/ROUTES/" + routeFolder + "/PATHS";
    QHash<QString, QPair<QString, QString>> pathEnds;
    for (const QString &one : files) {
        OrTimetable timetable;
        if (!timetable.load(folder + "/" + one))
            continue;
        if (stations != nullptr)
            for (const OrTimetable::Station &row : timetable.stations)
                if (!row.name.trimmed().isEmpty() && !stations->contains(row.name.trimmed(), Qt::CaseInsensitive))
                    *stations << row.name.trimmed();
        for (const OrTimetable::Train &train : timetable.trains) {
            if (train.isStatic)
                continue;
            TrainInfo info;
            info.name = train.name;
            info.startTime = train.startTime;
            info.category = train.fields[TrainFields::Category];
            info.number = train.fields[TrainFields::Number];
            info.line = train.fields[TrainFields::Line];
            const auto station = [&timetable](int index) {
                return index >= 0 && index < timetable.stations.size() ? timetable.stations[index].name : QString();
            };
            for (const OrTimetable::Stop &stop : train.stops) {
                TrainInfo::Time time;
                time.station = station(stop.station).trimmed();
                time.arrival = stop.timing.arrival;
                time.departure = stop.timing.departure;
                time.passing = stop.timing.passing;
                if (!time.station.isEmpty() && (time.arrival >= 0 || time.departure >= 0 || time.passing >= 0))
                    info.times << time;
            }
            // In the train's order (the rows go one way, half the trains the
            // other): by time from an hour before its start, past midnight
            // too.
            const auto from = [start = train.startTime](const TrainInfo::Time &t) {
                const int at = t.arrival >= 0 ? t.arrival : t.departure >= 0 ? t.departure : t.passing;
                return start >= 0 ? (at - (start - 3600) + 2 * 86400) % 86400 : at;
            };
            std::stable_sort(info.times.begin(), info.times.end(),
                             [&](const TrainInfo::Time &a, const TrainInfo::Time &b) { return from(a) < from(b); });
            for (const TrainInfo::Time &t : info.times)
                if (t.arrival >= 0 || t.departure >= 0)
                    ++info.stops;
            if (!info.times.isEmpty()) {
                const TrainInfo::Time &last = info.times.last();
                info.endTime = last.arrival >= 0 ? last.arrival : last.departure >= 0 ? last.departure : last.passing;
            }
            info.comment = train.comment.trimmed();
            info.briefing = shownText(train.briefing);
            info.path = train.pathName();
            for (const OrTimetable::ConsistPart &part : train.consists)
                info.consist += (info.consist.isEmpty() ? QString() : QString(" + ")) + part.name.trimmed();
            // Its first and last calls in time (the rows' order is the set's
            // own: Prudnik's has KK-Azoty last, the first call of trains
            // leaving it).
            if (!info.times.isEmpty()) {
                info.from = info.times.first().station;
                info.to = info.times.last().station;
            } else if (!train.stops.isEmpty()) {
                info.from = station(train.stops.first().station);
                info.to = station(train.stops.last().station);
            }
            // A train with no stops (freight): its path's start and end.
            if ((info.from.isEmpty() || info.to.isEmpty()) && !train.pathName().isEmpty()) {
                if (!pathEnds.contains(train.pathName())) {
                    QString name, start, end;
                    unsigned int flags = 0;
                    readPath(paths + "/" + train.pathName() + ".pat", name, flags, start, end);
                    pathEnds.insert(train.pathName(), {start, end});
                }
                const QPair<QString, QString> &ends = pathEnds[train.pathName()];
                if (info.from.isEmpty())
                    info.from = ends.first;
                if (info.to.isEmpty())
                    info.to = ends.second;
            }
            if (!train.fields[TrainFields::Origin].isEmpty())
                info.from = train.fields[TrainFields::Origin];
            if (!train.fields[TrainFields::Destination].isEmpty())
                info.to = train.fields[TrainFields::Destination];
            for (const OrTimetable::ConsistPart &part : train.consists) {
                const QString consistFile = OrTimetable::ConsistFile(part.name);
                if (!read.contains(consistFile)) {
                    ConsistRead consist;
                    readConsist(consists + "/" + consistFile, consist);
                    read.insert(consistFile, consist);
                }
                const ConsistRead &consist = read[consistFile];
                const QString engine = engineFile(consist);
                if (engine.isEmpty() || !ContentPath::readable(trainsets + "/" + engine))
                    continue;
                if (!engineNames.contains(engine)) {
                    const QString name = engineDisplayName(trainsets + "/" + engine);
                    engineNames.insert(engine, name.isEmpty() ? consist.engineName : name);
                }
                info.engine = engineNames[engine];
                info.drivable = true;
                break;
            }
            list << info;
        }
    }
    return list;
}

QVector<PathInfo> playerPaths(const QString &root, const QString &routeFolder) {
    QVector<PathInfo> list;
    const QString folder = root + "/ROUTES/" + routeFolder + "/PATHS";
    for (const QString &file : QDir(folder).entryList({"*.pat"}, QDir::Files)) {
        PathInfo path;
        path.file = file;
        unsigned int flags = 0;
        if (!readPath(folder + "/" + file, path.name, flags, path.start, path.end))
            continue;
        // Open Rails' NotPlayerPath.
        if (flags & 0x20)
            continue;
        if (path.name.isEmpty())
            path.name = QFileInfo(file).completeBaseName();
        list << path;
    }
    sortBy(list, [](const PathInfo &p) { return p.name; });
    sortBy(list, [](const PathInfo &p) { return p.end; });
    sortBy(list, [](const PathInfo &p) { return p.start; });
    return list;
}

QVector<ConsistInfo> drivableConsists(const QString &root) {
    QVector<ConsistInfo> list;
    const QString folder = root + "/TRAINS/CONSISTS";
    const QString trainsets = root + "/TRAINS/TRAINSET";
    QHash<QString, QString> engineNames;
    for (const QString &file : QDir(folder).entryList({"*.con"}, QDir::Files)) {
        ConsistRead consist;
        if (!readConsist(folder + "/" + file, consist))
            continue;
        const QString engine = engineFile(consist);
        if (engine.isEmpty() || !ContentPath::readable(trainsets + "/" + engine))
            continue;
        if (!engineNames.contains(engine)) {
            const QString name = engineDisplayName(trainsets + "/" + engine);
            engineNames.insert(engine, name.isEmpty() ? consist.engineName : name);
        }
        ConsistInfo info;
        info.file = file;
        info.name = consist.name.trimmed().isEmpty() ? QFileInfo(file).completeBaseName() : consist.name.trimmed();
        info.engine = engine;
        info.engineName = engineNames[engine];
        info.cars = consist.cars;
        list << info;
    }
    sortBy(list, [](const ConsistInfo &c) { return c.name; });
    sortBy(list, [](const ConsistInfo &c) { return c.engineName; });
    return list;
}

RouteStations routeStations(const QString &root, const QString &routeFolder) {
    RouteStations out;
    const QString folder = root + "/ROUTES/" + routeFolder;
    // The TRK's track database's name (FileName), its .tit beside it,
    // else its .tdb (Open Rails reads either).
    QString base;
    QString trkFile = folder + "/" + routeFolder + ".trk";
    if (!QFileInfo(trkFile).isFile()) {
        const QStringList trks = QDir(folder).entryList({"*.trk"}, QDir::Files);
        trkFile = trks.isEmpty() ? QString() : folder + "/" + trks.first();
    }
    if (!trkFile.isEmpty()) {
        Trk trk;
        trk.load(trkFile);
        base = trk.routeName.trimmed();
    }
    QStringList candidates;
    if (!base.isEmpty())
        candidates << folder + "/" + base + ".tit" << folder + "/" + base + ".tdb";
    for (const QString &found : QDir(folder).entryList({"*.tit"}, QDir::Files))
        candidates << folder + "/" + found;
    for (const QString &found : QDir(folder).entryList({"*.tdb"}, QDir::Files))
        candidates << folder + "/" + found;
    std::unique_ptr<FileBuffer> data;
    for (const QString &candidate : candidates)
        if (ContentPath::readable(candidate) && (data = readText(candidate)) != nullptr)
            break;
    if (data == nullptr)
        return out;
    QHash<QString, QString> names;
    QString sh;
    // TrItemTable ( n PlatformItem ( TrItemId ( id ) ... Station ( name ) ) ...),
    // at the top of a .tit, inside TrackDB in a .tdb.
    const std::function<void()> items = [&] {
        while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
            if (sh == "platformitem") {
                int id = -1;
                QString station;
                while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                    if (sh == "tritemid")
                        id = int(ParserX::GetNumber(data.get()));
                    else if (sh == "station")
                        station = ParserX::GetStringInside(data.get()).trimmed();
                    ParserX::SkipToken(data.get());
                }
                if (id >= 0 && !station.isEmpty()) {
                    out.ofItem.insert(id, station);
                    names.insert(station.toLower(), station);
                }
            }
            ParserX::SkipToken(data.get());
        }
    };
    while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
        if (isHeader(sh))
            continue;
        if (sh == "tritemtable") {
            ParserX::GetNumber(data.get());
            items();
        } else if (sh == "trackdb") {
            while (!(sh = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
                if (sh == "tritemtable") {
                    ParserX::GetNumber(data.get());
                    items();
                }
                ParserX::SkipToken(data.get());
            }
        }
        ParserX::SkipToken(data.get());
    }
    out.names = names.values();
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(out.names.begin(), out.names.end(),
                     [&](const QString &a, const QString &b) { return collator.compare(a, b) < 0; });
    return out;
}

int seasonOf(const QDate &day, double latitude) {
    if (!day.isValid())
        return 1;
    const int month = day.month();
    const int north = month == 12 || month <= 2 ? 3 : (month - 3) / 3;
    return latitude >= 0.0 ? north : (north + 2) % 4;
}

}
}
