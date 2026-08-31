/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tests/TdbLoadTestSuite.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <tsre/Game.h>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/SpeedPostDAT.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TSectionDAT.h>

namespace {

bool writeFixture(const QString &path, const QByteArray &data) {
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(data) == data.size();
}

QByteArray readFixture(const QString &path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

} // namespace

int TsreTests::runTdbLoadSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    auto check = [&](bool condition, const char *name) {
        if(condition) {
            ++passed;
            if(verbose)
                qInfo() << "[tests:tdb-load] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:tdb-load] FAIL" << name;
        }
    };

    const QString originalRoot = Game::root;
    const QString originalRoute = Game::route;
    const QString originalRouteName = Game::routeName;
    const bool originalWriteEnabled = Game::writeEnabled;
    const bool originalWriteTdb = Game::writeTDB;
    const bool originalWriteSessionAllowed = Game::writeTDBSessionAllowed;

    QTemporaryDir temporaryDirectory;
    check(temporaryDirectory.isValid(), "temporary-directory");
    if(!temporaryDirectory.isValid())
        return 1;

    const QString routeDirectory = temporaryDirectory.path() + "/routes/empty";
    QDir().mkpath(routeDirectory);
    Game::root = temporaryDirectory.path();
    Game::route = "empty";
    Game::routeName = "empty";

    TSectionDAT tsection(false, false);
    TDB missingTdb(&tsection, false);
    missingTdb.loadTdb();
    check(missingTdb.loaded && !missingTdb.sourceFileExists,
          "missing-tdb-initializes-empty");
    check(missingTdb.sigCfg != NULL && missingTdb.sigCfg->loaded
          && !missingTdb.sigCfg->sourceFileExists,
          "missing-sigcfg-initializes-empty");
    check(missingTdb.speedPostDAT != NULL && missingTdb.speedPostDAT->loaded
          && !missingTdb.speedPostDAT->sourceFileExists,
          "missing-speedpost-initializes-empty");

    TDB missingRdb(&tsection, true);
    missingRdb.loadTdb();
    check(missingRdb.loaded && !missingRdb.sourceFileExists,
          "missing-rdb-initializes-empty");

    const QByteArray brokenDatabase =
            "SIMISA@@@@@@@@@@JINX0T0t______\n\nTrackDB (\n Serial ( 0 )\n";
    const QString tdbPath = routeDirectory + "/empty.tdb";
    check(writeFixture(tdbPath, brokenDatabase), "write-broken-tdb-fixture");
    TDB brokenTdb(&tsection, false);
    brokenTdb.loadTdb();
    check(!brokenTdb.loaded && brokenTdb.sourceFileExists,
          "existing-broken-tdb-is-not-empty");

    Game::writeEnabled = true;
    Game::writeTDB = true;
    Game::writeTDBSessionAllowed = true;
    const QByteArray beforeSave = readFixture(tdbPath);
    brokenTdb.save();
    check(readFixture(tdbPath) == beforeSave,
          "existing-broken-tdb-is-not-overwritten");

    const QString rdbPath = routeDirectory + "/empty.rdb";
    check(writeFixture(rdbPath, brokenDatabase), "write-broken-rdb-fixture");
    TDB brokenRdb(&tsection, true);
    brokenRdb.loadTdb();
    check(!brokenRdb.loaded && brokenRdb.sourceFileExists,
          "existing-broken-rdb-is-not-empty");
    const QByteArray rdbBeforeSave = readFixture(rdbPath);
    brokenRdb.save();
    check(readFixture(rdbPath) == rdbBeforeSave,
          "existing-broken-rdb-is-not-overwritten");

    const QByteArray brokenConfig =
            "SIMISA@@@@@@@@@@JINX0G0t______\n\nSignalShapes (\n";
    check(writeFixture(routeDirectory + "/sigcfg.dat", brokenConfig),
          "write-broken-sigcfg-fixture");
    SigCfg brokenSigCfg;
    check(!brokenSigCfg.loaded && brokenSigCfg.sourceFileExists,
          "existing-broken-sigcfg-is-not-missing");

    check(writeFixture(routeDirectory + "/speedpost.dat", brokenConfig),
          "write-broken-speedpost-fixture");
    SpeedPostDAT brokenSpeedPost;
    check(!brokenSpeedPost.loaded && brokenSpeedPost.sourceFileExists,
          "existing-broken-speedpost-is-not-missing");

    Game::root = originalRoot;
    Game::route = originalRoute;
    Game::routeName = originalRouteName;
    Game::writeEnabled = originalWriteEnabled;
    Game::writeTDB = originalWriteTdb;
    Game::writeTDBSessionAllowed = originalWriteSessionAllowed;

    qInfo() << "[tests:tdb-load] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
