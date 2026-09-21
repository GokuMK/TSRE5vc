#include "QuadTreeRecoveryTestSuite.h"
#include <tsre/world/QuadTree.h>
#ifndef TSRE_QUADTREE_CORE_ONLY
#include <tsre/world/TerrainLibQt.h>
#include <tsre/world/Terrain.h>
#include <settings/SettingsManager.h>
#include <routeEditor/ErrorMessageProperties.h>
#include <routeEditor/ErrorMessagesWindow.h>
#include <QPushButton>
#endif
#include <tsre/world/TerrainInfo.h>
#include <tsre/world/TFile.h>
#include <tsre/Game.h>
#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>
#include <QTemporaryDir>
#include <QDirIterator>
#include <QFile>
#include <QDebug>
#include <QElapsedTimer>
#include <QScopedValueRollback>
#include <QPoint>
#include <cmath>

namespace {
bool write(const QString &path, const QByteArray &bytes) {
    QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QByteArray read(const QString &path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
}
int TsreTests::runQuadTreeRecoverySuite(bool verbose, const QString &corpus) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) {
        if (ok) ++passed; else ++failed;
        if (!ok || verbose) qInfo() << "[tests:quadtree-recovery]" << (ok ? "PASS" : "FAIL") << name;
    };
    QTemporaryDir temp;
    QScopedValueRollback<QString> root(Game::root, temp.path()), route(Game::route, QString("recovery"));
    QScopedValueRollback<bool> writable(Game::writeEnabled, true);
#ifndef TSRE_QUADTREE_CORE_ONLY
    QScopedValueRollback<TerrainLib*> library(Game::terrainLib);
#endif
    const QString path = temp.filePath("ROUTES/recovery");
    QDir().mkpath(path + "/TILES");
    QDir().mkpath(path + "/LO_TILES");
    QString error;
#ifndef TSRE_QUADTREE_CORE_ONLY
    auto &settings = SettingsManager::instance();
    const auto oldUse = settings.runtimeValue("core.advanced.useQuadTree");
    const auto oldAuto = settings.runtimeValue("core.route.validation.autoFix");
    settings.setSessionValue("core.advanced.useQuadTree", true);
    settings.setSessionValue("core.route.validation.autoFix", false);
#endif

    // Every TD-supported footprint, signs, poles and valid zero-valued filename ID.
    for (int level = 1; level <= 256; level *= 2) {
        for (int x : {-16384, -512, 0, 16384-level}) for (int y : {-16384, -512, 0, 16384-level}) {
            QuadTree qt;
            check(qt.insertTile(x, y, level), "insert size-aware tile");
            int dx, dy, dl;
            check(QuadTree::decodeTileName(qt.getMyName(x, y), dx, dy, dl) &&
                  dx == x && dy == y && dl == level, "filename placement round trip");
            check(qt.getMyNameId(x, y) != 0, "populated identity is nonzero");
            TerrainInfo info; qt.fillTerrainInfo(x, y, &info);
            check(info.cx == x && info.cy == y && info.level == level, "lookup footprint");
        }
    }
    QuadTree fixture;
    QuadTree zeroNames;
    for (int level = 1; level <= 256; level *= 2)
        zeroNames.insertTile(-16384, 16384-level, level);
    QSet<unsigned int> zeroIds;
    for (int level = 1; level <= 256; level *= 2)
        zeroIds.insert(zeroNames.getMyNameId(-16384, 16384-level));
    check(zeroIds.size() == 9 && !zeroIds.contains(0), "nested zero-name cache identities remain distinct");
    check(!fixture.insertTile(1, 1, 2) && !fixture.insertTile(0, 0, 3) &&
          !fixture.insertTile(32768, 0, 1), "reject unsupported insertion");
    fixture.insertTile(0, 0, 1);
    fixture.insertTile(2, 0, 2);
    fixture.insertTile(-32, -32, 32);
    for (const auto &entry : {QPoint(0,0), QPoint(2,0), QPoint(-32,-32)}) {
        TerrainInfo info; fixture.fillTerrainInfo(entry.x(), entry.y(), &info);
        TFile file; file.initNew(info.name, 256, 8 * info.level, 16);
        check(file.save(path + "/TILES/" + info.name + ".t"), "write descriptor-only fixture");
    }
    check(!QDir(path + "/TD").exists(), "deferred insertion did not create TD");
    QuadTree recovered; QStringList issues; int count;
    check(recovered.reconstruct(path + "/TILES", issues, count) && count == 3 && issues.isEmpty(), "reconstruct mixed footprints without RAW");
    check(recovered.isTemporary() && !recovered.isModified(), "temporary excluded from ordinary save");
    check(!recovered.saveChecked(path + "/TD", error) && !QDir(path + "/TD").exists(), "temporary save refuses without side effects");
    recovered.adoptRecovery();
    check(recovered.isModified() && !QDir(path + "/TD").exists(), "adopt is memory only");
    check(recovered.saveChecked(path + "/TD", error) && !recovered.isModified(), "save creates TD and clears dirty state");
    QuadTree loaded;
    check(loaded.loadChecked(path + "/TD", error) == QuadTree::LoadStatus::Loaded, "saved tree parses");
    check(loaded.getMyName(3, 1) == fixture.getMyName(3, 1) && loaded.getMyName(-20, -20) == fixture.getMyName(-20, -20), "mixed-size loaded coverage");
    const QString tdPath = path + "/TD/" + QDir(path + "/TD").entryList({"*.td"}, QDir::Files).first();
    const QByteArray originalTd = read(tdPath);
    check(write(tdPath, originalTd.left(54)) && loaded.loadChecked(path + "/TD", error) == QuadTree::LoadStatus::Invalid,
          "truncated TD rejected without crash");
    check(write(tdPath, originalTd) && loaded.loadChecked(path + "/TD", error) == QuadTree::LoadStatus::Loaded,
          "restored TD loads");
    const int entries = loaded.td.size();
    for (int i = 1000; i < 1500; ++i) loaded.getMyNameId(i, i);
    check(loaded.td.size() == entries, "empty lookups do not allocate TD entries");
    const QString index = path + "/TD/td_idx.dat";
    const QByteArray original = read(index);
    check(write(index, "not a tree") && loaded.loadChecked(path + "/TD", error) == QuadTree::LoadStatus::Invalid &&
          loaded.getMyName(3, 1) == fixture.getMyName(3,1), "invalid load preserves prior tree");
    recovered.adoptRecovery();
    check(recovered.saveChecked(path + "/TD", error), "repair overwrites invalid index only on save");
    const auto backups = QDir(path + "/TD").entryList({"recovery-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
    bool backupFound = false;
    for (const auto &b : backups) backupFound |= read(path + "/TD/" + b + "/td_idx.dat") == "not a tree";
    check(backupFound, "repair backup preserves old index");
    check(write(temp.filePath("blocked"), "file"), "blocked-directory fixture");
    recovered.adoptRecovery();
    check(!recovered.saveChecked(temp.filePath("blocked/TD"), error) && recovered.isModified(), "failed save stays modified");
    Game::writeEnabled = false;
    check(!recovered.saveChecked(temp.filePath("readonly/TD"), error) && !QDir(temp.filePath("readonly")).exists(),
          "checked save honors global write-disable");
    Game::writeEnabled = true;

#ifndef TSRE_QUADTREE_CORE_ONLY
    // Reusable targeted presentation is non-modal, one-shot and selection-stable.
    auto *firstMessage = new ErrorMessage(ErrorMessage::Type_Info, ErrorMessage::Source_Editor, "Target message");
    ErrorMessagesLib::PushErrorMessage(firstMessage);
    check(!ErrorMessagesLib::ShowMessage(nullptr, nullptr), "null presentation request rejected");
    ErrorMessagesLib::RequestShowMessage(firstMessage, [] { return false; });
    check(!ErrorMessagesLib::ShowRequestedMessage(nullptr), "expired presentation discarded");
    ErrorMessagesLib::RequestShowMessage(firstMessage);
    ErrorMessagesLib::RequestShowMessage(firstMessage);
    check(ErrorMessagesLib::ShowRequestedMessage(nullptr), "deferred presentation opens target");
    auto *messageWindow = ErrorMessagesLib::GetWindow(nullptr);
    auto *messageList = messageWindow->findChild<QTreeWidget*>();
    auto isSelected = [&](ErrorMessage *message) {
        return messageList && messageList->currentItem() &&
            ErrorMessagesLib::ErrorMessages.value(messageList->currentItem()->type()) == message;
    };
    check(messageWindow->isVisible() && isSelected(firstMessage), "target row selected on opening");
    auto *newMessage = new ErrorMessage(ErrorMessage::Type_Info, ErrorMessage::Source_Editor, "Newer message");
    ErrorMessagesLib::PushErrorMessage(newMessage);
    check(isSelected(firstMessage), "incoming messages preserve selection");
    messageWindow->hide();
    check(!ErrorMessagesLib::ShowRequestedMessage(nullptr) && !messageWindow->isVisible(), "closing does not repeat consumed request");
    check(ErrorMessagesLib::ShowMessage(newMessage, nullptr) && isSelected(newMessage), "immediate presentation selects requested message");
    messageWindow->hide();

    // In-memory library recovery and E&M action lifecycle, no OpenGL needed.
    Game::route = "missing";
    const QString missing = temp.filePath("ROUTES/missing");
    QDir().mkpath(missing + "/TILES");
    for (const auto &f : QDir(path + "/TILES").entryList(QDir::Files))
        QFile::copy(path + "/TILES/" + f, missing + "/TILES/" + f);
    ErrorMessage *stale = nullptr;
    {
        TerrainLibQt lib; Game::terrainLib = &lib;
        const int before = ErrorMessagesLib::ErrorMessages.size();
        lib.loadQuadTree();
        check(ErrorMessagesLib::ErrorMessages.size() == before + 1, "missing tree reports once; absent distant terrain is normal");
        stale = ErrorMessagesLib::ErrorMessages.back();
        check(lib.getQuadTreeDetailed()->isTemporary() && lib.getQuadTreeDetailed()->getMyNameId(0,0), "missing tree usable immediately");
        QVector<QString> pending; lib.getUnsavedInfo(pending);
        lib.save();
        check(pending.isEmpty() && !QDir(missing + "/TD").exists(), "ignored Fix does not save tree");
        Game::writeEnabled = false;
        check(ErrorMessagesLib::ShowRequestedMessage(nullptr) && isSelected(stale), "missing QuadTree shown even with writes disabled");
        auto *windowFix = messageWindow->findChild<QPushButton*>("errorMessageFix");
        check(windowFix && !windowFix->isHidden() && !windowFix->isEnabled(), "targeted details expose disabled Fix in read-only route");
        messageWindow->hide();
        check(!stale->applyFix(error), "read-only Fix refused");
        Game::writeEnabled = true;
        ErrorMessageProperties properties(nullptr);
        properties.showMessage(stale);
        auto *fixButton = properties.findChild<QPushButton*>("errorMessageFix");
        check(fixButton && fixButton->isEnabled(), "E&M presents enabled Fix action");
        if (fixButton) fixButton->click();
        check(stale->type == ErrorMessage::Type_AutoFix && !stale->fix, "E&M button adopts current tree");
        lib.getUnsavedInfo(pending);
        check(pending.size() == 1 && !QDir(missing + "/TD").exists(), "Fix listed in ordinary unsaved dialog, no disk writes");
        lib.save();
        check(QFile::exists(missing + "/TD/td_idx.dat") && !lib.getQuadTreeDetailed()->isModified(), "ordinary Save persists adopted tree");
        check(!stale->applyFix(error), "repeated Fix harmless");
        // Disabled saved lookup reconstructs without parsing the corrupt index.
        settings.setSessionValue("core.advanced.useQuadTree", false);
        check(Terrain::SaveEmpty(fixture.getMyName(0,0), 512, 4, 16, false, true), "create real 512/4 payload in test route");
        check(write(missing + "/TD/td_idx.dat", "deliberately ignored"), "ignored metadata fixture");
        lib.loadQuadTree();
        stale = ErrorMessagesLib::ErrorMessages.back();
        check(!ErrorMessagesLib::ShowRequestedMessage(nullptr), "deliberately ignored existing tree does not request attention");
        check(lib.getQuadTreeDetailed()->getMyNameId(0,0) && lib.getQuadTreeDetailed()->isTemporary(), "disabled mode uses reconstructed common backend");
        auto *terrain = lib.getTerrainByXY(0,0,true);
        check(terrain && terrain->loaded && terrain->getSampleCount() == 512,
              "disabled mode loads high-resolution terrain through common backend");
        if (terrain && terrain->loaded) {
            lib.setHeight(0,0,0,0,17.0f);
            check(std::abs(lib.getHeight(0,0,0,0)-17.0f)<0.01f, "disabled mode edits common terrain heights");
        }
        lib.getQuadTreeDetailed()->addTile(10, 10); lib.save();
        check(read(missing + "/TD/td_idx.dat") == "deliberately ignored", "creation and Save leave ignored metadata untouched");
        lib.loadQuadTree();
        check(!stale->applyFix(error), "tree reload invalidates earlier Fix action");
        stale = ErrorMessagesLib::ErrorMessages.back();
    }
    check(!stale->applyFix(error), "destroyed route action is safe");
    settings.setSessionValue("core.advanced.useQuadTree", true);
    settings.setSessionValue("core.route.validation.autoFix", true);
    Game::route = "auto";
    QDir().mkpath(temp.filePath("ROUTES/auto/TILES"));
    for (const auto &f : QDir(path + "/TILES").entryList(QDir::Files))
        QFile::copy(path + "/TILES/" + f, temp.filePath("ROUTES/auto/TILES/" + f));
    {
        TerrainLibQt lib; Game::terrainLib = &lib; lib.loadQuadTree();
        check(lib.getQuadTreeDetailed()->isModified() && !QDir(temp.filePath("ROUTES/auto/TD")).exists(), "auto-fix only adopts; no startup write");
    }
    Game::route = "missing";
    {
        TerrainLibQt lib; Game::terrainLib = &lib; lib.loadQuadTree();
        check(!lib.getQuadTreeDetailed()->getMyNameId(0,0), "corrupt existing index not automatically replaced even with auto-fix");
        auto *message = ErrorMessagesLib::ErrorMessages.back();
        check(ErrorMessagesLib::ShowRequestedMessage(nullptr) && isSelected(message), "invalid tree selected after stale session requests discarded");
        messageWindow->hide();
        check(message->applyFix(error) && lib.getQuadTreeDetailed()->getMyNameId(0,0), "explicit Fix activates corrupt-tree replacement");
        check(read(missing + "/TD/td_idx.dat") == "deliberately ignored", "corrupt-tree Fix still does not write");
        stale = message;
    }
    // Valid empty index must not trigger automatic discovery of unpopulated payloads.
    QuadTree empty;
    check(empty.saveChecked(missing + "/TD", error), "empty index fixture");
    {
        TerrainLibQt lib; Game::terrainLib = &lib; const int before = ErrorMessagesLib::ErrorMessages.size();
        lib.loadQuadTree();
        check(!lib.getQuadTreeDetailed()->getMyNameId(0,0) && ErrorMessagesLib::ErrorMessages.size() == before,
              "valid empty tree preserved");
        check(!lib.getQuadTreeDistant()->isTemporary(), "no-distant route allows ordinary first distant tile creation");
    }
    settings.setSessionValue("core.advanced.useQuadTree", false);
    settings.setSessionValue("core.route.validation.autoFix", false);
    {
        TerrainLibQt lib; Game::terrainLib = &lib; lib.loadQuadTree();
        lib.setDistantAsCurrent();
        lib.saveEmpty(0,0);
        auto *message = ErrorMessagesLib::ErrorMessages.back();
        check(lib.getQuadTreeDistant()->isTemporary() && lib.getQuadTreeDistant()->getMyNameId(0,0)
              && !QFile::exists(missing + "/TD/lo_td_idx.dat"), "disabled-mode first distant tile stays temporary");
        check(message->applyFix(error) && lib.getQuadTreeDistant()->isModified(), "first distant tile offers Fix");
    }
    settings.setSessionValue("core.advanced.useQuadTree", oldUse);
    settings.setSessionValue("core.route.validation.autoFix", oldAuto);
    check(!ErrorMessagesLib::ShowRequestedMessage(nullptr), "destroyed route presentation request is discarded");
    delete messageWindow;
#endif

    if (!corpus.isEmpty()) {
        // Read-only real-route check; no corpus saves or recovery adoption.
        QElapsedTimer timer; timer.start(); int indexes = 0, descriptors = 0;
        QDirIterator it(corpus, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString file = it.next(); const auto info = it.fileInfo();
            if (info.fileName().compare("td_idx.dat", Qt::CaseInsensitive) == 0 ||
                info.fileName().compare("lo_td_idx.dat", Qt::CaseInsensitive) == 0) {
                QuadTree qt(info.fileName().startsWith("lo_", Qt::CaseInsensitive));
                const bool ok = qt.loadChecked(info.absolutePath(), error) == QuadTree::LoadStatus::Loaded;
                check(ok, "corpus index load"); if (!ok) qWarning() << file << error;
                ++indexes;
            }
            if (!info.suffix().compare("t", Qt::CaseInsensitive)) {
                int x,y,l; TFile::LayoutInfo layout;
                const bool ok = QuadTree::decodeTileName(info.completeBaseName(),x,y,l) && TFile::readLayoutInfo(file,layout)
                    && std::abs(double(layout.samples)*layout.spacing-l*2048.0)<0.01;
                check(ok, "corpus filename-layout match"); if (!ok) qWarning() << file;
                ++descriptors;
            }
        }
        qInfo() << "[tests:quadtree-recovery] corpus indexes" << indexes << "descriptors" << descriptors << "ms" << timer.elapsed();
        if (QDir(corpus + "/TILES").exists()) {
            qint64 total = 0;
            for (int run = 0; run < 3; ++run) {
                QuadTree qt; QStringList problems; int rebuilt = 0;
                timer.restart(); qt.reconstruct(corpus + "/TILES", problems, rebuilt); total += timer.nsecsElapsed();
                check(problems.isEmpty(), "corpus reconstruction has no skipped descriptors");
                qInfo() << "[tests:quadtree-recovery] reconstruct run" << run << "tiles" << rebuilt << "ms" << timer.elapsed();
            }
            qInfo() << "[tests:quadtree-recovery] reconstruct mean ms" << total / 3e6;
        }
    }
    qInfo() << "[tests:quadtree-recovery] passed" << passed << "failed" << failed;
    return failed ? 1 : 0;
}
