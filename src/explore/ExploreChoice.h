/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef EXPLORECHOICE_H
#define EXPLORECHOICE_H

#include <QDate>
#include <QString>

namespace Explore {

// A run chosen in the Explore window (task explore 02): what Run::start
// loads and drives.
struct RunChoice {
    enum class Kind { Explore, Activity, Timetable };
    Kind kind = Kind::Explore;
    // The route's folder under ROUTES, its name and loading screen (an
    // absolute path, empty for none).
    QString route;
    QString routeName;
    QString loadingScreen;
    // An activity's file (ACTIVITIES), a timetable set's (ACTIVITIES/
    // OPENRAILS).
    QString file;
    // A timetable's train.
    QString train;
    // Exploring: the path's file (PATHS) and the consist's
    // (TRAINS/CONSISTS), the start in seconds after midnight.
    QString path;
    QString consist;
    int time = 12 * 3600;
    // The day; the season it gives at the route (MSTS's 0 spring to 3
    // winter) and the weather (0 clear, 1 snow, 2 rain). An activity keeps
    // its own season and weather.
    QDate day;
    int season = 1;
    int weather = 0;
    // Walking (task explore 04): the run starts with the player's character
    // on a platform of `station`, every train its own driver. `time` is then
    // the run's start (an activity's no earlier than its own, a timetable's
    // with a train chosen 5 minutes before it calls there); exploring with a
    // train keeps `time` its start and the run starts 5 minutes before it
    // reaches the station. Exploring with no train: `path` and `consist`
    // empty. A timetable with no train: `train` empty.
    bool walk = false;
    QString station;
};

}

#endif // EXPLORECHOICE_H
