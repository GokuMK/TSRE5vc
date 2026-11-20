#include <tsre/sim/RunData.h>

#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QTextStream>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/trains/OrTimetable.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Traffic.h>

namespace Sim {

namespace {

// As Save writes an MSTS file: UTF-16 with its byte order mark.
QByteArray utf16File(const QString &text) {
    QByteArray bytes;
    QTextStream out(&bytes, QIODevice::WriteOnly);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << text;
    out.flush();
    return bytes;
}

// As Save writes an Open Rails timetable: UTF-8 with its byte order mark.
QByteArray utf8File(const QString &text) {
    return QByteArray("\xEF\xBB\xBF") + text.toUtf8();
}

constexpr quint32 Mark = 0x54525244; // "TRRD"
constexpr quint16 Version = 1;

}

QString PathUnder(const QString &folder, const QString &file) {
    const QString root = QDir::cleanPath(QDir::current().absoluteFilePath(ContentPath::normalize(folder)));
    const QString full = QDir::cleanPath(QDir::current().absoluteFilePath(ContentPath::normalize(file)));
    if (full.size() <= root.size() + 1 || !full.startsWith(root, Qt::CaseInsensitive) || full[root.size()] != '/')
        return QString();
    return full.mid(root.size() + 1);
}

RunData GatherRunData(Activity *activity) {
    RunData data;
    if (activity == nullptr)
        return data;
    const QString route = ActivityTrains::RouteDirectory(activity);
    const auto put = [&](const QString &file, const QByteArray &bytes) {
        const QString under = PathUnder(route, file);
        if (!under.isEmpty())
            data.files.insert(under, bytes);
    };
    const auto putPath = [&](Path *path) {
        if (path != nullptr && path->loaded == 1)
            put(path->pathid, utf16File(path->toText(9)));
    };
    data.timetables = activity->isTimetables();
    data.activity = PathUnder(route, activity->pathid);
    if (data.timetables) {
        // A group's list: its name and its timetables' files, as
        // OrTimetableGroup reads them.
        if (activity->name.contains(".timetablelist", Qt::CaseInsensitive)) {
            QString list = "#" + (activity->header != nullptr ? activity->header->name : activity->nameid) + "\n";
            for (const std::shared_ptr<OrTimetable> &timetable : activity->timetables)
                list += QFileInfo(timetable->file).fileName() + "\n";
            put(activity->pathid, utf8File(list));
        }
        for (const std::shared_ptr<OrTimetable> &timetable : activity->timetables) {
            put(timetable->file, utf8File(timetable->toText(true)));
            for (const OrTimetable::Train &train : timetable->trains)
                putPath(ActLib::GetPathByName(train.pathName(), route));
        }
        return data;
    }
    put(activity->pathid, utf16File(activity->toText()));
    // Its traffic pattern (ActivityTrains::List reads its runs).
    if (activity->traffic != nullptr)
        if (Traffic *traffic = ActLib::GetTrafficByName(activity->traffic->name, route))
            put(traffic->pathid, utf16File(traffic->toText()));
    for (const ActivityTrains::Train &train : ActivityTrains::List(activity)) {
        if (train.kind == ActivityTrains::PatternOnly)
            continue;
        Service *service = ActLib::GetServiceByName(train.service, route);
        if (service == nullptr)
            continue;
        put(service->pathid, utf16File(service->toText()));
        putPath(ActLib::GetPathByName(service->pathId, route));
    }
    return data;
}

Activity *LoadRunData(const RunData &data, const QString &routeFolder) {
    if (data.empty())
        return nullptr;
    const auto folderOf = [](const QString &name) { return name.section('/', 0, -2); };
    const auto fileOf = [](const QString &name) { return name.section('/', -1); };
    // Paths first, then services and traffic: the activity's trains find
    // them by name.
    for (const char *kind : {"PATHS", "SERVICES", "TRAFFIC"})
        for (auto it = data.files.constBegin(); it != data.files.constEnd(); ++it) {
            if (folderOf(it.key()).compare(kind, Qt::CaseInsensitive) != 0)
                continue;
            const QString folder = routeFolder + "/" + folderOf(it.key());
            if (QLatin1String(kind) == QLatin1String("PATHS"))
                ActLib::AddPath(folder, fileOf(it.key()));
            else if (QLatin1String(kind) == QLatin1String("SERVICES"))
                ActLib::AddService(folder, fileOf(it.key()));
            else
                ActLib::AddTraffic(folder, fileOf(it.key()));
        }
    const QString folder = routeFolder + "/" + folderOf(data.activity);
    if (data.timetables) {
        const int id = ActLib::AddTimetableSet(folder, fileOf(data.activity));
        Activity *set = ActLib::TimetableSets.value(id);
        return set != nullptr && set->loaded == 1 ? set : nullptr;
    }
    const int id = ActLib::AddAct(folder, fileOf(data.activity));
    Activity *activity = ActLib::Act.value(id);
    return activity != nullptr && activity->loaded == 1 ? activity : nullptr;
}

QByteArray RunData::encode() const {
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << Mark << Version << activity << timetables << quint32(files.size());
    for (auto it = files.constBegin(); it != files.constEnd(); ++it)
        out << it.key() << it.value();
    return bytes;
}

bool RunData::Decode(const QByteArray &bytes, RunData &out, QString *error) {
    QDataStream in(bytes);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 mark = 0, count = 0;
    quint16 version = 0;
    out = RunData();
    in >> mark >> version;
    if (mark != Mark || version != Version) {
        if (error != nullptr)
            *error = "not a run's data";
        return false;
    }
    in >> out.activity >> out.timetables >> count;
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        QString name;
        QByteArray file;
        in >> name >> file;
        // Under the route's folder only: no way up, no root.
        if (name.isEmpty() || name.startsWith('/') || name.contains("..") || name.contains('\\') || name.contains(':')) {
            if (error != nullptr)
                *error = "a run's file outside its route: " + name;
            return false;
        }
        out.files.insert(name, file);
    }
    if (in.status() != QDataStream::Ok) {
        if (error != nullptr)
            *error = "a run's data cut short";
        return false;
    }
    return true;
}

}
