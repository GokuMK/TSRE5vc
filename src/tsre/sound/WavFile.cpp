/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/sound/WavFile.h>

#include <QFile>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr uint16_t FormatPcm = 1;
constexpr uint16_t FormatFloat = 3;
constexpr uint16_t FormatExtensible = 0xFFFE;

uint16_t u16(const char *p) {
    return uint16_t(uint8_t(p[0]) | (uint8_t(p[1]) << 8));
}

uint32_t u32(const char *p) {
    return uint32_t(uint8_t(p[0])) | (uint32_t(uint8_t(p[1])) << 8) | (uint32_t(uint8_t(p[2])) << 16)
            | (uint32_t(uint8_t(p[3])) << 24);
}

bool fail(QString *error, const QString &reason) {
    if (error != nullptr)
        *error = reason;
    return false;
}

}

bool WavFile::parse(const QByteArray &bytes, WavData &out, QString *error) {
    out = WavData();
    const char *data = bytes.constData();
    const qsizetype size = bytes.size();
    if (size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
        return fail(error, "not a RIFF WAVE file");

    uint16_t format = 0;
    uint16_t channels = 0;
    uint32_t rate = 0;
    uint16_t bits = 0;
    bool haveFormat = false;
    qsizetype dataOffset = -1;
    qsizetype dataSize = 0;
    std::vector<uint32_t> points;

    // The RIFF size is often wrong: walk the chunks to the end of the bytes.
    qsizetype pos = 12;
    while (pos + 8 <= size) {
        const char *id = data + pos;
        const qsizetype chunkSize = qsizetype(u32(data + pos + 4));
        const qsizetype body = pos + 8;
        const qsizetype available = std::min(chunkSize, size - body);
        if (std::memcmp(id, "fmt ", 4) == 0 && available >= 16) {
            format = u16(data + body);
            channels = u16(data + body + 2);
            rate = u32(data + body + 4);
            bits = u16(data + body + 14);
            // WAVE_FORMAT_EXTENSIBLE: the real format is the subformat GUID's
            // first two bytes.
            if (format == FormatExtensible && available >= 26)
                format = u16(data + body + 24);
            haveFormat = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            dataOffset = body;
            dataSize = available;
        } else if (std::memcmp(id, "cue ", 4) == 0 && available >= 4) {
            // Each point: id, position, data chunk id, chunk start, block
            // start, sample offset; Open Rails sums the last three.
            points.clear();
            const uint32_t count = u32(data + body);
            for (uint32_t i = 0; i < count && 4 + 24 * qsizetype(i + 1) <= available; ++i) {
                const char *point = data + body + 4 + 24 * i;
                points.push_back(u32(point + 12) + u32(point + 16) + u32(point + 20));
            }
        } else if (std::memcmp(id, "smpl", 4) == 0 && available >= 36) {
            const uint32_t loops = u32(data + body + 28);
            if (loops > 0) {
                points.clear();
                for (uint32_t i = 0; i < loops && 36 + 24 * qsizetype(i + 1) <= available; ++i) {
                    const char *loop = data + body + 36 + 24 * i;
                    points.push_back(u32(loop + 8));
                    points.push_back(u32(loop + 12));
                }
            }
        }
        // Chunks are padded to an even length.
        pos = body + chunkSize + (chunkSize & 1);
    }

    if (!haveFormat)
        return fail(error, "no fmt chunk");
    if (dataOffset < 0 || dataSize <= 0)
        return fail(error, "no data");
    if (channels < 1 || channels > 2)
        return fail(error, QString("%1 channels").arg(channels));
    if (rate == 0)
        return fail(error, "sample rate 0");
    const bool pcm = format == FormatPcm && (bits == 8 || bits == 16);
    const bool floating = format == FormatFloat && bits == 32;
    if (!pcm && !floating)
        return fail(error, QString("unsupported format %1 with %2 bits").arg(format).arg(bits));

    const qsizetype frameBytes = qsizetype(bits / 8) * channels;
    const qsizetype frames = dataSize / frameBytes;
    if (frames == 0)
        return fail(error, "no data");
    out.sampleRate = int(rate);
    out.channels = channels;
    out.sourceBits = bits;
    out.samples.resize(size_t(frames * channels));
    const char *source = data + dataOffset;
    for (size_t i = 0; i < out.samples.size(); ++i) {
        if (bits == 8) {
            out.samples[i] = int16_t((int(uint8_t(source[i])) - 128) * 256);
        } else if (bits == 16) {
            out.samples[i] = int16_t(u16(source + 2 * i));
        } else {
            const uint32_t word = u32(source + 4 * i);
            float value;
            std::memcpy(&value, &word, sizeof(value));
            if (!std::isfinite(value))
                value = 0.0f;
            out.samples[i] = int16_t(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
        }
    }

    for (uint32_t point : points)
        if (point <= uint32_t(frames))
            out.cuePoints.push_back(point);
    std::sort(out.cuePoints.begin(), out.cuePoints.end());
    return true;
}

bool WavFile::load(const QString &path, WavData &out, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        out = WavData();
        return fail(error, "cannot open " + path);
    }
    return parse(file.readAll(), out, error);
}

WavData WavFile::toMono(const WavData &wave) {
    if (wave.channels != 2)
        return wave;
    WavData mono = wave;
    mono.channels = 1;
    mono.samples.resize(wave.frames());
    for (size_t i = 0; i < mono.samples.size(); ++i)
        mono.samples[i] = wave.samples[2 * i + 1];
    return mono;
}
