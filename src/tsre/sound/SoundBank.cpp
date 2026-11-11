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
#include <tsre/sound/OggFile.h>
#include <tsre/sound/WavFile.h>
#include <tsre/fileFunctions/ContentPath.h>

#include <QDebug>
#include <QDeadlineTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <atomic>

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

struct SoundBank::Inbox {
    struct Done {
        QString key;
        QString path;
        std::shared_ptr<SoundClip> clip;
        QString error;
    };
    QMutex mutex;
    std::vector<Done> done;
    std::atomic<bool> any{false};
};

namespace {

// The workers decoding Ogg Vorbis: a core or two, leaving the rest to the
// game (QThreadPool, not std::thread: MinGW, Qt's file access).
QThreadPool &decoders() {
    static QThreadPool pool;
    static const bool sized = [] {
        pool.setMaxThreadCount(std::clamp(QThread::idealThreadCount() - 1, 1, 2));
        return true;
    }();
    (void)sized;
    return pool;
}

const char *suffixOf(SoundBank::Channels channels) {
    switch (channels) {
    case SoundBank::Channels::Second:
        return "|mono";
    case SoundBank::Channels::Mixed:
        return "|mix";
    default:
        return "";
    }
}

std::shared_ptr<SoundClip> clipOf(const QString &path, const WavData &wave, SoundBank::Channels channels) {
    const bool originallyMono = wave.channels == 1;
    std::shared_ptr<SoundClip> clip = SoundClip::fromWave(
            path, channels == SoundBank::Channels::Second  ? WavFile::toMono(wave)
                  : channels == SoundBank::Channels::Mixed ? WavFile::mixToMono(wave)
                                                           : wave);
    clip->originallyMono = originallyMono;
    return clip;
}

}

SoundBank::SoundBank() : inbox(std::make_shared<Inbox>()) {}

SoundBank::~SoundBank() = default;

const SoundClip *SoundBank::get(const QString &path, bool mono) {
    return get(path, mono ? Channels::Second : Channels::Stored);
}

const SoundClip *SoundBank::get(const QString &path, Channels channels, bool *isLoading) {
    if (isLoading != nullptr)
        *isLoading = false;
    collect();
    const QString key = ContentPath::key(path) + suffixOf(channels);
    auto found = clips.constFind(key);
    if (found != clips.constEnd())
        return found.value().get();
    if (loading.contains(key)) {
        if (isLoading != nullptr)
            *isLoading = true;
        return nullptr;
    }
    QFile file(ContentPath::normalize(path));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "Sound: cannot read" << path << "- cannot open it";
        clips.insert(key, nullptr);
        return nullptr;
    }
    const QByteArray bytes = file.readAll();
    if (OggFile::isOgg(bytes)) {
        loading.insert(key);
        if (isLoading != nullptr)
            *isLoading = true;
        decoders().start([box = inbox, key, path, bytes, channels] {
            WavData wave;
            Inbox::Done done{key, path, nullptr, QString()};
            if (OggFile::parse(bytes, wave, &done.error))
                done.clip = clipOf(path, wave, channels);
            QMutexLocker lock(&box->mutex);
            box->done.push_back(std::move(done));
            box->any.store(true, std::memory_order_release);
        });
        return nullptr;
    }
    WavData wave;
    QString error;
    std::shared_ptr<SoundClip> clip;
    if (WavFile::parse(bytes, wave, &error))
        clip = clipOf(path, wave, channels);
    else
        qWarning().noquote() << "Sound: cannot read" << path << "-" << error;
    clips.insert(key, clip);
    return clip.get();
}

void SoundBank::collect() {
    if (!inbox->any.load(std::memory_order_acquire))
        return;
    std::vector<Inbox::Done> done;
    {
        QMutexLocker lock(&inbox->mutex);
        done.swap(inbox->done);
        inbox->any.store(false, std::memory_order_release);
    }
    for (Inbox::Done &d : done) {
        if (!loading.remove(d.key))
            continue;
        if (d.clip == nullptr)
            qWarning().noquote() << "Sound: cannot read" << d.path << "-" << d.error;
        clips.insert(d.key, std::move(d.clip));
    }
}

bool SoundBank::waitForLoading(int msecs) {
    const QDeadlineTimer deadline = msecs < 0 ? QDeadlineTimer(QDeadlineTimer::Forever) : QDeadlineTimer(msecs);
    while (!loading.isEmpty()) {
        collect();
        if (loading.isEmpty())
            break;
        if (deadline.hasExpired())
            return false;
        QThread::msleep(2);
    }
    return true;
}

void SoundBank::clear() {
    clips.clear();
    loading.clear();
    // Files still being decoded land in the old inbox, which nothing reads.
    inbox = std::make_shared<Inbox>();
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
