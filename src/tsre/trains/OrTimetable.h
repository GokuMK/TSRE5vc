#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

class TDB;

// An Open Rails timetable (task simulation 08): a `.timetable-or` (or
// `.timetable_or`) file read as Open Rails reads it (its TimetableReader and
// ProcessTimetable; the manual's timetable chapter), every cell kept as
// written beside what it says. Trains are columns, stations rows; special
// rows (#path, #consist, #start, #note, #dispose, #speed…, #restartdelay,
// #briefing, #comment) and columns (#comment, blank continuations).
class OrTimetable {
public:
    // A command ("$name=value+value /qualifier=value"): its name in lower
    // case without the '$', its values, its qualifiers, as written.
    struct Qualifier {
        QString name;
        QStringList values;
    };
    struct Command {
        QString name;
        QStringList values;
        QVector<Qualifier> qualifiers;
        QString text;
        // The qualifier's values (empty when it is not there); whether it
        // is there.
        QStringList qualifier(const QString &name) const;
        bool has(const QString &name) const;
    };
    // A stop cell's timing: arrival, departure, passing (seconds after
    // midnight, -1 none); '*' may depart early, 'x' a request stop.
    struct Timing {
        int arrival = -1;
        int departure = -1;
        int passing = -1;
        bool early = false;
        bool request = false;
        bool valid = true;
        QString text;
    };
    // A train's cell in a station row.
    struct Stop {
        int station = -1;
        Timing timing;
        QVector<Command> commands;
        QString text;
    };
    // A station row: the platforms' station name (as Open Rails matches it,
    // in lower case), its commands for every train, its row.
    struct Station {
        QString name;
        QString key;
        QVector<Command> commands;
        int row = -1;
    };
    // One consist of a train's consist row, `$reverse` or not.
    struct ConsistPart {
        QString name;
        bool reverse = false;
    };
    struct Train {
        QString name;
        int column = -1;
        // Static: named `$static` (no name of its own), or `$static` at its
        // start.
        bool isStatic = false;
        QString path;
        QString consist;
        QVector<ConsistPart> consists;
        // #start: the time (seconds after midnight, -1 none) and the
        // commands after it.
        QString start;
        int startTime = -1;
        QVector<Command> startCommands;
        QVector<Command> notes;
        QVector<Command> dispose;
        // #speed…: the train's top speed in m/s (-1 none) and the commands
        // there.
        float speed = -1.0f;
        QVector<Command> speedCommands;
        QString restartDelay;
        QString briefing;
        QVector<Stop> stops;
        // The stop at a station, or none.
        const Stop *stopAt(int station) const;
        // Its path's name without ".pat", as Open Rails finds the file
        // (ProcessTimetable.ExtractPathString): ".pat" added where the cell
        // has no extension (a trailing '.' none either).
        QString pathName() const;
    };

    // Reads the file; false when it cannot be read as a timetable (the
    // problems say why).
    bool load(const QString &file);

    QString file;
    // The cell where the first #comment row and column meet: the
    // timetable's description, as Open Rails shows it.
    QString description;
    QChar separator;
    QVector<Station> stations;
    QVector<Train> trains;
    // Problems reading it: rows, columns, cells Open Rails would skip or
    // warn about.
    QStringList problems;

    // The train named so (any case), or -1.
    int trainNamed(const QString &name) const;

    // A command string ("$a=b /c $d") split into its commands.
    static QVector<Command> Commands(const QString &text);
    static Command ParseCommand(const QString &text);
    // A cell's timing part ("12:30", "12:28-12:30", "12P30", "12*30", "x").
    static Timing ParseTiming(const QString &text);
    // "HH:mm" or "HH:mm:ss" as seconds after midnight, -1 not a time.
    static int ParseTime(const QString &text);
    // A consist row's cell ("a + b $reverse", "<a+b>$reverse").
    static QVector<ConsistPart> ParseConsist(const QString &text);
    // A file's text as Open Rails' StreamReader decodes it: the byte order
    // mark's encoding, else UTF-8.
    static QString Decode(const QByteArray &bytes);
    // A folder under `base` by its relative path, each name's case not
    // minded (Open Rails content made on Windows: OR_PSK has both TRAINS
    // and Trains, the consists in the second); the variant holding the rest
    // of the path first. Empty when there is none.
    static QString FindPath(const QString &base, const QString &relative);
};

// A timetable group (`.timetablelist-or`): its name (the first line after
// '#', else the file's), the timetables' files in its folder.
struct OrTimetableGroup {
    QString file;
    QString name;
    QStringList timetables;
    bool load(const QString &file);
};

// What a timetable asks that the route or TSRE cannot give (task 08's
// check): paths and consists not there, stations no platform has, trains
// referred to that are not in it, commands TSRE does not run yet, timings
// it cannot read. `routeDirectory` holds PATHS; `consistDirectory` the
// .con files; `tdb` the route's platforms (none: stations not checked).
struct OrTimetableProblem {
    QString train;
    QString text;
};
QVector<OrTimetableProblem> CheckTimetable(const OrTimetable &timetable, const QString &routeDirectory,
                                           const QString &consistDirectory, TDB *tdb);

// The commands TSRE runs (those PRUDNIK's timetable uses, the user's
// decision of 2026-10-21); the others are read and reported.
bool OrTimetableCommandRun(const QString &name);
