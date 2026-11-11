#pragma once

namespace TsreTests {
// MaSzyna's sounds in .mmd (task maszyna 05) on small files built here:
// both forms, every block key, the key table's numbers and rescaling,
// tables and crossfades, sets per car, includes, locations, the
// soundproofing table, the cab's pass, cab control values, the quirks.
int runMmdSoundSuite(bool verbose);
// Every vehicle's sounds under the game root's dynamic/ (MaSzyna's folder
// as the root): the files found, the names missing, the typos, ET22's
// sounds, the time.
int runMmdSoundCorpusSuite(bool verbose);
// MaSzyna's sounds played (MaszynaEmitter, MaszynaSounds) without a device:
// begin, loop and end, one-shots, tables' crossfades and pitches, distance,
// the start cutoff, soundproofing, the emitters' places.
int runMmdSoundPlayerSuite(bool verbose);
}
