#include <routeEditor/activity/OrTrainProperties.h>
#include <routeEditor/activity/ActivityEdit.h>
#include <tsre/gui/GuiFunct.h>
#include <tsre/trains/Activity.h>

namespace {

enum Column { StationColumn, ArrivalColumn, DepartureColumn, CommandsColumn, StationCommandsColumn, Columns };
// An item's data: its stop's place in the train, whether it has times to
// edit.
constexpr int StopRole = Qt::UserRole;
constexpr int TimedRole = Qt::UserRole + 1;

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

}

OrTrainProperties::OrTrainProperties(QWidget *parent) : QWidget(parent) {
    setObjectName("orTrainProperties");
    const char *shownNames[5] = {"orTrainName", "orTrainPath", "orTrainConsist", "orTrainComment", "orTrainFile"};
    for (int k = 0; k < 5; ++k) {
        shownFields[k].setObjectName(shownNames[k]);
        shownFields[k].setReadOnly(true);
        shownFields[k].setEnabled(false);
    }
    startTime.setDisplayFormat("HH:mm:ss");
    startTime.setObjectName("orTrainStart");
    const char *names[4] = {"orTrainStartCommands", "orTrainNote", "orTrainDispose", "orTrainSpeed"};
    const QString placeholders[4] = {"$create=04:30", "$acc=2 $follow=T2", "$triggers=T3", "$max=120"};
    for (int k = 0; k < 4; ++k) {
        commandFields[k].setObjectName(names[k]);
        commandFields[k].setPlaceholderText(placeholders[k]);
        QObject::connect(&commandFields[k], &QLineEdit::editingFinished, this,
                         [this, k] { commandsEdited(TrainCommandKinds[k]); });
    }
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
    stops.setToolTip(
        //% "Arrival and departure as HH:mm or HH:mm:ss; Commands this train's at the station, Station's commands every train's there (double-click to change)."
        qtTrId("route.editor.activity.or.train.stops.tooltip"));
    problems.setWordWrap(true);
    problems.setStyleSheet("QLabel { color : rgb(200, 120, 0); }");
    problems.setContentsMargins(3, 0, 3, 0);
    problems.setObjectName("orTrainProblems");

    // One grid, a label and a box a row: what the train is, then its
    // commands.
    info = new QWidget(this);
    QGridLayout *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);
    int row = 0;
    auto section = [grid, &row](const QString &text) {
        if (row > 0)
            grid->addItem(new QSpacerItem(0, 6), row++, 0);
        grid->addWidget(GuiFunct::newTQLabel(text), row++, 0, 1, 2);
    };
    auto field = [grid, &row](const QString &text, QWidget *box) {
        QLabel *label = new QLabel(text);
        label->setContentsMargins(3, 0, 0, 0);
        grid->addWidget(label, row, 0);
        grid->addWidget(box, row++, 1);
    };
    //% "Info:"
    section(qtTrId("route.editor.activity.or.train.section.info"));
    //% "Train:"
    field(qtTrId("route.editor.activity.or.train.field.name"), &shownFields[0]);
    //% "Path:"
    field(qtTrId("route.editor.activity.or.train.field.path"), &shownFields[1]);
    //% "Consist:"
    field(qtTrId("route.editor.activity.or.train.field.consist"), &shownFields[2]);
    //% "Comment:"
    field(qtTrId("route.editor.activity.or.train.field.comment"), &shownFields[3]);
    //% "Timetable:"
    field(qtTrId("route.editor.activity.or.train.field.file"), &shownFields[4]);
    //% "Start Time:"
    field(qtTrId("route.editor.activity.or.train.field.start.time"), &startTime);
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
    set = chosenSet;
    timetable = chosenTimetable;
    train = chosenTrain;
    filling = true;
    stops.clear();
    const bool valid = set != nullptr && timetable >= 0 && timetable < int(set->timetables.size()) && train >= 0
            && train < set->timetables[size_t(timetable)]->trains.size();
    startTime.setEnabled(valid);
    for (QLineEdit &field : commandFields) {
        field.setEnabled(valid);
        if (!valid)
            field.clear();
    }
    if (!valid) {
        for (QLineEdit &field : shownFields)
            field.clear();
        problems.clear();
        problems.hide();
        filling = false;
        return;
    }
    const OrTimetable &t = *set->timetables[size_t(timetable)];
    const OrTimetable::Train &one = t.trains[train];
    const QString shown[5] = {one.name, one.pathText.isEmpty() ? one.path : one.pathText, one.consist, one.comment,
                              QFileInfo(t.file).fileName()};
    for (int k = 0; k < 5; ++k) {
        shownFields[k].setText(shown[k]);
        shownFields[k].setCursorPosition(0);
        shownFields[k].setToolTip(shown[k]);
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
    // The stops in the timetable's station order.
    for (int s = 0; s < t.stations.size(); ++s) {
        int k = -1;
        for (int i = 0; i < one.stops.size() && k < 0; ++i)
            if (one.stops[i].station == s)
                k = i;
        if (k < 0)
            continue;
        const OrTimetable::Stop &stop = one.stops[k];
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
    }
    problems.setText(found.join("\n"));
    problems.setVisible(!found.isEmpty());
    filling = false;
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
    if (train < 0 || train >= t.trains.size() || k < 0 || k >= t.trains[train].stops.size())
        return;
    const OrTimetable::Stop &stop = t.trains[train].stops[k];
    if (column == CommandsColumn || column == StationCommandsColumn) {
        const bool own = column == CommandsColumn;
        const QString was = OrTimetable::CommandsText(own ? stop.commands : t.stations[stop.station].commands);
        if (OrTimetable::CommandsText(OrTimetable::Commands(OrTimetable::TypedCommands(item->text(column)))) == was)
            return;
        {
            ActivityEdit edit(set);
            if (own)
                t.setStopCommands(train, k, item->text(column));
            else
                t.setStationCommands(stop.station, item->text(column));
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
    // A passing time or a cell without times, not a time, or both gone: as
    // it was.
    if (!item->data(StationColumn, TimedRole).toBool() || (!text.isEmpty() && time < 0)
            || (arrival < 0 && departure < 0)) {
        filling = true;
        const bool timed = was.passing < 0 && (was.arrival >= 0 || was.departure >= 0);
        item->setText(column, column == ArrivalColumn ? (timed ? OrTimetable::TimeText(was.arrival) : was.text)
                                                      : (timed ? OrTimetable::TimeText(was.departure) : QString()));
        filling = false;
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
