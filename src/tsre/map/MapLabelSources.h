/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MAPLABELSOURCES_H
#define MAPLABELSOURCES_H

#include "MapLabelLayer.h"
#include <vector>

class Activity;
class Coords;
class TDB;

// The names the map labels, by source (MapLabelLayer places them all together, so
// they never overlap one another). Ranked: location events, stations, platforms,
// sidings, marker sets (places by feature code and population), then OSM names.
namespace MapLabelSources {

// Platform and siding names show from this resolution in (metres per pixel); station
// and event names at every one.
constexpr float PlatformMetresPerPixel = 2.0f;
constexpr float SidingMetresPerPixel = 4.0f;

// A marker set's names at their first point (lines and areas included); nothing for
// the sets made from the track database ("Route: Stations", "Route: Sidings").
void appendMarkers(std::vector<MapLabel> &out, const Coords *markers);
// Station names once each, among their platforms; platform names (when not the
// station's) and siding names between their two ends; those of the kinds the map
// draws (platforms with their stations, sidings).
void appendTrackDatabase(std::vector<MapLabel> &out, TDB *database, bool platforms = true,
                         bool sidings = true);
// Location events' names at their place, with the time each fired in the
// activity's last run (task simulation 06) while that is for the activity
// as it is.
void appendActivity(std::vector<MapLabel> &out, Activity *activity);
// An activity's trains named beside them, as the simulation names them
// ("Local 09:30", "Express (player)"): where the simulation has them while
// it shows the activity, otherwise the player's and the AI trains shown,
// at their starts. Not its loose consists, nor an Open Rails set's static
// trains (the users' request, 2026-10-26).
void appendTrains(std::vector<MapLabel> &out, Activity *activity);

}

#endif
