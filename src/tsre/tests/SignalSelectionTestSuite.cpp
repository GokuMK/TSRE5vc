#include "SignalSelectionTestSuite.h"
#include "TokenTestSupport.h"
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <routeEditor/properties/PropertiesSignal.h>
#include <routeEditor/properties/SignalWindow.h>
#include <tsre/Game.h>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/SignalShape.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/ErrorMessage.h>
#include <tsre/world/objects/SignalObj.h>

int TsreTests::runSignalSelectionSuite(const QString &configurationPath, bool verbose) {
    TokenTest::Suite test{"[tests:signal-selection]", verbose};
    QTemporaryDir temp;
    test.check(temp.isValid(), "isolated route directory");
    if(!temp.isValid()) return test.finish();
    QDir().mkpath(temp.path() + "/ROUTES/signals");
    const QString target = temp.path() + "/ROUTES/signals/sigcfg.dat";
    if(configurationPath.isEmpty()){
        QFile file(target);
        const QByteArray fixture = R"(SIMISA@@@@@@@@@@JINX0t1t______
SignalShapes ( 1
 SignalShape ( "UKSemaphore2.s" "Home signal"
  SignalSubObjs ( 1
   SignalSubObj ( 0 "HEAD1" "Home arm"
    SigSubType ( SIGNAL_HEAD ) SignalFlags ( OPTIONAL DEFAULT )
    SigSubSType ( "UKSemHome" )
   )
  )
 )
)
)";
        if(!file.open(QIODevice::WriteOnly) || file.write(fixture) != fixture.size())
            return 1;
    } else if(!QFile::copy(configurationPath, target)){
        test.check(false, "copy supplied signal configuration");
        return test.finish();
    }
    QScopedValueRollback<QString> root(Game::root, temp.path());
    QScopedValueRollback<QString> route(Game::route, "signals");
    SigCfg config;
    test.check(config.loaded && !config.signalShapeById.isEmpty(), "load signal definitions");
    if(!config.loaded || config.signalShapeById.isEmpty()) return test.finish();
    TDB database(nullptr, false);
    database.sigCfg = &config;
    QScopedValueRollback<TDB*> trackDB(Game::trackDB, &database);
    PropertiesSignal properties;
    SignalObj signal;
    signal.type = "signal";
    signal.x = signal.y = 0;
    for(float &v : signal.position) v = 0;
    for(float &v : signal.qDirection) v = 0;
    signal.qDirection[3] = 1;
    auto mask = TokenTest::buffer(QByteArray(1, '\0') + TokenTest::uints({1}));
    signal.set(TS::SignalSubObj, mask.get());
    const auto hasText = [&](const QString &text) {
        for(auto *edit : properties.findChildren<QLineEdit*>())
            if(edit->text() == text) return true;
        return false;
    };
    const int definitionCount = config.signalShape.size();
    for(auto *definition : config.signalShapeById){
        const QString authoredName = definition->name;
        for(const auto &name : {definition->name, definition->name.toUpper(), definition->name.toLower()}){
            signal.fileName = name;
            signal.loadInit();
            signal.select(1);
            properties.showObj(&signal);
            // The real editor refreshes properties after selecting the object.
            properties.updateObj(&signal);
            test.check(hasText(name) && hasText(definition->desc), "select and refresh " + name);
            test.check(config.findSignalShape(name) == definition && signal.checkForErrors() == nullptr,
                       "object validation resolves the same signal definition");
            std::unique_ptr<Ref::RefItem> reference(signal.getRefInfo());
            test.check(reference->value == definition->listId,
                       "object initialization retains the definition for editing");
            test.check(signal.fileName == name && definition->name == authoredName,
                       "preserve authored filename spelling");
        }
    }
    signal.fileName = "MissingSignal.S";
    signal.loadInit();
    properties.showObj(&signal);
    properties.updateObj(&signal);
    auto *window = properties.findChild<SignalWindow*>();
    window->showObj(&signal);
    window->updateObj(&signal);
    int linkRequests = 0;
    QObject::connect(window, &SignalWindow::sendMsg, [&](QString, QString) { ++linkRequests; });
    window->setLink();
    test.check(linkRequests == 0, "missing definition cannot start signal linking");
    window->showObj(nullptr);
    window->updateObj(nullptr);
    properties.showObj(nullptr);
    properties.updateObj(nullptr);
    test.check(config.signalShape.size() == definitionCount,
               "missing definitions and deselection are safe without inserting null entries");

    // A signal's options in its TDB item against its world file (task
    // simulation 09, case A1: USER1 in the world file only, the simulation
    // showed S10 where Open Rails showed S2).
    {
        QDir().mkpath(temp.path() + "/ROUTES/options");
        QFile file(temp.path() + "/ROUTES/options/sigcfg.dat");
        const QByteArray fixture = R"(SIMISA@@@@@@@@@@JINX0t1t______
SignalShapes ( 1
 SignalShape ( "PS_Options.s" "Exit signal"
  SignalSubObjs ( 3
   SignalSubObj ( 0 "HEAD1" "Head" SigSubType ( SIGNAL_HEAD ) SignalFlags ( DEFAULT ) SigSubSType ( "Exit" ) )
   SignalSubObj ( 1 "" "Link for Vmax" SigSubType ( USER1 ) SignalFlags ( OPTIONAL ) )
   SignalSubObj ( 2 "" "Always Vmax" SigSubType ( USER2 ) SignalFlags ( OPTIONAL ) )
  )
 )
)
)";
        test.check(file.open(QIODevice::WriteOnly) && file.write(fixture) == fixture.size(), "options fixture");
        file.close();
        QScopedValueRollback<QString> optionsRoute(Game::route, "options");
        SigCfg options;
        TDB optionsDb(nullptr, false);
        optionsDb.sigCfg = &options;
        QScopedValueRollback<TDB*> optionsTrackDB(Game::trackDB, &optionsDb);
        // The head's item: its junction link bit and USER2 set, USER1 not;
        // the world file enables the head and USER1.
        optionsDb.trackItems[28] = TRitem::newSignalItem(28, 10.0f, 0, 0x81u, "Exit");
        SignalObj exit;
        exit.type = "signal";
        exit.fileName = "PS_Options.s";
        auto subObjects = TokenTest::buffer(QByteArray(1, '\0') + TokenTest::uints({0x3u}));
        exit.set(TS::SignalSubObj, subObjects.get());
        auto units = TokenTest::buffer(QByteArray(1, '\0') + TokenTest::uints({1})
                                       + TokenTest::block(TS::SignalUnit, TokenTest::uints({0})
                                                          + TokenTest::block(TS::TrItemId, TokenTest::uints({0, 28}))));
        exit.set(TS::SignalUnits, units.get());
        exit.loadInit();
        ErrorMessage *wrong = exit.checkOptionFlags();
        test.check(wrong != nullptr && wrong->type == ErrorMessage::Type_Error && wrong->action.contains("USER1")
                       && wrong->action.contains("USER2") && wrong->canFix && wrong->canFix(),
                   "TDB options that differ from the world file: an error naming the item's missing and extra "
                   "options, with a Fix");
        QString result;
        const bool fixed = wrong != nullptr && wrong->applyFix(result);
        test.check(fixed && optionsDb.trackItems[28]->trSignalType1 == 0x41u && wrong->type == ErrorMessage::Type_AutoFix
                       && exit.checkOptionFlags() == nullptr,
                   "Fix sets the options from the world file and keeps the junction link bit: " + result);
        delete optionsDb.trackItems[28];
        optionsDb.trackItems.erase(28);
    }
    return test.finish();
}
