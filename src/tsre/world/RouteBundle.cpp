/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/RouteBundle.h>

#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <algorithm>
#include <tsre/tdb/SigCfg.h>

namespace {

// One file at most, and the whole bundle (the server may send 256 MB).
constexpr qint64 MostAFile = 32ll * 1024 * 1024;
constexpr qint64 MostInAll = 192ll * 1024 * 1024;

qint64 total(const RouteBundle::Bundle &bundle) {
    qint64 sum = 0;
    for (const QByteArray &bytes : bundle.files)
        sum += bytes.size();
    return sum;
}

// A file under the game root, by the spelling a local load uses: added when
// it is there.
bool add(RouteBundle::Bundle &bundle, const QString &root, const QString &relative) {
    const QString path = root + "/" + relative;
    const QFileInfo info(path);
    if (!info.isFile())
        return false;
    if (info.size() > MostAFile || total(bundle) + info.size() > MostInAll) {
        bundle.skipped << relative;
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        bundle.skipped << relative;
        return false;
    }
    bundle.files.insert(relative, file.readAll());
    return true;
}

// A folder's files (and its subfolders' when `deep`), but `except`'s.
void addFolder(RouteBundle::Bundle &bundle, const QString &root, const QString &relative,
               const QStringList &filters, bool deep, const QStringList &except = {}) {
    const QString folder = root + "/" + relative;
    if (!QDir(folder).exists())
        return;
    QList<QRegularExpression> expressions;
    for (const QString &filter : filters)
        expressions << QRegularExpression(QRegularExpression::wildcardToRegularExpression(filter),
                                          QRegularExpression::CaseInsensitiveOption);
    QDirIterator it(folder, QDir::Files | QDir::Readable,
                    deep ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    QStringList found;
    while (it.hasNext()) {
        const QString path = it.next();
        const QString inside = QDir(folder).relativeFilePath(path);
        bool left = false;
        for (const QString &folderOut : except)
            left = left || inside.startsWith(folderOut + "/", Qt::CaseInsensitive);
        if (left)
            continue;
        bool matches = expressions.isEmpty();
        for (const QRegularExpression &expression : expressions)
            matches = matches || expression.match(QFileInfo(path).fileName()).hasMatch();
        if (matches)
            found << inside;
    }
    found.sort(Qt::CaseInsensitive);
    for (const QString &inside : found)
        add(bundle, root, relative + "/" + inside);
}

// The names a tsection file includes (MSTS's include ( "name" )), found in
// its text, UTF-16 with its BOM or 8 bit.
QStringList includesOf(const QByteArray &bytes) {
    QString text;
    if (bytes.startsWith("\xff\xfe") || bytes.startsWith("\xfe\xff")) {
        QStringDecoder decode(QStringDecoder::Utf16);
        text = decode(bytes);
    } else {
        text = QString::fromLatin1(bytes);
    }
    static const QRegularExpression include(
            QStringLiteral("\\binclude\\s*\\(\\s*\"?([^\")]+?)\"?\\s*\\)"),
            QRegularExpression::CaseInsensitiveOption);
    QStringList names;
    auto it = include.globalMatch(text);
    while (it.hasNext())
        names << it.next().captured(1).trimmed().replace('\\', '/');
    return names;
}

// A tsection file and what it includes, beside it (as TSectionDAT reads
// them), included files' includes too.
void addTSection(RouteBundle::Bundle &bundle, const QString &root, const QString &folder, const QString &name) {
    QStringList waiting{name};
    QSet<QString> seen;
    while (!waiting.isEmpty()) {
        // As the include names it from beside the file (Open Rails' route
        // tsection includes "../../../Global/tsection.dat"), cleaned: no
        // ".." in a name sent (the review: the client refused the bundle).
        const QString relative = QDir::cleanPath(folder + "/" + waiting.takeFirst());
        if (relative.startsWith("../") || relative == ".." || QDir::isAbsolutePath(relative))
            continue;
        if (seen.contains(relative.toLower()))
            continue;
        seen.insert(relative.toLower());
        if (!add(bundle, root, relative))
            continue;
        waiting << includesOf(bundle.files.value(relative));
    }
}

void heavyFolder(QVector<RouteBundle::HeavyFile> &out, const QString &root, const QString &relative) {
    const QString folder = root + "/" + relative;
    if (!QDir(folder).exists())
        return;
    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        out.push_back({relative + "/" + QDir(folder).relativeFilePath(info.filePath()), info.size()});
    }
}

}

namespace RouteBundle {

Bundle gatherFixed(const QString &root, const QString &route) {
    Bundle bundle;
    const QString folder = "ROUTES/" + route;
    // The track sections: the route's OPENRAILS one when it has one, as
    // TSectionDAT takes it, else GLOBAL's.
    if (QFileInfo(root + "/" + folder + "/OPENRAILS/tsection.dat").isFile())
        addTSection(bundle, root, folder + "/OPENRAILS", "tsection.dat");
    else
        addTSection(bundle, root, "GLOBAL", "tsection.dat");
    for (const char *name : {"speedpost.dat", "forests.dat", "carspawn.dat",
                             "OPENRAILS/carspawn.dat", "ssource.dat", "ttype.dat", "telepole.dat",
                             "humans.dat", "load.ace", "details.ace", "graphic.ace"})
        add(bundle, root, folder + "/" + QString::fromLatin1(name));
    // The signals as the editor and the simulation read them (task server
    // 05: a client's run must make the server's signals): sigcfg.dat and
    // the OPENRAILS one, each with the scripts it lists, beside it, else in
    // the route's folder.
    for (const QString &config : {folder + "/sigcfg.dat", folder + "/OPENRAILS/sigcfg.dat"}) {
        if (!add(bundle, root, config))
            continue;
        const SigCfg read(root + "/" + config);
        const QString beside = QFileInfo(config).path();
        for (const QString &name : read.scriptFiles)
            if (!QFileInfo(root + "/" + beside + "/" + name).isFile() || !add(bundle, root, beside + "/" + name))
                add(bundle, root, folder + "/" + name);
    }
    addFolder(bundle, root, folder + "/TRACKPROFILES", {"*.stf", "*.xml"}, false);
    // The default track profile of every route (OrtsTrackProfileCatalog's).
    addFolder(bundle, root, "GLOBAL/TRACKPROFILES", {"TrProfile.xml", "TrProfile.stf"}, false);
    addFolder(bundle, root, folder + "/PROCEDURAL", {}, false);
    addFolder(bundle, root, folder + "/DISPLAYS", {}, true);
    return bundle;
}

void gatherCurrent(const QString &root, const QString &route, Bundle &bundle) {
    const QString folder = "ROUTES/" + route;
    // The env files and their weather maps (in folders beside them).
    addFolder(bundle, root, folder + "/ENVFILES", {}, true, {"TEXTURES"});
    add(bundle, root, folder + "/terrainmaterials.dat");
    bundle.heavy = heavyFiles(root, route);
}

QByteArray encode(const Bundle &bundle) {
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out.setByteOrder(QDataStream::LittleEndian);
    QStringList names = bundle.files.keys();
    names.sort(Qt::CaseInsensitive);
    out << quint32(names.size());
    for (const QString &name : names)
        out << name << bundle.files.value(name);
    out << quint32(bundle.heavy.size());
    for (const HeavyFile &file : bundle.heavy)
        out << file.name << qint64(file.size);
    return bytes;
}

bool decode(const QByteArray &bytes, Bundle &bundle, QString &error) {
    QDataStream in(bytes);
    in.setVersion(QDataStream::Qt_6_0);
    in.setByteOrder(QDataStream::LittleEndian);
    quint32 count = 0;
    in >> count;
    if (in.status() != QDataStream::Ok || count > 1000000) {
        error = "The route files' list is malformed";
        return false;
    }
    for (quint32 i = 0; i < count; i++) {
        QString name;
        QByteArray file;
        in >> name >> file;
        if (in.status() != QDataStream::Ok) {
            error = "A route file is malformed";
            return false;
        }
        // A name leaving the game root, or naming no file: that file not
        // taken, the others are (the review: one refused them all).
        bool outside = name.isEmpty() || name.startsWith('/') || name.contains('\\') || name.contains(':');
        for (const QString &part : name.split('/'))
            outside = outside || part.isEmpty() || part == "." || part == "..";
        if (outside) {
            bundle.skipped << name;
            continue;
        }
        bundle.files.insert(name, file);
    }
    in >> count;
    if (in.status() != QDataStream::Ok || count > 10000000) {
        error = "The heavy folders' list is malformed";
        return false;
    }
    bundle.heavy.reserve(int(count));
    for (quint32 i = 0; i < count; i++) {
        HeavyFile file;
        in >> file.name >> file.size;
        if (in.status() != QDataStream::Ok) {
            error = "The heavy folders' list is malformed";
            return false;
        }
        bundle.heavy.push_back(file);
    }
    return true;
}

QVector<HeavyFile> heavyFiles(const QString &root, const QString &route) {
    QVector<HeavyFile> out;
    const QString folder = "ROUTES/" + route;
    for (const char *name : {"SHAPES", "TEXTURES", "TERRTEX", "SOUND", "ENVFILES/TEXTURES"})
        heavyFolder(out, root, folder + "/" + QString::fromLatin1(name));
    for (const char *name : {"GLOBAL/SHAPES", "GLOBAL/TEXTURES"})
        heavyFolder(out, root, QString::fromLatin1(name));
    return out;
}

HeavyDifferences compareHeavy(const QVector<HeavyFile> &server, const QVector<HeavyFile> &client) {
    // Case ignored: the content case rule names files alike on both.
    QHash<QString, qint64> here;
    for (const HeavyFile &file : client)
        here.insert(file.name.toLower(), file.size);
    HeavyDifferences out;
    QSet<QString> there;
    for (const HeavyFile &file : server) {
        const QString key = file.name.toLower();
        there.insert(key);
        const auto found = here.constFind(key);
        if (found == here.constEnd())
            out.missing << file.name;
        else if (found.value() != file.size)
            out.otherSize << file.name;
    }
    for (const HeavyFile &file : client)
        if (!there.contains(file.name.toLower()))
            out.extra << file.name;
    out.missing.sort(Qt::CaseInsensitive);
    out.otherSize.sort(Qt::CaseInsensitive);
    out.extra.sort(Qt::CaseInsensitive);
    return out;
}

}
