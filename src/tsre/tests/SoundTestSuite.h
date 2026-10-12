#ifndef TSRE_SOUND_TEST_SUITE_H
#define TSRE_SOUND_TEST_SUITE_H

#include <QString>

namespace TsreTests {
// The sound module without a device: wave reading, .sms parsing, triggers,
// playback queues and offline renders.
int runSoundSuite(bool verbose);
// Reads every .wav and .sms under a content root and reports what fails.
int runSoundCorpus(const QString &root, bool verbose);
}

#endif
