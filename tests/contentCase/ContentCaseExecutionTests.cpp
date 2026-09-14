#include <contentCase/ContentCaseExecution.h>
#include <contentCase/ContentCaseDocument.h>
#include <tsre/fileFunctions/TS.h>
#include <QTemporaryDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QProcess>
#include <QDateTime>
#include <QtEndian>
#include <stdexcept>
namespace {
void check(bool ok, const QString &message) {
    if (!ok)
        throw std::runtime_error(message.toStdString());
}
void put(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    check(f.open(QIODevice::WriteOnly), "write fixture " + path);
    check(f.write(bytes) == bytes.size(), "fixture bytes");
}
QByteArray get(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "read fixture " + path);
    return f.readAll();
}
QByteArray word(quint32 n) {
    QByteArray b(4, Qt::Uninitialized);
    qToLittleEndian(n, b.data());
    return b;
}
QByteArray wide(const QString &s) {
    QByteArray b;
    for (auto c : s) {
        char v[2];
        qToLittleEndian<quint16>(c.unicode(), v);
        b.append(v, 2);
    }
    return b;
}
QByteArray string(const QString &s) {
    QByteArray b(2, Qt::Uninitialized);
    qToLittleEndian<quint16>(s.size(), b.data());
    return b + wide(s);
}
QByteArray block(TS::TokenId id, QByteArray payload) {
    payload.prepend(char(0));
    return word(id) + word(payload.size()) + payload;
}
QByteArray binary(QByteArray body) { return QByteArray("SIMISA@@@@@@@@@@JINX0s1b________") + body; }
QByteArray zip(const QByteArray &plain) {
    return QByteArray("SIMISA@F") + word(plain.size() - 16) + "@@@@" +
           qCompress(plain.mid(16)).mid(4);
}
QMap<QString, QByteArray> snapshot(const QString &root) {
    QMap<QString, QByteArray> result;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const auto f = it.fileInfo();
        result[QDir(root).relativeFilePath(f.filePath()) + (f.isDir() ? "/" : "")] =
            f.isDir() ? QByteArray() : get(f.filePath());
    }
    return result;
}
const QByteArray shape =
    binary(block(TS::shape, block(TS::images, word(1) + block(TS::image, string("Bark.ace")))));
int command(const QStringList &args) {
    QProcess p;
    p.start(QCoreApplication::applicationFilePath(), QStringList{"--command"} + args);
    check(p.waitForFinished(30000), "execution CLI timeout");
    return p.exitCode();
}
void fixture(const QString &root, const QString &route = "RouteOne") {
    const auto base = root + "/routes/" + route;
    put(base + '/' + route + ".trk", "Tr_RouteFile ( Name ( RouteOne ) )");
    put(base + "/world/test.w", "Tr_WorldFile ( Static ( FileName ( Tree.s ) ) )\r\n");
    put(base + "/shapes/TREE.s", shape);
    put(base + "/textures/bark.ace", "opaque ACE bytes\0unchanged");
}
} // namespace
void runExecutionTests() {
    QString error;
    QByteArray output;
    const QByteArray plain = "SIMISA@@@@@@@@@@JINX0w1t______\r\nTr_WorldFile ( comment ( untouched "
                             ") Static ( FileName ( \"tree.s\" ) ) )\r\n";
    const QByteArray expected = QByteArray(plain).replace("tree.s", "LargeTree.s");
    auto le = QByteArray("\xff\xfe", 2) + wide(QString::fromUtf8(plain));
    auto be = le;
    for (int i = 0; i < be.size(); i += 2)
        std::swap(be[i], be[i + 1]);
    auto bom = QByteArray("\xef\xbb\xbf", 3) + plain;
    auto latin = plain;
    latin.replace("untouched", QByteArray("caf\xe9", 4));
    const auto wideZip = QByteArray("\xff\xfe", 2) + wide("SIMISA@F") + word(le.size() - 34) +
                         wide("@@@@@@") + qCompress(le.mid(34)).mid(4);
    for (const auto &bytes : {plain, zip(plain), le, be, bom, latin, wideZip}) {
        const auto doc = ContentCase::inspectDocument(bytes, "w");
        check(doc.valid && doc.fields.size() == 1, "patch fixture parsed");
        const bool patched = ContentCase::patchDocument(
            bytes, "w", {{0, 0, "tree.s", "LargeTree.s"}}, output, error);
        check(patched, "text patch (" + QString::fromLatin1(bytes.left(5).toHex()) + ", " +
                           doc.encoding + ", compressed=" + QString::number(doc.compressed) +
                           "): " + error);
        const auto result = ContentCase::inspectDocument(output, "w");
        check(result.fields[0].values[0].text == "LargeTree.s", "edited text scalar");
        check(result.compressed == doc.compressed && result.encoding == doc.encoding,
              "text encoding/compression preserved");
        if (bytes == plain)
            check(output == expected, "all unrelated plain text bytes preserved");
        if (bytes == latin)
            check(output == QByteArray(latin).replace("tree.s", "LargeTree.s"),
                  "legacy bytes preserved");
        if (bytes == bom)
            check(output == QByteArray(bom).replace("tree.s", "LargeTree.s"), "UTF8 BOM preserved");
    }
    const auto originalBinary =
        binary(block(TS::shape, block(TS::shape_header, word(2) + word(3) + word(0x12345678)) +
                                    block(TS::images, word(2) + block(TS::image, string("A.ace")) +
                                                          block(TS::image, string("B.ace")))));
    const auto expectedBinary = binary(
        block(TS::shape, block(TS::shape_header, word(2) + word(3) + word(0x12345678)) +
                             block(TS::images, word(2) + block(TS::image, string("LongerA.ace")) +
                                                   block(TS::image, string("b.ace")))));
    for (const auto &bytes : {originalBinary, zip(originalBinary)}) {
        check(ContentCase::patchDocument(bytes, "s",
                                         {{0, 0, "A.ace", "LongerA.ace"}, {1, 0, "B.ace", "b.ace"}},
                                         output, error),
              "binary patch: " + error);
        if (bytes == originalBinary)
            check(output == expectedBinary,
                  "binary ancestors resized; every unrelated payload byte preserved");
        if (bytes != originalBinary) {
            QByteArray size(4, Qt::Uninitialized);
            qToBigEndian<quint32>(qFromLittleEndian<quint32>(output.constData() + 8), size.data());
            check(qUncompress(size + output.mid(16)) == expectedBinary.mid(16),
                  "compressed binary exact payload");
        }
    }
    check(!ContentCase::patchDocument(plain, "w", {{0, 0, "WRONG.s", "Tree.s"}}, output, error),
          "stale scalar rejected");
    const auto sd = QByteArray("SIMISA@@@@@@@@@@JINX0t1b________") +
                    block(TS::shape, string("Tree.s") + block(TS::ESD_Detail_Level, word(3)));
    const auto editedSd =
        QByteArray("SIMISA@@@@@@@@@@JINX0t1b________") +
        block(TS::shape, string("LongerTree.s") + block(TS::ESD_Detail_Level, word(3)));
    check(ContentCase::patchDocument(sd, "sd", {{0, 0, "Tree.s", "LongerTree.s"}}, output, error) &&
              output == editedSd,
          "SD root string patch preserves metadata and updates block length");

    QTemporaryDir tmp;
    check(tmp.isValid(), "execution temporary directory");
    // A TRK scalar expands into several independent companion references.
    const auto companionRoot = tmp.path() + "/Companions";
    fixture(companionRoot);
    const auto companionBase = companionRoot + "/routes/Tutorial Route";
    put(companionBase + "/tutorial route.trk",
        "Tr_RouteFile ( FileName ( \"Tutorial Route\" ) )");
    for (const auto &suffix : {".tdb", ".rdb", ".tit", ".rit", ".ref"})
        put(companionBase + "/Tutorial Route" + suffix, "SIMISA@@@@@@@@@@JINX0t1t______\n");
    const auto companionInitial = snapshot(companionRoot);
    const auto companionPlan = ContentCase::scan(companionRoot, error);
    check(ContentCase::verifyReferences(companionPlan, companionPlan, {}, error),
          "unchanged route companion references verify independently: " + error);
    int companionCount = 0;
    const auto companionRefs = companionPlan["references"].toArray();
    for (int i = 0; i < companionRefs.size(); ++i) {
        auto ref = companionRefs[i].toObject();
        if (ref["kind"] != "route-companion-stem")
            continue;
        ++companionCount;
        check(ref["targetFileId"].toInt(-1) >= 0, "companion fixture resolves");
        auto damaged = companionPlan;
        auto refs = companionRefs;
        ref["targetFileId"] = -1;
        refs[i] = ref;
        damaged["references"] = refs;
        check(!ContentCase::verifyReferences(companionPlan, damaged, {}, error),
              "lost companion target still fails verification: " + ref["suffix"].toString());
    }
    check(companionCount == 4, "all four route companion suffixes covered");
    const auto companionResult = ContentCase::execute(
        companionRoot, {}, tmp.path() + "/CompanionJournal", error);
    check(!companionResult.isEmpty() && companionResult["actionsApplied"].toInt() > 0,
          "conversion with route companions passes postverification: " + error);
    check(ContentCase::rollback(companionRoot, companionResult["journal"].toString(), error),
          "route companion rollback: " + error);
    check(snapshot(companionRoot) == companionInitial, "route companion rollback exact");
    const auto root = tmp.path() + "/Pure";
    fixture(root);
    const auto initial = snapshot(root);
    const auto before = ContentCase::scan(root, error);
    const auto result = ContentCase::execute(root, before, tmp.path() + "/PureJournal", error);
    check(!result.isEmpty(), "pure execution: " + error);
    check(result["status"] == "complete",
          "pure repair complete: " + QString::fromUtf8(QJsonDocument(result).toJson()));
    check(get(root + "/ROUTES/ROUTEONE/SHAPES/Tree.s") == shape, "pure rename does not rewrite S");
    check(get(root + "/ROUTES/ROUTEONE/WORLD/test.w") == initial["routes/RouteOne/world/test.w"],
          "pure rename does not rewrite W");
    check(ContentCase::scan(root, error)["operations"].toArray().isEmpty(),
          "second plan is idempotent");
    check(ContentCase::rollback(root, result["journal"].toString(), error),
          "pure rollback: " + error);
    check(snapshot(root) == initial, "rollback exact original names and bytes");
    check(ContentCase::rollback(root, result["journal"].toString(), error), "rollback idempotent");
    const auto editedRoot = tmp.path() + "/Edited";
    fixture(editedRoot);
    put(editedRoot + "/routes/RouteOne/shapes/TREE.sd",
        "SIMISA@@@@@@@@@@JINX0t1t______\nShape ( tReE.s ESD_Detail_Level ( 0 ) )\n");
    const auto editedInitial = snapshot(editedRoot);
    const auto edited = ContentCase::execute(editedRoot, {}, tmp.path() + "/EditJournal", error);
    check(!edited.isEmpty() && edited["status"] == "complete",
          "conflicting incoming spellings: " + error +
              QString::fromUtf8(QJsonDocument(edited).toJson()));
    const auto journal = QJsonDocument::fromJson(get(edited["journal"].toString())).object();
    int replacements = 0;
    for (const auto &v : journal["actions"].toArray())
        replacements += v.toObject()["kind"] == "replace";
    check(replacements > 0, "conflicting spellings require a preservation edit");
    QMap<QString, qint64> modifiedTimes;
    QDirIterator modifiedFiles(editedRoot, QDir::Files, QDirIterator::Subdirectories);
    while (modifiedFiles.hasNext()) {
        modifiedFiles.next();
        modifiedTimes[QDir(editedRoot).relativeFilePath(modifiedFiles.filePath()).toLower()] =
            modifiedFiles.fileInfo().lastModified().toMSecsSinceEpoch();
    }
    for (const auto &v : journal["actions"].toArray()) {
        const auto action = v.toObject();
        if (action["kind"] == "replace")
            check(modifiedTimes.value(action["from"].toString().toLower()) ==
                      action["modifiedMs"].toVariant().toLongLong(),
                  "edited file retains original millisecond modification time");
    }
    check(ContentCase::rollback(editedRoot, edited["journal"].toString(), error),
          "edited rollback: " + error);
    check(snapshot(editedRoot) == editedInitial, "edited files restored byte for byte");
    const auto trainRoot = tmp.path() + "/Train";
    put(trainRoot + "/trains/consists/Test.con",
        "Train ( TrainCfg ( Test Engine ( EngineData ( Loco PRODUCT ) ) ) )");
    put(trainRoot + "/trains/trainset/Product/Loco.eng", "Wagon ( Loco )");
    const auto trains = ContentCase::execute(trainRoot, {}, tmp.path() + "/TrainJournal", error);
    check(!trains.isEmpty() && trains["status"] == "complete",
          "two-scalar vehicle reference: " + error);
    const auto trainDoc =
        ContentCase::inspectDocument(get(trainRoot + "/TRAINS/CONSISTS/Test.con"), "con");
    check(trainDoc.fields[0].values[0].text == "Loco" &&
              trainDoc.fields[0].values[1].text == "Product",
          "vehicle name and folder edited independently");
    const auto staleRoot = tmp.path() + "/Stale";
    fixture(staleRoot);
    const auto stale = ContentCase::scan(staleRoot, error);
    put(staleRoot + "/routes/RouteOne/shapes/TREE.s", QByteArray(shape) + ' ');
    const auto changed = snapshot(staleRoot);
    check(ContentCase::execute(staleRoot, stale, tmp.path() + "/StaleJournal", error).isEmpty(),
          "stale plan refused");
    check(snapshot(staleRoot) == changed, "stale refusal changes no source");
    for (int stop : {1, 2, 7}) {
        const auto interruptedRoot = tmp.path() + "/Interrupted" + QString::number(stop);
        fixture(interruptedRoot);
        const auto saved = snapshot(interruptedRoot);
        const auto interrupted = ContentCase::execute(
            interruptedRoot, {}, tmp.path() + "/InterruptedJournal" + QString::number(stop), error,
            {}, [&](int step) { return step < stop; });
        check(interrupted["status"] == "interrupted", "interruption fixture: " + error);
        check(ContentCase::execute(interruptedRoot, {},
                                   tmp.path() + "/ForbiddenJournal" + QString::number(stop), error)
                  .isEmpty(),
              "unfinished transaction prevents another conversion");
        check(ContentCase::rollback(interruptedRoot, interrupted["journal"].toString(), error),
              "interrupted rollback: " + error);
        check(snapshot(interruptedRoot) == saved, "interrupted move recovery exact");
    }

    const auto tornRoot = tmp.path() + "/TornLog";
    fixture(tornRoot);
    const auto tornInitial = snapshot(tornRoot);
    const auto torn = ContentCase::execute(tornRoot, {}, tmp.path() + "/TornJournal", error, {},
                                           [](int) { return false; });
    check(torn["status"] == "interrupted", "torn journal fixture");
    const auto eventsPath = QFileInfo(torn["journal"].toString()).absolutePath() + "/events.jsonl";
    QByteArray startedOnly;
    for (const auto &line : get(eventsPath).split('\n'))
        if (line.contains("\"phase\":\"started\""))
            startedOnly += line + '\n';
    put(eventsPath, startedOnly + "{\"step\":");
    check(ContentCase::rollback(tornRoot, torn["journal"].toString(), error),
          "recover completed move without durable done event: " + error);
    check(snapshot(tornRoot) == tornInitial,
          "torn event tail safely discarded; started move reversed");
    // Extra files after interruption must be reported, never deleted as an
    // assumed writer temporary. Removing the obstruction allows retry.
    const auto extraRoot = tmp.path() + "/ExtraEntry";
    fixture(extraRoot);
    const auto extra = ContentCase::execute(extraRoot, {}, tmp.path() + "/ExtraJournal", error, {},
                                            [](int) { return false; });
    check(extra["status"] == "interrupted", "extra-entry interruption");
    put(extraRoot + "/unexpected.tmp", "might belong to the user");
    check(!ContentCase::rollback(extraRoot, extra["journal"].toString(), error),
          "unexpected recovery entry reported");
    check(get(extraRoot + "/unexpected.tmp") == "might belong to the user",
          "unknown entry is never removed by recovery");
    check(QFile::remove(extraRoot + "/unexpected.tmp"), "remove own fixture obstruction");
    check(ContentCase::rollback(extraRoot, extra["journal"].toString(), error),
          "retry recovery after obstruction removal");
    const auto racingRoot = tmp.path() + "/Racing";
    fixture(racingRoot);
    auto racingExpected = snapshot(racingRoot);
    racingExpected["routes/RouteOne/textures/bark.ace"] = "user change during conversion";
    const auto racing = ContentCase::execute(
        racingRoot, {}, tmp.path() + "/RacingJournal", error, {}, [&](int step) {
            if (step == 1)
                put(racingRoot + "/routes/RouteOne/textures/bark.ace",
                    "user change during conversion");
            return true;
        });
    check(racing.isEmpty(), "concurrent change aborts conversion");
    check(snapshot(racingRoot) == racingExpected,
          "automatic rollback preserves concurrent change to an unstarted file");
    const auto sharedRoot = tmp.path() + "/Shared";
    put(sharedRoot + "/global/shapes/GLOBALTREE.s", shape);
    for (const auto &route : {QString("A"), QString("B")}) {
        const auto base = sharedRoot + "/routes/" + route;
        put(base + '/' + route + ".trk", "Tr_RouteFile ( Name ( Route ) )");
        put(base + "/world/tile.w", "Tr_WorldFile ( TrackObj ( FileName ( GlobalTree.s ) ) )");
        put(base + "/textures/" + (route == "A" ? "BARK.ace" : "bark.ace"),
            "different texture content: " + route.toUtf8());
        put(base + "/textures/winter/bARK.ace", "seasonal ACE: " + route.toUtf8());
        put(base + "/textures/winter/BaRk.dds", "seasonal DDS: " + route.toUtf8());
    }
    const auto sharedInitial = snapshot(sharedRoot);
    const auto shared = ContentCase::execute(sharedRoot, {}, tmp.path() + "/SharedJournal", error);
    check(!shared.isEmpty() && shared["status"] == "complete",
          "shared shape and seasonal/DDS coordination: " + error +
              QString::fromUtf8(QJsonDocument(shared).toJson()));
    check(ContentCase::scan(sharedRoot, error)["operations"].toArray().isEmpty(),
          "shared routes reach a stable exact-case plan");
    check(ContentCase::rollback(sharedRoot, shared["journal"].toString(), error),
          "shared graph rollback");
    check(snapshot(sharedRoot) == sharedInitial, "shared route asset versions preserved");
    const auto modifiedRoot = tmp.path() + "/Modified";
    fixture(modifiedRoot);
    const auto converted =
        ContentCase::execute(modifiedRoot, {}, tmp.path() + "/ModifiedJournal", error);
    check(!converted.isEmpty(), "modified fixture converted: " + error);
    put(modifiedRoot + "/ROUTES/ROUTEONE/TEXTURES/Bark.ace", "user edited texture");
    const auto modified = snapshot(modifiedRoot);
    check(!ContentCase::rollback(modifiedRoot, converted["journal"].toString(), error),
          "rollback refuses later user edit");
    check(snapshot(modifiedRoot) == modified, "failed rollback does not overwrite user edit");

    const auto cliRoot = tmp.path() + "/Cli";
    fixture(cliRoot);
    const auto cliInitial = snapshot(cliRoot);
    const auto planPath = tmp.path() + "/cli-plan.json", cliJournal = tmp.path() + "/CliJournal";
    check(command({"--contentcase", cliRoot, "--verify"}) == 1,
          "CLI verification detects spelling repairs needed");
    check(command({"--contentcase", cliRoot, "--plan", planPath}) == 0, "CLI dry-run exports plan");
    check(snapshot(cliRoot) == cliInitial, "CLI dry-run is still read-only");
    check(command({"--contentcase", cliRoot, "--apply", planPath, "--journal", cliJournal}) == 0,
          "CLI saved-plan apply");
    const auto cliApplied = snapshot(cliRoot);
    check(command({"--contentcase", cliRoot, "--verify"}) == 0,
          "CLI strict verification after conversion");
    check(command({"--contentcase", cliRoot, "--rollback", cliJournal + "/journal.json"}) == 0,
          "CLI rollback");
    check(snapshot(cliRoot) == cliInitial, "CLI rollback exact bytes and names");
    check(command({"--contentcase", cliRoot, "--journal", tmp.path() + "/DirectJournal"}) == 0,
          "CLI direct repair needs no exported plan");
    check(snapshot(cliRoot) == cliApplied,
          "direct and saved-plan execution produce identical output");
    const auto partialRoot = tmp.path() + "/Partial";
    fixture(partialRoot);
    fixture(partialRoot, "Broken");
    put(partialRoot + "/routes/Broken/shapes/TREE.s", "broken shape");
    const auto broken = get(partialRoot + "/routes/Broken/shapes/TREE.s");
    const auto partial =
        ContentCase::execute(partialRoot, {}, tmp.path() + "/PartialJournal", error);
    check(!partial.isEmpty() && partial["status"] == "partial",
          "broken component isolation: " + error);
    check(get(partialRoot + "/routes/Broken/shapes/TREE.s") == broken,
          "broken component left untouched");
    check(QFileInfo::exists(partialRoot + "/routes/ROUTEONE/SHAPES/Tree.s"),
          "independent route repaired despite isolated error");
}
