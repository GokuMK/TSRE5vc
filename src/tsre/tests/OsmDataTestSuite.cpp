#include <tsre/tests/OsmDataTestSuite.h>
#include <tsre/geo/osm/OsmConversionUi.h>
#include <tsre/geo/osm/OsmDirectory.h>
#include <QApplication>
#include <QDebug>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
#include <QFile>
#include <QTemporaryDir>

namespace TsreTests {

namespace {

// A small download near (lon, lat) in the Geofabrik layout.
bool writeDownload(const QString &path, double lon, double lat) {
    using namespace Osm;
    HeaderInfo h;
    h.bbox = Box::fromDegrees(lon - 0.5, lat - 0.5, lon + 0.5, lat + 0.5);
    h.requiredFeatures << "OsmSchema-V0.6" << "DenseNodes";
    h.optionalFeatures << "Sort.Type_then_ID";
    BlockBuilder nodes, ways;
    const Tag none[] = {{"", ""}};
    for (int i = 0; i < 4; ++i) nodes.addNode(i + 1, Location::fromDegrees(lon + i * 0.001, lat), none, 0);
    const Tag rail[] = {{"railway", "rail"}};
    const int64_t refs[] = {1, 2, 3, 4};
    ways.addWay(7, rail, 1, refs, nullptr, 4);
    std::string a, b;
    nodes.build(a); ways.build(b);
    const std::string bytes = encodeBlobFrame("OSMHeader", encodeHeaderBlock(h), {}, 6) + encodeBlobFrame("OSMData", a, {}, 6) + encodeBlobFrame("OSMData", b, {}, 6);
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes.data(), qint64(bytes.size())) == qint64(bytes.size());
}

// Presses the dialog button with the given role once the modal question appears; optionally
// saves a snapshot (TSRE_OSM_UI_SNAPSHOTS=<dir>) for visual review.
void answerQuestion(QDialogButtonBox::ButtonRole role, const QString &snapshot, bool &seen) {
    QTimer::singleShot(200, [=, &seen] {
        QWidget *w = QApplication::activeModalWidget();
        auto *box = w ? w->findChild<QDialogButtonBox *>() : nullptr;
        if (!box) return;
        seen = true;
        const QString dir = qEnvironmentVariable("TSRE_OSM_UI_SNAPSHOTS");
        if (!dir.isEmpty()) w->grab().save(dir + "/" + snapshot);
        for (QAbstractButton *b : box->buttons())
            if (box->buttonRole(b) == role) { b->click(); return; }
    });
}

}

int runOsmDataSuite(bool verbose) {
    using namespace Osm;
    int passed = 0, failed = 0;
    auto check = [&](bool condition, const char *name) {
        if (condition) { ++passed; if (verbose) qInfo() << "[tests:osm-data] PASS" << name; }
        else { ++failed; qWarning() << "[tests:osm-data] FAIL" << name; }
    };
    QTemporaryDir tmp;
    check(writeDownload(tmp.filePath("gdansk.osm.pbf"), 18.6, 54.35) && writeDownload(tmp.filePath("warszawa.osm.pbf"), 21.0, 52.23),
          "write downloads");
    const Box tczew = Box::fromDegrees(18.78, 54.08, 18.81, 54.10), far = Box::fromDegrees(10.0, 40.0, 10.1, 40.1);

    EnsureResult none = ensureConverted(nullptr, tmp.filePath("missing"), false, tczew, EnsureMode::AcceptAll);
    check(!none.hasDirectory && none.converted == 0, "no directory: nothing to do");
    EnsureResult nothing = ensureConverted(nullptr, tmp.path(), false, far, EnsureMode::AcceptAll);
    check(nothing.hasDirectory && nothing.converted == 0, "an area no download covers converts nothing");
    EnsureResult first = ensureConverted(nullptr, tmp.path(), false, tczew, EnsureMode::AcceptAll);
    check(first.converted == 1 && first.errors.isEmpty() && QFile::exists(tmp.filePath("gdansk.tsre.osm.pbf"))
              && !QFile::exists(tmp.filePath("warszawa.tsre.osm.pbf")) && QFile::exists(tmp.filePath("gdansk.osm.pbf")),
          "first use converts only the download covering the area and keeps it");
    EnsureResult again = ensureConverted(nullptr, tmp.path(), false, tczew, EnsureMode::AcceptAll);
    check(again.converted == 0 && again.declined == 0, "a converted area is not converted again");
    const Box warsaw = Box::fromDegrees(20.9, 52.1, 21.1, 52.3);
    EnsureResult del = ensureConverted(nullptr, tmp.path(), true, warsaw, EnsureMode::AcceptAll);
    check(del.converted == 1 && QFile::exists(tmp.filePath("warszawa.tsre.osm.pbf")) && !QFile::exists(tmp.filePath("warszawa.osm.pbf")),
          "delete mode removes the download after converting");
    OsmDirectory dir;
    QString error;
    check(dir.scan(tmp.path(), error) && dir.convertedFiles().size() == 2 && dir.pendingConversions().empty(), "directory is fully converted");
    check(availableMemoryBytes() >= 0, "available memory query does not fail");

    // The question: "Not now" postpones for the session, "Convert now" converts.
    QTemporaryDir ui;
    writeDownload(ui.filePath("pomorskie.osm.pbf"), 18.6, 54.35);
    bool seen = false;
    answerQuestion(QDialogButtonBox::RejectRole, "osm-question.png", seen);
    EnsureResult later = ensureConverted(nullptr, ui.path(), false, tczew, EnsureMode::Ask);
    check(seen && later.declined == 1 && later.converted == 0 && !QFile::exists(ui.filePath("pomorskie.tsre.osm.pbf")), "declining the question converts nothing");
    seen = false;
    EnsureResult silent = ensureConverted(nullptr, ui.path(), false, tczew, EnsureMode::Ask);
    check(!seen && silent.declined == 1 && silent.converted == 0, "a declined file is not offered again in the same session");
    QTemporaryDir ui2;
    writeDownload(ui2.filePath("pomorskie.osm.pbf"), 18.6, 54.35);
    answerQuestion(QDialogButtonBox::AcceptRole, "osm-question-accept.png", seen);
    EnsureResult accepted = ensureConverted(nullptr, ui2.path(), false, tczew, EnsureMode::Ask);
    check(seen && accepted.converted == 1 && QFile::exists(ui2.filePath("pomorskie.tsre.osm.pbf")), "accepting the question converts");

    qInfo().noquote() << QStringLiteral("[tests:osm-data] %1 passed, %2 failed").arg(passed).arg(failed);
    return failed ? 1 : 0;
}

}
