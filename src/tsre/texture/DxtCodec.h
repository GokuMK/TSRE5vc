// Shared CPU BC1/BC2/BC3 codec. No OpenGL context or file container required.
#ifndef TSRE_DXT_CODEC_H
#define TSRE_DXT_CODEC_H

#include <QByteArray>
#include <QString>

namespace DxtCodec {
enum class Format { Dxt1, Dxt2, Dxt3, Dxt4, Dxt5 };
int blockBytes(Format format);
qint64 byteSize(int width, int height, Format format);
bool decode(const QByteArray &blocks, int width, int height, Format format, bool alpha,
            QByteArray &pixels, QString &error);
// The output must not alias blocks and must have exactly width*height*(alpha?4:3) bytes.
bool decodeInto(const QByteArray &blocks, int width, int height, Format format, bool alpha,
                unsigned char *pixels, qsizetype size, QString &error);
// Portable, deterministic fast endpoint-fit encoder; not an offline quality optimizer.
bool encode(const unsigned char *pixels, qsizetype size, int width, int height, int components,
            Format format, bool alpha, QByteArray &blocks, QString &error);
} // namespace DxtCodec
#endif
