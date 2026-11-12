/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef LEVELCROSSINGS_H
#define LEVELCROSSINGS_H

#include <vector>

class LevelCrObj;

// Level crossings on the main thread (task traffic 01): where their states
// come from while a simulation runs, the crossing objects loaded (cars find
// the crossing of a road item; the simulation is told their values), and
// the seconds cars and crossings move in a frame.
namespace LevelCrossings {

// Where the crossings' states come from while a simulation runs.
class StateSource {
public:
    virtual ~StateSource() = default;
    // Changes whenever a crossing's state may have.
    virtual unsigned long long crossingSerial() const = 0;
    // The crossing of a track TDB item is closed.
    virtual bool crossingClosed(int trackItem) const = 0;
};
// Null: every crossing open.
void setStateSource(const StateSource *source);
const StateSource *stateSource();
// Changes whenever the source or a state it gives may have.
unsigned long long stateSerial();

// The crossing objects, as they load and go; `changed` when their items or
// values change.
void add(LevelCrObj *object);
void remove(LevelCrObj *object);
void changed();
// Changes whenever the objects, their items or their values do.
unsigned long long version();
const std::vector<LevelCrObj *> &objects();
// The loaded crossing object with this road TDB item; null for none.
LevelCrObj *ofRoadItem(int roadItem);

// The seconds cars and crossings move in this frame: the simulation's
// while one exists (none while it is paused, more at a higher speed), else
// the view's (decided 2026-11-12).
void setFrameSeconds(double seconds);
double frameSeconds();

}

#endif
