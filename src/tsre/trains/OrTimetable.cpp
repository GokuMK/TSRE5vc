#include <tsre/trains/OrTimetable.h>

#include <QCoreApplication>
#include <algorithm>
#include <functional>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringDecoder>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/trains/ActLib.h>

namespace {

// Open Rails' column and row kinds (ProcessTimetable).
enum class Column { Station, StationMore, Comment, Train, TrainMore, Invalid };
enum class Row { Trains, Station, StationMore, Comment, Path, Consist, Start, Note, Dispose, Speed, RestartDelay,
                 Briefing, Direction, Invalid };

// The first cell's lower-case word and what follows a '/' (#path /binary).
QString rowWord(const QString &cell) {
    QString word = cell.toLower();
    const int slash = word.indexOf('/');
    if (slash >= 0)
        word = word.left(slash).trimmed();
    return word;
}

// A cell's text up to its first '$' (its name or time), and the commands
// after it.
QString beforeCommands(const QString &text) {
    const int at = text.indexOf('$');
    return (at >= 0 ? text.left(at) : text).trimmed();
}

QString afterCommands(const QString &text) {
    const int at = text.indexOf('$');
    return at >= 0 ? text.mid(at) : QString();
}

}

QStringList OrTimetable::Command::qualifier(const QString &name) const {
    for (const Qualifier &q : qualifiers)
        if (q.name == name)
            return q.values;
    return {};
}

bool OrTimetable::Command::has(const QString &name) const {
    for (const Qualifier &q : qualifiers)
        if (q.name == name)
            return true;
    return false;
}

const OrTimetable::Stop *OrTimetable::Train::stopAt(int station) const {
    for (const Stop &stop : stops)
        if (stop.station == station)
            return &stop;
    return nullptr;
}

QString OrTimetable::ConsistFile(const QString &name) {
    return name.endsWith(".con", Qt::CaseInsensitive) ? name : name + ".con";
}

QString OrTimetable::Train::pathName() const {
    QString name = path;
    if (name.endsWith(".pat", Qt::CaseInsensitive))
        name.chop(4);
    else if (name.endsWith('.'))
        name.chop(1);
    return name;
}

int OrTimetable::trainNamed(const QString &name) const {
    const QString key = name.trimmed().toLower();
    for (int k = 0; k < trains.size(); ++k)
        if (trains[k].name.toLower() == key)
            return k;
    return -1;
}

QString OrTimetable::Decode(const QByteArray &bytes) {
    if (bytes.startsWith("\xFF\xFE"))
        return QStringDecoder(QStringConverter::Utf16LE).decode(bytes.mid(2));
    if (bytes.startsWith("\xFE\xFF"))
        return QStringDecoder(QStringConverter::Utf16BE).decode(bytes.mid(2));
    if (bytes.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(bytes.mid(3));
    return QString::fromUtf8(bytes);
}

OrTimetable::Command OrTimetable::ParseCommand(const QString &text) {
    // As Open Rails' TTTrainCommands: in lower case, qualifiers after '/',
    // the value after the first '=', values and qualifier values split at
    // '+'.
    Command command;
    command.text = text.trimmed();
    const QString work = command.text.toLower();
    QStringList parts = work.split('/');
    const QString head = parts.takeFirst();
    for (const QString &part : parts) {
        Qualifier q;
        const int equals = part.indexOf('=');
        q.name = (equals >= 0 ? part.left(equals) : part).trimmed();
        if (equals >= 0)
            for (const QString &value : part.mid(equals + 1).trimmed().split('+'))
                q.values << value.trimmed();
        command.qualifiers.push_back(q);
    }
    const int equals = head.indexOf('=');
    command.name = (equals >= 0 ? head.left(equals) : head).trimmed();
    if (command.name.startsWith('$'))
        command.name = command.name.mid(1).trimmed();
    if (equals >= 0) {
        const QString value = head.mid(equals + 1);
        if (!value.trimmed().isEmpty())
            for (const QString &one : value.split('+'))
                command.values << one.trimmed();
    }
    return command;
}

QVector<OrTimetable::Command> OrTimetable::Commands(const QString &text) {
    QVector<Command> out;
    for (const QString &part : text.split('$'))
        if (!part.trimmed().isEmpty())
            out.push_back(ParseCommand(part));
    return out;
}

int OrTimetable::ParseTime(const QString &text) {
    const QStringList parts = text.trimmed().split(':');
    if (parts.size() < 2 || parts.size() > 3)
        return -1;
    int fields[3] = {0, 0, 0};
    const int limits[3] = {24, 60, 60};
    for (int k = 0; k < parts.size(); ++k) {
        bool ok = false;
        fields[k] = parts[k].trimmed().toInt(&ok);
        if (!ok || parts[k].trimmed().isEmpty() || fields[k] < 0 || fields[k] >= limits[k])
            return -1;
    }
    return fields[0] * 3600 + fields[1] * 60 + fields[2];
}

OrTimetable::Timing OrTimetable::ParseTiming(const QString &text) {
    // As Open Rails' StopInfo: "arrival-departure"; one time both; '*'
    // early, 'x' a request stop, 'P' a passing time, each also alone or in
    // place of the ':'.
    Timing timing;
    timing.text = text.trimmed();
    QString arrival = timing.text, departure;
    bool departureSet = false;
    const int dash = timing.text.indexOf('-');
    if (dash >= 0) {
        arrival = timing.text.left(dash).trimmed();
        departure = timing.text.mid(dash + 1).trimmed();
        departureSet = true;
    }
    if (arrival.isEmpty() && !departureSet)
        return timing;
    if (arrival.size() == 1 && (arrival == "*" || arrival == "x")) {
        timing.early = true;
        timing.request = arrival == "x";
        return timing;
    }
    if (arrival.startsWith('P') || arrival.contains('P')) {
        const QString time = arrival.startsWith('P') ? arrival.mid(1) : QString(arrival).replace('P', ':');
        timing.passing = ParseTime(time);
        timing.valid = timing.passing >= 0;
        return timing;
    }
    if (arrival.contains('*') || arrival.contains('x')) {
        timing.early = arrival.contains('*');
        timing.request = arrival.contains('x');
        timing.arrival = timing.departure = ParseTime(QString(arrival).replace('*', ':').replace('x', ':'));
        timing.valid = timing.arrival >= 0;
        return timing;
    }
    timing.arrival = arrival.isEmpty() ? -1 : ParseTime(arrival);
    if (departureSet)
        timing.departure = ParseTime(departure);
    if (timing.arrival >= 0 && timing.departure < 0)
        timing.departure = timing.arrival;
    else if (timing.arrival < 0 && timing.departure >= 0)
        timing.arrival = timing.departure;
    timing.valid = timing.arrival >= 0 || timing.departure >= 0;
    return timing;
}

QVector<OrTimetable::ConsistPart> OrTimetable::ParseConsist(const QString &text) {
    // As Open Rails' ProcessConsistInfo: names between '+', "$reverse"
    // after a name turning it, a name in "<…>" taken as written.
    QVector<ConsistPart> out;
    QString rest = text.trimmed();
    while (!rest.isEmpty()) {
        if (rest.startsWith('<')) {
            const int end = rest.indexOf('>');
            out.push_back(ConsistPart{end < 0 ? rest.mid(1) : rest.mid(1, end - 1), false});
            rest = end < 0 ? QString() : rest.mid(end + 1).trimmed();
        } else if (rest.startsWith('$')) {
            if (rest.mid(1, 7).toLower() == "reverse") {
                if (!out.isEmpty())
                    out.last().reverse = true;
                rest = rest.mid(8).trimmed();
            } else {
                rest.clear();
            }
        } else if (rest.startsWith('+')) {
            rest = rest.mid(1).trimmed();
        } else {
            const int plus = rest.indexOf('+');
            QString name = plus >= 0 ? rest.left(plus) : rest;
            rest = plus >= 0 ? rest.mid(plus) : QString();
            const int dollar = name.indexOf('$');
            if (dollar > 0) {
                rest = name.mid(dollar).trimmed() + rest;
                name = name.left(dollar);
            }
            out.push_back(ConsistPart{name.trimmed(), false});
            rest = rest.trimmed();
        }
    }
    return out;
}

bool OrTimetable::load(const QString &path) {
    file = path;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        description.clear();
        stations.clear();
        trains.clear();
        problems.clear();
        //% "cannot read %1"
        problems << qtTrId("timetable.read.unreadable").arg(path);
        return false;
    }
    return loadText(Decode(in.readAll()));
}

bool OrTimetable::loadText(const QString &source) {
    description.clear();
    stations.clear();
    trains.clear();
    problems.clear();
    speedRow.clear();
    modified = false;
    QString text = source;
    text.replace("\r\n", "\n").replace('\r', '\n');
    QStringList lines = text.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    if (lines.isEmpty() || lines.first().isEmpty()) {
        //% "the file is empty"
        problems << qtTrId("timetable.read.empty");
        return false;
    }
    separator = lines.first().at(0);
    if (separator != ';' && separator != ',' && separator != '\t') {
        //% "the first character is '%1', not a separator (';', ',' or a tab)"
        problems << qtTrId("timetable.read.separator").arg(separator);
        return false;
    }
    QVector<QStringList> cells;
    for (const QString &line : lines) {
        QStringList row = line.split(separator);
        for (QString &cell : row)
            cell = cell.trimmed();
        cells.push_back(row);
    }
    const auto cell = [&](int row, int column) {
        return column < cells[row].size() ? cells[row][column] : QString();
    };

    // The columns: trains, their continuations, comments.
    const int columns = cells[0].size();
    QVector<Column> columnKind(columns, Column::Invalid);
    QVector<int> owner(columns, -1);
    columnKind[0] = Column::Station;
    int firstCommentColumn = -1;
    for (int c = 1; c < columns; ++c) {
        const QString head = cells[0][c];
        if (head.isEmpty()) {
            switch (columnKind[c - 1]) {
            case Column::Station:
            case Column::StationMore:
                columnKind[c] = Column::StationMore;
                break;
            case Column::Comment:
                columnKind[c] = Column::Comment;
                break;
            case Column::Train:
                columnKind[c] = Column::TrainMore;
                owner[c] = c - 1;
                break;
            case Column::TrainMore:
                columnKind[c] = Column::TrainMore;
                owner[c] = owner[c - 1];
                break;
            case Column::Invalid:
                break;
            }
        } else if (head.compare("#comment", Qt::CaseInsensitive) == 0) {
            columnKind[c] = Column::Comment;
            if (firstCommentColumn < 0)
                firstCommentColumn = c;
        } else if (head.startsWith('#')) {
            //% "column %1: %2 is not a column Open Rails knows (skipped)"
            problems << qtTrId("timetable.read.column").arg(c + 1).arg(head);
        } else {
            columnKind[c] = Column::Train;
            owner[c] = c;
        }
    }

    // The rows.
    QVector<Row> rowKind(cells.size(), Row::Invalid);
    rowKind[0] = Row::Trains;
    int pathRow = -1, consistRow = -1, startRow = -1, disposeRow = -1, speedRow = -1, delayRow = -1;
    int briefingRow = -1, firstCommentRow = -1;
    QVector<int> noteRows;
    float speedFactor = 1.0f;
    QVector<int> stationOfRow(cells.size(), -1);
    for (int r = 1; r < cells.size(); ++r) {
        const QString word = rowWord(cell(r, 0));
        if (word.isEmpty()) {
            const bool station = rowKind[r - 1] == Row::Station || rowKind[r - 1] == Row::StationMore;
            rowKind[r] = station ? Row::StationMore : Row::Comment;
            if (station)
                stationOfRow[r] = stationOfRow[r - 1];
        } else if (word == "#consist") {
            rowKind[r] = Row::Consist;
            consistRow = r;
        } else if (word == "#path") {
            rowKind[r] = Row::Path;
            pathRow = r;
        } else if (word == "#start") {
            rowKind[r] = Row::Start;
            startRow = r;
        } else if (word == "#dispose") {
            rowKind[r] = Row::Dispose;
            disposeRow = r;
        } else if (word == "#direction") {
            rowKind[r] = Row::Direction;
        } else if (word == "#note") {
            rowKind[r] = Row::Note;
            noteRows << r;
        } else if (word == "#restartdelay") {
            rowKind[r] = Row::RestartDelay;
            delayRow = r;
        } else if (word == "#speed" || word == "#speedmph" || word == "#speedkph") {
            if (speedRow >= 0) {
                //% "row %1: a second speed row (skipped)"
                problems << qtTrId("timetable.read.speed.again").arg(r + 1);
                rowKind[r] = Row::Comment;
            } else {
                rowKind[r] = Row::Speed;
                speedRow = r;
                this->speedRow = word;
                speedFactor = word == "#speedmph" ? 0.44704f : word == "#speedkph" ? 1.0f / 3.6f : 1.0f;
            }
        } else if (word == "#comment") {
            rowKind[r] = Row::Comment;
            if (firstCommentRow < 0)
                firstCommentRow = r;
        } else if (word == "#briefing") {
            rowKind[r] = Row::Briefing;
            briefingRow = r;
        } else if (word.startsWith('#')) {
            //% "row %1: %2 is not a row Open Rails knows (skipped)"
            problems << qtTrId("timetable.read.row").arg(r + 1).arg(cell(r, 0));
        } else {
            rowKind[r] = Row::Station;
            // The station's name and commands: the first cell up to a '/',
            // and the station columns' cells after it.
            QString definition = cell(r, 0);
            const int slash = definition.indexOf('/');
            if (slash >= 0) {
                //% "row %1: '%2' after the '/' in the station's cell is not read (nor by Open Rails); qualifiers belong in the station's next column"
                problems << qtTrId("timetable.read.station.slash").arg(r + 1).arg(definition.mid(slash).trimmed());
                definition = definition.left(slash);
            }
            for (int c = 1; c < columns && columnKind[c] == Column::StationMore; ++c)
                if (!cell(r, c).isEmpty())
                    definition += " " + cell(r, c);
            Station station;
            station.name = beforeCommands(definition);
            station.key = station.name.toLower();
            station.commands = Commands(afterCommands(definition));
            station.row = r;
            for (const Station &other : stations)
                if (other.key == station.key)
                    //% "row %1: station %2 again (Open Rails matches stations by name)"
                    problems << qtTrId("timetable.read.station.again").arg(r + 1).arg(station.name);
            stationOfRow[r] = stations.size();
            stations.push_back(station);
        }
    }
    if (firstCommentRow >= 0 && firstCommentColumn >= 0)
        description = cell(firstCommentRow, firstCommentColumn);
    if (pathRow < 0)
        //% "no #path row"
        problems << qtTrId("timetable.read.no.path");
    if (consistRow < 0)
        //% "no #consist row"
        problems << qtTrId("timetable.read.no.consist");
    if (startRow < 0)
        //% "no #start row"
        problems << qtTrId("timetable.read.no.start");

    // A train's cell: its column's and its continuation columns'.
    const auto trainCell = [&](int row, int column) {
        QString text = cell(row, column);
        for (int c = column + 1; c < columns && columnKind[c] == Column::TrainMore && owner[c] == column; ++c)
            if (!cell(row, c).isEmpty())
                text = text.isEmpty() ? cell(row, c) : text + " " + cell(row, c);
        return text;
    };
    QSet<QString> names;
    for (int c = 1; c < columns; ++c) {
        if (columnKind[c] != Column::Train)
            continue;
        Train train;
        train.column = c;
        const QString head = cells[0][c];
        train.header = head;
        if (firstCommentRow >= 0)
            train.comment = trainCell(firstCommentRow, c);
        if (head.toLower().contains("$static")) {
            train.isStatic = true;
            train.name = head.startsWith('$') ? QString("S%1").arg(c) : beforeCommands(head);
        } else {
            train.name = head;
        }
        if (names.contains(train.name.toLower()))
            //% "column %1: train %2 again (names must be unique)"
            problems << qtTrId("timetable.read.train.again").arg(c + 1).arg(train.name);
        names.insert(train.name.toLower());
        if (pathRow >= 0) {
            train.pathText = trainCell(pathRow, c);
            train.path = beforeCommands(train.pathText);
        }
        if (consistRow >= 0) {
            train.consist = trainCell(consistRow, c);
            train.consists = ParseConsist(train.consist);
        }
        if (startRow >= 0) {
            train.start = train.isStatic ? QString("$static") : trainCell(startRow, c);
            const QString time = beforeCommands(train.start);
            train.startTime = time.isEmpty() ? -1 : ParseTime(time);
            if (!time.isEmpty() && train.startTime < 0)
                //% "train %1: start %2 is not a time"
                problems << qtTrId("timetable.read.start").arg(train.name, time);
            train.startCommands = Commands(afterCommands(train.start));
            for (const Command &command : train.startCommands)
                train.isStatic = train.isStatic || command.name == "static";
        }
        for (int r : noteRows)
            train.notes += Commands(trainCell(r, c));
        if (disposeRow >= 0)
            train.dispose = Commands(trainCell(disposeRow, c));
        if (speedRow >= 0) {
            train.speedText = trainCell(speedRow, c);
            train.speedCommands = Commands(train.speedText);
            for (const Command &command : train.speedCommands) {
                bool ok = false;
                const float value = command.values.isEmpty() ? 0.0f : command.values.first().toFloat(&ok);
                if (command.name == "max" && ok)
                    train.speed = value * speedFactor;
            }
        }
        if (delayRow >= 0)
            train.restartDelay = trainCell(delayRow, c);
        if (briefingRow >= 0)
            train.briefing = trainCell(briefingRow, c).replace("<br>", "\n");
        for (int r = 1; r < cells.size(); ++r) {
            if (rowKind[r] != Row::Station && rowKind[r] != Row::StationMore)
                continue;
            const QString text = trainCell(r, c);
            if (text.isEmpty())
                continue;
            const int station = stationOfRow[r];
            if (rowKind[r] == Row::StationMore) {
                // A station's continuation row: more for the stop there.
                for (Stop &stop : train.stops)
                    if (stop.station == station) {
                        stop.text += " " + text;
                        stop.commands += Commands(afterCommands(text));
                    }
                continue;
            }
            Stop stop;
            stop.station = station;
            stop.text = text;
            stop.timing = ParseTiming(beforeCommands(text));
            stop.commands = Commands(afterCommands(text));
            if (!stop.timing.valid)
                //% "train %1 at %2: %3 is not a timing"
                problems << qtTrId("timetable.read.timing").arg(train.name, stations[station].name, stop.timing.text);
            train.stops.push_back(stop);
        }
        trains.push_back(train);
    }
    return true;
}

namespace {

QString commandsText(const QVector<OrTimetable::Command> &commands) {
    QStringList out;
    for (const OrTimetable::Command &command : commands)
        out << "$" + command.text;
    return out.join(' ');
}

// A cell's text and its commands' after it.
QString joined(const QString &text, const QString &commands) {
    return text.isEmpty() ? commands : commands.isEmpty() ? text : text + " " + commands;
}

}

QString OrTimetable::TimeText(int seconds) {
    if (seconds < 0)
        return QString();
    const int s = seconds % 86400;
    return s % 60 != 0 ? QString("%1:%2:%3").arg(s / 3600, 2, 10, QChar('0')).arg(s / 60 % 60, 2, 10, QChar('0'))
                             .arg(s % 60, 2, 10, QChar('0'))
                       : QString("%1:%2").arg(s / 3600, 2, 10, QChar('0')).arg(s / 60 % 60, 2, 10, QChar('0'));
}

void OrTimetable::setStartTime(int train, int time) {
    if (train < 0 || train >= trains.size() || trains[train].isStatic)
        return;
    Train &t = trains[train];
    t.startTime = time;
    t.start = joined(TimeText(time), commandsText(t.startCommands));
    modified = true;
}

void OrTimetable::setStopTimes(int train, int stop, int arrival, int departure) {
    if (train < 0 || train >= trains.size() || stop < 0 || stop >= trains[train].stops.size())
        return;
    Stop &s = trains[train].stops[stop];
    if (arrival < 0)
        arrival = departure;
    if (departure < 0)
        departure = arrival;
    // A request stop ('x') or one it may leave early ('*') has one time,
    // written in place of the time's ':' (Open Rails reads no departure
    // after "13x21" or "17*23"): kept so, at the arrival.
    const bool request = s.timing.request, early = s.timing.early;
    if (request || early)
        departure = arrival;
    s.timing = Timing();
    s.timing.request = request;
    s.timing.early = early && !request;
    s.timing.arrival = arrival;
    s.timing.departure = departure;
    s.timing.text = arrival < 0 ? QString() : arrival == departure ? TimeText(arrival)
                                                                   : TimeText(arrival) + "-" + TimeText(departure);
    if ((request || early) && arrival >= 0)
        s.timing.text.replace(s.timing.text.indexOf(':'), 1, request ? QChar('x') : QChar('*'));
    s.text = joined(s.timing.text, commandsText(s.commands));
    modified = true;
}

QString OrTimetable::CommandsText(const QVector<Command> &commands) {
    return commandsText(commands);
}

const QVector<OrTimetable::Command> &OrTimetable::TrainCommandsOf(const Train &train, TrainCommands which) {
    switch (which) {
    case TrainCommands::Start:
        return train.startCommands;
    case TrainCommands::Note:
        return train.notes;
    case TrainCommands::Dispose:
        return train.dispose;
    case TrainCommands::Speed:
        break;
    }
    return train.speedCommands;
}

// Commands as typed, made a cell's text: one line, no separator, a '$'
// before the first.
QString OrTimetable::TypedCommands(const QString &typed) {
    QString text = typed;
    for (QChar &c : text)
        if (c == ';' || c == '\t' || c == '\n' || c == '\r')
            c = ' ';
    text = text.simplified();
    if (!text.isEmpty() && !text.startsWith('$'))
        text.prepend('$');
    return text;
}

void OrTimetable::setTrainCommands(int train, TrainCommands which, const QString &typed) {
    if (train < 0 || train >= trains.size())
        return;
    Train &t = trains[train];
    const QString text = TypedCommands(typed);
    switch (which) {
    case TrainCommands::Start: {
        // A train static by its column's head has no start cell of its own.
        if (t.header.startsWith('$'))
            return;
        t.startCommands = Commands(text);
        t.start = joined(beforeCommands(t.start), commandsText(t.startCommands));
        t.isStatic = t.header.toLower().contains("$static");
        for (const Command &command : t.startCommands)
            t.isStatic = t.isStatic || command.name == "static";
        break;
    }
    case TrainCommands::Note:
        t.notes = Commands(text);
        break;
    case TrainCommands::Dispose:
        t.dispose = Commands(text);
        break;
    case TrainCommands::Speed: {
        if (speedRow.isEmpty() && !text.isEmpty())
            speedRow = "#speedkph";
        t.speedText = text;
        t.speedCommands = Commands(text);
        const float factor = speedRow == "#speedmph" ? 0.44704f : speedRow == "#speedkph" ? 1.0f / 3.6f : 1.0f;
        t.speed = -1.0f;
        for (const Command &command : t.speedCommands) {
            bool ok = false;
            const float value = command.values.isEmpty() ? 0.0f : command.values.first().toFloat(&ok);
            if (command.name == "max" && ok)
                t.speed = value * factor;
        }
        break;
    }
    }
    modified = true;
}

void OrTimetable::setStopCommands(int train, int stop, const QString &typed) {
    if (train < 0 || train >= trains.size() || stop < 0 || stop >= trains[train].stops.size())
        return;
    Stop &s = trains[train].stops[stop];
    s.commands = Commands(TypedCommands(typed));
    s.text = joined(s.timing.text, commandsText(s.commands));
    modified = true;
}

void OrTimetable::setStationCommands(int station, const QString &typed) {
    if (station < 0 || station >= stations.size())
        return;
    stations[station].commands = Commands(TypedCommands(typed));
    modified = true;
}

void OrTimetable::setTrainPath(int train, const QString &path) {
    if (train < 0 || train >= trains.size())
        return;
    Train &t = trains[train];
    const QString cell = t.pathText.isEmpty() ? t.path : t.pathText;
    const int dollar = cell.indexOf('$');
    const QString commands = dollar >= 0 ? cell.mid(dollar).trimmed() : QString();
    t.path = path.trimmed();
    t.pathText = joined(t.path, commands);
    modified = true;
}

void OrTimetable::setTrainConsist(int train, const QString &consist) {
    if (train < 0 || train >= trains.size())
        return;
    Train &t = trains[train];
    t.consist = consist.trimmed();
    t.consists = ParseConsist(t.consist);
    modified = true;
}

int OrTimetable::addTrain(int after, const QString &name, int startTime) {
    Train t;
    t.name = name.trimmed();
    t.startTime = startTime;
    t.start = TimeText(startTime);
    for (const Train &other : trains)
        t.column = std::max(t.column, other.column + 1);
    const int at = after >= 0 && after < trains.size() ? after + 1 : trains.size();
    trains.insert(at, t);
    modified = true;
    return at;
}

int OrTimetable::cloneTrain(int train, const QString &name) {
    if (train < 0 || train >= trains.size())
        return -1;
    Train t = trains[train];
    t.name = name.trimmed();
    // A train static by its column's head stays so, now named.
    t.header = t.header.contains("$static", Qt::CaseInsensitive) ? t.name + " $static" : QString();
    for (const Train &other : trains)
        t.column = std::max(t.column, other.column + 1);
    trains.insert(train + 1, t);
    modified = true;
    return train + 1;
}

void OrTimetable::removeTrain(int train) {
    if (train < 0 || train >= trains.size())
        return;
    trains.remove(train);
    modified = true;
}

int OrTimetable::addStop(int train, int station, int arrival, int departure) {
    if (train < 0 || train >= trains.size() || station < 0 || station >= stations.size()
            || trains[train].stopAt(station) != nullptr)
        return -1;
    QVector<Stop> &list = trains[train].stops;
    int at = 0;
    while (at < list.size() && list[at].station < station)
        ++at;
    Stop stop;
    stop.station = station;
    list.insert(at, stop);
    setStopTimes(train, at, arrival, departure);
    return at;
}

void OrTimetable::removeStop(int train, int stop) {
    if (train < 0 || train >= trains.size() || stop < 0 || stop >= trains[train].stops.size())
        return;
    trains[train].stops.remove(stop);
    modified = true;
}

int OrTimetable::addStation(const QString &name, int before) {
    Station station;
    station.name = name.trimmed();
    station.key = station.name.toLower();
    const int at = before >= 0 && before < stations.size() ? before : stations.size();
    stations.insert(at, station);
    for (Train &t : trains)
        for (Stop &stop : t.stops)
            if (stop.station >= at)
                ++stop.station;
    modified = true;
    return at;
}

QStringList OrTimetable::trainsNaming(const QString &name) const {
    const QString key = name.trimmed().toLower();
    const auto names = [&key](const QVector<Command> &commands) {
        for (const Command &command : commands) {
            for (const QString &value : command.values)
                if (value == key || value.startsWith(key + ":"))
                    return true;
            for (const Qualifier &qualifier : command.qualifiers)
                for (const QString &value : qualifier.values)
                    if (value == key || value.startsWith(key + ":"))
                        return true;
        }
        return false;
    };
    QStringList out;
    for (const Train &t : trains) {
        if (t.name.toLower() == key)
            continue;
        bool named = names(t.startCommands) || names(t.notes) || names(t.dispose) || names(t.speedCommands);
        for (const Stop &stop : t.stops)
            named = named || names(stop.commands);
        if (named)
            out << t.name;
    }
    return out;
}

QString OrTimetable::toText() const {
    QVector<QStringList> rows;
    // The stations' commands in a station continuation column after their
    // names: Open Rails reads a station's first cell only up to a '/', its
    // next columns whole (commands with qualifiers).
    const bool stationColumn = std::any_of(stations.begin(), stations.end(),
                                           [](const Station &station) { return !station.commands.isEmpty(); });
    const auto row = [&](const QString &head, const QString &stationCommands, const QString &second,
                         const std::function<QString(const Train &)> &cell) {
        QStringList r{head};
        if (stationColumn)
            r << stationCommands;
        r << second;
        for (const Train &train : trains)
            r << cell(train);
        rows.push_back(r);
    };
    const auto any = [&](const std::function<bool(const Train &)> &has) {
        return std::any_of(trains.begin(), trains.end(), has);
    };
    row(QString(), QString(), "#comment", [](const Train &t) {
        return t.header.isEmpty() ? (t.isStatic ? QString("$static") : t.name) : t.header;
    });
    row("#comment", QString(), description, [](const Train &t) { return t.comment; });
    row("#path", QString(), QString(), [](const Train &t) { return t.pathText.isEmpty() ? t.path : t.pathText; });
    row("#consist", QString(), QString(), [](const Train &t) { return t.consist; });
    // (A train static by its column's head has "$static" as its start.)
    row("#start", QString(), QString(), [](const Train &t) {
        return t.header.startsWith('$') && t.start == "$static" ? QString() : t.start;
    });
    if (any([](const Train &t) { return !t.notes.isEmpty(); }))
        row("#note", QString(), QString(), [](const Train &t) { return commandsText(t.notes); });
    if (any([](const Train &t) { return !t.dispose.isEmpty(); }))
        row("#dispose", QString(), QString(), [](const Train &t) { return commandsText(t.dispose); });
    if (!speedRow.isEmpty() && any([](const Train &t) { return !t.speedText.isEmpty(); }))
        row(speedRow, QString(), QString(), [](const Train &t) { return t.speedText; });
    if (any([](const Train &t) { return !t.restartDelay.isEmpty(); }))
        row("#restartdelay", QString(), QString(), [](const Train &t) { return t.restartDelay; });
    if (any([](const Train &t) { return !t.briefing.isEmpty(); }))
        row("#briefing", QString(), QString(), [](const Train &t) { return QString(t.briefing).replace("\n", "<br>"); });
    for (int s = 0; s < stations.size(); ++s)
        row(stations[s].name, commandsText(stations[s].commands), QString(), [s](const Train &t) {
            const Stop *stop = t.stopAt(s);
            return stop != nullptr ? stop->text : QString();
        });
    QStringList lines;
    for (const QStringList &r : rows)
        lines << r.join(';');
    return lines.join("\r\n") + "\r\n";
}

bool OrTimetable::save(QString *error) {
    // The first time over a file TSRE did not write: the original kept.
    const QString original = file + ".original";
    if (QFileInfo::exists(file) && !QFileInfo::exists(original) && !QFile::copy(file, original)) {
        if (error != nullptr)
            //% "The original %1 could not be kept as %2."
            *error = qtTrId("timetable.save.original").arg(file, original);
        return false;
    }
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr)
            //% "%1 could not be written."
            *error = qtTrId("timetable.save.unwritable").arg(file);
        return false;
    }
    out.write("\xEF\xBB\xBF");
    out.write(toText().toUtf8());
    modified = false;
    return true;
}

bool OrTimetableGroup::load(const QString &path) {
    file = path;
    name = QFileInfo(path).completeBaseName();
    timetables.clear();
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    QString text = OrTimetable::Decode(in.readAll());
    text.replace("\r\n", "\n").replace('\r', '\n');
    bool first = true;
    for (const QString &raw : text.split('\n')) {
        const QString line = raw.trimmed();
        if (first && line.startsWith('#'))
            name = line.mid(1).trimmed();
        else if (!line.isEmpty() && !line.startsWith('#'))
            timetables << line;
        first = false;
    }
    return true;
}

bool OrTimetableCommandRun(const QString &name) {
    // Phase 8 adds the station's `$terminal`, `$stoptime`,
    // `$restrictplatformtosignal`, `$keepclear`, request stops' `$req`,
    // `$wait`, `$cruise` and `$maxdelay`, `$activate` and `$activated`,
    // `$forms`; and three Open Rails' AI does nothing with: `$creep` (its
    // creep speed is fixed), `$doo` (only the player's departure sound),
    // `$endstop` (its AI and autopilot trains run on past it).
    static const QSet<QString> run = {"acc", "dec", "follow", "nowaitsignal", "create", "hold", "nohold", "forcehold",
                                      "static", "triggers", "closeupsignal", "extendplatformtosignal", "forcewait",
                                      "max", "waitsignal", "next", "terminal", "stoptime", "endstop",
                                      "restrictplatformtosignal", "keepclear", "req", "wait", "cruise", "maxdelay",
                                      "activate", "activated", "forms", "creep", "doo"};
    return run.contains(name);
}

bool OrTimetableQualifierRun(const QString &command, const QString &qualifier) {
    // Phase 8 (the catalogue in task 08's doc). Not yet: `$static /ahead
    // /pool /closeup`, `$triggers` and `$forms`' /runround…, `$follow` and
    // `$wait` /trigger /endtrigger (experimental in Open Rails), `$wait
    // /atstart` (waiting in the section it stands in), `$req`'s
    // notices for the player (/laststop, /approach, /message, /sound).
    static const QSet<QString> run = {"create/ahead", "follow/maxdelay", "follow/notstarted", "follow/owndelay",
                                      "wait/maxdelay", "wait/notstarted", "wait/owndelay",
                                      "keepclear/front", "keepclear/rear", "keepclear/force", "req/pu", "req/pd",
                                      "activate/depart"};
    return run.contains(command + "/" + qualifier);
}

QVector<OrTimetableProblem> CheckTimetable(const OrTimetable &timetable, const QString &routeDirectory,
                                           const QString &consistDirectory, TDB *tdb) {
    QVector<OrTimetableProblem> out;
    for (const QString &problem : timetable.problems)
        out.push_back(OrTimetableProblem{QString(), problem});
    // The route's stations: its platforms' station names.
    QSet<QString> stationNames;
    if (tdb != nullptr)
        for (const auto &[id, item] : tdb->trackItems)
            if (item != nullptr && item->type == "platformitem")
                stationNames.insert(item->stationName.trimmed().toLower());
    if (tdb != nullptr)
        for (const OrTimetable::Station &station : timetable.stations)
            if (!stationNames.contains(station.key))
                out.push_back(OrTimetableProblem{QString(),
                                                 //% "station %1: no platform of the route has it"
                                                 qtTrId("timetable.check.station").arg(station.name)});
    // The commands naming another train.
    static const QSet<QString> referring = {"follow", "triggers", "forms", "wait", "connect", "activate", "attach",
                                            "pickup", "transfer"};
    const auto checkCommands = [&](const OrTimetable::Train &train, const QVector<OrTimetable::Command> &commands,
                                   const QString &where) {
        for (const OrTimetable::Command &command : commands) {
            // (A name with a space: what follows a command in the cell, a
            // timing after it, which Open Rails does not read either.)
            if (command.name.contains(' '))
                out.push_back(OrTimetableProblem{train.name,
                                                 //% "%1: '$%2' is no command; a timing must come before the commands"
                                                 qtTrId("timetable.check.command.text").arg(where, command.name)});
            else if (!OrTimetableCommandRun(command.name))
                out.push_back(OrTimetableProblem{train.name,
                                                 //% "%1: $%2 is not run by TSRE yet"
                                                 qtTrId("timetable.check.command").arg(where, command.name),
                                                 OrTimetableProblem::Unsupported});
            else
                for (const OrTimetable::Qualifier &qualifier : command.qualifiers)
                    if (!OrTimetableQualifierRun(command.name, qualifier.name))
                        out.push_back(OrTimetableProblem{train.name,
                                                         //% "%1: $%2 runs, its /%3 is not run by TSRE yet"
                                                         qtTrId("timetable.check.qualifier")
                                                             .arg(where, command.name, qualifier.name),
                                                         OrTimetableProblem::Unsupported});
            // (A name with ':' is another timetable's train.)
            if (referring.contains(command.name) && !command.values.isEmpty() && !command.values.first().contains(':')
                    && timetable.trainNamed(command.values.first()) < 0)
                out.push_back(OrTimetableProblem{train.name,
                                                 //% "%1: $%2 names %3, not a train of the timetable"
                                                 qtTrId("timetable.check.reference")
                                                     .arg(where, command.name, command.values.first())});
            const QStringList ahead = command.qualifier("ahead");
            if (!ahead.isEmpty() && timetable.trainNamed(ahead.first()) < 0)
                out.push_back(OrTimetableProblem{train.name,
                                                 //% "%1: /ahead=%2 is not a train of the timetable"
                                                 qtTrId("timetable.check.ahead").arg(where, ahead.first())});
        }
    };
    for (const OrTimetable::Train &train : timetable.trains) {
        if (train.path.isEmpty())
            //% "no path"
            out.push_back(OrTimetableProblem{train.name, qtTrId("timetable.check.no.path")});
        else if (ActLib::GetPathByName(train.pathName(), routeDirectory) == nullptr)
            //% "path %1 is not in the route's PATHS"
            out.push_back(OrTimetableProblem{train.name, qtTrId("timetable.check.path").arg(train.path),
                                             OrTimetableProblem::Path});
        if (train.consists.isEmpty())
            //% "no consist"
            out.push_back(OrTimetableProblem{train.name, qtTrId("timetable.check.no.consist")});
        for (const OrTimetable::ConsistPart &part : train.consists)
            if (!QFileInfo::exists(consistDirectory + "/" + OrTimetable::ConsistFile(part.name)))
                //% "consist %1 is not there"
                out.push_back(OrTimetableProblem{train.name, qtTrId("timetable.check.consist").arg(part.name)});
        if (train.startTime < 0 && !train.isStatic)
            //% "no start time"
            out.push_back(OrTimetableProblem{train.name, qtTrId("timetable.check.no.start")});
        //% "path"
        checkCommands(train, OrTimetable::Commands(afterCommands(train.pathText)), qtTrId("timetable.check.where.path"));
        //% "start"
        checkCommands(train, train.startCommands, qtTrId("timetable.check.where.start"));
        //% "note"
        checkCommands(train, train.notes, qtTrId("timetable.check.where.note"));
        //% "dispose"
        checkCommands(train, train.dispose, qtTrId("timetable.check.where.dispose"));
        //% "speed"
        checkCommands(train, train.speedCommands, qtTrId("timetable.check.where.speed"));
        for (const OrTimetable::Stop &stop : train.stops)
            checkCommands(train, stop.commands, timetable.stations[stop.station].name);
        // The cell notation and the row TSRE reads but does not run: one
        // line a kind, its stations (request stops and leaving early run
        // since phase 8).
        QStringList passing;
        for (const OrTimetable::Stop &stop : train.stops)
            if (stop.timing.passing >= 0)
                passing << timetable.stations[stop.station].name;
        if (!passing.isEmpty())
            out.push_back(OrTimetableProblem{train.name,
                                             //% "passing times (P) at %1: no stops, as in Open Rails; TSRE does not use the times yet"
                                             qtTrId("timetable.check.passing").arg(passing.join(", ")),
                                             OrTimetableProblem::Unsupported});
        if (!train.restartDelay.trimmed().isEmpty())
            out.push_back(OrTimetableProblem{train.name,
                                             //% "#restartdelay is not run by TSRE yet: no random restart delays"
                                             qtTrId("timetable.check.restartdelay"), OrTimetableProblem::Unsupported});
    }
    for (const OrTimetable::Station &station : timetable.stations)
        for (const OrTimetable::Command &command : station.commands)
            if (!OrTimetableCommandRun(command.name))
                out.push_back(OrTimetableProblem{QString(),
                                                 //% "station %1: $%2 is not run by TSRE yet"
                                                 qtTrId("timetable.check.station.command").arg(station.name, command.name),
                                                 OrTimetableProblem::Unsupported});
            else
                for (const OrTimetable::Qualifier &qualifier : command.qualifiers)
                    if (!OrTimetableQualifierRun(command.name, qualifier.name))
                        out.push_back(OrTimetableProblem{QString(),
                                                         qtTrId("timetable.check.qualifier")
                                                             .arg(station.name, command.name, qualifier.name),
                                                         OrTimetableProblem::Unsupported});
    return out;
}
