#include "SimisDocument.h"
#include "SimisTextReader.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSet>
#include <algorithm>
#include <tsre/units/UnitConversion.h>

namespace {
// Includes nested deeper than this are not read (a loop through roots).
constexpr int IncludeDepth = 16;
#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity PathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity PathCase = Qt::CaseSensitive;
#endif

QString absolute(const QString &path) {
    return QDir::cleanPath(QFileInfo(QString(path).replace(u'\\', u'/')).absoluteFilePath());
}

// Each file's tree, shared while anything holds it, read again when the file
// changed (its time or size).
struct CachedTree {
    QDateTime modified;
    qint64 size = 0;
    std::weak_ptr<const SimisTree> tree;
};
QMutex cacheMutex;
QHash<QString, CachedTree> cache;

std::shared_ptr<const SimisTree> fileTree(const QString &path, QString &error) {
    const QFileInfo info(path);
    if (!info.isFile()) {
        error = "not found";
        return {};
    }
    const QDateTime modified = info.lastModified();
    const qint64 size = info.size();
    {
        QMutexLocker lock(&cacheMutex);
        const auto at = cache.constFind(path);
        if (at != cache.cend() && at->modified == modified && at->size == size)
            if (auto tree = at->tree.lock())
                return tree;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return {};
    }
    QString text, note;
    if (!SimisTextReader::decode(file.readAll(), text, error, &note))
        return {};
    auto tree = SimisTree::read(text, path, {}, note);
    QMutexLocker lock(&cacheMutex);
    cache.insert(path, {modified, size, tree});
    return tree;
}
} // namespace

struct SimisDocumentReader {
    SimisDocument &doc;
    const SimisDocument::Roots &roots;
    QSet<const SimisTree *> reported;

    void report(const SimisTree *tree) {
        if (reported.contains(tree))
            return;
        reported.insert(tree);
        for (const auto &p : tree->problems())
            doc.found.push_back({tree->files().value(p.file), p.line, p.message});
    }
    bool unbalanced(const QString &target) {
        QString error;
        const auto tree = fileTree(target, error);
        return tree && !tree->balanced();
    }
    // A file's tree with its includes linked; `chain` the files including it.
    std::shared_ptr<const SimisTree> open(const QString &path, QStringList &chain, QString &error) {
        auto tree = fileTree(path, error);
        if (!tree)
            return {};
        if (!doc.paths.contains(path))
            doc.paths << path;
        // An include reading a file whose parentheses do not balance: that
        // file's items are read in place of the include (Open Rails reads
        // every include so), giving this file a tree of its own.
        bool inlining = false;
        for (int i = 0; i < tree->size() && !inlining; ++i) {
            if (tree->node(i).kind != SimisTree::Kind::Include)
                continue;
            const QString target = SimisDocument::resolve(tree->text(i), path, roots);
            inlining = !target.isEmpty() && !chain.contains(target, PathCase) && unbalanced(target);
        }
        if (inlining) {
            QString note;
            for (const auto &p : tree->problems())
                if (p.file == 0 && p.line == 0)
                    note = p.message;
            QStringList holders{path};
            tree = SimisTree::read(
                    tree->whole(), path,
                    [&](QStringView written, int file, SimisTree::Inlined &out) {
                        const QString holder = holders.value(file);
                        const QString target = SimisDocument::resolve(written, holder, roots);
                        if (target.isEmpty() || chain.contains(target, PathCase)
                            || holders.contains(target, PathCase) || !unbalanced(target))
                            return false;
                        QString unused;
                        out.text = fileTree(target, unused)->whole();
                        out.path = target;
                        holders << target;
                        doc.found.push_back(
                                {holder, 0,
                                 QString("Includes %1, whose parentheses do not balance: read in its place, as Open "
                                         "Rails reads it")
                                         .arg(target)});
                        return true;
                    },
                    note);
            for (int f = 1; f < tree->files().size(); ++f)
                if (!doc.paths.contains(tree->files()[f]))
                    doc.paths << tree->files()[f];
        }
        doc.kept << tree;
        report(tree.get());
        for (int i = 0; i < tree->size(); ++i) {
            if (tree->node(i).kind != SimisTree::Kind::Include)
                continue;
            const QString holder = tree->files().value(tree->file(i));
            QString problem;
            const QString target = SimisDocument::resolve(tree->text(i), holder, roots, &problem);
            if (target.isEmpty()) {
                doc.found.push_back({holder, tree->line(i), problem});
                continue;
            }
            if (chain.contains(target, PathCase)) {
                doc.found.push_back({holder, tree->line(i), QString("Includes %1, which includes it").arg(target)});
                continue;
            }
            if (chain.size() > IncludeDepth) {
                doc.found.push_back({holder, tree->line(i), QString("Includes nested too deep at %1").arg(target)});
                continue;
            }
            chain << target;
            QString includeError;
            const auto included = open(target, chain, includeError);
            chain.removeLast();
            if (!included) {
                doc.found.push_back({holder, tree->line(i), QString("Include %1: %2").arg(target, includeError)});
                continue;
            }
            doc.links.insert({tree.get(), i}, included.get());
        }
        return tree;
    }
};

std::shared_ptr<const SimisDocument> SimisDocument::read(const QString &path, const Roots &roots) {
    auto doc = std::make_shared<SimisDocument>();
    SimisDocumentReader reader{*doc, roots, {}};
    const QString file = absolute(path);
    QStringList chain{file};
    QString error;
    doc->tree = reader.open(file, chain, error);
    if (!doc->tree)
        doc->found.push_back({file, 0, error});
    return doc;
}

QString SimisDocument::resolve(QStringView written, const QString &holder, const Roots &roots, QString *problem) {
    const auto fail = [&](const QString &message) {
        if (problem)
            *problem = message;
        return QString();
    };
    QString path = written.trimmed().toString();
    path.replace(u'\\', u'/');
    const QFileInfo holding(holder);
    // Open Rails' one special name: the file holding the include.
    const qsizetype slash = path.lastIndexOf(u'/');
    if (QStringView(path).mid(slash + 1).compare(u"[[SameName]]", Qt::CaseInsensitive) == 0)
        path = path.left(slash + 1) + holding.fileName();
    if (path.isEmpty())
        return fail("An empty path");
    const QString game = roots.game.isEmpty() ? QString() : absolute(roots.game);
    QString base = holding.absolutePath();
    if (path.startsWith(u"[[")) {
        const qsizetype end = path.indexOf(u"]]");
        const QString root = end < 0 ? QString() : path.mid(2, end - 2).toLower();
        static const QHash<QString, QString> inGame{{"game", ""},
                                                    {"trains", "/TRAINS"},
                                                    {"trainset", "/TRAINS/TRAINSET"},
                                                    {"global", "/GLOBAL"},
                                                    {"sound", "/SOUND"}};
        if (root == "here") {
        } else if (root == "vehicle") {
            if (roots.vehicle.isEmpty())
                return fail(QString("%1: no vehicle's folder here").arg(written));
            base = absolute(roots.vehicle);
        } else if (inGame.contains(root)) {
            if (game.isEmpty())
                return fail(QString("%1: no game folder here").arg(written));
            base = game + inGame.value(root);
        } else {
            return fail(QString("%1: an unknown root (Game, Trains, Trainset, Global, Sound, Vehicle, Here)")
                                .arg(written));
        }
        path = path.mid(end + 2);
    } else if (path.startsWith(u'/') || (path.size() > 1 && path[1] == u':')) {
        return fail(QString("%1: an absolute path (a root or a relative path is needed)").arg(written));
    }
    const QString result = QDir::cleanPath(base + u'/' + path);
    if (!game.isEmpty() && result.compare(game, PathCase) != 0 && !result.startsWith(game + u'/', PathCase))
        return fail(QString("%1: leaves the game folder").arg(written));
    return result;
}

void SimisDocument::collect(const SimisTree *from, int first, QVector<Item> &out, int depth) const {
    for (int i = first; i >= 0; i = from->node(i).next) {
        if (from->node(i).kind == SimisTree::Kind::Include) {
            const SimisTree *included = links.value({from, i});
            if (included && depth < IncludeDepth)
                collect(included, included->first(), out, depth + 1);
            continue;
        }
        out.push_back({from, i});
    }
}

QVector<SimisDocument::Item> SimisDocument::items() const {
    QVector<Item> out;
    if (tree)
        collect(tree.get(), tree->first(), out, 0);
    return out;
}

QVector<SimisDocument::Item> SimisDocument::items(Item block) const {
    QVector<Item> out;
    if (isBlock(block))
        collect(block.tree, block.tree->node(block.node).child, out, 0);
    return out;
}

QVector<SimisDocument::Item> SimisDocument::values(Item block) const {
    QVector<Item> out;
    for (const Item &item : items(block))
        if (item.tree->node(item.node).kind == SimisTree::Kind::Value)
            out.push_back(item);
    return out;
}

QVector<SimisDocument::Item> SimisDocument::all(QAnyStringView path) const {
    const QString written = path.toString();
    const auto names = QStringView(written).split(u'(', Qt::SkipEmptyParts);
    if (names.isEmpty())
        return {};
    QVector<Item> level;
    for (int k = 0; k < names.size(); ++k) {
        QVector<Item> next;
        const auto take = [&](const QVector<Item> &list) {
            for (const Item &item : list)
                if (isBlock(item) && is(item, names[k].trimmed()))
                    next.push_back(item);
        };
        if (k == 0)
            take(items());
        else
            for (const Item &parent : level)
                take(items(parent));
        level = next;
    }
    return level;
}

SimisDocument::Item SimisDocument::last(QAnyStringView path) const {
    const auto list = all(path);
    return list.isEmpty() ? Item() : list.last();
}

bool SimisDocument::isBlock(Item item) const {
    return item.tree && item.node >= 0 && item.tree->node(item.node).kind == SimisTree::Kind::Block;
}

QStringView SimisDocument::text(Item item) const {
    return item.tree && item.node >= 0 ? item.tree->text(item.node) : QStringView();
}

bool SimisDocument::is(Item item, QAnyStringView name) const {
    return item.tree && item.node >= 0 && item.tree->is(item.node, name);
}

bool SimisDocument::number(Item value, Units::Quantity quantity, double &si, QStringView defaultUnit) const {
    return value.tree && value.node >= 0 && Units::fromFileToken(text(value), quantity, si, defaultUnit);
}

QString SimisDocument::write(Item item) const {
    QString out;
    if (item.tree && item.node >= 0)
        write(item.tree, item.node, out, 0);
    return out;
}

void SimisDocument::write(const SimisTree *tree, int index, QString &out, int depth) const {
    const SimisTree::Node &node = tree->node(index);
    const QStringView text = tree->text(index);
    if (node.kind == SimisTree::Kind::Include) {
        // Its file's items in its place.
        const SimisTree *included = links.value({tree, index});
        if (included && depth < IncludeDepth)
            for (int i = included->first(); i >= 0; i = included->node(i).next) {
                if (i != included->first())
                    out += u' ';
                write(included, i, out, depth + 1);
            }
        return;
    }
    if (node.kind == SimisTree::Kind::Value) {
        const bool plain = !(node.flags & SimisTree::Quoted) && !text.isEmpty()
                           && std::none_of(text.begin(), text.end(), [](QChar c) {
                                  return c.isSpace() || c == u'(' || c == u')' || c == u'"';
                              });
        if (plain)
            out += text;
        else
            out += SimisTextReader::quote(text.toString());
        return;
    }
    if (!text.isEmpty()) {
        out += text;
        out += u' ';
    }
    out += u'(';
    for (int i = node.child; i >= 0; i = tree->node(i).next) {
        out += u' ';
        write(tree, i, out, depth);
    }
    out += QLatin1String(" )");
}

bool SimisDocument::readsVehicleRoot(Item block) const {
    return isBlock(block) && readsVehicleRoot(block.tree, block.tree->node(block.node).child, 0);
}

bool SimisDocument::readsVehicleRoot(const SimisTree *tree, int first, int depth) const {
    for (int i = first; i >= 0; i = tree->node(i).next) {
        const SimisTree::Node &node = tree->node(i);
        if (node.kind == SimisTree::Kind::Include) {
            if (tree->text(i).trimmed().startsWith(u"[[Vehicle]]", Qt::CaseInsensitive))
                return true;
            const SimisTree *included = links.value({tree, i});
            if (included && depth < IncludeDepth && readsVehicleRoot(included, included->first(), depth + 1))
                return true;
        } else if (node.kind == SimisTree::Kind::Block && readsVehicleRoot(tree, node.child, depth)) {
            return true;
        }
    }
    return false;
}

QString SimisDocument::file(Item item) const {
    return item.tree && item.node >= 0 ? item.tree->files().value(item.tree->file(item.node)) : QString();
}

int SimisDocument::line(Item item) const {
    return item.tree && item.node >= 0 ? item.tree->line(item.node) : 0;
}

QVector<const SimisTree *> SimisDocument::trees() const {
    QVector<const SimisTree *> out;
    for (const auto &tree : kept)
        if (!out.contains(tree.get()))
            out.push_back(tree.get());
    return out;
}
