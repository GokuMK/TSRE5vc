#include "AceConverter.h"
#include <tsre/texture/DdsLib.h>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <cstring>

namespace AceConverter {
const QVector<Encoding> &encodings() {
    static const QVector<Encoding> values = {
        {"rgb", "RGB (24-bit, no alpha)", AceEncoding::Rgb},
        {"rgba", "RGBA (8-bit alpha)", AceEncoding::Rgba},
        {"mask", "RGB + 1-bit mask", AceEncoding::Mask},
        {"dxt1", "DXT1 (opaque)", AceEncoding::Dxt1},
        {"dxt1mask", "DXT1 (1-bit alpha)", AceEncoding::Dxt1Mask},
        {"dxt3", "DXT3 (4-bit alpha)", AceEncoding::Dxt3},
        {"dxt5", "DXT5 (interpolated alpha)", AceEncoding::Dxt5},
        {"rgb565", "RGB565 (16-bit, no alpha)", AceEncoding::Rgb565},
        {"argb1555", "ARGB1555 (1-bit alpha)", AceEncoding::Argb1555},
        {"argb4444", "ARGB4444 (4-bit alpha)", AceEncoding::Argb4444},
        {"dxt2", "DXT2 (premultiplied alpha)", AceEncoding::Dxt2},
        {"dxt4", "DXT4 (premultiplied alpha)", AceEncoding::Dxt4},
        {"indexed-rgb", "Indexed RGB (up to 256 colors)", AceEncoding::IndexedRgb},
        {"indexed-rgba", "Indexed RGBA (up to 256 colors)", AceEncoding::IndexedRgba}
    };
    return values;
}
bool parseEncoding(const QString &name, AceEncoding &encoding) {
    for (const auto &entry : encodings()) {
        if (name.compare(QLatin1String(entry.key), Qt::CaseInsensitive) == 0) {
            encoding = entry.value;
            return true;
        }
    }
    return false;
}
AlphaKind alphaKind(AceEncoding encoding) {
    switch (encoding) {
    case AceEncoding::Rgb: case AceEncoding::Rgb565:
    case AceEncoding::Dxt1: case AceEncoding::IndexedRgb:
        return AlphaKind::Opaque;
    case AceEncoding::Mask: case AceEncoding::Dxt1Mask: case AceEncoding::Argb1555:
        return AlphaKind::Binary;
    default:
        return AlphaKind::Full;
    }
}
bool recommendedForSimulators(AceEncoding encoding) {
    // Conservative everyday OR/MSTS choices. Advanced packed/indexed formats
    // remain available with the filter off; DXT2/4 and indexed RGBA have native
    // alpha qualifications documented in docs/features/ace-library.md.
    switch (encoding) {
    case AceEncoding::Rgb: case AceEncoding::Rgba: case AceEncoding::Mask:
    case AceEncoding::Dxt1: case AceEncoding::Dxt1Mask:
    case AceEncoding::Dxt3: case AceEncoding::Dxt5:
        return true;
    default:
        return false;
    }
}
bool matchesSource(AceEncoding encoding, const Image &image) {
    const AlphaKind target = alphaKind(encoding);
    if (image.alpha == AlphaKind::Binary)
        return target != AlphaKind::Opaque;
    return target == image.alpha;
}
AceEncoding suggestedEncoding(const Image &image, bool simulatorFormatsOnly) {
    if (image.sourceEncoding &&
        (!simulatorFormatsOnly || recommendedForSimulators(*image.sourceEncoding)))
        return *image.sourceEncoding;
    if (image.sourceEncoding == AceEncoding::Dxt2) return AceEncoding::Dxt3;
    if (image.sourceEncoding == AceEncoding::Dxt4) return AceEncoding::Dxt5;
    if (image.alpha == AlphaKind::Full) return AceEncoding::Rgba;
    if (image.alpha == AlphaKind::Binary) return AceEncoding::Mask;
    return AceEncoding::Rgb;
}
bool canGenerateMips(const QImage &image) {
    const int width = image.width();
    return width > 0 && width == image.height() && !(width & (width - 1));
}
static QString aceStorage(const AceDocument &doc) {
    QString encoding;
    switch (doc.surface()) {
    case 4: case 12: encoding = "Indexed / palette"; break;
    case 14: encoding = (doc.options() & 16) ? "RGB565" : "RGB"; break;
    case 16: encoding = (doc.options() & 16) ? "ARGB1555" : "RGB + mask"; break;
    case 17: encoding = (doc.options() & 16) ? "ARGB4444" : "RGBA + mask"; break;
    case 18: encoding = "DXT1"; break;
    case 19: encoding = "DXT2"; break;
    case 20: encoding = "DXT3"; break;
    case 21: encoding = "DXT4"; break;
    case 22: encoding = "DXT5"; break;
    default: encoding = QString("Surface %1").arg(doc.surface()); break;
    }
    return encoding;
}
static std::optional<AceEncoding> sourceAceEncoding(const AceDocument &doc) {
    const bool raw = doc.options() & 16;
    switch (doc.surface()) {
    case 4: return AceEncoding::IndexedRgb;
    case 12: return AceEncoding::IndexedRgba;
    case 14: return raw ? AceEncoding::Rgb565 : AceEncoding::Rgb;
    case 16: return raw ? AceEncoding::Argb1555 : AceEncoding::Mask;
    case 17: return raw ? AceEncoding::Argb4444 : AceEncoding::Rgba;
    case 18: return doc.hasAlpha() ? AceEncoding::Dxt1Mask : AceEncoding::Dxt1;
    case 19: return AceEncoding::Dxt2;
    case 20: return AceEncoding::Dxt3;
    case 21: return AceEncoding::Dxt4;
    case 22: return AceEncoding::Dxt5;
    default: return {};
    }
}
bool load(const QString &path, Image &out, QString &error) {
    Image result;
    result.path = QFileInfo(path).absoluteFilePath();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "ace") {
        AceDocument doc;
        if (!AceDocument::read(path, doc, error)) return false;
        QByteArray pixels;
        int components = 0;
        if (!doc.decode(0, pixels, components, error, &result.mask)) return false;
        const auto &base = doc.levels.first();
        result.pixels = QImage(reinterpret_cast<const uchar *>(pixels.constData()),
                               base.width, base.height, base.width * components,
                               components == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888)
                                .convertToFormat(QImage::Format_RGBA8888).copy();
        result.format = "ACE";
        result.storage = aceStorage(doc);
        result.zlibEnvelope = doc.compressedEnvelope;
        result.sourceEncoding = sourceAceEncoding(doc);
        result.alpha = result.sourceEncoding ? alphaKind(*result.sourceEncoding)
                                            : (doc.hasAlpha() ? AlphaKind::Full : AlphaKind::Opaque);
        if (doc.hasAlpha() && result.alpha == AlphaKind::Opaque) {
            result.alpha = AlphaKind::Binary;
            for (const auto &channel : doc.metadata.channels)
                if (channel.id == 6) result.alpha = AlphaKind::Full;
            for (const auto &palette : doc.metadata.palettes)
                if (palette.type == 8) result.alpha = AlphaKind::Full;
            result.sourceEncoding.reset();
        }
        result.mipCount = int(doc.levels.size());
        result.warnings = doc.warnings;
    } else if (suffix == "dds") {
        DdsImageInfo info;
        if (!DdsLib::loadImage(path, result.pixels, info, error)) return false;
        result.format = "DDS";
        result.storage = info.encoding;
        result.mipCount = info.mipCount;
        result.alpha = info.alphaBits > 1 ? AlphaKind::Full
                                         : (info.alphaBits ? AlphaKind::Binary : AlphaKind::Opaque);
        switch (info.fourCC) {
        case 0x31545844: result.sourceEncoding = info.alphaBits ? AceEncoding::Dxt1Mask : AceEncoding::Dxt1; break;
        case 0x32545844: result.sourceEncoding = AceEncoding::Dxt2; break;
        case 0x33545844: result.sourceEncoding = AceEncoding::Dxt3; break;
        case 0x34545844: result.sourceEncoding = AceEncoding::Dxt4; break;
        case 0x35545844: result.sourceEncoding = AceEncoding::Dxt5; break;
        default:
            if (info.redBits == 5 && info.greenBits == 6 && info.blueBits == 5 && info.alphaBits == 0)
                result.sourceEncoding = AceEncoding::Rgb565;
            else if (info.redBits == 5 && info.greenBits == 5 && info.blueBits == 5 && info.alphaBits == 1)
                result.sourceEncoding = AceEncoding::Argb1555;
            else if (info.redBits == 4 && info.greenBits == 4 && info.blueBits == 4 && info.alphaBits == 4)
                result.sourceEncoding = AceEncoding::Argb4444;
            break;
        }
    } else {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (size.isValid() && (size.width() > 16384 || size.height() > 16384 ||
                              qint64(size.width()) * size.height() > 64 * 1024 * 1024)) {
            error = "Image exceeds 16384 per side or 64 million pixels.";
            return false;
        }
        const QByteArray detectedFormat = reader.format();
        const QImage decoded = reader.read();
        if (decoded.isNull()) { error = reader.errorString(); return false; }
        result.pixels = decoded.convertToFormat(QImage::Format_RGBA8888);
        result.format = QString::fromLatin1(detectedFormat).toUpper();
        result.alpha = decoded.hasAlphaChannel() ? AlphaKind::Full : AlphaKind::Opaque;
        result.storage = QString("%1-bit source; %2").arg(decoded.depth())
                             .arg(decoded.hasAlphaChannel() ? "alpha channel" : "no alpha channel");
    }
    if (result.pixels.isNull() || result.pixels.width() > 16384 || result.pixels.height() > 16384 ||
        qint64(result.pixels.width()) * result.pixels.height() > 64 * 1024 * 1024) {
        error = "Cannot allocate image, or image exceeds converter limits.";
        return false;
    }
    for (int y = 0; y < result.pixels.height() && !result.transparency; ++y) {
        const uchar *row = result.pixels.constScanLine(y);
        for (int x = 0; x < result.pixels.width(); ++x) {
            if (row[4 * x + 3] != 255) { result.transparency = true; break; }
        }
    }
    // Some BC1 files use transparent selectors without declaring an alpha flag.
    if (result.sourceEncoding == AceEncoding::Dxt1 && result.transparency) {
        result.sourceEncoding = AceEncoding::Dxt1Mask;
        result.alpha = AlphaKind::Binary;
    }
    if (result.mipCount > 1)
        result.warnings.append("Preview and conversion use the base image. Exported mipmaps are regenerated.");
    out = std::move(result);
    error.clear();
    return true;
}
bool save(const Image &image, const QString &path, const AceWriteOptions &options,
          QString &error, const QByteArray &imageFormat) {
    if (image.pixels.isNull()) { error = "Open a source image first."; return false; }
    const QByteArray format = imageFormat.isEmpty() ? QFileInfo(path).suffix().toLatin1().toLower()
                                                   : imageFormat.toLower();
    if (format == "ace") {
        if (options.mipmaps && !canGenerateMips(image.pixels)) {
            error = "ACE mipmaps require a square image with power-of-two dimensions. Disable mipmaps to keep this size.";
            return false;
        }
        AceWriteOptions effective = options;
        if (effective.mask.isEmpty()) effective.mask = image.mask;
        const QImage rgba = image.pixels.convertToFormat(QImage::Format_RGBA8888);
        AceDocument doc;
        if (!AceDocument::fromPixels(rgba.constBits(), rgba.sizeInBytes(), rgba.width(), rgba.height(),
                                     4, effective, doc, error)) return false;
        return doc.write(path, effective.zlib, error);
    }
    if (!QImageWriter::supportedImageFormats().contains(format)) {
        error = "Unsupported output format: " + QString::fromLatin1(format);
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { error = file.errorString(); return false; }
    QImageWriter writer(&file, format);
    if (!writer.write(image.pixels)) { error = writer.errorString(); return false; }
    if (!file.commit()) { error = file.errorString(); return false; }
    error.clear();
    return true;
}
QString inputFilter() {
    QStringList patterns{"*.ace", "*.dds"};
    for (const QByteArray &format : QImageReader::supportedImageFormats())
        patterns.append("*." + QString::fromLatin1(format));
    patterns.removeDuplicates();
    return "Images (" + patterns.join(' ') + ");;All files (*)";
}
QStringList outputFilters() {
    QStringList filters;
    for (const QByteArray &format : QImageWriter::supportedImageFormats()) {
        if (format == "ace") continue;
        filters.append(QString::fromLatin1(format).toUpper() + " image (*." +
                       QString::fromLatin1(format) + ')');
    }
    return filters;
}
}
