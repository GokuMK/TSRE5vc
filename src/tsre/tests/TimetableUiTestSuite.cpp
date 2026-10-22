#include <tsre/tests/ActivityUiTestSuite.h>

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTreeWidget>
#include <routeEditor/ActivityTools.h>
#include <routeEditor/activity/ActivityEventWindow.h>
#include <routeEditor/activity/ActivityTimetableProperties.h>
#include <routeEditor/activity/ActivityTimetableWindow.h>
#include <routeEditor/activity/ActivityTrafficProperties.h>
#include <routeEditor/activity/ActivityTrafficWindow.h>
#include <routeEditor/activity/OrTrainProperties.h>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/tdb/TDB.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityCheck.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/OrTimetable.h>
#include <tsre/world/Route.h>

namespace {

bool writeUtf16(const QString &path, const QString &text) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write("\xFF\xFE");
    file.write(QStringEncoder(QStringConverter::Utf16LE).encode(QString(text).replace("\n", "\r\n")));
    return true;
}

const char *timetablePath = R"(SIMISA@@@@@@@@@@JINX0P0t______

Serial ( 1 )
TrackPDPs (
	TrackPDP ( 0 0 100 0 0 2 0 )
	TrackPDP ( 0 0 1900 0 0 2 0 )
)
TrackPath (
	TrPathName ( %1 )
	Name ( "%1" )
	TrPathStart ( "A" )
	TrPathEnd ( "B" )
	TrPathNodes ( 2
		TrPathNode ( 00000000 1 4294967295 0 )
		TrPathNode ( 00000000 4294967295 4294967295 1 )
	)
)
)";

QStringList unsavedItems() {
    QVector<QString> items;
    ActLib::GetUnsavedInfo(items);
    return QStringList(items.begin(), items.end());
}

}

namespace TsreTests {

// The activity windows with an Open Rails timetable (task simulation 08,
// phases 5, 6 and 8): the kind chosen first, the set listed, its player's
// train, the Traffic window's timetables, the Timetable window's train and
// its times and commands edited, undone and saved (the original kept), its
// check.
int runTimetableUiSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:timetable-ui] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:timetable-ui] FAIL" << name; }
    };
    QTemporaryDir temp;
    const QString routePath = temp.path() + "/ROUTES/TT";
    const QString file = routePath + "/ACTIVITIES/OPENRAILS/test.timetable-or";
    const QString text = ";#comment;T1;T2;$static\n"
                         "#comment;UI timetable;first;;\n"
                         "#path;;P1;P1;P1\n"
                         "#consist;;C1;C1;C1\n"
                         "#start;;08:00;08:30 $create=08:20;\n"
                         "#note;;$acc=2;;\n"
                         "Alpha $hold;;08:05-08:07;08:35;\n"
                         "Beta;;08:20;08:50 $nowaitsignal;\n";
    QDir().mkpath(routePath + "/PATHS");
    QDir().mkpath(temp.path() + "/TRAINS/CONSISTS");
    check(writeUtf16(file, text) && writeUtf16(routePath + "/PATHS/P1.pat", QString(timetablePath).arg("P1")),
          "write the test route");
    QFile originalFile(file);
    originalFile.open(QIODevice::ReadOnly);
    const QByteArray originalBytes = originalFile.readAll();
    originalFile.close();

    const QScopedValueRollback<QString> root(Game::root, temp.path());
    const QScopedValueRollback<QString> routeName(Game::route, "TT");
    const QScopedValueRollback<bool> write(Game::writeEnabled, true);
    const QScopedValueRollback<TDB *> trackDB(Game::trackDB, nullptr);
    EngLib engines;
    const QScopedValueRollback<EngLib *> engineLibrary(Game::currentEngLib,
                                                       Game::currentEngLib != nullptr ? Game::currentEngLib : &engines);
    const int firstPath = ActLib::jestpath;
    Route *route = new Route();
    route->loadPaths();
    route->loadActivities();
    route->loadTimetableSets();
    Activity *set = route->timetableSetId.isEmpty() ? nullptr : ActLib::TimetableSets.value(route->timetableSetId[0]);
    check(set != nullptr && set->isTimetables() && set->timetables.size() == 1, "the route's timetable set loads");
    if (set == nullptr) {
        qInfo().noquote() << QString("timetable-ui: %1 passed, %2 failed").arg(passed).arg(failed);
        return 1;
    }

    ActivityTools tools("ActivityTools");
    ActivityEventWindow events(nullptr);
    ActivityTrafficWindow traffic(nullptr);
    ActivityTimetableWindow timetable(nullptr);
    QObject::connect(&tools, &ActivityTools::showEvents, &events, &ActivityEventWindow::showEvents);
    QObject::connect(&tools, &ActivityTools::showTraffic, &traffic, &ActivityTrafficWindow::showTraffic);
    QObject::connect(&tools, &ActivityTools::showTimetable, &timetable, &ActivityTimetableWindow::showTimetable);
    tools.show();
    traffic.show();
    timetable.show();
    tools.routeLoaded(route);

    // The kind first: the timetable sets listed, the MSTS activity's parts
    // hidden, the player's train shown.
    QComboBox *kind = tools.findChild<QComboBox *>("activityKind");
    QComboBox *player = tools.findChild<QComboBox *>("activityPlayerTrain");
    QComboBox *activities = nullptr;
    for (QComboBox *combo : tools.findChildren<QComboBox *>())
        if (combo != kind && combo != player && combo->styleSheet().contains("combobox-popup") && combo->maxVisibleItems() == 35
                && combo->count() == 0)
            activities = activities == nullptr ? combo : activities;
    check(kind != nullptr && player != nullptr && !player->isVisible(), "MSTS activities first: no player's train choice");
    if (kind != nullptr) {
        kind->setCurrentIndex(1);
        emit kind->activated(1);
    }
    for (QComboBox *combo : tools.findChildren<QComboBox *>())
        if (combo->findText("UI timetable") >= 0)
            activities = combo;
    bool eventsButtonHidden = true;
    for (QPushButton *button : tools.findChildren<QPushButton *>())
        if (button->text() == qtTrId("route.editor.activity.tools.button.act.events.open"))
            eventsButtonHidden = !button->isVisible();
    check(ActivityNotifier::timetables() && activities != nullptr && player->isVisible() && eventsButtonHidden,
          "Open Rails timetables: the set listed, the events and services hidden, the player's train shown");
    if (activities != nullptr) {
        activities->setCurrentIndex(activities->findText("UI timetable"));
        emit activities->textActivated(activities->currentText());
    }
    QLabel *summary = tools.findChild<QLabel *>("activityTimetableSummary");
    check(route->getCurrentActivity() == set && player->count() == 3 && player->itemData(0).toString().isEmpty()
              && summary != nullptr && !summary->text().isEmpty(),
          "the set chosen: None and its two running trains to drive; " + (summary ? summary->text() : QString()));
    player->setCurrentIndex(player->findData("T2"));
    emit player->activated(player->currentIndex());
    check(set->playerTrain == "T2" && unsavedItems().isEmpty(), "T2 chosen as the player's train (not an edit)");

    // The Traffic window: the route's timetables, the chosen one's summary.
    traffic.showTraffic(route);
    QTreeWidget *list = traffic.findChild<QTreeWidget *>("trafficList");
    QTextBrowser *info = traffic.findChild<QTextBrowser *>("trafficTimetableInfo");
    if (list != nullptr && list->topLevelItemCount() == 1)
        traffic.lTrafficSelected(list->topLevelItem(0), 0);
    check(list != nullptr && list->topLevelItemCount() == 1 && list->topLevelItem(0)->text(1) == "3" && info != nullptr
              && info->isVisible() && info->toPlainText().contains("UI timetable") && !traffic.trafficProperties->isVisible(),
          "the Traffic window lists the timetable: 3 trains, its summary and problems in place of the editor");

    // The Timetable window: the set's trains; T1's start and stops.
    tools.actTimetableOpenEnabled();
    QTreeWidget *trains = timetable.findChild<QTreeWidget *>("timetableTrains");
    OrTrainProperties *panel = timetable.findChild<OrTrainProperties *>("orTrainProperties");
    QTreeWidget *stops = timetable.findChild<QTreeWidget *>("orTrainStops");
    int row = -1;
    for (int i = 0; trains != nullptr && i < trains->topLevelItemCount(); ++i)
        if (trains->topLevelItem(i)->text(1) == "T1")
            row = i;
    if (row >= 0) {
        trains->setCurrentItem(trains->topLevelItem(row));
        trains->topLevelItem(row)->setSelected(true);
    }
    check(trains != nullptr && trains->topLevelItemCount() == 3 && panel != nullptr && panel->isVisible()
              && !timetable.timetableProperties->isVisible() && stops != nullptr && stops->topLevelItemCount() == 2
              && stops->topLevelItem(0)->text(1) == "08:05" && stops->topLevelItem(0)->text(2) == "08:07"
              && stops->topLevelItem(0)->text(3).isEmpty() && stops->topLevelItem(0)->text(4) == "$hold",
          "the Timetable window: the set's trains; T1's stops at Alpha 08:05-08:07 (the station's $hold) and Beta in "
          "its panel");

    // T1's departure from Alpha edited: the set unsaved, one undo step.
    if (stops != nullptr && stops->topLevelItemCount() > 0)
        stops->topLevelItem(0)->setText(2, "08:09");
    QApplication::processEvents();
    const int t1 = set->timetables[0]->trainNamed("T1");
    check(set->isUnSaved() && unsavedItems().join(" ").contains("[T]")
              && set->timetables[0]->trains[t1].stops[0].timing.departure == 8 * 3600 + 540
              && stops != nullptr && stops->topLevelItem(0)->text(2) == "08:09",
          "T1's departure from Alpha set to 08:09: the timetable unsaved");
    Undo::UndoLast();
    check(!set->isUnSaved() && set->timetables[0]->trains[t1].stops[0].timing.departure == 8 * 3600 + 420,
          "undo: 08:07 again, nothing unsaved");
    if (stops != nullptr) {
        timetable.showTimetable(set);
        for (int i = 0; trains != nullptr && i < trains->topLevelItemCount(); ++i)
            if (trains->topLevelItem(i)->text(1) == "T1") {
                trains->setCurrentItem(trains->topLevelItem(i));
                trains->topLevelItem(i)->setSelected(true);
            }
        stops->topLevelItem(0)->setText(2, "08:10");
        QApplication::processEvents();
    }
    QTimeEdit *start = timetable.findChild<QTimeEdit *>("orTrainStart");
    if (start != nullptr) {
        start->setTime(QTime(8, 2));
        emit start->editingFinished();
        QApplication::processEvents();
    }
    // Phase 8: commands edited: T1's note (with $connect, not run by TSRE:
    // kept, shown in orange), its stop's at Beta, Beta's for every train
    // ($closeup, not run either; undone and set again).
    QLineEdit *note = timetable.findChild<QLineEdit *>("orTrainNote");
    if (note != nullptr) {
        note->setText("$acc=2 $connect=T2 /maxdelay=5 /hold=2");
        emit note->editingFinished();
        QApplication::processEvents();
    }
    const OrTimetable::Train &noted = set->timetables[0]->trains[t1];
    note = timetable.findChild<QLineEdit *>("orTrainNote");
    check(note != nullptr && noted.notes.size() == 2 && noted.notes[1].name == "connect"
              && note->text() == "$acc=2 $connect=T2 /maxdelay=5 /hold=2" && note->styleSheet().contains("color")
              && note->toolTip().contains("$connect") && set->isUnSaved(),
          "T1's note edited: $connect kept, the field orange with what TSRE does not run: "
              + (note ? note->toolTip() : QString()));
    stops = timetable.findChild<QTreeWidget *>("orTrainStops");
    if (stops != nullptr && stops->topLevelItemCount() > 1)
        stops->topLevelItem(1)->setText(3, "forcewait");
    QApplication::processEvents();
    stops = timetable.findChild<QTreeWidget *>("orTrainStops");
    if (stops != nullptr && stops->topLevelItemCount() > 1)
        stops->topLevelItem(1)->setText(4, "$closeup");
    QApplication::processEvents();
    const int beta = 1;
    stops = timetable.findChild<QTreeWidget *>("orTrainStops");
    check(set->timetables[0]->trains[t1].stops[1].commands.size() == 1
              && set->timetables[0]->trains[t1].stops[1].commands[0].name == "forcewait"
              && set->timetables[0]->stations[beta].commands.size() == 1
              && set->timetables[0]->stations[beta].commands[0].name == "closeup" && stops != nullptr
              && stops->topLevelItem(1)->text(3) == "$forcewait" && stops->topLevelItem(1)->text(4) == "$closeup"
              && stops->topLevelItem(1)->foreground(4).color() != stops->topLevelItem(1)->foreground(3).color(),
          "a stop's commands ($forcewait) and its station's ($closeup, not run: orange) edited in the stops list");
    Undo::UndoLast();
    check(set->timetables[0]->stations[beta].commands.isEmpty()
              && set->timetables[0]->trains[t1].stops[1].commands.size() == 1,
          "undo: the station's commands as they were, the stop's kept");
    timetable.showTimetable(set);
    for (int i = 0; trains != nullptr && i < trains->topLevelItemCount(); ++i)
        if (trains->topLevelItem(i)->text(1) == "T1") {
            trains->setCurrentItem(trains->topLevelItem(i));
            trains->topLevelItem(i)->setSelected(true);
        }
    stops = timetable.findChild<QTreeWidget *>("orTrainStops");
    if (stops != nullptr && stops->topLevelItemCount() > 1)
        stops->topLevelItem(1)->setText(4, "$closeup");
    QApplication::processEvents();
    // TSRE_ACTIVITY_UI_SHOTS: the window as it is now, for a look.
    if (qEnvironmentVariableIsSet("TSRE_ACTIVITY_UI_SHOTS"))
        timetable.grab().save(qEnvironmentVariable("TSRE_ACTIVITY_UI_SHOTS") + "/timetable-or-train.png");
    ActLib::SaveAll();
    QFile kept(file + ".original");
    kept.open(QIODevice::ReadOnly);
    OrTimetable reread;
    reread.load(file);
    const int r1 = reread.trainNamed("T1");
    check(!set->isUnSaved() && kept.readAll() == originalBytes && r1 >= 0
              && reread.trains[r1].stops[0].timing.departure == 8 * 3600 + 600 && reread.trains[r1].startTime == 8 * 3600 + 120
              && reread.trains[r1].notes.size() == 2 && reread.trains[r1].notes[1].name == "connect"
              && reread.trains[r1].stops.size() == 2 && reread.trains[r1].stops[1].commands.size() == 1
              && reread.stations.size() == 2 && reread.stations[1].commands.size() == 1
              && reread.stations[0].commands.size() == 1 && reread.description == "UI timetable"
              && reread.trainNamed("T2") >= 0 && reread.trains.size() == 3,
          "saved: the timetable written with T1 from 08:02, its departure 08:10 and its commands, the original kept "
          "as .original");

    // Check Activity: the set's problems (its consist is not there).
    const QVector<ActivityFinding> findings = CheckActivity(set);
    bool consist = false;
    for (const ActivityFinding &finding : findings)
        consist = consist || (finding.severity == ActivityFinding::Error && finding.text.contains("C1"));
    check(consist, QString("Check Activity lists the set's problems: %1 findings").arg(findings.size()));

    if (kind != nullptr) {
        kind->setCurrentIndex(0);
        emit kind->activated(0);
    }
    check(!ActivityNotifier::timetables() && !player->isVisible(), "back to MSTS activities");
    for (int id = firstPath; id < ActLib::jestpath; ++id)
        ActLib::Paths[id] = nullptr;
    for (int id : route->timetableSetId)
        ActLib::TimetableSets.remove(id);
    qInfo().noquote() << QString("timetable-ui: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}

}
