/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef EXPLOREWALK_H
#define EXPLOREWALK_H

#include <QString>
#include <functional>
#include <memory>

class TDB;

namespace Characters {
class RouteCollision;
}

namespace Sim {
struct SimulationInput;
struct StopPlan;
struct TrainPlan;
}

namespace Explore {

// Where a walking run starts (task explore 04): a platform of the chosen
// station and the track at its middle.
struct WalkStart {
    QString station;
    // The platform's two track items and its name.
    int platformStart = -1;
    int platformEnd = -1;
    QString platformName;
    // How it was chosen: the train's stop there, the station's busiest
    // platform, or its longest.
    enum class Why { TrainStop, Busiest, Longest } why = Why::Longest;
    // The track's middle along the platform: the editor's tile, the place
    // in it (y the track database's height, the track's base), and the
    // track's way there (unit, editor x and z).
    int tile[2] = {0, 0};
    float at[3] = {0.0f, 0.0f, 0.0f};
    float along[2] = {0.0f, 1.0f};
};

// The platform a walking run starts on, from the run's input: `train`'s
// stop at `station` (-1 none); else the station's platform the most stops
// of the run use; else its longest. False when the station has no
// platform in the track database.
bool walkPlatform(const Sim::SimulationInput &in, TDB &tdb, const QString &station, int train, WalkStart &out);

// The train of the run a walk follows: the player's, or a timetable's train
// by name; -1 none.
int walkTrain(const Sim::SimulationInput &in, const QString &timetableTrain);

// When `train` reaches `station` (seconds after midnight; its start when it
// starts at a platform there) in a run of `in` simulated as fast as it
// goes, as the Timetable window's Simulate runs one (exploring with a
// train: its arrival is known only so); -1 when its stops have none there
// or it does not get there within 24 hours.
double walkArrival(const std::shared_ptr<const Sim::SimulationInput> &in, TDB &tdb, int train,
                   const QString &station);

// The place to stand on the platform, once `probe` (gathered about the
// start) has its objects: its side (the floor 0.15 to 1.8 m above the rail
// 2.5 to 4.5 m out; else the side with any floor), 3.5 m out from the
// track, a metre above that floor; the way facing the track (the camera's
// yaw). Waiting while the probe's tiles or objects are not read yet.
enum class WalkPlace { Waiting, Found, None };
WalkPlace walkPlace(const WalkStart &start, Characters::RouteCollision &probe, bool waitForObjects, int *tile,
                    float *at, float &yaw);

}

#endif // EXPLOREWALK_H
