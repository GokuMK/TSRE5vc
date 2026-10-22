#pragma once
#include <QString>
#include <QVector>

class Activity;

// A problem Check Activity finds in an activity or a file it uses, and
// what its row in the list jumps to.
struct ActivityFinding {
    // Run: from the activity's last simulated run (task simulation 06), not
    // a problem of its files.
    enum Severity { Error, Warning, Run };
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
// Where a loose consist stands on the track database: the distance in
// whole metres from its position (tile x and z, x and z, as the file
// writes them) to the nearest track (-1 none near), and the track behind
// it up to `length` metres (TSRE places the cars behind the position; the
// loose consists of the MSTS routes all fit so). False without a track
// database.
bool LooseConsistPlace(const float *tile, int direction, float length, float *distance, float *room);
