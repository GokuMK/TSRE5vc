/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef WAVFILE_H
#define WAVFILE_H

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <vector>

// A wave file's sound as interleaved 16-bit samples, with its loop points.
// MSTS sounds are RIFF WAVE files: PCM, sometimes with "cue " points or a
// "smpl" chunk's loops after the data, which split a sound into an intro,
// a sustain part and a release (see SoundClip).
struct WavData {
    int sampleRate = 0;
    int channels = 0;
    // Bits per sample in the file (8, 16 or 32 for float).
    int sourceBits = 0;
    std::vector<int16_t> samples;
    // Positions in frames, sorted: the points of the last "cue " chunk, or
    // the start and end of each loop of the last "smpl" chunk with loops,
    // whichever comes later in the file (as Open Rails reads them). Points
    // past the end are dropped.
    std::vector<uint32_t> cuePoints;

    size_t frames() const { return channels > 0 ? samples.size() / size_t(channels) : 0; }
};

namespace WavFile {

// Reads PCM (8 or 16 bit, also as WAVE_FORMAT_EXTENSIBLE) and 32-bit float
// waves with one or two channels; chunks may come in any order. A data chunk
// longer than the file is cut to what is there. False with a reason for
// anything else (compressed formats, more channels, no data).
bool parse(const QByteArray &bytes, WavData &out, QString *error = nullptr);
bool load(const QString &path, WavData &out, QString *error = nullptr);

// One channel of a stereo wave, for sounds placed in 3D: Open Rails keeps
// the second channel. Cue points stay (they count frames).
WavData toMono(const WavData &wave);

}

#endif
