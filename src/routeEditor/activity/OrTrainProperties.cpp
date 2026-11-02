#include <routeEditor/activity/OrTrainProperties.h>
#include <routeEditor/activity/ActivityEdit.h>
#include <routeEditor/activity/OrCommandPicker.h>
#include <tsre/gui/GuiFunct.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/Game.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/ActivityTrains.h>
#include <tsre/trains/Path.h>

namespace {

enum Column { StationColumn, ArrivalColumn, DepartureColumn, CommandsColumn, StationCommandsColumn, Columns };
// An item's data: its stop's place in the train (-1: a station it has no
// stop at), whether it has times to edit, its station (-1: one the
// timetable does not have), where it is (Place); for a station the
// timetable does not have, the station it goes before and its name.
constexpr int StopRole = Qt::UserRole;
constexpr int TimedRole = Qt::UserRole + 1;
constexpr int StationRole = Qt::UserRole + 2;
constexpr int PlaceRole = Qt::UserRole + 3;
constexpr int BeforeRole = Qt::UserRole + 4;
constexpr int NameRole = Qt::UserRole + 5;
enum class Place { OnPath, NoStop, NotInTimetable, OffPath, NotInRoute };
const QColor OffPathColour(255, 200, 0, 80);
const QColor NotInRouteColour(255, 0, 0, 60);

constexpr OrTimetable::TrainCommands TrainCommandKinds[4] = {
    OrTimetable::TrainCommands::Start, OrTimetable::TrainCommands::Note, OrTimetable::TrainCommands::Dispose,
    OrTimetable::TrainCommands::Speed};

// What of the commands TSRE does not run yet ("$wait", "$create /ahead").
QStringList notRun(const QVector<OrTimetable::Command> &commands) {
    QStringList out;
    for (const OrTimetable::Command &command : commands) {
        if (!OrTimetableCommandRun(command.name)) {
            out << "$" + command.name;
            continue;
        }
        for (const OrTimetable::Qualifier &qualifier : command.qualifiers)
            if (!OrTimetableQualifierRun(command.name, qualifier.name))
                out << QString("$%1 /%2").arg(command.name, qualifier.name);
    }
    return out;
}

QString notRunText(const QStringList &items) {
    //% "Not run by TSRE yet (kept in the timetable): %1"
    return items.isEmpty() ? QString() : qtTrId("route.editor.activity.or.train.not.run").arg(items.join(", "));
}

const QColor NotRunColour(200, 120, 0);

constexpr int FieldPlaces[4] = {OrCommandSpec::Start, OrCommandSpec::Note, OrCommandSpec::Dispose,
                                OrCommandSpec::SpeedRow};

// The stops' command cells: a "…" at their right end opens the picker
// (double-click still edits the text).
class CommandCells : public QStyledItemDelegate {
public:
    CommandCells(QObject *parent, std::function<void(const QModelIndex &)> pick)
        : QStyledItemDelegate(parent), pick(std::move(pick)) {
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        if (!commands(index)) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        // The cell as it is, its text ending before the "…".
        QStyleOptionViewItem cell(option);
        initStyleOption(&cell, index);
        const QWidget *view = option.widget;
        QStyle *style = view != nullptr ? view->style() : QApplication::style();
        const QRect at = button(option.rect);
        cell.text = cell.fontMetrics.elidedText(cell.text, Qt::ElideRight, std::max(0, at.left() - option.rect.left() - 8));
        style->drawControl(QStyle::CE_ItemViewItem, &cell, painter, view);
        OrCommandPicker::Icon(view).paint(painter, at.adjusted(1, 0, -1, 0), Qt::AlignCenter,
                                          (option.state & QStyle::State_Enabled) ? QIcon::Normal : QIcon::Disabled);
    }
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override {
        const QEvent::Type type = event->type();
        if (commands(index) && (type == QEvent::MouseButtonRelease || type == QEvent::MouseButtonDblClick
                                || type == QEvent::MouseButtonPress)) {
            const QMouseEvent *mouse = static_cast<QMouseEvent *>(event);
            if (button(option.rect).contains(mouse->pos())) {
                if (type == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton)
                    pick(index);
                return true;
            }
        }
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

private:
    std::function<void(const QModelIndex &)> pick;
    // A station's commands; a stop's own where the train stops there.
    static bool commands(const QModelIndex &index) {
        const QModelIndex first = index.sibling(index.row(), StationColumn);
        return (index.column() == StationCommandsColumn && first.data(StationRole).toInt() >= 0)
                || (index.column() == CommandsColumn && first.data(StopRole).toInt() >= 0);
    }
    static QRect button(const QRect &cell) {
        return QRect(cell.right() - 19, cell.top(), 20, cell.height());
    }
};

enum ExtraColumn { KeyColumn, ValueColumn };
// The cars' columns (task scripting 03), and the number's roles: the
// consist's number, the timetable's given.
enum CarColumn { VehicleColumn, CarNumberColumn, CarTagsColumn };
constexpr int ConsistNumberRole = Qt::UserRole + 20;
constexpr int GivenNumberRole = Qt::UserRole + 21;

// The own data's values edited in place, not their keys (every train's: ✎
// renames one).
class ValueCells : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        return index.column() == ValueColumn ? QStyledItemDelegate::createEditor(parent, option, index) : nullptr;
    }
};

// The cars' numbers and tags edited in place, not their vehicles.
class CarCells : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        return index.column() != VehicleColumn ? QStyledItemDelegate::createEditor(parent, option, index) : nullptr;
    }
};

// A key as typed: letters made lower case (Open Rails reads a row's first
// cell so), only letters, digits and '_' taken.
class KeyValidator : public QValidator {
public:
    using QValidator::QValidator;
    State validate(QString &input, int &) const override {
        input = input.toLower();
        return input.isEmpty() ? Intermediate : OrTimetable::ExtraKey(input) ? Acceptable : Invalid;
    }
};

// The key asked for, empty when cancelled.
QString askKey(QWidget *parent, const QString &title, const QString &label, const QString &key) {
    QInputDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setTextValue(key);
    if (QLineEdit *line = dialog.findChild<QLineEdit *>())
        line->setValidator(new KeyValidator(line));
    return dialog.exec() == QDialog::Accepted ? dialog.textValue().trimmed() : QString();
}

// A stop's first time (-1 none).
int firstTime(const OrTimetable::Timing &timing) {
    return timing.arrival >= 0 ? timing.arrival : timing.departure >= 0 ? timing.departure : timing.passing;
}

}

OrTrainProperties::OrTrainProperties(QWidget *parent) : QWidget(parent) {
    setObjectName("orTrainProperties");
    const char *shownNames[3] = {"orTrainName", "orTrainComment", "orTrainFile"};
    for (int k = 0; k < 3; ++k) {
        shownFields[k].setObjectName(shownNames[k]);
        shownFields[k].setReadOnly(true);
        shownFields[k].setEnabled(false);
    }
    // Chosen from the route's paths and the consists, or typed (a consist
    // cell may join several: "a + b $reverse").
    for (QComboBox *box : {&pathBox, &consistBox}) {
        box->setEditable(true);
        box->setInsertPolicy(QComboBox::NoInsert);
        box->setStyleSheet("combobox-popup: 0;");
        box->setMaxVisibleItems(35);
        box->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    pathBox.setObjectName("orTrainPath");
    consistBox.setObjectName("orTrainConsist");
    QObject::connect(&pathBox, QOverload<int>::of(&QComboBox::activated), this, &OrTrainProperties::pathEdited);
    QObject::connect(pathBox.lineEdit(), &QLineEdit::editingFinished, this, &OrTrainProperties::pathEdited);
    QObject::connect(&consistBox, QOverload<int>::of(&QComboBox::activated), this, &OrTrainProperties::consistEdited);
    QObject::connect(consistBox.lineEdit(), &QLineEdit::editingFinished, this, &OrTrainProperties::consistEdited);
    startTime.setDisplayFormat("HH:mm:ss");
    startTime.setObjectName("orTrainStart");
    const char *names[4] = {"orTrainStartCommands", "orTrainNote", "orTrainDispose", "orTrainSpeed"};
    const QString placeholders[4] = {"$create=04:30", "$acc=2 $follow=T2", "$triggers=T3", "$max=120"};
    for (int k = 0; k < 4; ++k) {
        commandFields[k].setObjectName(names[k]);
        commandFields[k].setPlaceholderText(placeholders[k]);
        QObject::connect(&commandFields[k], &QLineEdit::editingFinished, this,
                         [this, k] { commandsEdited(TrainCommandKinds[k]); });
        QAction *pick = commandFields[k].addAction(OrCommandPicker::Icon(this), QLineEdit::TrailingPosition);
        pick->setObjectName(QString(names[k]) + "Pick");
        //% "Choose the commands from a list..."
        pick->setToolTip(qtTrId("route.editor.activity.or.train.pick"));
        QObject::connect(pick, &QAction::triggered, this, [this, k] { pickTrainCommands(k); });
    }
    // TSRE's fields: their rows and the documents' names in the tooltips.
    const QString fieldTips[TrainFields::Count] = {
        //% "Its category, as passengers see it: IR, TLK, Os."
        qtTrId("route.editor.activity.or.train.field.category.tooltip"),
        //% "Its number: 46140."
        qtTrId("route.editor.activity.or.train.field.number.tooltip"),
        //% "Its own name: SZYNDZIELNIA."
        qtTrId("route.editor.activity.or.train.field.own.name.tooltip"),
        //% "Who runs it: POLREGIO."
        qtTrId("route.editor.activity.or.train.field.operator.tooltip"),
        //% "Its line: S1."
        qtTrId("route.editor.activity.or.train.field.line.tooltip"),
        //% "Where it comes from when it starts before the route; empty: its first stop's station (greyed)."
        qtTrId("route.editor.activity.or.train.field.origin.tooltip"),
        //% "Where it goes when it runs on beyond the route; empty: its last stop's station (greyed)."
        qtTrId("route.editor.activity.or.train.field.destination.tooltip")};
    for (int f = 0; f < TrainFields::Count; ++f) {
        fieldBoxes[f].setObjectName(QString("orTrainField.") + TrainFields::key(f));
        //% "The timetable's row #tsre.%1; train.%1 and row.%1 in display documents."
        const QString names = qtTrId("route.editor.activity.or.train.field.row").arg(TrainFields::key(f));
        fieldBoxes[f].setToolTip(fieldTips[f] + "\n" + names);
        QObject::connect(&fieldBoxes[f], &QLineEdit::editingFinished, this, [this, f] { fieldEdited(f); });
        QObject::connect(&fieldBoxes[f], &QLineEdit::textEdited, this, &OrTrainProperties::showShownAs);
    }
    shownAs.setObjectName("orTrainShownAs");
    shownAs.setWordWrap(true);
    shownAs.setContentsMargins(3, 0, 0, 0);
    shownAs.setToolTip(
        //% "The train as the displays' examples show it: its line, category, number and name (else its name in the timetable), where it comes from and where it goes."
        qtTrId("route.editor.activity.or.train.shown.as.tooltip"));
    extras.setObjectName("orTrainExtras");
    extras.setColumnCount(2);
    extras.setHeaderLabels({
        //% "Key"
        qtTrId("route.editor.activity.or.train.column.key"),
        //% "Value"
        qtTrId("route.editor.activity.or.train.column.value")});
    extras.setRootIsDecorated(false);
    extras.setUniformRowHeights(true);
    extras.setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                           | QAbstractItemView::SelectedClicked);
    extras.setItemDelegate(new ValueCells(&extras));
    extras.header()->resizeSection(KeyColumn, 110);
    extras.setToolTip(
        //% "The train's own data for displays and scripts: the timetable's rows #tsre.extra.<key>, train.extra.<key> and row.extra.<key> in display documents. Double-click a value to change it. The keys are every train's: + adds one, ✎ renames the chosen one, − removes it."
        qtTrId("route.editor.activity.or.train.extras.tooltip"));
    // About four rows: it scrolls with more.
    extras.setFixedHeight(extras.header()->sizeHint().height() + 4 * (extras.fontMetrics().height() + 6)
                          + 2 * extras.frameWidth());
    QObject::connect(&extras, &QTreeWidget::itemChanged, this, &OrTrainProperties::extraEdited);
    carsTable.setObjectName("orTrainCars");
    carsTable.setColumnCount(3);
    carsTable.setHeaderLabels({
        //% "Vehicle"
        qtTrId("route.editor.activity.or.train.column.vehicle"),
        //% "Number"
        qtTrId("route.editor.activity.or.train.column.car.number"),
        //% "Tags"
        qtTrId("route.editor.activity.or.train.column.tags")});
    carsTable.setRootIsDecorated(false);
    carsTable.setUniformRowHeights(true);
    carsTable.setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                              | QAbstractItemView::SelectedClicked);
    carsTable.setItemDelegate(new CarCells(&carsTable));
    carsTable.header()->resizeSection(VehicleColumn, 170);
    carsTable.header()->resizeSection(CarNumberColumn, 60);
    carsTable.setToolTip(
        //% "The train's cars as its consists list them (a reversed consist the other way round), their numbers and tags for displays: the timetable's row #tsre.cars; car.number, car.tags and train.cars in display documents. A greyed number is the consist's (its UiD): type another, '-' for none, or clear it for the consist's again. Tags are words separated by spaces, such as A, B, WR, bike."
        qtTrId("route.editor.activity.or.train.cars.tooltip"));
    // About five rows: it scrolls with more.
    carsTable.setFixedHeight(carsTable.header()->sizeHint().height() + 5 * (carsTable.fontMetrics().height() + 6)
                             + 2 * carsTable.frameWidth());
    QObject::connect(&carsTable, &QTreeWidget::itemChanged, this, &OrTrainProperties::carEdited);
    QObject::connect(&extras, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current) {
        keyButtons[1].setEnabled(current != nullptr && shown());
        keyButtons[2].setEnabled(current != nullptr && shown());
    });
    // (Double-click on a key: rename it.)
    QObject::connect(&extras, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int column) {
        if (column == KeyColumn)
            QMetaObject::invokeMethod(this, [this] { renameKeyAsked(); }, Qt::QueuedConnection);
    });
    const char *keyNames[3] = {"orTrainExtraAdd", "orTrainExtraRename", "orTrainExtraRemove"};
    const QString keyTexts[3] = {"+", "✎", "−"};
    const QString keyTips[3] = {
        //% "Add a key, every train's in the timetable..."
        qtTrId("route.editor.activity.or.train.extra.add"),
        //% "Rename the chosen key, every train's..."
        qtTrId("route.editor.activity.or.train.extra.rename"),
        //% "Remove the chosen key and its values, every train's"
        qtTrId("route.editor.activity.or.train.extra.remove")};
    for (int k = 0; k < 3; ++k) {
        keyButtons[k].setObjectName(keyNames[k]);
        keyButtons[k].setText(keyTexts[k]);
        keyButtons[k].setToolTip(keyTips[k]);
        keyButtons[k].setFixedWidth(26);
    }
    QObject::connect(&keyButtons[0], &QToolButton::clicked, this, &OrTrainProperties::addKeyAsked);
    QObject::connect(&keyButtons[1], &QToolButton::clicked, this, &OrTrainProperties::renameKeyAsked);
    QObject::connect(&keyButtons[2], &QToolButton::clicked, this, &OrTrainProperties::removeKeyAsked);
    stops.setObjectName("orTrainStops");
    stops.setColumnCount(Columns);
    stops.setHeaderLabels({
        //% "Station"
        qtTrId("route.editor.activity.or.train.column.station"),
        //% "Arrival"
        qtTrId("route.editor.activity.or.train.column.arrival"),
        //% "Departure"
        qtTrId("route.editor.activity.or.train.column.departure"),
        //% "Commands"
        qtTrId("route.editor.activity.or.train.column.commands"),
        //% "Station's commands"
        qtTrId("route.editor.activity.or.train.column.station.commands")});
    stops.setRootIsDecorated(false);
    stops.setUniformRowHeights(true);
    stops.setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                          | QAbstractItemView::SelectedClicked);
    stops.header()->resizeSection(StationColumn, 170);
    stops.header()->resizeSection(ArrivalColumn, 70);
    stops.header()->resizeSection(DepartureColumn, 70);
    stops.header()->resizeSection(CommandsColumn, 150);
    stops.setItemDelegate(new CommandCells(this, [this](const QModelIndex &index) {
        const QModelIndex first = index.sibling(index.row(), StationColumn);
        const int stop = first.data(StopRole).toInt();
        const int station = first.data(StationRole).toInt();
        const int column = index.column();
        // After the view's mouse event.
        QMetaObject::invokeMethod(this, [this, stop, station, column] { pickStopCommands(stop, station, column); },
                                  Qt::QueuedConnection);
    }));
    stops.setToolTip(
        //% "Arrival and departure as HH:mm or HH:mm:ss; Commands this train's at the station, Station's commands every train's there (double-click to change, … to choose them from a list)."
        qtTrId("route.editor.activity.or.train.stops.tooltip"));
    problems.setWordWrap(true);
    problems.setStyleSheet("QLabel { color : rgb(200, 120, 0); }");
    problems.setContentsMargins(3, 0, 3, 0);
    problems.setObjectName("orTrainProblems");

    // One grid, a label and a box a row: what the train is, its data, then
    // its commands. Short boxes two in a row, the second's label in the
    // third column.
    info = new QWidget(this);
    QGridLayout *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);
    int row = 0;
    auto section = [grid, &row](const QString &text) {
        if (row > 0)
            grid->addItem(new QSpacerItem(0, 6), row++, 0);
        grid->addWidget(GuiFunct::newTQLabel(text), row++, 0, 1, 4);
    };
    auto label = [](const QString &text, int left) {
        QLabel *label = new QLabel(text);
        label->setContentsMargins(left, 0, 0, 0);
        return label;
    };
    auto field = [grid, &row, &label](const QString &text, QWidget *box) {
        grid->addWidget(label(text, 3), row, 0);
        grid->addWidget(box, row++, 1, 1, 3);
    };
    auto pair = [grid, &row, &label](const QString &text, QWidget *box, const QString &secondText, QWidget *second) {
        grid->addWidget(label(text, 3), row, 0);
        grid->addWidget(box, row, 1);
        grid->addWidget(label(secondText, 6), row, 2);
        grid->addWidget(second, row++, 3);
    };
    //% "Info:"
    section(qtTrId("route.editor.activity.or.train.section.info"));
    //% "Train:"
    field(qtTrId("route.editor.activity.or.train.field.name"), &shownFields[0]);
    //% "Path:"
    field(qtTrId("route.editor.activity.or.train.field.path"), &pathBox);
    //% "Consist:"
    field(qtTrId("route.editor.activity.or.train.field.consist"), &consistBox);
    //% "Comment:"
    field(qtTrId("route.editor.activity.or.train.field.comment"), &shownFields[1]);
    //% "Timetable:"
    field(qtTrId("route.editor.activity.or.train.field.file"), &shownFields[2]);
    //% "Start Time:"
    field(qtTrId("route.editor.activity.or.train.field.start.time"), &startTime);
    //% "Train data:"
    section(qtTrId("route.editor.activity.or.train.section.data"));
    pair(
        //% "Category:"
        qtTrId("route.editor.activity.or.train.field.category"), &fieldBoxes[TrainFields::Category],
        //% "Number:"
        qtTrId("route.editor.activity.or.train.field.number"), &fieldBoxes[TrainFields::Number]);
    //% "Name:"
    field(qtTrId("route.editor.activity.or.train.field.own.name"), &fieldBoxes[TrainFields::Name]);
    pair(
        //% "Operator:"
        qtTrId("route.editor.activity.or.train.field.operator"), &fieldBoxes[TrainFields::Operator],
        //% "Line:"
        qtTrId("route.editor.activity.or.train.field.line"), &fieldBoxes[TrainFields::Line]);
    pair(
        //% "Origin:"
        qtTrId("route.editor.activity.or.train.field.origin"), &fieldBoxes[TrainFields::Origin],
        //% "Destination:"
        qtTrId("route.editor.activity.or.train.field.destination"), &fieldBoxes[TrainFields::Destination]);
    //% "Shown as:"
    field(qtTrId("route.editor.activity.or.train.field.shown.as"), &shownAs);
    QWidget *own = new QWidget;
    QHBoxLayout *ownLayout = new QHBoxLayout(own);
    ownLayout->setContentsMargins(0, 0, 0, 0);
    ownLayout->setSpacing(2);
    ownLayout->addWidget(&extras, 1);
    QVBoxLayout *keyLayout = new QVBoxLayout;
    keyLayout->setSpacing(2);
    for (QToolButton &button : keyButtons)
        keyLayout->addWidget(&button);
    keyLayout->addStretch(1);
    ownLayout->addLayout(keyLayout);
    //% "Own data:"
    grid->addWidget(label(qtTrId("route.editor.activity.or.train.field.extras"), 3), row, 0, Qt::AlignTop);
    grid->addWidget(own, row++, 1, 1, 3);
    //% "Cars:"
    grid->addWidget(label(qtTrId("route.editor.activity.or.train.field.cars"), 3), row, 0, Qt::AlignTop);
    grid->addWidget(&carsTable, row++, 1, 1, 3);
    //% "Commands:"
    section(qtTrId("route.editor.activity.or.train.section.commands"));
    //% "Start:"
    field(qtTrId("route.editor.activity.or.train.field.start"), &commandFields[0]);
    //% "Note:"
    field(qtTrId("route.editor.activity.or.train.field.note"), &commandFields[1]);
    //% "Dispose:"
    field(qtTrId("route.editor.activity.or.train.field.dispose"), &commandFields[2]);
    //% "Speed row:"
    field(qtTrId("route.editor.activity.or.train.field.speed"), &commandFields[3]);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    QVBoxLayout *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);
    infoLayout->addLayout(grid);
    infoLayout->addWidget(&problems);
    // Until the window takes it: above the stops.
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(info);
    layout->addWidget(&stops, 1);
    QObject::connect(&startTime, &QTimeEdit::editingFinished, this, &OrTrainProperties::startEdited);
    QObject::connect(&stops, &QTreeWidget::itemChanged, this, &OrTrainProperties::stopEdited);
    showTrain(nullptr, -1, -1, {});
}

void OrTrainProperties::showTrain(Activity *chosenSet, int chosenTimetable, int chosenTrain, const QStringList &found) {
    // The same train again (after an edit): its chosen key kept.
    const QString wasKey = chosenSet == set && chosenTimetable == timetable && chosenTrain == train ? chosenKey()
                                                                                                    : QString();
    set = chosenSet;
    timetable = chosenTimetable;
    train = chosenTrain;
    filling = true;
    stops.clear();
    const bool valid = shown();
    startTime.setEnabled(valid);
    pathBox.setEnabled(valid);
    consistBox.setEnabled(valid);
    for (QLineEdit &field : commandFields) {
        field.setEnabled(valid);
        if (!valid) {
            field.clear();
            field.setStyleSheet(QString());
            field.setToolTip(QString());
        }
    }
    for (QLineEdit &field : fieldBoxes) {
        field.setEnabled(valid);
        if (!valid) {
            field.clear();
            field.setPlaceholderText(QString());
        }
    }
    extras.clear();
    extras.setEnabled(valid);
    carsTable.clear();
    carsTable.setEnabled(valid);
    for (QToolButton &button : keyButtons)
        button.setEnabled(false);
    keyButtons[0].setEnabled(valid);
    if (!valid) {
        shownAs.clear();
        QSignalBlocker blocker(startTime);
        startTime.setTime(QTime(0, 0));
        for (QLineEdit &field : shownFields)
            field.clear();
        pathBox.clear();
        consistBox.clear();
        problems.clear();
        problems.hide();
        filling = false;
        return;
    }
    const OrTimetable &t = *set->timetables[size_t(timetable)];
    const OrTimetable::Train &one = t.trains[train];
    const QString texts[3] = {one.name, one.comment, QFileInfo(t.file).fileName()};
    for (int k = 0; k < 3; ++k) {
        shownFields[k].setText(texts[k]);
        shownFields[k].setCursorPosition(0);
        shownFields[k].setToolTip(texts[k]);
    }
    {
        QSignalBlocker pathBlocker(pathBox);
        QSignalBlocker consistBlocker(consistBox);
        pathBox.clear();
        for (Path *path : ActLib::RoutePaths(ActivityTrains::RouteDirectory(set)))
            pathBox.addItem(path->nameId);
        pathBox.setCurrentIndex(-1);
        pathBox.setEditText(one.path);
        pathBox.setToolTip(one.pathText.isEmpty() ? one.path : one.pathText);
        consistBox.clear();
        ConLib::loadSimpleList(Game::root);
        for (const QString &file : ConLib::conFileList) {
            QString name = file.section('/', -1);
            if (name.endsWith(".con", Qt::CaseInsensitive))
                name.chop(4);
            consistBox.addItem(name);
        }
        consistBox.setCurrentIndex(-1);
        consistBox.setEditText(one.consist);
        consistBox.setToolTip(one.consist);
        pathBox.lineEdit()->setCursorPosition(0);
        consistBox.lineEdit()->setCursorPosition(0);
    }
    {
        QSignalBlocker blocker(startTime);
        startTime.setTime(QTime::fromMSecsSinceStartOfDay(std::max(0, one.startTime) % 86400 * 1000));
    }
    startTime.setEnabled(!one.isStatic);
    for (int k = 0; k < 4; ++k) {
        QLineEdit &field = commandFields[k];
        const QVector<OrTimetable::Command> &commands = OrTimetable::TrainCommandsOf(one, TrainCommandKinds[k]);
        const QStringList missing = notRun(commands);
        QSignalBlocker blocker(field);
        field.setText(OrTimetable::CommandsText(commands));
        field.setCursorPosition(0);
        field.setToolTip(notRunText(missing));
        field.setStyleSheet(missing.isEmpty() ? QString() : QString("QLineEdit { color: %1; }").arg(NotRunColour.name()));
    }
    // A train static by its column's head has no start cell.
    commandFields[0].setEnabled(!one.header.startsWith('$'));
    for (int f = 0; f < TrainFields::Count; ++f) {
        fieldBoxes[f].setText(one.fields[f]);
        fieldBoxes[f].setCursorPosition(0);
    }
    for (const QString &key : t.extraKeys) {
        QTreeWidgetItem *item = new QTreeWidgetItem(&extras, {key, one.extra.value(key)});
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setToolTip(KeyColumn, OrTimetable::ExtraRow + key);
        item->setToolTip(ValueColumn, item->text(ValueColumn));
    }
    chooseKey(wasKey, false);
    // Its cars (task scripting 03): its consists' vehicles, each one's
    // number from #tsre.cars (else its consist's, greyed) and tags; a row
    // that does not fit reported, the consists' numbers shown.
    QStringList shownProblems = found;
    QVector<Sim::ConsistCar> vehicles;
    const bool carsListed = Sim::TimetableTrainCars(one, vehicles);
    QVector<TrainCar> given;
    const bool read = TrainCar::Parse(one.cars, given);
    const bool fits = read && given.size() == vehicles.size();
    if (!read)
        //% "#tsre.cars '%1' is not a bracket a vehicle ([272 A]); the consists' numbers are shown."
        shownProblems << qtTrId("route.editor.activity.or.train.cars.unread").arg(one.cars);
    else if (carsListed && !given.isEmpty() && !fits)
        //% "#tsre.cars has %1 cars, the train %2; the consists' numbers are shown."
        shownProblems << qtTrId("route.editor.activity.or.train.cars.count").arg(given.size()).arg(vehicles.size());
    const QColor consistColour = carsTable.palette().color(QPalette::Disabled, QPalette::Text);
    for (int k = 0; k < vehicles.size(); ++k) {
        const QString consistNumber = QString::number(vehicles[k].uid);
        const bool own = fits && !given[k].consistNumber;
        const QString number = own ? (given[k].number.isEmpty() ? QString("-") : given[k].number) : consistNumber;
        QTreeWidgetItem *item = new QTreeWidgetItem(&carsTable, {vehicles[k].name, number,
                                                                 fits ? given[k].tags : QString()});
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(CarNumberColumn, ConsistNumberRole, consistNumber);
        item->setData(CarNumberColumn, GivenNumberRole, own);
        if (!own)
            item->setForeground(CarNumberColumn, consistColour);
        item->setToolTip(VehicleColumn, vehicles[k].name);
    }
    carsTable.setEnabled(carsListed);
    // Its stations along its path, in its order, as the simulation finds
    // its stops (Sim::TimetablePathStations): one it has no stop at
    // greyed, one the timetable does not have in italics as well (a time
    // typed adds both); then its stops not on its path (yellow) and at a
    // station no platform of the route has (red). Without the route's
    // track or the path: its stops in the timetable's order.
    TDB *tdb = Game::trackDB;
    Path *path = tdb != nullptr && !one.path.isEmpty()
            ? ActLib::GetPathByName(one.pathName(), ActivityTrains::RouteDirectory(set)) : nullptr;
    QStringList keys;
    for (const OrTimetable::Station &station : t.stations)
        keys << station.key;
    const QVector<Sim::PathStation> along = Sim::TimetablePathStations(tdb, path, keys);
    QSet<QString> routeStations;
    if (tdb != nullptr)
        for (const auto &entry : tdb->trackItems)
            if (entry.second != nullptr && entry.second->type == "platformitem")
                routeStations.insert(entry.second->stationName.trimmed().toLower());
    const auto stationOf = [&t](const QString &key) {
        for (int s = 0; s < t.stations.size(); ++s)
            if (t.stations[s].key == key)
                return s;
        return -1;
    };
    const QColor greyed = stops.palette().color(QPalette::Disabled, QPalette::Text);
    const auto addStop = [&](int k, Place place) {
        const OrTimetable::Stop &stop = one.stops[k];
        const int s = stop.station;
        const bool timed = stop.timing.passing < 0 && (stop.timing.arrival >= 0 || stop.timing.departure >= 0);
        // A request stop's or early one's mark beside the station (one time).
        QString station = t.stations[s].name;
        if (stop.timing.request)
            //% "%1 (request stop)"
            station = qtTrId("route.editor.activity.or.train.request").arg(station);
        else if (stop.timing.early)
            //% "%1 (may leave early)"
            station = qtTrId("route.editor.activity.or.train.early").arg(station);
        QTreeWidgetItem *item = new QTreeWidgetItem(&stops, {station,
            timed ? OrTimetable::TimeText(stop.timing.arrival) : stop.timing.text,
            timed ? OrTimetable::TimeText(stop.timing.departure) : QString(),
            OrTimetable::CommandsText(stop.commands), OrTimetable::CommandsText(t.stations[s].commands)});
        item->setData(StationColumn, StopRole, k);
        item->setData(StationColumn, TimedRole, timed && !one.isStatic);
        item->setData(StationColumn, StationRole, s);
        item->setData(StationColumn, PlaceRole, int(place));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        for (int column = 0; column < Columns; ++column)
            item->setToolTip(column, item->text(column));
        const QStringList ownMissing = notRun(stop.commands);
        const QStringList stationMissing = notRun(t.stations[s].commands);
        if (!ownMissing.isEmpty()) {
            item->setForeground(CommandsColumn, NotRunColour);
            item->setToolTip(CommandsColumn, notRunText(ownMissing));
        }
        if (!stationMissing.isEmpty()) {
            item->setForeground(StationCommandsColumn, NotRunColour);
            item->setToolTip(StationCommandsColumn, notRunText(stationMissing));
        }
        if (place == Place::OffPath || place == Place::NotInRoute) {
            for (int column = 0; column < Columns; ++column)
                item->setBackground(column, place == Place::OffPath ? OffPathColour : NotInRouteColour);
            item->setToolTip(StationColumn, place == Place::OffPath ?
                //% "Not on the train's path: it does not stop here."
                qtTrId("route.editor.activity.or.train.off.path") :
                //% "No platform of the route has this station."
                qtTrId("route.editor.activity.or.train.not.in.route"));
        }
    };
    QVector<bool> listed(one.stops.size(), false);
    for (int i = 0; i < along.size(); ++i) {
        const int s = stationOf(along[i].key);
        int k = -1;
        for (int j = 0; s >= 0 && j < one.stops.size() && k < 0; ++j)
            if (one.stops[j].station == s)
                k = j;
        if (k >= 0) {
            listed[k] = true;
            addStop(k, Place::OnPath);
            continue;
        }
        QTreeWidgetItem *item = new QTreeWidgetItem(&stops, {s >= 0 ? t.stations[s].name : along[i].name, QString(),
            QString(), QString(), s >= 0 ? OrTimetable::CommandsText(t.stations[s].commands) : QString()});
        item->setData(StationColumn, StopRole, -1);
        item->setData(StationColumn, TimedRole, !one.isStatic);
        item->setData(StationColumn, StationRole, s);
        item->setData(StationColumn, PlaceRole, int(s >= 0 ? Place::NoStop : Place::NotInTimetable));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        for (int column = StationColumn; column <= CommandsColumn; ++column)
            item->setForeground(column, greyed);
        if (s >= 0) {
            //% "No stop: type a time to add one."
            item->setToolTip(StationColumn, qtTrId("route.editor.activity.or.train.no.stop"));
            continue;
        }
        // Not in the timetable: added before the next of its path's
        // stations the timetable has, else after the last before it.
        int before = -1;
        for (int j = i + 1; j < along.size() && before < 0; ++j)
            before = stationOf(along[j].key);
        for (int j = i - 1; j >= 0 && before < 0; --j)
            if (const int previous = stationOf(along[j].key); previous >= 0)
                before = previous + 1;
        item->setData(StationColumn, BeforeRole, before);
        item->setData(StationColumn, NameRole, along[i].name);
        QFont italic = item->font(StationColumn);
        italic.setItalic(true);
        item->setFont(StationColumn, italic);
        //% "Not in the timetable: a time typed adds the station and the stop."
        item->setToolTip(StationColumn, qtTrId("route.editor.activity.or.train.not.in.timetable"));
    }
    for (int k = 0; k < one.stops.size(); ++k)
        if (!listed[k])
            addStop(k, tdb == nullptr ? Place::OnPath
                       : !routeStations.contains(t.stations[one.stops[k].station].key) ? Place::NotInRoute
                       : path != nullptr ? Place::OffPath : Place::OnPath);
    // Without its own origin and destination the displays show its first
    // and last stops' stations: the boxes' placeholders. Its stops on its
    // path, in its order; without the path in the timetable's, where a
    // train the other way has them backwards.
    QVector<int> onPath;
    for (int i = 0; i < stops.topLevelItemCount(); ++i) {
        const QTreeWidgetItem *item = stops.topLevelItem(i);
        const int k = item->data(StationColumn, StopRole).toInt();
        if (k >= 0 && Place(item->data(StationColumn, PlaceRole).toInt()) == Place::OnPath)
            onPath << k;
    }
    QString from, to;
    if (!onPath.isEmpty()) {
        int first = onPath.first();
        int last = onPath.last();
        const int firstAt = firstTime(one.stops[first].timing);
        const int lastAt = firstTime(one.stops[last].timing);
        if (along.isEmpty() && firstAt >= 0 && lastAt >= 0 && firstAt > lastAt)
            std::swap(first, last);
        from = t.stations[one.stops[first].station].name;
        to = t.stations[one.stops[last].station].name;
    }
    fieldBoxes[TrainFields::Origin].setPlaceholderText(from);
    fieldBoxes[TrainFields::Destination].setPlaceholderText(to);
    showShownAs();
    problems.setText(shownProblems.join("\n"));
    problems.setVisible(!shownProblems.isEmpty());
    filling = false;
}

bool OrTrainProperties::shown() const {
    return set != nullptr && timetable >= 0 && timetable < int(set->timetables.size()) && train >= 0
            && train < set->timetables[size_t(timetable)]->trains.size();
}

void OrTrainProperties::editedLater() {
    QMetaObject::invokeMethod(this, [this] { emit edited(); }, Qt::QueuedConnection);
}

void OrTrainProperties::startEdited() {
    if (filling || set == nullptr || timetable < 0)
        return;
    OrTimetable &t = *set->timetables[size_t(timetable)];
    const int time = startTime.time().msecsSinceStartOfDay() / 1000;
    if (train < 0 || train >= t.trains.size() || t.trains[train].startTime == time)
        return;
    {
        ActivityEdit edit(set);
        t.setStartTime(train, time);
    }
    emit edited();
}

void OrTrainProperties::commandsEdited(OrTimetable::TrainCommands which) {
    if (filling || set == nullptr || timetable < 0)
        return;
    OrTimetable &t = *set->timetables[size_t(timetable)];
    if (train < 0 || train >= t.trains.size())
        return;
    const QLineEdit &field = commandFields[int(which)];
    // Unchanged (the field left without an edit): no step.
    if (OrTimetable::CommandsText(OrTimetable::Commands(OrTimetable::TypedCommands(field.text())))
            == OrTimetable::CommandsText(OrTimetable::TrainCommandsOf(t.trains[train], which)))
        return;
    {
        ActivityEdit edit(set);
        t.setTrainCommands(train, which, field.text());
    }
    editedLater();
}

void OrTrainProperties::stopEdited(QTreeWidgetItem *item, int column) {
    if (filling || set == nullptr || timetable < 0 || item == nullptr)
        return;
    OrTimetable &t = *set->timetables[size_t(timetable)];
    const int k = item->data(StationColumn, StopRole).toInt();
    const int station = item->data(StationColumn, StationRole).toInt();
    if (train < 0 || train >= t.trains.size() || k >= t.trains[train].stops.size() || station >= t.stations.size())
        return;
    const auto putBack = [this, item, column](const QString &text) {
        filling = true;
        item->setText(column, text);
        filling = false;
    };
    // A station of its path the timetable does not have: a time adds it
    // and the stop.
    if (station < 0) {
        const int time = OrTimetable::ParseTime(item->text(column).trimmed());
        if ((column != ArrivalColumn && column != DepartureColumn) || time < 0
                || !item->data(StationColumn, TimedRole).toBool()) {
            putBack(QString());
            return;
        }
        {
            ActivityEdit edit(set);
            const int added = t.addStation(item->data(StationColumn, NameRole).toString(),
                                           item->data(StationColumn, BeforeRole).toInt());
            t.addStop(train, added, time, time);
        }
        editedLater();
        return;
    }
    if (column == StationCommandsColumn) {
        if (OrTimetable::CommandsText(OrTimetable::Commands(OrTimetable::TypedCommands(item->text(column))))
                == OrTimetable::CommandsText(t.stations[station].commands))
            return;
        {
            ActivityEdit edit(set);
            t.setStationCommands(station, item->text(column));
        }
        editedLater();
        return;
    }
    // A station without a stop: a time adds one there.
    if (k < 0) {
        const QString text = item->text(column).trimmed();
        const int time = OrTimetable::ParseTime(text);
        if ((column != ArrivalColumn && column != DepartureColumn) || time < 0
                || !item->data(StationColumn, TimedRole).toBool()) {
            putBack(QString());
            return;
        }
        {
            ActivityEdit edit(set);
            t.addStop(train, station, time, time);
        }
        editedLater();
        return;
    }
    const OrTimetable::Stop &stop = t.trains[train].stops[k];
    if (column == CommandsColumn) {
        if (OrTimetable::CommandsText(OrTimetable::Commands(OrTimetable::TypedCommands(item->text(column))))
                == OrTimetable::CommandsText(stop.commands))
            return;
        {
            ActivityEdit edit(set);
            t.setStopCommands(train, k, item->text(column));
        }
        editedLater();
        return;
    }
    if (column != ArrivalColumn && column != DepartureColumn)
        return;
    const OrTimetable::Timing &was = stop.timing;
    const QString text = item->text(column).trimmed();
    const int time = text.isEmpty() ? -1 : OrTimetable::ParseTime(text);
    int arrival = column == ArrivalColumn ? time : was.arrival;
    int departure = column == DepartureColumn ? time : was.departure;
    // A request stop or an early one has one time: either column sets it.
    if ((was.request || was.early) && time >= 0)
        arrival = departure = time;
    // A passing time or a cell without times, or not a time: as it was.
    const bool timed = item->data(StationColumn, TimedRole).toBool();
    if (!timed || (!text.isEmpty() && time < 0)) {
        const bool times = was.passing < 0 && (was.arrival >= 0 || was.departure >= 0);
        putBack(column == ArrivalColumn ? (times ? OrTimetable::TimeText(was.arrival) : was.text)
                                        : (times ? OrTimetable::TimeText(was.departure) : QString()));
        return;
    }
    // Its only time gone (one time, or a request or early stop): the stop
    // gone.
    if ((arrival < 0 && departure < 0) || (time < 0 && (was.arrival == was.departure || was.request || was.early))) {
        {
            ActivityEdit edit(set);
            t.removeStop(train, k);
        }
        editedLater();
        return;
    }
    if (arrival == was.arrival && departure == was.departure)
        return;
    {
        ActivityEdit edit(set);
        t.setStopTimes(train, k, arrival, departure);
    }
    editedLater();
}

void OrTrainProperties::pathEdited() {
    if (filling || set == nullptr || timetable < 0)
        return;
    OrTimetable &t = *set->timetables[size_t(timetable)];
    const QString path = pathBox.currentText().trimmed();
    if (train < 0 || train >= t.trains.size() || t.trains[train].path == path)
        return;
    {
        ActivityEdit edit(set);
        t.setTrainPath(train, path);
    }
    editedLater();
}

void OrTrainProperties::consistEdited() {
    if (filling || set == nullptr || timetable < 0)
        return;
    OrTimetable &t = *set->timetables[size_t(timetable)];
    const QString consist = consistBox.currentText().trimmed();
    if (train < 0 || train >= t.trains.size() || t.trains[train].consist == consist)
        return;
    {
        ActivityEdit edit(set);
        t.setTrainConsist(train, consist);
    }
    editedLater();
}

void OrTrainProperties::fieldEdited(int field) {
    if (filling || set == nullptr || timetable < 0)
        return;
    OrTimetable &t = *set->timetables[size_t(timetable)];
    if (train < 0 || train >= t.trains.size()
            || OrTimetable::TypedCell(fieldBoxes[field].text()) == t.trains[train].fields[field])
        return;
    {
        ActivityEdit edit(set);
        t.setTrainField(train, field, fieldBoxes[field].text());
    }
    editedLater();
}

void OrTrainProperties::extraEdited(QTreeWidgetItem *item, int column) {
    if (filling || set == nullptr || timetable < 0 || item == nullptr || column != ValueColumn)
        return;
    OrTimetable &t = *set->timetables[size_t(timetable)];
    const QString key = item->text(KeyColumn);
    if (train < 0 || train >= t.trains.size()
            || OrTimetable::TypedCell(item->text(ValueColumn)) == t.trains[train].extra.value(key))
        return;
    {
        ActivityEdit edit(set);
        t.setTrainExtra(train, key, item->text(ValueColumn));
    }
    editedLater();
}

void OrTrainProperties::carEdited(QTreeWidgetItem *item, int column) {
    if (filling || set == nullptr || timetable < 0 || item == nullptr || column == VehicleColumn)
        return;
    OrTimetable &t = *set->timetables[size_t(timetable)];
    if (train < 0 || train >= t.trains.size())
        return;
    QVector<TrainCar> cars;
    bool any = false;
    for (int k = 0; k < carsTable.topLevelItemCount(); ++k) {
        const QTreeWidgetItem *line = carsTable.topLevelItem(k);
        TrainCar car;
        const QString number = TrainCar::Word(line->text(CarNumberColumn));
        // Its own number: given before, or typed now; cleared, the
        // consist's again.
        const bool own = line == item && column == CarNumberColumn
                ? !number.isEmpty()
                : line->data(CarNumberColumn, GivenNumberRole).toBool();
        car.consistNumber = !own;
        if (own && number != "-")
            car.number = number;
        car.tags = TrainCar::Words(line->text(CarTagsColumn));
        any = any || own || !car.tags.isEmpty();
        cars << car;
    }
    const QString cell = any ? TrainCar::Text(cars) : QString();
    if (cell == t.trains[train].cars)
        return;
    {
        ActivityEdit edit(set);
        t.setTrainCars(train, cell);
    }
    editedLater();
}

// As the guide's examples show it: "IR 46140 SZYNDZIELNIA · Kraków Główny
// – Bielsko-Biała Główna", the boxes as typed, the origin and destination
// where empty as the displays find them.
void OrTrainProperties::showShownAs() {
    if (!shown()) {
        shownAs.clear();
        return;
    }
    const auto value = [this](int field) {
        const QString typed = OrTimetable::TypedCell(fieldBoxes[field].text());
        return typed.isEmpty() ? fieldBoxes[field].placeholderText() : typed;
    };
    QStringList head;
    for (int field : {TrainFields::Line, TrainFields::Category, TrainFields::Number, TrainFields::Name})
        if (!value(field).isEmpty())
            head << value(field);
    QString text = head.isEmpty() ? shownFields[0].text() : head.join(' ');
    const QString from = value(TrainFields::Origin);
    const QString to = value(TrainFields::Destination);
    if (!from.isEmpty() || !to.isEmpty())
        text += QString(" · %1 – %2").arg(from, to);
    shownAs.setText(text);
}

QString OrTrainProperties::addExtraKey(const QString &typed) {
    if (set == nullptr || timetable < 0 || timetable >= int(set->timetables.size()))
        return QString();
    OrTimetable &t = *set->timetables[size_t(timetable)];
    const QString key = typed.trimmed().toLower();
    if (!OrTimetable::ExtraKey(key))
        //% "A key is letters, digits and '_' (in lower case)."
        return qtTrId("route.editor.activity.or.train.extra.key.invalid");
    if (t.extraKeys.contains(key))
        //% "The timetable has the key %1 already."
        return qtTrId("route.editor.activity.or.train.extra.key.exists").arg(key);
    {
        ActivityEdit edit(set);
        t.addExtraKey(key);
    }
    emit edited();
    chooseKey(key, false);
    return QString();
}

QString OrTrainProperties::renameExtraKey(const QString &key, const QString &typed) {
    if (set == nullptr || timetable < 0 || timetable >= int(set->timetables.size()))
        return QString();
    OrTimetable &t = *set->timetables[size_t(timetable)];
    const QString to = typed.trimmed().toLower();
    if (!t.extraKeys.contains(key) || to == key)
        return QString();
    if (!OrTimetable::ExtraKey(to))
        return qtTrId("route.editor.activity.or.train.extra.key.invalid");
    if (t.extraKeys.contains(to))
        return qtTrId("route.editor.activity.or.train.extra.key.exists").arg(to);
    {
        ActivityEdit edit(set);
        t.renameExtraKey(key, to);
    }
    emit edited();
    chooseKey(to, false);
    return QString();
}

void OrTrainProperties::removeExtraKey(const QString &key) {
    if (set == nullptr || timetable < 0 || timetable >= int(set->timetables.size())
            || !set->timetables[size_t(timetable)]->extraKeys.contains(key))
        return;
    {
        ActivityEdit edit(set);
        set->timetables[size_t(timetable)]->removeExtraKey(key);
    }
    emit edited();
}

void OrTrainProperties::addKeyAsked() {
    if (set == nullptr || timetable < 0)
        return;
    //% "Add Key"
    const QString title = qtTrId("route.editor.activity.or.train.extra.add.title");
    const QString key = askKey(this, title,
        //% "The new key, every train's in the timetable (letters, digits and '_'):"
        qtTrId("route.editor.activity.or.train.extra.add.label"), QString());
    if (key.isEmpty())
        return;
    const QString problem = addExtraKey(key);
    if (!problem.isEmpty()) {
        QMessageBox::warning(this, title, problem);
        return;
    }
    // Its value typed next.
    chooseKey(key.toLower(), true);
}

void OrTrainProperties::renameKeyAsked() {
    const QString key = chosenKey();
    if (key.isEmpty() || !keyButtons[1].isEnabled())
        return;
    //% "Rename Key"
    const QString title = qtTrId("route.editor.activity.or.train.extra.rename.title");
    const QString to = askKey(this, title,
        //% "The key %1's new name, every train's in the timetable:"
        qtTrId("route.editor.activity.or.train.extra.rename.label").arg(key), key);
    if (to.isEmpty())
        return;
    const QString problem = renameExtraKey(key, to);
    if (!problem.isEmpty())
        QMessageBox::warning(this, title, problem);
}

void OrTrainProperties::removeKeyAsked() {
    const QString key = chosenKey();
    if (key.isEmpty() || set == nullptr || timetable < 0 || !keyButtons[2].isEnabled())
        return;
    // Asked when trains have values for it.
    int values = 0;
    for (const OrTimetable::Train &other : set->timetables[size_t(timetable)]->trains)
        values += other.extra.contains(key) ? 1 : 0;
    if (values > 0 && QMessageBox::question(this,
            //% "Remove Key"
            qtTrId("route.editor.activity.or.train.extra.remove.title"),
            //% "Remove the key %1 and its values from every train of the timetable? Trains with a value: %2."
            qtTrId("route.editor.activity.or.train.extra.remove.question").arg(key).arg(values)) != QMessageBox::Yes)
        return;
    removeExtraKey(key);
}

void OrTrainProperties::chooseKey(const QString &key, bool edit) {
    for (int i = 0; !key.isEmpty() && i < extras.topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = extras.topLevelItem(i);
        if (item->text(KeyColumn) == key) {
            extras.setCurrentItem(item, ValueColumn);
            if (edit)
                extras.editItem(item, ValueColumn);
            return;
        }
    }
}

QString OrTrainProperties::chosenKey() const {
    const QTreeWidgetItem *item = extras.currentItem();
    return item != nullptr ? item->text(KeyColumn) : QString();
}

void OrTrainProperties::openPicker(int place, const QString &title, const QString &text,
                                   const std::function<void(const QString &)> &apply) {
    if (set == nullptr || timetable < 0 || timetable >= int(set->timetables.size()))
        return;
    const OrTimetable &t = *set->timetables[size_t(timetable)];
    // The other trains by name; a group's other timetables' as
    // "name:description", as Open Rails refers to them.
    QStringList trains;
    for (int k = 0; k < t.trains.size(); ++k)
        if (k != train && !t.trains[k].name.isEmpty())
            trains << t.trains[k].name;
    for (size_t o = 0; o < set->timetables.size(); ++o)
        if (int(o) != timetable && !set->timetables[o]->description.isEmpty())
            for (const OrTimetable::Train &other : set->timetables[o]->trains)
                trains << other.name + ":" + set->timetables[o]->description;
    QStringList paths;
    for (Path *path : ActLib::RoutePaths(ActivityTrains::RouteDirectory(set)))
        paths << path->nameId;
    const QString row = t.speedRow.toLower();
    const QString unit = row == "#speedmph" ? QStringLiteral("mph") : row == "#speed" ? QStringLiteral("m/s")
                                                                                       : QStringLiteral("km/h");
    OrCommandPicker *picker = new OrCommandPicker(place, title, text, trains, paths, unit, this);
    picker->setAttribute(Qt::WA_DeleteOnClose);
    // Applied to the train it was opened for, if still shown.
    const Activity *forSet = set;
    const int forTimetable = timetable;
    const int forTrain = train;
    QObject::connect(picker, &QDialog::accepted, this, [this, picker, apply, forSet, forTimetable, forTrain] {
        if (set == forSet && timetable == forTimetable && train == forTrain)
            apply(picker->text());
    });
    picker->open();
}

void OrTrainProperties::pickTrainCommands(int field) {
    if (field < 0 || field >= 4 || !commandFields[field].isEnabled() || set == nullptr || timetable < 0
            || train < 0 || train >= set->timetables[size_t(timetable)]->trains.size())
        return;
    const QString name = set->timetables[size_t(timetable)]->trains[train].name;
    const QString fields[4] = {
        //% "%1, start"
        qtTrId("route.editor.activity.or.train.pick.start").arg(name),
        //% "%1, note"
        qtTrId("route.editor.activity.or.train.pick.note").arg(name),
        //% "%1, dispose"
        qtTrId("route.editor.activity.or.train.pick.dispose").arg(name),
        //% "%1, speed row"
        qtTrId("route.editor.activity.or.train.pick.speed").arg(name)};
    openPicker(FieldPlaces[field], fields[field], commandFields[field].text(), [this, field](const QString &text) {
        commandFields[field].setText(text);
        commandsEdited(TrainCommandKinds[field]);
    });
}

void OrTrainProperties::pickStopCommands(int stop, int station, int column) {
    if (set == nullptr || timetable < 0 || train < 0 || !stops.isEnabled())
        return;
    const OrTimetable &t = *set->timetables[size_t(timetable)];
    if (train >= t.trains.size() || stop >= t.trains[train].stops.size() || station < 0 || station >= t.stations.size())
        return;
    const bool own = column == CommandsColumn;
    // A station the train has no stop at: only the station's commands.
    if (own && stop < 0)
        return;
    const QString name = t.stations[station].name;
    const QString title = own ?
        //% "%1 at %2"
        qtTrId("route.editor.activity.or.train.pick.stop").arg(t.trains[train].name, name) :
        //% "%1, every train"
        qtTrId("route.editor.activity.or.train.pick.station").arg(name);
    const QString text = OrTimetable::CommandsText(own ? t.trains[train].stops[stop].commands : t.stations[station].commands);
    openPicker(own ? OrCommandSpec::Stop : OrCommandSpec::Station, title, text, [this, station, column](const QString &chosen) {
        for (int i = 0; i < stops.topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = stops.topLevelItem(i);
            if (item->data(StationColumn, StationRole).toInt() == station) {
                // As typed: stopEdited takes it.
                item->setText(column, chosen);
                return;
            }
        }
    });
}
