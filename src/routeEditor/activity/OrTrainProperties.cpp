#include <routeEditor/activity/OrTrainProperties.h>
#include <routeEditor/activity/ActivityEdit.h>
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
    heading.setStyleSheet("QLabel { font-weight: bold; }");
    details.setWordWrap(true);
    details.setTextInteractionFlags(Qt::TextSelectableByMouse);
    details.setObjectName("orTrainDetails");
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
    problems.setObjectName("orTrainProblems");

    QHBoxLayout *startLine = new QHBoxLayout;
    startLine->setContentsMargins(0, 0, 0, 0);
    startLine->addWidget(new QLabel(
        //% "Start:"
        qtTrId("route.editor.activity.or.train.start")));
    startLine->addWidget(&startTime);
    startLine->addStretch(1);
    QFormLayout *commands = new QFormLayout;
    commands->setContentsMargins(0, 0, 0, 0);
    //% "Start commands:"
    commands->addRow(qtTrId("route.editor.activity.or.train.field.start"), &commandFields[0]);
    //% "Note:"
    commands->addRow(qtTrId("route.editor.activity.or.train.field.note"), &commandFields[1]);
    //% "Dispose:"
    commands->addRow(qtTrId("route.editor.activity.or.train.field.dispose"), &commandFields[2]);
    //% "Speed row:"
    commands->addRow(qtTrId("route.editor.activity.or.train.field.speed"), &commandFields[3]);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(&heading);
    layout->addLayout(startLine);
    layout->addLayout(commands);
    layout->addWidget(&details);
    layout->addWidget(&stops, 1);
    layout->addWidget(&problems);
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
        heading.clear();
        details.clear();
        problems.clear();
        filling = false;
        return;
    }
    const OrTimetable &t = *set->timetables[size_t(timetable)];
    const OrTimetable::Train &one = t.trains[train];
    heading.setText(one.name);
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
        field.setToolTip(notRunText(missing));
        field.setStyleSheet(missing.isEmpty() ? QString() : QString("QLineEdit { color: %1; }").arg(NotRunColour.name()));
    }
    // A train static by its column's head has no start cell.
    commandFields[0].setEnabled(!one.header.startsWith('$'));
    QStringList lines;
    //% "Path: %1"
    lines << qtTrId("route.editor.activity.or.train.path").arg(one.pathText.isEmpty() ? one.path : one.pathText);
    //% "Consist: %1"
    lines << qtTrId("route.editor.activity.or.train.consist").arg(one.consist);
    if (!one.comment.isEmpty())
        //% "Comment: %1"
        lines << qtTrId("route.editor.activity.or.train.comment").arg(one.comment);
    //% "In %1."
    lines << qtTrId("route.editor.activity.or.train.file").arg(QFileInfo(t.file).fileName());
    details.setText(lines.join("\n"));
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
