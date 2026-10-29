#include <tsre/tests/VehicleFileTestSuite.h>

#include <QDate>
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
#include <tsre/trains/EngLights.h>
#include <QScopedValueRollback>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/VehicleDefinition.h>
#include <tsre/trains/VehicleKeys.h>

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

    // The first line: MSTS's SIMIS header is not read, anything else is
    // (the user's, 2026-10-28), data there told (Open Rails never reads a
    // file's first line).
    {
        const QString a = trainset + "/FIRST/a.wag";
        write(a, "Mass ( 1t )\r\nMass ( 2t )\r\n");
        auto doc = SimisDocument::read(a, roots);
        test.check(doc->all("mass").size() == 2 && lastValues(*doc, "mass") == QStringList{"2t"}
                           && hasProblem(*doc, "first line"),
                   "data on the first line read and told: " + problems(*doc));
        const QString b = trainset + "/FIRST/b.wag";
        write(b, Header + "Mass ( 2t )\r\n");
        const QString c = trainset + "/FIRST/c.inc";
        write(c, "comment ( Mass ( 3t ) )\r\nMass ( 4t )\r\n");
        const auto header = SimisDocument::read(b, roots), comment = SimisDocument::read(c, roots);
        write(trainset + "/FIRST/stray.wag", "<" + Header + "Mass ( 6t )\r\n");
        const auto stray = SimisDocument::read(trainset + "/FIRST/stray.wag", roots);
        test.check(header->problems().isEmpty() && header->all("simisa@@@@@@@@@@jinx0d0t______").isEmpty()
                           && header->items().size() == 1 && stray->problems().isEmpty() && stray->items().size() == 1
                           && comment->problems().isEmpty()
                           && lastValues(*comment, "mass") == QStringList{"4t"},
                   "a header line not read, a comment on the first line left out, neither told");
        // A first line that does not read alone (OR_PSK's comments with an
        // open quote, made for Open Rails, which never reads it): not read.
        write(trainset + "/FIRST/e.inc", "comment (\"Size, Mass etc. dla modelu SM31)\r\nMass ( 5t )\r\n");
        auto open = SimisDocument::read(trainset + "/FIRST/e.inc", roots);
        test.check(lastValues(*open, "mass") == QStringList{"5t"} && hasProblem(*open, "quote open"),
                   "a first line leaving a quote open not read, told: " + problems(*open));
        // An include opening a block on its first line (OR_PSK's
        // Agregat_SA134_B.inc): read whole, so its ')' closes its own block.
        write(trainset + "/FIRST/diesel.inc", "ORTSDieselEngines ( 1\r\nDiesel ( MaxRPM ( 1000 ) )\r\n)\r\n");
        write(trainset + "/FIRST/d.eng", Header + "Engine ( D include ( diesel.inc ) MaxForce ( 300kN ) )\r\n");
        auto engine = SimisDocument::read(trainset + "/FIRST/d.eng", roots);
        test.check(lastValues(*engine, "engine(ortsdieselengines(diesel(maxrpm") == QStringList{"1000"}
                           && lastValues(*engine, "engine(maxforce") == QStringList{"300kN"}
                           && hasProblem(*engine, "first line") && !hasProblem(*engine, "balance"),
                   "an include opening a block on its first line read whole: " + problems(*engine));
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
                             "  Half ( x.sms\" y\"z )\n"
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
        test.check(lastValues(*doc, "wagon(half") == QStringList({"x.sms", "y", "z"}),
                   "a quote ends an unquoted item: " + lastValues(*doc, "wagon(half").join("|"));
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

    // A vehicle definition: typed fields in SI, the file names found with
    // and without roots, the OPENRAILS copy over the original.
    {
        const QString folder = trainset + "/DEF";
        write(trainset + "/common.s/body.s", Header);
        write(folder + "/load.s", Header);
        write(folder + "/SOUND/def.sms", Header);
        write(game + "/SOUND/cab.sms", Header);
        write(folder + "/CABVIEW/def.cvf", Header);
        write(folder + "/CABVIEW3D/cab.s", Header);
        write(folder + "/def.eng",
              Header + "Wagon ( DEF\n"
                       "  Type ( Engine )\n"
                       "  WagonShape ( [[Trainset]]/common.s/body.s )\n"
                       "  FreightAnim ( load.s 1 0 )\n"
                       "  Size ( 3m 4m 20m )\n"
                       "  Mass ( 80t )\n"
                       "  Sound ( def.sms )\n"
                       "  Friction ( 100N/m/s 1 5mph 2N/m/s 1 )\n"
                       "  Coupling ( Type ( Automatic ) Spring ( r0 ( 1cm 5cm ) ) Velocity ( 0.1 ) )\n"
                       "  ORTS3DCab ( ORTS3DCabFile ( cab.s ) ORTS3DCabHeadPos ( 1 2 3 ) StartDirection ( 0 180 0 ) )\n"
                       "  Lights ( 1 Light ( Type ( 0 ) Conditions ( Headlight ( 0 ) ) Cycle ( 0 ) FadeIn ( 0 ) FadeOut ( 0 )\n"
                       "    States ( 1 State ( Duration ( 0 ) LightColour ( ffffffff ) Position ( 0 1 2 ) Radius ( 0.5 ) ) ) ) )\n"
                       ")\n"
                       "Engine ( DEF\n"
                       "  Type ( Electric )\n"
                       "  MaxForce ( 300kN )\n"
                       "  MaxPower ( 5600kW )\n"
                       "  MaxVelocity ( 100mph )\n"
                       "  Sound ( [[Sound]]/cab.sms )\n"
                       "  CabView ( def.cvf )\n"
                       "  Name ( \"The DEF\" )\n"
                       ")\n");
        write(folder + "/OPENRAILS/def.eng", Header + "include ( ../[[SameName]] )\n"
                                                      "Wagon ( DEF ORTSDavis_A ( 600N ) ORTSDavis_B ( 10N/m/s ) ORTSDavis_C ( 1 )\n"
                                                      "  Mass ( 82t ) Sound ( [[Nowhere]]/x.sms ) )\n");
        const auto msts = VehicleDefinition::read(folder, "def.eng", game, false);
        const auto copy = VehicleDefinition::read(folder, "def.eng", game, true);
        test.check(msts->ok() && msts->path == folder + "/def.eng" && copy->path == folder + "/OPENRAILS/def.eng"
                           && equal(msts->massKg, 80000) && equal(copy->massKg, 82000),
                   "the OPENRAILS copy read over the original when asked");
        test.check(msts->wagonTypeId == 4 && msts->typeHash == "Engine-Electric" && msts->engName == "DEF"
                           && msts->displayName == "The DEF" && equal(msts->forceN, 300000)
                           && equal(msts->powerW, 5600000) && equal(msts->maxSpeedMps, 100 * 0.44704)
                           && equal(msts->size[2], 20) && msts->couplings.size() == 1
                           && equal(msts->couplings[0].r0[1], 0.05),
                   "typed fields in SI");
        test.check(msts->shapePath == trainset + "/common.s/body.s" && msts->freightAnims.value(0).path == folder + "/load.s"
                           && msts->soundPath == folder + "/SOUND/def.sms" && msts->cabSoundPath == game + "/SOUND/cab.sms"
                           && msts->cabViewPath == folder + "/CABVIEW/def.cvf"
                           && msts->cab3DShapePath == folder + "/CABVIEW3D/cab.s" && msts->problems.isEmpty(),
                   "files with roots and in Open Rails' places: " + msts->shapePath + " " + msts->soundPath);
        test.check(msts->resistanceFrom == VehicleDefinition::Resistance::Friction
                           && copy->resistanceFrom == VehicleDefinition::Resistance::Davis
                           && equal(copy->resistance[0], 600) && equal(copy->resistance[1], 10)
                           && copy->soundPath.isEmpty() && !copy->problems.isEmpty(),
                   "the resistance and where it came from; an unknown root told");
        // MSTS's Friction fitted as Open Rails fits it, C1·V2^E1 + C2·V2 +
        // C2·v^E2 by least squares from V2 to 80 mph: a straight line gives
        // itself back, (100 + 2) × 5 mph + 2 v; TS_STARTER's ES44AC (Friction
        // ( 2023.3N/m/s -0.10 1.3mph 23.760N/m/s 1.798 )) Open Rails'
        // coefficients (its fit's closed form, ported).
        write(folder + "/es44.wag", Header + "Wagon ( ES44 Type ( Freight ) Mass ( 188t )\n"
                                             "  Friction ( 2023.3N/m/s -0.10 1.3mph 23.760N/m/s 1.798 ) )\n");
        const auto es44 = VehicleDefinition::read(folder, "es44.wag", game, false);
        const auto near = [](double a, double b) { return std::fabs(a - b) <= 1e-6 * std::max(1.0, std::fabs(b)); };
        test.check(near(msts->resistance[0], 227.9904) && near(msts->resistance[1], 2.0)
                           && std::fabs(msts->resistance[2]) < 1e-9
                           && es44->resistanceFrom == VehicleDefinition::Resistance::Friction
                           && near(es44->resistance[0], 1990.04872) && near(es44->resistance[1], 72.7405353)
                           && near(es44->resistance[2], 9.68753083),
                   QString("MSTS's Friction as Open Rails fits it: %1 N + %2 v + %3 v² (227.99 + 2 v); "
                           "the ES44AC's %4 + %5 v + %6 v² (1990.05 + 72.741 v + 9.6875 v²)")
                           .arg(msts->resistance[0]).arg(msts->resistance[1]).arg(msts->resistance[2])
                           .arg(es44->resistance[0]).arg(es44->resistance[1]).arg(es44->resistance[2]));
        const EngLights *lights = msts->lights ? msts->lights->lights() : nullptr;
        test.check(lights && lights->lights.size() == 1 && lights->lights[0].states.size() == 1
                           && equal(lights->lights[0].states[0].position[2], 2),
                   "the Lights block read from its text");
    }

    // The key catalogue: Open Rails' paths, any case, each value's quantity
    // and the unit of a number written without one.
    {
        using V = VehicleKeys::Value;
        const auto *force = VehicleKeys::find("Engine(MaxForce");
        const auto *r0 = VehicleKeys::find("wagon(coupling(spring(r0");
        const auto *friction = VehicleKeys::find("wagon(friction");
        const V speed = VehicleKeys::value("engine(maxvelocity", 0);
        const V pressure = VehicleKeys::value("wagon(maxreleaserate", 0);
        test.check(VehicleKeys::all().size() > 400 && force && force->values.size() == 1
                           && force->values[0].quantity == Quantity::Force && r0 && r0->values.size() == 2
                           && r0->values[1].quantity == Quantity::Length && friction && friction->values.size() == 5
                           && friction->values[2].quantity == Quantity::Speed && speed.quantity == Quantity::Speed
                           && speed.defaultUnit.isEmpty() && pressure.quantity == Quantity::PressureRate
                           && pressure.defaultUnit == "psi/s" && !VehicleKeys::find("wagon(nosuchkey")
                           && VehicleKeys::find("wagon(lights")->read == VehicleKeys::Read::Block
                           && VehicleKeys::find("wagon(passengercapacity")->read == VehicleKeys::Read::Present,
                   QString("the key catalogue: %1 keys").arg(VehicleKeys::all().size()));
        test.check(VehicleKeys::unmappedUnits().isEmpty(),
                   "every Open Rails unit has a quantity: " + VehicleKeys::unmappedUnits().join(","));
    }

    // Lights from one include shared by the vehicles reading it; not where
    // an include inside goes through [[Vehicle]].
    {
        const QString light = "Light ( Type ( 0 ) Conditions ( Headlight ( 0 ) ) Cycle ( 0 ) FadeIn ( 0 ) FadeOut ( 0 ) "
                              "States ( 1 State ( Duration ( 0 ) LightColour ( ffffffff ) Position ( 0 1 %1 ) "
                              "Radius ( 0.5 ) ) ) )\n";
        write(trainset + "/common.inc/plainLights.inc", Header + "Lights ( 1 " + light.arg(7) + " )\n");
        write(trainset + "/common.inc/ownLights.inc", Header + "Lights ( 1 include ( [[Vehicle]]/light.inc ) )\n");
        for (const QString name : {"LA", "LB"}) {
            write(QString("%1/%2/%2.wag").arg(trainset, name),
                  Header + "Wagon ( " + name + " include ( ../common.inc/plainLights.inc ) )\n");
            write(QString("%1/O%2/O%2.wag").arg(trainset, name),
                  Header + "Wagon ( O" + name + " include ( ../common.inc/ownLights.inc ) )\n");
            write(QString("%1/O%2/light.inc").arg(trainset, name), Header + light.arg(name == "LA" ? 1 : 2));
        }
        const auto a = VehicleDefinition::read(trainset + "/LA", "LA.wag", game, false);
        const auto b = VehicleDefinition::read(trainset + "/LB", "LB.wag", game, false);
        const auto oa = VehicleDefinition::read(trainset + "/OLA", "OLA.wag", game, false);
        const auto ob = VehicleDefinition::read(trainset + "/OLB", "OLB.wag", game, false);
        const EngLights *la = oa->lights ? oa->lights->lights() : nullptr;
        const EngLights *lb = ob->lights ? ob->lights->lights() : nullptr;
        test.check(a->lights && a->lights == b->lights && oa->lights != ob->lights && la && lb
                           && la->lights.size() == 1 && lb->lights.size() == 1
                           && equal(la->lights[0].states[0].position[2], 1) && equal(lb->lights[0].states[0].position[2], 2),
                   "lights from one include shared; with a [[Vehicle]] include inside, each vehicle's own");
    }

    // An editor's car: Reload reads its file again into a new definition,
    // cars made before keep theirs; problems go to the error list once.
    {
        QScopedValueRollback<QString> restoreRoot(Game::root, game);
        const QString folder = trainset + "/RELOAD";
        write(folder + "/r.wag", Header + "Wagon ( R Mass ( 10t ) include ( missing.inc ) )\n");
        const int before = ErrorMessagesLib::ErrorMessages.size();
        Eng eng(folder, "r.wag");
        Eng sameFile(folder, "r.wag");
        Eng car(&eng);
        const int told = ErrorMessagesLib::ErrorMessages.size() - before;
        QThread::msleep(20);
        write(folder + "/r.wag", Header + "Wagon ( R Mass ( 25t ) )\n");
        eng.reload();
        test.check(eng.loaded == 1 && equal(eng.definition->massKg, 25000) && equal(car.definition->massKg, 10000)
                           && car.definition != eng.definition && told == 1,
                   QString("Reload reads the file again, a car made before keeps its definition, a problem told once (%1)")
                           .arg(told));
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
    // Each problem once, with the vehicles meeting it, for the report
    // (TSRE_VEHICLE_REPORT).
    struct Listed {
        QString file;
        int line = 0;
        QString message;
        QString kind;
        int vehicles = 0;
        bool unused = false;
    };
    QMap<QString, Listed> listed;
    const auto kindOf = [](const QString &message) {
        // The kind: the message without its details (a quoted line, a path,
        // a block's name, numbers).
        QString kind = message;
        const qsizetype quoted = kind.indexOf(QLatin1String(": \""));
        if (quoted >= 0)
            kind.truncate(quoted);
        if (kind.startsWith(QLatin1String("Include ")) && kind.contains(QLatin1String(": ")))
            kind = "Include …" + kind.mid(kind.lastIndexOf(QLatin1String(": ")));
        kind.replace(QRegularExpression("The block \\S+ is"), "The block … is");
        kind.replace(QRegularExpression("\\S*[/\\\\]\\S*|\\d+(\\.\\d+)?"), "…");
        return kind;
    };
    const auto list = [&](const SimisDocument::Problem &p, bool unused, QSet<QString> &counted) {
        const QString key = p.file + "\x1f" + QString::number(p.line) + "\x1f" + p.message;
        Listed &entry = listed[key];
        if (entry.file.isEmpty())
            entry = {p.file, p.line, p.message, kindOf(p.message), 0, unused};
        if (!unused && !counted.contains(key)) {
            counted.insert(key);
            ++entry.vehicles;
        }
    };
    int read = 0, withProblems = 0;
    for (const QString &path : files) {
        const QString folder = QFileInfo(path).path();
        const QString vehicle = folder.endsWith("/OPENRAILS") ? QFileInfo(folder).path() : folder;
        auto doc = SimisDocument::read(path, {root, vehicle});
        read += doc->ok() ? 1 : 0;
        withProblems += doc->problems().isEmpty() ? 0 : 1;
        QSet<QString> counted;
        for (const auto &p : doc->problems())
            list(p, false, counted);
        for (const auto &p : doc->problems()) {
            if (verbose && p.line == 1 && p.message.startsWith("Data on the first line"))
                qInfo().noquote() << "  first line read:" << QString(p.file).remove(root) << p.message.mid(p.message.indexOf(": \"") + 2);
            const QString kind = kindOf(p.message);
            const QString key = kind + "\x1f" + p.file;
            if (seen.contains(key))
                continue;
            seen.insert(key);
            ++kinds[kind];
            if (!examples.contains(kind))
                examples[kind] = QString("%1:%2 %3").arg(QString(p.file).remove(root)).arg(p.line).arg(p.message);
        }
        // TSRE_VEHICLE_FILE=<part of a path>: what a vehicle's lights read.
        const QString show = qEnvironmentVariable("TSRE_VEHICLE_FILE");
        if (!show.isEmpty() && path.contains(show))
        {
            qInfo().noquote() << "  " << QString(path).remove(root) << "lights:"
                              << doc->all("wagon(lights(light").size() << "problems:" << doc->problems().size();
            for (const auto &p : doc->problems())
                qInfo().noquote() << "     " << QString(p.file).remove(root) << p.line << p.message;
            for (const QString &f : doc->files())
                qInfo().noquote() << "      file" << QString(f).remove(root);
            const auto def = VehicleDefinition::read(QFileInfo(path).path(), QFileInfo(path).fileName(), root, false);
            const EngLights *parsed = def->lights ? def->lights->lights() : nullptr;
            qInfo().noquote() << "      parsed lights" << (parsed ? parsed->lights.size() : -1);
            for (const Item &lights : doc->all("wagon(lights"))
                qInfo().noquote() << "      Lights in" << QString(doc->file(lights)).remove(root) << doc->line(lights)
                                  << "lights" << doc->items(lights).size();
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
    // The trees' memory shared, and as it would be with every vehicle
    // holding copies of its own (no shared includes).
    QSet<const SimisTree *> trees;
    qint64 bytes = 0, nodes = 0, unshared = 0;
    for (const auto &doc : docs)
        for (const SimisTree *tree : doc->trees()) {
            unshared += tree->bytes();
            if (!trees.contains(tree)) {
                trees.insert(tree);
                bytes += tree->bytes();
                nodes += tree->size();
            }
        }
    qInfo().noquote() << QString("[tests:vehicle-file-corpus] %1 files read of %2 in %3 ms; %4 with problems; "
                                 "%5 trees, %6 nodes, %7 MB (%9 MB unshared); %8")
                                 .arg(read)
                                 .arg(files.size())
                                 .arg(ms)
                                 .arg(withProblems)
                                 .arg(trees.size())
                                 .arg(nodes)
                                 .arg(bytes / 1048576.0, 0, 'f', 1)
                                 .arg(peak.trimmed())
                                 .arg(unshared / 1048576.0, 0, 'f', 1);
    for (auto k = kinds.cbegin(); k != kinds.cend(); ++k)
        qInfo().noquote() << QString("  %1 x %2  e.g. %3").arg(k.value(), 5).arg(k.key(), examples[k.key()]);
    test.check(read == files.size(), QString("every file read: %1 of %2").arg(read).arg(files.size()));

    // TSRE_VEHICLE_REPORT=<file.md>: every problem, by file and line, for
    // fixing the content; include files no vehicle reads too.
    const QString reportPath = qEnvironmentVariable("TSRE_VEHICLE_REPORT");
    if (!reportPath.isEmpty()) {
        QSet<QString> reached;
        for (const auto &doc : docs)
            for (const QString &f : doc->files())
                reached.insert(f);
        int unusedFiles = 0;
        QDirIterator includes(root + "/TRAINS/TRAINSET", QDir::Files, QDirIterator::Subdirectories);
        while (includes.hasNext()) {
            const QString path = QDir::cleanPath(includes.next());
            if (QFileInfo(path).suffix().toLower() != "inc" || reached.contains(path))
                continue;
            ++unusedFiles;
            // Read alone: no vehicle, so [[Vehicle]] paths are not known.
            const auto doc = SimisDocument::read(path, {root, QString()});
            QSet<QString> counted;
            for (const auto &p : doc->problems())
                if (!p.message.contains("no vehicle's folder"))
                    list(p, true, counted);
        }
        // Content to fix; differences from Open Rails and encodings to look at.
        const auto note = [](const QString &message) {
            return message.startsWith("Data on the first line") || message.contains("read as Latin-1")
                   || message.contains("without a byte order mark");
        };
        QMap<QString, QVector<Listed>> fix, look;
        for (const Listed &entry : listed)
            (note(entry.message) ? look : fix)[entry.kind] << entry;
        const auto cell = [](QString text) { return text.replace('|', "\\|").replace('\n', ' '); };
        QString out;
        out += "# Vehicle file problems: " + QFileInfo(root).fileName() + "\n\n";
        out += QString("Every ENG/WAG file under `TRAINS/TRAINSET` (%1) as TSRE reads it (task driving 01), and "
                       "the %2 include files no vehicle reads. Made %3 by `TSRE_VEHICLE_REPORT=<file> TSRE5vc --test "
                       "--test-suite vehicle-file-corpus --game-root <game folder>`: run it again after fixing.\n\n"
                       "Paths are from the game folder; a line 0 is the file as a whole. *Vehicles* counts the "
                       "vehicles reading the file (*none*: an include file no vehicle reads).\n\n")
                       .arg(files.size())
                       .arg(unusedFiles)
                       .arg(QDate::currentDate().toString(Qt::ISODate));
        const auto section = [&](const QString &title, const QString &about, const QMap<QString, QVector<Listed>> &groups) {
            int count = 0;
            for (const auto &group : groups)
                count += group.size();
            out += QString("## %1 (%2)\n\n%3\n\n").arg(title).arg(count).arg(about);
            for (auto g = groups.cbegin(); g != groups.cend(); ++g) {
                QVector<Listed> entries = g.value();
                std::sort(entries.begin(), entries.end(), [](const Listed &a, const Listed &b) {
                    return a.file != b.file ? a.file < b.file : a.line < b.line;
                });
                out += QString("### %1 (%2)\n\n| File | Line | Detail | Vehicles |\n| --- | --- | --- | --- |\n")
                               .arg(cell(g.key()))
                               .arg(entries.size());
                for (const Listed &e : entries) {
                    // What the section's title does not say: a quoted line,
                    // an include's path.
                    QString detail;
                    const qsizetype quoted = e.message.indexOf(QLatin1String(": \""));
                    if (quoted >= 0)
                        detail = e.message.mid(quoted + 2);
                    else if (e.message.startsWith(QLatin1String("Include ")))
                        detail = e.message.mid(8, e.message.lastIndexOf(QLatin1String(": ")) - 8);
                    detail.remove(root + "/");
                    out += QString("| `%1` | %2 | %3 | %4 |\n")
                                   .arg(QString(e.file).remove(root + "/"))
                                   .arg(e.line)
                                   .arg(detail.isEmpty() ? QString() : "`" + cell(detail) + "`")
                                   .arg(e.unused ? QString("none") : QString::number(e.vehicles));
                }
                out += "\n";
            }
        };
        section("To fix", "Data is lost or misread here, in TSRE and mostly in Open Rails too.", fix);
        section("To look at",
                "Read here, but Open Rails reads it differently (a first line), or the text's encoding is a guess.",
                look);
        QFile report(reportPath);
        test.check(report.open(QIODevice::WriteOnly) && report.write(out.toUtf8()) > 0,
                   "the report written: " + reportPath);
    }
    Q_UNUSED(verbose);
    return test.finish();
}

