/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "TextureAlpha.h"

namespace {

constexpr int Dxt1Rgb = 0x83F0;
constexpr int Dxt1Rgba = 0x83F1;
constexpr int Dxt3 = 0x83F2;
constexpr int Dxt5 = 0x83F3;
constexpr int OpaqueFrom = 250;
constexpr int TransparentTo = 5;

// Tracks the widest class seen; Partial ends the scan.
struct Classifier {
    bool transparent = false;
    bool partial = false;
    void add(int alpha) {
        if (alpha <= TransparentTo)
            transparent = true;
        else if (alpha < OpaqueFrom)
            partial = true;
    }
    TextureAlpha::Class result() const {
        return partial ? TextureAlpha::Partial
                       : transparent ? TextureAlpha::Binary : TextureAlpha::Opaque;
    }
};

quint16 word(const unsigned char *bytes) {
    return quint16(bytes[0] | (bytes[1] << 8));
}

}

namespace TextureAlpha {

Class ofPixels(const unsigned char *pixels, qsizetype texels, int components) {
    if (pixels == nullptr || texels <= 0)
        return Unknown;
    if (components != 4)
        return components == 3 || components == 1 ? Opaque : Unknown;
    Classifier classifier;
    for (qsizetype i = 0; i < texels && !classifier.partial; ++i)
        classifier.add(pixels[i * 4 + 3]);
    return classifier.result();
}

Class ofBlocks(const QByteArray &blocks, int glFormat) {
    const auto *data = reinterpret_cast<const unsigned char *>(blocks.constData());
    const qsizetype size = blocks.size();
    switch (glFormat) {
    case Dxt1Rgb:
        // Decoded without alpha: the transparent index reads as black.
        return Opaque;
    case Dxt1Rgba:
        // Three-colour blocks (colour 0 not above colour 1) make index 3
        // transparent.
        for (qsizetype b = 0; b + 8 <= size; b += 8) {
            if (word(data + b) > word(data + b + 2))
                continue;
            for (int t = 0; t < 16; ++t)
                if (((data[b + 4 + t / 4] >> ((t % 4) * 2)) & 3) == 3)
                    return Binary;
        }
        return Opaque;
    case Dxt3: {
        // Explicit 4-bit alpha: 0 and 15 only are binary.
        Classifier classifier;
        for (qsizetype b = 0; b + 16 <= size && !classifier.partial; b += 16)
            for (int t = 0; t < 16; ++t)
                classifier.add(((data[b + t / 2] >> ((t % 2) * 4)) & 15) * 17);
        return classifier.result();
    }
    case Dxt5: {
        // Two endpoints and 3-bit indices into their eight-value palette.
        Classifier classifier;
        for (qsizetype b = 0; b + 16 <= size && !classifier.partial; b += 16) {
            const int a0 = data[b], a1 = data[b + 1];
            int palette[8] = {a0, a1};
            if (a0 > a1) {
                for (int i = 1; i < 7; ++i)
                    palette[i + 1] = ((7 - i) * a0 + i * a1) / 7;
            } else {
                for (int i = 1; i < 5; ++i)
                    palette[i + 1] = ((5 - i) * a0 + i * a1) / 5;
                palette[6] = 0;
                palette[7] = 255;
            }
            quint64 indices = 0;
            for (int i = 0; i < 6; ++i)
                indices |= quint64(data[b + 2 + i]) << (8 * i);
            for (int t = 0; t < 16; ++t)
                classifier.add(palette[(indices >> (3 * t)) & 7]);
        }
        return classifier.result();
    }
    default:
        return Unknown;
    }
}

}
