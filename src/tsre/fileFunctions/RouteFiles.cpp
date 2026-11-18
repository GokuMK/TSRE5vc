/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/fileFunctions/RouteFiles.h>

#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ReadFile.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <algorithm>

namespace {

QMutex &lock() {
    static QMutex mutex;
    return mutex;
}

// Keyed lower case: the content case rule names files exactly, but a
// loader's spelling of a fixed name ("OpenRails", "OPENRAILS") may differ.
struct File {
    QString name; // the server's spelling, under the game root
    QByteArray bytes;
};

struct Served {
    bool active = false;
    QString root; // the client's game root
    QHash<QString, File> files;
};

Served &state() {
    static Served value;
    return value;
}

QString keyOf(const QString &rootRelative) {
    return rootRelative.toLower();
}

// The routes' folders and GLOBAL's whose files stay the client's own.
bool heavy(const QStringList &parts) {
    if (parts.size() >= 2 && parts[0].compare("GLOBAL", Qt::CaseInsensitive) == 0) {
        const QString folder = parts[1].toUpper();
        return parts.size() > 2 && (folder == "SHAPES" || folder == "TEXTURES");
    }
    if (parts.size() > 3 && parts[0].compare("ROUTES", Qt::CaseInsensitive) == 0) {
        const QString folder = parts[2].toUpper();
        if (folder == "SHAPES" || folder == "TEXTURES" || folder == "TERRTEX" || folder == "SOUND")
            return true;
        if (folder == "ENVFILES" && parts.size() > 4 && parts[3].compare("TEXTURES", Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

// Any route's folder: between two routes (a switch) a client reads none of
// its own files either.
bool answeredFromServer(const QString &rootRelative) {
    if (rootRelative.isEmpty())
        return false;
    const QStringList parts = rootRelative.split('/');
    if (heavy(parts))
        return false;
    if (parts.size() >= 2 && parts[0].compare("GLOBAL", Qt::CaseInsensitive) == 0)
        return true;
    return parts.size() >= 3 && parts[0].compare("ROUTES", Qt::CaseInsensitive) == 0;
}

QString relativeTo(const QString &rootPath, const QString &path) {
    if (rootPath.isEmpty() || path.isEmpty())
        return QString();
    const QString root = QDir::cleanPath(QDir::current().absoluteFilePath(rootPath));
    const QString full = QDir::cleanPath(QDir::current().absoluteFilePath(QString(path).replace('\\', '/')));
    if (full.size() <= root.size() + 1 || !full.startsWith(root, Qt::CaseInsensitive) || full[root.size()] != '/')
        return QString();
    return full.mid(root.size() + 1);
}

}

namespace RouteFiles {
namespace Internal {

Source lookup(const QString &path, QByteArray *bytes) {
    QMutexLocker locker(&lock());
    if (!state().active)
        return Source::Disk;
    const QString relative = relativeTo(state().root, path);
    if (!answeredFromServer(relative))
        return Source::Disk;
    const auto found = state().files.constFind(keyOf(relative));
    if (found == state().files.constEnd())
        return Source::Missing;
    if (bytes != nullptr)
        *bytes = found.value().bytes;
    return Source::Held;
}

}
}

namespace {
using RouteFiles::Internal::Source;
using RouteFiles::Internal::lookup;

QStringList filtersAsExpressions(const QStringList &filters, QList<QRegularExpression> &out) {
    for (const QString &filter : filters)
        out.push_back(QRegularExpression(QRegularExpression::wildcardToRegularExpression(filter),
                                         QRegularExpression::CaseInsensitiveOption));
    return filters;
}

}

namespace RouteFiles {

void setServed(const QString &root, const QHash<QString, QByteArray> &files) {
    QMutexLocker locker(&lock());
    state().active = true;
    state().root = root;
    state().files.clear();
    for (auto it = files.constBegin(); it != files.constEnd(); ++it)
        state().files.insert(keyOf(it.key()), File{it.key(), it.value()});
}

void putServed(const QString &rootRelative, const QByteArray &bytes) {
    QMutexLocker locker(&lock());
    state().active = true;
    state().files.insert(keyOf(rootRelative), File{rootRelative, bytes});
}

void dropServed(const QString &rootRelative) {
    QMutexLocker locker(&lock());
    state().files.remove(keyOf(rootRelative));
}

void dropAllServed() {
    QMutexLocker locker(&lock());
    state().files.clear();
}

void clearServed() {
    QMutexLocker locker(&lock());
    state().active = false;
    state().root.clear();
    state().files.clear();
}

bool serving() {
    QMutexLocker locker(&lock());
    return state().active;
}

QStringList servedNames() {
    QMutexLocker locker(&lock());
    QStringList names;
    for (const File &file : std::as_const(state().files))
        names.push_back(file.name);
    names.sort(Qt::CaseInsensitive);
    return names;
}

QString rootRelative(const QString &path) {
    QMutexLocker locker(&lock());
    return relativeTo(state().root, path);
}

bool served(const QString &path) {
    QMutexLocker locker(&lock());
    return state().active && answeredFromServer(relativeTo(state().root, path));
}

bool exists(const QString &path) {
    switch (lookup(path, nullptr)) {
    case Source::Held: return true;
    case Source::Missing: return false;
    default: return QFileInfo(path).isFile();
    }
}

qint64 size(const QString &path) {
    QByteArray bytes;
    switch (lookup(path, &bytes)) {
    case Source::Held: return bytes.size();
    case Source::Missing: return -1;
    default: {
        const QFileInfo info(path);
        return info.isFile() ? info.size() : -1;
    }
    }
}

bool read(const QString &path, QByteArray &bytes, QString *error) {
    switch (lookup(path, &bytes)) {
    case Source::Held: return true;
    case Source::Missing:
        if (error) *error = "The server sent no " + path;
        return false;
    default: break;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    bytes = file.readAll();
    if (file.error() != QFile::NoError) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

FileBuffer *buffer(const QString &path) {
    QByteArray bytes;
    if (!read(path, bytes))
        return nullptr;
    return ReadFile::read(bytes);
}

FileBuffer *rawBuffer(const QString &path) {
    QByteArray bytes;
    if (!read(path, bytes))
        return nullptr;
    return ReadFile::readRAW(bytes);
}

QStringList entries(const QString &folder, const QStringList &filters) {
    QList<QRegularExpression> expressions;
    filtersAsExpressions(filters, expressions);
    const auto matches = [&](const QString &name) {
        bool any = expressions.isEmpty();
        for (const QRegularExpression &expression : expressions)
            any = any || expression.match(name).hasMatch();
        return any;
    };
    bool clientFolder = false;
    QStringList names;
    {
        QMutexLocker locker(&lock());
        const QString relative = relativeTo(state().root, folder);
        if (state().active && answeredFromServer(relative + "/x")) {
            clientFolder = true;
            const QString prefix = keyOf(relative) + "/";
            for (auto it = state().files.constBegin(); it != state().files.constEnd(); ++it) {
                if (!it.key().startsWith(prefix))
                    continue;
                if (it.key().indexOf('/', prefix.size()) >= 0)
                    continue;
                const QString name = it.value().name.mid(prefix.size());
                if (matches(name))
                    names.push_back(name);
            }
        }
    }
    if (!clientFolder) {
        // Case ignored in the filters, as on a client.
        for (const QString &name : QDir(folder).entryList(QDir::Files | QDir::Readable))
            if (matches(name))
                names.push_back(name);
    }
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return names;
}

bool folderExists(const QString &folder) {
    {
        QMutexLocker locker(&lock());
        const QString relative = relativeTo(state().root, folder);
        if (state().active && answeredFromServer(relative + "/x")) {
            const QString prefix = keyOf(relative) + "/";
            for (auto it = state().files.constBegin(); it != state().files.constEnd(); ++it)
                if (it.key().startsWith(prefix))
                    return true;
            return false;
        }
    }
    return QDir(folder).exists();
}

}
