#include "AceDxt3Diagnostic.h"
#include <tsre/texture/DxtCodec.h>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QElapsedTimer>
#include <QtEndian>
#include <QDebug>
#include <algorithm>
#include <array>
#include <memory>
#include <numeric>
#include <cstdio>
#include <cstring>

int runReadbackBenchmark() {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    if (!context.create()) return 2;
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!context.makeCurrent(&surface)) return 2;
    QOpenGLFunctions_3_3_Core gl;
    if (!gl.initializeOpenGLFunctions()) return 2;
    printf("GPU: %s\n", gl.glGetString(GL_RENDERER));
    printf("31 measured repetitions, 3 warmups; allocation included; idle resident GPU texture\n");
    printf("format,side,method,mean_ms,median_ms,p95_ms\n");
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    for (bool alpha : {false, true}) for (int side : {256, 512, 1024, 2048}) {
        const auto codec = alpha ? DxtCodec::Format::Dxt3 : DxtCodec::Format::Dxt1;
        const GLenum internal = alpha ? 0x83f2 : 0x83f0;
        const int stride = alpha ? 16 : 8;
        const int pixelBytes = side * side * (alpha ? 4 : 3);
        QByteArray encoded(DxtCodec::byteSize(side, side, codec), '\0');
        for (int n = 0; n < encoded.size() / stride; ++n) {
            auto *block = reinterpret_cast<unsigned char *>(encoded.data()) + n * stride;
            if (alpha) for (int i = 0; i < 8; ++i) block[i] = (i * 2) | ((i * 2 + 1) << 4);
            auto *color = block + (alpha ? 8 : 0);
            qToLittleEndian<quint16>(0xf800 | (n & 31), color);
            qToLittleEndian<quint16>(0x07e0 | ((n >> 5) & 31), color + 2);
            qToLittleEndian<quint32>(0xe4e4e4e4u, color + 4);
        }
        GLuint texture = 0;
        gl.glGenTextures(1, &texture);
        gl.glBindTexture(GL_TEXTURE_2D, texture);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.glCompressedTexImage2D(GL_TEXTURE_2D, 0, internal, side, side, 0,
                                  encoded.size(), encoded.constData());
        gl.glFinish();
        if (gl.glGetError() != GL_NO_ERROR) return 3;
        QByteArray readback(encoded.size(), Qt::Uninitialized), reference;
        gl.glGetCompressedTexImage(GL_TEXTURE_2D, 0, readback.data());
        QString error;
        if (readback != encoded || !DxtCodec::decode(encoded, side, side, codec, alpha, reference, error)) return 4;
        std::array<QVector<double>, 3> timings;
        unsigned checksum = 0;
        for (int round = -3; round < 31; ++round) for (int order = 0; order < 3; ++order) {
            const int method = (round + 3 + order) % 3;
            gl.glFinish(); // Exclude unrelated pending rendering from every path equally.
            QElapsedTimer timer;
            timer.start();
            std::unique_ptr<unsigned char[]> pixels(new unsigned char[pixelBytes]);
            if (method == 0) {
                gl.glBindTexture(GL_TEXTURE_2D, texture);
                gl.glGetTexImage(GL_TEXTURE_2D, 0, alpha ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, pixels.get());
            } else if (method == 1) {
                gl.glBindTexture(GL_TEXTURE_2D, texture);
                GLint size = 0;
                gl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &size);
                if (size != encoded.size()) return 5;
                QByteArray blocks(size, Qt::Uninitialized);
                gl.glGetCompressedTexImage(GL_TEXTURE_2D, 0, blocks.data());
                if (!DxtCodec::decodeInto(blocks, side, side, codec, alpha, pixels.get(), pixelBytes, error)) return 6;
            } else {
                if (!DxtCodec::decodeInto(encoded, side, side, codec, alpha, pixels.get(), pixelBytes, error)) return 6;
            }
            const double elapsed = timer.nsecsElapsed() / 1e6;
            if (round >= 0) timings[method].push_back(elapsed);
            if (gl.glGetError() != GL_NO_ERROR) return 7;
            if (method != 0 && memcmp(pixels.get(), reference.constData(), pixelBytes) != 0) return 8;
            checksum += pixels[(round + 3) % pixelBytes];
        }
        const char *names[] = {"direct_readback", "compressed_readback_cpu_decode", "cpu_decode_only"};
        for (int method = 0; method < 3; ++method) {
            auto values = timings[method];
            std::sort(values.begin(), values.end());
            const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
            printf("%s,%d,%s,%.6f,%.6f,%.6f\n", alpha ? "DXT3_RGBA" : "DXT1_RGB", side,
                   names[method], mean, values[values.size()/2], values[(values.size()*95)/100]);
        }
        if (!checksum) return 9;
        gl.glDeleteTextures(1, &texture);
        fflush(stdout);
    }
    return 0;
}
