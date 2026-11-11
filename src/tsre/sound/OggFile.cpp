/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/sound/OggFile.h>
#include <tsre/sound/StbVorbisConfig.h>

#include <QFile>

namespace {

bool fail(WavData &out, QString *error, const QString &reason) {
    out = WavData();
    if (error != nullptr)
        *error = reason;
    return false;
}

// stb_vorbis's reasons, the ones a broken or foreign file gives.
QString reasonOf(int code) {
    switch (code) {
    case VORBIS_outofmem:
        return "out of memory";
    case VORBIS_unexpected_eof:
        return "cut short";
    case VORBIS_invalid_setup:
    case VORBIS_invalid_stream:
    case VORBIS_invalid_first_page:
    case VORBIS_invalid_stream_structure_version:
    case VORBIS_continued_packet_flag_invalid:
    case VORBIS_incorrect_stream_serial_number:
    case VORBIS_bad_packet_type:
    case VORBIS_cant_find_last_page:
    case VORBIS_seek_failed:
    case VORBIS_ogg_skeleton_not_supported:
        return "not an Ogg Vorbis stream (" + QString::number(code) + ")";
    case VORBIS_missing_capture_pattern:
        return "no Ogg page";
    default:
        return "unreadable (" + QString::number(code) + ")";
    }
}

}

bool OggFile::isOgg(const QByteArray &bytes) {
    return bytes.startsWith("OggS");
}

bool OggFile::parse(const QByteArray &bytes, WavData &out, QString *error) {
    if (!isOgg(bytes))
        return fail(out, error, "no Ogg page");
    int code = 0;
    stb_vorbis *vorbis = stb_vorbis_open_memory(reinterpret_cast<const unsigned char *>(bytes.constData()),
                                                int(bytes.size()), &code, nullptr);
    if (vorbis == nullptr)
        return fail(out, error, reasonOf(code));
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    if (info.channels < 1 || info.channels > 2) {
        stb_vorbis_close(vorbis);
        return fail(out, error, QString("%1 channels").arg(info.channels));
    }
    out = WavData();
    out.sampleRate = int(info.sample_rate);
    out.channels = info.channels;
    out.sourceBits = 16;
    // The length from the last page, when it can be found: one allocation.
    const unsigned int length = stb_vorbis_stream_length_in_samples(vorbis);
    if (length > 0)
        out.samples.reserve(size_t(length) * size_t(info.channels));
    short buffer[4096];
    for (;;) {
        const int frames = stb_vorbis_get_samples_short_interleaved(vorbis, info.channels, buffer, 4096);
        if (frames <= 0)
            break;
        out.samples.insert(out.samples.end(), buffer, buffer + size_t(frames) * size_t(info.channels));
    }
    code = stb_vorbis_get_error(vorbis);
    stb_vorbis_close(vorbis);
    if (out.samples.empty())
        return fail(out, error, code != VORBIS__no_error ? reasonOf(code) : QString("no sound"));
    return true;
}

bool OggFile::load(const QString &path, WavData &out, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(out, error, "cannot open " + path);
    return parse(file.readAll(), out, error);
}
