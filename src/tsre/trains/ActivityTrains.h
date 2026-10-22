#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

class Activity;
class ActivityServiceDefinition;
class ActivityTimetable;
class Traffic;

// The activity's trains as the Timetable window lists them (task editor
// 07, phase 7): the player's, its AI trains, and the runs of its traffic
// pattern it does not have. The player's times are in the activity; an AI
// train's in the pattern, found by service name and start time.
namespace ActivityTrains {

enum Kind { Player, AI, PatternOnly };

struct Train {
    Kind kind = AI;
    QString service;
    int time = 0;
    // AI: its place in the activity's traffic (Activity::traffic->service);
    // PatternOnly: the run's place in the pattern.
    int index = -1;
    // Where its times are, or none.
    ActivityTimetable *timetable = nullptr;
    // An Open Rails timetable set's train (task simulation 08): its
    // timetable and place there (Activity::timetables), static or not.
    int orTimetable = -1;
    int orTrain = -1;
    bool staticTrain = false;
    QString path, consist;
    QString firstStop, lastStop;
    QStringList problems;
};

// Sorted by start time, the player's first at equal times. An Open Rails
// timetable set's trains: AI, the one chosen (Activity::playerTrain) the
// player's.
QVector<Train> List(Activity *activity);
// The route's folder of an activity of either kind.
QString RouteDirectory(Activity *activity);
// The traffic pattern the activity names, or none.
Traffic *Pattern(Activity *activity);
// The other activities naming the pattern.
QStringList OtherUsers(Activity *activity);
// The activity's entry for the train (player or AI), its times linked.
ActivityServiceDefinition *Definition(Activity *activity, const Train &train);
// A new traffic pattern with the runs of the activity's, the file named
// `name` (without .trf), and the activity naming it. Null when there is
// no pattern or the name is taken. The caller keeps the activity for
// undo.
Traffic *CopyPattern(Activity *activity, const QString &name);
// Where the train starts: tile x, tile z, x, z (the file's convention).
bool StartPosition(Activity *activity, const Train &train, float *txz);

}
