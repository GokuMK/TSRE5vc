#include "ContentCaseExecution.h"
#include "ContentCaseDocument.h"
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QLockFile>
#include <QStorageInfo>
#include <QUuid>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <utility>
#ifdef Q_OS_WIN
#include <io.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <cerrno>
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#elif defined(Q_OS_MACOS)
#include <stdio.h>
#endif
#endif
namespace ContentCase {
namespace {
QString clean(QString s) { return QDir::cleanPath(s.replace('\\', '/')); }
QString parent(const QString &s) { return s.section('/', 0, -2); }
QString leaf(const QString &s) { return s.section('/', -1); }
bool under(const QString &s, const QString &base) { return s == base || s.startsWith(base + '/'); }
bool relative(const QString &s) {
    return !s.isEmpty() && s != "." && s != ".." && !s.startsWith("../") &&
           !QDir::isAbsolutePath(s) && !s.contains(':') && clean(s) == s;
}
QString registry(const QString &root) {
    return QFileInfo(root).absolutePath() + "/.tsre-case-" +
           QString::fromLatin1(QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256)
                                   .toHex()
                                   .left(24));
}
QString hash(const QByteArray &b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
QString fileHash(const QString &path, QString &error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = "Cannot read " + path + ": " + f.errorString();
        return {};
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) {
        error = "Cannot hash " + path;
        return {};
    }
    return QString::fromLatin1(h.result().toHex());
}
bool exact(const QString &root, const QString &name) {
    if (!relative(name))
        return false;
    QString path = root;
    for (const auto &part : name.split('/')) {
        if (!QDir(path)
                 .entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)
                 .contains(part))
            return false;
        path += '/' + part;
    }
    return true;
}
bool safe(const QString &root, const QString &name, QString &error) {
    if (!relative(name)) {
        error = "Unsafe transaction path: " + name;
        return false;
    }
    QString path = root;
    for (const auto &part : name.split('/')) {
        path += '/' + part;
        QFileInfo f(path);
        if (f.isSymLink() || f.isJunction()) {
            error = "Transaction path crosses a link: " + path;
            return false;
        }
    }
    return true;
}
bool writeNew(const QString &path, const QByteArray &bytes, QString &error) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly) || f.write(bytes) != bytes.size() ||
        !f.flush()) {
        error = "Cannot stage new file: " + path + ": " + f.errorString();
        return false;
    }
#ifdef Q_OS_WIN
    if (_commit(f.handle()) != 0) {
        error = "Cannot sync staged file: " + path;
        return false;
    }
#else
    if (fsync(f.handle()) != 0) {
        error = "Cannot sync staged file: " + path;
        return false;
    }
#endif
    return true;
}
bool writeJson(const QString &path, const QJsonObject &object, QString &error) {
    QSaveFile f(path);
    f.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        error = "Cannot update transaction journal: " + path + ": " + f.errorString();
        return false;
    }
    return true;
}
bool append(const QString &directory, const QJsonObject &event, QString &error) {
    QFile f(directory + "/events.jsonl");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        error = "Cannot open recovery log: " + f.errorString();
        return false;
    }
    const auto b = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    if (f.write(b) != b.size() || !f.flush()) {
        error = "Cannot persist recovery event";
        return false;
    }
#ifdef Q_OS_WIN
    if (_commit(f.handle()) != 0) {
        error = "Cannot sync recovery event";
        return false;
    }
#else
    if (fsync(f.handle()) != 0) {
        error = "Cannot sync recovery event";
        return false;
    }
#endif
    return true;
}
QString treeHash(const QString &path, QString &error) {
    QCryptographicHash digest(QCryptographicHash::Sha256);
    std::function<bool(QString, QString)> walk = [&](QString base, QString prefix) {
        const auto entries = QDir(base).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
        for (const auto &f : entries) {
            if (f.isSymLink() || f.isJunction()) {
                error = "Linked entry in moved directory: " + f.filePath();
                return false;
            }
            const QString name = prefix + f.fileName();
            digest.addData((name + (f.isDir() ? "/\n" : "\n")).toUtf8());
            if (f.isDir()) {
                if (!walk(f.filePath(), name + '/'))
                    return false;
            } else {
                const auto sha = fileHash(f.filePath(), error);
                if (sha.isEmpty())
                    return false;
                digest.addData(sha.toLatin1());
            }
        }
        return true;
    };
    return walk(path, {}) ? QString::fromLatin1(digest.result().toHex()) : QString();
}
QString bootstrapDigest(const QString &path, const QString &type, QString &error) {
    if (type == "file")
        return fileHash(path, error);
    if (type == "link") {
        const QFileInfo info(path);
        if (!info.isSymLink() && !info.isJunction()) {
            error = "Expected a link at " + path;
            return {};
        }
        return hash(QByteArray("link\n") + QFile::encodeName(info.symLinkTarget()));
    }
    if (type != "directory") {
        error = "Unknown bootstrap entry type: " + type;
        return {};
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    auto entries = QDir(path).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    std::sort(entries.begin(), entries.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.fileName() < b.fileName();
    });
    for (const auto &entry : entries) {
        const bool link = entry.isSymLink() || entry.isJunction();
        const QString childType = link ? "link" : entry.isDir() ? "directory" : "file";
        digest.addData(entry.fileName().toUtf8());
        digest.addData(QByteArrayView("\0", 1));
        digest.addData(childType.toLatin1());
        digest.addData(QByteArrayView("\0", 1));
        const auto child = bootstrapDigest(entry.filePath(), childType, error);
        if (child.isEmpty())
            return {};
        digest.addData(child.toLatin1());
    }
    return QString::fromLatin1(digest.result().toHex());
}
QString bootstrapLayoutDigest(const QString &path, const QString &type, QString &error) {
    if (type == "file")
        return hash("file");
    if (type == "link") {
        const QFileInfo info(path);
        if (!info.isSymLink() && !info.isJunction()) {
            error = "Expected a link at " + path;
            return {};
        }
        return hash(QByteArray("link\n") + QFile::encodeName(info.symLinkTarget()));
    }
    if (type != "directory") {
        error = "Unknown bootstrap entry type: " + type;
        return {};
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    auto entries = QDir(path).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    std::sort(entries.begin(), entries.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.fileName() < b.fileName();
    });
    for (const auto &entry : entries) {
        const bool link = entry.isSymLink() || entry.isJunction();
        const QString childType = link ? "link" : entry.isDir() ? "directory" : "file";
        digest.addData(entry.fileName().toUtf8());
        digest.addData(QByteArrayView("\0", 1));
        digest.addData(childType.toLatin1());
        digest.addData(QByteArrayView("\0", 1));
        const auto child = bootstrapLayoutDigest(entry.filePath(), childType, error);
        if (child.isEmpty())
            return {};
        digest.addData(child.toLatin1());
    }
    return QString::fromLatin1(digest.result().toHex());
}
bool safeMovePath(const QString &base, const QString &name, QString &error) {
    if (!relative(name)) {
        error = "Unsafe transaction path: " + name;
        return false;
    }
    QString path = base;
    const auto parts = name.split('/');
    for (int i = 0; i + 1 < parts.size(); ++i) {
        path += '/' + parts[i];
        const QFileInfo f(path);
        if (f.isSymLink() || f.isJunction()) {
            error = "Transaction path crosses a link: " + path;
            return false;
        }
    }
    return true;
}
bool moveBetween(const QString &fromBase, const QString &from, const QString &toBase,
                 const QString &to, QString &error) {
    if (!safeMovePath(fromBase, from, error) || !safeMovePath(toBase, to, error))
        return false;
    if (!exact(fromBase, from) || exact(toBase, to) ||
        QFileInfo(toBase + '/' + to).exists() || QFileInfo(toBase + '/' + to).isSymLink()) {
        error = "Rename source/destination changed: " + from + " -> " + to;
        return false;
    }
    std::error_code ec;
#ifdef Q_OS_WIN
    const auto source = (fromBase + '/' + from).toStdWString(),
               destination = (toBase + '/' + to).toStdWString();
    if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
        ec = std::error_code(GetLastError(), std::system_category());
#elif defined(Q_OS_LINUX)
    const auto source = QFile::encodeName(fromBase + '/' + from),
               destination = QFile::encodeName(toBase + '/' + to);
    if (syscall(SYS_renameat2, AT_FDCWD, source.constData(), AT_FDCWD, destination.constData(),
                RENAME_NOREPLACE) != 0)
        ec = std::error_code(errno, std::generic_category());
#elif defined(Q_OS_MACOS)
    if (renamex_np(QFile::encodeName(fromBase + '/' + from).constData(),
                   QFile::encodeName(toBase + '/' + to).constData(), RENAME_EXCL) != 0)
        ec = std::error_code(errno, std::generic_category());
#else
    ec = std::make_error_code(std::errc::operation_not_supported);
#endif
    if (ec) {
        error = "Rename failed: " + from + " -> " + to + ": " +
                QString::fromStdString(ec.message());
        return false;
    }
    return true;
}
bool move(const QString &root, const QString &from, const QString &to, QString &error) {
    if (!safe(root, from, error) || !safe(root, to, error))
        return false;
    return moveBetween(root, from, root, to, error);
}
bool setFileMetadata(const QString &path, QFile::Permissions permissions, qint64 modifiedMs,
                     QString &error) {
    if (!QFile::setPermissions(path, permissions)) {
        error = "Cannot restore file permissions: " + path;
        return false;
    }
#ifdef Q_OS_WIN
    const auto nativePath = path.toStdWString();
    HANDLE metadata = CreateFileW(nativePath.c_str(), FILE_WRITE_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (metadata == INVALID_HANDLE_VALUE) {
        error = "Cannot open file metadata: " + path;
        return false;
    }
    ULARGE_INTEGER ticks;
    ticks.QuadPart = quint64(modifiedMs) * 10000ULL + 116444736000000000ULL;
    FILETIME modified{ticks.LowPart, ticks.HighPart};
    const bool restored = SetFileTime(metadata, nullptr, nullptr, &modified);
    CloseHandle(metadata);
    if (!restored) {
        error = "Cannot restore modification time: " + path;
        return false;
    }
#else
    QFile metadata(path);
    if (!metadata.open(QIODevice::ReadOnly) ||
        !metadata.setFileTime(QDateTime::fromMSecsSinceEpoch(modifiedMs),
                              QFileDevice::FileModificationTime)) {
        error = "Cannot restore modification time: " + path;
        return false;
    }
#endif
    return true;
}
bool replace(const QString &path, const QByteArray &bytes, const QJsonObject &action,
             QString &error) {
    QSaveFile f(path);
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
        error = "Atomic replacement staging failed: " + path + ": " + f.errorString();
        return false;
    }
    const QString expected = hash(bytes) == action["afterSha256"].toString()
                                 ? action["beforeSha256"].toString()
                                 : action["afterSha256"].toString();
    if (fileHash(path, error) != expected) {
        f.cancelWriting();
        error = "Source changed while replacement was staged: " + path;
        return false;
    }
    if (!f.commit()) {
        error = "Atomic replacement failed: " + path + ": " + f.errorString();
        return false;
    }
    return setFileMetadata(path, QFile::Permissions(action["permissions"].toInt()),
                           action["modifiedMs"].toVariant().toLongLong(), error);
}
QJsonObject readJson(const QString &path, QString &error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = "Cannot read " + path;
        return {};
    }
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(f.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        error = "Invalid JSON: " + path;
        return {};
    }
    return doc.object();
}
struct BootstrapNode {
    QString name;
    QString diskPath;
    QString livePath;
    QString liveArea = "root";
    bool directory = false;
    bool link = false;
    qint64 size = 0;
    qint64 modified = 0;
    QList<std::shared_ptr<BootstrapNode>> children;
};
struct BootstrapCollision {
    std::shared_ptr<BootstrapNode> kept;
    std::shared_ptr<BootstrapNode> alternate;
    QString reason;
};
struct BootstrapPlan {
    QJsonArray actions;
    QJsonArray conflicts;
    QJsonArray originalLayout;
    QJsonArray expectedLayout;
    int directoriesRenamed = 0;
    int directoryTreesMerged = 0;
    int uniqueEntriesMoved = 0;
    int identicalEntriesVaulted = 0;
};
class BootstrapPlanner {
public:
    QString root;
    QString error;
    BootstrapPlan plan;

    explicit BootstrapPlanner(QString path) : root(std::move(path)) {}

    std::shared_ptr<BootstrapNode> inventory() {
        auto result = std::make_shared<BootstrapNode>();
        result->directory = true;
        if (!readChildren(result, {}))
            return {};
        return result;
    }

    bool build() {
        tree = inventory();
        if (!tree)
            return false;
        plan.originalLayout = layout(tree, false);
        for (const auto &name : {QString("ROUTES"), QString("GLOBAL"), QString("TRAINS"),
                                 QString("SOUND"), QString("TEMPLATE")})
            canonicalize(tree, name);
        const auto global = childDirectory(tree, "GLOBAL");
        if (global)
            for (const auto &name : {QString("SHAPES"), QString("TEXTURES")})
                canonicalize(global, name);
        const auto trains = childDirectory(tree, "TRAINS");
        if (trains) {
            for (const auto &name : {QString("TRAINSET"), QString("CONSISTS")})
                canonicalize(trains, name);
            const auto consists = childDirectory(trains, "CONSISTS");
            if (consists)
                canonicalize(consists, "RANDOMSETS");
        }
        const auto routes = childDirectory(tree, "ROUTES");
        if (routes) {
            const QStringList routeDirectories{
                "ACTIVITIES",    "ADDONS",   "ENVFILES",     "LO_TILES", "OPENRAILS",
                "PATHS",         "PROCEDURAL", "SERVICES",   "SHAPES",   "SOUND",
                "TD",            "TERRAIN_MAPS", "TERRTEX",  "TEXTURES", "TILES",
                "TRACKPROFILES", "TRAFFIC", "WORLD"};
            const auto routeChildren = routes->children;
            for (const auto &route : routeChildren) {
                if (!route->directory || route->link)
                    continue;
                for (const auto &name : routeDirectories)
                    canonicalize(route, name);
                const auto activities = childDirectory(route, "ACTIVITIES");
                if (activities)
                    canonicalize(activities, "OPENRAILS");
                const auto envfiles = childDirectory(route, "ENVFILES");
                if (envfiles)
                    canonicalize(envfiles, "TEXTURES");
            }
        }
        for (const auto &collision : conflicts)
            plan.conflicts.append(QJsonObject{{"kept", collision.kept->livePath},
                                              {"alternate", collision.alternate->diskPath},
                                              {"vault", collision.alternate->livePath},
                                              {"reason", collision.reason}});
        plan.expectedLayout = layout(tree, true);
        return error.isEmpty();
    }

private:
    std::shared_ptr<BootstrapNode> tree;
    int temporaryId = 0;
    int vaultId = 0;
    QHash<QString, QString> hashes;
    QHash<QString, QString> layoutHashes;
    QVector<BootstrapCollision> conflicts;

    bool readChildren(const std::shared_ptr<BootstrapNode> &node, const QString &relativePath) {
        QDir directory(root + (relativePath.isEmpty() ? QString() : '/' + relativePath));
        if (!directory.isReadable()) {
            error = "Unreadable core directory during bootstrap inventory: " + relativePath;
            return false;
        }
        const auto entries = directory.entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
            QDir::Name);
        for (const auto &entry : entries) {
            auto child = std::make_shared<BootstrapNode>();
            child->name = entry.fileName();
            child->diskPath = relativePath.isEmpty() ? child->name
                                                     : relativePath + '/' + child->name;
            child->livePath = child->diskPath;
            child->link = entry.isSymLink() || entry.isJunction();
            child->directory = entry.isDir();
            child->size = child->directory || child->link ? 0 : entry.size();
            child->modified = child->directory || child->link
                                  ? 0
                                  : entry.lastModified().toMSecsSinceEpoch();
            node->children.append(child);
            if (child->directory && !child->link && !readChildren(child, child->diskPath))
                return false;
        }
        return true;
    }

    static QString entryType(const std::shared_ptr<BootstrapNode> &node) {
        return node->link ? "link" : node->directory ? "directory" : "file";
    }

    QJsonArray layout(const std::shared_ptr<BootstrapNode> &rootNode, bool live) const {
        QVector<QJsonObject> entries;
        std::function<void(const std::shared_ptr<BootstrapNode> &)> visit =
            [&](const std::shared_ptr<BootstrapNode> &node) {
                for (const auto &child : node->children) {
                    if (live && child->liveArea != "root")
                        continue;
                    const QString path = live ? child->livePath : child->diskPath;
                    entries.append(QJsonObject{{"path", path},
                                               {"directory", child->directory},
                                               {"link", child->link},
                                               {"size", child->size},
                                               {"modifiedMs", child->modified}});
                    visit(child);
                }
            };
        visit(rootNode);
        std::sort(entries.begin(), entries.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a["path"].toString() < b["path"].toString();
        });
        QJsonArray result;
        for (const auto &entry : entries)
            result.append(entry);
        return result;
    }

    QString digest(const std::shared_ptr<BootstrapNode> &node) {
        if (!node->directory || node->link) {
            auto found = hashes.constFind(node->diskPath);
            if (found != hashes.cend())
                return found.value();
        }
        QString value;
        if (node->directory && !node->link) {
            QCryptographicHash treeDigest(QCryptographicHash::Sha256);
            auto children = node->children;
            std::sort(children.begin(), children.end(), [](const auto &a, const auto &b) {
                return a->name < b->name;
            });
            for (const auto &child : children) {
                treeDigest.addData(child->name.toUtf8());
                treeDigest.addData(QByteArrayView("\0", 1));
                treeDigest.addData(entryType(child).toLatin1());
                treeDigest.addData(QByteArrayView("\0", 1));
                const auto childDigest = digest(child);
                if (childDigest.isEmpty())
                    return {};
                treeDigest.addData(childDigest.toLatin1());
            }
            value = QString::fromLatin1(treeDigest.result().toHex());
        } else {
            value = bootstrapDigest(root + '/' + node->diskPath, entryType(node), error);
        }
        if (!value.isEmpty() && (!node->directory || node->link))
            hashes[node->diskPath] = value;
        return value;
    }

    QString layoutDigest(const std::shared_ptr<BootstrapNode> &node) {
        if (!node->directory || node->link) {
            auto found = layoutHashes.constFind(node->diskPath);
            if (found != layoutHashes.cend())
                return found.value();
        }
        QString value;
        if (node->directory && !node->link) {
            QCryptographicHash treeDigest(QCryptographicHash::Sha256);
            auto children = node->children;
            std::sort(children.begin(), children.end(), [](const auto &a, const auto &b) {
                return a->name < b->name;
            });
            for (const auto &child : children) {
                treeDigest.addData(child->name.toUtf8());
                treeDigest.addData(QByteArrayView("\0", 1));
                treeDigest.addData(entryType(child).toLatin1());
                treeDigest.addData(QByteArrayView("\0", 1));
                const auto childDigest = layoutDigest(child);
                if (childDigest.isEmpty())
                    return {};
                treeDigest.addData(childDigest.toLatin1());
            }
            value = QString::fromLatin1(treeDigest.result().toHex());
        } else if (node->link) {
            value = bootstrapLayoutDigest(root + '/' + node->diskPath, "link", error);
        } else {
            value = hash("file");
        }
        if (!value.isEmpty() && (!node->directory || node->link))
            layoutHashes[node->diskPath] = value;
        return value;
    }

    void setLive(const std::shared_ptr<BootstrapNode> &node, const QString &area,
                 const QString &path) {
        node->liveArea = area;
        node->livePath = path;
        for (const auto &child : node->children)
            setLive(child, area, path + '/' + child->name);
    }

    QString newVaultPath() {
        return QString("bootstrap-vault/%1").arg(++vaultId, 8, 10, QChar('0'));
    }

    QString newTemporaryPath(const QString &base) {
        QString path;
        do {
            const QString name = QString(".tsre-core-%1").arg(++temporaryId, 8, 10, QChar('0'));
            path = base.isEmpty() ? name : base + '/' + name;
        } while (exact(root, path));
        return path;
    }

    void appendMove(const std::shared_ptr<BootstrapNode> &node, const QString &toArea,
                    const QString &to, const QString &expected = {}) {
        const QString value = expected.isEmpty() ? digest(node) : expected;
        if (value.isEmpty())
            return;
        QJsonObject action{{"kind", "bootstrap-move"},
                           {"entryType", entryType(node)},
                           {"fromArea", node->liveArea},
                           {"from", node->livePath},
                           {"toArea", toArea},
                           {"to", to},
                           {"expectedSha256", value}};
        if (node->directory && !node->link) {
            const auto expectedLayout = layoutDigest(node);
            if (expectedLayout.isEmpty())
                return;
            action["expectedLayoutSha256"] = expectedLayout;
        }
        plan.actions.append(action);
        setLive(node, toArea, to);
    }

    void vaultWhole(const std::shared_ptr<BootstrapNode> &node) {
        appendMove(node, "journal", newVaultPath());
    }

    std::shared_ptr<BootstrapNode> childDirectory(
        const std::shared_ptr<BootstrapNode> &parentNode, const QString &name) const {
        for (const auto &child : parentNode->children)
            if (child->directory && !child->link &&
                child->name.compare(name, Qt::CaseInsensitive) == 0)
                return child;
        return {};
    }

    static std::shared_ptr<BootstrapNode> preferred(
        QList<std::shared_ptr<BootstrapNode>> entries, const QString &name) {
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
            if (a->name != b->name)
                return a->name < b->name;
            return a->diskPath < b->diskPath;
        });
        for (const auto &entry : entries)
            if (entry->name == name)
                return entry;
        return entries.isEmpty() ? std::shared_ptr<BootstrapNode>() : entries.first();
    }

    void recordCollision(const std::shared_ptr<BootstrapNode> &kept,
                         const std::shared_ptr<BootstrapNode> &alternate,
                         const QString &reason) {
        conflicts.append({kept, alternate, reason});
    }

    void mergeDirectories(const std::shared_ptr<BootstrapNode> &target,
                          const std::shared_ptr<BootstrapNode> &source) {
        const auto sourceChildren = source->children;
        for (const auto &entry : sourceChildren) {
            QList<std::shared_ptr<BootstrapNode>> matches;
            for (const auto &candidate : target->children)
                if (candidate->name.compare(entry->name, Qt::CaseInsensitive) == 0)
                    matches.append(candidate);
            if (matches.isEmpty()) {
                const QString destination = target->livePath.isEmpty()
                                                ? entry->name
                                                : target->livePath + '/' + entry->name;
                appendMove(entry, "root", destination);
                if (!error.isEmpty())
                    return;
                source->children.removeOne(entry);
                target->children.append(entry);
                ++plan.uniqueEntriesMoved;
                continue;
            }
            const auto kept = preferred(matches, entry->name);
            if (kept->directory && !kept->link && entry->directory && !entry->link) {
                mergeDirectories(kept, entry);
                if (!error.isEmpty())
                    return;
                ++plan.directoryTreesMerged;
                continue;
            }
            bool identical = false;
            if (!kept->directory && !kept->link && !entry->directory && !entry->link) {
                const auto keptHash = digest(kept), alternateHash = digest(entry);
                if (!error.isEmpty())
                    return;
                identical = keptHash == alternateHash;
            } else if (kept->link && entry->link) {
                identical = QFileInfo(root + '/' + kept->diskPath).symLinkTarget() ==
                            QFileInfo(root + '/' + entry->diskPath).symLinkTarget();
            }
            if (identical) {
                ++plan.identicalEntriesVaulted;
            } else {
                recordCollision(kept, entry,
                                entryType(kept) == entryType(entry)
                                    ? "Different entries share one case-insensitive core path"
                                    : "Different entry types share one case-insensitive core path");
            }
        }
    }

    void canonicalize(const std::shared_ptr<BootstrapNode> &parentNode,
                      const QString &canonicalName) {
        if (!error.isEmpty())
            return;
        QList<std::shared_ptr<BootstrapNode>> directories, other;
        for (const auto &child : parentNode->children) {
            if (child->name.compare(canonicalName, Qt::CaseInsensitive) != 0)
                continue;
            if (child->directory && !child->link)
                directories.append(child);
            else
                other.append(child);
        }
        if (directories.isEmpty())
            return;
        auto winner = preferred(directories, canonicalName);
        for (const auto &entry : other) {
            vaultWhole(entry);
            if (!error.isEmpty())
                return;
            parentNode->children.removeOne(entry);
        }
        if (winner->name != canonicalName) {
            const QString base = parent(winner->livePath);
            const QString destination = base.isEmpty() ? canonicalName : base + '/' + canonicalName;
            const QString temporary = newTemporaryPath(base);
            const QString expected = digest(winner);
            if (expected.isEmpty())
                return;
            appendMove(winner, "root", temporary, expected);
            appendMove(winner, "root", destination, expected);
            winner->name = canonicalName;
            ++plan.directoriesRenamed;
        }
        for (const auto &entry : directories) {
            if (entry == winner)
                continue;
            vaultWhole(entry);
            if (!error.isEmpty())
                return;
            mergeDirectories(winner, entry);
            if (!error.isEmpty())
                return;
            parentNode->children.removeOne(entry);
            ++plan.directoryTreesMerged;
        }
        for (const auto &entry : other)
            recordCollision(winner, entry, "A non-directory blocks a fixed core directory name");
    }
};

BootstrapPlan bootstrapPlan(const QString &root, QString &error) {
    BootstrapPlanner planner(root);
    if (!planner.build()) {
        error = planner.error;
        return {};
    }
    error.clear();
    return planner.plan;
}

QJsonArray lightweightLayout(const QString &root, QString &error) {
    BootstrapPlanner planner(root);
    const auto tree = planner.inventory();
    if (!tree) {
        error = planner.error;
        return {};
    }
    // A no-op plan leaves the live paths equal to the current disk paths.
    BootstrapPlan current;
    std::function<void(const std::shared_ptr<BootstrapNode> &)> visit =
        [&](const std::shared_ptr<BootstrapNode> &node) {
            for (const auto &child : node->children) {
                current.expectedLayout.append(QJsonObject{{"path", child->livePath},
                                                           {"directory", child->directory},
                                                           {"link", child->link},
                                                           {"size", child->size},
                                                           {"modifiedMs", child->modified}});
                visit(child);
            }
        };
    visit(tree);
    QVector<QJsonObject> sorted;
    for (const auto &value : current.expectedLayout)
        sorted.append(value.toObject());
    std::sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a["path"].toString() < b["path"].toString();
    });
    QJsonArray result;
    for (const auto &entry : sorted)
        result.append(entry);
    error.clear();
    return result;
}

struct OverlayEntry {
    QString source;
    QString target;
    QString sourceSha256;
    QString targetSha256;
    bool directory = false;
    bool targetExists = false;
    qint64 sourceModifiedMs = 0;
    qint64 targetModifiedMs = 0;
    QFile::Permissions sourcePermissions;
    QFile::Permissions targetPermissions;
};

struct OverlayPlan {
    QVector<OverlayEntry> directories;
    QVector<OverlayEntry> files;
    QJsonObject summary;
    QByteArray fingerprint;

    bool hasActions() const {
        for (const auto &entry : directories)
            if (!entry.targetExists)
                return true;
        for (const auto &entry : files)
            if (!entry.targetExists || entry.sourceSha256 != entry.targetSha256)
                return true;
        return false;
    }
};

QString canonicalOverlayName(const QString &targetParent, const QString &sourceName) {
    auto fixed = [&](const QStringList &names) {
        for (const auto &name : names)
            if (sourceName.compare(name, Qt::CaseInsensitive) == 0)
                return name;
        return sourceName;
    };
    if (targetParent.isEmpty())
        return fixed({"ROUTES", "GLOBAL", "TRAINS", "SOUND", "TEMPLATE"});
    if (targetParent == "GLOBAL")
        return fixed({"SHAPES", "TEXTURES"});
    if (targetParent == "TRAINS")
        return fixed({"TRAINSET", "CONSISTS"});
    if (targetParent == "TRAINS/CONSISTS")
        return fixed({"RANDOMSETS"});
    const auto parts = targetParent.split('/', Qt::SkipEmptyParts);
    if (parts.size() == 2 && parts[0] == "ROUTES")
        return fixed({"ACTIVITIES", "ADDONS", "ENVFILES", "LO_TILES", "OPENRAILS",
                      "PATHS", "PROCEDURAL", "SERVICES", "SHAPES", "SOUND", "TD",
                      "TERRAIN_MAPS", "TERRTEX", "TEXTURES", "TILES", "TRACKPROFILES",
                      "TRAFFIC", "WORLD"});
    if (parts.size() == 3 && parts[0] == "ROUTES" && parts[2] == "ACTIVITIES")
        return fixed({"OPENRAILS"});
    if (parts.size() == 3 && parts[0] == "ROUTES" && parts[2] == "ENVFILES")
        return fixed({"TEXTURES"});
    return sourceName;
}

OverlayPlan updateOverlayPlan(const QString &root, const QString &updateRoot, QString &error) {
    OverlayPlan plan;
    QJsonArray directoryFingerprint, fileFingerprint;
    int identicalFiles = 0, replacedFiles = 0, addedFiles = 0, createdDirectories = 0,
        caseMatchedEntries = 0;
    std::function<bool(const QString &, const QString &, bool)> walk =
        [&](const QString &sourceParent, const QString &targetParent, bool targetParentExists) {
            const QString sourcePath = updateRoot +
                                       (sourceParent.isEmpty() ? QString() : '/' + sourceParent);
            QDir sourceDirectory(sourcePath);
            if (!sourceDirectory.isReadable()) {
                error = "Unreadable update directory: " + sourcePath;
                return false;
            }
            const auto sourceEntries = sourceDirectory.entryInfoList(
                QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                QDir::Name);
            QHash<QString, QString> sourceNames;
            for (const auto &entry : sourceEntries) {
                const QString key = entry.fileName().toCaseFolded();
                if (sourceNames.contains(key)) {
                    error = "Update contains case-only sibling entries: " +
                            (sourceParent.isEmpty() ? QString() : sourceParent + '/') +
                            sourceNames.value(key) + " / " + entry.fileName();
                    return false;
                }
                sourceNames[key] = entry.fileName();
            }
            QFileInfoList targetEntries;
            if (targetParentExists) {
                const QString targetPath = root +
                                           (targetParent.isEmpty() ? QString() : '/' + targetParent);
                QDir targetDirectory(targetPath);
                if (!targetDirectory.isReadable()) {
                    error = "Unreadable overlay destination directory: " + targetPath;
                    return false;
                }
                targetEntries = targetDirectory.entryInfoList(
                    QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDir::Name);
            }
            for (const auto &sourceInfo : sourceEntries) {
                if (sourceInfo.isSymLink() || sourceInfo.isJunction()) {
                    error = "Update overlays cannot contain links: " + sourceInfo.filePath();
                    return false;
                }
                if (!sourceInfo.isDir() && !sourceInfo.isFile()) {
                    error = "Unsupported update entry type: " + sourceInfo.filePath();
                    return false;
                }
                QFileInfoList matches;
                for (const auto &targetInfo : targetEntries)
                    if (targetInfo.fileName().compare(sourceInfo.fileName(),
                                                      Qt::CaseInsensitive) == 0)
                        matches.append(targetInfo);
                if (matches.size() > 1) {
                    error = "Overlay destination has ambiguous case variants for " +
                            (targetParent.isEmpty() ? sourceInfo.fileName()
                                                    : targetParent + '/' + sourceInfo.fileName());
                    return false;
                }
                const bool targetExists = !matches.isEmpty();
                const QString targetName = targetExists
                                               ? matches.first().fileName()
                                               : canonicalOverlayName(targetParent,
                                                                      sourceInfo.fileName());
                const QString sourceRelative = sourceParent.isEmpty()
                                                   ? sourceInfo.fileName()
                                                   : sourceParent + '/' + sourceInfo.fileName();
                const QString targetRelative = targetParent.isEmpty()
                                                   ? targetName
                                                   : targetParent + '/' + targetName;
                if (targetName != sourceInfo.fileName() &&
                    targetName.compare(sourceInfo.fileName(), Qt::CaseInsensitive) == 0)
                    ++caseMatchedEntries;
                OverlayEntry entry;
                entry.source = sourceRelative;
                entry.target = targetRelative;
                entry.directory = sourceInfo.isDir();
                entry.targetExists = targetExists;
                entry.sourceModifiedMs = sourceInfo.lastModified().toMSecsSinceEpoch();
                entry.sourcePermissions = sourceInfo.permissions();
                if (targetExists) {
                    const auto targetInfo = matches.first();
                    if (targetInfo.isSymLink() || targetInfo.isJunction()) {
                        error = "Overlay destination is a link: " + targetInfo.filePath();
                        return false;
                    }
                    if (targetInfo.isDir() != sourceInfo.isDir() ||
                        targetInfo.isFile() != sourceInfo.isFile()) {
                        error = "Update/destination entry type conflict: " + sourceRelative +
                                " -> " + targetRelative;
                        return false;
                    }
                    entry.targetModifiedMs = targetInfo.lastModified().toMSecsSinceEpoch();
                    entry.targetPermissions = targetInfo.permissions();
                }
                if (entry.directory) {
                    plan.directories.append(entry);
                    directoryFingerprint.append(
                        QJsonObject{{"source", entry.source},
                                    {"target", entry.target},
                                    {"targetExists", entry.targetExists}});
                    if (!targetExists)
                        ++createdDirectories;
                    if (!walk(sourceRelative, targetRelative, targetExists))
                        return false;
                    continue;
                }
                entry.sourceSha256 = fileHash(sourceInfo.filePath(), error);
                if (entry.sourceSha256.isEmpty())
                    return false;
                if (targetExists) {
                    entry.targetSha256 = fileHash(matches.first().filePath(), error);
                    if (entry.targetSha256.isEmpty())
                        return false;
                    if (entry.sourceSha256 == entry.targetSha256)
                        ++identicalFiles;
                    else
                        ++replacedFiles;
                } else {
                    ++addedFiles;
                }
                plan.files.append(entry);
                fileFingerprint.append(
                    QJsonObject{{"source", entry.source},
                                {"target", entry.target},
                                {"sourceSha256", entry.sourceSha256},
                                {"targetSha256", entry.targetSha256},
                                {"targetExists", entry.targetExists},
                                {"sourceModifiedMs", entry.sourceModifiedMs},
                                {"targetModifiedMs", entry.targetModifiedMs},
                                {"sourcePermissions", int(entry.sourcePermissions)},
                                {"targetPermissions", int(entry.targetPermissions)}});
            }
            return true;
        };
    if (!walk({}, {}, true))
        return {};
    plan.summary = QJsonObject{{"sourceRoot", updateRoot},
                               {"sourceFiles", plan.files.size()},
                               {"sourceDirectories", plan.directories.size()},
                               {"identicalFiles", identicalFiles},
                               {"filesReplaced", replacedFiles},
                               {"filesAdded", addedFiles},
                               {"directoriesCreated", createdDirectories},
                               {"caseMatchedEntries", caseMatchedEntries}};
    plan.fingerprint = QJsonDocument(QJsonObject{{"directories", directoryFingerprint},
                                                  {"files", fileFingerprint}})
                           .toJson(QJsonDocument::Compact);
    error.clear();
    return plan;
}

QString mapped(const QMap<QString, QString> &paths, QString path) {
    path = clean(path);
    QString at = path, tail;
    while (!at.isEmpty() && at != ".") {
        auto found = paths.constFind(at);
        if (found == paths.cend())
            found = paths.constFind(QChar(0) + at.toLower());
        if (found != paths.cend())
            return clean(found.value() + tail);
        tail = '/' + leaf(at) + tail;
        auto next = parent(at);
        if (next == at)
            break;
        at = next;
    }
    return path;
}
QString refKey(const QJsonObject &ref, const QString &source, const QMap<QString, QString> &paths) {
    QStringList bases;
    for (const auto &v : ref["searchBases"].toArray())
        bases << mapped(paths, v.toString()).toLower();
    // One scalar can name several route companions or seasonal ACE/DDS targets.
    return source + '|' + QString::number(ref["fieldIndex"].toInt(-1)) + '|' +
           QString::number(ref["scalarIndex"].toInt()) + '|' + ref["kind"].toString() + '|' +
           ref["suffix"].toString() + '|' + QString::number(ref["derivedDds"].toBool()) + '|' +
           bases.join(';');
}
struct Selection {
    QSet<int> blocked;
    QMap<int, QString> leaves;
    QMap<QString, QString> paths;
    QMap<int, QVector<ReferenceEdit>> edits;
    QJsonArray skipped;
};
Selection select(const QJsonObject &plan, const QSet<int> &extra) {
    const auto files = plan["files"].toArray(), refs = plan["references"].toArray();
    Selection result;
    // Content editability is local to a source. An unreadable shape or an opaque
    // database must not freeze every asset reachable through the reference graph.
    result.blocked = extra;
    QMap<int, QString> renameBlocks;
    QSet<QString> fixedPaths, collisionScopes;
    for (const auto &v : plan["failures"].toArray()) {
        const auto failure = v.toObject();
        const int id = failure["sourceFileId"].toInt(-1);
        if (id < 0)
            continue;
        const auto code = failure["code"].toString();
        if (code == "source-read-failed" || code == "reference-discovery-incomplete" ||
            code == "include-cycle")
            result.blocked.insert(id);
        if (code == "physical-name-conflict") {
            const auto f = files[id].toObject();
            renameBlocks[id] = "Unresolved physical name collision";
            if (f["directory"].toBool())
                collisionScopes.insert(f["path"].toString());
        }
    }
    for (int i = 0; i < files.size(); ++i) {
        const auto f = files[i].toObject();
        if (f["link"].toBool() || f["coverage"] == "unclassified" ||
            f["coverage"].toString().startsWith("excluded") ||
            f["referenceEditPolicy"].toString() == "rename-targets-only")
            result.blocked.insert(i);
        if (f["coverage"].toString().startsWith("excluded"))
            renameBlocks[i] = "Entry excluded from content repair";
        if (f["link"].toBool()) {
            renameBlocks[i] = "Link is not relocated";
            QString at = f["path"].toString();
            while (!at.isEmpty()) {
                fixedPaths.insert(at);
                at = parent(at);
            }
        }
    }
    for (const auto &v : plan["operations"].toArray()) {
        const auto op = v.toObject();
        if (op["operation"] == "edit-reference")
            continue; // Recompute required edits from the actually selected names.
        const int id = op["fileId"].toInt(-1);
        if (id < 0)
            continue;
        const QString from = files[id].toObject()["path"].toString();
        if (op["readiness"].toString().startsWith("blocked"))
            renameBlocks[id] = op["readiness"].toString();
        if (fixedPaths.contains(from))
            renameBlocks[id] = "Path contains a link that must not be relocated";
        for (const auto &scope : collisionScopes)
            if (under(from, scope))
                renameBlocks[id] = "Entry belongs to an unresolved directory collision";
        if (!renameBlocks.contains(id))
            result.leaves[id] = leaf(op["to"].toString());
    }
    QMap<QString, QString> plannedFieldSpellings;
    for (const auto &v : plan["operations"].toArray()) {
        const auto op = v.toObject();
        if (op["operation"] != "edit-reference" ||
            op["readiness"].toString().startsWith("blocked"))
            continue;
        const int edgeId = op["edgeId"].toInt(-1);
        if (edgeId < 0 || edgeId >= refs.size())
            continue;
        const auto edge = refs[edgeId].toObject();
        const QString key = QString::number(edge["sourceFileId"].toInt(-1)) + '|' +
                            QString::number(edge["fieldIndex"].toInt(-1)) + '|' +
                            QString::number(edge["scalarIndex"].toInt());
        plannedFieldSpellings[key] = op["proposedLogicalReference"].toString();
    }
    // Inventory is parent-before-child; map ancestors even when a parent rename
    // was withheld, so independent leaf repairs use the actual selected paths.
    QMap<QString, int> spellings;
    for (const auto &v : files)
        ++spellings[v.toObject()["path"].toString().toLower()];
    auto mapPaths = [&]() {
        result.paths.clear();
        for (int i = 0; i < files.size(); ++i) {
            const QString from = files[i].toObject()["path"].toString();
            const QString base = parent(from),
                          to = clean((base.isEmpty() ? QString() : mapped(result.paths, base) + '/') +
                                     result.leaves.value(i, leaf(from)));
            result.paths[from] = to;
            if (spellings.value(from.toLower()) == 1)
                result.paths[QChar(0) + from.toLower()] = to;
        }
    };
    QMap<QString, int> fileIds;
    for (int i = 0; i < files.size(); ++i)
        fileIds[files[i].toObject()["path"].toString()] = i;
    // A noneditable scalar constrains only components explicitly named by that
    // reference. Implicit companions and seasonal-only candidates have no
    // independent scalar which execution can rewrite, so they also constrain
    // their target leaf. Default lookup directories may still adopt their
    // runtime case. Removing a rename can affect other constraints, so repeat
    // to a fixed point.
    bool changed;
    do {
        mapPaths();
        changed = false;
        for (const auto &v : refs) {
            const auto r = v.toObject();
            const int s = r["sourceFileId"].toInt(-1), t = r["targetFileId"].toInt(-1);
            const bool companion = r["implicit"].toBool() &&
                                   r["kind"] == "shape-companion" &&
                                   r["status"] == "exact";
            const bool seasonalOnly = r["kind"] == "seasonal-texture-candidate" &&
                                      r["status"] == "exact";
            const bool fixedSource = result.blocked.contains(s) && !r["implicit"].toBool();
            if (s < 0 || t < 0 || (!fixedSource && !companion && !seasonalOnly))
                continue;
            const auto bases = r["searchBases"].toArray();
            const int priority = r["selectedSearchBase"].toInt(-1);
            if (priority < 0 || priority >= bases.size())
                continue;
            const QString targetPath = files[t].toObject()["path"].toString();
            const auto actual = mapped(result.paths, targetPath).split('/');
            QString spelling = r["spelling"].toString();
            if (companion) {
                QString sourceName = leaf(mapped(result.paths,
                                                 files[s].toObject()["path"].toString()));
                const int suffix = sourceName.lastIndexOf('.');
                if (suffix > 0)
                    sourceName.truncate(suffix);
                spelling = sourceName + ".sd";
            } else if (seasonalOnly && !result.blocked.contains(s)) {
                const QString key = QString::number(s) + '|' +
                                    QString::number(r["fieldIndex"].toInt(-1)) + '|' +
                                    QString::number(r["scalarIndex"].toInt());
                spelling = plannedFieldSpellings.value(key, spelling);
            }
            const auto requested = clean(mapped(result.paths, bases[priority].toString()) + '/' +
                                         spelling).split('/');
            // Planned repairs are case-only; unresolved pre-existing mismatches
            // remain reported. Withhold only renames introducing a mismatching
            // component, without propagating protection through unrelated edges.
            const QString reason = (companion ? "Name required by implicit companion: "
                                              : seasonalOnly
                                                    ? "Name required by seasonal-only reference: "
                                                    : "Name required by noneditable reference: ") +
                                   files[s].toObject()["path"].toString() + " / " +
                                   r["location"].toString();
            auto withhold = [&](QString at, const QStringList &selected,
                                const QStringList &required) {
                for (int component = selected.size() - 1;
                     component >= 0 && !at.isEmpty(); --component) {
                    const int id = fileIds.value(at, -1);
                    if (id >= 0 && result.leaves.contains(id) &&
                        (component >= required.size() || selected[component] != required[component])) {
                        result.leaves.remove(id);
                        renameBlocks[id] = reason;
                        changed = true;
                    }
                    at = parent(at);
                }
            };
            withhold(targetPath, actual, requested);
            // If the target itself is fixed, the source side of a companion pair
            // must not move to a different stem either.
            if (companion && actual != requested && result.leaves.contains(s)) {
                result.leaves.remove(s);
                renameBlocks[s] = reason;
                changed = true;
            }
        }
    } while (changed);
    for (const auto &v : plan["operations"].toArray()) {
        auto op = v.toObject();
        if (op["operation"] == "edit-reference") {
            if (!result.blocked.contains(op["sourceFileId"].toInt(-1)))
                continue;
            op["executionReason"] = "Source cannot be rewritten; known references constrain target names";
        } else {
            const int id = op["fileId"].toInt(-1);
            if (!renameBlocks.contains(id))
                continue;
            op["executionReason"] = renameBlocks.value(id);
        }
        result.skipped.append(op);
    }
    for (const auto &v : refs) {
        const auto r = v.toObject();
        const int s = r["sourceFileId"].toInt(-1), t = r["targetFileId"].toInt(-1);
        if (s < 0 || t < 0 || result.blocked.contains(s) ||
            r["implicit"].toBool() || r["kind"] == "seasonal-texture-candidate")
            continue;
        const auto bases = r["searchBases"].toArray();
        const int priority = r["selectedSearchBase"].toInt(-1);
        if (priority < 0 || priority >= bases.size())
            continue;
        const QString base = mapped(result.paths, bases[priority].toString()),
                      target = mapped(result.paths, files[t].toObject()["path"].toString());
        if (clean(base + '/' + r["spelling"].toString()) == target)
            continue;
        // QDir::relativeFilePath on Windows folds case. Use a component-exact
        // relative path calculation on all platforms.
        const auto from = base.split('/', Qt::SkipEmptyParts),
                   to = target.split('/', Qt::SkipEmptyParts);
        int common = 0;
        while (common < from.size() && common < to.size() && from[common] == to[common])
            ++common;
        QStringList parts;
        for (int i = common; i < from.size(); ++i)
            parts << "..";
        parts.append(to.mid(common));
        QString spelling = parts.join('/');
        const QString suffix = r["suffix"].toString();
        if (!suffix.isEmpty() && spelling.endsWith(suffix, Qt::CaseInsensitive))
            spelling.chop(suffix.size());
        if (r["derivedDds"].toBool())
            spelling = spelling.left(spelling.size() - 4) + '.' +
                       QFileInfo(r["authoredTextureReference"].toString()).suffix();
        const auto values = r["sourceScalars"].toArray();
        const int field = r["fieldIndex"].toInt(-1), scalar = r["scalarIndex"].toInt();
        if (field < 0 || scalar >= values.size())
            continue;
        if (r["kind"] == "enginedata" || r["kind"] == "wagondata") {
            if (values.size() < 2)
                continue;
            result.edits[s].push_back(
                {field, 0, values[0].toObject()["text"].toString(), leaf(spelling)});
            result.edits[s].push_back(
                {field, 1, values[1].toObject()["text"].toString(), parent(spelling)});
        } else
            result.edits[s].push_back(
                {field, scalar, values[scalar].toObject()["text"].toString(), spelling});
    }
    return result;
}
QString actionBase(const QString &root, const QString &journalDirectory, const QString &area) {
    return area == "journal" ? journalDirectory : root;
}
bool applyJournalAction(const QString &root, const QString &journalDirectory,
                        const QJsonObject &action, int step, QString &error) {
    const QString kind = action["kind"].toString(), from = action["from"].toString();
    const QString fromBase = actionBase(root, journalDirectory,
                                        action["fromArea"].toString("root"));
    if (kind == "bootstrap-move") {
        const QString to = action["to"].toString();
        const QString toBase = actionBase(root, journalDirectory,
                                          action["toArea"].toString("root"));
        if (!safeMovePath(fromBase, from, error) || !exact(fromBase, from)) {
            if (error.isEmpty())
                error = "Bootstrap source disappeared: " + from;
            return false;
        }
        const QString digest =
            bootstrapDigest(fromBase + '/' + from, action["entryType"].toString(), error);
        if (digest.isEmpty())
            return false;
        if (digest != action["expectedSha256"].toString()) {
            error = "Bootstrap source changed: " + from;
            return false;
        }
        if (!append(journalDirectory,
                    {{"step", step}, {"phase", "started"}, {"digest", digest}}, error))
            return false;
        if (!moveBetween(fromBase, from, toBase, to, error))
            return false;
        return append(journalDirectory, {{"step", step}, {"phase", "done"}}, error);
    }
    if (kind == "make-directory" || kind == "add-file") {
        if (!safe(root, from, error))
            return false;
        const QString base = parent(from), name = leaf(from);
        const QString parentPath = base.isEmpty() ? root : root + '/' + base;
        if ((!base.isEmpty() && !exact(root, base)) || !QFileInfo(parentPath).isDir()) {
            error = "Overlay destination parent disappeared: " + base;
            return false;
        }
        for (const auto &entry : QDir(parentPath).entryList(
                 QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
            if (entry.compare(name, Qt::CaseInsensitive) == 0) {
                error = "Overlay destination appeared before apply: " + from;
                return false;
            }
        }
        if (!append(journalDirectory, {{"step", step}, {"phase", "started"}}, error))
            return false;
        if (kind == "make-directory") {
            if (!QDir(parentPath).mkdir(name)) {
                error = "Cannot create overlay directory: " + from;
                return false;
            }
        } else {
            QFile staged(journalDirectory + '/' + action["staged"].toString());
            if (!staged.open(QIODevice::ReadOnly)) {
                error = "Missing staged overlay file";
                return false;
            }
            const auto bytes = staged.readAll();
            if (hash(bytes) != action["afterSha256"].toString()) {
                error = "Staged overlay file hash mismatch";
                return false;
            }
            if (!writeNew(root + '/' + from, bytes, error) ||
                !setFileMetadata(root + '/' + from,
                                 QFile::Permissions(action["permissions"].toInt()),
                                 action["modifiedMs"].toVariant().toLongLong(), error))
                return false;
        }
        return append(journalDirectory, {{"step", step}, {"phase", "done"}}, error);
    }
    if (!safe(root, from, error) || !exact(root, from)) {
        if (error.isEmpty())
            error = "Source disappeared: " + from;
        return false;
    }
    const QString digest = kind == "move-directory" ? treeHash(root + '/' + from, error)
                                                     : fileHash(root + '/' + from, error);
    if (digest.isEmpty())
        return false;
    if (kind == "replace" && digest != action["beforeSha256"].toString()) {
        error = "Edit source changed: " + from;
        return false;
    }
    if (kind == "move-file" && digest != action["expectedSha256"].toString()) {
        error = "Rename source changed: " + from;
        return false;
    }
    if (!append(journalDirectory,
                {{"step", step}, {"phase", "started"}, {"digest", digest}}, error))
        return false;
    if (kind == "replace") {
        QFile f(journalDirectory + '/' + action["staged"].toString());
        if (!f.open(QIODevice::ReadOnly)) {
            error = "Missing staged replacement";
            return false;
        }
        const auto bytes = f.readAll();
        if (hash(bytes) != action["afterSha256"] ||
            !replace(root + '/' + from, bytes, action, error)) {
            if (error.isEmpty())
                error = "Staged replacement hash mismatch";
            return false;
        }
    } else if (kind == "move-file" || kind == "move-directory") {
        if (!move(root, from, action["to"].toString(), error))
            return false;
    } else {
        error = "Unknown journal operation: " + kind;
        return false;
    }
    return append(journalDirectory, {{"step", step}, {"phase", "done"}}, error);
}
} // namespace

bool verifyReferences(const QJsonObject &before, const QJsonObject &after,
                      const QMap<QString, QString> &paths, QString &error) {
    const auto oldFiles = before["files"].toArray(), newFiles = after["files"].toArray();
    QMap<QString, QVector<QJsonObject>> refs;
    for (const auto &v : after["references"].toArray()) {
        const auto r = v.toObject();
        const int s = r["sourceFileId"].toInt();
        refs[refKey(r, newFiles[s].toObject()["path"].toString(), {})].push_back(r);
    }
    for (const auto &v : before["references"].toArray()) {
        const auto r = v.toObject();
        const int target = r["targetFileId"].toInt(-1);
        if (target < 0)
            continue;
        const QString source =
            mapped(paths, oldFiles[r["sourceFileId"].toInt()].toObject()["path"].toString());
        const QString expected = mapped(paths, oldFiles[target].toObject()["path"].toString());
        bool targetPreserved = false, exactPreserved = false;
        for (const auto &actual : refs.value(refKey(r, source, paths))) {
            const int actualTarget = actual["targetFileId"].toInt(-1);
            if (actualTarget < 0 || newFiles[actualTarget].toObject()["path"] != expected)
                continue;
            targetPreserved = true;
            exactPreserved |= actual["status"] == "exact";
        }
        if (!targetPreserved) {
            error = "Verification changed a reference target: " + source + " / " +
                    r["location"].toString() + " expected " + expected;
            return false;
        }
        if (r["status"] == "exact" && !exactPreserved) {
            error = "Previously exact reference no longer has exact spelling: " + source;
            return false;
        }
    }
    return true;
}

bool rollback(const QString &rootInput, const QString &journalPath, QString &error,
              const std::function<void(const QString &)> &progress) {
    error.clear();
    const QString root = QFileInfo(rootInput).canonicalFilePath(),
                  directory = QFileInfo(journalPath).absolutePath();
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        error = "Invalid rollback game root";
        return false;
    }
    if (under(QFileInfo(directory).canonicalFilePath().toLower(), root.toLower())) {
        error = "Recovery journal must be outside the game root";
        return false;
    }
    QLockFile lock(registry(root) + ".lock");
    if (!lock.tryLock(0)) {
        error = "Another converter is using this root";
        return false;
    }
    auto journal = readJson(journalPath, error);
    if (journal.isEmpty())
        return false;
    const QString activePath = registry(root) + ".json";
    if (QFileInfo::exists(activePath)) {
        const auto active = readJson(activePath, error);
        if (active.isEmpty())
            return false;
        if (QFileInfo(active["journal"].toString()).absoluteFilePath() !=
            QFileInfo(journalPath).absoluteFilePath()) {
            error = "Another interrupted transaction must be recovered first";
            return false;
        }
    }
    if ((journal["schema"] != "tsre-content-case-journal-v1" &&
         journal["schema"] != "tsre-content-case-journal-v2") ||
        journal["gameRoot"] != root) {
        error = "Journal belongs to another root or version";
        return false;
    }
    const auto actions = journal["actions"].toArray();
    QMap<int, QJsonObject> states;
    QFile log(directory + "/events.jsonl");
    if (!log.open(QIODevice::ReadWrite)) {
        error = "Cannot open recovery events";
        return false;
    }
    qint64 complete = 0;
    while (!log.atEnd()) {
        const auto line = log.readLine();
        if (!line.endsWith('\n'))
            break;
        QJsonParseError parse;
        const auto event = QJsonDocument::fromJson(line, &parse).object();
        if (parse.error != QJsonParseError::NoError) {
            error = "Damaged recovery log";
            return false;
        }
        const int id = event["step"].toInt(-1);
        if (id >= 0) {
            if (id >= actions.size()) {
                error = "Invalid recovery step";
                return false;
            }
            auto state = states.value(id);
            for (auto it = event.begin(); it != event.end(); ++it)
                state[it.key()] = it.value();
            states[id] = state;
        }
        complete = log.pos();
    }
    if (log.size() != complete && !log.resize(complete)) {
        error = "Cannot discard interrupted event tail";
        return false;
    }
    log.close();
    for (int i = actions.size() - 1; i >= 0; --i) {
        const auto state = states.value(i);
        if (state.isEmpty() || state["phase"] == "undone")
            continue;
        const auto a = actions[i].toObject();
        const QString kind = a["kind"].toString(), from = a["from"].toString();
        if (progress)
            progress(QString("Rollback %1/%2: %3")
                         .arg(actions.size() - i)
                         .arg(actions.size())
                         .arg(from));
        if (kind == "bootstrap-move") {
            const QString to = a["to"].toString();
            const QString fromBase =
                              actionBase(root, directory, a["fromArea"].toString("root")),
                          toBase = actionBase(root, directory, a["toArea"].toString("root"));
            if (!safeMovePath(fromBase, from, error) || !safeMovePath(toBase, to, error))
                return false;
            const bool atFrom = exact(fromBase, from), atTo = exact(toBase, to);
            if (atFrom == atTo) {
                error = "Ambiguous bootstrap recovery locations: " + from + " / " + to;
                return false;
            }
            const QString current = bootstrapDigest(
                (atTo ? toBase + '/' + to : fromBase + '/' + from),
                a["entryType"].toString(), error);
            if (current.isEmpty())
                return false;
            if (current != state["digest"].toString()) {
                const QString currentPath = atTo ? toBase + '/' + to : fromBase + '/' + from;
                const auto layout = a["entryType"] == "directory"
                                        ? bootstrapLayoutDigest(currentPath, "directory", error)
                                        : QString();
                if (layout.isEmpty() || layout != a["expectedLayoutSha256"].toString()) {
                    error = "Refusing rollback of structurally modified bootstrap entry: " +
                            (atTo ? to : from);
                    return false;
                }
            }
            if (atTo && !moveBetween(toBase, to, fromBase, from, error))
                return false;
        } else if (kind == "add-file") {
            if (!safe(root, from, error))
                return false;
            if (exact(root, from)) {
                const auto current = fileHash(root + '/' + from, error);
                if (current.isEmpty())
                    return false;
                if (current != a["afterSha256"].toString()) {
                    error = "Refusing rollback over modified overlay file: " + from;
                    return false;
                }
                if (!QFile::remove(root + '/' + from)) {
                    error = "Cannot remove added overlay file: " + from;
                    return false;
                }
            }
        } else if (kind == "make-directory") {
            if (!safe(root, from, error))
                return false;
            if (exact(root, from)) {
                QDir directory(root + '/' + from);
                if (!directory.exists() ||
                    !directory.entryList(QDir::AllEntries | QDir::Hidden | QDir::System |
                                         QDir::NoDotAndDotDot)
                         .isEmpty()) {
                    error = "Refusing rollback of non-empty overlay directory: " + from;
                    return false;
                }
                const QString base = parent(from), name = leaf(from);
                if (!QDir(base.isEmpty() ? root : root + '/' + base).rmdir(name)) {
                    error = "Cannot remove added overlay directory: " + from;
                    return false;
                }
            }
        } else if (!safe(root, from, error)) {
            return false;
        } else if (kind == "replace") {
            const auto current = fileHash(root + '/' + from, error);
            if (current.isEmpty())
                return false;
            if (current != a["beforeSha256"].toString()) {
                if (current != a["afterSha256"].toString()) {
                    error = "Refusing rollback over modified content: " + from;
                    return false;
                }
                const QString backup = a["backup"].toString();
                if (!safe(directory, backup, error))
                    return false;
                QFile f(directory + '/' + backup);
                if (!f.open(QIODevice::ReadOnly)) {
                    error = "Missing backup: " + backup;
                    return false;
                }
                const auto bytes = f.readAll();
                if (hash(bytes) != a["beforeSha256"]) {
                    error = "Backup hash mismatch: " + backup;
                    return false;
                }
                if (!replace(root + '/' + from, bytes, a, error))
                    return false;
            }
        } else if (kind == "move-file" || kind == "move-directory") {
            const QString to = a["to"].toString();
            if (!safe(root, to, error))
                return false;
            const bool atFrom = exact(root, from), atTo = exact(root, to);
            if (atFrom == atTo) {
                error = "Ambiguous recovery locations: " + from + " / " + to;
                return false;
            }
            const auto current = kind == "move-directory"
                                     ? treeHash(root + '/' + (atTo ? to : from), error)
                                     : fileHash(root + '/' + (atTo ? to : from), error);
            if (current.isEmpty() || current != state["digest"].toString()) {
                error = "Refusing rollback of modified asset/directory: " + (atTo ? to : from);
                return false;
            }
            if (atTo && !move(root, to, from, error))
                return false;
        } else {
            error = "Unknown journal operation";
            return false;
        }
        if (!append(directory, {{"step", i}, {"phase", "undone"}}, error))
            return false;
    }
    // A killed atomic writer may leave a temporary file even when its target
    // still has the old bytes. Do not silently declare the original layout
    // restored, or remove an unexpected file which might belong to the user.
    const auto restoredPlan = scan(root, error, progress);
    if (restoredPlan.isEmpty())
        return false;
    QMap<QString, int> expectedLayout, actualLayout;
    for (const auto &v : journal["originalLayout"].toArray()) {
        const auto f = v.toObject();
        expectedLayout[f["path"].toString()] =
            int(f["directory"].toBool()) + 2 * int(f["link"].toBool());
    }
    for (const auto &v : restoredPlan["files"].toArray()) {
        const auto f = v.toObject();
        actualLayout[f["path"].toString()] =
            int(f["directory"].toBool()) + 2 * int(f["link"].toBool());
    }
    if (expectedLayout != actualLayout) {
        error = "Rollback restored recorded actions, but the root has unexpected, missing, or "
                "differently named entries; inspect the root and retry rollback";
        return false;
    }
    if (!append(directory, {{"phase", "rolled-back"}}, error))
        return false;
    if (QFileInfo::exists(activePath) && !QFile::remove(activePath)) {
        error = "Cannot clear recovered transaction marker";
        return false;
    }
    return true;
}

static QJsonObject executeImpl(const QString &rootInput, const QJsonObject &savedPlan,
                               const QString &journalDirectory, const QString &updateRootInput,
                               QString &error,
                               const std::function<void(const QString &)> &progress,
                               const std::function<bool(int)> &continuation) {
    error.clear();
    const QString root = QFileInfo(rootInput).canonicalFilePath();
    auto fail = [&](const QString &s) -> QJsonObject {
        error = s;
        return {};
    };
    if (root.isEmpty() || !QFileInfo(root).isDir())
        return fail("Invalid game root");
    const bool hasUpdate = !updateRootInput.isEmpty();
    const QString updateRoot = hasUpdate ? QFileInfo(updateRootInput).canonicalFilePath()
                                         : QString();
    if (hasUpdate && (updateRoot.isEmpty() || !QFileInfo(updateRoot).isDir()))
        return fail("Invalid update root");
    if (hasUpdate &&
        (under(updateRoot.toCaseFolded(), root.toCaseFolded()) ||
         under(root.toCaseFolded(), updateRoot.toCaseFolded())))
        return fail("Update root and game root must not contain one another");
    if (hasUpdate && !savedPlan.isEmpty())
        return fail("A saved content-case plan cannot be combined with an update overlay");
    QLockFile lock(registry(root) + ".lock");
    if (!lock.tryLock(0))
        return fail("Another converter is using this root");
    const QString activePath = registry(root) + ".json";
    if (QFileInfo::exists(activePath))
        return fail("An interrupted transaction requires rollback first; see " + activePath);
    QJsonObject savedBaseline;
    if (!savedPlan.isEmpty()) {
        savedBaseline = scan(root, error, progress);
        if (savedBaseline.isEmpty())
            return {};
        if (savedPlan != savedBaseline)
            return fail(
                "Saved plan is stale or differs from the current planner; generate a new plan");
    }

    const auto bootstrap = bootstrapPlan(root, error);
    if (!error.isEmpty())
        return {};
    const bool hasBootstrap = !bootstrap.actions.isEmpty();
    const int bootstrapActionCount = bootstrap.actions.size();
    QJsonArray actions = bootstrap.actions;
    QString directory;
    bool journalReady = false;

    auto createJournalDirectory = [&]() {
        const QFileInfo requested(journalDirectory), journalParent(requested.absolutePath());
        if (requested.exists() || requested.isSymLink() || requested.isJunction() ||
            journalParent.canonicalFilePath().isEmpty()) {
            error = "Journal directory must be new with an existing parent";
            return false;
        }
        directory = journalParent.canonicalFilePath() + '/' + requested.fileName();
        if (under(directory.toLower(), root.toLower())) {
            error = "Journal must be outside the game root";
            return false;
        }
        if (hasUpdate && under(directory.toCaseFolded(), updateRoot.toCaseFolded())) {
            error = "Journal must be outside the update root";
            return false;
        }
        if (QStorageInfo(root).device() != QStorageInfo(journalParent.canonicalFilePath()).device()) {
            error = "Journal must be on the game-root filesystem";
            return false;
        }
        if (!QDir().mkdir(directory) || !QDir().mkdir(directory + "/backup") ||
            !QDir().mkdir(directory + "/staged") ||
            !QDir().mkdir(directory + "/bootstrap-vault")) {
            error = "Cannot create transaction directory";
            return false;
        }
        journalReady = true;
        return true;
    };

    const QJsonObject bootstrapSummary{
        {"actions", bootstrapActionCount},
        {"directoriesRenamed", bootstrap.directoriesRenamed},
        {"directoryTreesMerged", bootstrap.directoryTreesMerged},
        {"uniqueEntriesMoved", bootstrap.uniqueEntriesMoved},
        {"identicalEntriesVaulted", bootstrap.identicalEntriesVaulted},
        {"conflicts", bootstrap.conflicts.size()}};

    auto abortTransaction = [&](const QString &reason) -> QJsonObject {
        lock.unlock();
        QString recovery;
        const bool restored =
            rollback(root, directory + "/journal.json", recovery, progress);
        error = reason +
                (restored ? "; applied changes rolled back"
                          : "; rollback needs attention: " + recovery) +
                "; journal: " + directory + "/journal.json";
        return {};
    };
    auto failOrAbort = [&](const QString &reason) -> QJsonObject {
        return journalReady ? abortTransaction(reason) : fail(reason);
    };

    if (hasBootstrap) {
        if (!createJournalDirectory())
            return {};
        const QJsonObject initialJournal{{"schema", hasUpdate
                                                       ? "tsre-content-case-journal-v2"
                                                       : "tsre-content-case-journal-v1"},
                                         {"gameRoot", root},
                                         {"actions", actions},
                                         {"originalLayout", bootstrap.originalLayout},
                                         {"bootstrap", bootstrapSummary},
                                         {"bootstrapConflicts", bootstrap.conflicts}};
        auto journal = initialJournal;
        if (hasUpdate)
            journal["updateRoot"] = updateRoot;
        if (!writeNew(directory + "/journal.json",
                      QJsonDocument(journal).toJson(QJsonDocument::Indented), error) ||
            !writeNew(directory + "/events.jsonl", {}, error) ||
            !writeNew(activePath,
                      QJsonDocument(QJsonObject{{"journal", directory + "/journal.json"}})
                          .toJson(),
                      error))
            return {};
        for (int i = 0; i < bootstrapActionCount; ++i) {
            const auto action = actions[i].toObject();
            if (progress)
                progress(QString("Bootstrap %1/%2: %3")
                             .arg(i + 1)
                             .arg(bootstrapActionCount)
                             .arg(action["from"].toString()));
            if (!applyJournalAction(root, directory, action, i, error))
                return abortTransaction(error);
        }
        const auto actualLayout = lightweightLayout(root, error);
        if (!error.isEmpty())
            return abortTransaction(error);
        if (actualLayout != bootstrap.expectedLayout)
            return abortTransaction("Game root changed during core-directory bootstrap");
    }

    OverlayPlan overlay;
    int overlayActionCount = 0;
    if (hasUpdate) {
        overlay = updateOverlayPlan(root, updateRoot, error);
        if (!error.isEmpty())
            return failOrAbort(error);
        if (overlay.hasActions()) {
            if (!journalReady) {
                if (!createJournalDirectory())
                    return {};
                QJsonObject initialJournal{{"schema", "tsre-content-case-journal-v2"},
                                           {"gameRoot", root},
                                           {"updateRoot", updateRoot},
                                           {"actions", actions},
                                           {"originalLayout", bootstrap.originalLayout},
                                           {"bootstrap", bootstrapSummary},
                                           {"bootstrapConflicts", bootstrap.conflicts},
                                           {"overlay", overlay.summary}};
                if (!writeNew(directory + "/journal.json",
                              QJsonDocument(initialJournal).toJson(QJsonDocument::Indented),
                              error) ||
                    !writeNew(directory + "/events.jsonl", {}, error) ||
                    !writeNew(activePath,
                              QJsonDocument(
                                  QJsonObject{{"journal", directory + "/journal.json"}})
                                  .toJson(),
                              error))
                    return {};
            }
            qint64 needed = 0;
            for (const auto &entry : overlay.files)
                if (!entry.targetExists || entry.sourceSha256 != entry.targetSha256) {
                    needed += QFileInfo(updateRoot + '/' + entry.source).size();
                    if (entry.targetExists)
                        needed += QFileInfo(root + '/' + entry.target).size();
                }
            if (QStorageInfo(root).bytesAvailable() < needed + 1024 * 1024)
                return abortTransaction("Insufficient space for update overlay staging");

            const int overlayStart = actions.size();
            for (const auto &entry : overlay.directories)
                if (!entry.targetExists)
                    actions.append(QJsonObject{{"kind", "make-directory"},
                                               {"from", entry.target},
                                               {"source", entry.source}});
            int stagedId = 0;
            for (const auto &entry : overlay.files) {
                if (entry.targetExists && entry.sourceSha256 == entry.targetSha256)
                    continue;
                const QString stage =
                    "staged/update-" + QString::number(++stagedId).rightJustified(8, '0');
                QFile source(updateRoot + '/' + entry.source);
                if (!source.open(QIODevice::ReadOnly))
                    return abortTransaction("Cannot read update file: " + entry.source);
                const auto updateBytes = source.readAll();
                if (hash(updateBytes) != entry.sourceSha256)
                    return abortTransaction("Update file changed during staging: " +
                                            entry.source);
                if (!writeNew(directory + '/' + stage, updateBytes, error))
                    return abortTransaction(error);
                if (!entry.targetExists) {
                    actions.append(QJsonObject{{"kind", "add-file"},
                                               {"from", entry.target},
                                               {"source", entry.source},
                                               {"staged", stage},
                                               {"afterSha256", entry.sourceSha256},
                                               {"permissions", int(entry.sourcePermissions)},
                                               {"modifiedMs", entry.sourceModifiedMs}});
                    continue;
                }
                const QString backup =
                    "backup/update-" + QString::number(stagedId).rightJustified(8, '0');
                QFile target(root + '/' + entry.target);
                if (!target.open(QIODevice::ReadOnly))
                    return abortTransaction("Cannot back up overlay target: " + entry.target);
                const auto targetBytes = target.readAll();
                if (hash(targetBytes) != entry.targetSha256)
                    return abortTransaction("Overlay target changed before backup: " +
                                            entry.target);
                if (!writeNew(directory + '/' + backup, targetBytes, error))
                    return abortTransaction(error);
                actions.append(QJsonObject{{"kind", "replace"},
                                           {"from", entry.target},
                                           {"source", entry.source},
                                           {"backup", backup},
                                           {"staged", stage},
                                           {"beforeSha256", entry.targetSha256},
                                           {"afterSha256", entry.sourceSha256},
                                           {"permissions", int(entry.targetPermissions)},
                                           {"modifiedMs", entry.targetModifiedMs},
                                           {"overlay", true}});
            }
            overlayActionCount = actions.size() - overlayStart;
            const auto rechecked = updateOverlayPlan(root, updateRoot, error);
            if (!error.isEmpty() || rechecked.fingerprint != overlay.fingerprint)
                return abortTransaction(error.isEmpty()
                                            ? "Game or update root changed while staging overlay"
                                            : error);
            QJsonObject journal{{"schema", "tsre-content-case-journal-v2"},
                                {"gameRoot", root},
                                {"updateRoot", updateRoot},
                                {"actions", actions},
                                {"originalLayout", bootstrap.originalLayout},
                                {"bootstrap", bootstrapSummary},
                                {"bootstrapConflicts", bootstrap.conflicts},
                                {"overlay", overlay.summary}};
            if (!writeJson(directory + "/journal.json", journal, error))
                return abortTransaction(error);
            int completedOverlayActions = 0;
            for (int i = overlayStart; i < actions.size(); ++i) {
                const auto action = actions[i].toObject();
                if (progress)
                    progress(QString("Overlay %1/%2: %3")
                                 .arg(i - overlayStart + 1)
                                 .arg(overlayActionCount)
                                 .arg(action["source"].toString(action["from"].toString())));
                if (!applyJournalAction(root, directory, action, i, error))
                    return abortTransaction(error);
                ++completedOverlayActions;
                if (continuation && !continuation(completedOverlayActions)) {
                    append(directory, {{"phase", "interrupted"}}, error);
                    error = "Execution interrupted; recover using " + directory +
                            "/journal.json";
                    return QJsonObject{{"gameRoot", root},
                                       {"updateRoot", updateRoot},
                                       {"journal", directory + "/journal.json"},
                                       {"status", "interrupted"},
                                       {"actionsApplied", bootstrapActionCount +
                                                              completedOverlayActions},
                                       {"bootstrap", bootstrapSummary},
                                       {"overlay", overlay.summary}};
                }
            }
            const auto installed = updateOverlayPlan(root, updateRoot, error);
            if (!error.isEmpty() || installed.hasActions() ||
                installed.files.size() != overlay.files.size() ||
                installed.directories.size() != overlay.directories.size())
                return abortTransaction(error.isEmpty()
                                            ? "Update overlay verification failed"
                                            : error);
        }
    }

    const int preparedActionCount = actions.size();
    const bool priorTransaction = hasBootstrap || overlayActionCount > 0;

    QJsonObject before;
    if (!journalReady && !savedBaseline.isEmpty())
        before = savedBaseline;
    else
        before = scan(root, error, progress);
    if (before.isEmpty())
        return journalReady ? abortTransaction(error.isEmpty() ? "Content scan failed" : error)
                            : QJsonObject();
    const auto files = before["files"].toArray();
    QSet<int> extra;
    Selection selection;
    QMap<int, QByteArray> replacements;
    QJsonArray writerErrors;
    QMap<int, QString> families;
    for (const auto &v : before["references"].toArray()) {
        const auto r = v.toObject();
        if (!r["sourceFamily"].toString().isEmpty())
            families[r["sourceFileId"].toInt()] = r["sourceFamily"].toString();
    }
    for (;;) {
        selection = select(before, extra);
        replacements.clear();
        bool again = false;
        for (auto it = selection.edits.cbegin(); it != selection.edits.cend(); ++it) {
            const auto f = files[it.key()].toObject();
            const QString path = f["path"].toString();
            QFile input(root + '/' + path);
            QString detail;
            QByteArray output;
            QString family = families.value(it.key(), QFileInfo(path).suffix().toLower());
            if (!QFileInfo(input).isWritable()) {
                detail = "Edit source is not writable";
            } else if (!input.open(QIODevice::ReadOnly)) {
                detail = "Cannot read edit source";
            } else {
                const auto bytes = input.readAll();
                if (hash(bytes) != f["sha256"].toString())
                    return priorTransaction
                               ? abortTransaction("Source changed during preparation: " + path)
                               : fail("Source changed during preparation: " + path);
                if (patchDocument(bytes, family, it.value(), output, detail)) {
                    if (output != bytes)
                        replacements[it.key()] = output;
                    continue;
                }
            }
            extra.insert(it.key());
            again = true;
            writerErrors.append(QJsonObject{{"path", path}, {"reason", detail}});
        }
        if (!again)
            break;
    }
    QJsonObject result{{"gameRoot", root},
                       {"coverageCertified", false},
                       {"skippedOperations", selection.skipped},
                       {"writerErrors", writerErrors},
                       {"preExistingErrors", before["summary"].toObject()["failedCases"]},
                       {"status", "no-changes"},
                       {"summary", before["summary"]},
                       {"remainingOperations", before["operations"].toArray().size()},
                       {"errors", before["failures"]},
                       {"missingTargets", before["missingTargets"]},
                       {"bootstrap", bootstrapSummary},
                       {"bootstrapConflicts", bootstrap.conflicts}};
    if (hasUpdate)
        result["overlay"] = overlay.summary;
    if (priorTransaction)
        result["journal"] = directory + "/journal.json";
    if (!priorTransaction && selection.leaves.isEmpty() && replacements.isEmpty())
        return result;
    if (!journalReady && !createJournalDirectory())
        return {};
    qint64 needed = 0;
    for (auto it = replacements.cbegin(); it != replacements.cend(); ++it)
        needed += it.value().size() + files[it.key()].toObject()["size"].toVariant().toLongLong();
    if (QStorageInfo(root).bytesAvailable() < needed + 1024 * 1024)
        return priorTransaction ? abortTransaction("Insufficient space for backups and staged edits")
                                : fail("Insufficient space for backups and staged edits");
    for (auto it = replacements.cbegin(); it != replacements.cend(); ++it) {
        const auto entry = files[it.key()].toObject();
        const QString name = entry["path"].toString(),
                      backup = "backup/" + QString::number(it.key()),
                      stage = "staged/" + QString::number(it.key());
        if (!safe(root, name, error))
            return priorTransaction ? abortTransaction(error) : QJsonObject();
        QFile input(root + '/' + name);
        if (!input.open(QIODevice::ReadOnly))
            return priorTransaction ? abortTransaction("Cannot back up " + name)
                                    : fail("Cannot back up " + name);
        const auto bytes = input.readAll();
        if (hash(bytes) != entry["sha256"].toString())
            return priorTransaction ? abortTransaction("Source changed before backup: " + name)
                                    : fail("Source changed before backup: " + name);
        if (!writeNew(directory + '/' + backup, bytes, error) ||
            !writeNew(directory + '/' + stage, it.value(), error))
            return priorTransaction ? abortTransaction(error) : QJsonObject();
        actions.append(QJsonObject{{"kind", "replace"},
                                   {"from", name},
                                   {"backup", backup},
                                   {"staged", stage},
                                   {"beforeSha256", hash(bytes)},
                                   {"afterSha256", hash(it.value())},
                                   {"permissions", int(QFileInfo(input).permissions())},
                                   {"modifiedMs", entry["modifiedMs"]}});
    }
    QVector<int> ids;
    for (auto it = selection.leaves.cbegin(); it != selection.leaves.cend(); ++it)
        ids << it.key();
    // File renames first, then deepest directories; every case-only move uses a
    // unique sibling hop, including on case-insensitive Windows filesystems.
    std::sort(ids.begin(), ids.end(), [&](int a, int b) {
        const auto x = files[a].toObject(), y = files[b].toObject();
        if (x["directory"] != y["directory"])
            return !x["directory"].toBool();
        return x["path"].toString().count('/') > y["path"].toString().count('/');
    });
    for (int id : ids) {
        const auto f = files[id].toObject();
        const QString from = f["path"].toString(), base = parent(from);
        const QString to = (base.isEmpty() ? QString() : base + '/') + selection.leaves.value(id);
        if (from.toLower() != to.toLower())
            return priorTransaction
                       ? abortTransaction("Only case repair renames are supported: " + from +
                                          " -> " + to)
                       : fail("Only case repair renames are supported: " + from + " -> " + to);
        const QString temporary = (base.isEmpty() ? QString() : base + '/') + ".tsre-case-" +
                                  QUuid::createUuid().toString(QUuid::Id128);
        const QString kind = f["directory"].toBool() ? "move-directory" : "move-file";
        QString expected;
        if (kind == "move-file") {
            const QFileInfo current(root + '/' + from);
            if (current.size() != f["size"].toVariant().toLongLong() ||
                current.lastModified().toMSecsSinceEpoch() !=
                    f["modifiedMs"].toVariant().toLongLong())
                return priorTransaction ? abortTransaction("Rename source changed: " + from)
                                        : fail("Rename source changed: " + from);
            expected = replacements.contains(id) ? hash(replacements.value(id))
                                                 : fileHash(root + '/' + from, error);
            if (expected.isEmpty())
                return priorTransaction ? abortTransaction(error) : QJsonObject();
        }
        actions.append(QJsonObject{
            {"kind", kind}, {"from", from}, {"to", temporary}, {"expectedSha256", expected}});
        actions.append(QJsonObject{
            {"kind", kind}, {"from", temporary}, {"to", to}, {"expectedSha256", expected}});
    }
    // Detect inventory changes after staging, including added files. Hashes are
    // rechecked by scan as well. A bootstrap-only run already did this check.
    if (actions.size() > preparedActionCount) {
        const auto rechecked = scan(root, error, progress);
        if (rechecked.isEmpty())
            return priorTransaction
                       ? abortTransaction(error.isEmpty() ? "Content rescan failed" : error)
                       : QJsonObject();
        if (rechecked != before)
            return priorTransaction
                       ? abortTransaction("Game root changed while staging; no content repairs "
                                          "were applied")
                       : fail("Game root changed while staging; no repairs were applied");
    }
    QJsonArray originalLayout = bootstrap.originalLayout;
    QJsonObject journal{{"schema", hasUpdate ? "tsre-content-case-journal-v2"
                                              : "tsre-content-case-journal-v1"},
                        {"gameRoot", root},
                        {"actions", actions},
                        {"originalLayout", originalLayout},
                        {"inventorySha256", before["inventorySha256"]},
                        {"inspectedContentSha256", before["inspectedContentSha256"]},
                        {"bootstrap", bootstrapSummary},
                        {"bootstrapConflicts", bootstrap.conflicts}};
    if (hasUpdate) {
        journal["updateRoot"] = updateRoot;
        journal["overlay"] = overlay.summary;
    }
    if (priorTransaction) {
        if (!writeJson(directory + "/journal.json", journal, error))
            return abortTransaction(error);
    } else {
        if (!writeNew(directory + "/journal.json",
                      QJsonDocument(journal).toJson(QJsonDocument::Indented), error) ||
            !writeNew(directory + "/events.jsonl", {}, error) ||
            !writeNew(activePath,
                      QJsonDocument(QJsonObject{{"journal", directory + "/journal.json"}})
                          .toJson(),
                      error))
            return {};
    }
    result["journal"] = directory + "/journal.json";
    int completedContentActions = 0;
    for (int i = preparedActionCount; i < actions.size(); ++i) {
        const auto a = actions[i].toObject();
        const QString from = a["from"].toString();
        if (progress)
            progress(QString("Apply %1/%2: %3")
                         .arg(i - preparedActionCount + 1)
                         .arg(actions.size() - preparedActionCount)
                         .arg(from));
        if (!applyJournalAction(root, directory, a, i, error))
            return abortTransaction(error);
        ++completedContentActions;
        if (continuation && !continuation(completedContentActions)) {
            append(directory, {{"phase", "interrupted"}}, error);
            error = "Execution interrupted; recover using " + directory + "/journal.json";
            result["status"] = "interrupted";
            result["actionsApplied"] = preparedActionCount + completedContentActions;
            return result;
        }
    }
    const auto after = actions.size() == preparedActionCount ? before
                                                               : scan(root, error, progress);
    QString verificationError;
    if (after.isEmpty()) {
        verificationError = error.isEmpty() ? "Post-repair scan failed" : error;
    } else if (!verifyReferences(before, after, selection.paths, error)) {
        verificationError = error;
    }
    if (verificationError.isEmpty()) {
        QMap<QString, QJsonObject> actualFiles;
        for (const auto &v : after["files"].toArray())
            actualFiles[v.toObject()["path"].toString()] = v.toObject();
        if (actualFiles.size() != files.size()) {
            verificationError = "Directory inventory changed during execution";
        } else {
            for (int i = 0; i < files.size(); ++i) {
                const auto original = files[i].toObject();
                const QString path = mapped(selection.paths, original["path"].toString());
                const auto actual = actualFiles.value(path);
                if (actual.isEmpty() || actual["directory"] != original["directory"] ||
                    actual["link"] != original["link"]) {
                    verificationError = "Unexpected final inventory entry: " + path;
                    break;
                }
                if (!original["directory"].toBool() && !original["link"].toBool()) {
                    const QString expected = replacements.contains(i)
                                                 ? hash(replacements.value(i))
                                                 : original["sha256"].toString();
                    QString hashError;
                    if (!expected.isEmpty() && fileHash(root + '/' + path, hashError) != expected) {
                        verificationError = hashError.isEmpty()
                                                ? "File bytes changed unexpectedly: " + path
                                                : hashError;
                        break;
                    }
                    if (!replacements.contains(i) &&
                        (actual["size"] != original["size"] ||
                         actual["modifiedMs"] != original["modifiedMs"])) {
                        verificationError = "File metadata changed unexpectedly: " + path;
                        break;
                    }
                }
            }
        }
    }
    const bool verificationPassed = verificationError.isEmpty();
    result["status"] = verificationPassed && selection.skipped.isEmpty() &&
                               writerErrors.isEmpty() &&
                               bootstrap.conflicts.isEmpty() &&
                               after["summary"].toObject()["failedCases"].toInt() == 0 &&
                               after["operations"].toArray().isEmpty()
                           ? "complete"
                           : "partial";
    result["actionsApplied"] = actions.size();
    if (!after.isEmpty()) {
        result["summary"] = after["summary"];
        result["errors"] = after["failures"];
        result["missingTargets"] = after["missingTargets"];
        result["remainingOperations"] = after["operations"].toArray().size();
    }
    if (!verificationPassed)
        result["verificationErrors"] = QJsonArray{verificationError};
    QJsonObject completion{{"phase", "verified"}, {"status", result["status"]}};
    if (!verificationPassed)
        completion["error"] = verificationError;
    if (!append(directory, completion, error)) {
        result["status"] = "interrupted";
        error = "All repair actions completed, but the final journal event failed: " + error +
                "; recover using " + directory + "/journal.json";
        return result;
    }
    if (!writeNew(directory + "/result.json", QJsonDocument(result).toJson(QJsonDocument::Indented),
                  error)) {
        result["status"] = "interrupted";
        error = "All repair actions completed, but the result could not be stored: " + error +
                "; recover using " + directory + "/journal.json";
        return result;
    }
    if (!QFile::remove(activePath))
        return fail("Repair completed, but active journal marker could not be cleared: " +
                    activePath);
    error.clear();
    return result;
}

QJsonObject execute(const QString &root, const QJsonObject &savedPlan,
                    const QString &journalDirectory, QString &error,
                    const std::function<void(const QString &)> &progress,
                    const std::function<bool(int)> &continuation) {
    return executeImpl(root, savedPlan, journalDirectory, {}, error, progress, continuation);
}

QJsonObject executeUpdate(const QString &root, const QString &updateRoot,
                          const QString &journalDirectory, QString &error,
                          const std::function<void(const QString &)> &progress,
                          const std::function<bool(int)> &continuation) {
    return executeImpl(root, {}, journalDirectory, updateRoot, error, progress, continuation);
}
} // namespace ContentCase
