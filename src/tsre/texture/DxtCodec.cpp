#include "DxtCodec.h"
#include <QtEndian>
#include <algorithm>
#include <array>
#include <limits>

namespace {
using Pixel = std::array<int, 4>;
quint16 pack565(const Pixel &p) {
    return quint16(((p[0] * 31 + 127) / 255) << 11 | ((p[1] * 63 + 127) / 255) << 5 |
                   (p[2] * 31 + 127) / 255);
}
Pixel unpack565(quint16 v) {
    return {int(v >> 11) * 255 / 31, int((v >> 5) & 63) * 255 / 63, int(v & 31) * 255 / 31, 255};
}
std::array<Pixel, 4> colors(quint16 a, quint16 b, bool bc1) {
    std::array<Pixel, 4> c{unpack565(a), unpack565(b), Pixel{}, Pixel{}};
    for (int k = 0; k < 3; ++k) {
        c[2][k] = bc1 && a <= b ? (c[0][k] + c[1][k]) / 2 : (2 * c[0][k] + c[1][k]) / 3;
        c[3][k] = bc1 && a <= b ? 0 : (c[0][k] + 2 * c[1][k]) / 3;
    }
    c[2][3] = 255;
    c[3][3] = bc1 && a <= b ? 0 : 255;
    return c;
}
std::array<int, 8> alphas(int a, int b) {
    std::array<int, 8> v{a, b, 0, 0, 0, 0, 0, 0};
    if (a > b)
        for (int i = 1; i <= 6; ++i)
            v[i + 1] = ((7 - i) * a + i * b) / 7;
    else {
        for (int i = 1; i <= 4; ++i)
            v[i + 1] = ((5 - i) * a + i * b) / 5;
        v[6] = 0;
        v[7] = 255;
    }
    return v;
}
bool dimensions(int w, int h) { return w > 0 && h > 0 && qint64(w) * h <= 64 * 1024 * 1024; }
} // namespace

namespace DxtCodec {
int blockBytes(Format f) {
    return f == Format::Dxt1 ? 8 : f >= Format::Dxt2 && f <= Format::Dxt5 ? 16 : 0;
}
qint64 byteSize(int w, int h, Format f) {
    return dimensions(w, h) ? ((qint64(w) + 3) / 4) * ((qint64(h) + 3) / 4) * blockBytes(f) : 0;
}

bool decode(const QByteArray &data, int w, int h, Format f, bool alpha, QByteArray &out,
            QString &error) {
    if (!dimensions(w, h) || !blockBytes(f) || data.size() != byteSize(w, h, f)) {
        error = "Invalid DXT dimensions, format or payload size";
        return false;
    }
    const int components = alpha ? 4 : 3;
    QByteArray result(qsizetype(w) * h * components, Qt::Uninitialized);
    if (!decodeInto(data, w, h, f, alpha, reinterpret_cast<unsigned char *>(result.data()),
                    result.size(), error))
        return false;
    out = std::move(result);
    return true;
}

bool decodeInto(const QByteArray &data, int w, int h, Format f, bool alpha, unsigned char *dst,
                qsizetype size, QString &error) {
    const int components = alpha ? 4 : 3;
    if (!dimensions(w, h) || !blockBytes(f) || data.size() != byteSize(w, h, f) || !dst ||
        size != qsizetype(w) * h * components) {
        error = "Invalid DXT dimensions, format, payload or destination size";
        return false;
    }
    const auto *src = reinterpret_cast<const unsigned char *>(data.constData());
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4, src += blockBytes(f)) {
            const auto *color = src + (f == Format::Dxt1 ? 0 : 8);
            const auto c = colors(qFromLittleEndian<quint16>(color),
                                  qFromLittleEndian<quint16>(color + 2), f == Format::Dxt1);
            const quint32 selectors = qFromLittleEndian<quint32>(color + 4);
            const auto a = alphas(src[0], src[1]);
            quint64 indices = 0;
            if (f == Format::Dxt4 || f == Format::Dxt5)
                for (int i = 0; i < 6; ++i)
                    indices |= quint64(src[2 + i]) << (8 * i);
            for (int y = 0; y < 4 && by + y < h; ++y)
                for (int x = 0; x < 4 && bx + x < w; ++x) {
                    const int i = y * 4 + x;
                    Pixel p = c[(selectors >> (2 * i)) & 3];
                    if (f == Format::Dxt2 || f == Format::Dxt3)
                        p[3] = ((src[i / 2] >> ((i % 2) * 4)) & 15) * 17;
                    if (f == Format::Dxt4 || f == Format::Dxt5)
                        p[3] = a[(indices >> (3 * i)) & 7];
                    if (f == Format::Dxt2 || f == Format::Dxt4)
                        for (int k = 0; k < 3; ++k)
                            p[k] = p[3] ? std::min(255, (p[k] * 255 + p[3] / 2) / p[3]) : 0;
                    unsigned char *pixel = dst + (qsizetype(by + y) * w + bx + x) * components;
                    for (int k = 0; k < components; ++k)
                        pixel[k] = static_cast<unsigned char>(p[k]);
                }
        }
    error.clear();
    return true;
}

bool encode(const unsigned char *pixels, qsizetype size, int w, int h, int components, Format f,
            bool alpha, QByteArray &out, QString &error) {
    if (!dimensions(w, h) || !blockBytes(f) || (components != 3 && components != 4) || !pixels ||
        size != qsizetype(w) * h * components) {
        error = "Invalid DXT input pixels";
        return false;
    }
    QByteArray result(byteSize(w, h, f), '\0');
    auto *dst = reinterpret_cast<unsigned char *>(result.data());
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4, dst += blockBytes(f)) {
            std::array<Pixel, 16> block;
            Pixel lo{255, 255, 255, 255}, hi{0, 0, 0, 0};
            bool transparent = false;
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const auto *src = pixels + (qsizetype(std::min(h - 1, by + y)) * w +
                                                std::min(w - 1, bx + x)) *
                                                   components;
                    Pixel p{src[0], src[1], src[2], components == 4 ? src[3] : 255};
                    transparent |= alpha && p[3] < 128;
                    if (f == Format::Dxt2 || f == Format::Dxt4)
                        for (int k = 0; k < 3; ++k)
                            p[k] = (p[k] * p[3] + 127) / 255;
                    block[y * 4 + x] = p;
                    for (int k = 0; k < 4; ++k) {
                        lo[k] = std::min(lo[k], p[k]);
                        hi[k] = std::max(hi[k], p[k]);
                    }
                }
            const bool cutout = f == Format::Dxt1 && transparent;
            // Fit actual colors rather than opposite corners of an RGB box:
            // e.g. a red/green block must not become a black/yellow ramp.
            Pixel mean{};
            int visible = 0;
            for (const auto &p : block)
                if (!cutout || p[3] >= 128) {
                    for (int k = 0; k < 3; ++k)
                        mean[k] += p[k];
                    ++visible;
                }
            if (visible)
                for (int k = 0; k < 3; ++k)
                    mean[k] /= visible;
            auto farthest = [&](const Pixel &origin) {
                Pixel result = origin;
                int maximum = -1;
                for (const auto &p : block)
                    if (!cutout || p[3] >= 128) {
                        int distance = 0;
                        for (int k = 0; k < 3; ++k) {
                            const int d = p[k] - origin[k];
                            distance += d * d;
                        }
                        if (distance > maximum) {
                            maximum = distance;
                            result = p;
                        }
                    }
                return result;
            };
            const Pixel endpoint = farthest(mean);
            quint16 a = pack565(endpoint), b = pack565(farthest(endpoint));
            if (cutout) {
                if (a > b)
                    std::swap(a, b);
            } else {
                if (a < b)
                    std::swap(a, b);
                if (a == b) {
                    if (b > 0)
                        a = b, b--;
                    else
                        a = 1;
                }
            }
            const auto c = colors(a, b, f == Format::Dxt1);
            quint32 selectors = 0;
            for (int i = 0; i < 16; ++i) {
                int best = 0, distance = std::numeric_limits<int>::max();
                if (cutout && block[i][3] < 128)
                    best = 3;
                else
                    for (int j = 0; j < (cutout ? 3 : 4); ++j) {
                        int d = 0;
                        for (int k = 0; k < 3; ++k) {
                            int v = block[i][k] - c[j][k];
                            d += v * v;
                        }
                        if (d < distance) {
                            distance = d;
                            best = j;
                        }
                    }
                selectors |= quint32(best) << (2 * i);
            }
            auto *color = dst + (f == Format::Dxt1 ? 0 : 8);
            qToLittleEndian(a, color);
            qToLittleEndian(b, color + 2);
            qToLittleEndian(selectors, color + 4);
            if (f == Format::Dxt2 || f == Format::Dxt3)
                for (int i = 0; i < 16; ++i)
                    dst[i / 2] |= ((block[i][3] + 8) / 17) << ((i % 2) * 4);
            if (f == Format::Dxt4 || f == Format::Dxt5) {
                dst[0] = hi[3];
                dst[1] = lo[3];
                const auto values = alphas(hi[3], lo[3]);
                quint64 bits = 0;
                for (int i = 0; i < 16; ++i) {
                    int best = 0, distance = 256;
                    for (int j = 0; j < 8; ++j) {
                        int d = std::abs(block[i][3] - values[j]);
                        if (d < distance) {
                            distance = d;
                            best = j;
                        }
                    }
                    bits |= quint64(best) << (3 * i);
                }
                for (int i = 0; i < 6; ++i)
                    dst[2 + i] = (bits >> (8 * i)) & 255;
            }
        }
    out = std::move(result);
    error.clear();
    return true;
}
} // namespace DxtCodec
