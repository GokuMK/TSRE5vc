/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef WEATHERGENERATOR_H
#define WEATHERGENERATOR_H

#include <tsre/environment/Weather.h>
#include <QStringList>

// Route-scale weather from a scenario (task environment 01): a front
// sweeping over the route's area, so part of it can lie in clear air while
// another part is under cloud or rain. A front is a cross-section: cloud
// layers, rain, visibility and wind at distances ahead of and behind its
// line at the ground; the line moves across the area, bent and varied along
// its length by large-scale noise. The result is a day (hourly keyframes,
// looping) with per-cell maps, as an import gives.
namespace WeatherGenerator {

enum Scenario { ColdFront = 0, WarmFront = 1, ScenarioCount };

struct Settings {
    Scenario scenario = ColdFront;
    // The direction the front moves towards (degrees on the route's grid,
    // 0 north, 90 east) and its speed (km/h).
    double heading = 70.0;
    double speed = 35.0;
    // The hour of the day the line crosses the area's centre.
    double arrival = 14.0;
    quint32 seed = 1;
    int cellSize = 4096;
    // The area (world metres east and north).
    double west = 0.0;
    double north = 0.0;
    double east = 0.0;
    double south = 0.0;
};

// Internal names, in the order of Scenario.
QStringList scenarioNames();

// The cross-section's weather at a distance (km) ahead of the line (behind
// is negative), before the variation along the front.
WeatherKeyframe section(Scenario scenario, double ahead);

Weather generate(const Settings &settings);

}

#endif
