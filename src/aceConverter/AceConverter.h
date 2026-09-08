#ifndef TSRE_ACE_CONVERTER_H
#define TSRE_ACE_CONVERTER_H

#include <tsre/texture/AceDocument.h>
#include <QImage>
#include <functional>
#include <optional>

namespace AceConverter {
struct Encoding {
    const char *key;
    const char *label;
    AceEncoding value;
};
const QVector<Encoding> &encodings();
bool parseEncoding(const QString &name, AceEncoding &encoding);
enum class AlphaKind { Opaque, Binary, Full };
AlphaKind alphaKind(AceEncoding encoding);
bool recommendedForSimulators(AceEncoding encoding);

struct Image {
    QImage pixels; // Full-resolution, straight-alpha RGBA8888, one base level.
    QByteArray mask;
    QString path;
    QString format;
    QString storage;
    std::optional<bool> zlibEnvelope; // ACE only; independent of pixel encoding.
    QStringList warnings;
    int mipCount = 1;
    bool transparency = false;
    AlphaKind alpha = AlphaKind::Opaque; // Source channel capability, even for opaque pixels.
    std::optional<AceEncoding> sourceEncoding;
};
bool matchesSource(AceEncoding encoding, const Image &image);
AceEncoding suggestedEncoding(const Image &image, bool simulatorFormatsOnly);
bool load(const QString &path, Image &image, QString &error);
bool save(const Image &image, const QString &path, const AceWriteOptions &options,
          QString &error, const QByteArray &imageFormat = {});
bool canGenerateMips(const QImage &image);
QString inputFilter();
QStringList outputFilters();
// Handles a terminal --aceconv launch before TSRE changes cwd or loads game assets.
// The application can defer GUI creation until its normal settings/palette setup.
int run(int argc, char **argv, const std::function<int(const QString &)> &launchGui = {});
}
#endif
