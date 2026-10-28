#include "SimisTree.h"
#include "SimisTextReader.h"
#include <algorithm>
#include <vector>

using Kind = SimisTextReader::Kind;

namespace {
bool same(QStringView a, const char *b) { return a.compare(QLatin1String(b), Qt::CaseInsensitive) == 0; }
// Inlined includes nested deeper than this are kept as nodes (a loop).
constexpr int InlineDepth = 16;
// A line reads alone: no quote left open, no ')' closing a block it did
// not open.
bool standsAlone(QStringView line) {
    bool quoted = false;
    int depth = 0;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (c == u'"' && (i == 0 || line[i - 1] != u'\\'))
            quoted = !quoted;
        else if (!quoted && c == u'(')
            ++depth;
        else if (!quoted && c == u')' && --depth < 0)
            return false;
    }
    return !quoted;
}
} // namespace

struct SimisTreeBuilder {
    SimisTree &tree;
    const SimisTree::Inliner &inliner;
    struct Frame {
        int block;
        int last;
    };
    std::vector<Frame> stack{{-1, -1}};
    int depth = 0;

    int push(const SimisTree::Node &node) {
        tree.items.push_back(node);
        const int index = int(tree.items.size()) - 1;
        Frame &frame = stack.back();
        if (frame.last >= 0)
            tree.items[frame.last].next = index;
        else if (frame.block >= 0)
            tree.items[frame.block].child = index;
        else
            tree.top = index;
        frame.last = index;
        return index;
    }
    int lineAt(int file, qsizetype offset) const {
        const qsizetype start = tree.starts[file];
        return int(QStringView(tree.source).mid(start, offset - start).count(u'\n')) + 1;
    }
    void problem(int file, qsizetype offset, const QString &message) {
        tree.found.push_back({file, lineAt(file, offset), message});
    }
    // The item after a comment or skip keyword, or a '#' or '_' item: a
    // block or one item ('(' or ')' left for the blocks).
    void skipNext(SimisTextReader &reader, int file, qsizetype base) {
        const Kind next = reader.peekKind();
        if (next == Kind::Open) {
            const auto open = reader.next();
            if (!reader.skipBlock())
                problem(file, base + open.begin, "A comment's block is not closed");
        } else if (next == Kind::Atom || next == Kind::String) {
            reader.next();
        }
    }
    int value(const SimisTextReader::Token &token, qsizetype base) {
        SimisTree::Node node;
        node.begin = qint32(base + token.begin);
        if (token.kind == Kind::Atom) {
            // A quote ends an unquoted item and is dropped (Open Rails: its
            // leading quote missing, `x.sms"`); what follows is the next.
            const qsizetype quote = token.text.indexOf(u'"');
            node.length = qint32(quote < 0 ? token.end - token.begin : quote);
            int index = node.length > 0 || quote < 0 ? push(node) : -1;
            if (quote >= 0 && quote + 1 < token.text.size()) {
                SimisTextReader::Token rest = token;
                rest.begin = token.begin + quote + 1;
                rest.text = token.text.mid(quote + 1);
                index = value(rest, base);
            }
            return index;
        }
        node.flags = SimisTree::Quoted;
        const qsizetype inner = token.end - token.begin - 2;
        if (inner == token.text.size()
            && QStringView(tree.source).mid(base + token.begin + 1, inner) == token.text) {
            node.begin += 1;
            node.length = qint32(inner);
        } else {
            node.flags |= SimisTree::Decoded;
            node.child = qint32(tree.strings.size());
            node.length = qint32(token.text.size());
            tree.strings += token.text;
        }
        return push(node);
    }
    void include(SimisTextReader &reader, int file, qsizetype base, const SimisTextReader::Token &keyword) {
        SimisTextReader::Token path;
        if (reader.peekKind() == Kind::Open) {
            reader.next();
            const Kind kind = reader.peekKind();
            if (kind == Kind::Atom || kind == Kind::String) {
                path = reader.next();
                if (reader.peekKind() == Kind::Close)
                    reader.next();
                else if (!reader.skipBlock())
                    problem(file, base + keyword.begin, "An include's block is not closed");
            } else if (kind == Kind::Close) {
                reader.next();
            }
        } else if (reader.peekKind() == Kind::Atom || reader.peekKind() == Kind::String) {
            path = reader.next();
        }
        if (path.kind != Kind::Atom && path.kind != Kind::String) {
            problem(file, base + keyword.begin, "An include without a file");
            return;
        }
        SimisTree::Inlined inlined;
        if (inliner && depth < InlineDepth && inliner(path.text, file, inlined)) {
            const int inlinedFile = int(tree.paths.size());
            tree.paths << inlined.path;
            const qsizetype start = tree.source.size();
            tree.starts << qint32(start);
            tree.source += inlined.text;
            ++depth;
            readText(inlinedFile, start, inlined.text);
            --depth;
            return;
        }
        const int node = value(path, base);
        tree.items[node].kind = SimisTree::Kind::Include;
    }
    void readText(int file, qsizetype base, const QString &text) {
        // The first line: MSTS's SIMIS header is not read, anything else is
        // (the user's, 2026-10-28) where it reads alone. Open Rails takes any
        // first line for the header and reads none of it, so content made for
        // it may leave a broken line there (OR_PSK's comments with an open
        // quote): such a line is not read either, and told; data read there
        // is told too, as that file reads differently in Open Rails.
        const qsizetype end = text.indexOf(u'\n');
        const qsizetype firstEnd = end < 0 ? text.size() : end;
        QStringView first = QStringView(text).left(firstEnd).trimmed();
        while (first.startsWith(QChar(0xfeff)))
            first = first.mid(1).trimmed();
        // The header's word, a stray character before it too (OR_PSK has
        // "<SIMISA@@@@@@@@@@JINX0D0t______").
        const QStringView word = first.left(first.indexOf(u' ') < 0 ? first.size() : first.indexOf(u' '));
        bool header = word.contains(QLatin1String("SIMISA@"), Qt::CaseInsensitive);
        if (!header && !standsAlone(first)) {
            header = true;
            tree.found.push_back({file, 1, QString("The first line leaves a quote open or closes a block it did not "
                                                   "open: not read, as Open Rails never reads it: \"%1\"")
                                                   .arg(first.left(100).toString())});
        }
        SimisTextReader reader(text, header ? firstEnd : 0);
        const int itemsBefore = int(tree.items.size());
        int previous = -1;
        for (;;) {
            if (reader.peekKind() == Kind::End)
                break;
            const auto token = reader.next();
            if (token.kind == Kind::Error) {
                problem(file, base + token.begin, token.text);
                tree.closed = false;
                break;
            }
            if (token.kind == Kind::Open) {
                if (previous >= 0) {
                    tree.items[previous].kind = SimisTree::Kind::Block;
                    stack.push_back({previous, -1});
                } else {
                    SimisTree::Node block;
                    block.kind = SimisTree::Kind::Block;
                    block.begin = qint32(base + token.begin);
                    stack.push_back({push(block), -1});
                }
                previous = -1;
                continue;
            }
            previous = -1;
            if (token.kind == Kind::Close) {
                if (stack.size() > 1) {
                    stack.pop_back();
                } else {
                    problem(file, base + token.begin, "A ')' that closes no block, skipped");
                    tree.closed = false;
                }
                continue;
            }
            if (token.kind == Kind::Atom) {
                const QStringView atom = token.text;
                if (same(atom, "comment") || same(atom, "skip") || atom.startsWith(u'#')
                    || atom.startsWith(u'_')) {
                    skipNext(reader, file, base);
                    continue;
                }
                if (same(atom, "include")) {
                    include(reader, file, base, token);
                    continue;
                }
            }
            previous = value(token, base);
        }
        if (!header)
            for (int i = itemsBefore; i < int(tree.items.size()); ++i)
                if (tree.items[i].begin >= base && tree.items[i].begin < base + firstEnd) {
                    tree.found.push_back({file, 1, QString("Data on the first line, which Open Rails does not read "
                                                           "(it takes it for the file's header): \"%1\"")
                                                           .arg(first.left(100).toString())});
                    break;
                }
    }
};

std::shared_ptr<const SimisTree> SimisTree::read(const QString &text, const QString &path,
                                                 const Inliner &inliner, const QString &note) {
    auto tree = std::make_shared<SimisTree>();
    tree->source = text;
    tree->paths << path;
    tree->starts << 0;
    if (!note.isEmpty())
        tree->found.push_back({0, 0, note});
    SimisTreeBuilder builder{*tree, inliner};
    builder.readText(0, 0, text);
    if (builder.stack.size() > 1) {
        const int block = builder.stack[1].block;
        tree->found.push_back({tree->file(block), tree->line(block),
                               QString("The block %1 is not closed at the end of the file")
                                       .arg(tree->text(block).toString())});
        tree->closed = false;
    }
    tree->items.squeeze();
    return tree;
}

QStringView SimisTree::text(int index) const {
    const Node &n = items[index];
    if (n.flags & Decoded)
        return QStringView(strings).mid(n.child, n.length);
    return QStringView(source).mid(n.begin, n.length);
}

bool SimisTree::is(int index, QAnyStringView name) const {
    return QAnyStringView::compare(text(index), name, Qt::CaseInsensitive) == 0;
}

int SimisTree::file(int index) const {
    const auto at = std::upper_bound(starts.begin(), starts.end(), items[index].begin);
    return int(at - starts.begin()) - 1;
}

int SimisTree::line(int index) const {
    const int f = file(index);
    return int(QStringView(source).mid(starts[f], items[index].begin - starts[f]).count(u'\n')) + 1;
}

qint64 SimisTree::bytes() const {
    return sizeof(SimisTree) + 2 * (source.capacity() + strings.capacity())
           + items.capacity() * qint64(sizeof(Node));
}
