#include <tsre/texture/AceDocument.h>
#include <tsre/texture/AceLib.h>
#include <tsre/texture/AceLibLegacy.h>
#include <tsre/texture/Texture.h>
#include <tsre/texture/DxtCodec.h>
#include "AceDxt3Diagnostic.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOffscreenSurface>
#include <QtEndian>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
int failures = 0, checks = 0;
void check(bool ok, const QString &name) {
    ++checks;
    if (!ok) {
        ++failures;
        qCritical().noquote() << "FAIL" << name;
    }
}
void release(Texture &t) {
    if (t.tex && QOpenGLContext::currentContext())
        glDeleteTextures(1, t.tex);
    delete[] t.tex;
    t.tex = nullptr;
    delete[] t.imageData;
    t.imageData = nullptr;
}
struct Encoding {
    const char *name;
    AceEncoding encoding;
};
const Encoding encodings[] = {{"rgb", AceEncoding::Rgb},
                              {"mask", AceEncoding::Mask},
                              {"rgba", AceEncoding::Rgba},
                              {"raw565", AceEncoding::Rgb565},
                              {"raw1555", AceEncoding::Argb1555},
                              {"raw4444", AceEncoding::Argb4444},
                              {"dxt1", AceEncoding::Dxt1},
                              {"dxt1mask", AceEncoding::Dxt1Mask},
                              {"dxt2", AceEncoding::Dxt2},
                              {"dxt3", AceEncoding::Dxt3},
                              {"dxt4", AceEncoding::Dxt4},
                              {"dxt5", AceEncoding::Dxt5},
                              {"palette_rgb", AceEncoding::IndexedRgb},
                              {"palette_rgba", AceEncoding::IndexedRgba}};
QByteArray pattern(int w, int h, bool alpha = true) {
    QByteArray data(qsizetype(w) * h * 4, Qt::Uninitialized);
    const unsigned char colors[4][4] = {
        {255, 0, 0, 255}, {0, 255, 0, 170}, {0, 0, 255, 85}, {255, 255, 0, 0}};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int index = int(x >= w / 2) + 2 * int(y >= h / 2);
            auto *p = reinterpret_cast<unsigned char *>(data.data() + (qsizetype(y) * w + x) * 4);
            memcpy(p, colors[index], 4);
            if (!alpha)
                p[3] = 255;
        }
    return data;
}
bool build(int w, int h, AceEncoding encoding, bool mips, bool zipped, AceDocument &doc,
           QString &error, bool alpha = true) {
    const QByteArray p = pattern(w, h, alpha);
    AceWriteOptions o;
    o.encoding = encoding;
    o.mipmaps = mips;
    o.zlib = zipped;
    return AceDocument::fromPixels(reinterpret_cast<const unsigned char *>(p.constData()), p.size(),
                                   w, h, 4, o, doc, error);
}
int selfTest() {
    QString error;
    QTemporaryDir dir;
    for (const auto &format : encodings)
        for (int size : {1, 4, 8, 64, 256})
            for (bool mips : {false, true})
                for (bool zipped : {false, true}) {
                    const QString name = QString("%1/%2/mips=%3/zlib=%4")
                                             .arg(format.name)
                                             .arg(size)
                                             .arg(mips)
                                             .arg(zipped);
                    AceDocument doc, parsed;
                    QByteArray file, pixels;
                    int components = 0;
                    if (!build(size, size, format.encoding, mips, zipped, doc, error)) {
                        check(false, name + " build: " + error);
                        continue;
                    }
                    check(doc.serialize(file, zipped, error), name + " serialize: " + error);
                    AceReadOptions options;
                    options.retainOriginal = true;
                    if (!AceDocument::parse(file, parsed, error, options)) {
                        check(false, name + " parse: " + error);
                        continue;
                    }
                    check(parsed.originalBytes() == file, name + " original envelope retained");
                    check(parsed.levels.size() == doc.levels.size(), name + " levels");
                    QByteArray copy;
                    check(parsed.serialize(copy, zipped, error), name + " serialize parsed");
                    AceDocument again;
                    check(AceDocument::parse(copy, again, error), name + " round trip");
                    for (int level = 0; level < parsed.levels.size(); ++level) {
                        check(parsed.levels[level].data == doc.levels[level].data,
                              name + " payload preservation");
                        check(parsed.decode(level, pixels, components, error),
                              name + " decode: " + error);
                        check(pixels.size() == qsizetype(parsed.levels[level].width) *
                                                   parsed.levels[level].height * components,
                              name + " decoded length");
                    }
                    check(parsed.decode(0, pixels, components, error), name + " base decode");
                    QByteArray direct(pixels.size(), Qt::Uninitialized);
                    check(parsed.decodeInto(0, reinterpret_cast<unsigned char *>(direct.data()),
                                            direct.size(), components, error) &&
                              direct == pixels,
                          name + " decode into caller storage");
                    check(!parsed.decodeInto(0, reinterpret_cast<unsigned char *>(direct.data()),
                                             direct.size() - 1, components, error),
                          name + " reject short caller storage");
                    if (size >= 8) {
                        const QByteArray source = pattern(size, size);
                        for (int y : {size / 4, 3 * size / 4})
                            for (int x : {size / 4, 3 * size / 4}) {
                                const qsizetype i = qsizetype(y) * size + x;
                                auto *p = reinterpret_cast<const unsigned char *>(
                                    pixels.constData() + i * components);
                                auto *expected = reinterpret_cast<const unsigned char *>(
                                    source.constData() + i * 4);
                                bool premult = format.encoding == AceEncoding::Dxt2 ||
                                               format.encoding == AceEncoding::Dxt4;
                                bool mask = format.encoding == AceEncoding::Mask ||
                                            format.encoding == AceEncoding::Argb1555 ||
                                            format.encoding == AceEncoding::Dxt1Mask;
                                for (int k = 0; k < 3; ++k)
                                    if (!(premult && !expected[3]) &&
                                        !(format.encoding == AceEncoding::Dxt1Mask &&
                                          expected[3] < 128))
                                        check(std::abs(int(p[k]) - expected[k]) <= 10,
                                              name + " color");
                                if (components == 4)
                                    check(p[3] ==
                                              (mask ? (expected[3] >= 128 ? 255 : 0) : expected[3]),
                                          name + " alpha");
                            }
                    }
                }
    // Independent binary mask and alpha, odd RGB rows, one-pixel and non-square bitmaps.
    for (auto dims : {QSize(9, 3), QSize(1, 7), QSize(4096, 1)}) {
        QByteArray source = pattern(dims.width(), dims.height());
        AceWriteOptions o;
        o.encoding = AceEncoding::Rgba;
        o.mask = QByteArray(dims.width() * dims.height(), 1);
        AceDocument doc, parsed;
        QByteArray file, pixels, mask;
        int c;
        check(AceDocument::fromPixels(reinterpret_cast<const unsigned char *>(source.constData()),
                                      source.size(), dims.width(), dims.height(), 4, o, doc, error),
              "odd fixture");
        check(doc.serialize(file, false, error) && AceDocument::parse(file, parsed, error),
              "odd parse");
        check(parsed.decode(0, pixels, c, error, &mask) && pixels == source && mask == o.mask,
              "independent mask / alpha");
    }
    AceDocument doc, parsed;
    QByteArray file;
    build(8, 8, AceEncoding::Rgb, false, false, doc, error);
    doc.serialize(file, false, error);
    for (qsizetype length = 0; length < file.size(); ++length)
        check(!AceDocument::parse(file.left(length), parsed, error),
              "truncation " + QString::number(length));
    QByteArray bad = file;
    qToLittleEndian<quint32>(0xffffffff, bad.data() + 16 + 8);
    check(!AceDocument::parse(bad, parsed, error), "oversized width rejected");
    bad = file;
    qToLittleEndian<quint32>(0, bad.data() + 16 + 152 + 48);
    check(!AceDocument::parse(bad, parsed, error), "header-pointing offset rejected");
    bad = file;
    const qsizetype table = 16 + 152 + 48;
    for (int y = 0; y < 8; ++y)
        qToLittleEndian<quint32>(200 + 32 + y * 8 * 12, bad.data() + table + y * 4);
    check(AceDocument::parse(bad, parsed, error) && !parsed.warnings.isEmpty(),
          "exact legacy stride recovery");
    AceReadOptions strict;
    strict.allowLegacyRgbOffsets = false;
    check(!AceDocument::parse(bad, parsed, error, strict), "strict rejects legacy stride");
    bad[table + 4] ^= 1;
    check(!AceDocument::parse(bad, parsed, error), "near-legacy corruption not recovered");
    // Metadata edit, raw-preserving rewrite, bounded zlib, and safe save failure.
    doc.metadata.header[0x84] = char(0x5a);
    doc.metadata.trailing = "opaque-footer";
    check(doc.serialize(file, true, error) && AceDocument::parse(file, parsed, error),
          "metadata zlib");
    check(parsed.metadata.header[0x84] == char(0x5a) && parsed.metadata.trailing == "opaque-footer",
          "metadata preserved");
    bad = file;
    qToLittleEndian<quint32>(152, bad.data() + 8);
    check(!AceDocument::parse(bad, parsed, error), "zlib output bounded to declared length");
    check(!doc.write(dir.path() + "/missing/destination.ace", false, error),
          "write failure reported");
    const QString path = dir.path() + "/probe.ace";
    doc.write(path, false, error);
    Texture t(path);
    AceLoadOptions load;
    check(AceLib::load(path, t, load, error) && t.editable && !t.aceDocument &&
              t.aceMetadata->trailing.isEmpty(),
          "render memory policy");
    Texture moved;
    moved.pathid = "stable-cache-key";
    moved.ref = 7;
    auto *pointer = t.imageData;
    moved.takeContentFrom(t);
    check(moved.imageData == pointer && !t.imageData && moved.pathid == "stable-cache-key" &&
              moved.ref == 7,
          "move content preserves identity, no copy");
    release(moved);
    load.preserveDocument = true;
    check(AceLib::load(path, t, load, error) && t.aceDocument &&
              t.aceMetadata->trailing == "opaque-footer",
          "document opt-in");
    t.pixelsChanged();
    check(!t.aceDocument && t.sourceMipmaps.isEmpty(), "edit invalidation");
    release(t);
    // Readiness remains a state, not a request; failed readback cannot invent pixels.
    Texture absent;
    absent.loaded = true;
    absent.width = absent.height = 8;
    absent.bytesPerPixel = 3;
    absent.setEditable();
    check(!absent.editable && !absent.imageData, "no-context readback stays unready");
    check(absent.getImageData(8, 8) == nullptr, "no-context scaling is safe");
    // Rectangular rotations keep the existing square-image orientation contract.
    Texture rotated(3, 2, 24);
    for (int i = 0; i < 6; ++i)
        memset(rotated.imageData + i * 3, i + 1, 3);
    rotated.crop(1, 0, 0, 1);
    const int ccw[] = {3, 6, 2, 5, 1, 4};
    check(rotated.width == 2 && rotated.height == 3, "rectangular crop dimensions");
    for (int i = 0; i < 6; ++i)
        check(rotated.imageData[i * 3] == ccw[i], "rectangular crop pixels");
    rotated.crop(0, 1, 1, 0);
    for (int i = 0; i < 6; ++i)
        check(rotated.imageData[i * 3] == i + 1, "inverse rectangular crop pixels");
    float uv[] = {0, 0, 0, 1.0f / 16, 0, 0, 1.0f / 16};
    rotated.advancedCrop(uv, 6, 4);
    check(rotated.width == 6 && rotated.height == 4 && rotated.imageSize == 72,
          "rectangular advanced crop");
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 6; ++x)
            check(rotated.imageData[(y * 6 + x) * 3] == (y / 2) * 3 + x / 2 + 1,
                  "advanced crop pixels");
    release(rotated);
    build(8, 8, AceEncoding::Dxt1Mask, false, false, doc, error);
    QByteArray maskPixels, maskBits;
    int c;
    check(doc.decode(0, maskPixels, c, error, &maskBits) && maskBits.size() == 64,
          "DXT1 independent mask extraction");
    const auto unknown = static_cast<DxtCodec::Format>(99);
    check(!DxtCodec::decode({}, 4, 4, unknown, false, maskPixels, error),
          "invalid DXT format rejected");
    QByteArray checker(4 * 4 * 3, '\0'), blocks, decoded;
    for (int i = 0; i < 16; ++i)
        checker[i * 3 + (i % 2)] = char(255);
    check(DxtCodec::encode(reinterpret_cast<const unsigned char *>(checker.constData()),
                           checker.size(), 4, 4, 3, DxtCodec::Format::Dxt1, false, blocks, error) &&
              DxtCodec::decode(blocks, 4, 4, DxtCodec::Format::Dxt1, false, decoded, error) &&
              decoded == checker,
          "DXT saturated-color endpoint fit");
    // Bounded deterministic mutation corpus: parsed unknown records may be preserved,
    // but malformed offsets/lengths must never escape the allocation/read limits.
    build(8, 8, AceEncoding::Rgba, true, false, doc, error);
    doc.serialize(file, false, error);
    quint32 random = 0xace12345;
    AceReadOptions bounded;
    bounded.maxBytes = 65536;
    bounded.maxDimension = 64;
    bounded.maxPixels = 4096;
    for (int i = 0; i < 2000; ++i) {
        bad = file;
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        bad[random % bad.size()] ^= char(1u << (random % 8));
        if (AceDocument::parse(bad, parsed, error, bounded)) {
            for (int m = 0; m < parsed.levels.size(); ++m)
                parsed.decode(m, maskPixels, c, error);
            QByteArray rewrite;
            parsed.serialize(rewrite, false, error);
        }
    }
    check(true, "2000 bounded mutations completed");
    // General QImage writer replaces the former temporary RGB-only helper.
    QImage image(9, 3, QImage::Format_RGBA8888);
    image.fill(QColor(25, 50, 100, 85));
    AceWriteOptions write;
    write.encoding = AceEncoding::Rgba;
    write.zlib = true;
    check(AceLib::save(path, image, write, error), "QImage RGBA writer");
    check(AceDocument::read(path, doc, error) && doc.decode(0, maskPixels, c, error) && c == 4 &&
              quint8(maskPixels[3]) == 85,
          "QImage alpha preserved");
    QFile previous(path);
    check(previous.open(QIODevice::ReadOnly), "saved QImage readable");
    const QByteArray before = previous.readAll();
    previous.close();
    check(!AceLib::save(path, QImage(), write, error), "empty QImage save rejected");
    check(previous.open(QIODevice::ReadOnly) && previous.readAll() == before,
          "failed QImage save preserves file");
    qInfo() << "ACE checks" << checks << "failures" << failures;
    return failures ? 1 : 0;
}

int fixture(const QStringList &args) {
    const QString path = args[4];
    if (QFile::exists(path) || QFile::exists(path + ".json"))
        return 2;
    bool valid = false;
    const int size = args[3].toInt(&valid);
    if (!valid || size < 1 || size > 4096)
        return 2;
    for (const auto &format : encodings)
        if (args[2] == format.name) {
            AceDocument doc;
            QString error;
            const bool mips = args.contains("--mips"), zipped = args.contains("--zlib");
            if (!build(size, size, format.encoding, mips, zipped, doc, error) ||
                !doc.write(path, zipped, error)) {
                qCritical() << error;
                return 1;
            }
            QFile input(path);
            if (!input.open(QIODevice::ReadOnly))
                return 1;
            const QByteArray bytes = input.readAll();
            QJsonObject record{
                {"path", path},
                {"encoding", format.name},
                {"size", size},
                {"mips", mips},
                {"zlib", zipped},
                {"bytes", bytes.size()},
                {"sha256",
                 QString(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
            QFile receipt(path + ".json");
            if (!receipt.open(QIODevice::WriteOnly))
                return 1;
            receipt.write(QJsonDocument(record).toJson());
            qInfo() << "Generated fixture" << path;
            return 0;
        }
    return 2;
}

int generate(const QString &path) {
    QDir root(path);
    if (root.exists()) {
        qCritical() << "Refusing existing output directory";
        return 2;
    }
    if (!QDir().mkpath(path))
        return 2;
    QJsonArray manifest;
    QString error;
    for (const auto &format : encodings)
        for (int size : {64, 128, 256, 512, 1024, 2048, 4096})
            for (bool mips : {false, true}) {
                AceDocument doc;
                // Paletted chains need explicit quantization for interpolated tiny levels.
                if (mips && (format.encoding == AceEncoding::IndexedRgb ||
                             format.encoding == AceEncoding::IndexedRgba))
                    continue;
                if (!build(size, size, format.encoding, mips, false, doc, error)) {
                    qCritical() << error;
                    return 1;
                }
                const QString name =
                    QString("%1_%2%3").arg(format.name).arg(size).arg(mips ? "_mips" : "");
                QByteArray data;
                if (!doc.serialize(data, false, error))
                    return 1;
                QFile file(root.filePath(name + ".ace"));
                if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
                    return 1;
                manifest.append(QJsonObject{
                    {"case", name},
                    {"size", size},
                    {"mips", mips},
                    {"surface", int(doc.surface())},
                    {"bytes", data.size()},
                    {"sha256",
                     QString(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())}});
            }
    QFile file(root.filePath("manifest.json"));
    if (!file.open(QIODevice::WriteOnly))
        return 1;
    file.write(QJsonDocument(manifest).toJson());
    qInfo() << "Generated" << manifest.size() << "fixtures in" << path;
    return 0;
}

int scan(const QString &path) {
    QDirIterator files(path, {"*.ace", "*.ACE"}, QDir::Files, QDirIterator::Subdirectories);
    int good = 0, bad = 0;
    QString error;
    while (files.hasNext()) {
        QString file = files.next();
        AceDocument doc;
        QByteArray pixels;
        int components;
        if (!AceDocument::read(file, doc, error) || !doc.decode(0, pixels, components, error)) {
            ++bad;
            qWarning().noquote() << file << error;
        } else
            ++good;
    }
    qInfo() << "ACE corpus decoded" << good << "failed" << bad;
    return bad ? 1 : 0;
}

int benchmark(const QString &directory) {
    // Same process/build/allocator, warm cache, alternating order; legacy inputs
    // restricted to profiles it really handles (never feed its unchecked parser fuzz).
    QTemporaryDir dir;
    QString error;
    QJsonArray results;
    for (const auto &format : {encodings[0], encodings[1], encodings[2], encodings[6]})
        for (int size : {256, 512, 1024, 2048, 4096}) {
            AceDocument doc;
            build(size, size, format.encoding, true, false, doc, error);
            const QString path = dir.path() + "/bench.ace";
            doc.write(path, false, error);
            QVector<double> legacy, newBase, newMips;
            const int repeats = size >= 2048 ? 7 : 15;
            for (int round = -2; round < repeats; ++round)
                for (int j = 0; j < 3; ++j) {
                    int mode = (j + (round + 2) % 3) % 3;
                    Texture texture(path);
                    QElapsedTimer clock;
                    clock.start();
                    bool ok;
                    if (mode == 0) {
                        AceLibLegacy loader;
                        loader.texture = &texture;
                        loader.run();
                        ok = texture.loaded && !texture.error;
                    } else {
                        AceLoadOptions o;
                        o.stageMipmaps = mode == 2;
                        ok = AceLib::load(path, texture, o, error);
                    }
                    const double ms = clock.nsecsElapsed() / 1e6;
                    if (!ok) {
                        qCritical() << "Benchmark failure" << path << error;
                        release(texture);
                        return 1;
                    }
                    if (round >= 0)
                        (mode == 0 ? legacy : mode == 1 ? newBase : newMips).push_back(ms);
                    release(texture);
                }
            auto median = [](QVector<double> v) {
                std::sort(v.begin(), v.end());
                return v[v.size() / 2];
            };
            qInfo() << "Benchmark" << format.name << size << "legacy/base/mips ms" << median(legacy)
                    << median(newBase) << median(newMips);
            results.append(QJsonObject{{"format", format.name},
                                       {"size", size},
                                       {"repeats", repeats},
                                       {"legacy_ms", median(legacy)},
                                       {"new_base_ms", median(newBase)},
                                       {"new_mips_ms", median(newMips)},
                                       {"base_ratio", median(newBase) / median(legacy)},
                                       {"mips_ratio", median(newMips) / median(legacy)}});
        }
    const QByteArray json = QJsonDocument(results).toJson();
    if (directory.isEmpty())
        fwrite(json.constData(), 1, json.size(), stdout);
    else {
        QFile file(directory);
        if (file.exists() || !file.open(QIODevice::WriteOnly))
            return 2;
        file.write(json);
    }
    return 0;
}

int benchmarkWrite(const QString &destination) {
    QTemporaryDir dir;
    QString error;
    QJsonArray results;
    for (int size : {256, 512, 1024, 2048, 4096}) {
        const QByteArray pixels = pattern(size, size, false);
        const QImage image(reinterpret_cast<const unsigned char *>(pixels.constData()), size, size,
                           size * 4, QImage::Format_RGBA8888);
        const QString path = dir.path() + "/write.ace";
        QVector<double> legacy, current;
        const int repeats = size >= 2048 ? 7 : 15;
        AceWriteOptions options;
        options.encoding = AceEncoding::Rgb;
        for (int round = -2; round < repeats; ++round)
            for (int j = 0; j < 2; ++j) {
                const bool old = (j + round + 2) % 2 == 0;
                QElapsedTimer timer;
                timer.start();
                const bool ok = old ? AceLibLegacy::saveRgbChecked(path, image, error)
                                    : AceLib::save(path, image, options, error);
                const double ms = timer.nsecsElapsed() / 1e6;
                if (!ok) {
                    qCritical() << "Writer benchmark failed" << error;
                    return 1;
                }
                if (round >= 0)
                    (old ? legacy : current).push_back(ms);
            }
        auto median = [](QVector<double> values) {
            std::sort(values.begin(), values.end());
            return values[values.size() / 2];
        };
        qInfo() << "RGB writer" << size << "legacy/new ms" << median(legacy) << median(current);
        results.append(QJsonObject{{"size", size},
                                   {"repeats", repeats},
                                   {"legacy_ms", median(legacy)},
                                   {"new_ms", median(current)},
                                   {"ratio", median(current) / median(legacy)}});
    }
    const QByteArray json = QJsonDocument(results).toJson();
    if (destination.isEmpty())
        fwrite(json.constData(), 1, json.size(), stdout);
    else {
        QFile file(destination);
        if (file.exists() || !file.open(QIODevice::WriteOnly))
            return 2;
        file.write(json);
    }
    return 0;
}

int glTest() {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    if (!context.create()) {
        qCritical() << "No GL context";
        return 2;
    }
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!context.makeCurrent(&surface))
        return 2;
    qInfo() << "GL renderer" << reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    QTemporaryDir dir;
    QString error;
    for (const auto &encoding : encodings)
        for (int size : {8, 64, 512, 2048, 4096})
            for (bool mips : {false, true}) {
                if (mips && (encoding.encoding == AceEncoding::IndexedRgb ||
                             encoding.encoding == AceEncoding::IndexedRgba))
                    continue;
                AceDocument doc;
                const QString name =
                    QString("%1/%2/mips=%3").arg(encoding.name).arg(size).arg(mips);
                if (!build(size, size, encoding.encoding, mips, false, doc, error)) {
                    check(false, name + error);
                    continue;
                }
                // Make a visibly distinct authored mip: this must not become a generated base
                // average.
                if (mips && !doc.levels[1].raw)
                    doc.levels[1].data.fill(char(64));
                const QString path = dir.path() + "/probe.ace";
                doc.write(path, false, error);
                Texture texture(path);
                AceLoadOptions o;
                check(AceLib::load(path, texture, o, error), name + " load");
                check(texture.GLTextures(mips), name + " upload " + texture.errorMessage);
                check(glGetError() == GL_NO_ERROR, name + " GL error");
                check(!texture.imageData && texture.compressedData.isEmpty() &&
                          texture.sourceMipmaps.isEmpty() && !texture.editable &&
                          !texture.aceDocument,
                      name + " CPU released");
                glBindTexture(GL_TEXTURE_2D, texture.tex[0]);
                GLint filter;
                glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
                check(filter == (mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR),
                      name + " opt-in mip filtering");
                QByteArray expected;
                int c;
                doc.decode(0, expected, c, error);
                texture.setEditable();
                check(texture.editable && texture.imageData, name + " readback readiness");
                if (texture.imageData) {
                    // GL's BC endpoint expansion can differ by 1 from the integer CPU reference.
                    int maxError = 0, worstChannel = -1, worstExpected = 0, worstActual = 0;
                    for (int y : {size / 4, 3 * size / 4})
                        for (int x : {size / 4, 3 * size / 4})
                            for (int k = 0; k < c; ++k) {
                                qsizetype i = (qsizetype(y) * size + x) * c + k;
                                const int actual = texture.imageData[i], reference = quint8(expected[i]);
                                const int difference = std::abs(actual - reference);
                                if (difference > maxError) {
                                    maxError = difference;
                                    worstChannel = k;
                                    worstExpected = reference;
                                    worstActual = actual;
                                }
                            }
                    check(maxError <= 2, name + " GPU pixels (maximum component difference " +
                                                QString::number(maxError) + ", channel " +
                                                QString::number(worstChannel) + ", expected " +
                                                QString::number(worstExpected) + ", actual " +
                                                QString::number(worstActual) + ")");
                    if (mips && !doc.levels[1].raw) {
                        QByteArray mip;
                        doc.decode(1, mip, c, error);
                        QByteArray gpu(mip.size(), Qt::Uninitialized);
                        glPixelStorei(GL_PACK_ALIGNMENT, 1);
                        glGetTexImage(GL_TEXTURE_2D, 1, texture.type, GL_UNSIGNED_BYTE, gpu.data());
                        check(gpu == mip, name + " authored mip used");
                    }
                    texture.imageData[0] = 123;
                    texture.update();
                    check(texture.sourceMipmaps.isEmpty() && glGetError() == GL_NO_ERROR,
                          name + " edit regenerates mips safely");
                }
                release(texture);
            }
    // Odd RGB rows with hostile inherited pack/unpack state.
    AceDocument doc;
    build(9, 3, AceEncoding::Rgb, false, false, doc, error);
    QString path = dir.path() + "/odd.ace";
    doc.write(path, false, error);
    Texture texture(path);
    AceLoadOptions o;
    check(AceLib::load(path, texture, o, error), "odd load");
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 32);
    check(texture.GLTextures(false), "odd upload");
    GLint state;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &state);
    check(state == 8, "unpack state restored");
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glPixelStorei(GL_PACK_ROW_LENGTH, 32);
    texture.setEditable();
    QByteArray expected;
    int c;
    doc.decode(0, expected, c, error);
    check(texture.imageData &&
              memcmp(texture.imageData, expected.constData(), expected.size()) == 0,
          "odd readback pixels");
    glGetIntegerv(GL_PACK_ROW_LENGTH, &state);
    check(state == 32, "pack state restored");
    const auto oldHandle = texture.tex[0];
    check(AceLib::load(path, texture, o, error) && texture.tex && texture.tex[0] == oldHandle,
          "CPU reload reuses resident GL handle");
    check(texture.GLTextures(false) && glGetError() == GL_NO_ERROR, "reload upload");
    release(texture);
    qInfo() << "ACE GL checks" << checks << "failures" << failures;
    return failures ? 1 : 0;
}
} // namespace

int main(int argc, char **argv) {
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        const QByteArray text = message.toLocal8Bit();
        fprintf(stderr, "%s\n", text.constData());
    });
    bool gl = false;
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--gl" || QByteArray(argv[i]) == "--gl-dxt3"
            || QByteArray(argv[i]) == "--bench-readback")
            gl = true;
    if (!gl && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.contains("--self-test"))
        return selfTest();
    if (args.contains("--gl-dxt3"))
        return runDxt3Diagnostic();
    if (args.contains("--bench-readback"))
        return runReadbackBenchmark();
    if (gl)
        return glTest();
    if (args.size() == 3 && args[1] == "--generate")
        return generate(args[2]);
    if (args.size() >= 5 && args[1] == "--fixture")
        return fixture(args);
    if (args.size() == 3 && args[1] == "--scan")
        return scan(args[2]);
    if (args.contains("--bench"))
        return benchmark(args.size() == 3 ? args[2] : QString());
    if (args.contains("--bench-write"))
        return benchmarkWrite(args.size() == 3 ? args[2] : QString());
    qInfo()
        << "--self-test | --gl | --gl-dxt3 | --bench-readback | --generate NEW_DIRECTORY | --fixture FORMAT SIZE NEW_FILE "
           "[--mips] [--zlib] | --scan DIRECTORY | --bench [NEW_JSON] | --bench-write [NEW_JSON]";
    return 2;
}
