#include <routeEditor/activity/OrTrainProperties.h>
#include <routeEditor/activity/ActivityEdit.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/OrTimetable.h>

namespace {

enum Column { StationColumn, ArrivalColumn, DepartureColumn, CommandsColumn, Columns };

QString commandsText(const QVector<OrTimetable::Command> &commands) {
    QStringList out;
    for (const OrTimetable::Command &command : commands)
        out << "$" + command.text;
    return out.join(' ');
}

}

OrTrainProperties::OrTrainProperties(QWidget *parent) : QWidget(parent) {
    setObjectName("orTrainProperties");
    heading.setStyleSheet("QLabel { font-weight: bold; }");
    details.setWordWrap(true);
    details.setTextInteractionFlags(Qt::TextSelectableByMouse);
    details.setObjectName("orTrainDetails");
    startTime.setDisplayFormat("HH:mm:ss");
    startTime.setObjectName("orTrainStart");
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
        qtTrId("route.editor.activity.or.train.column.commands")});
    stops.setRootIsDecorated(false);
    stops.setUniformRowHeights(true);
    stops.setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                          | QAbstractItemView::SelectedClicked);
    stops.header()->resizeSection(StationColumn, 170);
    stops.header()->resizeSection(ArrivalColumn, 70);
    stops.header()->resizeSection(DepartureColumn, 70);
    stops.setToolTip(
        //% "Arrival and departure as HH:mm or HH:mm:ss (double-click to change); one time empty: both the same."
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
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(&heading);
    layout->addLayout(startLine);
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
    QStringList lines;
    //% "Path: %1"
    lines << qtTrId("route.editor.activity.or.train.path").arg(one.pathText.isEmpty() ? one.path : one.pathText);
    //% "Consist: %1"
    lines << qtTrId("route.editor.activity.or.train.consist").arg(one.consist);
    if (!one.startCommands.isEmpty())
        //% "Start: %1"
        lines << qtTrId("route.editor.activity.or.train.start.commands").arg(commandsText(one.startCommands));
    if (!one.notes.isEmpty())
        //% "Note: %1"
        lines << qtTrId("route.editor.activity.or.train.note").arg(commandsText(one.notes));
    if (!one.dispose.isEmpty())
        //% "Dispose: %1"
        lines << qtTrId("route.editor.activity.or.train.dispose").arg(commandsText(one.dispose));
    if (!one.speedText.isEmpty())
        //% "Speed: %1"
        lines << qtTrId("route.editor.activity.or.train.speed").arg(one.speedText);
    if (!one.comment.isEmpty())
        //% "Comment: %1"
        lines << qtTrId("route.editor.activity.or.train.comment").arg(one.comment);
    //% "In %1."
    lines << qtTrId("route.editor.activity.or.train.file").arg(QFileInfo(t.file).fileName());
    details.setText(lines.join("\n"));
    // The stops in the timetable's station order (a station's commands
    // under its own).
    for (int s = 0; s < t.stations.size(); ++s) {
        int k = -1;
        for (int i = 0; i < one.stops.size() && k < 0; ++i)
            if (one.stops[i].station == s)
                k = i;
        if (k < 0)
            continue;
        const OrTimetable::Stop &stop = one.stops[k];
        const bool timed = stop.timing.passing < 0 && (stop.timing.arrival >= 0 || stop.timing.departure >= 0);
        QStringList commands;
        if (!t.stations[s].commands.isEmpty())
            commands << commandsText(t.stations[s].commands);
        if (!stop.commands.isEmpty())
            commands << commandsText(stop.commands);
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
            timed ? OrTimetable::TimeText(stop.timing.departure) : QString(), commands.join(" ")}, k);
        if (timed && !one.isStatic)
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        for (int column = 0; column < Columns; ++column)
            item->setToolTip(column, item->text(column));
    }
    problems.setText(found.join("\n"));
    filling = false;
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

void OrTrainProperties::stopEdited(QTreeWidgetItem *item, int column) {
    if (filling || set == nullptr || timetable < 0 || item == nullptr
            || (column != ArrivalColumn && column != DepartureColumn))
        return;
    OrTimetable &t = *set->timetables[size_t(timetable)];
    const int k = item->type();
    if (train < 0 || train >= t.trains.size() || k < 0 || k >= t.trains[train].stops.size())
        return;
    const OrTimetable::Timing &was = t.trains[train].stops[k].timing;
    const QString text = item->text(column).trimmed();
    const int time = text.isEmpty() ? -1 : OrTimetable::ParseTime(text);
    int arrival = column == ArrivalColumn ? time : was.arrival;
    int departure = column == DepartureColumn ? time : was.departure;
    // A request stop or an early one has one time: either column sets it.
    if ((was.request || was.early) && time >= 0)
        arrival = departure = time;
    // Not a time, or both gone: as it was.
    if ((!text.isEmpty() && time < 0) || (arrival < 0 && departure < 0)) {
        filling = true;
        item->setText(column, OrTimetable::TimeText(column == ArrivalColumn ? was.arrival : was.departure));
        filling = false;
        return;
    }
    if (arrival == was.arrival && departure == was.departure)
        return;
    {
        ActivityEdit edit(set);
        t.setStopTimes(train, k, arrival, departure);
    }
    // After the item's own signal: the window fills the stops again.
    QMetaObject::invokeMethod(this, [this] { emit edited(); }, Qt::QueuedConnection);
}
