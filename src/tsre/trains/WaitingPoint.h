#pragma once
#include <QString>

// The value of a path's waiting point: MSTS's seconds, or one of Open
// Rails' meanings (its manual, "Extended activities").
namespace WaitingPoint {

enum Kind {
    Seconds,      // 1-29999: wait so long
    UntilTime,    // 30000 + HHMM: wait until the time of day
    KeepFirst,    // 4NNSS: uncouple, keep the first NN cars, wait SS s
    KeepLast,     // 5NNSS: uncouple, keep the last NN cars, wait SS s
    Horn,         // 60010 + N: the horn for N (1-10) seconds, no stop
    CrossingHorn, // 60021: the level crossing horn pattern, no stop
    Attach,       // 60001: attach to the next train standing ahead, then
                  // join it (Open Rails' join and split); no wait
    Permission    // 60002: ask to pass the next signal into an occupied
                  // block, wait 20 s
};

struct Point {
    Kind kind = Seconds;
    int seconds = 0;
    int hour = 0;
    int minute = 0;
    int cars = 0;
};

Point decode(int value);
int encode(const Point &point);
// The value as the editor shows it.
QString describe(int value);

}
