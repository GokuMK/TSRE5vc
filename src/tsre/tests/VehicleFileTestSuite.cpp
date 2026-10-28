#include <tsre/tests/VehicleFileTestSuite.h>

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/fileFunctions/SimisDocument.h>
#include <tsre/fileFunctions/SimisTextReader.h>
#include <tsre/tests/TokenTestSupport.h>

using Item = SimisDocument::Item;
using Units::Quantity;

namespace {

const QString Header = "SIMISA@@@@@@@@@@JINX0D0t______\r\n\r\n";

enum class Encoding { Utf16Le, Utf16Be, Utf8Bom, Utf8, Utf16LeNoBom, Latin1 };

bool write(const QString &path, const QString &text, Encoding encoding = Encoding::Utf16Le) {
    QDir().mkpath(QFileInfo(path).path());
    QByteArray bytes;
    switch (encoding) {
    case Encoding::Utf16Le:
    case Encoding::Utf16LeNoBom: {
        QStringEncoder encoder(QStringEncoder::Utf16LE);
        bytes = encoder(text);
        if (encoding == Encoding::Utf16Le)
            bytes.prepend("\xff\xfe");
        break;
    }
    case Encoding::Utf16Be: {
        QStringEncoder encoder(QStringEncoder::Utf16BE);
        bytes = "\xfe\xff" + QByteArray(encoder(text));
        break;
    }
    case Encoding::Utf8Bom:
        bytes = "\xef\xbb\xbf" + text.toUtf8();
        break;
    case Encoding::Utf8:
        bytes = text.toUtf8();
        break;
    case Encoding::Latin1:
        bytes = text.toLatin1();
        break;
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool equal(double a, double b) { return std::fabs(a - b) <= 1e-9 * std::max(1.0, std::fabs(b)); }

bool hasProblem(const SimisDocument &doc, const QString &part) {
    for (const auto &p : doc.problems())
        if (p.message.contains(part, Qt::CaseInsensitive))
            return true;
    return false;
}

QString problems(const SimisDocument &doc) {
    QStringList list;
    for (const auto &p : doc.problems())
        list << QString("%1:%2 %3").arg(QFileInfo(p.file).fileName()).arg(p.line).arg(p.message);
    return list.join(" | ");
}

QStringList texts(const SimisDocument &doc, const QVector<Item> &items) {
    QStringList list;
    for (const Item &item : items)
        list << doc.text(item).toString();
    return list;
}

// The values of the last block at a path, as text.
QStringList lastValues(const SimisDocument &doc, const QString &path) {
    return texts(doc, doc.values(doc.last(path)));
}

} // namespace

int TsreTests::runVehicleFileSuite(bool verbose) {
    TokenTest::Suite test{"[tests:vehicle-file]", verbose};
    QTemporaryDir dir;
    test.check(dir.isValid(), "a temporary game folder");
    const QString game = dir.path();
    const QString trainset = game + "/TRAINS/TRAINSET";
    const SimisDocument::Roots roots{game, {}};

    // Encodings: each the same text, a name with letters outside ASCII
    // (Latin-1 can hold "ó" but not "ł").
    {
        const QString text = Header + "Wagon ( W\n\tName ( \"Łódź\" )\n\tMass ( 80t )\n)\n";
        const QString latin = Header + "Wagon ( W\n\tName ( \"Bóbr\" )\n\tMass ( 80t )\n)\n";
        const struct {
            Encoding encoding;
            const char *label;
            const char *note;
        } cases[] = {{Encoding::Utf16Le, "UTF-16 LE with a byte order mark", nullptr},
                     {Encoding::Utf16Be, "UTF-16 BE with a byte order mark", nullptr},
                     {Encoding::Utf8Bom, "UTF-8 with a byte order mark", nullptr},
                     {Encoding::Utf8, "UTF-8 without one", nullptr},
                     {Encoding::Utf16LeNoBom, "UTF-16 LE without one", "without a byte order mark"},
                     {Encoding::Latin1, "Latin-1", "Latin-1"}};
        int n = 0;
        for (const auto &c : cases) {
            const QString path = QString("%1/ENC/e%2.wag").arg(trainset).arg(n++);
            const bool isLatin = c.encoding == Encoding::Latin1;
            write(path, isLatin ? latin : text, c.encoding);
            const auto doc = SimisDocument::read(path, roots);
            double mass = 0;
            const bool read = doc->ok() && lastValues(*doc, "wagon(name") == QStringList{isLatin ? "Bóbr" : "Łódź"}
                              && doc->number(doc->values(doc->last("wagon(mass")).value(0), Quantity::Mass, mass)
                              && equal(mass, 80000);
            const bool noted = c.note ? hasProblem(*doc, c.note) : doc->problems().isEmpty();
            test.check(read && noted, QString("encoding %1 read%2: %3").arg(c.label, c.note ? ", noted" : "",
                                                                           problems(*doc)));
        }
        QByteArray odd("\xff\xfe" "A\0B", 5);
        QString out, error;
        test.check(!SimisTextReader::decode(odd, out, error) && error.contains("Odd"),
                   "odd UTF-16 bytes refused");
    }

    // The first line is the header and never read, as in Open Rails; told
    // when it holds more than a header, a blank or a comment.
    {
        const QString a = trainset + "/FIRST/a.wag";
        write(a, "Mass ( 1t )\r\nMass ( 2t )\r\n");
        auto doc = SimisDocument::read(a, roots);
        test.check(doc->all("mass").size() == 1 && lastValues(*doc, "mass") == QStringList{"2t"}
                           && hasProblem(*doc, "first line"),
                   "the first line is not read and is told: " + problems(*doc));
        const QString b = trainset + "/FIRST/b.wag";
        write(b, Header + "Mass ( 2t )\r\n");
        const QString c = trainset + "/FIRST/c.inc";
        write(c, "comment ( Mass ( 3t ) )\r\nMass ( 4t )\r\n");
        const auto header = SimisDocument::read(b, roots), comment = SimisDocument::read(c, roots);
        test.check(header->problems().isEmpty() && comment->problems().isEmpty()
                           && lastValues(*comment, "mass") == QStringList{"4t"},
                   "a header or a comment on the first line is not told");
    }

    // Comments, skip blocks, '#' and '_' items with the item after them;
    // an item followed by '(' names the block; quoted strings.
    {
        const QString path = trainset + "/COMMENTS/c.wag";
        write(path, Header + "Wagon ( W\n"
                             "  comment ( Mass ( 9t ) )\n"
                             "  Skip ( Mass ( 8t ) )\n"
                             "  #Mass ( 7t )\n"
                             "  _Mass ( 6t )\n"
                             "  # 5t\n"
                             "  comment( 22.805t empty, 106.747t full\" )\n"
                             "  Mass ( 80t #368000lb )\n"
                             "  Size ( 3m 4.2m 20m )\n"
                             "  Name ( \"A \" + \"B\" )\n"
                             "  Description ( \"say \\\"hi\\\"\" )\n"
                             "  Plain ( \"abc\" )\n"
                             "  ( anonymous )\n"
                             ")\n");
        auto doc = SimisDocument::read(path, roots);
        double x = 0, z = 0;
        const auto size = doc->values(doc->last("wagon(size"));
        test.check(doc->all("wagon(mass").size() == 1 && lastValues(*doc, "wagon(mass") == QStringList{"80t"},
                   "comment, skip, '#' and '_' items are left out (a quote inside a comment's word too): " + lastValues(*doc, "wagon(mass").join(","));
        test.check(size.size() == 3 && doc->number(size[0], Quantity::Length, x) && equal(x, 3)
                           && doc->number(size[2], Quantity::Length, z) && equal(z, 20),
                   "a block's values as numbers with units");
        const Item name = doc->values(doc->last("wagon(name")).value(0);
        const Item plain = doc->values(doc->last("wagon(plain")).value(0);
        test.check(doc->text(name) == u"A B" && lastValues(*doc, "wagon(description") == QStringList{"say \"hi\""}
                           && (name.tree->node(name.node).flags & SimisTree::Decoded)
                           && !(plain.tree->node(plain.node).flags & SimisTree::Decoded)
                           && doc->text(plain) == u"abc",
                   "quoted strings: joined with '+', escapes; a plain one read in place");
        const auto top = doc->items(doc->last("wagon"));
        test.check(doc->text(top.value(0)) == u"W" && doc->isBlock(top.last()) && doc->text(top.last()).isEmpty()
                           && doc->problems().isEmpty() && doc->line(doc->last("wagon(size")) == 11,
                   "the block's name value, an anonymous block, lines: " + problems(*doc));
    }

    // Includes: relative to the file holding them, nested, without
    // parentheses, [[SameName]], the roots, last wins, couplings add.
    {
        write(trainset + "/common.inc/coupler.inc",
              Header + "Coupling ( Type ( Automatic ) Spring ( r0 ( 1cm 5cm ) ) )\n"
                       "include ( nested/more.inc )\n");
        write(trainset + "/common.inc/nested/more.inc", Header + "Coupling ( Type ( Chain ) )\n");
        write(trainset + "/VEH/veh.wag", Header + "Wagon ( VEH\n"
                                                  "  Mass ( 20t )\n"
                                                  "  include ( ../common.inc/coupler.inc )\n"
                                                  "  include \"[[Trainset]]/common.inc/extra.inc\"\n"
                                                  ")\n");
        write(trainset + "/common.inc/extra.inc", Header + "WheelRadius ( 0.46m )\n");
        write(trainset + "/VEH/OPENRAILS/veh.wag", Header + "include ( ../[[SameName]] )\n"
                                                            "Wagon ( VEH\n"
                                                            "  Mass ( 25t )\n"
                                                            "  ORTSDavis_A ( 600N )\n"
                                                            ")\n");
        auto doc = SimisDocument::read(trainset + "/VEH/OPENRAILS/veh.wag", roots);
        const auto couplings = doc->all("wagon(coupling");
        const auto types = doc->all("wagon(coupling(type");
        test.check(doc->ok() && doc->problems().isEmpty() && couplings.size() == 2
                           && texts(*doc, doc->values(types.value(0))) == QStringList{"Automatic"}
                           && texts(*doc, doc->values(types.value(1))) == QStringList{"Chain"},
                   "includes relative to their file, nested, couplings add: " + problems(*doc));
        test.check(lastValues(*doc, "wagon(mass") == QStringList{"25t"} && doc->all("wagon(mass").size() == 2
                           && doc->all("wagon").size() == 2 && doc->last("wagon(ortsdavis_a").tree
                           && lastValues(*doc, "wagon(wheelradius") == QStringList{"0.46m"},
                   "[[SameName]] reads the original; the override's value is the last");
        test.check(doc->files().size() == 5 && QFileInfo(doc->file(doc->last("wagon(coupling(type"))).fileName()
                                                       == "more.inc"
                           && doc->line(doc->last("wagon(coupling(type")) == 3,
                   "the files read, and where an included item was written: " + doc->files().join(","));

        write(game + "/GLOBAL/g.inc", Header + "G ( 1 )\n");
        write(game + "/SOUND/s.inc", Header + "S ( 1 )\n");
        write(game + "/TRAINS/t.inc", Header + "T ( 1 )\n");
        write(game + "/top.inc", Header + "Top ( 1 )\n");
        write(trainset + "/ROOTS/here.inc", Header + "H ( 1 )\n");
        write(trainset + "/ROOTS/own.inc", Header + "V ( 1 )\n");
        write(trainset + "/ROOTS/r.wag", Header + "include ( \"[[Global]]/g.inc\" )\n"
                                                  "include ( \"[[sound]]\\\\s.inc\" )\n"
                                                  "include ( [[Trains]]/t.inc )\n"
                                                  "include ( [[Game]]/top.inc )\n"
                                                  "include ( [[Here]]/here.inc )\n"
                                                  "include ( [[Vehicle]]/own.inc )\n");
        auto rooted = SimisDocument::read(trainset + "/ROOTS/r.wag", {game, trainset + "/ROOTS"});
        test.check(rooted->problems().isEmpty() && rooted->last("g").tree && rooted->last("s").tree
                           && rooted->last("t").tree && rooted->last("top").tree && rooted->last("h").tree
                           && rooted->last("v").tree,
                   "every root, in any case, with '\\' too: " + problems(*rooted));
        auto unknown = SimisDocument::read(trainset + "/ROOTS/r.wag", roots);
        test.check(hasProblem(*unknown, "no vehicle's folder") && !unknown->last("v").tree && unknown->last("g").tree,
                   "[[Vehicle]] where no vehicle is known: told, the rest read");
    }

    // [[Vehicle]] in a shared include: each vehicle gets its own file, the
    // shared file's tree is one.
    {
        write(trainset + "/common.inc/lights.inc", Header + "Lights ( include ( [[Vehicle]]/lamps.inc ) )\n");
        for (const QString name : {"V1", "V2"}) {
            write(QString("%1/%2/%2.wag").arg(trainset, name),
                  Header + "Wagon ( " + name + " include ( ../common.inc/lights.inc ) )\n");
            write(QString("%1/%2/lamps.inc").arg(trainset, name), Header + "Lamp ( " + name + " )\n");
        }
        auto v1 = SimisDocument::read(trainset + "/V1/V1.wag", {game, trainset + "/V1"});
        auto v2 = SimisDocument::read(trainset + "/V2/V2.wag", {game, trainset + "/V2"});
        const Item lights1 = v1->last("wagon(lights"), lights2 = v2->last("wagon(lights");
        test.check(lastValues(*v1, "wagon(lights(lamp") == QStringList{"V1"}
                           && lastValues(*v2, "wagon(lights(lamp") == QStringList{"V2"} && lights1.tree == lights2.tree,
                   "[[Vehicle]] in a shared include: the vehicle's own file, one shared tree");
    }

    // Problems: a missing include, a loop, a path leaving the game folder,
    // an absolute path, an unknown root; reading goes on after each.
    {
        write(trainset + "/BAD/loop.inc", Header + "include ( bad.wag )\n");
        write(trainset + "/BAD/bad.wag", Header + "Wagon ( BAD\n"
                                                  "  include ( missing.inc )\n"
                                                  "  include ( loop.inc )\n"
                                                  "  include ( ../../../../../outside.inc )\n"
                                                  "  include ( /etc/passwd )\n"
                                                  "  include ( [[Nowhere]]/x.inc )\n"
                                                  "  Mass ( 5t )\n"
                                                  ")\n");
        auto doc = SimisDocument::read(trainset + "/BAD/bad.wag", roots);
        test.check(hasProblem(*doc, "not found") && hasProblem(*doc, "which includes it")
                           && hasProblem(*doc, "leaves the game folder") && hasProblem(*doc, "absolute path")
                           && hasProblem(*doc, "unknown root") && lastValues(*doc, "wagon(mass") == QStringList{"5t"}
                           && doc->problems().size() == 5,
                   "missing, looping, outside, absolute, unknown root: told, the rest read: " + problems(*doc));
    }

    // An include whose parentheses do not balance is read in place, as Open
    // Rails reads it: its extra ')' closes the block holding the include.
    {
        write(trainset + "/common.inc/extra_close.inc", Header + "MaxForce ( 300kN )\n)\n");
        write(trainset + "/UNB/unb.eng", Header + "Engine ( UNB\n"
                                                  "  include ( ../common.inc/extra_close.inc )\n"
                                                  "  CabView ( unb.cvf )\n"
                                                  ")\n");
        auto doc = SimisDocument::read(trainset + "/UNB/unb.eng", roots);
        test.check(lastValues(*doc, "engine(maxforce") == QStringList{"300kN"} && !doc->last("engine(cabview").tree
                           && doc->last("cabview").tree && hasProblem(*doc, "do not balance")
                           && hasProblem(*doc, "closes no block") && doc->files().size() == 2,
                   "an unbalanced include read in place, closing its holder's block: " + problems(*doc));
        write(trainset + "/UNB/open.wag", Header + "Wagon ( OPEN\n  Mass ( 1t )\n");
        auto open = SimisDocument::read(trainset + "/UNB/open.wag", roots);
        test.check(lastValues(*open, "wagon(mass") == QStringList{"1t"} && hasProblem(*open, "not closed")
                           && !open->trees().value(0)->balanced(),
                   "a block left open at the end: read, told");
    }

    // The cache: a file read once while held; a changed file read again,
    // documents made before keep theirs.
    {
        const QString path = trainset + "/CACHE/c.wag";
        write(path, Header + "Wagon ( C Mass ( 1t ) )\n");
        auto first = SimisDocument::read(path, roots);
        auto again = SimisDocument::read(path, roots);
        test.check(first->trees().value(0) == again->trees().value(0), "a file read once while held");
        QThread::msleep(20);
        write(path, Header + "Wagon ( C Mass ( 22t ) )\n");
        auto changed = SimisDocument::read(path, roots);
        test.check(first->trees().value(0) != changed->trees().value(0)
                           && lastValues(*changed, "wagon(mass") == QStringList{"22t"}
                           && lastValues(*first, "wagon(mass") == QStringList{"1t"},
                   "a changed file read again; the document made before keeps the old");
    }
    return test.finish();
}

int TsreTests::runVehicleFileCorpusSuite(bool verbose) {
    TokenTest::Suite test{"[tests:vehicle-file-corpus]", verbose};
    const QString root = QDir::cleanPath(Game::root);
    QStringList files;
    QDirIterator it(root + "/TRAINS/TRAINSET", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == "eng" || suffix == "wag")
            files << path;
    }
    files.sort();
    QElapsedTimer timer;
    timer.start();
    std::vector<std::shared_ptr<const SimisDocument>> docs;
    docs.reserve(files.size());
    // Each problem kind, counted once per file it is in.
    QMap<QString, int> kinds;
    QMap<QString, QString> examples;
    QSet<QString> seen;
    int read = 0, withProblems = 0;
    for (const QString &path : files) {
        const QString folder = QFileInfo(path).path();
        const QString vehicle = folder.endsWith("/OPENRAILS") ? QFileInfo(folder).path() : folder;
        auto doc = SimisDocument::read(path, {root, vehicle});
        read += doc->ok() ? 1 : 0;
        withProblems += doc->problems().isEmpty() ? 0 : 1;
        for (const auto &p : doc->problems()) {
            // The kind: the message without paths, names and numbers.
            QString kind = p.message;
            kind.replace(QRegularExpression("(\"[^\"]*\"|\\S*[/\\\\]\\S*|\\d+(\\.\\d+)?)"), "…");
            const QString key = kind + "\x1f" + p.file;
            if (seen.contains(key))
                continue;
            seen.insert(key);
            ++kinds[kind];
            if (!examples.contains(kind))
                examples[kind] = QString("%1:%2 %3").arg(QString(p.file).remove(root)).arg(p.line).arg(p.message);
        }
        docs.push_back(doc);
    }
    const qint64 ms = timer.elapsed();
    // The process's peak memory (Linux), with the documents all held.
    QString peak;
    QFile status("/proc/self/status");
    if (status.open(QIODevice::ReadOnly))
        for (const QByteArray &line : status.readAll().split('\n'))
            if (line.startsWith("VmHWM:") || line.startsWith("VmRSS:"))
                peak += QString::fromLatin1(line.simplified()) + " ";
    QSet<const SimisTree *> trees;
    qint64 bytes = 0, nodes = 0;
    for (const auto &doc : docs)
        for (const SimisTree *tree : doc->trees())
            if (!trees.contains(tree)) {
                trees.insert(tree);
                bytes += tree->bytes();
                nodes += tree->size();
            }
    qInfo().noquote() << QString("[tests:vehicle-file-corpus] %1 files read of %2 in %3 ms; %4 with problems; "
                                 "%5 trees, %6 nodes, %7 MB; %8")
                                 .arg(read)
                                 .arg(files.size())
                                 .arg(ms)
                                 .arg(withProblems)
                                 .arg(trees.size())
                                 .arg(nodes)
                                 .arg(bytes / 1048576.0, 0, 'f', 1)
                                 .arg(peak.trimmed());
    for (auto k = kinds.cbegin(); k != kinds.cend(); ++k)
        qInfo().noquote() << QString("  %1 x %2  e.g. %3").arg(k.value(), 5).arg(k.key(), examples[k.key()]);
    test.check(read == files.size(), QString("every file read: %1 of %2").arg(read).arg(files.size()));
    Q_UNUSED(verbose);
    return test.finish();
}
