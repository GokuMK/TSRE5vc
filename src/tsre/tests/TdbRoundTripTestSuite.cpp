#include <tsre/tests/TdbRoundTripTestSuite.h>
#include <tsre/Game.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TSectionDAT.h>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

int TsreTests::runTdbRoundTripSuite(const QString &outputDirectory) {
    const QDir sourceRoute(Game::root + "/ROUTES/" + Game::route);
    const auto databases = sourceRoute.entryInfoList({"*.tdb"}, QDir::Files);
    if (databases.size() != 1 || outputDirectory.isEmpty()
            || QFileInfo::exists(outputDirectory)) {
        qWarning() << "tdb-roundtrip requires exactly one route TDB and a new --test-cases output directory";
        return 1;
    }
    const QString originalRoot = Game::root;
    const QString originalName = Game::routeName;
    const bool writeEnabled = Game::writeEnabled;
    const bool writeTdb = Game::writeTDB;
    const bool writeSession = Game::writeTDBSessionAllowed;
    Game::routeName = databases.first().completeBaseName();
    TSectionDAT sections(false, false);
    const bool hasSections = QFileInfo::exists(sourceRoute.filePath("tsection.dat"));
    if (hasSections && !sections.loadRoute(false)) {
        Game::routeName = originalName;
        return 1;
    }
    TDB database(&sections, false);
    database.loadTdb();
    if (!database.loaded || !database.sourceFileExists) {
        Game::routeName = originalName;
        return 1;
    }

    const QString output = QFileInfo(outputDirectory).absoluteFilePath();
    const QString raw = output + "/raw/ROUTES/" + Game::route;
    const QString precise = output + "/float32/ROUTES/" + Game::route;
    const QString saved = output + "/saved/ROUTES/" + Game::route;
    bool ok = QDir().mkpath(raw) && QDir().mkpath(precise) && QDir().mkpath(saved);
    auto capture = [&](const QString &path, int precision, auto serialize) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf16);
        stream.setGenerateByteOrderMark(true);
        stream.setRealNumberPrecision(precision);
        stream << "SIMISA@@@@@@@@@@JINX0T0t______\n\n";
        serialize(stream);
        stream.flush();
        return stream.status() == QTextStream::Ok;
    };
    if (ok) {
        // Nine significant digits distinguish every stored float32 value, so
        // the external comparator can separate parsing loss from formatting.
        ok = capture(precise + "/" + databases.first().fileName(), 9,
                     [&](QTextStream &stream) { database.saveToStream(stream); });
        if (hasSections)
            ok = capture(precise + "/tsection.dat", 9,
                         [&](QTextStream &stream) { sections.saveRouteToStream(stream); }) && ok;
        ok = capture(raw + "/" + databases.first().fileName(), 6,
                     [&](QTextStream &stream) { database.saveToStream(stream); }) && ok;
        if (hasSections)
            ok = capture(raw + "/tsection.dat", 6,
                         [&](QTextStream &stream) { sections.saveRouteToStream(stream); }) && ok;
    }
    if (ok) {
        // Only the fresh capture tree can be written by the production save path.
        Game::root = output + "/saved";
        Game::writeEnabled = Game::writeTDB = Game::writeTDBSessionAllowed = true;
        database.save();
        ok = QFileInfo(saved + "/" + databases.first().fileName()).size() > 0
                && (!hasSections || QFileInfo(saved + "/tsection.dat").size() > 0);
    }
    Game::root = originalRoot;
    Game::routeName = originalName;
    Game::writeEnabled = writeEnabled;
    Game::writeTDB = writeTdb;
    Game::writeTDBSessionAllowed = writeSession;
    qInfo() << "[tests:tdb-roundtrip] capture" << (ok ? "PASS" : "FAIL")
            << sourceRoute.absolutePath() << output;
    return ok ? 0 : 1;
}
