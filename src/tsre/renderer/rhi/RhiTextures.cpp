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
#include <tsre/renderer/FrameLog.h>
#include <QElapsedTimer>
#include <tsre/renderer/MemoryLog.h>
#include <QMap>
#include <QStringList>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace {
struct Entry {
    QRhiTexture *texture = nullptr;
    bool mipmaps = false;
    bool clamp = false;
    bool nearest = false;
    // Released for memory, the handle kept for its texture to come back.
    bool evicted = false;
    qint64 used = 0;
};
std::vector<Entry> &store() {
    static std::vector<Entry> textures;
    return textures;
}
QRhiResourceUpdateBatch *pending = nullptr;
std::vector<QRhiTexture *> retired;
qint64 useClock = 0;

// Handles are never reused, except an evicted one given back as reuse.
unsigned int add(Entry entry, unsigned int reuse = 0) {
    entry.used = useClock;
    if (reuse != 0 && reuse <= store().size() && store()[reuse - 1].evicted) {
        Entry &slot = store()[reuse - 1];
        entry.clamp = slot.clamp;
        entry.nearest = slot.nearest;
        slot = entry;
        return reuse;
    }
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

qint64 queuedBytes = 0;
qint64 allocatedNs = 0;

qint64 bytesQueued() {
    return queuedBytes;
}

qint64 allocationNs() {
    return allocatedNs;
}

qint64 openedBlocks = 0;

qint64 blocksOpened() {
    return openedBlocks;
}

// The memory blocks QRhi holds, when tracing (statistics() walks them).
quint32 blocksNow(QRhi *rhi) {
    static const bool trace = qEnvironmentVariableIsSet("TSRE_UPLOAD_TRACE");
    return trace ? rhi->statistics().blockCount : 0;
}

QRhiResourceUpdateBatch *takeUpdates() {
    QRhiResourceUpdateBatch *batch = pending;
    pending = nullptr;
    return batch;
}

unsigned int create(int width, int height, const QVector<QByteArray> &levels, unsigned int reuse) {
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
    const quint32 blocksBefore = blocksNow(rhi);
    QElapsedTimer allocation;
    allocation.start();
    QRhiTexture *texture = rhi->newTexture(QRhiTexture::RGBA8, size, 1, flags);
    if (!MemoryLog::created(texture, "texture")) {
        delete texture;
        return 0;
    }
    allocatedNs += allocation.nsecsElapsed();
    openedBlocks += qint64(blocksNow(rhi)) - qint64(blocksBefore);
    QRhiResourceUpdateBatch *batch = updates();
    QVarLengthArray<QRhiTextureUploadEntry, 16> entries;
    const int uploaded = complete ? levelCount : 1;
    qint64 bytes = 0;
    for (int level = 0; level < uploaded; ++level) {
        const QSize levelSize = rhi->sizeForMipLevel(level, size);
        QRhiTextureSubresourceUploadDescription description(levels[level]);
        description.setSourceSize(levelSize);
        entries.append(QRhiTextureUploadEntry(0, level, description));
        bytes += levels[level].size();
    }
    queuedBytes += bytes;
    FrameLog::add(FrameLog::TextureKB, bytes / 1024.0);
    QRhiTextureUploadDescription upload;
    upload.setEntries(entries.cbegin(), entries.cend());
    batch->uploadTexture(texture, upload);
    if (!complete)
        batch->generateMips(texture);
    return add(Entry{texture, false, false, false}, reuse);
}

unsigned int createTarget(int width, int height) {
    RhiContext *context = RhiContext::instance();
    if (context == nullptr || context->rhi() == nullptr || width <= 0 || height <= 0)
        return 0;
    QRhiTexture *texture = context->rhi()->newTexture(
            QRhiTexture::RGBA8, QSize(width, height), 1,
            QRhiTexture::RenderTarget | QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips);
    if (!MemoryLog::created(texture, "render texture")) {
        delete texture;
        return 0;
    }
    return add(Entry{texture, true, false, false});
}

static std::atomic<int> blocksKnown{-1};

bool supportsBlocks() {
    RhiContext *context = RhiContext::instance();
    QRhi *rhi = context != nullptr ? context->rhi() : nullptr;
    if (rhi == nullptr)
        return false;
    const bool supported = rhi->isTextureFormatSupported(QRhiTexture::BC1)
            && rhi->isTextureFormatSupported(QRhiTexture::BC2)
            && rhi->isTextureFormatSupported(QRhiTexture::BC3);
    blocksKnown.store(supported ? 1 : 0, std::memory_order_release);
    return supported;
}

int blocksSupportKnown() {
    return blocksKnown.load(std::memory_order_acquire);
}

unsigned int createCompressed(int width, int height, Blocks format,
                              const QVector<QByteArray> &levels, unsigned int reuse) {
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
    const quint32 blocksBefore = blocksNow(rhi);
    QElapsedTimer allocation;
    allocation.start();
    QRhiTexture *texture = rhi->newTexture(rhiFormat, size, 1,
                                           mipmapped ? QRhiTexture::MipMapped : QRhiTexture::Flags());
    if (!MemoryLog::created(texture, "texture")) {
        delete texture;
        return 0;
    }
    allocatedNs += allocation.nsecsElapsed();
    openedBlocks += qint64(blocksNow(rhi)) - qint64(blocksBefore);
    QVarLengthArray<QRhiTextureUploadEntry, 16> entries;
    qint64 bytes = 0;
    for (int level = 0; level < levels.size(); ++level) {
        entries.append(QRhiTextureUploadEntry(0, level,
                                              QRhiTextureSubresourceUploadDescription(levels[level])));
        bytes += levels[level].size();
    }
    queuedBytes += bytes;
    FrameLog::add(FrameLog::TextureKB, bytes / 1024.0);
    QRhiTextureUploadDescription upload;
    upload.setEntries(entries.cbegin(), entries.cend());
    updates()->uploadTexture(texture, upload);
    return add(Entry{texture, false, false, false}, reuse);
}

unsigned int createForParts(int width, int height, bool rgba, Blocks format, int levels) {
    RhiContext *context = RhiContext::instance();
    if (context == nullptr || context->rhi() == nullptr || width <= 0 || height <= 0 || levels <= 0)
        return 0;
    QRhi *rhi = context->rhi();
    const QSize size(width, height);
    const int levelCount = rhi->mipLevelsForSize(size);
    QRhiTexture::Format rhiFormat = QRhiTexture::RGBA8;
    QRhiTexture::Flags flags;
    if (rgba) {
        flags = QRhiTexture::MipMapped;
        if (levels < levelCount)
            flags |= QRhiTexture::UsedWithGenerateMips;
    } else {
        if (levels != 1 && levels != levelCount)
            return 0;
        rhiFormat = format == Blocks::Bc1 ? QRhiTexture::BC1 : format == Blocks::Bc2 ? QRhiTexture::BC2 : QRhiTexture::BC3;
        if (levels > 1)
            flags = QRhiTexture::MipMapped;
    }
    const quint32 blocksBefore = blocksNow(rhi);
    QElapsedTimer allocation;
    allocation.start();
    QRhiTexture *texture = rhi->newTexture(rhiFormat, size, 1, flags);
    if (!texture->create()) {
        delete texture;
        return 0;
    }
    allocatedNs += allocation.nsecsElapsed();
    openedBlocks += qint64(blocksNow(rhi)) - qint64(blocksBefore);
    return add(Entry{texture, false, false, false});
}

void uploadPart(unsigned int handle, int level, int firstRow, int rows, const QByteArray &data) {
    QRhiTexture *t = texture(handle);
    RhiContext *context = RhiContext::instance();
    if (t == nullptr || context == nullptr || context->rhi() == nullptr || rows <= 0)
        return;
    const QSize levelSize = context->rhi()->sizeForMipLevel(level, t->pixelSize());
    const int texelRows = t->format() == QRhiTexture::RGBA8 ? 1 : 4;
    const int top = firstRow * texelRows;
    QRhiTextureSubresourceUploadDescription description(data);
    description.setDestinationTopLeft(QPoint(0, top));
    description.setSourceSize(QSize(levelSize.width(), std::min(rows * texelRows, levelSize.height() - top)));
    updates()->uploadTexture(t, QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, level, description)));
    queuedBytes += data.size();
    FrameLog::add(FrameLog::TextureKB, data.size() / 1024.0);
}

void finishParts(unsigned int handle, bool generateMips) {
    QRhiTexture *t = texture(handle);
    if (t != nullptr && generateMips)
        updates()->generateMips(t);
}

unsigned int createData(int width, int height, bool floats, const QByteArray &data) {
    RhiContext *context = RhiContext::instance();
    if (context == nullptr || context->rhi() == nullptr || width <= 0 || height <= 0
            || data.size() != qsizetype(width) * height * (floats ? 16 : 1))
        return 0;
    QRhiTexture *texture = context->rhi()->newTexture(floats ? QRhiTexture::RGBA32F : QRhiTexture::R8,
                                                     QSize(width, height));
    if (!MemoryLog::created(texture, "data texture")) {
        delete texture;
        return 0;
    }
    QRhiTextureSubresourceUploadDescription description(data);
    description.setSourceSize(QSize(width, height));
    updates()->uploadTexture(texture, QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, description)));
    return add(Entry{texture, false, true, true});
}

bool replaceLevel0(unsigned int handle, int width, int height, const QByteArray &rgba) {
    QRhiTexture *target = texture(handle);
    if (target == nullptr || target->format() != QRhiTexture::RGBA8
            || target->pixelSize() != QSize(width, height)
            || rgba.size() != qsizetype(width) * height * 4 || updates() == nullptr)
        return false;
    // Without its own mipmaps (file levels) it cannot make them again.
    const bool remake = target->flags().testFlag(QRhiTexture::UsedWithGenerateMips);
    if (target->flags().testFlag(QRhiTexture::MipMapped) && !remake)
        return false;
    QRhiTextureSubresourceUploadDescription description(rgba);
    description.setSourceSize(QSize(width, height));
    updates()->uploadTexture(target, QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, description)));
    if (remake)
        updates()->generateMips(target);
    return true;
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

Contents readBack(unsigned int handle, QByteArray &data) {
    data.clear();
    RhiContext *context = RhiContext::instance();
    QRhi *rhi = context != nullptr ? context->rhi() : nullptr;
    QRhiTexture *source = texture(handle);
    // An offscreen frame cannot start inside the frame being recorded.
    if (rhi == nullptr || source == nullptr || rhi->isRecordingFrame())
        return Contents::None;
    Contents contents;
    int blockBytes = 0;
    switch (source->format()) {
    case QRhiTexture::RGBA8: contents = Contents::Rgba8; break;
    case QRhiTexture::BC1: contents = Contents::Bc1; blockBytes = 8; break;
    case QRhiTexture::BC2: contents = Contents::Bc2; blockBytes = 16; break;
    case QRhiTexture::BC3: contents = Contents::Bc3; blockBytes = 16; break;
    default: return Contents::None;
    }
    QRhiCommandBuffer *commandBuffer = nullptr;
    if (rhi->beginOffscreenFrame(&commandBuffer) != QRhi::FrameOpSuccess)
        return Contents::None;
    // The texture's own upload may still wait for the next frame.
    if (QRhiResourceUpdateBatch *waiting = takeUpdates())
        commandBuffer->resourceUpdate(waiting);
    QRhiReadbackResult result;
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->readBackTexture(QRhiReadbackDescription(source), &result);
    commandBuffer->resourceUpdate(batch);
    rhi->endOffscreenFrame();

    const QSize size = source->pixelSize();
    if (contents != Contents::Rgba8) {
        const qsizetype expected =
                qsizetype((size.width() + 3) / 4) * ((size.height() + 3) / 4) * blockBytes;
        if (result.data.size() != expected)
            return Contents::None;
        data = result.data;
        return contents;
    }
    // RGBA8 rows, tightly packed; a backend may pad them.
    const qsizetype row = qsizetype(size.width()) * 4;
    if (size.height() <= 0 || result.data.size() < row * size.height()
            || result.data.size() % size.height() != 0)
        return Contents::None;
    const qsizetype stride = result.data.size() / size.height();
    if (stride == row) {
        data = result.data;
    } else {
        data.resize(row * size.height());
        for (int y = 0; y < size.height(); ++y)
            std::memcpy(data.data() + y * row, result.data.constData() + y * stride, row);
    }
    return contents;
}

unsigned int createArray(int side, const QVector<QByteArray> &layers) {
    RhiContext *context = RhiContext::instance();
    if (context == nullptr || context->rhi() == nullptr || layers.isEmpty() || side <= 0)
        return 0;
    QRhi *rhi = context->rhi();
    const QSize size(side, side);
    QRhiTexture *texture = rhi->newTextureArray(QRhiTexture::RGBA8, layers.size(), size, 1,
                                                QRhiTexture::MipMapped);
    if (!MemoryLog::created(texture, "texture array")) {
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
    Entry &entry = store()[handle - 1];
    entry.used = useClock;
    return entry.texture;
}

void evict(unsigned int handle) {
    if (handle == 0 || handle > store().size())
        return;
    Entry &entry = store()[handle - 1];
    if (entry.texture != nullptr)
        retired.push_back(entry.texture);
    entry.texture = nullptr;
    entry.evicted = true;
}

bool evicted(unsigned int handle) {
    return handle != 0 && handle <= store().size() && store()[handle - 1].evicted;
}

qint64 lastUsed(unsigned int handle) {
    return handle != 0 && handle <= store().size() ? store()[handle - 1].used : 0;
}

void setClock(qint64 milliseconds) {
    useClock = milliseconds;
}

std::vector<QRhiTexture *> takeRetired() {
    std::vector<QRhiTexture *> taken;
    taken.swap(retired);
    return taken;
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
    for (QRhiTexture *texture : retired)
        delete texture;
    retired.clear();
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
        retired.push_back(entry.texture);
    entry = Entry();
}

}
