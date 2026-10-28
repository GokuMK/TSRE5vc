#pragma once
#include <QString>
#include <QAnyStringView>
#include <QStringList>
#include <QStringView>
#include <QVector>
#include <functional>
#include <memory>

// A SIMIS text file read into a compact tree (task driving 01), as Open
// Rails reads it: the first line is the file's header and is not read;
// comment and skip blocks, and an item starting with '#' or '_' together
// with the item after it, are left out; an item followed by '(' names the
// block. An include is kept as a node holding its written path, for the
// reader of the tree to resolve (SimisDocument), so one file's tree can be
// shared by every file that includes it. One text and a flat array of
// nodes, made once and never changed: shared by const pointer between
// readers and threads.
class SimisTree {
  public:
    enum class Kind : quint8 { Value, Block, Include };
    enum Flag : quint8 { Quoted = 1, Decoded = 2 };
    struct Node {
        // A value's text, a block's name or an include's path, in the
        // tree's text; a decoded quoted string (escapes, '+' joins) has its
        // text in the strings instead, at `child`.
        qint32 begin = 0, length = 0;
        // A block's first item; the next item in its block (-1 none).
        qint32 child = -1, next = -1;
        Kind kind = Kind::Value;
        quint8 flags = 0;
    };
    struct Problem {
        // Its file: 0 the file read, then each inlined (files()).
        int file = 0;
        int line = 0;
        QString message;
    };
    // An include met while reading: true with the text of the file to read
    // in its place (Open Rails reads an include's items as if written
    // there) and its path, false to keep the include as a node. `file` is
    // the file holding the include (files()).
    struct Inlined {
        QString text;
        QString path;
    };
    using Inliner = std::function<bool(QStringView path, int file, Inlined &out)>;

    // Reads a file's decoded text; `path` names it in files(), `note` is a
    // problem of its decoding (SimisTextReader::decode) kept with it.
    static std::shared_ptr<const SimisTree> read(const QString &text, const QString &path,
                                                 const Inliner &inliner = {}, const QString &note = {});
    // The text read: the file's (and those inlined after it).
    const QString &whole() const { return source; }

    // The first item at the top level (-1 none).
    int first() const { return top; }
    int size() const { return int(items.size()); }
    const Node &node(int index) const { return items[index]; }
    QStringView text(int index) const;
    // A block's name or a value is `name`, in any case.
    bool is(int index, QAnyStringView name) const;
    // Where a node was written: its file (files()) and line (1 the first).
    int file(int index) const;
    int line(int index) const;
    // The file read, then each file inlined into it.
    const QStringList &files() const { return paths; }
    // Every block was closed and no ')' closed more than was opened. An
    // include that is not balanced changes the blocks of the file holding
    // it, so it cannot be read on its own (SimisDocument inlines it).
    bool balanced() const { return closed; }
    const QVector<Problem> &problems() const { return found; }
    // The memory it takes, roughly (bytes).
    qint64 bytes() const;

  private:
    friend struct SimisTreeBuilder;
    QString source;
    QString strings;
    QVector<Node> items;
    // Where each inlined file's text starts in the source, by file index.
    QVector<qint32> starts;
    QStringList paths;
    QVector<Problem> found;
    int top = -1;
    bool closed = true;
};
