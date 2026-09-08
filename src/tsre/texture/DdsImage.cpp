// CPU-only DDS import for image consumers; the renderer retains its staging API.
#include "DdsLib.h"
#include "DxtCodec.h"
#include <QFile>
#include <QImage>
#include <QtEndian>
#include <algorithm>

namespace {
quint32 word(const QByteArray &data, int offset) {
    return qFromLittleEndian<quint32>(data.constData() + offset);
}
bool fail(QString &error, const QString &message) {
    error = "DDS: " + message;
    return false;
}
bool validMask(quint32 mask, int bits) {
    if (!mask || (bits < 32 && (mask >> bits)))
        return false;
    while (!(mask & 1)) mask >>= 1;
    return (mask & (mask + 1)) == 0;
}
int channel(quint32 pixel, quint32 mask) {
    if (!mask) return 255;
    int shift = 0;
    while (!(mask & 1)) { mask >>= 1; ++shift; }
    return int((quint64((pixel >> shift) & mask) * 255 + mask / 2) / mask);
}
int maskBits(quint32 mask) {
    int count = 0;
    for (; mask; mask &= mask - 1) ++count;
    return count;
}
}

bool DdsLib::loadImage(const QString &path, QImage &image, DdsImageInfo &info,
                       QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, file.errorString());
    if (file.size() < 128 || file.size() > 512LL * 1024 * 1024)
        return fail(error, "File is truncated or exceeds the 512 MiB limit.");
    const QByteArray header = file.read(128);
    if (header.size() != 128 || header.left(4) != "DDS " || word(header, 4) != 124 ||
        word(header, 76) != 32)
        return fail(error, "Invalid header.");
    const quint32 w = word(header, 16), h = word(header, 12);
    if (!w || !h || w > 16384 || h > 16384 || quint64(w) * h > 64 * 1024 * 1024)
        return fail(error, "Dimensions exceed 16384 per side or 64 million pixels.");
    if ((word(header, 112) & (0xfe00u | 0x200000u)) || word(header, 24) > 1)
        return fail(error, "Cubemaps and volume textures are not supported; supply a 2D texture.");
    const quint32 flags = word(header, 80), fourcc = word(header, 84);
    DdsImageInfo resultInfo;
    resultInfo.mipCount = int(std::max(1u, word(header, 28)));
    int maxMips = 1;
    for (quint32 size = std::max(w, h); size > 1; size >>= 1) ++maxMips;
    if (word(header, 28) > quint32(maxMips))
        return fail(error, "Invalid mipmap count.");
    QImage result;
    if (flags & 4) {
        if (fourcc == 0x30315844)
            return fail(error, "DX10 DDS formats are not supported (use legacy DXT1-5 or RGB DDS).");
        DxtCodec::Format format;
        switch (fourcc) {
        case 0x31545844: format = DxtCodec::Format::Dxt1; resultInfo.encoding = "DXT1 / BC1"; break;
        case 0x32545844: format = DxtCodec::Format::Dxt2; resultInfo.encoding = "DXT2 (premultiplied)"; break;
        case 0x33545844: format = DxtCodec::Format::Dxt3; resultInfo.encoding = "DXT3 / BC2"; break;
        case 0x34545844: format = DxtCodec::Format::Dxt4; resultInfo.encoding = "DXT4 (premultiplied)"; break;
        case 0x35545844: format = DxtCodec::Format::Dxt5; resultInfo.encoding = "DXT5 / BC3"; break;
        default: return fail(error, "Unsupported FOURCC 0x" + QString::number(fourcc, 16) + '.');
        }
        resultInfo.fourCC = fourcc;
        resultInfo.alphaBits = format == DxtCodec::Format::Dxt1 ? ((flags & 1) ? 1 : 0)
                               : ((format == DxtCodec::Format::Dxt2 || format == DxtCodec::Format::Dxt3) ? 4 : 8);
        const qint64 size = DxtCodec::byteSize(int(w), int(h), format);
        if (!size || size > file.size() - 128)
            return fail(error, "Truncated compressed pixels.");
        const QByteArray blocks = file.read(size);
        result = QImage(int(w), int(h), QImage::Format_RGBA8888);
        if (result.isNull()) return fail(error, "Cannot allocate image.");
        if (!DxtCodec::decodeInto(blocks, int(w), int(h), format, true,
                                 result.bits(), result.sizeInBytes(), error))
            return false;
    } else if (flags & 0x40) {
        const quint32 bits = word(header, 88);
        if (bits != 16 && bits != 24 && bits != 32)
            return fail(error, "Only 16-, 24- and 32-bit RGB pixels are supported.");
        const quint32 r = word(header, 92), g = word(header, 96), b = word(header, 100);
        const quint32 a = (flags & 1) ? word(header, 104) : 0;
        resultInfo.bitCount = int(bits);
        resultInfo.alphaBits = maskBits(a);
        resultInfo.redBits = maskBits(r);
        resultInfo.greenBits = maskBits(g);
        resultInfo.blueBits = maskBits(b);
        if (!validMask(r, bits) || !validMask(g, bits) || !validMask(b, bits) ||
            ((flags & 1) && !validMask(a, bits)) || (r & g) || (r & b) || (g & b) ||
            (a & (r | g | b)))
            return fail(error, "Invalid or overlapping channel masks.");
        const qint64 tightPitch = qint64(w) * (bits / 8);
        const qint64 pitch = (word(header, 8) & 8) ? word(header, 20) : tightPitch;
        if (pitch < tightPitch || pitch * h > file.size() - 128)
            return fail(error, "Invalid row pitch or truncated pixels.");
        result = QImage(int(w), int(h), QImage::Format_RGBA8888);
        if (result.isNull()) return fail(error, "Cannot allocate image.");
        for (quint32 y = 0; y < h; ++y) {
            const QByteArray row = file.read(pitch);
            if (row.size() != pitch) return fail(error, "Cannot read pixel row.");
            auto *dst = result.scanLine(int(y));
            const auto *src = reinterpret_cast<const uchar *>(row.constData());
            for (quint32 x = 0; x < w; ++x) {
                quint32 pixel = 0;
                for (quint32 c = 0; c < bits / 8; ++c)
                    pixel |= quint32(src[x * (bits / 8) + c]) << (8 * c);
                *dst++ = uchar(channel(pixel, r));
                *dst++ = uchar(channel(pixel, g));
                *dst++ = uchar(channel(pixel, b));
                *dst++ = uchar(channel(pixel, a));
            }
        }
        resultInfo.encoding = QString("%1-bit %2 (uncompressed)").arg(bits).arg(a ? "RGBA" : "RGB");
    } else {
        return fail(error, "Unsupported pixel format; expected RGB or DXT1-5.");
    }
    image = std::move(result);
    info = resultInfo;
    error.clear();
    return true;
}
