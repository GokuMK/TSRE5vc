#pragma once
#include <QString>
#include <QVector>

// Open Rails' timetable commands as its manual (the timetable chapter) and
// its code (ProcessTimetable, TTTrain) describe them, for the Timetable
// window's command picker (task simulation 08): where each is written, its
// value and qualifiers, what it does. Whether TSRE runs it is
// OrTimetableCommandRun's.
struct OrCommandSpec {
    // Where a command is written: a train's #start, #note, #dispose, speed
    // row; its cell at a station (a stop); a station's row (every train's
    // stop there).
    enum Place { Start = 1, Note = 2, Dispose = 4, SpeedRow = 8, Stop = 16, Station = 32 };
    // The picker's headings, in this order.
    enum Group { StartGroup, NoteGroup, SpeedGroup, StationGroup, TimingGroup, ShuntingGroup, DisposeGroup };
    // A value: none, a time (HH:mm), another train's name, a number (its
    // unit in the hint), a speed (the speed row's unit), a path's name, a
    // word.
    enum Value { None, Time, Train, Number, Speed, Path, Text };
    struct QualifierSpec {
        QString name;
        Value value = None;
        QString hint;
        QString description;
    };
    QString name;
    Group group = StartGroup;
    int places = 0;
    Value value = None;
    // The value may be left out ($create without a time).
    bool valueOptional = false;
    QString hint;
    QString description;
    QVector<QualifierSpec> qualifiers;

    const QualifierSpec *qualifier(const QString &name) const;
    // The heading of a group.
    static QString GroupName(Group group);
};

// Every command (the same name twice where its meaning differs by place:
// the speed row's $attach is a speed, a stop's a train); built once, in
// the language then.
const QVector<OrCommandSpec> &OrCommandSpecs();
// The command written there, or none.
const OrCommandSpec *OrCommandSpecAt(const QString &name, int place);
