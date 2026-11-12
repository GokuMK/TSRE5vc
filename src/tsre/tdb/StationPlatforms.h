/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef STATIONPLATFORMS_H
#define STATIONPLATFORMS_H

#include <QString>
#include <vector>

class TDB;

// A route's platforms from its track database (task humans 04; first the
// route displays' own, task scripting 02): each pair of platform items (its
// begin and end on one track), its station and name, and points along its
// track. Sidings are not platforms. Main thread (the track database).
namespace StationPlatforms {

struct Platform {
    // Its two items, the lower id first.
    int start = 0;
    int end = 0;
    QString station;
    QString name;
    // The track node both items lie on, -1 when they lie on two nodes; the
    // distances along it, from below to.
    int node = -1;
    double from = 0.0;
    double to = 0.0;
    // The items' places and points along the track between them (sample):
    // three each, the track's base (its shape's origin, not the rail's top)
    // in MSTS's sense: x = tileX * 2048 + x, z = tileZ * 2048 + z (north).
    double ends[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::vector<double> points;
    bool sampled = false;
};

// Every platform; none without both items.
std::vector<Platform> read(TDB &tdb);
// Its points every `step` metres along its track (its two ends only when
// its items lie on two nodes).
void sample(TDB &tdb, Platform &platform, double step);

}

#endif
