#include "ContentCaseExecution.h"
#include "ContentCaseDocument.h"
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
bool move(const QString &root, const QString &from, const QString &to, QString &error) {
    if (!safe(root, from, error) || !safe(root, to, error))
        return false;
    if (!exact(root, from) || QFileInfo::exists(root + '/' + to)) {
        error = "Rename source/destination changed: " + from + " -> " + to;
        return false;
    }
    std::error_code ec;
#ifdef Q_OS_WIN
    const auto source = (root + '/' + from).toStdWString(),
               destination = (root + '/' + to).toStdWString();
    if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
        ec = std::error_code(GetLastError(), std::system_category());
#elif defined(Q_OS_LINUX)
    const auto source = QFile::encodeName(root + '/' + from),
               destination = QFile::encodeName(root + '/' + to);
    if (syscall(SYS_renameat2, AT_FDCWD, source.constData(), AT_FDCWD, destination.constData(),
                RENAME_NOREPLACE) != 0)
        ec = std::error_code(errno, std::generic_category());
#elif defined(Q_OS_MACOS)
    if (renamex_np(QFile::encodeName(root + '/' + from).constData(),
                   QFile::encodeName(root + '/' + to).constData(), RENAME_EXCL) != 0)
        ec = std::error_code(errno, std::generic_category());
#else
    ec = std::make_error_code(std::errc::operation_not_supported);
#endif
    if (ec) {
        error =
            "Rename failed: " + from + " -> " + to + ": " + QString::fromStdString(ec.message());
        return false;
    }
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
    if (!QFile::setPermissions(path, QFile::Permissions(action["permissions"].toInt()))) {
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
    ticks.QuadPart =
        quint64(action["modifiedMs"].toVariant().toLongLong()) * 10000ULL + 116444736000000000ULL;
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
        !metadata.setFileTime(
            QDateTime::fromMSecsSinceEpoch(action["modifiedMs"].toVariant().toLongLong()),
            QFileDevice::FileModificationTime)) {
        error = "Cannot restore modification time: " + path;
        return false;
    }
#endif
    return true;
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
    return source + '|' + QString::number(ref["fieldIndex"].toInt(-1)) + '|' +
           QString::number(ref["scalarIndex"].toInt()) + '|' + ref["kind"].toString() + '|' +
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
    QVector<int> sets(files.size()), weights(files.size(), 1);
    for (int i = 0; i < sets.size(); ++i)
        sets[i] = i;
    auto find = [&](int a) {
        int root = a;
        while (sets[root] != root)
            root = sets[root];
        while (sets[a] != a) {
            const int next = sets[a];
            sets[a] = root;
            a = next;
        }
        return root;
    };
    auto unite = [&](int a, int b) {
        if (a >= 0 && b >= 0 && a < sets.size() && b < sets.size()) {
            a = find(a);
            b = find(b);
            if (a == b)
                return;
            if (weights[a] < weights[b])
                std::swap(a, b);
            sets[b] = a;
            weights[a] += weights[b];
        }
    };
    for (const auto &v : refs) {
        const auto r = v.toObject();
        unite(r["sourceFileId"].toInt(-1), r["targetFileId"].toInt(-1));
    }
    for (const auto &v : plan["textureNamingGroups"].toArray()) {
        const auto ids = v.toObject()["fileIds"].toArray();
        for (int i = 1; i < ids.size(); ++i)
            unite(ids[0].toInt(), ids[i].toInt());
    }
    Selection result;
    QSet<int> blocked = extra;
    for (const auto &v : plan["failures"].toArray())
        if (v.toObject()["sourceFileId"].toInt(-1) >= 0)
            blocked.insert(v.toObject()["sourceFileId"].toInt());
    for (int i = 0; i < files.size(); ++i) {
        const auto f = files[i].toObject();
        if (f["link"].toBool() || f["coverage"] == "unclassified" ||
            f["coverage"].toString().startsWith("excluded"))
            blocked.insert(i);
    }
    QMap<int, QStringList> sourceBases;
    for (const auto &v : refs) {
        const auto r = v.toObject();
        for (const auto &base : r["searchBases"].toArray()) {
            auto &list = sourceBases[r["sourceFileId"].toInt()];
            if (!list.contains(base.toString()))
                list << base.toString();
        }
    }
    // Incomplete discovery can hide inbound edges into default asset scopes.
    // Freeze those scopes as well as the known connected component. A corrupt S
    // never licenses renaming its undiscovered texture names.
    QSet<QString> protectedScopes;
    bool protectSharedTextures = false;
    for (const auto &v : plan["failures"].toArray()) {
        const auto failure = v.toObject();
        const int id = failure["sourceFileId"].toInt(-1);
        if (id < 0 || (failure["code"] != "source-read-failed" &&
                       failure["code"] != "reference-discovery-incomplete" &&
                       failure["code"] != "unclassified-reference-field" &&
                       failure["code"] != "context-unbound"))
            continue;
        const QString path = files[id].toObject()["path"].toString(), lower = path.toLower();
        QString scope = parent(path);
        if (lower.startsWith("routes/"))
            scope = path.section('/', 0, 1);
        else if (lower.startsWith("trains/trainset/"))
            scope = path.section('/', 0, 2);
        const bool shared = lower.endsWith(".s") &&
                            (lower.startsWith("global/") ||
                             (!lower.startsWith("routes/") && !lower.startsWith("trains/")));
        protectedScopes.insert(scope.toLower());
        protectSharedTextures |= shared;
        for (const auto &base : sourceBases.value(id))
            protectedScopes.insert(clean(base).toLower());
    }
    for (int i = 0; i < files.size(); ++i) {
        const QString path = files[i].toObject()["path"].toString().toLower();
        if (protectSharedTextures && (path.contains("/textures/") || path.contains("/terrtex/")))
            blocked.insert(i);
        QString at = path;
        while (!at.isEmpty()) {
            if (protectedScopes.contains(at)) {
                blocked.insert(i);
                break;
            }
            at = parent(at);
        }
    }
    QSet<int> roots;
    for (int id : blocked)
        roots.insert(find(id));
    for (int i = 0; i < files.size(); ++i)
        if (roots.contains(find(i)))
            result.blocked.insert(i);
    QSet<QString> protectedAncestors;
    for (int id : result.blocked) {
        QString at = files[id].toObject()["path"].toString();
        while (!at.isEmpty()) {
            protectedAncestors.insert(at);
            at = parent(at);
        }
    }
    for (const auto &v : plan["operations"].toArray()) {
        const auto op = v.toObject();
        if (op["operation"] == "edit-reference") {
            if (result.blocked.contains(op["sourceFileId"].toInt(-1))) {
                auto entry = op;
                entry["executionReason"] = "Unresolved source reference component";
                result.skipped.append(entry);
            }
            continue;
        }
        const int id = op["fileId"].toInt(-1);
        if (id < 0)
            continue;
        bool skip = result.blocked.contains(id) || op["readiness"].toString().startsWith("blocked");
        if (op["operation"] == "rename-directory")
            for (int blockedId : result.blocked)
                skip |=
                    under(files[blockedId].toObject()["path"].toString(), op["from"].toString());
        if (skip) {
            auto entry = op;
            entry["executionReason"] =
                "Unresolved reference component, unknown source, link, or protected descendant";
            result.skipped.append(entry);
        } else
            result.leaves[id] = leaf(op["to"].toString());
    }
    // Inventory is parent-before-child; map ancestors even when a parent rename
    // was withheld, so independent leaf repairs use the actual selected paths.
    QMap<QString, int> spellings;
    for (const auto &v : files)
        ++spellings[v.toObject()["path"].toString().toLower()];
    for (int i = 0; i < files.size(); ++i) {
        const QString from = files[i].toObject()["path"].toString();
        const QString base = parent(from),
                      to = clean((base.isEmpty() ? QString() : mapped(result.paths, base) + '/') +
                                 result.leaves.value(i, leaf(from)));
        result.paths[from] = to;
        if (spellings.value(from.toLower()) == 1)
            result.paths[QChar(0) + from.toLower()] = to;
    }
    for (const auto &v : refs) {
        const auto r = v.toObject();
        const int s = r["sourceFileId"].toInt(-1), t = r["targetFileId"].toInt(-1);
        if (s < 0 || t < 0 || result.blocked.contains(s) || result.blocked.contains(t) ||
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
} // namespace

bool verifyReferences(const QJsonObject &before, const QJsonObject &after,
                      const QMap<QString, QString> &paths, QString &error) {
    const auto oldFiles = before["files"].toArray(), newFiles = after["files"].toArray();
    QMap<QString, QJsonObject> refs;
    for (const auto &v : after["references"].toArray()) {
        const auto r = v.toObject();
        const int s = r["sourceFileId"].toInt();
        refs[refKey(r, newFiles[s].toObject()["path"].toString(), {})] = r;
    }
    for (const auto &v : before["references"].toArray()) {
        const auto r = v.toObject();
        const int target = r["targetFileId"].toInt(-1);
        if (target < 0)
            continue;
        const QString source =
            mapped(paths, oldFiles[r["sourceFileId"].toInt()].toObject()["path"].toString());
        const auto actual = refs.value(refKey(r, source, paths));
        const int actualTarget = actual["targetFileId"].toInt(-1);
        const QString expected = mapped(paths, oldFiles[target].toObject()["path"].toString());
        if (actual.isEmpty() || actualTarget < 0 ||
            newFiles[actualTarget].toObject()["path"] != expected) {
            error = "Verification changed a reference target: " + source + " / " +
                    r["location"].toString() + " expected " + expected;
            return false;
        }
        if (r["status"] == "exact" && actual["status"] != "exact") {
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
    if (journal["schema"] != "tsre-content-case-journal-v1" || journal["gameRoot"] != root) {
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
        if (!safe(root, from, error))
            return false;
        if (progress)
            progress(QString("Rollback %1/%2: %3")
                         .arg(actions.size() - i)
                         .arg(actions.size())
                         .arg(from));
        if (kind == "replace") {
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

QJsonObject execute(const QString &rootInput, const QJsonObject &savedPlan,
                    const QString &journalDirectory, QString &error,
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
    QLockFile lock(registry(root) + ".lock");
    if (!lock.tryLock(0))
        return fail("Another converter is using this root");
    const QString activePath = registry(root) + ".json";
    if (QFileInfo::exists(activePath))
        return fail("An interrupted transaction requires rollback first; see " + activePath);
    const auto before = scan(root, error, progress);
    if (before.isEmpty())
        return {};
    if (!savedPlan.isEmpty() && savedPlan != before)
        return fail("Saved plan is stale or differs from the current planner; generate a new plan");
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
            if (!input.open(QIODevice::ReadOnly)) {
                detail = "Cannot read edit source";
            } else {
                const auto bytes = input.readAll();
                if (hash(bytes) != f["sha256"].toString())
                    return fail("Source changed during preparation: " + path);
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
                       {"missingTargets", before["missingTargets"]}};
    if (selection.leaves.isEmpty() && replacements.isEmpty())
        return result;
    const QFileInfo requested(journalDirectory), journalParent(requested.absolutePath());
    if (requested.exists() || requested.isSymLink() || requested.isJunction() ||
        journalParent.canonicalFilePath().isEmpty())
        return fail("Journal directory must be new with an existing parent");
    const QString directory = journalParent.canonicalFilePath() + '/' + requested.fileName();
    if (under(directory.toLower(), root.toLower()))
        return fail("Journal must be outside the game root");
    if (QStorageInfo(root).device() != QStorageInfo(journalParent.canonicalFilePath()).device())
        return fail("Journal must be on the game-root filesystem");
    qint64 needed = 0;
    for (auto it = replacements.cbegin(); it != replacements.cend(); ++it)
        needed += it.value().size() + files[it.key()].toObject()["size"].toVariant().toLongLong();
    if (QStorageInfo(root).bytesAvailable() < needed + 1024 * 1024)
        return fail("Insufficient space for backups and staged edits");
    if (!QDir().mkdir(directory) || !QDir().mkdir(directory + "/backup") ||
        !QDir().mkdir(directory + "/staged"))
        return fail("Cannot create transaction directory");
    QJsonArray actions;
    for (auto it = replacements.cbegin(); it != replacements.cend(); ++it) {
        const auto entry = files[it.key()].toObject();
        const QString name = entry["path"].toString(),
                      backup = "backup/" + QString::number(it.key()),
                      stage = "staged/" + QString::number(it.key());
        if (!safe(root, name, error))
            return {};
        QFile input(root + '/' + name);
        if (!input.open(QIODevice::ReadOnly))
            return fail("Cannot back up " + name);
        const auto bytes = input.readAll();
        if (hash(bytes) != entry["sha256"].toString())
            return fail("Source changed before backup: " + name);
        if (!writeNew(directory + '/' + backup, bytes, error) ||
            !writeNew(directory + '/' + stage, it.value(), error))
            return {};
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
            return fail("Only case repair renames are supported: " + from + " -> " + to);
        const QString temporary = (base.isEmpty() ? QString() : base + '/') + ".tsre-case-" +
                                  QUuid::createUuid().toString(QUuid::Id128);
        const QString kind = f["directory"].toBool() ? "move-directory" : "move-file";
        QString expected;
        if (kind == "move-file") {
            const QFileInfo current(root + '/' + from);
            if (current.size() != f["size"].toVariant().toLongLong() ||
                current.lastModified().toMSecsSinceEpoch() !=
                    f["modifiedMs"].toVariant().toLongLong())
                return fail("Rename source changed: " + from);
            expected = replacements.contains(id) ? hash(replacements.value(id))
                                                 : fileHash(root + '/' + from, error);
            if (expected.isEmpty())
                return {};
        }
        actions.append(QJsonObject{
            {"kind", kind}, {"from", from}, {"to", temporary}, {"expectedSha256", expected}});
        actions.append(QJsonObject{
            {"kind", kind}, {"from", temporary}, {"to", to}, {"expectedSha256", expected}});
    }
    // Detect inventory changes after staging, including added files. No game
    // content has been changed yet. Hashes are rechecked by scan as well.
    const auto rechecked = scan(root, error, progress);
    if (rechecked.isEmpty())
        return {};
    if (rechecked != before)
        return fail("Game root changed while staging; no repairs were applied");
    QJsonArray originalLayout;
    for (const auto &v : files) {
        const auto f = v.toObject();
        originalLayout.append(
            QJsonObject{{"path", f["path"]}, {"directory", f["directory"]}, {"link", f["link"]}});
    }
    QJsonObject journal{{"schema", "tsre-content-case-journal-v1"},
                        {"gameRoot", root},
                        {"actions", actions},
                        {"originalLayout", originalLayout},
                        {"inventorySha256", before["inventorySha256"]},
                        {"inspectedContentSha256", before["inspectedContentSha256"]}};
    if (!writeNew(directory + "/journal.json",
                  QJsonDocument(journal).toJson(QJsonDocument::Indented), error) ||
        !writeNew(directory + "/events.jsonl", {}, error))
        return {};
    if (!writeNew(activePath,
                  QJsonDocument(QJsonObject{{"journal", directory + "/journal.json"}}).toJson(),
                  error))
        return {};
    result["journal"] = directory + "/journal.json";
    auto abort = [&](QString reason) -> QJsonObject {
        lock.unlock();
        QString recovery;
        const bool restored = rollback(root, directory + "/journal.json", recovery, progress);
        error = reason +
                (restored ? "; applied changes rolled back"
                          : "; rollback needs attention: " + recovery) +
                "; journal: " + directory + "/journal.json";
        return {};
    };
    for (int i = 0; i < actions.size(); ++i) {
        const auto a = actions[i].toObject();
        const QString from = a["from"].toString(), kind = a["kind"].toString();
        if (progress)
            progress(QString("Apply %1/%2: %3").arg(i + 1).arg(actions.size()).arg(from));
        if (!safe(root, from, error) || !exact(root, from))
            return abort(error.isEmpty() ? "Source disappeared: " + from : error);
        const QString digest = kind == "move-directory" ? treeHash(root + '/' + from, error)
                                                        : fileHash(root + '/' + from, error);
        if (digest.isEmpty())
            return abort(error);
        if (kind == "replace" && digest != a["beforeSha256"].toString())
            return abort("Edit source changed: " + from);
        if (kind == "move-file" && digest != a["expectedSha256"].toString())
            return abort("Rename source changed: " + from);
        if (!append(directory, {{"step", i}, {"phase", "started"}, {"digest", digest}}, error))
            return abort(error);
        if (kind == "replace") {
            QFile f(directory + '/' + a["staged"].toString());
            if (!f.open(QIODevice::ReadOnly))
                return abort("Missing staged replacement");
            const auto bytes = f.readAll();
            if (hash(bytes) != a["afterSha256"] || !replace(root + '/' + from, bytes, a, error))
                return abort(error.isEmpty() ? "Staged replacement hash mismatch" : error);
        } else if (!move(root, from, a["to"].toString(), error))
            return abort(error);
        if (!append(directory, {{"step", i}, {"phase", "done"}}, error))
            return abort(error);
        if (continuation && !continuation(i + 1)) {
            append(directory, {{"phase", "interrupted"}}, error);
            error = "Execution interrupted; recover using " + directory + "/journal.json";
            result["status"] = "interrupted";
            return result;
        }
    }
    const auto after = scan(root, error, progress);
    if (after.isEmpty() || !verifyReferences(before, after, selection.paths, error))
        return abort(error);
    QMap<QString, QJsonObject> actualFiles;
    for (const auto &v : after["files"].toArray())
        actualFiles[v.toObject()["path"].toString()] = v.toObject();
    if (actualFiles.size() != files.size())
        return abort("Directory inventory changed during execution");
    for (int i = 0; i < files.size(); ++i) {
        const auto original = files[i].toObject();
        const QString path = mapped(selection.paths, original["path"].toString());
        const auto actual = actualFiles.value(path);
        if (actual.isEmpty() || actual["directory"] != original["directory"] ||
            actual["link"] != original["link"])
            return abort("Unexpected final inventory entry: " + path);
        if (!original["directory"].toBool() && !original["link"].toBool()) {
            const QString expected = replacements.contains(i) ? hash(replacements.value(i))
                                                              : original["sha256"].toString();
            if (!expected.isEmpty() && fileHash(root + '/' + path, error) != expected)
                return abort("File bytes changed unexpectedly: " + path);
            if (!replacements.contains(i) && (actual["size"] != original["size"] ||
                                              actual["modifiedMs"] != original["modifiedMs"]))
                return abort("File metadata changed unexpectedly: " + path);
        }
    }
    result["status"] = selection.skipped.isEmpty() && writerErrors.isEmpty() &&
                               after["summary"].toObject()["failedCases"].toInt() == 0 &&
                               after["operations"].toArray().isEmpty()
                           ? "complete"
                           : "partial";
    result["actionsApplied"] = actions.size();
    result["summary"] = after["summary"];
    result["errors"] = after["failures"];
    result["missingTargets"] = after["missingTargets"];
    result["remainingOperations"] = after["operations"].toArray().size();
    if (!append(directory, {{"phase", "verified"}, {"status", result["status"]}}, error))
        return abort(error);
    if (!writeNew(directory + "/result.json", QJsonDocument(result).toJson(QJsonDocument::Indented),
                  error))
        return abort(error);
    if (!QFile::remove(activePath))
        return fail("Repair verified, but active journal marker could not be cleared: " +
                    activePath);
    return result;
}
} // namespace ContentCase
