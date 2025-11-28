/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/texture/DdsLib.h>
#include <QDebug>
#include <QString>
#include <QFile>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>
#include <cstdint>

bool DdsLib::IsThread = true;

DdsLib::DdsLib() {
}

#pragma pack(push,1)
struct DDS_PIXELFORMAT {
    quint32 dwSize;
    quint32 dwFlags;
    quint32 dwFourCC;
    quint32 dwRGBBitCount;
    quint32 dwRBitMask;
    quint32 dwGBitMask;
    quint32 dwBBitMask;
    quint32 dwABitMask;
};

struct DDS_HEADER {
    quint32 dwSize;
    quint32 dwFlags;
    quint32 dwHeight;
    quint32 dwWidth;
    quint32 dwPitchOrLinearSize;
    quint32 dwDepth;
    quint32 dwMipMapCount;
    quint32 dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    quint32 dwCaps;
    quint32 dwCaps2;
    quint32 dwCaps3;
    quint32 dwCaps4;
    quint32 dwReserved2;
};
#pragma pack(pop)

// Magic and flags
static const quint32 DDS_MAGIC = 0x20534444; // "DDS "

// ddspf.dwFlags
static const quint32 DDPF_ALPHAPIXELS = 0x00000001;
static const quint32 DDPF_FOURCC      = 0x00000004;
static const quint32 DDPF_RGB         = 0x00000040;

// FOURCCs
static const quint32 FOURCC_DXT1 = 0x31545844; // "DXT1"
static const quint32 FOURCC_DXT3 = 0x33545844; // "DXT3" (not handled here)
static const quint32 FOURCC_DXT5 = 0x35545844; // "DXT5"

// Helper: 16-bit 565 to RGB8
static inline void decodeRGB565(quint16 c, uint8_t &r, uint8_t &g, uint8_t &b)
{
    r = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
    g = static_cast<uint8_t>(((c >> 5)  & 0x3F) * 255 / 63);
    b = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
}

// Decompress a DXT1 block into a 4x4 RGBA tile
static void decodeDXT1Block(const uint8_t *block, uint8_t *rgba,
                            int stride /* bytes per row */, bool &hasAlpha)
{
    quint16 c0 = block[0] | (block[1] << 8);
    quint16 c1 = block[2] | (block[3] << 8);

    uint8_t r0, g0, b0;
    uint8_t r1, g1, b1;
    decodeRGB565(c0, r0, g0, b0);
    decodeRGB565(c1, r1, g1, b1);

    uint8_t colors[4][4]; // RGBA
    colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0; colors[0][3] = 255;
    colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1; colors[1][3] = 255;

    if (c0 > c1) {
        // 4-color block
        colors[2][0] = (2 * r0 + r1) / 3;
        colors[2][1] = (2 * g0 + g1) / 3;
        colors[2][2] = (2 * b0 + b1) / 3;
        colors[2][3] = 255;

        colors[3][0] = (r0 + 2 * r1) / 3;
        colors[3][1] = (g0 + 2 * g1) / 3;
        colors[3][2] = (b0 + 2 * b1) / 3;
        colors[3][3] = 255;
    } else {
        // 3-color block + 1 transparent
        colors[2][0] = (r0 + r1) / 2;
        colors[2][1] = (g0 + g1) / 2;
        colors[2][2] = (b0 + b1) / 2;
        colors[2][3] = 255;

        colors[3][0] = 0;
        colors[3][1] = 0;
        colors[3][2] = 0;
        colors[3][3] = 0;   // transparent
        hasAlpha = true;
    }

    const uint32_t code = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);

    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const int idx = (code >> (2 * (4 * j + i))) & 0x03;
            uint8_t *dst = rgba + j * stride + i * 4;
            dst[0] = colors[idx][0];
            dst[1] = colors[idx][1];
            dst[2] = colors[idx][2];
            dst[3] = colors[idx][3];
        }
    }
}

// Decompress a DXT3 block into a 4x4 RGBA tile
static void decodeDXT3Block(const uint8_t *block, uint8_t *rgba, int stride /* bytes per row */)
{
    // First 8 bytes: 4-bit alpha for 16 pixels (64 bits)
    uint64_t alphaBits = 0;
    for (int i = 0; i < 8; ++i) {
        alphaBits |= (uint64_t(block[i]) << (8 * i));
    }

    // Next 8 bytes: color data like DXT1, but always 4-color mode (no implicit transparency)
    const uint8_t *colorBlock = block + 8;

    quint16 c0 = colorBlock[0] | (colorBlock[1] << 8);
    quint16 c1 = colorBlock[2] | (colorBlock[3] << 8);

    uint8_t r0, g0, b0;
    uint8_t r1, g1, b1;
    decodeRGB565(c0, r0, g0, b0);
    decodeRGB565(c1, r1, g1, b1);

    uint8_t colors[4][3]; // RGB
    colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0;
    colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1;

    // DXT3 always treats this as a 4-color block (no transparent color)
    colors[2][0] = (2 * r0 + r1) / 3;
    colors[2][1] = (2 * g0 + g1) / 3;
    colors[2][2] = (2 * b0 + b1) / 3;

    colors[3][0] = (r0 + 2 * r1) / 3;
    colors[3][1] = (g0 + 2 * g1) / 3;
    colors[3][2] = (b0 + 2 * b1) / 3;

    const uint32_t code = colorBlock[4] | (colorBlock[5] << 8) |
                          (colorBlock[6] << 16) | (colorBlock[7] << 24);

    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const int pixelIndex = 4 * j + i;

            // 4-bit alpha for each pixel
            const uint8_t alpha4 = (alphaBits >> (4 * pixelIndex)) & 0x0F;
            const uint8_t a = alpha4 * 17; // scale 0..15 to 0..255

            const int colorIndex = (code >> (2 * pixelIndex)) & 0x03;

            uint8_t *dst = rgba + j * stride + i * 4;
            dst[0] = colors[colorIndex][0];
            dst[1] = colors[colorIndex][1];
            dst[2] = colors[colorIndex][2];
            dst[3] = a;
        }
    }
}

// Decompress a DXT5 block into a 4x4 RGBA tile
static void decodeDXT5Block(const uint8_t *block, uint8_t *rgba, int stride)
{
    // Alpha
    const uint8_t alpha0 = block[0];
    const uint8_t alpha1 = block[1];

    uint8_t alphaTable[8];
    alphaTable[0] = alpha0;
    alphaTable[1] = alpha1;
    if (alpha0 > alpha1) {
        // 6 interpolated alpha values.
        alphaTable[2] = (6 * alpha0 + 1 * alpha1) / 7;
        alphaTable[3] = (5 * alpha0 + 2 * alpha1) / 7;
        alphaTable[4] = (4 * alpha0 + 3 * alpha1) / 7;
        alphaTable[5] = (3 * alpha0 + 4 * alpha1) / 7;
        alphaTable[6] = (2 * alpha0 + 5 * alpha1) / 7;
        alphaTable[7] = (1 * alpha0 + 6 * alpha1) / 7;
    } else {
        // 4 interpolated alpha values, then 0 and 255.
        alphaTable[2] = (4 * alpha0 + 1 * alpha1) / 5;
        alphaTable[3] = (3 * alpha0 + 2 * alpha1) / 5;
        alphaTable[4] = (2 * alpha0 + 3 * alpha1) / 5;
        alphaTable[5] = (1 * alpha0 + 4 * alpha1) / 5;
        alphaTable[6] = 0;
        alphaTable[7] = 255;
    }

    // 48 bits of alpha indices
    uint64_t alphaBits = 0;
    for (int i = 0; i < 6; ++i) {
        alphaBits |= (uint64_t(block[2 + i]) << (8 * i));
    }

    // Color data (DXT1-like) in block[8..15]
    const uint8_t *colorBlock = block + 8;

    quint16 c0 = colorBlock[0] | (colorBlock[1] << 8);
    quint16 c1 = colorBlock[2] | (colorBlock[3] << 8);

    uint8_t r0, g0, b0;
    uint8_t r1, g1, b1;
    decodeRGB565(c0, r0, g0, b0);
    decodeRGB565(c1, r1, g1, b1);

    uint8_t colors[4][3]; // RGB only
    colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0;
    colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1;

    colors[2][0] = (2 * r0 + r1) / 3;
    colors[2][1] = (2 * g0 + g1) / 3;
    colors[2][2] = (2 * b0 + b1) / 3;

    colors[3][0] = (r0 + 2 * r1) / 3;
    colors[3][1] = (g0 + 2 * g1) / 3;
    colors[3][2] = (b0 + 2 * b1) / 3;

    const uint32_t code = colorBlock[4] | (colorBlock[5] << 8) |
                          (colorBlock[6] << 16) | (colorBlock[7] << 24);

    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const int pixelIndex = 4 * j + i;

            // color index
            const int colorIndex = (code >> (2 * pixelIndex)) & 0x03;

            // alpha index (3 bits per pixel)
            const int alphaIndex = (alphaBits >> (3 * pixelIndex)) & 0x07;

            uint8_t *dst = rgba + j * stride + i * 4;
            dst[0] = colors[colorIndex][0];
            dst[1] = colors[colorIndex][1];
            dst[2] = colors[colorIndex][2];
            dst[3] = alphaTable[alphaIndex];
        }
    }
}

void DdsLib::run()
{
    QFile file(texture->pathid);
    if (!file.open(QIODevice::ReadOnly)) {
        texture->missing = true;
        if (!IsThread) {
            qDebug() << "DDS: cannot open" << texture->pathid;
        }
        return;
    }

    // Magic
    quint32 magic = 0;
    if (file.read(reinterpret_cast<char*>(&magic), sizeof(magic)) != sizeof(magic) || magic != DDS_MAGIC) {
        texture->missing = true;
        if (!IsThread) {
            qDebug() << "DDS: not a DDS file" << texture->pathid;
        }
        return;
    }

    // Header
    DDS_HEADER header;
    if (file.read(reinterpret_cast<char*>(&header), sizeof(DDS_HEADER)) != sizeof(DDS_HEADER)) {
        texture->missing = true;
        if (!IsThread) {
            qDebug() << "DDS: header too short" << texture->pathid;
        }
        return;
    }

    if (header.dwSize != sizeof(DDS_HEADER) || header.ddspf.dwSize != sizeof(DDS_PIXELFORMAT)) {
        texture->missing = true;
        if (!IsThread) {
            qDebug() << "DDS: invalid header sizes" << texture->pathid;
        }
        return;
    }

    const quint32 width  = header.dwWidth;
    const quint32 height = header.dwHeight;
    if (width == 0 || height == 0) {
        texture->missing = true;
        if (!IsThread) {
            qDebug() << "DDS: invalid dimensions"
                     << "w:" << width << "h:" << height
                     << texture->pathid;
        }
        return;
    }

    const DDS_PIXELFORMAT &pf = header.ddspf;

    // Read all remaining data
    const qint64 dataOffset = file.pos();
    const qint64 fileSize   = file.size();
    const qint64 dataSize   = fileSize - dataOffset;
    if (dataSize <= 0) {
        texture->missing = true;
        if (!IsThread) {
            qDebug() << "DDS: no pixel data" << texture->pathid;
        }
        return;
    }

    QByteArray data = file.readAll();
    if (data.size() != dataSize) {
        texture->missing = true;
        if (!IsThread) {
            qDebug() << "DDS: failed to read pixel data" << texture->pathid;
        }
        return;
    }
    const uint8_t *src = reinterpret_cast<const uint8_t*>(data.constData());

    // Check if it's compressed (FOURCC) or uncompressed (RGB)
    if (pf.dwFlags & DDPF_FOURCC) {
        // --- COMPRESSED PATH (DXT1 / DXT5) ---
        const quint32 fourCC = pf.dwFourCC;

        const int blocksWide  = (width  + 3) / 4;
        const int blocksHigh  = (height + 3) / 4;

        bool hasAlphaDXT1 = false;
        bool hasAlpha = false;

        // We'll decompress to RGBA8, then optionally drop alpha for pure-RGB DXT1.
        const int outBpp = 4;
        const size_t outSize = size_t(width) * size_t(height) * outBpp;
        if(texture->imageData != nullptr)
            delete[] texture->imageData;
        texture->imageData = new unsigned char[outSize];

        unsigned char *dst = texture->imageData;

        if (fourCC == FOURCC_DXT1) {
            // 8 bytes per block
            const int blockSize = 8;
            const uint8_t *blockPtr = src;

            for (int by = 0; by < blocksHigh; ++by) {
                for (int bx = 0; bx < blocksWide; ++bx) {
                    uint8_t tile[4 * 4 * 4]; // 4x4 RGBA
                    bool tileHasAlpha = false;
                    decodeDXT1Block(blockPtr, tile, 4 * 4, tileHasAlpha);
                    if (tileHasAlpha) hasAlphaDXT1 = true;

                    const int x0 = bx * 4;
                    const int y0 = by * 4;
                    for (int j = 0; j < 4; ++j) {
                        const int y = y0 + j;
                        if (y >= int(height)) break;

                        for (int i = 0; i < 4; ++i) {
                            const int x = x0 + i;
                            if (x >= int(width)) break;

                            const uint8_t *srcPixel = &tile[(j * 4 + i) * 4];
                            uint8_t *dstPixel = dst + (y * width + x) * 4;

                            dstPixel[0] = srcPixel[0];
                            dstPixel[1] = srcPixel[1];
                            dstPixel[2] = srcPixel[2];
                            dstPixel[3] = srcPixel[3];
                        }
                    }

                    blockPtr += blockSize;
                }
            }

            hasAlpha = hasAlphaDXT1 || (pf.dwFlags & DDPF_ALPHAPIXELS);

            // If no alpha, convert RGBA -> RGB tightly packed
            if (!hasAlpha) {
                unsigned char *rgbData = new unsigned char[size_t(width) * size_t(height) * 3];
                for (quint32 y = 0; y < height; ++y) {
                    for (quint32 x = 0; x < width; ++x) {
                        const unsigned char *srcPixel = dst + (y * width + x) * 4;
                        unsigned char *rgbPixel = rgbData + (y * width + x) * 3;
                        rgbPixel[0] = srcPixel[0];
                        rgbPixel[1] = srcPixel[1];
                        rgbPixel[2] = srcPixel[2];
                    }
                }
                delete[] texture->imageData;
                texture->imageData = rgbData;
                texture->bytesPerPixel = 3;
                texture->type = GL_RGB;
            } else {
                texture->bytesPerPixel = 4;
                texture->type = GL_RGBA;
            }

        } else if (fourCC == FOURCC_DXT3) {
            // ---- NEW DXT3 BRANCH ----
            const int blockSize = 16;
            const uint8_t *blockPtr = src;

            for (int by = 0; by < blocksHigh; ++by) {
                for (int bx = 0; bx < blocksWide; ++bx) {
                    uint8_t tile[4 * 4 * 4]; // 4x4 RGBA
                    decodeDXT3Block(blockPtr, tile, 4 * 4);

                    const int x0 = bx * 4;
                    const int y0 = by * 4;
                    for (int j = 0; j < 4; ++j) {
                        const int y = y0 + j;
                        if (y >= int(height)) break;

                        for (int i = 0; i < 4; ++i) {
                            const int x = x0 + i;
                            if (x >= int(width)) break;

                            const uint8_t *srcPixel = &tile[(j * 4 + i) * 4];
                            uint8_t *dstPixel = dst + (y * width + x) * 4;

                            dstPixel[0] = srcPixel[0];
                            dstPixel[1] = srcPixel[1];
                            dstPixel[2] = srcPixel[2];
                            dstPixel[3] = srcPixel[3];
                        }
                    }

                    blockPtr += blockSize;
                }
            }

            // DXT3 always has 4-bit alpha, so keep RGBA
            texture->bytesPerPixel = 4;
            texture->type = GL_RGBA;

        } else if (fourCC == FOURCC_DXT5) {
            // 16 bytes per block
            const int blockSize = 16;
            const uint8_t *blockPtr = src;

            for (int by = 0; by < blocksHigh; ++by) {
                for (int bx = 0; bx < blocksWide; ++bx) {
                    uint8_t tile[4 * 4 * 4]; // 4x4 RGBA
                    decodeDXT5Block(blockPtr, tile, 4 * 4);

                    const int x0 = bx * 4;
                    const int y0 = by * 4;
                    for (int j = 0; j < 4; ++j) {
                        const int y = y0 + j;
                        if (y >= int(height)) break;

                        for (int i = 0; i < 4; ++i) {
                            const int x = x0 + i;
                            if (x >= int(width)) break;

                            const uint8_t *srcPixel = &tile[(j * 4 + i) * 4];
                            uint8_t *dstPixel = dst + (y * width + x) * 4;

                            dstPixel[0] = srcPixel[0];
                            dstPixel[1] = srcPixel[1];
                            dstPixel[2] = srcPixel[2];
                            dstPixel[3] = srcPixel[3];
                        }
                    }

                    blockPtr += blockSize;
                }
            }

            hasAlpha = true; // DXT5 always has alpha
            texture->bytesPerPixel = 4;
            texture->type = GL_RGBA;
        } else {
            if (!IsThread) {
                qDebug() << "DDS: unsupported FOURCC"
                         << QString::number(fourCC, 16)
                         << "in" << texture->pathid;
            }
            delete[] texture->imageData;
            texture->imageData = nullptr;
            texture->missing = true;
            return;
        }

        texture->width  = int(width);
        texture->height = int(height);

        if (!IsThread) {
            qDebug() << "DDS tex (compressed):" << texture->pathid
                     << "w:" << texture->width
                     << "h:" << texture->height
                     << "bpp:" << texture->bytesPerPixel;
        }

        texture->loaded = true;
        texture->editable = true;
        return;

    } else if (pf.dwFlags & DDPF_RGB) {
        // --- UNCOMPRESSED PATH (like previous version) ---
        int bytesPerPixel = 0;
        bool hasAlphaUncompressed = (pf.dwFlags & DDPF_ALPHAPIXELS);

        if (pf.dwRGBBitCount == 32) {
            bytesPerPixel = 4;
        } else if (pf.dwRGBBitCount == 24) {
            bytesPerPixel = 3;
        } else {
            if (!IsThread) {
                qDebug() << "DDS: unsupported bit count:" << pf.dwRGBBitCount
                         << "in" << texture->pathid;
            }
            texture->missing = true;
            return;
        }

        const quint32 expectedTightPitch = width * bytesPerPixel;
        quint32 srcRowPitch = header.dwPitchOrLinearSize ?
                              header.dwPitchOrLinearSize : expectedTightPitch;

        if (srcRowPitch < expectedTightPitch ||
            static_cast<quint64>(srcRowPitch) * height > static_cast<quint64>(data.size())) {
            srcRowPitch = expectedTightPitch;
        }
        if(texture->imageData != nullptr)
            delete[] texture->imageData;
        texture->imageData = new unsigned char[size_t(width) * size_t(height) * bytesPerPixel];

        unsigned char *dst = texture->imageData;

        // Check channel masks for 32bpp
        bool srcIsRGBA = false;
        bool srcIsBGRA = false;

        if (bytesPerPixel == 4) {
            if (pf.dwRBitMask == 0x000000ff &&
                pf.dwGBitMask == 0x0000ff00 &&
                pf.dwBBitMask == 0x00ff0000) {
                srcIsRGBA = true;
            } else if (pf.dwRBitMask == 0x00ff0000 &&
                       pf.dwGBitMask == 0x0000ff00 &&
                       pf.dwBBitMask == 0x000000ff) {
                srcIsBGRA = true;
            } else {
                if (!IsThread) {
                    qDebug() << "DDS: unsupported channel masks R/G/B:"
                             << QString::number(pf.dwRBitMask, 16)
                             << QString::number(pf.dwGBitMask, 16)
                             << QString::number(pf.dwBBitMask, 16)
                             << "in" << texture->pathid;
                }
                texture->missing = true;
                delete[] texture->imageData;
                texture->imageData = nullptr;
                return;
            }
        }

        for (quint32 y = 0; y < height; ++y) {
            const uint8_t *srcRow = src + y * srcRowPitch;
            unsigned char *dstRow = dst + y * expectedTightPitch;

            if (bytesPerPixel == 3) {
                // Assume BGR -> RGB
                for (quint32 x = 0; x < width; ++x) {
                    uint8_t b = srcRow[x * 3 + 0];
                    uint8_t g = srcRow[x * 3 + 1];
                    uint8_t r = srcRow[x * 3 + 2];

                    dstRow[x * 3 + 0] = r;
                    dstRow[x * 3 + 1] = g;
                    dstRow[x * 3 + 2] = b;
                }
            } else {
                for (quint32 x = 0; x < width; ++x) {
                    const uint8_t c0 = srcRow[x * 4 + 0];
                    const uint8_t c1 = srcRow[x * 4 + 1];
                    const uint8_t c2 = srcRow[x * 4 + 2];
                    const uint8_t a  = srcRow[x * 4 + 3];

                    uint8_t r, g, b;
                    if (srcIsRGBA) {
                        r = c0; g = c1; b = c2;
                    } else { // BGRA
                        r = c2; g = c1; b = c0;
                    }

                    dstRow[x * 4 + 0] = r;
                    dstRow[x * 4 + 1] = g;
                    dstRow[x * 4 + 2] = b;
                    dstRow[x * 4 + 3] = a;
                }
            }
        }

        texture->width  = int(width);
        texture->height = int(height);
        texture->bytesPerPixel = bytesPerPixel;
        texture->type = (bytesPerPixel == 4) ? GL_RGBA : GL_RGB;

        if (!IsThread) {
            qDebug() << "DDS tex (uncompressed):" << texture->pathid
                     << "w:" << texture->width
                     << "h:" << texture->height
                     << "bpp:" << texture->bytesPerPixel;
        }

        texture->loaded = true;
        texture->editable = true;
        return;
    } else {
        texture->missing = true;
        if (!IsThread) {
            qDebug() << "DDS: unsupported pixel format flags"
                     << pf.dwFlags << "in" << texture->pathid;
        }
        return;
    }
}