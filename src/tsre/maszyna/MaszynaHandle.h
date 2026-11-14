/*  This file is part of TSRE5 (task driving 10, step 5).
 *
 *  It is a port of parts of MaSzyna's TMoverParameters (McZapkie/Mover.cpp
 *  at 66a940d4, https://github.com/eu07/maszyna: the brake handles' keys in
 *  LoadFIZ_Brake and LoadFIZ_Cntrl, their creation in
 *  CheckLocomotiveParameters, the driver's part of UpdatePipePressure), and
 *  so is under the Mozilla Public License 2.0, as MaSzyna's code is: this
 *  Source Code Form is subject to the terms of the Mozilla Public License,
 *  v. 2.0. If a copy of the MPL was not distributed with this file, You can
 *  obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef MASZYNAHANDLE_H
#define MASZYNAHANDLE_H

struct lua_State;

// The native module `require "maszyna.handle"` (task driving 10, step 5):
// a MaSzyna loco's driver's brake handle (its .fiz's BrakeHandle: FV4a,
// FVel6, FVE408, MHZ_EN57, MHZ_K8P, MHZ_K5P, MHZ_6P, M394, Knorr, St113)
// and local brake handle (LocBrakeHandle: FD1, Knorr), as MaSzyna's
// TMoverParameters works them (MaszynaHamulce's handles), for its loco
// script.
//   at = handle.positions(name)
//     a BrakeHandle's positions by function { min, max, fs, rp, np, mb, fb,
//     eb, epr, epn, epb } (MaSzyna's GetPos), nil for one not ported.
//   config = handle.config(inputs.vehicle)
//     nil without a ported handle (or BCPN under 2); else { name, localName
//     (nil without one), ep (the handle gives EP), positions = { min, max,
//     fs, rp, np, mb, fb, eb, epr, epn, epb } (MaSzyna's GetPos) }.
//   air, info = handle.step(inputs.vehicle, dt, state, controls)
//     state     a list of numbers it keeps (empty: Init, the ready state);
//     controls  position (BrakeCtrlPosR, MaSzyna's position), pipe (Pa,
//               the brake pipe at the loco), volume (m³, its length),
//               supply (Pa, the main reservoir feeding the handle),
//               localPosition (0 to 1, LocalBrakePosA), cylinder (Pa, the
//               loco's), releaser (held: the pipe's lock opened),
//               emergency (MaSzyna's emergency valve open: the vigilance's
//               braking, the alarm chain), reductor (bar), universal (the
//               universal buttons' flags), epPowered (24 V and a
//               direction: EP handles give EP), main (false: the local
//               handle only, a loco no driver occupies);
//   gives the air into the brake pipe (Pa·m³, - out of it) and a table:
//   supply (Pa·m³ the handle took from the main reservoir), control (Pa,
//   the handle's control chamber), gauge (Pa, its GetCP: MaSzyna's
//   limpipepress gauge), localPressure (Pa, the local handle's: the
//   independent brake's line), eps (MaSzyna's EP signal), epDemand (that as
//   TSRE's EP wire: 0 to 1 a demand, -2 holding, -1 none).
namespace MaszynaHandle {
int openModule(lua_State *L);
}

#endif // MASZYNAHANDLE_H
