/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SOUNDCSV_H
#define SOUNDCSV_H

#include <QString>

// TSRE_SOUND_CSV=<folder> (read once; task sound 03, the comparison with
// MaSzyna, extra/sound-compare): what is heard, once a second of the sound
// clock, as MaSzyna's patched build writes it.
// - sounds.csv: t,key,file,x,y,z,gain,pitch,soundproofing,distance,final;
//   each voice playing: its key (MaSzyna's, "cab:" or "control:" before a
//   cab's; "sms:<file>#<stream>" an MSTS source's), its clip, its place in
//   its car (+x left, +y up, +z front; empty when not a car's), the gain
//   its driver set, its pitch (no Doppler), the soundproofing, the
//   distance's factor, the gain it plays at (empty what is not known).
// - state.csv: t,name,value; the driven train's cab vehicle's values (what
//   its loco script publishes), its speed (speedKmh), the simulated time
//   (simTime), the listener's window share (window).
// - marks.csv: t,sim,event; what a driver did (the capture's timeline).
namespace SoundCsv {

bool enabled();
// The files written out (once a second: a run cut short keeps them).
void flush();

void sound(double t, const QString &key, const QString &file, const float *place, float gain, float pitch,
           float soundproofing, float distance, float final);
// Once a second: true for the first caller of that second.
bool takeState(double t);
void state(double t, const QString &name, const QString &value);
void mark(double t, double sim, const QString &event);

}

#endif
