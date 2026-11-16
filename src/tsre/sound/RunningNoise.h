/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RUNNINGNOISE_H
#define RUNNINGNOISE_H

#include <QString>

// Which running noise vehicles that carry their own play on a route (MaSzyna
// cars: outernoise:, the cab's runningnoise:), the route's
// TsreRunningNoise ( Vehicle ) in its .trk:
// - Vehicle: their own only (the default, also when the route says nothing);
// - Both: the route's track sounds (ttype.dat by sound region) and their own;
// - Route: the route's track sounds only;
// - Regions: their own, and the route's track sounds only where a sound
//   region gives a type other than the default (bridges, tunnels).
// Vehicles without their own (ENG cars) always play the route's track sounds.
enum class RunningNoise { Unset, Vehicle, Both, Route, Regions };

namespace RunningNoiseNames {
// As written in the .trk ("Vehicle"...); empty for Unset.
QString name(RunningNoise value);
// Any case; Unset when it is none of them.
RunningNoise parse(const QString &text);
}

#endif
