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
