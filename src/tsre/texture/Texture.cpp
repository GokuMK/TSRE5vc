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
#include <tsre/texture/AceDocument.h>
#include <algorithm>
#include <cstring>
#include <cmath>

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

} // namespace

Texture::Texture() {}

bool Texture::decodeToCpu() {
    bool ok =
        imageData != nullptr || (!compressedData.isEmpty() && decodeCompressedToImageData(this));
    if (ok)
        editable = true;
    return ok;
}

Texture::Texture(QString pathid) {
    this->pathid = pathid;
    this->hashid.push_back(pathid);
    // temp fix for dds/ace loading
    //  Openrails uses .dds textures instead of .ace
    QString tType = pathid.toLower().split(".").last();
    if (tType == "dds") {
        hashid.push_back(pathid.left(pathid.length() - 3) + "ace");
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
    } else if (orig->glLoaded && orig->tex && QOpenGLContext::currentContext()) {
        imageData = new unsigned char[imageSize];
        if (!readGpuPixels(*orig, *this)) {
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
    if (!loaded)
        return;
    if (decodeToCpu())
        return;
    if (!glLoaded || !tex || !QOpenGLContext::currentContext())
        return;
    imageSize = width * height * bytesPerPixel;
    imageData = new unsigned char[imageSize];
    if (!readGpuPixels(*this, *this)) {
        delete[] imageData;
        imageData = nullptr;
        return;
    }
    editable = true;
}

void Texture::pixelsChanged() {
    sourceMipmaps.clear();
    compressedData.clear();
    compressedGLFormat = 0;
    aceDocument.reset(); // Its source pixels are no longer the edited document.
    imageSize = width * height * bytesPerPixel;
    bpp = bytesPerPixel * 8;
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
    if (tex && tex[0] && QOpenGLContext::currentContext())
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
    gpuInternalFormat = other.gpuInternalFormat;
    sourceMipmaps = std::move(other.sourceMipmaps);
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
    if (!editable)
        setEditable();

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

void Texture::advancedCrop(float *texCoords, int w, int h) {
    if (!editable)
        setEditable();
    if (!imageData || !texCoords)
        return;
    if (w == 0)
        w = width;
    if (h == 0)
        h = height;
    if (w <= 0 || h <= 0 || qint64(w) * h > 64 * 1024 * 1024)
        return;
    for (int i = 1; i <= 6; ++i)
        if (!std::isfinite(texCoords[i]))
            return;
    unsigned char *next = new unsigned char[qsizetype(w) * h * bytesPerPixel];
    // Preserve the terrain patch's historical 16-unit UV transformation.
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double u =
                (texCoords[1] * w + 16.0 * (texCoords[3] * x + texCoords[4] * y)) * width / w;
            const double v =
                (texCoords[2] * h + 16.0 * (texCoords[5] * x + texCoords[6] * y)) * height / h;
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

void Texture::update() {
    if (!imageData)
        return;
    pixelsChanged();
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

Texture::~Texture() {
    // Existing callers own/free the legacy raw pointers. New containers are RAII.
}

bool Texture::GLTextures(bool mipmaps) {
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

void Texture::delVBO() {
    sourceMipmaps.clear();
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
