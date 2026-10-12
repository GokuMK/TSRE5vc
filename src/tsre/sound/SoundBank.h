/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SOUNDBANK_H
#define SOUNDBANK_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <memory>
#include <vector>

struct WavData;

// A wave ready to play, split into parts at its loop points: with n points
// (n > 1) part 0 is the intro, parts 1 to n - 1 the sustain that loops, and
// part n the release; fewer points make one part, the whole wave (as Open
// Rails splits them).
struct SoundClip {
    struct Part {
        uint32_t begin = 0;
        uint32_t end = 0;
    };

    QString path;
    int sampleRate = 0;
    int channels = 0;
    // The file had one channel: MSTS plays those at double frequency in
    // groups marked Stereo (Open Rails' "MSTS mono treatment").
    bool originallyMono = false;
    std::vector<int16_t> samples;
    std::vector<Part> parts;

    size_t frames() const { return channels > 0 ? samples.size() / size_t(channels) : 0; }

    static std::unique_ptr<SoundClip> fromWave(const QString &path, const WavData &wave);
};

// Clips read once per file and kept until clear(): players on the audio
// thread hold plain pointers to them.
class SoundBank {
public:
    // The clip of a file, mono (for sounds placed in 3D) or as stored. Null
    // when it cannot be read; reported once.
    const SoundClip *get(const QString &path, bool mono);
    void clear();
    int size() const { return int(clips.size()); }

    // The first of the folders holding the file, as Open Rails looks for a
    // sound's waves (its .sms folder, the route's SOUND, the root's SOUND).
    static QString find(const QString &name, const QStringList &folders);

private:
    QHash<QString, std::shared_ptr<SoundClip>> clips;
};

#endif
