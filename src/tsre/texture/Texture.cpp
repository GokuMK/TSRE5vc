/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/texture/Texture.h>
#include <tsre/world/TerrainFileData.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/texture/Brush.h>
#include <tsre/Undo.h>
#include <QOpenGLShaderProgram>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QString>
#include <QDebug>
#include <QColor>
#include <tsre/ogl/GLUU.h>
#include <tsre/Game.h>
#include <cstddef>
#include <cstdint>
#include <tsre/texture/DxtCodec.h>
#include <tsre/texture/TextureAlpha.h>
#include <tsre/texture/AceDocument.h>
#include <tsre/renderer/rhi/RhiTextures.h>
#include <tsre/texture/TexLib.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <cmath>

unsigned Texture::uploads = 0;

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

namespace {

bool supportsDXT1() {
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (ctx == nullptr) {
        return false;
    }

    const bool ok = ctx->hasExtension(QByteArrayLiteral("GL_EXT_texture_compression_s3tc")) ||
                    ctx->hasExtension(QByteArrayLiteral("GL_EXT_texture_compression_dxt1")) ||
                    ctx->hasExtension(QByteArrayLiteral("GL_NV_texture_compression_s3tc")) ||
                    ctx->hasExtension(QByteArrayLiteral("GL_S3_s3tc"));
    return ok;
}

bool supportsS3TCFull() {
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (ctx == nullptr) {
        return false;
    }

    const bool ok = ctx->hasExtension(QByteArrayLiteral("GL_EXT_texture_compression_s3tc")) ||
                    ctx->hasExtension(QByteArrayLiteral("GL_NV_texture_compression_s3tc")) ||
                    ctx->hasExtension(QByteArrayLiteral("GL_S3_s3tc"));
    return ok;
}

bool supportsCompressedFormat(int glFormat) {
    if (glFormat == 0) {
        return false;
    }
    if (glFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ||
        glFormat == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
        return supportsDXT1();
    }
    if (glFormat == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT ||
        glFormat == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) {
        return supportsS3TCFull();
    }
    return false;
}

int dxtBlockBytes(int glFormat) {
    if (glFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ||
        glFormat == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
        return 8;
    }
    if (glFormat == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT ||
        glFormat == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) {
        return 16;
    }
    return 0;
}

DxtCodec::Format codecFormat(int format) {
    return format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT   ? DxtCodec::Format::Dxt3
           : format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT ? DxtCodec::Format::Dxt5
                                                        : DxtCodec::Format::Dxt1;
}

static bool decodeCompressedToImageData(Texture *texture) {
    if (!texture || !dxtBlockBytes(texture->compressedGLFormat))
        return false;
    QByteArray pixels;
    QString error;
    if (!DxtCodec::decode(texture->compressedData, texture->width, texture->height,
                          codecFormat(texture->compressedGLFormat), texture->type == GL_RGBA,
                          pixels, error)) {
        texture->errorMessage = error;
        return false;
    }
    texture->bytesPerPixel = texture->type == GL_RGBA ? 4 : 3;
    texture->bpp = texture->bytesPerPixel * 8;
    texture->imageSize = pixels.size();
    delete[] texture->imageData;
    texture->imageData = new unsigned char[texture->imageSize];
    memcpy(texture->imageData, pixels.constData(), pixels.size());
    return true;
}

// Tight CPU rows must not inherit another caller's pixel-transfer state.
struct PixelRows {
    bool pack;
    GLint alignment = 4, length = 0, rows = 0, pixels = 0, buffer = 0;
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    explicit PixelRows(bool readback = false) : pack(readback) {
        glGetIntegerv(pack ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT, &alignment);
        glGetIntegerv(pack ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH, &length);
        glGetIntegerv(pack ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS, &rows);
        glGetIntegerv(pack ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS, &pixels);
        glGetIntegerv(pack ? GL_PIXEL_PACK_BUFFER_BINDING : GL_PIXEL_UNPACK_BUFFER_BINDING,
                      &buffer);
        f->glBindBuffer(pack ? GL_PIXEL_PACK_BUFFER : GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(pack ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(pack ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(pack ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(pack ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS, 0);
    }
    ~PixelRows() {
        glPixelStorei(pack ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT, alignment);
        glPixelStorei(pack ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH, length);
        glPixelStorei(pack ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS, rows);
        glPixelStorei(pack ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS, pixels);
        f->glBindBuffer(pack ? GL_PIXEL_PACK_BUFFER : GL_PIXEL_UNPACK_BUFFER, buffer);
    }
};

void beginPixelTransfer() {
    // Isolate errors from this transfer without attributing another renderer's
    // stale error to the ACE file. Never spin on a lost context.
    for (int i = 0; i < 16; ++i) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR)
            return;
        qWarning() << "Texture: pre-existing OpenGL error" << Qt::hex << error;
    }
}

bool pixelTransferSucceeded(Texture &texture) {
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR)
        return true;
    texture.errorMessage = QString("OpenGL texture transfer failed (0x%1)").arg(error, 0, 16);
    texture.error = true;
    return false;
}

// Read a resident texture into the destination's already allocated CPU pixels.
// Some AMD drivers expand DXT3 alpha to n*16 in glGetTexImage, while shader
// sampling correctly uses n*17. Keep rendering compressed; bypass only this
// readback decompression path. Other GPU formats retain ordinary readback.
bool readGpuPixels(const Texture &source, Texture &destination) {
    beginPixelTransfer();
    PixelRows rows(true);
    glBindTexture(GL_TEXTURE_2D, source.tex[0]);
    if (source.gpuInternalFormat != GL_COMPRESSED_RGBA_S3TC_DXT3_EXT) {
        glGetTexImage(GL_TEXTURE_2D, 0, destination.type, GL_UNSIGNED_BYTE, destination.imageData);
        return pixelTransferSucceeded(destination);
    }
    using ReadCompressed = void (QOPENGLF_APIENTRYP)(GLenum, GLint, void *);
    const auto readCompressed = reinterpret_cast<ReadCompressed>(
        QOpenGLContext::currentContext()->getProcAddress("glGetCompressedTexImage"));
    GLint size = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &size);
    if (!pixelTransferSucceeded(destination)) return false;
    const qint64 expected = DxtCodec::byteSize(source.width, source.height, DxtCodec::Format::Dxt3);
    if (!readCompressed || expected <= 0 || size != expected) {
        destination.error = true;
        destination.errorMessage = "Cannot read resident DXT3 blocks: unavailable API or invalid size";
        return false; // Do not silently fall back to the faulty alpha decompression path.
    }
    QByteArray blocks(size, Qt::Uninitialized);
    readCompressed(GL_TEXTURE_2D, 0, blocks.data());
    if (!pixelTransferSucceeded(destination)) return false;
    if (!DxtCodec::decodeInto(blocks, source.width, source.height, DxtCodec::Format::Dxt3,
                              destination.type == GL_RGBA, destination.imageData,
                              destination.imageSize, destination.errorMessage)) {
        destination.error = true;
        return false;
    }
    return true;
}

// The same for a texture the QRhi renderer holds: its RGBA8 rows into the
// destination's 3 or 4 bytes a pixel, or its blocks decoded.
bool readRhiPixels(const Texture &source, Texture &destination) {
    if (source.tex == nullptr || source.tex[0] == 0)
        return false;
    QByteArray data;
    const RhiTextures::Contents contents = RhiTextures::readBack(source.tex[0], data);
    const qsizetype pixels = qsizetype(source.width) * source.height;
    if (contents == RhiTextures::Contents::Rgba8) {
        if (data.size() != pixels * 4 || destination.imageSize != pixels * destination.bytesPerPixel)
            return false;
        const auto *in = reinterpret_cast<const unsigned char *>(data.constData());
        if (destination.bytesPerPixel == 4) {
            std::memcpy(destination.imageData, in, size_t(pixels) * 4);
        } else {
            for (qsizetype i = 0; i < pixels; ++i)
                std::memcpy(destination.imageData + i * 3, in + i * 4, 3);
        }
        return true;
    }
    if (contents == RhiTextures::Contents::None) {
        destination.errorMessage = "Cannot read the texture back from the QRhi renderer";
        return false;
    }
    const DxtCodec::Format format = contents == RhiTextures::Contents::Bc1 ? DxtCodec::Format::Dxt1
            : contents == RhiTextures::Contents::Bc2 ? DxtCodec::Format::Dxt3
                                                     : DxtCodec::Format::Dxt5;
    if (!DxtCodec::decodeInto(data, source.width, source.height, format,
                              destination.type == GL_RGBA, destination.imageData,
                              destination.imageSize, destination.errorMessage)) {
        destination.error = true;
        return false;
    }
    return true;
}

// Reads a resident texture back with the renderer that holds it.
bool readResidentPixels(const Texture &source, Texture &destination) {
    return Game::renderBackend == "qrhi" ? readRhiPixels(source, destination)
                                         : readGpuPixels(source, destination);
}

// Whether a resident texture can be read back: QRhi needs no current
// OpenGL context.
bool canReadResident(const Texture &texture) {
    return texture.glLoaded && texture.tex != nullptr
            && (Game::renderBackend == "qrhi" || QOpenGLContext::currentContext() != nullptr);
}

// A prepared texture's level 0 as its bytesPerPixel pixels.
bool preparedPixels(const Texture &texture, QByteArray &pixels) {
    if (!texture.prepared || texture.preparedLevels.isEmpty())
        return false;
    const qsizetype count = qsizetype(texture.width) * texture.height;
    const QByteArray &level = texture.preparedLevels.first();
    if (texture.preparedFormat != 0) {
        QString message;
        return DxtCodec::decode(level, texture.width, texture.height, codecFormat(texture.preparedFormat),
                                texture.bytesPerPixel == 4, pixels, message)
                && pixels.size() == count * texture.bytesPerPixel;
    }
    if (level.size() != count * 4)
        return false;
    if (texture.bytesPerPixel == 4) {
        pixels = level;
        return true;
    }
    pixels.resize(count * 3);
    for (qsizetype i = 0; i < count; ++i)
        std::memcpy(pixels.data() + i * 3, level.constData() + i * 4, 3);
    return true;
}

} // namespace

Texture::Texture() {}

bool Texture::decodeToCpu() {
    if (released)
        TexLib::reloadNow(this);
    bool ok =
        imageData != nullptr || (!compressedData.isEmpty() && decodeCompressedToImageData(this));
    if (ok)
        editable = true;
    return ok;
}

Texture::Texture(QString pathid) {
    this->pathid = pathid;
    this->hashid.push_back(ContentPath::key(pathid));
    // temp fix for dds/ace loading
    //  Openrails uses .dds textures instead of .ace
    QString tType = pathid.toLower().split(".").last();
    if (tType == "dds") {
        hashid.push_back(ContentPath::key(ContentPath::withExtension(pathid, "ace")));
    }
}

Texture::Texture(int x, int y, int bpp, Brush *brush) {
    width = x;
    height = y;
    this->bpp = bpp;
    bytesPerPixel = (bpp / 8);
    imageSize = (bytesPerPixel * width * height);
    imageData = new unsigned char[imageSize];
    std::fill(imageData, imageData + imageSize, 255);
    if (bpp == 24) {
        type = GL_RGB;
    } else {
        type = GL_RGBA;
    }

    editable = true;
    loaded = true;
}

Texture::Texture(const Texture *orig) {
    if (!orig || !orig->loaded)
        return;
    width = orig->width;
    height = orig->height;
    bpp = orig->bpp;
    type = orig->type;
    bytesPerPixel = orig->bytesPerPixel;
    imageSize = width * height * bytesPerPixel;
    QByteArray decoded;
    QString message;
    if (orig->imageData) {
        imageData = new unsigned char[imageSize];
        memcpy(imageData, orig->imageData, imageSize);
    } else if (!orig->compressedData.isEmpty() &&
               DxtCodec::decode(orig->compressedData, width, height,
                                codecFormat(orig->compressedGLFormat), type == GL_RGBA, decoded,
                                message)) {
        imageData = new unsigned char[imageSize];
        memcpy(imageData, decoded.constData(), imageSize);
    } else if (preparedPixels(*orig, decoded)) {
        imageData = new unsigned char[imageSize];
        memcpy(imageData, decoded.constData(), imageSize);
    } else if (canReadResident(*orig)) {
        imageData = new unsigned char[imageSize];
        if (!readResidentPixels(*orig, *this)) {
            delete[] imageData;
            imageData = nullptr;
            return;
        }
    } else {
        error = true;
        errorMessage = "Cannot clone texture pixels without a source or GL context";
        return;
    }
    if (orig->aceMetadata)
        aceMetadata = std::make_shared<AceMetadata>(*orig->aceMetadata);
    editable = loaded = true;
    gpuMipmaps = orig->gpuMipmaps;
    if (QOpenGLContext::currentContext())
        update();
}

void Texture::setEditable() {
    // Only displayed by its requesters: its source went with the preparation
    // for the GPU, and nothing edits it.
    if (displayOnly)
        return;
    if (released)
        TexLib::reloadNow(this);
    if (!loaded)
        return;
    // Edited, it no longer matches its file: never released for memory.
    reloadable = false;
    if (decodeToCpu())
        return;
    if (!canReadResident(*this))
        return;
    imageSize = width * height * bytesPerPixel;
    imageData = new unsigned char[imageSize];
    if (!readResidentPixels(*this, *this)) {
        delete[] imageData;
        imageData = nullptr;
        return;
    }
    editable = true;
}

void Texture::pixelsChanged() {
    reloadable = false;
    sourceMipmaps.clear();
    compressedData.clear();
    compressedGLFormat = 0;
    aceDocument.reset(); // Its source pixels are no longer the edited document.
    imageSize = width * height * bytesPerPixel;
    bpp = bytesPerPixel * 8;
}

void Texture::classifyAlpha() {
    TextureAlpha::Class alpha = TextureAlpha::Unknown;
    if (!compressedData.isEmpty() && compressedGLFormat != 0)
        alpha = TextureAlpha::ofBlocks(compressedData, compressedGLFormat);
    else if (imageData != nullptr && width > 0 && height > 0)
        alpha = TextureAlpha::ofPixels(imageData, qsizetype(width) * height, bytesPerPixel);
    alphaClass.store(alpha);
}

void Texture::takeContentFrom(Texture &other) {
    if (this == &other)
        return;
    const bool ready = other.loaded.load();
    loaded = false;
    // CPU reloads keep the old GL name for the next upload. In particular, the
    // disk worker must not discard a resident handle without a GL context.
    unsigned int *reusable = other.tex ? nullptr : tex;
    if (reusable)
        tex = nullptr;
    // Moving an already GPU-resident source still requires the owning context.
    if (tex && tex[0] && Game::renderBackend == "qrhi")
        RhiTextures::release(tex[0]);
    else if (tex && tex[0] && QOpenGLContext::currentContext())
        glDeleteTextures(1, tex);
    delete[] tex;
    tex = nullptr;
    delete[] imageData;
    imageData = nullptr;
    width = other.width;
    height = other.height;
    bpp = other.bpp;
    imageSize = other.imageSize;
    bytesPerPixel = other.bytesPerPixel;
    compressed = other.compressed;
    type = other.type;
    typk = other.typk;
    compressedData = std::move(other.compressedData);
    compressedGLFormat = other.compressedGLFormat;
    alphaClass.store(other.alphaClass.load());
    gpuInternalFormat = other.gpuInternalFormat;
    sourceMipmaps = std::move(other.sourceMipmaps);
    prepared = other.prepared;
    preparedFormat = other.preparedFormat;
    preparedLevels = std::move(other.preparedLevels);
    other.prepared = false;
    aceMetadata = std::move(other.aceMetadata);
    aceDocument = std::move(other.aceDocument);
    gpuMipmaps = other.gpuMipmaps;
    gpuMipLevels = other.gpuMipLevels;
    imageData = other.imageData;
    other.imageData = nullptr;
    tex = other.tex ? other.tex : reusable;
    other.tex = nullptr;
    glLoaded = other.glLoaded;
    editable = other.editable;
    missing = other.missing;
    error = other.error;
    errorMessage = std::move(other.errorMessage);
    other.loaded = other.glLoaded = other.editable = false;
    other.compressedGLFormat = other.gpuInternalFormat = 0;
    other.gpuMipmaps = false;
    other.gpuMipLevels = 1;
    loaded.store(ready);
}

unsigned char *Texture::getImageData(int width, int height) {
    // A display-only texture is read from a copy: its pixels, its prepared
    // level 0, or the GPU's.
    QByteArray copy;
    const unsigned char *imageData = this->imageData;
    if (displayOnly && imageData == nullptr) {
        if (!readPixels(copy))
            return nullptr;
        imageData = reinterpret_cast<const unsigned char *>(copy.constData());
    } else {
        if (!editable)
            setEditable();
        imageData = this->imageData;
    }

    if (!imageData || width <= 0 || height <= 0 || qint64(width) * height > 64 * 1024 * 1024)
        return nullptr;
    // qDebug() << width << height << bytesPerPixel;
    unsigned char *out = new unsigned char[width * height * bytesPerPixel];

    float scalew = (float)this->width / width;
    float scaleh = (float)this->height / height;

    qDebug() << this->width << " " << this->height;

    int lineWidth = (this->width * bytesPerPixel);
    // if( lineWidth%4 !=0)
    //     lineWidth = lineWidth + 4 - lineWidth%4;
    // lineWidth /= 4;
    // if(lineWidth*4 < this->width*bytesPerPixel)
    //     lineWidth = lineWidth*4+4;
    // else
    //     lineWidth = lineWidth*4;

    for (int i = 0; i < height; i++)
        for (int j = 0; j < width; j++) {
            int wsi = scaleh * i;
            int hsi = scalew * j;
            out[i * width * bytesPerPixel + j * bytesPerPixel + 0] =
                imageData[wsi * lineWidth + hsi * bytesPerPixel + 0];
            out[i * width * bytesPerPixel + j * bytesPerPixel + 1] =
                imageData[wsi * lineWidth + hsi * bytesPerPixel + 1];
            out[i * width * bytesPerPixel + j * bytesPerPixel + 2] =
                imageData[wsi * lineWidth + hsi * bytesPerPixel + 2];
            if (bytesPerPixel == 4)
                out[i * width * bytesPerPixel + j * bytesPerPixel + 3] =
                    imageData[wsi * lineWidth + hsi * bytesPerPixel + 3];
        }

    return out;
}

void Texture::advancedCrop(const TerrainFile::PatchUv &uv, int patchSamples, int w, int h) {
    if (!editable)
        setEditable();
    if (!imageData || patchSamples<=0)
        return;
    if (w == 0)
        w = width;
    if (h == 0)
        h = height;
    if (w <= 0 || h <= 0 || qint64(w) * h > 64 * 1024 * 1024)
        return;
    for (const float value : {uv.x,uv.y,uv.w,uv.b,uv.c,uv.h})
        if (!std::isfinite(value))
            return;
    unsigned char *next = new unsigned char[qsizetype(w) * h * bytesPerPixel];
    // The stored transform consumes patch-local sample coordinates, not meters.
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double u =
                (uv.x + double(patchSamples) * (uv.w * x / w + uv.b * y / h)) * width;
            const double v =
                (uv.y + double(patchSamples) * (uv.c * x / w + uv.h * y / h)) * height;
            const int sx = int(std::fmod(std::fmod(u, width) + width, width));
            const int sy = int(std::fmod(std::fmod(v, height) + height, height));
            memcpy(next + (qsizetype(y) * w + x) * bytesPerPixel,
                   imageData + (qsizetype(sy) * width + sx) * bytesPerPixel, bytesPerPixel);
        }
    delete[] imageData;
    imageData = next;
    width = w;
    height = h;
    update();
}

void Texture::crop(float x1, float y1, float x2, float y2) {
    if (!editable)
        setEditable();
    if (!imageData || x1 == x2 || y1 == y2 || (x1 < x2 && y1 < y2))
        return;
    const bool halfTurn = x1 > x2 && y1 > y2;
    const int newWidth = halfTurn ? width : height, newHeight = halfTurn ? height : width;
    unsigned char *next = new unsigned char[qsizetype(newWidth) * newHeight * bytesPerPixel];
    for (int y = 0; y < newHeight; ++y)
        for (int x = 0; x < newWidth; ++x) {
            const int sx = halfTurn ? width - 1 - x : x1 > x2 ? width - 1 - y : y;
            const int sy = halfTurn ? height - 1 - y : x1 > x2 ? x : height - 1 - x;
            memcpy(next + (qsizetype(y) * newWidth + x) * bytesPerPixel,
                   imageData + (qsizetype(sy) * width + sx) * bytesPerPixel, bytesPerPixel);
        }
    delete[] imageData;
    imageData = next;
    width = newWidth;
    height = newHeight;
    update();
}

void Texture::sendToUndo(int id) {
    if (!editable)
        setEditable();
    if (imageData)
        Undo::PushTextureData(id, imageData, bytesPerPixel * width * height);
}

void Texture::fillData(unsigned char *data) {
    if (imageData == NULL)
        return;
    memcpy(imageData, data, bytesPerPixel * width * height);
    update();
}

void Texture::paint(Brush *brush, float x, float z) {
    if (!editable)
        setEditable();
    if (!brush || !imageData)
        return;
    pixelsChanged();

    Texture *tex = brush->tex;

    if (tex != NULL) {
        // The brush's texture, released for memory, comes back first.
        if (tex->released)
            TexLib::reloadNow(tex);
        if (!tex->loaded)
            tex = NULL;
        else if (!tex->editable)
            tex->setEditable();
        if (tex && !tex->imageData)
            tex = nullptr;
    }

    int tx = x * width;
    int tz = z * height;

    int txi, tzj;

    float talpha = 0;

    int size = (brush->size * this->width) / 512;
    if (size < 1)
        size = 1;
    // size = (size/512);

    for (int i = -size; i < size; i++)
        for (int j = -size; j < size; j++) {
            txi = tx + i;
            tzj = tz + j;
            if (tx + i >= height)
                continue;
            if (tz + j >= width)
                continue;
            if (tx + i < 0)
                continue;
            if (tz + j < 0)
                continue;
            // if(sqrt(i*i + j*j) > size) continue;

            talpha = (brush->alpha) * brush->getAlpha(i, j, size);
            // talpha = (brush->alpha)*(1.0-(float)sqrt(i*i + j*j)/size);
            txi *= 1;
            tzj *= 1;

            if (tex != NULL && brush->useTexture) {

                if (tzj >= tex->width) {
                    tzj = tzj % tex->width;
                }
                if (txi >= tex->height) {
                    txi = txi % tex->height;
                }

                imageData[(tx + i) * width * bytesPerPixel + (tz + j) * bytesPerPixel] =
                    (1 - talpha) *
                        imageData[(tx + i) * width * bytesPerPixel + (tz + j) * bytesPerPixel] +
                    (talpha)*tex->imageData[(txi)*tex->width * tex->bytesPerPixel +
                                            (tzj)*tex->bytesPerPixel];
                imageData[(tx + i) * width * bytesPerPixel + (tz + j) * bytesPerPixel + 1] =
                    (1 - talpha) *
                        imageData[(tx + i) * width * bytesPerPixel + (tz + j) * bytesPerPixel + 1] +
                    (talpha)*tex->imageData[(txi)*tex->width * tex->bytesPerPixel +
                                            (tzj)*tex->bytesPerPixel + 1];
                imageData[(tx + i) * width * bytesPerPixel + (tz + j) * bytesPerPixel + 2] =
                    (1 - talpha) *
                        imageData[(tx + i) * width * bytesPerPixel + (tz + j) * bytesPerPixel + 2] +
                    (talpha)*tex->imageData[(txi)*tex->width * tex->bytesPerPixel +
                                            (tzj)*tex->bytesPerPixel + 2];
            } else {
                imageData[(tx + i) * width * bytesPerPixel + (tz + j) * bytesPerPixel] =
                    (1 - talpha) *
                        imageData[(tx + i) * width * bytesPerPixel + (tz + j) * bytesPerPixel] +
                    (talpha) * (brush->color[0]);
                imageData[(tx + i) * width * bytesPerPixel + (tz + j) * bytesPerPixel + 1] =
                    (1 - talpha) *
                        imageData[(tx + i) * width * bytesPerPixel + (tz + j) * bytesPerPixel + 1] +
                    (talpha) * (brush->color[1]);
                imageData[(tx + i) * width * bytesPerPixel + (tz + j) * bytesPerPixel + 2] =
                    (1 - talpha) *
                        imageData[(tx + i) * width * bytesPerPixel + (tz + j) * bytesPerPixel + 2] +
                    (talpha) * (brush->color[2]);
            }
        }
}

// An edited texture's pixels to the QRhi renderer's texture. Its CPU pixels
// stay for the next edit, as OpenGL's update keeps them (the upload of a
// loaded texture drops them, and every stroke read them back and was not
// shown). Level 0 is replaced in place when the texture is RGBA of its size,
// else it is made again.
static void updateEditedForRhi(Texture &t) {
    if (t.width <= 0 || t.height <= 0 || (t.bytesPerPixel != 3 && t.bytesPerPixel != 4))
        return;
    const qsizetype count = qsizetype(t.width) * t.height;
    QByteArray rgba;
    if (t.bytesPerPixel == 4) {
        rgba = QByteArray(reinterpret_cast<const char *>(t.imageData), count * 4);
    } else {
        rgba.resize(count * 4);
        unsigned char *out = reinterpret_cast<unsigned char *>(rgba.data());
        for (qsizetype i = 0; i < count; ++i) {
            std::memcpy(out + i * 4, t.imageData + i * 3, 3);
            out[i * 4 + 3] = 255;
        }
    }
    const unsigned int old = t.tex != nullptr ? t.tex[0] : 0;
    if (old != 0 && t.glLoaded && RhiTextures::replaceLevel0(old, t.width, t.height, rgba)) {
        ++Texture::uploads;
        return;
    }
    const unsigned int handle = RhiTextures::create(t.width, t.height, {rgba});
    if (handle == 0)
        return;
    const bool clamp = old != 0 && RhiTextures::clampedToEdge(old);
    if (old != 0)
        RhiTextures::release(old);
    if (t.tex == nullptr)
        t.tex = new unsigned int[1]{};
    t.tex[0] = handle;
    RhiTextures::setSampling(handle, t.gpuMipmaps, clamp);
    t.gpuInternalFormat = GL_RGBA8;
    t.gpuMipLevels = 1;
    for (int n = std::max(t.width, t.height); n > 1; n >>= 1)
        ++t.gpuMipLevels;
    t.glLoaded = true;
    ++Texture::uploads;
}

void Texture::update() {
    if (!imageData)
        return;
    pixelsChanged();
    if (Game::renderBackend == "qrhi") {
        updateEditedForRhi(*this);
        return;
    }
    if (!QOpenGLContext::currentContext())
        return;
    auto *f = QOpenGLContext::currentContext()->functions();
    beginPixelTransfer();
    if (!tex) {
        tex = new unsigned int[1]{};
        glGenTextures(1, tex);
    }
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    PixelRows rows;
    const int internal = type == GL_RGBA ? GL_RGBA8 : GL_RGB8;
    glTexImage2D(GL_TEXTURE_2D, 0, internal, width, height, 0, type, GL_UNSIGNED_BYTE, imageData);
    gpuInternalFormat = internal;
    glLoaded = true;
    ++uploads;
    gpuMipLevels = 1;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);
    if (gpuMipmaps) {
        f->glGenerateMipmap(GL_TEXTURE_2D);
        for (int n = std::max(width, height); n > 1; n >>= 1)
            ++gpuMipLevels;
    } else
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    gpuMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (!pixelTransferSucceeded(*this))
        glLoaded = false;
}

void Texture::updateRegion(int x, int y, int w, int h, const unsigned char *pixels) {
    if (!glLoaded || tex == nullptr || bytesPerPixel != 4 || w <= 0 || h <= 0)
        return;
    if (Game::renderBackend == "qrhi") {
        RhiTextures::updateRegion(tex[0], x, y, w, h,
                                  QByteArray(reinterpret_cast<const char *>(pixels), qsizetype(w) * h * 4));
        return;
    }
    if (!QOpenGLContext::currentContext())
        return;
    PixelRows rows;
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

Texture::~Texture() {
    // Existing callers own/free the legacy raw pointers. New containers are RAII.
}

// QRhi renderer: the pixels go to an RhiTextures texture, whose handle takes
// the place of the OpenGL texture name. The texture always has all mipmap
// levels; gpuMipmaps tells the renderer whether to sample them, as an OpenGL
// texture uploaded without mipmaps has none.
// QRhi renderer: DXT data goes to the GPU as it is, as GLTextures uploads it
// to OpenGL. DXT1 with alpha has no QRhi format and goes as BC3 (twice its
// size, a quarter of decoded RGBA); a chain of mipmaps that stops before 1x1
// when mipmaps are wanted is decoded.
// The levels of a DXT texture for the GPU (no QRhi call: loader threads too).
static bool compressedLevels(const Texture &t, bool mipmaps, bool blocksSupported,
                             QVector<QByteArray> &levels, int &gpuFormat, bool keepPartial = false) {
    const int format = t.compressedGLFormat;
    if (t.compressedData.isEmpty() || !dxtBlockBytes(format) ||
        t.compressedData.size() != DxtCodec::byteSize(t.width, t.height, codecFormat(format)) ||
        !blocksSupported)
        return false;
    const DxtCodec::Format codec = codecFormat(format);
    levels = {t.compressedData};
    int previousW = t.width, previousH = t.height;
    for (const TextureMip &mip : std::as_const(t.sourceMipmaps)) {
        if (mip.width != std::max(1, previousW / 2) || mip.height != std::max(1, previousH / 2))
            break;
        QByteArray blocks = mip.data;
        if (mip.compressedFormat != format) {
            if (mip.compressedFormat)
                break;
            // ACE tiny tails are planar: encode only these tiny levels, as
            // GLTextures does.
            QString message;
            if (!DxtCodec::encode(reinterpret_cast<const unsigned char *>(mip.data.constData()),
                                  mip.data.size(), mip.width, mip.height, t.bytesPerPixel, codec,
                                  t.type == GL_RGBA, blocks, message))
                break;
        }
        if (blocks.size() != DxtCodec::byteSize(mip.width, mip.height, codec))
            break;
        levels.push_back(blocks);
        previousW = mip.width;
        previousH = mip.height;
    }
    int fullChain = 1;
    for (int n = std::max(t.width, t.height); n > 1; n >>= 1)
        ++fullChain;
    if (levels.size() < fullChain && !keepPartial) {
        if (mipmaps)
            return false;
        levels.resize(1);
    }
    gpuFormat = format;
    if (format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
        int w = t.width, h = t.height;
        for (QByteArray &level : levels) {
            if (!DxtCodec::dxt1ToBc3(level, level))
                return false;
            // Its cutout edges filter without a halo: the transparent texels
            // coloured as the opaque ones beside them.
            DxtCodec::bleedBc3Transparent(level, w, h);
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        }
        gpuFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    }
    return true;
}

// The GPU texture from its levels (render thread): block-compressed in
// gpuFormat, or RGBA8 (0); an evicted handle comes back, its sampling kept.
// The CPU source goes.
static bool createRhiTexture(Texture &t, const QVector<QByteArray> &levels, int gpuFormat, bool mipmaps) {
    const unsigned int reuse = t.tex != nullptr && RhiTextures::evicted(t.tex[0]) ? t.tex[0] : 0;
    const bool clamp = reuse != 0 && RhiTextures::clampedToEdge(reuse);
    if (t.tex != nullptr && reuse == 0)
        RhiTextures::release(t.tex[0]);
    unsigned int handle = 0;
    if (gpuFormat == 0) {
        handle = RhiTextures::create(t.width, t.height, levels, reuse);
    } else {
        const RhiTextures::Blocks blocks = gpuFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT
                ? RhiTextures::Blocks::Bc1
                : gpuFormat == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT ? RhiTextures::Blocks::Bc2
                                                                : RhiTextures::Blocks::Bc3;
        handle = RhiTextures::createCompressed(t.width, t.height, blocks, levels, reuse);
    }
    if (handle == 0)
        return false;
    if (t.tex == nullptr)
        t.tex = new unsigned int[1]{};
    t.tex[0] = handle;
    RhiTextures::setSampling(handle, mipmaps, clamp);
    t.gpuInternalFormat = gpuFormat == 0 ? GL_RGBA8 : gpuFormat;
    t.gpuMipmaps = mipmaps;
    if (gpuFormat == 0) {
        t.gpuMipLevels = 1;
        for (int n = std::max(t.width, t.height); n > 1; n >>= 1)
            ++t.gpuMipLevels;
    } else {
        t.gpuMipLevels = int(levels.size());
    }
    delete[] t.imageData;
    t.imageData = nullptr;
    t.editable = false;
    t.compressedData.clear();
    t.compressedGLFormat = 0;
    t.sourceMipmaps.clear();
    t.prepared = false;
    t.preparedLevels.clear();
    t.glLoaded = true;
    ++Texture::uploads;
    return true;
}

static bool uploadCompressedForRhi(Texture &t, bool mipmaps) {
    QVector<QByteArray> levels;
    int gpuFormat = 0;
    return compressedLevels(t, mipmaps, RhiTextures::supportsBlocks(), levels, gpuFormat)
            && createRhiTexture(t, levels, gpuFormat, mipmaps);
}

// The mipmap levels below an RGBA image, each filtered from the level above
// it, its alpha scaled so as many texels reach the cutoff as in the image:
// cutout foliage keeps its coverage far away instead of falling under the
// cutoff as averaging blurs it (Castano's coverage-preserving mipmaps).
static QVector<QByteArray> coverageMipmaps(const QByteArray &image, int width, int height, float cutoff) {
    const float limit = cutoff * 255.0f;
    auto coverage = [limit](const QByteArray &rgba, float scale) {
        const unsigned char *p = reinterpret_cast<const unsigned char *>(rgba.constData());
        const qsizetype count = rgba.size() / 4;
        qsizetype passed = 0;
        for (qsizetype i = 0; i < count; ++i)
            if (p[i * 4 + 3] * scale >= limit)
                ++passed;
        return count > 0 ? double(passed) / double(count) : 0.0;
    };
    const double target = coverage(image, 1.0f);
    QVector<QByteArray> levels;
    QByteArray above = image;  // unscaled
    int w = width, h = height;
    while (w > 1 || h > 1) {
        const int levelW = std::max(1, w / 2), levelH = std::max(1, h / 2);
        QByteArray level(qsizetype(levelW) * levelH * 4, 0);
        const unsigned char *src = reinterpret_cast<const unsigned char *>(above.constData());
        unsigned char *dst = reinterpret_cast<unsigned char *>(level.data());
        for (int y = 0; y < levelH; ++y)
            for (int x = 0; x < levelW; ++x) {
                const int x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1);
                const int y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
                for (int c = 0; c < 4; ++c) {
                    const int sum = src[(qsizetype(y0) * w + x0) * 4 + c] + src[(qsizetype(y0) * w + x1) * 4 + c]
                            + src[(qsizetype(y1) * w + x0) * 4 + c] + src[(qsizetype(y1) * w + x1) * 4 + c];
                    dst[(qsizetype(y) * levelW + x) * 4 + c] = static_cast<unsigned char>((sum + 2) / 4);
                }
            }
        // The alpha scale reaching the image's coverage, by bisection.
        float scale = 1.0f;
        if (target > 0.0) {
            float low = 0.0f, high = 8.0f;
            for (int i = 0; i < 16; ++i) {
                const float middle = 0.5f * (low + high);
                if (coverage(level, middle) < target)
                    low = middle;
                else
                    high = middle;
            }
            scale = high;
        }
        QByteArray scaled = level;
        unsigned char *out = reinterpret_cast<unsigned char *>(scaled.data());
        for (qsizetype i = 0; i < scaled.size() / 4; ++i)
            out[i * 4 + 3] = static_cast<unsigned char>(std::min(255.0f, out[i * 4 + 3] * scale + 0.5f));
        levels.push_back(scaled);
        above = level;
        w = levelW;
        h = levelH;
    }
    return levels;
}

// A cutout image's transparent texels (alpha under 128) take a colour from
// the opaque ones, alpha kept (alpha bleeding): beside an opaque texel the
// mean of its opaque neighbours, all bilinear filtering of this level
// reaches; elsewhere the mean of every opaque texel, what mipmaps made from
// this level mix in. MSTS textures keep black or a chroma-key blue there,
// which filtered into dark outlines round alpha-tested leaves (Prudnik's
// birches, BIRKE_01.ACE). Two passes in place: it runs on the render thread,
// as the texture is uploaded.
static void bleedTransparent(QByteArray &rgba, int width, int height) {
    const qsizetype count = qsizetype(width) * height;
    if (width <= 0 || height <= 0 || rgba.size() < count * 4)
        return;
    unsigned char *p = reinterpret_cast<unsigned char *>(rgba.data());
    quint64 sum[3] = {0, 0, 0}, opaque = 0;
    for (qsizetype i = 0; i < count; ++i) {
        const unsigned char *t = p + i * 4;
        if (t[3] >= 128) {
            sum[0] += t[0];
            sum[1] += t[1];
            sum[2] += t[2];
            ++opaque;
        }
    }
    if (opaque == 0 || opaque == quint64(count))
        return;
    unsigned char mean[3];
    for (int c = 0; c < 3; ++c)
        mean[c] = static_cast<unsigned char>(sum[c] / opaque);
    auto opaqueAt = [p, width](int x, int y) {
        return p[(qsizetype(y) * width + x) * 4 + 3] >= 128 ? 1 : 0;
    };
    // Opaque texels of each column in the rows around y (column x at x + 1).
    std::vector<unsigned char> column(size_t(width) + 2, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x)
            column[size_t(x) + 1] = static_cast<unsigned char>(
                (y > 0 ? opaqueAt(x, y - 1) : 0) + opaqueAt(x, y)
                + (y + 1 < height ? opaqueAt(x, y + 1) : 0));
        unsigned char *row = p + qsizetype(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            unsigned char *t = row + x * 4;
            if (t[3] >= 128)
                continue;
            if (column[size_t(x)] + column[size_t(x) + 1] + column[size_t(x) + 2] == 0) {
                std::memcpy(t, mean, 3);
                continue;
            }
            // Only alpha is read around it, never changed: in place is safe.
            int s[3] = {0, 0, 0}, n = 0;
            for (int ny = std::max(0, y - 1); ny <= std::min(height - 1, y + 1); ++ny)
                for (int nx = std::max(0, x - 1); nx <= std::min(width - 1, x + 1); ++nx) {
                    const unsigned char *q = p + (qsizetype(ny) * width + nx) * 4;
                    if (q[3] >= 128) {
                        s[0] += q[0];
                        s[1] += q[1];
                        s[2] += q[2];
                        ++n;
                    }
                }
            for (int c = 0; c < 3; ++c)
                t[c] = static_cast<unsigned char>((s[c] + n / 2) / n);
        }
    }
}

// The RGBA levels of a texture for the GPU (no QRhi call: loader threads
// too): its pixels (decoded from its blocks when needed), bled when a cutout,
// then with mipmaps its own mipmaps (or coverage-preserving ones).
static bool rgbaLevels(Texture &t, bool mipmaps, QVector<QByteArray> &levels) {
    if (!t.decodeToCpu())
        return false;
    const int width = t.width, height = t.height, bytesPerPixel = t.bytesPerPixel;
    unsigned char *imageData = t.imageData;
    if (Game::AASamples > 0 && Game::AARemoveBorder && t.type == GL_RGBA) {
        for (int y = 0; y < height; ++y) {
            imageData[(qsizetype(y) * width) * 4 + 3] = 0;
            imageData[(qsizetype(y) * width + width - 1) * 4 + 3] = 0;
        }
        for (int x = 0; x < width; ++x) {
            imageData[x * 4 + 3] = 0;
            imageData[(qsizetype(height - 1) * width + x) * 4 + 3] = 0;
        }
        t.sourceMipmaps.clear();
    }
    // RGBA levels: the base, then the texture's own mipmaps when they chain.
    auto toRgba = [bytesPerPixel](const unsigned char *pixels, int w, int h) {
        QByteArray rgba(qsizetype(w) * h * 4, char(255));
        if (bytesPerPixel == 4) {
            std::memcpy(rgba.data(), pixels, rgba.size());
        } else {
            for (qsizetype i = 0; i < qsizetype(w) * h; ++i)
                std::memcpy(rgba.data() + i * 4, pixels + i * 3, 3);
        }
        return rgba;
    };
    levels.clear();
    levels.push_back(toRgba(imageData, width, height));
    // A cutout's levels bled (bleedTransparent): the base before mipmaps
    // are made from it, the texture's own mipmaps each.
    const bool cutout = bytesPerPixel == 4 && t.alphaClass.load() == TextureAlpha::Binary;
    if (cutout)
        bleedTransparent(levels.first(), width, height);
    if (mipmaps && t.coverageCutoff >= 0.0f && bytesPerPixel == 4) {
        levels += coverageMipmaps(levels.first(), width, height, t.coverageCutoff);
    } else if (mipmaps) {
        int previousW = width, previousH = height;
        for (const TextureMip &mip : std::as_const(t.sourceMipmaps)) {
            if (mip.width != std::max(1, previousW / 2) || mip.height != std::max(1, previousH / 2))
                break;
            QByteArray decoded;
            QString message;
            const QByteArray *data = &mip.data;
            if (mip.compressedFormat) {
                if (!DxtCodec::decode(mip.data, mip.width, mip.height,
                                      codecFormat(mip.compressedFormat), t.type == GL_RGBA,
                                      decoded, message))
                    break;
                data = &decoded;
            }
            if (data->size() != qsizetype(mip.width) * mip.height * bytesPerPixel)
                break;
            levels.push_back(toRgba(reinterpret_cast<const unsigned char *>(data->constData()),
                                    mip.width, mip.height));
            if (cutout)
                bleedTransparent(levels.last(), mip.width, mip.height);
            previousW = mip.width;
            previousH = mip.height;
        }
    }
    return true;
}

// Set by readMipmapSettings (TexLib.cpp, with the settings).
std::atomic<bool> Texture::shapeMipmaps{false};
std::atomic<int> Texture::mipmapSource{int(Texture::MipmapSource::Improve)};
float Texture::lodBias = 0.0f;

namespace {
// sRGB to linear light and back, for averaging colours.
struct SrgbTables {
    float toLinear[256];
    unsigned char toSrgb[4096];
    SrgbTables() {
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            toLinear[i] = float(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        }
        for (int i = 0; i < 4096; ++i) {
            const double l = i / 4095.0;
            const double c = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
            toSrgb[i] = static_cast<unsigned char>(std::clamp(c * 255.0 + 0.5, 0.0, 255.0));
        }
    }
};
const SrgbTables &srgb() {
    static const SrgbTables tables;
    return tables;
}

// Alpha-tested shape materials cut below 0.51 (vertex alpha -0.51): texels
// counted as passing, and scaled to pass, reach the next whole byte above.
// At 0.5 a file's own 0/255 levels scaled to keep coverage became 128, under
// the shaders' test: whole canopies gone.
constexpr float ShapeCutoff = 131.0f / 255.0f;
// Improve: a file level is kept while its colours differ from ours (made
// from the level above) by at most this on average (0 to 255).
constexpr double ImproveTolerance = 12.0;
// and, for a cutout, while its coverage (scaled) is within this of level 0's:
// a file's 0/255 levels that thinned out cannot be scaled back.
constexpr double CoverageTolerance = 0.1;

// The RGBA level below: 2x2 means, colours in linear light, alpha as is.
QByteArray downsample(const QByteArray &above, int w, int h) {
    const int lw = std::max(1, w / 2), lh = std::max(1, h / 2);
    const SrgbTables &t = srgb();
    QByteArray level(qsizetype(lw) * lh * 4, Qt::Uninitialized);
    const unsigned char *src = reinterpret_cast<const unsigned char *>(above.constData());
    unsigned char *dst = reinterpret_cast<unsigned char *>(level.data());
    for (int y = 0; y < lh; ++y)
        for (int x = 0; x < lw; ++x) {
            const int x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1);
            const int y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
            const unsigned char *p[4] = {src + (qsizetype(y0) * w + x0) * 4, src + (qsizetype(y0) * w + x1) * 4,
                                         src + (qsizetype(y1) * w + x0) * 4, src + (qsizetype(y1) * w + x1) * 4};
            unsigned char *out = dst + (qsizetype(y) * lw + x) * 4;
            for (int c = 0; c < 3; ++c) {
                const float linear = 0.25f * (t.toLinear[p[0][c]] + t.toLinear[p[1][c]] + t.toLinear[p[2][c]]
                                              + t.toLinear[p[3][c]]);
                out[c] = t.toSrgb[std::clamp(int(linear * 4095.0f + 0.5f), 0, 4095)];
            }
            out[3] = static_cast<unsigned char>((p[0][3] + p[1][3] + p[2][3] + p[3][3] + 2) / 4);
        }
    return level;
}

double coverageOf(const QByteArray &rgba, float scale) {
    const unsigned char *p = reinterpret_cast<const unsigned char *>(rgba.constData());
    const qsizetype count = rgba.size() / 4;
    const float limit = ShapeCutoff * 255.0f;
    qsizetype passed = 0;
    for (qsizetype i = 0; i < count; ++i)
        if (p[i * 4 + 3] * scale >= limit)
            ++passed;
    return count > 0 ? double(passed) / double(count) : 0.0;
}

// A cutout level's alpha scaled so as many texels pass the alpha test as in
// level 0 (target), as coverageMipmaps does: trees and fences keep their
// density at a distance instead of thinning out.
void keepCoverage(QByteArray &level, double target) {
    if (target <= 0.0)
        return;
    float low = 0.0f, high = 8.0f;
    for (int i = 0; i < 16; ++i) {
        const float middle = 0.5f * (low + high);
        if (coverageOf(level, middle) < target)
            low = middle;
        else
            high = middle;
    }
    // Coverage moves in steps (many texels share an alpha in small levels):
    // the step nearer level 0's, above or below.
    const float scale = std::abs(coverageOf(level, low) - target) < std::abs(coverageOf(level, high) - target)
            ? low : high;
    // Truncated: a texel passes as it was counted (rounding passed some
    // counted below the cutoff).
    unsigned char *out = reinterpret_cast<unsigned char *>(level.data());
    for (qsizetype i = 0; i < level.size() / 4; ++i)
        out[i * 4 + 3] = static_cast<unsigned char>(std::min(255.0f, out[i * 4 + 3] * scale));
}

// The mean difference of two RGBA levels' colours.
double colourDifference(const QByteArray &a, const QByteArray &b) {
    if (a.size() != b.size() || a.isEmpty())
        return 255.0;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(a.constData());
    const unsigned char *q = reinterpret_cast<const unsigned char *>(b.constData());
    qint64 sum = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (i % 4 != 3)
            sum += std::abs(int(p[i]) - int(q[i]));
    return double(sum) / double(a.size() / 4 * 3);
}

// A shape texture's levels below level 0 (RGBA): the file's (fileLevels, as
// far as they go) or ours made from the level above, by the source; a
// cutout's keep level 0's coverage.
QVector<QByteArray> shapeMipChain(const QByteArray &level0, int width, int height,
                                  const QVector<QByteArray> &fileLevels, Texture::MipmapSource source,
                                  bool cutout) {
    QVector<QByteArray> chain;
    const double target = cutout ? coverageOf(level0, 1.0f) : 0.0;
    QByteArray above = level0;  // unscaled alpha: the next level is made from it
    int w = width, h = height;
    bool fileStill = source != Texture::MipmapSource::Regenerate;
    for (int k = 0; w > 1 || h > 1; ++k) {
        QByteArray ours = downsample(above, w, h);
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        QByteArray level;
        bool fair = fileStill && k < fileLevels.size() && fileLevels[k].size() == ours.size()
                && (source == Texture::MipmapSource::File
                    || colourDifference(fileLevels[k], ours) <= ImproveTolerance);
        QByteArray kept;
        if (fair && cutout && source == Texture::MipmapSource::Improve) {
            kept = fileLevels[k];
            keepCoverage(kept, target);
            fair = std::abs(coverageOf(kept, 1.0f) - target) <= CoverageTolerance;
        }
        if (fair) {
            level = fileLevels[k];
        } else {
            level = ours;
            fileStill = false;
        }
        above = level;
        // The file's own are kept as they are; ours and improved ones keep
        // the cutout's density.
        if (cutout && source != Texture::MipmapSource::File)
            level = fair && !kept.isEmpty() ? kept : (keepCoverage(level, target), level);
        chain.push_back(level);
    }
    return chain;
}
}

// A display-only shape texture's mipmaps by the settings (on its loader's
// thread): RGBA levels completed or replaced (shapeMipChain), block levels
// decoded, completed or replaced and encoded again (level 0 kept as it is).
static bool applyShapeMipmaps(const Texture &t, QVector<QByteArray> &levels, int gpuFormat) {
    const auto source = Texture::MipmapSource(Texture::mipmapSource.load(std::memory_order_relaxed));
    int fullChain = 1;
    for (int n = std::max(t.width, t.height); n > 1; n >>= 1)
        ++fullChain;
    if (levels.isEmpty() || (source == Texture::MipmapSource::File && levels.size() == fullChain))
        return true;
    const bool cutout = t.alphaClass.load() == TextureAlpha::Binary;
    if (gpuFormat == 0) {
        const QVector<QByteArray> file = levels.mid(1);
        levels = QVector<QByteArray>{levels.first()} + shapeMipChain(levels.first(), t.width, t.height, file, source, cutout);
        return true;
    }
    // Blocks: as RGBA, then the new levels encoded as level 0 is.
    const DxtCodec::Format codec = codecFormat(gpuFormat);
    const bool alpha = gpuFormat != GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    QVector<QByteArray> rgba;
    int w = t.width, h = t.height;
    for (const QByteArray &level : std::as_const(levels)) {
        QByteArray pixels;
        QString message;
        if (!DxtCodec::decode(level, w, h, codec, true, pixels, message))
            return false;
        rgba.push_back(pixels);
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    const QVector<QByteArray> chain = shapeMipChain(rgba.first(), t.width, t.height, rgba.mid(1), source, cutout);
    QVector<QByteArray> encoded{levels.first()};
    w = t.width;
    h = t.height;
    for (int k = 0; k < chain.size(); ++k) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        // A file level kept as it was keeps its blocks.
        if (k + 1 < levels.size() && k + 1 < rgba.size() && chain[k] == rgba[k + 1]) {
            encoded.push_back(levels[k + 1]);
            continue;
        }
        QByteArray blocks;
        QString message;
        if (!DxtCodec::encode(reinterpret_cast<const unsigned char *>(chain[k].constData()), chain[k].size(), w, h, 4,
                              codec, alpha, blocks, message))
            return false;
        encoded.push_back(blocks);
    }
    levels = std::move(encoded);
    return true;
}

bool Texture::prepareForRhi() {
    const int blocks = RhiTextures::blocksSupportKnown();
    if (blocks < 0 || Game::renderBackend != "qrhi" || width <= 0 || height <= 0 || (bytesPerPixel != 3 && bytesPerPixel != 4))
        return false;
    // As the upload makes them: the blocks with the file's whole chain of
    // mipmaps (else level 0 alone), or RGBA levels with the file's mipmaps
    // (the GPU makes them when there are none). A glTF cutout's coverage
    // mipmaps come at the upload, from level 0 (its cutoff is set later).
    // With mipmaps on shape textures, their levels by the settings (the
    // file's, ours or improved ones: applyShapeMipmaps).
    const bool mipmapped = shapeMipmaps.load(std::memory_order_relaxed);
    QVector<QByteArray> levels;
    int format = 0;
    if (!compressedLevels(*this, false, blocks == 1, levels, format, mipmapped)) {
        format = 0;
        if (!rgbaLevels(*this, true, levels))
            return false;
    }
    if (mipmapped && !applyShapeMipmaps(*this, levels, format))
        return false;
    preparedLevels = std::move(levels);
    preparedFormat = format;
    prepared = true;
    delete[] imageData;
    imageData = nullptr;
    editable = false;
    compressedData.clear();
    compressedGLFormat = 0;
    sourceMipmaps.clear();
    aceDocument.reset();
    return true;
}

bool Texture::uploadForRhi(bool mipmaps) {
    if (!loaded || width <= 0 || height <= 0 || (bytesPerPixel != 3 && bytesPerPixel != 4))
        return false;
    if (glLoaded) {
        if (mipmaps && !gpuMipmaps && tex != nullptr) {
            gpuMipmaps = true;
            RhiTextures::setSampling(tex[0], true, RhiTextures::clampedToEdge(tex[0]));
        }
        return true;
    }
    // A texture coming back after a release keeps its mipmaps (the upload
    // that made them may not come again).
    if (tex != nullptr && RhiTextures::evicted(tex[0]))
        mipmaps = mipmaps || gpuMipmaps;
    // Prepared on the loader's thread: only the GPU texture is made here.
    if (prepared && !preparedLevels.isEmpty()) {
        const bool blocksWithoutMipmaps = preparedFormat != 0 && mipmaps && preparedLevels.size() == 1
                && std::max(width, height) > 1;
        const bool coverage = mipmaps && coverageCutoff >= 0.0f && bytesPerPixel == 4;
        if (preparedFormat != 0 && !blocksWithoutMipmaps)
            return createRhiTexture(*this, preparedLevels, preparedFormat, mipmaps);
        if (preparedFormat == 0 && !coverage)
            return createRhiTexture(*this, preparedLevels, 0, mipmaps);
        // As the upload from the source would make it: level 0 as RGBA, its
        // mipmaps made from it (the GPU's, or ones keeping the coverage).
        QVector<QByteArray> levels(1);
        QString message;
        if (preparedFormat == 0)
            levels[0] = preparedLevels.first();
        else if (!DxtCodec::decode(preparedLevels.first(), width, height, codecFormat(preparedFormat), true,
                                   levels[0], message))
            return false;
        if (coverage)
            levels += coverageMipmaps(levels.first(), width, height, coverageCutoff);
        return createRhiTexture(*this, levels, 0, mipmaps);
    }
    if (uploadCompressedForRhi(*this, mipmaps))
        return true;
    QVector<QByteArray> levels;
    return rgbaLevels(*this, mipmaps, levels) && createRhiTexture(*this, levels, 0, mipmaps);
}

bool Texture::GLTextures(bool mipmaps) {
    if (Game::renderBackend == "qrhi")
        return uploadForRhi(mipmaps);
    auto *context = QOpenGLContext::currentContext();
    if (!loaded || !context || width <= 0 || height <= 0 ||
        (bytesPerPixel != 3 && bytesPerPixel != 4))
        return false;
    auto *f = context->functions();
    GLint maximum = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    if (width > maximum || height > maximum) {
        errorMessage = "Texture exceeds GL_MAX_TEXTURE_SIZE";
        error = true;
        return false;
    }
    beginPixelTransfer();
    if (glLoaded) {
        // A later consumer may request mips after an earlier base-only upload.
        glBindTexture(GL_TEXTURE_2D, tex[0]);
        if (mipmaps && !gpuMipmaps) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);
            f->glGenerateMipmap(GL_TEXTURE_2D);
            gpuMipmaps = true;
            gpuMipLevels = 1;
            for (int n = std::max(width, height); n > 1; n >>= 1)
                ++gpuMipLevels;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        }
        return pixelTransferSucceeded(*this);
    }
    if (!tex) {
        tex = new unsigned int[1]{};
        glGenTextures(1, tex);
    }
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    PixelRows rows;
    const bool direct = !compressedData.isEmpty() && dxtBlockBytes(compressedGLFormat) &&
                        compressedData.size() ==
                            DxtCodec::byteSize(width, height, codecFormat(compressedGLFormat)) &&
                        supportsCompressedFormat(compressedGLFormat);
    if (!direct && !decodeToCpu())
        return false;
    if (direct) {
        gpuInternalFormat = compressedGLFormat;
        f->glCompressedTexImage2D(GL_TEXTURE_2D, 0, gpuInternalFormat, width, height, 0,
                                  compressedData.size(), compressedData.constData());
    } else {
        gpuInternalFormat = type == GL_RGBA ? GL_RGBA8 : GL_RGB8;
        if (Game::AASamples > 0 && Game::AARemoveBorder && type == GL_RGBA) {
            for (int y = 0; y < height; ++y) {
                imageData[(qsizetype(y) * width) * 4 + 3] = 0;
                imageData[(qsizetype(y) * width + width - 1) * 4 + 3] = 0;
            }
            for (int x = 0; x < width; ++x) {
                imageData[x * 4 + 3] = 0;
                imageData[(qsizetype(height - 1) * width + x) * 4 + 3] = 0;
            }
            sourceMipmaps.clear(); // Do not combine altered base edges with unaltered mips.
        }
        glTexImage2D(GL_TEXTURE_2D, 0, gpuInternalFormat, width, height, 0, type, GL_UNSIGNED_BYTE,
                     imageData);
    }
    gpuMipmaps = mipmaps;
    gpuMipLevels = 1;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);
    if (mipmaps && !sourceMipmaps.isEmpty()) {
        int previousW = width, previousH = height;
        for (const auto &mip : sourceMipmaps) {
            if (mip.width != std::max(1, previousW / 2) ||
                mip.height != std::max(1, previousH / 2)) {
                errorMessage = "Invalid staged mip dimensions";
                return false;
            }
            QByteArray converted;
            QString message;
            const QByteArray *data = &mip.data;
            if (direct) {
                if (mip.compressedFormat != gpuInternalFormat) {
                    if (mip.compressedFormat) {
                        errorMessage = "Mixed compressed mip formats";
                        return false;
                    }
                    // ACE tiny tails are planar: encode only these tiny levels for the
                    // homogeneous GPU BC chain. The optional document retains originals.
                    if (!DxtCodec::encode(
                            reinterpret_cast<const unsigned char *>(mip.data.constData()),
                            mip.data.size(), mip.width, mip.height, bytesPerPixel,
                            codecFormat(gpuInternalFormat), type == GL_RGBA, converted, message)) {
                        errorMessage = message;
                        return false;
                    }
                    data = &converted;
                }
                if (data->size() !=
                    DxtCodec::byteSize(mip.width, mip.height, codecFormat(gpuInternalFormat)))
                    return false;
                f->glCompressedTexImage2D(GL_TEXTURE_2D, gpuMipLevels, gpuInternalFormat, mip.width,
                                          mip.height, 0, data->size(), data->constData());
            } else {
                if (mip.compressedFormat) {
                    if (!DxtCodec::decode(mip.data, mip.width, mip.height,
                                          codecFormat(mip.compressedFormat), type == GL_RGBA,
                                          converted, message)) {
                        errorMessage = message;
                        return false;
                    }
                    data = &converted;
                }
                if (data->size() != qsizetype(mip.width) * mip.height * bytesPerPixel)
                    return false;
                glTexImage2D(GL_TEXTURE_2D, gpuMipLevels, gpuInternalFormat, mip.width, mip.height,
                             0, type, GL_UNSIGNED_BYTE, data->constData());
            }
            ++gpuMipLevels;
            previousW = mip.width;
            previousH = mip.height;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, gpuMipLevels - 1);
    } else if (mipmaps) {
        f->glGenerateMipmap(GL_TEXTURE_2D);
        for (int n = std::max(width, height); n > 1; n >>= 1)
            ++gpuMipLevels;
    } else
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (!pixelTransferSucceeded(*this))
        return false; // Keep CPU data for retry/diagnostics.
    delete[] imageData;
    imageData = nullptr;
    editable = false;
    compressedData.clear();
    compressedGLFormat = 0;
    sourceMipmaps.clear();
    glLoaded = true;
    ++uploads;
    return true;
}

qint64 Texture::estimatedCpuBytes() const {
    qint64 bytes = 0;
    if (imageData != nullptr) {
        if (imageSize > 0) {
            bytes += imageSize;
        } else if (width > 0 && height > 0 && bytesPerPixel > 0) {
            bytes += qint64(width) * qint64(height) * qint64(bytesPerPixel);
        }
    }
    bytes += compressedData.size();
    for (const auto &mip : sourceMipmaps)
        bytes += mip.data.size();
    for (const QByteArray &level : preparedLevels)
        bytes += level.size();
    if (aceMetadata) {
        bytes += aceMetadata->header.size() + aceMetadata->trailing.size();
        for (const auto &p : aceMetadata->palettes)
            bytes += p.data.size();
        bytes += aceMetadata->channels.size() * sizeof(AceChannel);
    }
    if (aceDocument) {
        bytes += aceDocument->originalBytes().size();
        for (const auto &mip : aceDocument->levels)
            bytes += mip.data.size();
    }
    return bytes;
}

qint64 Texture::estimatedVramBytes() const {
    if (width <= 0 || height <= 0) {
        return 0;
    }

    const int format = (glLoaded && gpuInternalFormat != 0)                     ? gpuInternalFormat
                       : (!compressedData.isEmpty() && compressedGLFormat != 0) ? compressedGLFormat
                                                                                : 0;

    const int blockBytes = dxtBlockBytes(format);
    if (blockBytes > 0) {
        const int blocksWide = (width + 3) / 4;
        const int blocksHigh = (height + 3) / 4;
        qint64 total = qint64(blocksWide) * blocksHigh * blockBytes;
        if (glLoaded)
            for (int level = 1; level < gpuMipLevels; ++level)
                total += DxtCodec::byteSize(std::max(1, width >> level),
                                            std::max(1, height >> level), codecFormat(format));
        return total;
    }

    int bppBytes = bytesPerPixel;
    if (bppBytes <= 0) {
        if (type == GL_RGBA) {
            bppBytes = 4;
        } else if (type == GL_RGB) {
            bppBytes = 3;
        } else if (bpp > 0) {
            bppBytes = bpp / 8;
        }
    }
    if (glLoaded && gpuInternalFormat == GL_RGBA8)
        bppBytes = 4;
    if (bppBytes <= 0) {
        return 0;
    }

    qint64 total = qint64(width) * height * bppBytes;
    if (glLoaded)
        for (int level = 1; level < gpuMipLevels; ++level)
            total += qint64(std::max(1, width >> level)) * std::max(1, height >> level) * bppBytes;
    return total;
}

bool Texture::gpuIsCompressed() const {
    if (!glLoaded || gpuInternalFormat == 0) {
        return false;
    }
    return dxtBlockBytes(gpuInternalFormat) > 0;
}

bool Texture::readPixels(QByteArray &pixels) {
    if (released)
        TexLib::reloadNow(this);
    const qsizetype size = qsizetype(width) * height * bytesPerPixel;
    if (width <= 0 || height <= 0 || (bytesPerPixel != 3 && bytesPerPixel != 4))
        return false;
    if (imageData != nullptr) {
        pixels = QByteArray(reinterpret_cast<const char *>(imageData), size);
        return true;
    }
    if (preparedPixels(*this, pixels))
        return true;
    if (!canReadResident(*this))
        return false;
    // Read into a scratch texture (its pixels are not this one's).
    Texture read;
    read.width = width;
    read.height = height;
    read.bytesPerPixel = bytesPerPixel;
    read.type = type;
    read.imageSize = int(size);
    pixels.resize(size);
    read.imageData = reinterpret_cast<unsigned char *>(pixels.data());
    const bool ok = readResidentPixels(*this, read);
    read.imageData = nullptr;
    return ok;
}

void Texture::releaseContent(bool keepHandle) {
    if (tex != nullptr && tex[0] != 0 && glLoaded && Game::renderBackend == "qrhi") {
        if (keepHandle) {
            RhiTextures::evict(tex[0]);
        } else {
            RhiTextures::release(tex[0]);
            tex[0] = 0;
        }
    }
    delete[] imageData;
    imageData = nullptr;
    compressedData.clear();
    compressedGLFormat = 0;
    sourceMipmaps.clear();
    aceDocument.reset();
    prepared = false;
    preparedLevels.clear();
    editable = false;
    glLoaded = false;
    loaded = false;
}

void Texture::delVBO() {
    sourceMipmaps.clear();
    prepared = false;
    preparedLevels.clear();
    aceDocument.reset();
    aceMetadata.reset();
    gpuMipmaps = false;
    gpuMipLevels = 1;
    errorMessage.clear();
    // System.out.println("==== usuwam texture!");
    glLoaded = false;
    loaded = false;
    editable = false;
    missing = false;
    error = false;
    gpuInternalFormat = 0;
    // gl.glDeleteTextures(1, tex, 0);
}
