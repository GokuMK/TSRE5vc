#include <tsre/tests/ActivityUiTestSuite.h>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHashFunctions>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <routeEditor/ActivityTools.h>
#include <routeEditor/activity/ActivityEventProperties.h>
#include <routeEditor/activity/ActivityEventWindow.h>
#include <routeEditor/activity/ActivityServiceProperties.h>
#include <routeEditor/activity/ActivityServiceWindow.h>
#include <routeEditor/activity/ActivityTimetableProperties.h>
#include <routeEditor/activity/ActivityTimetableWindow.h>
#include <routeEditor/activity/ActivityTrafficWindow.h>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/ActivityEvent.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/trains/ActivityObject.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Traffic.h>
#include <tsre/world/Route.h>

namespace {

// Files of the test route, as MSTS writes them: UTF-16 with a byte order
// mark. The activity has events of each kind, an outcome of each kind, a
// wagon list, a loose consist, two speed zones and two failed signals; its
// player timetable has a stop missing fields; one AI instance names a
// service that does not exist; the consists name missing wagons.
const char *activityText = R"(SIMISA@@@@@@@@@@JINX0a0t______

Tr_Activity (
	Serial ( 1 )
	Tr_Activity_Header (
		RouteID ( TEST )
		Name ( "UI test" )
		Description ( "Description" )
		Briefing ( "Briefing" )
		CompleteActivity ( 1 )
		Type ( 0 )
		Mode ( 2 )
		StartTime ( 8 0 0 )
		Season ( 1 )
		Weather ( 0 )
		PathID ( "Main" )
		StartingSpeed ( 0 )
		Duration ( 1 0 )
		Difficulty ( 1 )
		Animals ( 10 )
		Workers ( 5 )
		FuelWater ( 100 )
		FuelCoal ( 100 )
		FuelDiesel ( 100 )
	)
	Tr_Activity_File (
		Player_Service_Definition ( Player
			Player_Traffic_Definition ( 28800
				ArrivalTime ( 29000 )
				DepartTime ( 29060 )
				SkipCount ( 0 )
				DistanceDownPath ( 1000 )
				PlatformStartID ( 12 )
				ArrivalTime ( 29500 )
			)
			UiD ( 0 )
			Efficiency ( 0.9 )
			SkipCount ( 0 )
			DistanceDownPath ( 1000 )
			PlatformStartID ( 12 )
			Efficiency ( 0.8 )
		)
		NextServiceUID ( 3 )
		NextActivityObjectUID ( 32770 )
		Traffic_Definition ( Traffic
			Service_Definition ( AI 30000
				UiD ( 1 )
			)
			Service_Definition ( Missing 31000
				UiD ( 2 )
			)
		)
		Events (
			EventCategoryAction (
				EventTypePickUpWagons ( )
				ID ( 1 )
				Activation_Level ( 1 )
				Outcomes (
					IncActLevel ( 2 )
					DisplayMessage ( "Picked up." )
				)
				Name ( "Pick up" )
				Wagon_List (
					UiD ( 2147581952 )
					SidingItem ( 4294967295 )
					Description ( "" )
				)
			)
			EventCategoryAction (
				EventTypeDropOffWagonsAtLocation ( )
				ID ( 2 )
				Activation_Level ( 0 )
				Outcomes (
					ActivitySuccess ( )
					ORTSWeatherChange ( "rain" )
				)
				Name ( "Drop off" )
				Wagon_List (
					UiD ( 2147581952 )
					SidingItem ( 553 )
					Description ( "" )
				)
				SidingItem ( 553 )
			)
			EventCategoryAction (
				EventTypeReachSpeed ( )
				ID ( 3 )
				Activation_Level ( 1 )
				Outcomes (
					ActivateEvent ( 9 )
					ORTSActSoundFile ( "horn.wav" Everywhere )
				)
				Name ( "Speed" )
				SpeedMpS ( 10 )
			)
			EventCategoryAction (
				EventTypeAllStops ( )
				ID ( 4 )
				Activation_Level ( 1 )
				Outcomes (
					StartIgnoringSpeedLimits ( )
				)
				TextToDisplayOnCompletionIfTriggered ( "All stops." )
				Name ( "Stops" )
			)
			EventCategoryLocation (
				EventTypeLocation ( )
				ID ( 5 )
				Activation_Level ( 1 )
				Outcomes (
					ActivityFail ( "Failed." )
				)
				Name ( "Here" )
				Location ( 0 0 10 10 50 )
				TriggerOnStop ( 1 )
			)
			EventCategoryTime (
				EventTypeTime ( )
				ID ( 6 )
				Activation_Level ( 1 )
				Outcomes (
					DecActLevel ( 2 )
					RestoreActLevel ( 3 )
				)
				Name ( "Time" )
				Time ( 60 )
			)
		)
		ActivityObjects (
			ActivityObject (
				ObjectType ( WagonsList )
				Train_Config (
					TrainCfg ( "loose"
						Name ( "Loose" )
						Serial ( 1 )
						MaxVelocity ( 0 0.001 )
						NextWagonUID ( 1 )
						Durability ( 1 )
						Wagon (
							WagonData ( missing_wagon missing_folder )
							UiD ( 0 )
						)
					)
				)
				Direction ( 1 )
				ID ( 32769 )
				Tile ( 0 0 10 10 )
			)
		)
		ActivityRestrictedSpeedZones (
			ActivityRestrictedSpeedZone (
				StartPosition ( 0 0 10 10 )
				EndPosition ( 0 0 20 20 )
			)
			ActivityRestrictedSpeedZone (
				StartPosition ( 0 0 30 30 )
				EndPosition ( 0 0 40 40 )
			)
		)
		ActivityFailedSignals (
			ActivityFailedSignal ( 3 )
			ActivityFailedSignal ( 4 )
		)
	)
)
)";

const char *serviceText = R"(SIMISA@@@@@@@@@@JINX0v0t______

Service_Definition (
	Serial ( 1 )
	Name ( "%1" )
	Train_Config ( %2 )
	PathID ( "Main" )
	MaxWheelAcceleration ( 0 )
	Efficiency ( 0.9 )
	TimeTable (
		StartingSpeed ( 0 )
		EndingSpeed ( 0 )
		StartInWorld ( 0 )
		EndInWorld ( 0 )
		Efficiency ( 0.9 )
		SkipCount ( 0 )
		DistanceDownPath ( 1000 )
		PlatformStartID ( 12 )
	)
)
)";

const char *trafficText = R"(SIMISA@@@@@@@@@@JINX0f0t______

Traffic_Definition ( Traffic
	Serial ( 1 )
	Service_Definition ( AI 30000
		ArrivalTime ( 30100 )
		DepartTime ( 30160 )
		SkipCount ( 0 )
		DistanceDownPath ( 500 )
		PlatformStartID ( 12 )
		ArrivalTime ( 30500 )
	)
	Service_Definition ( Missing 31000
	)
)
)";

const char *consistText = R"(SIMISA@@@@@@@@@@JINX0D0t______

Train (
	TrainCfg ( "%1"
		Name ( "%1" )
		Serial ( 1 )
		MaxVelocity ( 20 0.5 )
		NextWagonUID ( 1 )
		Durability ( 1 )
		%2
	)
)
)";

const char *pathText = R"(SIMISA@@@@@@@@@@JINX0P0t______

Serial ( 1 )
TrackPDPs (
	TrackPDP ( 0 0 10 0 10 2 0 )
	TrackPDP ( 0 0 100 0 10 2 0 )
)
TrackPath (
	TrPathName ( Main )
	Name ( "Main" )
	TrPathStart ( "A" )
	TrPathEnd ( "B" )
	TrPathNodes ( 2
		TrPathNode ( 00000000 1 4294967295 0 )
		TrPathNode ( 00000000 4294967295 4294967295 1 )
	)
)
)";

bool writeSimis(const QString &path, const QString &text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QString crlf = text;
    crlf.replace("\n", "\r\n");
    file.write("\xff\xfe", 2);
    file.write(reinterpret_cast<const char*>(crlf.utf16()), crlf.size() * 2);
    return true;
}

// Answers the modal dialogs the windows open: cancels them, or fills their
// empty name fields and confirms them.
class DialogCloser : public QObject {
public:
    bool accept = false;
    bool trace = false;
    QStringList names;
    QRandomGenerator *random = nullptr;
    int answered = 0;

    DialogCloser() {
        okTexts << qtTrId("route.editor.properties.edit.file.name.dialog.button.ok")
                << qtTrId("tsre.gui.text.edit.dialog.button.bok")
                << qtTrId("tsre.gui.unsaved.dialog.button.bok.2");
        timer.setInterval(1);
        QObject::connect(&timer, &QTimer::timeout, this, [this] { answer(); });
        timer.start();
    }

private:
    QTimer timer;
    QStringList okTexts;
    QPointer<QWidget> last;
    int attempts = 0;
    // A dialog asking again (a refused name): a user gives up after a few.
    QString lastTitle;
    int repeats = 0;

    void answer() {
        QWidget *modal = QApplication::activeModalWidget();
        if (modal == nullptr)
            return;
        // A dialog that stays open after an answer is closed.
        attempts = modal == last ? attempts + 1 : 0;
        last = modal;
        ++answered;
        if (trace)
            qInfo().noquote() << "[tests:activity-ui] dialog" << modal->metaObject()->className()
                              << modal->windowTitle() << (accept ? "accept" : "reject") << attempts;
        QDialog *dialog = qobject_cast<QDialog*>(modal);
        if (attempts > 2) {
            if (dialog)
                dialog->reject();
            else
                modal->close();
            return;
        }
        if (QMessageBox *box = qobject_cast<QMessageBox*>(modal)) {
            QAbstractButton *button = nullptr;
            if (accept)
                button = box->button(QMessageBox::Yes) ? box->button(QMessageBox::Yes) : box->button(QMessageBox::Ok);
            else
                button = box->escapeButton() ? box->escapeButton()
                       : box->button(QMessageBox::No) ? box->button(QMessageBox::No) : box->button(QMessageBox::Cancel);
            if (button)
                button->click();
            else
                box->reject();
            return;
        }
        repeats = modal->windowTitle() == lastTitle ? repeats + 1 : 0;
        lastTitle = modal->windowTitle();
        if (!accept || repeats > 3) {
            repeats = 0;
            lastTitle.clear();
            if (dialog)
                dialog->reject();
            else
                modal->close();
            return;
        }
        for (QLineEdit *edit : modal->findChildren<QLineEdit*>())
            if (edit->text().isEmpty() && !names.isEmpty())
                edit->setText(names[random ? random->bounded(names.size()) : 0]);
        if (QDialogButtonBox *buttons = modal->findChild<QDialogButtonBox*>()) {
            for (QAbstractButton *button : buttons->buttons()) {
                const auto role = buttons->buttonRole(button);
                if (role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::YesRole) {
                    button->click();
                    return;
                }
            }
        }
        for (QPushButton *button : modal->findChildren<QPushButton*>())
            if (okTexts.contains(button->text())) {
                button->click();
                return;
            }
        if (dialog)
            dialog->accept();
        else
            modal->close();
    }
};

// Uses the windows as a user would, at random: buttons, fields, lists,
// tables. Only visible, enabled widgets, as a user sees them.
class Monkey {
public:
    QList<QWidget*> windows;
    QRandomGenerator random;
    bool trace = false;
    int actions = 0;
    // Play puts the player's train on the terrain, which the test route
    // has none of.
    const QStringList skipped = {qtTrId("route.editor.activity.tools.button.act.play.button")};

    explicit Monkey(quint32 seed) : random(seed) {}

    int undos = 0;

    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            // Undo now and then (Ctrl+Z): the windows let go of what it
            // replaces.
            if (random.bounded(25) == 0) {
                ++undos;
                if (trace)
                    qInfo().noquote() << "[tests:activity-ui] undo";
                Undo::UndoLast();
                QApplication::processEvents();
                continue;
            }
            QList<QPointer<QWidget>> widgets = usable();
            if (widgets.isEmpty())
                return;
            QPointer<QWidget> widget = widgets[random.bounded(widgets.size())];
            if (widget)
                use(widget);
            QApplication::processEvents();
        }
    }

private:
    const QStringList values = {"", "0", "1", "-1", "12", "99999", "abc", "Name 2", "x/y"};

    QList<QPointer<QWidget>> usable() {
        QList<QPointer<QWidget>> widgets;
        for (QWidget *window : windows) {
            if (!window->isVisible())
                continue;
            for (QWidget *widget : window->findChildren<QWidget*>()) {
                if (!widget->isVisible() || !widget->isEnabled())
                    continue;
                // The parts of other widgets: a spin box's or combo box's
                // field, a scroll bar.
                if (qobject_cast<QAbstractSpinBox*>(widget->parentWidget())
                        || qobject_cast<QComboBox*>(widget->parentWidget())
                        || qobject_cast<QScrollBar*>(widget))
                    continue;
                if (qobject_cast<QAbstractButton*>(widget) || qobject_cast<QComboBox*>(widget)
                        || qobject_cast<QLineEdit*>(widget) || qobject_cast<QAbstractSpinBox*>(widget)
                        || qobject_cast<QListWidget*>(widget) || qobject_cast<QTreeWidget*>(widget)
                        || qobject_cast<QTableWidget*>(widget) || qobject_cast<QPlainTextEdit*>(widget)
                        || qobject_cast<QSlider*>(widget) || qobject_cast<QTabWidget*>(widget))
                    widgets << widget;
            }
        }
        return widgets;
    }

    void log(QWidget *widget, const QString &what) {
        ++actions;
        if (trace)
            qInfo().noquote() << "[tests:activity-ui] action" << actions << widget->metaObject()->className()
                              << widget->window()->windowTitle() << what;
    }

    void use(QPointer<QWidget> widget) {
        if (auto *button = qobject_cast<QAbstractButton*>(widget)) {
            if (skipped.contains(button->text()))
                return;
            log(button, "click " + button->text());
            button->click();
        } else if (auto *combo = qobject_cast<QComboBox*>(widget)) {
            if (combo->count() == 0)
                return;
            const int index = random.bounded(combo->count());
            log(combo, QString("choose %1 %2").arg(index).arg(combo->itemText(index)));
            combo->setCurrentIndex(index);
            if (combo && index < combo->count())
                emit combo->activated(index);
            if (combo && index < combo->count())
                emit combo->textActivated(combo->itemText(index));
        } else if (auto *edit = qobject_cast<QLineEdit*>(widget)) {
            if (edit->isReadOnly())
                return;
            const QString value = values[random.bounded(values.size())];
            log(edit, "type " + value);
            edit->setText(value);
            QPointer<QLineEdit> alive(edit);
            if (alive)
                emit alive->textEdited(value);
            if (alive)
                emit alive->editingFinished();
            if (alive)
                emit alive->returnPressed();
        } else if (auto *time = qobject_cast<QDateTimeEdit*>(widget)) {
            const QTime value = QTime::fromMSecsSinceStartOfDay(random.bounded(24 * 3600) * 1000);
            log(time, "time " + value.toString());
            time->setTime(value);
            if (time)
                emit time->editingFinished();
        } else if (auto *spin = qobject_cast<QSpinBox*>(widget)) {
            const int value = spin->minimum() + random.bounded(qMax(1, spin->maximum() - spin->minimum()));
            log(spin, QString("value %1").arg(value));
            spin->setValue(value);
            if (spin)
                emit spin->editingFinished();
        } else if (auto *spin = qobject_cast<QDoubleSpinBox*>(widget)) {
            const double value = spin->minimum() + random.generateDouble() * (spin->maximum() - spin->minimum());
            log(spin, QString("value %1").arg(value));
            spin->setValue(value);
            if (spin)
                emit spin->editingFinished();
        } else if (auto *table = qobject_cast<QTableWidget*>(widget)) {
            if (table->rowCount() == 0 || table->columnCount() == 0)
                return;
            const int row = random.bounded(table->rowCount());
            const int column = random.bounded(table->columnCount());
            const QString value = values[random.bounded(values.size())];
            log(table, QString("cell %1 %2 %3").arg(row).arg(column).arg(value));
            table->setCurrentCell(row, column);
            if (table && row < table->rowCount())
                emit table->cellClicked(row, column);
            if (table && row < table->rowCount() && table->item(row, column))
                table->item(row, column)->setText(value);
        } else if (auto *list = qobject_cast<QListWidget*>(widget)) {
            if (list->count() == 0)
                return;
            const int row = random.bounded(list->count());
            log(list, QString("row %1").arg(row));
            list->setCurrentRow(row);
            if (list && row < list->count())
                emit list->itemClicked(list->item(row));
            if (list && row < list->count() && random.bounded(4) == 0)
                emit list->itemDoubleClicked(list->item(row));
        } else if (auto *tree = qobject_cast<QTreeWidget*>(widget)) {
            if (tree->topLevelItemCount() == 0)
                return;
            const int row = random.bounded(tree->topLevelItemCount());
            const int column = random.bounded(qMax(1, tree->columnCount()));
            log(tree, QString("item %1 %2").arg(row).arg(column));
            tree->setCurrentItem(tree->topLevelItem(row));
            if (tree && row < tree->topLevelItemCount())
                emit tree->itemClicked(tree->topLevelItem(row), column);
            if (tree && row < tree->topLevelItemCount() && random.bounded(4) == 0)
                emit tree->itemDoubleClicked(tree->topLevelItem(row), column);
            // A check box in the row, as a click on it.
            if (tree && row < tree->topLevelItemCount() && random.bounded(4) == 0) {
                QTreeWidgetItem *item = tree->topLevelItem(row);
                if (item->flags() & Qt::ItemIsUserCheckable)
                    item->setCheckState(column, item->checkState(column) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            }
        } else if (auto *text = qobject_cast<QPlainTextEdit*>(widget)) {
            if (text->isReadOnly())
                return;
            log(text, "text");
            text->setPlainText(values[random.bounded(values.size())]);
        } else if (auto *slider = qobject_cast<QSlider*>(widget)) {
            const int value = slider->minimum() + random.bounded(qMax(1, slider->maximum() - slider->minimum()));
            log(slider, QString("slide %1").arg(value));
            slider->setValue(value);
            if (slider)
                emit slider->sliderReleased();
        } else if (auto *tabs = qobject_cast<QTabWidget*>(widget)) {
            if (tabs->count() == 0)
                return;
            log(tabs, "tab");
            tabs->setCurrentIndex(random.bounded(tabs->count()));
        }
    }
};

// Selects every row of every list and tree of the windows, nothing more.
void lookAtEverything(const QList<QWidget*> &windows) {
    for (QWidget *window : windows) {
        for (QListWidget *list : window->findChildren<QListWidget*>()) {
            for (int row = 0; row < list->count(); ++row) {
                list->setCurrentRow(row);
                emit list->itemClicked(list->item(row));
                // The event's outcomes and wagons are shown now: look at them.
                for (QWidget *inner : windows)
                    for (QListWidget *other : inner->findChildren<QListWidget*>())
                        if (other != list && other->isVisible())
                            for (int r = 0; r < other->count(); ++r) {
                                other->setCurrentRow(r);
                                emit other->itemClicked(other->item(r));
                            }
            }
        }
        for (QTreeWidget *tree : window->findChildren<QTreeWidget*>()) {
            for (int row = 0; row < tree->topLevelItemCount(); ++row) {
                tree->setCurrentItem(tree->topLevelItem(row));
                emit tree->itemClicked(tree->topLevelItem(row), 0);
                for (QWidget *inner : windows)
                    for (QTreeWidget *other : inner->findChildren<QTreeWidget*>())
                        if (other != tree && other->isVisible())
                            for (int r = 0; r < other->topLevelItemCount(); ++r) {
                                other->setCurrentItem(other->topLevelItem(r));
                                emit other->itemClicked(other->topLevelItem(r), 0);
                            }
            }
        }
        QApplication::processEvents();
    }
}

QStringList unsaved() {
    QVector<QString> items;
    ActLib::GetUnsavedInfo(items);
    return QStringList(items.begin(), items.end());
}

// The window's button with this text id.
QPushButton *buttonNamed(QWidget *window, const char *id) {
    const QString text = qtTrId(id);
    for (QPushButton *button : window->findChildren<QPushButton*>())
        if (button->text() == text)
            return button;
    return nullptr;
}

// The window's list or tree with a row of this text.
QListWidget *listWith(QWidget *window, const QString &text) {
    for (QListWidget *list : window->findChildren<QListWidget*>())
        if (!list->findItems(text, Qt::MatchExactly).isEmpty())
            return list;
    return nullptr;
}

QTreeWidget *treeWith(QWidget *window, const QString &text) {
    for (QTreeWidget *tree : window->findChildren<QTreeWidget*>())
        if (!tree->findItems(text, Qt::MatchExactly).isEmpty())
            return tree;
    return nullptr;
}

}

int TsreTests::runActivityUiSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:activity-ui] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:activity-ui] FAIL" << name; }
    };

    // The same actions in every run: lists filled from hashes keep their order.
    QHashSeed::setDeterministicGlobalSeed();
    QTemporaryDir temp;
    const QString routePath = temp.path() + "/ROUTES/TEST";
    for (const QString &dir : {"ACTIVITIES", "SERVICES", "TRAFFIC", "PATHS"})
        QDir().mkpath(routePath + "/" + dir);
    QDir().mkpath(temp.path() + "/GLOBAL");
    QDir().mkpath(temp.path() + "/TRAINS/CONSISTS");
    const QString engine = "Engine (\n\t\t\tUiD ( 0 )\n\t\t\tEngineData ( missing_engine missing_folder )\n\t\t)";
    const QString wagon = "Wagon (\n\t\t\tUiD ( 0 )\n\t\t\tWagonData ( missing_wagon missing_folder )\n\t\t)";
    check(writeSimis(routePath + "/ACTIVITIES/Test.act", activityText)
          && writeSimis(routePath + "/SERVICES/Player.srv", QString(serviceText).arg("Player", "Train"))
          && writeSimis(routePath + "/SERVICES/AI.srv", QString(serviceText).arg("AI", "Wagons"))
          && writeSimis(routePath + "/TRAFFIC/Traffic.trf", trafficText)
          && writeSimis(routePath + "/PATHS/Main.pat", pathText)
          && writeSimis(temp.path() + "/TRAINS/CONSISTS/Train.con", QString(consistText).arg("Train", engine))
          && writeSimis(temp.path() + "/TRAINS/CONSISTS/Wagons.con", QString(consistText).arg("Wagons", wagon)),
          "write the test route");

    const QScopedValueRollback<QString> root(Game::root, temp.path());
    const QScopedValueRollback<QString> routeName(Game::route, "TEST");
    const QScopedValueRollback<bool> write(Game::writeEnabled, true);
    const QScopedValueRollback<TDB*> trackDB(Game::trackDB, nullptr);
    // The consists' wagons are looked up as the editor does.
    EngLib engines;
    const QScopedValueRollback<EngLib*> engineLibrary(Game::currentEngLib,
            Game::currentEngLib != nullptr ? Game::currentEngLib : &engines);
    const int firstAct = ActLib::jestact, firstService = ActLib::jestservice;
    const int firstTraffic = ActLib::jesttraffic, firstPath = ActLib::jestpath;
    const bool routeListed = ActLib::route.contains("TEST");

    Route *route = new Route();
    route->loadServices();
    route->loadTraffic();
    route->loadPaths();
    route->loadActivities();
    check(route->activityId.size() == 1 && ActLib::Act.value(route->activityId.value(0)) != nullptr
          && ActLib::Act[route->activityId[0]]->loaded == 1, "the activity loads");
    Activity *activity = route->activityId.isEmpty() ? nullptr : ActLib::Act.value(route->activityId[0]);
    if (activity == nullptr || activity->loaded != 1) {
        qInfo().noquote() << QString("activity-ui: %1 passed, %2 failed").arg(passed).arg(failed + 1);
        return 1;
    }
    check(activity->event.size() == 6 && activity->restrictedSpeedZone.size() == 2
          && activity->activityFailedSignal.size() == 2 && activity->traffic != nullptr,
          "the activity has its events, zones, signals and traffic");
    check(unsaved().isEmpty(), "nothing is changed after loading: " + unsaved().join(", "));

    DialogCloser closer;
    ActivityTools tools("ActivityTools");
    ActivityEventWindow events(nullptr);
    ActivityServiceWindow services(nullptr);
    ActivityTrafficWindow traffic(nullptr);
    ActivityTimetableWindow timetable(nullptr);
    QObject::connect(&tools, &ActivityTools::showActivityEventEditor, &events, &QWidget::show);
    QObject::connect(&tools, &ActivityTools::showActivityServiceEditor, &services, &QWidget::show);
    QObject::connect(&tools, &ActivityTools::showActivityTrafficEditor, &traffic, &QWidget::show);
    QObject::connect(&tools, &ActivityTools::showActivityTimetableEditor, &timetable, &QWidget::show);
    QObject::connect(&tools, &ActivityTools::showEvents, &events, &ActivityEventWindow::showEvents);
    QObject::connect(&tools, &ActivityTools::showServices, &services, &ActivityServiceWindow::showServices);
    QObject::connect(&tools, &ActivityTools::showTraffic, &traffic, &ActivityTrafficWindow::showTraffic);
    QObject::connect(&tools, &ActivityTools::showTimetable, &timetable, &ActivityTimetableWindow::showTimetable);
    QObject::connect(&tools, &ActivityTools::selectEvent, &events, &ActivityEventWindow::selectEvent);
    QObject::connect(&tools, &ActivityTools::selectService, &services, &ActivityServiceWindow::selectService);
    QObject::connect(&tools, &ActivityTools::selectTraffic, &traffic, &ActivityTrafficWindow::selectTraffic);
    QObject::connect(&services, &ActivityServiceWindow::reloadServicesList, &tools, &ActivityTools::reloadServicesList);
    QObject::connect(&traffic, &ActivityTrafficWindow::reloadTrafficsList, &tools, &ActivityTools::reloadTrafficsList);
    const QList<QWidget*> windows = {&tools, &events, &services, &traffic, &timetable};
    tools.show();
    tools.routeLoaded(route);
    events.show();
    services.show();
    traffic.show();
    timetable.show();
    events.showEvents(nullptr);
    services.showServices(route);
    traffic.showTraffic(route);
    timetable.showTimetable(nullptr);

    // Nothing chosen: the panels that edit an object are disabled, and
    // their buttons do nothing.
    check(!events.eventProperties->isEnabled() && !services.serviceProperties->isEnabled()
          && !timetable.timetableProperties->isEnabled(), "panels without an object are disabled");
for (QWidget *window : windows)
        for (QPushButton *button : window->findChildren<QPushButton*>())
            if (button->isVisible() && button->isEnabled())
                button->click();
    check(unsaved().isEmpty(), "buttons with nothing chosen change nothing: " + unsaved().join(", "));

    // Choose the activity and open its windows.
    QComboBox *activities = nullptr;
    for (QComboBox *combo : tools.findChildren<QComboBox*>())
        if (combo->findText(activity->header->name) >= 0)
            activities = combo;
    check(activities != nullptr, "the activity is listed");
    if (activities) {
        activities->setCurrentIndex(activities->findText(activity->header->name));
        emit activities->textActivated(activities->currentText());
    }
    check(route->getCurrentActivity() == activity, "choosing the activity makes it current");
    tools.actEventsOpenEnabled();
    tools.actServiceOpenEnabled();
    tools.actTrafficOpenEnabled();
    tools.actTimetableOpenEnabled();

    // Looking changes nothing: every row of every list, each in turn.
    Traffic *trafficFile = nullptr;
    for (Traffic *t : ActLib::Traffics)
        if (t != nullptr && t->nameId.compare("Traffic", Qt::CaseInsensitive) == 0)
            trafficFile = t;
    const int instanceStops = trafficFile && !trafficFile->service.isEmpty() ? trafficFile->service[0]->arrivalTime.size() : -1;
    lookAtEverything(windows);
    check(unsaved().isEmpty(), "looking at every object changes nothing: " + unsaved().join(", "));
    check(trafficFile && !trafficFile->service.isEmpty() && trafficFile->service[0]->arrivalTime.size() == instanceStops,
          "selecting a traffic instance keeps its timetable");

    // Check Activity lists the fixture's problems; choosing them shows what
    // they name.
    QPushButton *checkButton = buttonNamed(&tools, "route.editor.activity.tools.button.act.check.button");
    if (checkButton)
        checkButton->click();
    QTreeWidget *findings = nullptr;
    for (QWidget *window : QApplication::topLevelWidgets())
        if (window->windowTitle() == qtTrId("route.editor.activity.check.window.title") && window->isVisible())
            findings = window->findChild<QTreeWidget*>();
    check(checkButton && findings && findings->topLevelItemCount() > 0, "Check Activity lists the problems");
    if (findings)
        for (int row = 0; row < findings->topLevelItemCount(); ++row)
            emit findings->itemClicked(findings->topLevelItem(row), 0);
    check(unsaved().isEmpty(), "showing what the findings name changes nothing: " + unsaved().join(", "));

    // Events: deleting the shown event clears the panel; a new one is shown.
    QPushButton *deleteEvent = buttonNamed(&events, "route.editor.activity.event.window.button.b.delete.action.event");
    QPushButton *newEvent = buttonNamed(&events, "route.editor.activity.event.window.button.b.new.action.event");
    QListWidget *actionList = listWith(&events, "Pick up");
    if (deleteEvent && newEvent && actionList && actionList->count() > 0) {
        const int before = activity->event.size();
        actionList->setCurrentRow(0);
        emit actionList->itemClicked(actionList->item(0));
        check(events.eventProperties->isEnabled(), "a chosen event is shown");
        deleteEvent->click();
        check(activity->event.size() == before - 1 && !events.eventProperties->isEnabled(),
              "deleting the shown event clears its panel");
        deleteEvent->click();
        check(activity->event.size() == before - 1, "delete without a chosen event does nothing");
        newEvent->click();
        check(activity->event.size() == before && events.eventProperties->isEnabled(),
              "a new event is chosen and shown");
    } else {
        check(false, "the event window has its buttons and lists");
    }

    // Speed zones keep their ids in order after a delete.
    activity->deleteObjectSpeedZone(0);
    check(activity->restrictedSpeedZone.size() == 1 && activity->restrictedSpeedZone[0]->id == 0,
          "a deleted speed zone renumbers the rest");

    // New service and traffic: bad and taken names are refused.
    closer.accept = true;
    closer.random = nullptr;
    const int servicesBefore = ActLib::jestservice;
    QPushButton *newService = buttonNamed(&services, "route.editor.activity.service.window.button.b.new.action.event");
    for (const QString &name : {QString("x/y"), QString("Player"), QString("player")}) {
        closer.names = {name};
        if (newService)
            newService->click();
    }
    check(newService && ActLib::jestservice == servicesBefore, "a new service with a bad or taken name is refused");
    closer.names = {"Fresh"};
    if (newService)
        newService->click();
    check(ActLib::jestservice == servicesBefore + 1, "a new service with a free name is made");

    // Deleting a service: its file goes, the windows forget it.
    QTreeWidget *serviceList = treeWith(&services, "Player");
    QPushButton *deleteService = buttonNamed(&services, "route.editor.activity.service.window.button.b.delete.action.event");
    Service *ai = ActLib::GetServiceByName("AI");
    if (serviceList && deleteService && ai) {
        for (int row = 0; row < serviceList->topLevelItemCount(); ++row)
            if (ActLib::Services.value(serviceList->topLevelItem(row)->type()) == ai)
                serviceList->setCurrentItem(serviceList->topLevelItem(row));
        emit serviceList->itemClicked(serviceList->currentItem(), 0);
        const QString file = ai->pathid;
        deleteService->click();
        check(!QFile::exists(file) && ActLib::GetServiceByName("AI") == nullptr && !services.serviceProperties->isEnabled(),
              "a deleted service loses its file and leaves the windows");
    } else {
        check(false, "the service window has its list and buttons");
    }
    QTreeWidget *trafficList = treeWith(&traffic, "Traffic");
    QPushButton *deleteTraffic = buttonNamed(&traffic, "route.editor.activity.traffic.window.button.b.delete");
    if (trafficList && deleteTraffic && trafficFile) {
        QTreeWidgetItem *row = trafficList->findItems("Traffic", Qt::MatchExactly).value(0);
        trafficList->setCurrentItem(row);
        emit trafficList->itemClicked(row, 0);
        const QString file = trafficFile->pathid;
        deleteTraffic->click();
        check(!QFile::exists(file) && ActLib::GetTrafficByName("Traffic") == nullptr,
              "a deleted traffic pattern loses its file");
    } else {
        check(false, "the traffic window has its list and buttons");
    }
    tools.actServiceOpenEnabled();
    tools.actTrafficOpenEnabled();

    // At random: dialogs cancelled, then confirmed; without and with a
    // track database; then with no activity chosen.
    TSectionDAT sections(false, false);
    TDB emptyDatabase(&sections, false);
    // TSRE_ACTIVITY_UI_TRACE logs each action; TSRE_ACTIVITY_UI_SEED tries
    // other sequences.
    const bool trace = qEnvironmentVariableIsSet("TSRE_ACTIVITY_UI_TRACE");
    bool seedSet = false;
    const quint32 seed = qEnvironmentVariableIntValue("TSRE_ACTIVITY_UI_SEED", &seedSet);
    int actions = 0, undos = 0, restores = 0;
    Undo::Clear();
    const QMetaObject::Connection restoring = QObject::connect(
            ActivityNotifier::instance(), &ActivityNotifier::restored, [&] { ++restores; });
    for (int round = 0; round < 4; ++round) {
        Monkey monkey((seedSet ? seed : 20261013) + round);
        monkey.windows = windows;
        monkey.trace = trace;
        closer.accept = round % 2 == 1;
        closer.trace = trace;
        closer.random = &monkey.random;
        closer.names = {"", "New", "x/y", "Player", QString("Monkey%1").arg(round)};
        Game::trackDB = round >= 2 ? &emptyDatabase : nullptr;
        for (QWidget *window : windows)
            window->show();
        monkey.run(1500);
        if (activities) {
            activities->setCurrentIndex(-1);
            emit activities->textActivated(QString());
        }
        events.showEvents(nullptr);
        timetable.showTimetable(nullptr);
        monkey.run(300);
        actions += monkey.actions;
        undos += monkey.undos;
        if (activities && activities->count() > 0) {
            activities->setCurrentIndex(0);
            emit activities->textActivated(activities->currentText());
        }
        tools.actEventsOpenEnabled();
        tools.actServiceOpenEnabled();
        tools.actTrafficOpenEnabled();
        tools.actTimetableOpenEnabled();
    }
    Game::trackDB = nullptr;
    QObject::disconnect(restoring);
    check(actions > 4000 && restores > 50,
          QString("the windows survive %1 random actions, %2 undos (%3 restoring activity data), %4 dialogs answered")
          .arg(actions).arg(undos).arg(restores).arg(closer.answered));
    Undo::Clear();

    for (QWidget *window : windows)
        window->hide();
    // The library forgets the test route.
    for (int id = firstAct; id < ActLib::jestact; ++id) ActLib::Act[id] = nullptr;
    for (int id = firstService; id < ActLib::jestservice; ++id) ActLib::Services[id] = nullptr;
    for (int id = firstTraffic; id < ActLib::jesttraffic; ++id) ActLib::Traffics[id] = nullptr;
    for (int id = firstPath; id < ActLib::jestpath; ++id) ActLib::Paths[id] = nullptr;
    if (!routeListed)
        ActLib::route.remove("TEST");

    QHashSeed::resetRandomGlobalSeed();
    qInfo().noquote() << QString("activity-ui: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
