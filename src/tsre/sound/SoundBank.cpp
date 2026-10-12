/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/sound/SoundBank.h>
#include <tsre/sound/WavFile.h>
#include <tsre/fileFunctions/ContentPath.h>

#include <QDebug>
#include <QFileInfo>

std::unique_ptr<SoundClip> SoundClip::fromWave(const QString &path, const WavData &wave) {
    auto clip = std::make_unique<SoundClip>();
    clip->path = path;
    clip->sampleRate = wave.sampleRate;
    clip->channels = wave.channels;
    clip->samples = wave.samples;
    const uint32_t frames = uint32_t(wave.frames());
    const std::vector<uint32_t> &points = wave.cuePoints;
    if (points.size() < 2) {
        clip->parts.push_back({0, frames});
    } else {
        uint32_t begin = 0;
        for (uint32_t point : points) {
            clip->parts.push_back({begin, point});
            begin = point;
        }
        clip->parts.push_back({begin, frames});
    }
    return clip;
}

const SoundClip *SoundBank::get(const QString &path, bool mono) {
    const QString key = ContentPath::key(path) + (mono ? "|mono" : "");
    auto found = clips.constFind(key);
    if (found != clips.constEnd())
        return found.value().get();
    WavData wave;
    QString error;
    std::shared_ptr<SoundClip> clip;
    if (WavFile::load(ContentPath::normalize(path), wave, &error)) {
        const bool originallyMono = wave.channels == 1;
        clip = SoundClip::fromWave(path, mono ? WavFile::toMono(wave) : wave);
        clip->originallyMono = originallyMono;
    } else {
        qWarning().noquote() << "Sound: cannot read" << path << "-" << error;
    }
    clips.insert(key, clip);
    return clip.get();
}

void SoundBank::clear() {
    clips.clear();
}

QString SoundBank::find(const QString &name, const QStringList &folders) {
    if (name.isEmpty())
        return QString();
    for (const QString &folder : folders) {
        if (folder.isEmpty())
            continue;
        const QString path = ContentPath::join(folder, name);
        if (QFileInfo(path).isFile())
            return path;
    }
    return QString();
}
