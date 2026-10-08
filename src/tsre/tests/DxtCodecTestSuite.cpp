#include "DxtCodecTestSuite.h"

#include <QByteArray>
#include <QDebug>
#include <QtEndian>
#include <cstdlib>
#include <tsre/texture/DxtCodec.h>

namespace {
// The colour endpoints of a DXT1 block, 8-bit per channel, as DxtCodec decodes them.
void endpoints(const unsigned char *block, int first[3], int second[3]) {
    const quint16 v[2] = {qFromLittleEndian<quint16>(block), qFromLittleEndian<quint16>(block + 2)};
    int *out[2] = {first, second};
    for (int e = 0; e < 2; ++e) {
        out[e][0] = int(v[e] >> 11) * 255 / 31;
        out[e][1] = int((v[e] >> 5) & 63) * 255 / 63;
        out[e][2] = int(v[e] & 31) * 255 / 31;
    }
}
} // namespace

int TsreTests::runDxtCodecSuite(bool verbose) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) {
        if (ok) {
            ++passed;
            if (verbose) qInfo() << "[tests:dxt-codec] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:dxt-codec] FAIL" << name;
        }
    };
    using DxtCodec::Format;

    // Decodes the DXT1 image and its BC3 transcode. Four-colour blocks and
    // alpha must match exactly; a three-colour block's midpoint may move a
    // sixth of the endpoints' distance.
    auto matches = [](const QByteArray &dxt1, int w, int h) {
        QByteArray bc3, expected, actual;
        QString error;
        if (!DxtCodec::dxt1ToBc3(dxt1, bc3) || bc3.size() != DxtCodec::byteSize(w, h, Format::Dxt5)
            || !DxtCodec::decode(dxt1, w, h, Format::Dxt1, true, expected, error)
            || !DxtCodec::decode(bc3, w, h, Format::Dxt5, true, actual, error))
            return false;
        const auto *blocks = reinterpret_cast<const unsigned char *>(dxt1.constData());
        const int blocksWide = (w + 3) / 4;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const unsigned char *block = blocks + ((y / 4) * blocksWide + x / 4) * 8;
                const bool threeColour = qFromLittleEndian<quint16>(block) <= qFromLittleEndian<quint16>(block + 2);
                int first[3], second[3];
                endpoints(block, first, second);
                const qsizetype at = (qsizetype(y) * w + x) * 4;
                if (expected[at + 3] != actual[at + 3])
                    return false;
                if (uchar(expected[at + 3]) == 0)
                    continue;
                for (int k = 0; k < 3; ++k) {
                    const int tolerance = threeColour ? std::abs(first[k] - second[k]) / 6 + 1 : 0;
                    if (std::abs(uchar(expected[at + k]) - uchar(actual[at + k])) > tolerance)
                        return false;
                }
            }
        return true;
    };

    // Red to blue in four colours, and blue to red in three with a
    // transparent index: indices 0, 1, 2, 3 in turn.
    QByteArray fourColour(8, '\0'), threeColour(8, '\0');
    qToLittleEndian<quint16>(0xF800, fourColour.data());
    qToLittleEndian<quint16>(0x001F, fourColour.data() + 2);
    qToLittleEndian<quint32>(0xE4E4E4E4, fourColour.data() + 4);
    qToLittleEndian<quint16>(0x001F, threeColour.data());
    qToLittleEndian<quint16>(0xF800, threeColour.data() + 2);
    qToLittleEndian<quint32>(0xE4E4E4E4, threeColour.data() + 4);
    check(matches(fourColour, 4, 4), "a four-colour block transcodes exactly");
    check(matches(threeColour, 4, 4), "a three-colour block keeps its transparency and colours");
    QByteArray bc3, pixels;
    QString error;
    check(DxtCodec::dxt1ToBc3(threeColour, bc3) && DxtCodec::decode(bc3, 4, 4, Format::Dxt5, true, pixels, error)
          && uchar(pixels[3]) == 255 && uchar(pixels[3 * 4 + 3]) == 0 && uchar(pixels[0]) == 0
          && uchar(pixels[2]) == 255 && uchar(pixels[4]) == 255 && uchar(pixels[6]) == 0,
          "endpoints stay exact and index 3 becomes transparent");

    // An encoded cutout image (a disc), 10x10 has partial edge blocks.
    for (const int size : {16, 10}) {
        QByteArray rgba(size * size * 4, '\0');
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                unsigned char *p = reinterpret_cast<unsigned char *>(rgba.data()) + (y * size + x) * 4;
                const int dx = 2 * x + 1 - size, dy = 2 * y + 1 - size;
                p[0] = uchar(x * 255 / size);
                p[1] = uchar(y * 255 / size);
                p[2] = uchar(128 + x * 7);
                p[3] = dx * dx + dy * dy < size * size ? 255 : 0;
            }
        QByteArray dxt1;
        const bool encoded = DxtCodec::encode(reinterpret_cast<const unsigned char *>(rgba.constData()),
                                              rgba.size(), size, size, 4, Format::Dxt1, true, dxt1, error);
        check(encoded && matches(dxt1, size, size), size == 16 ? "an encoded cutout image transcodes"
                                                               : "a cutout image with partial blocks transcodes");
    }
    check(!DxtCodec::dxt1ToBc3(QByteArray(12, '\0'), bc3), "a partial block is rejected");

    qInfo() << "[tests:dxt-codec] cases=" << (passed + failed) << "passed=" << passed
            << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
