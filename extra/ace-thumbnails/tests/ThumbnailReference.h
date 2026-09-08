#pragma once
#include <tsre/texture/AceDocument.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

// Independent full-frame reference for the bounded strip decoder.
inline QByteArray thumbnailReference(const AceDocument& doc, int mip, int dw, int dh) {
    QByteArray pixels; QString error; int components = 0;
    if (!doc.decode(mip, pixels, components, error)) throw std::runtime_error(error.toStdString());
    const int sw = doc.levels[mip].width, sh = doc.levels[mip].height;
    const auto* src = reinterpret_cast<const unsigned char*>(pixels.constData());
    QByteArray result(dw * dh * 4, '\0');
    for (int y = 0; y < dh; ++y) for (int x = 0; x < dw; ++x) {
        const double top = double(y) * sh / dh, bottom = double(y + 1) * sh / dh;
        const double left = double(x) * sw / dw, right = double(x + 1) * sw / dw;
        double sum[4]{};
        for (int sy = int(top); sy < std::min(sh, int(std::ceil(bottom))); ++sy)
            for (int sx = int(left); sx < std::min(sw, int(std::ceil(right))); ++sx) {
                const double weight = (std::min(bottom, double(sy+1)) - std::max(top, double(sy))) *
                                      (std::min(right, double(sx+1)) - std::max(left, double(sx)));
                const auto* p = src + (sy * sw + sx) * components;
                const double a = components == 4 ? p[3] : 255;
                for (int k = 0; k < 3; ++k) sum[k] += p[k] * a / 255.0 * weight;
                sum[3] += a * weight;
            }
        for (int k = 0; k < 4; ++k)
            result[(y * dw + x) * 4 + (k == 3 ? 3 : 2 - k)] = static_cast<char>(
                std::clamp(std::lround(sum[k] / ((bottom-top)*(right-left))), 0L, 255L));
    }
    return result;
}
inline bool thumbnailMatches(const QByteArray& a, const QByteArray& b) {
    if (a.size() != b.size()) return false;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (std::abs(int(static_cast<unsigned char>(a[i])) - int(static_cast<unsigned char>(b[i]))) > 1) return false;
    return true; // Floating-point summation may differ by one rounding unit.
}
