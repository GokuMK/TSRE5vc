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
#include <vector>

namespace {
std::vector<QRhiTexture *> &store() {
    static std::vector<QRhiTexture *> textures;
    return textures;
}
QRhiResourceUpdateBatch *pending = nullptr;
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
    store().push_back(texture);
    return static_cast<unsigned int>(store().size());
}

QRhiTexture *texture(unsigned int handle) {
    if (handle == 0 || handle > store().size())
        return nullptr;
    return store()[handle - 1];
}

void release(unsigned int handle) {
    if (handle == 0 || handle > store().size())
        return;
    QRhiTexture *&texture = store()[handle - 1];
    if (texture != nullptr)
        texture->deleteLater();
    texture = nullptr;
}

}
