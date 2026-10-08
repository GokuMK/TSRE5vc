/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "RhiTextures.h"
#include "RhiContext.h"
#include <QMap>
#include <QStringList>
#include <algorithm>
#include <vector>

namespace {
struct Entry {
    QRhiTexture *texture = nullptr;
    bool mipmaps = false;
    bool clamp = false;
    bool nearest = false;
};
std::vector<Entry> &store() {
    static std::vector<Entry> textures;
    return textures;
}
QRhiResourceUpdateBatch *pending = nullptr;

unsigned int add(const Entry &entry) {
    store().push_back(entry);
    return static_cast<unsigned int>(store().size());
}

// The next mipmap level of a square RGBA8 image: the mean of each 2x2 block.
QByteArray halved(const QByteArray &level, int side) {
    const int half = std::max(1, side / 2);
    QByteArray out(qsizetype(half) * half * 4, '\0');
    const auto *in = reinterpret_cast<const unsigned char *>(level.constData());
    auto *to = reinterpret_cast<unsigned char *>(out.data());
    for (int y = 0; y < half; ++y)
        for (int x = 0; x < half; ++x)
            for (int c = 0; c < 4; ++c) {
                const int x0 = std::min(2 * x, side - 1), x1 = std::min(2 * x + 1, side - 1);
                const int y0 = std::min(2 * y, side - 1), y1 = std::min(2 * y + 1, side - 1);
                const int sum = in[(y0 * side + x0) * 4 + c] + in[(y0 * side + x1) * 4 + c]
                        + in[(y1 * side + x0) * 4 + c] + in[(y1 * side + x1) * 4 + c];
                to[(y * half + x) * 4 + c] = static_cast<unsigned char>((sum + 2) / 4);
            }
    return out;
}
}

namespace RhiTextures {

QRhiResourceUpdateBatch *updates() {
    if (pending == nullptr) {
        RhiContext *context = RhiContext::instance();
        if (context == nullptr || context->rhi() == nullptr)
            return nullptr;
        pending = context->rhi()->nextResourceUpdateBatch();
    }
    return pending;
}

QRhiResourceUpdateBatch *takeUpdates() {
    QRhiResourceUpdateBatch *batch = pending;
    pending = nullptr;
    return batch;
}

unsigned int create(int width, int height, const QVector<QByteArray> &levels) {
    RhiContext *context = RhiContext::instance();
    if (context == nullptr || context->rhi() == nullptr || levels.isEmpty() || width <= 0
            || height <= 0)
        return 0;
    QRhi *rhi = context->rhi();
    const QSize size(width, height);
    const int levelCount = rhi->mipLevelsForSize(size);
    const bool complete = levels.size() >= levelCount;
    QRhiTexture::Flags flags = QRhiTexture::MipMapped;
    if (!complete)
        flags |= QRhiTexture::UsedWithGenerateMips;
    QRhiTexture *texture = rhi->newTexture(QRhiTexture::RGBA8, size, 1, flags);
    if (!texture->create()) {
        delete texture;
        return 0;
    }
    QRhiResourceUpdateBatch *batch = updates();
    QVarLengthArray<QRhiTextureUploadEntry, 16> entries;
    const int uploaded = complete ? levelCount : 1;
    for (int level = 0; level < uploaded; ++level) {
        const QSize levelSize = rhi->sizeForMipLevel(level, size);
        QRhiTextureSubresourceUploadDescription description(levels[level]);
        description.setSourceSize(levelSize);
        entries.append(QRhiTextureUploadEntry(0, level, description));
    }
    QRhiTextureUploadDescription upload;
    upload.setEntries(entries.cbegin(), entries.cend());
    batch->uploadTexture(texture, upload);
    if (!complete)
        batch->generateMips(texture);
    return add(Entry{texture, false, false, false});
}

bool supportsBlocks() {
    RhiContext *context = RhiContext::instance();
    QRhi *rhi = context != nullptr ? context->rhi() : nullptr;
    return rhi != nullptr && rhi->isTextureFormatSupported(QRhiTexture::BC1)
            && rhi->isTextureFormatSupported(QRhiTexture::BC2)
            && rhi->isTextureFormatSupported(QRhiTexture::BC3);
}

unsigned int createCompressed(int width, int height, Blocks format,
                              const QVector<QByteArray> &levels) {
    RhiContext *context = RhiContext::instance();
    if (context == nullptr || context->rhi() == nullptr || levels.isEmpty() || width <= 0
            || height <= 0)
        return 0;
    QRhi *rhi = context->rhi();
    const QSize size(width, height);
    const bool mipmapped = levels.size() > 1;
    if (mipmapped && levels.size() != rhi->mipLevelsForSize(size))
        return 0;
    const QRhiTexture::Format rhiFormat = format == Blocks::Bc1 ? QRhiTexture::BC1
            : format == Blocks::Bc2 ? QRhiTexture::BC2 : QRhiTexture::BC3;
    QRhiTexture *texture = rhi->newTexture(rhiFormat, size, 1,
                                           mipmapped ? QRhiTexture::MipMapped : QRhiTexture::Flags());
    if (!texture->create()) {
        delete texture;
        return 0;
    }
    QVarLengthArray<QRhiTextureUploadEntry, 16> entries;
    for (int level = 0; level < levels.size(); ++level)
        entries.append(QRhiTextureUploadEntry(0, level,
                                              QRhiTextureSubresourceUploadDescription(levels[level])));
    QRhiTextureUploadDescription upload;
    upload.setEntries(entries.cbegin(), entries.cend());
    updates()->uploadTexture(texture, upload);
    return add(Entry{texture, false, false, false});
}

unsigned int createData(int width, int height, bool floats, const QByteArray &data) {
    RhiContext *context = RhiContext::instance();
    if (context == nullptr || context->rhi() == nullptr || width <= 0 || height <= 0
            || data.size() != qsizetype(width) * height * (floats ? 16 : 1))
        return 0;
    QRhiTexture *texture = context->rhi()->newTexture(floats ? QRhiTexture::RGBA32F : QRhiTexture::R8,
                                                     QSize(width, height));
    if (!texture->create()) {
        delete texture;
        return 0;
    }
    QRhiTextureSubresourceUploadDescription description(data);
    description.setSourceSize(QSize(width, height));
    updates()->uploadTexture(texture, QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, description)));
    return add(Entry{texture, false, true, true});
}

void updateRegion(unsigned int handle, int x, int y, int width, int height, const QByteArray &data) {
    QRhiTexture *target = texture(handle);
    if (target == nullptr || width <= 0 || height <= 0 || updates() == nullptr)
        return;
    QRhiTextureSubresourceUploadDescription description(data);
    description.setSourceSize(QSize(width, height));
    description.setDestinationTopLeft(QPoint(x, y));
    updates()->uploadTexture(target, QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, description)));
}

unsigned int createArray(int side, const QVector<QByteArray> &layers) {
    RhiContext *context = RhiContext::instance();
    if (context == nullptr || context->rhi() == nullptr || layers.isEmpty() || side <= 0)
        return 0;
    QRhi *rhi = context->rhi();
    const QSize size(side, side);
    QRhiTexture *texture = rhi->newTextureArray(QRhiTexture::RGBA8, layers.size(), size, 1,
                                                QRhiTexture::MipMapped);
    if (!texture->create()) {
        delete texture;
        return 0;
    }
    // The levels are made here; generating mipmaps of arrays is not
    // available on every backend.
    const int levelCount = rhi->mipLevelsForSize(size);
    QVector<QRhiTextureUploadEntry> entries;
    for (int layer = 0; layer < layers.size(); ++layer) {
        if (layers[layer].size() != qsizetype(side) * side * 4) {
            delete texture;
            return 0;
        }
        QByteArray level = layers[layer];
        int levelSide = side;
        for (int mip = 0; mip < levelCount; ++mip) {
            QRhiTextureSubresourceUploadDescription description(level);
            description.setSourceSize(QSize(levelSide, levelSide));
            entries.append(QRhiTextureUploadEntry(layer, mip, description));
            if (mip + 1 < levelCount) {
                level = halved(level, levelSide);
                levelSide = std::max(1, levelSide / 2);
            }
        }
    }
    QRhiTextureUploadDescription upload;
    upload.setEntries(entries.cbegin(), entries.cend());
    updates()->uploadTexture(texture, upload);
    return add(Entry{texture, true, false, false});
}

QRhiTexture *texture(unsigned int handle) {
    if (handle == 0 || handle > store().size())
        return nullptr;
    return store()[handle - 1].texture;
}

void setSampling(unsigned int handle, bool mipmaps, bool clamp) {
    if (handle == 0 || handle > store().size())
        return;
    store()[handle - 1].mipmaps = mipmaps;
    store()[handle - 1].clamp = clamp;
}

bool sampledWithMipmaps(unsigned int handle) {
    return handle != 0 && handle <= store().size() && store()[handle - 1].mipmaps;
}

bool clampedToEdge(unsigned int handle) {
    return handle != 0 && handle <= store().size() && store()[handle - 1].clamp;
}

void releaseAll() {
    for (Entry &entry : store()) {
        delete entry.texture;
        entry = Entry();
    }
    if (pending != nullptr) {
        pending->release();
        pending = nullptr;
    }
}

QString memorySummary() {
    QMap<QString, QPair<int, qint64>> formats;
    for (const Entry &entry : store()) {
        if (entry.texture == nullptr)
            continue;
        const QRhiTexture *t = entry.texture;
        const QSize size = t->pixelSize();
        const qint64 texels = qint64(size.width()) * size.height() * std::max(1, t->arraySize());
        qint64 bytes = 0;
        QString name;
        switch (t->format()) {
        case QRhiTexture::BC1: bytes = texels / 2; name = "BC1"; break;
        case QRhiTexture::BC2: bytes = texels; name = "BC2"; break;
        case QRhiTexture::BC3: bytes = texels; name = "BC3"; break;
        case QRhiTexture::R8: bytes = texels; name = "R8"; break;
        case QRhiTexture::RGBA32F: bytes = texels * 16; name = "RGBA32F"; break;
        default: bytes = texels * 4; name = "RGBA8"; break;
        }
        if (t->flags() & QRhiTexture::MipMapped) {
            bytes = bytes * 4 / 3;
            name += " mipmapped";
        }
        if (t->arraySize() > 0)
            name += " array";
        auto &total = formats[name];
        total.first += 1;
        total.second += bytes;
    }
    QStringList parts;
    for (auto it = formats.cbegin(); it != formats.cend(); ++it)
        parts << QString("%1: %2 / %3 MB").arg(it.key()).arg(it.value().first)
                         .arg(it.value().second / 1048576.0, 0, 'f', 1);
    return parts.join(", ");
}

bool sampledNearest(unsigned int handle) {
    return handle != 0 && handle <= store().size() && store()[handle - 1].nearest;
}

void release(unsigned int handle) {
    if (handle == 0 || handle > store().size())
        return;
    Entry &entry = store()[handle - 1];
    if (entry.texture != nullptr)
        entry.texture->deleteLater();
    entry = Entry();
}

}
