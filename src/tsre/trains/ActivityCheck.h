#pragma once
#include <QString>
#include <QVector>

class Activity;

// A problem Check Activity finds in an activity or a file it uses, and
// what its row in the list jumps to.
struct ActivityFinding {
    enum Severity { Error, Warning };
    enum Target { None, Event, Service, Traffic, Path, Position };
    Severity severity = Error;
    // A fixed name of the kind of problem ("service.missing"), for tests.
    QString code;
    QString text;
    Target target = None;
    int eventId = -1;
    // The service, traffic or path named.
    QString name;
    // Tile x and z and the place in the tile, as the files write them.
    float position[4] = {0, 0, 0, 0};
};

// Checks an activity as MSAE's verify does, and more (task editor 07,
// section 4): files that exist, instances in the activity and its traffic,
// stops on their paths, times in order, what events name, consists' cars,
// broken paths, values MSTS keeps narrower. Stops and sidings are checked
// against the track database when one is loaded.
QVector<ActivityFinding> CheckActivity(Activity *activity);
