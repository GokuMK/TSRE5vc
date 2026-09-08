#include "AceLib.h"
#include "Texture.h"
#include <tsre/Game.h>
#include <QDebug>
#include <QFileInfo>
#include <QImage>
#include <algorithm>
#include <cstring>
#include <memory>

bool AceLib::IsThread = true;

namespace {
int compressedFormat(const AceDocument &doc, const AceLevel &level) {
    if (!level.raw)
        return 0;
    switch (doc.surface()) {
    case 18:
        return doc.hasAlpha() ? 0x83f1 : 0x83f0;
    case 20:
        return 0x83f2;
    case 22:
        return 0x83f3;
    default:
        return 0; // DXT2/4 normalized to straight alpha on CPU.
    }
}
} // namespace

bool AceLib::load(const QString &path, Texture &target, const AceLoadOptions &o, QString &error) {
    AceDocument doc;
    auto reader = o.reader;
    if (!o.preserveDocument)
        reader.retainOriginal = false;
    if (!AceDocument::read(path, doc, error, reader))
        return false;
    const int divisor = std::max(1, o.quality);
    const int width = std::max(1, doc.levels[0].width / divisor),
              height = std::max(1, doc.levels[0].height / divisor);
    int first = 0;
    while (first + 1 < doc.levels.size() && doc.levels[first + 1].width >= width &&
           doc.levels[first + 1].height >= height)
        ++first;
    Texture incoming;
    incoming.width = width;
    incoming.height = height;
    incoming.bytesPerPixel = doc.hasAlpha() ? 4 : 3;
    incoming.bpp = incoming.bytesPerPixel * 8;
    incoming.type = incoming.bytesPerPixel == 4 ? 0x1908 : 0x1907; // GL_RGBA / GL_RGB; no GL calls.
    incoming.imageSize = width * height * incoming.bytesPerPixel;
    incoming.compressed = doc.surface();
    incoming.typk = doc.hasAlpha() ? 1 : 0;
    QByteArray pixels;
    std::unique_ptr<unsigned char[]> base;
    int components = incoming.bytesPerPixel;
    const bool resample = doc.levels[first].width != width || doc.levels[first].height != height;
    incoming.compressedGLFormat = compressedFormat(doc, doc.levels[first]);
    if (incoming.compressedGLFormat && !o.cpuPixels && !resample)
        incoming.compressedData = doc.levels[first].data;
    else {
        incoming.compressedGLFormat = 0;
        base.reset(new unsigned char[incoming.imageSize]);
        if (resample) {
            if (!doc.decode(first, pixels, components, error))
                return false;
            const int sw = doc.levels[first].width, sh = doc.levels[first].height;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    memcpy(base.get() + (qsizetype(y) * width + x) * components,
                           pixels.constData() +
                               (qsizetype(qint64(y) * sh / height) * sw + qint64(x) * sw / width) *
                                   components,
                           components);
        } else if (!doc.decodeInto(first, base.get(), incoming.imageSize, components, error))
            return false;
    }
    if (o.stageMipmaps && !resample)
        for (int m = first + 1; m < doc.levels.size(); ++m) {
            TextureMip mip;
            mip.width = doc.levels[m].width;
            mip.height = doc.levels[m].height;
            mip.compressedFormat = compressedFormat(doc, doc.levels[m]);
            if (mip.compressedFormat && !o.cpuPixels)
                mip.data = doc.levels[m].data;
            else {
                mip.compressedFormat = 0;
                if (!doc.decode(m, mip.data, components, error))
                    return false;
            }
            incoming.sourceMipmaps.push_back(std::move(mip));
        }
    incoming.aceMetadata = std::make_shared<AceMetadata>(doc.metadata);
    if (!o.preserveDocument) {
        incoming.aceMetadata->trailing.clear();
        for (auto &palette : incoming.aceMetadata->palettes)
            palette.data.clear();
    } else
        incoming.aceDocument = std::make_shared<AceDocument>(std::move(doc));
    if (base) {
        incoming.imageData = base.release();
        incoming.editable = true;
    }
    incoming.loaded = true;
    target.takeContentFrom(incoming);
    error.clear();
    return true;
}

void AceLib::run() {
    if (!texture)
        return;
    AceLoadOptions options;
    options.quality = Game::textureQuality;
    QString error;
    if (!load(texture->pathid, *texture, options, error)) {
        texture->error = true;
        texture->missing = !QFileInfo::exists(texture->pathid);
        texture->errorMessage = error;
        texture->loaded = false;
        qWarning().noquote() << "ACE:" << texture->pathid << error;
    }
}

bool AceLib::save(QString path, Texture *texture) {
    AceWriteOptions options;
    options.encoding =
        texture && texture->bytesPerPixel == 4 ? AceEncoding::Rgba : AceEncoding::Rgb;
    QString error;
    const bool ok = save(path, texture, options, error);
    if (!ok)
        qWarning().noquote() << "ACE save:" << path << error;
    return ok;
}

bool AceLib::save(QString path, Texture *texture, const AceWriteOptions &options, QString &error) {
    if (!texture || !texture->loaded) {
        error = "No loaded texture to save";
        return false;
    }
    if (!texture->decodeToCpu())
        texture->setEditable();
    if (!texture->imageData) {
        error = "No CPU pixels and no usable OpenGL readback context";
        return false;
    }
    AceWriteOptions effective = options;
    if (effective.headerTemplate.isEmpty() && texture->aceMetadata)
        effective.headerTemplate = texture->aceMetadata->header;
    AceDocument doc;
    if (!AceDocument::fromPixels(
            texture->imageData,
            qsizetype(texture->width) * texture->height * texture->bytesPerPixel, texture->width,
            texture->height, texture->bytesPerPixel, effective, doc, error))
        return false;
    return doc.write(path, effective.zlib, error);
}

bool AceLib::save(const QString &path, const QImage &image, const AceWriteOptions &options,
                  QString &error) {
    if (image.isNull() || image.width() > 16384 || image.height() > 16384 ||
        qint64(image.width()) * image.height() > 64 * 1024 * 1024) {
        error = "Invalid ACE image dimensions";
        return false;
    }
    const bool opaque =
        options.encoding == AceEncoding::Rgb || options.encoding == AceEncoding::Rgb565 ||
        options.encoding == AceEncoding::Dxt1 || options.encoding == AceEncoding::IndexedRgb;
    const int components = !opaque && image.hasAlphaChannel() ? 4 : 3;
    const QImage converted =
        image.convertToFormat(components == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    const qsizetype row = qsizetype(image.width()) * components;
    QByteArray tight;
    const unsigned char *pixels = converted.constBits();
    if (converted.bytesPerLine() != row) {
        tight.resize(row * image.height());
        for (int y = 0; y < image.height(); ++y)
            memcpy(tight.data() + y * row, converted.constScanLine(y), row);
        pixels = reinterpret_cast<const unsigned char *>(tight.constData());
    }
    AceDocument doc;
    if (!AceDocument::fromPixels(pixels, row * image.height(), image.width(), image.height(),
                                 components, options, doc, error))
        return false;
    return doc.write(path, options.zlib, error);
}
