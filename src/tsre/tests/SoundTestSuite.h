#ifndef TSRE_SOUND_TEST_SUITE_H
#define TSRE_SOUND_TEST_SUITE_H

#include <QString>

namespace TsreTests {
// The sound module without a device: wave reading, .sms parsing, triggers,
// playback queues and offline renders.
int runSoundSuite(bool verbose);
// Reads every .wav and .sms under a content root and reports what fails.
int runSoundCorpus(const QString &root, bool verbose);
// MaSzyna's Ogg Vorbis sounds in the game root (SOUND/MASZYNA, or sounds/
// with MaSzyna's folder as the root): known files, every tenth one decoded
// and the speed, the bank's workers. Skipped without them.
int runOggCorpus(bool verbose);
}

#endif
