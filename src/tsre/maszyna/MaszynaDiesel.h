/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MASZYNADIESEL_H
#define MASZYNADIESEL_H

struct lua_State;

// The native module `require "maszyna.diesel"` (task maszyna 06): a MaSzyna
// mechanical or hydraulic diesel (EngineType DieselEngine) as MaSzyna's
// TMoverParameters runs it: the engine's revolutions from its torque and
// inertia against the clutch, the gears (automatic or by the second
// controller), the torque converter with its lockup, the retarder, the
// fuel governor. For maszyna.mechanical (the diesel loco's motor).
//   force, info = diesel.step(inputs.vehicle, dt, state, controls, speed)
//     state     a list of numbers it keeps (empty: started from
//               controls.rpm);
//     controls  position (the main controller, 0 to MCPN), gear (the
//               second controller), eimic (-1 to 1, controllers of
//               EIMCtrlType > 0), direction (-1, 0, 1), mains (the engine
//               on), fuel (its fuel pump running), localBrake (0 to 1),
//               cab (the loco driven: else the controller at least at
//               idle), rpm (the engine's at a start);
//     speed     m/s, + toward the loco's front;
//   gives the force (N, + toward the loco's front) and a table: rpm, fill,
//   torque (N·m), gear, clutch (0 to 1), converter, lockup, retarder (fills
//   0 to 1), stalled (the engine stopped for want of torque).
namespace MaszynaDiesel {
int openModule(lua_State *L);
}

#endif // MASZYNADIESEL_H
