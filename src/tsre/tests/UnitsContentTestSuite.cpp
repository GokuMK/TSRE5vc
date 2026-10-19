#include <tsre/tests/UnitsContentTestSuite.h>

#include <QDirIterator>
#include <QFileInfo>
#include <tsre/Game.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/units/UnitConversion.h>

// The files are found by their extension in any folder, any case: content
// spells TRAINS, Trains and trains (the libraries' loadAll take MSTS's).
int TsreTests::runUnitsContentSuite(bool verbose) {
    TokenTest::Suite test{"[tests:units-content]", verbose};
    QStringList engines, consists, activities;
    QDirIterator it(Game::root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == "eng" || suffix == "wag")
            engines << path;
        else if (suffix == "con")
            consists << path;
        else if (suffix == "act")
            activities << path;
    }
    EngLib *engs = new EngLib();
    Game::currentEngLib = engs;
    for (const QString &path : engines)
        engs->addEng(QFileInfo(path).path(), QFileInfo(path).fileName());
    int loaded = 0;
    for (const auto &entry : engs->eng)
        loaded += entry.second != nullptr && entry.second->loaded == 1 ? 1 : 0;
    test.check(engines.isEmpty() || loaded > 0, QString("rolling stock read: %1 of %2").arg(loaded).arg(engines.size()));
    for (const QString &path : consists)
        ConLib::addCon(QFileInfo(path).path(), QFileInfo(path).fileName());
    test.check(consists.isEmpty() || !ConLib::con.empty(), QString("consists read: %1").arg(ConLib::con.size()));
    for (const QString &path : activities)
        ActLib::AddAct(QFileInfo(path).path(), QFileInfo(path).fileName());
    test.check(activities.isEmpty() || !ActLib::Act.isEmpty(), QString("activities read: %1").arg(ActLib::Act.size()));
    // The separations of the global tsection.dat's shapes with parallel
    // tracks and lanes (the flex tool's presets), the most used first.
    TSectionDAT tsection(false, false);
    for (const bool road : {false, true}) {
        QStringList listed;
        const QVector<TSectionDAT::Separation> separations = tsection.parallelSeparations(road);
        for (int i = 0; i < separations.size() && i < 8; ++i)
            listed << QString("%1 (%2 shapes: %3)")
                          .arg(Units::format(separations[i].metres, Units::Quantity::Length, Units::System::Metric, 4))
                          .arg(separations[i].shapes)
                          .arg(QStringList(separations[i].families.mid(0, 4)).join(" "));
        qInfo().noquote() << "[tests:units-content]" << (road ? "road lanes:" : "track separations:")
                          << listed.join(", ");
    }
    return test.finish();
}
