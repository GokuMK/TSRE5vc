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
    static int cachedSupport = -1;
    if (cachedSupport != -1) {
        return cachedSupport == 1;
    }

    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (ctx == nullptr) {
        cachedSupport = 0;
        return false;
    }

    const bool ok =
            ctx->hasExtension(QByteArrayLiteral("GL_EXT_texture_compression_s3tc")) ||
            ctx->hasExtension(QByteArrayLiteral("GL_EXT_texture_compression_dxt1")) ||
            ctx->hasExtension(QByteArrayLiteral("GL_NV_texture_compression_s3tc")) ||
            ctx->hasExtension(QByteArrayLiteral("GL_S3_s3tc"));
    cachedSupport = ok ? 1 : 0;
    return ok;
}

bool supportsS3TCFull() {
    static int cachedSupport = -1;
    if (cachedSupport != -1) {
        return cachedSupport == 1;
    }

    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (ctx == nullptr) {
        cachedSupport = 0;
        return false;
    }

    const bool ok =
            ctx->hasExtension(QByteArrayLiteral("GL_EXT_texture_compression_s3tc")) ||
            ctx->hasExtension(QByteArrayLiteral("GL_NV_texture_compression_s3tc")) ||
            ctx->hasExtension(QByteArrayLiteral("GL_S3_s3tc"));
    cachedSupport = ok ? 1 : 0;
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

static inline void decodeRGB565(uint16_t c, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
    g = static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63);
    b = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
}

static void decodeDXT1Block(const uint8_t *block, uint8_t *rgba, int stride /* bytes per row */) {
    const uint16_t c0 = uint16_t(block[0]) | (uint16_t(block[1]) << 8);
    const uint16_t c1 = uint16_t(block[2]) | (uint16_t(block[3]) << 8);

    uint8_t r0, g0, b0;
    uint8_t r1, g1, b1;
    decodeRGB565(c0, r0, g0, b0);
    decodeRGB565(c1, r1, g1, b1);

    uint8_t colors[4][4];
    colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0; colors[0][3] = 255;
    colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1; colors[1][3] = 255;

    if (c0 > c1) {
        colors[2][0] = (2 * r0 + r1) / 3;
        colors[2][1] = (2 * g0 + g1) / 3;
        colors[2][2] = (2 * b0 + b1) / 3;
        colors[2][3] = 255;

        colors[3][0] = (r0 + 2 * r1) / 3;
        colors[3][1] = (g0 + 2 * g1) / 3;
        colors[3][2] = (b0 + 2 * b1) / 3;
        colors[3][3] = 255;
    } else {
        colors[2][0] = (r0 + r1) / 2;
        colors[2][1] = (g0 + g1) / 2;
        colors[2][2] = (b0 + b1) / 2;
        colors[2][3] = 255;

        colors[3][0] = 0;
        colors[3][1] = 0;
        colors[3][2] = 0;
        colors[3][3] = 0;
    }

    const uint32_t code = uint32_t(block[4]) |
                          (uint32_t(block[5]) << 8) |
                          (uint32_t(block[6]) << 16) |
                          (uint32_t(block[7]) << 24);

    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const int idx = (code >> (2 * (4 * j + i))) & 0x03;
            uint8_t *dst = rgba + j * stride + i * 4;
            dst[0] = colors[idx][0];
            dst[1] = colors[idx][1];
            dst[2] = colors[idx][2];
            dst[3] = colors[idx][3];
        }
    }
}

static void decodeDXT3Block(const uint8_t *block, uint8_t *rgba, int stride /* bytes per row */) {
    // First 8 bytes: 4-bit alpha for 16 pixels (64 bits)
    uint64_t alphaBits = 0;
    for (int i = 0; i < 8; ++i) {
        alphaBits |= (uint64_t(block[i]) << (8 * i));
    }

    const uint8_t *colorBlock = block + 8;

    const uint16_t c0 = uint16_t(colorBlock[0]) | (uint16_t(colorBlock[1]) << 8);
    const uint16_t c1 = uint16_t(colorBlock[2]) | (uint16_t(colorBlock[3]) << 8);

    uint8_t r0, g0, b0;
    uint8_t r1, g1, b1;
    decodeRGB565(c0, r0, g0, b0);
    decodeRGB565(c1, r1, g1, b1);

    uint8_t colors[4][3]; // RGB
    colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0;
    colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1;

    // DXT3 always treats this as a 4-color block (no transparent color)
    colors[2][0] = (2 * r0 + r1) / 3;
    colors[2][1] = (2 * g0 + g1) / 3;
    colors[2][2] = (2 * b0 + b1) / 3;

    colors[3][0] = (r0 + 2 * r1) / 3;
    colors[3][1] = (g0 + 2 * g1) / 3;
    colors[3][2] = (b0 + 2 * b1) / 3;

    const uint32_t code = uint32_t(colorBlock[4]) |
                          (uint32_t(colorBlock[5]) << 8) |
                          (uint32_t(colorBlock[6]) << 16) |
                          (uint32_t(colorBlock[7]) << 24);

    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const int pixelIndex = 4 * j + i;
            const uint8_t alpha4 = (alphaBits >> (4 * pixelIndex)) & 0x0F;
            const uint8_t a = alpha4 * 17; // 0..15 -> 0..255

            const int colorIndex = (code >> (2 * pixelIndex)) & 0x03;

            uint8_t *dst = rgba + j * stride + i * 4;
            dst[0] = colors[colorIndex][0];
            dst[1] = colors[colorIndex][1];
            dst[2] = colors[colorIndex][2];
            dst[3] = a;
        }
    }
}

static void decodeDXT5Block(const uint8_t *block, uint8_t *rgba, int stride /* bytes per row */) {
    // Alpha
    const uint8_t alpha0 = block[0];
    const uint8_t alpha1 = block[1];

    uint8_t alphaTable[8];
    alphaTable[0] = alpha0;
    alphaTable[1] = alpha1;
    if (alpha0 > alpha1) {
        alphaTable[2] = (6 * alpha0 + 1 * alpha1) / 7;
        alphaTable[3] = (5 * alpha0 + 2 * alpha1) / 7;
        alphaTable[4] = (4 * alpha0 + 3 * alpha1) / 7;
        alphaTable[5] = (3 * alpha0 + 4 * alpha1) / 7;
        alphaTable[6] = (2 * alpha0 + 5 * alpha1) / 7;
        alphaTable[7] = (1 * alpha0 + 6 * alpha1) / 7;
    } else {
        alphaTable[2] = (4 * alpha0 + 1 * alpha1) / 5;
        alphaTable[3] = (3 * alpha0 + 2 * alpha1) / 5;
        alphaTable[4] = (2 * alpha0 + 3 * alpha1) / 5;
        alphaTable[5] = (1 * alpha0 + 4 * alpha1) / 5;
        alphaTable[6] = 0;
        alphaTable[7] = 255;
    }

    // 48 bits of alpha indices
    uint64_t alphaBits = 0;
    for (int i = 0; i < 6; ++i) {
        alphaBits |= (uint64_t(block[2 + i]) << (8 * i));
    }

    // Color data (DXT1-like) in block[8..15], always 4-color mode for DXT5.
    const uint8_t *colorBlock = block + 8;

    const uint16_t c0 = uint16_t(colorBlock[0]) | (uint16_t(colorBlock[1]) << 8);
    const uint16_t c1 = uint16_t(colorBlock[2]) | (uint16_t(colorBlock[3]) << 8);

    uint8_t r0, g0, b0;
    uint8_t r1, g1, b1;
    decodeRGB565(c0, r0, g0, b0);
    decodeRGB565(c1, r1, g1, b1);

    uint8_t colors[4][3];
    colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0;
    colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1;
    colors[2][0] = (2 * r0 + r1) / 3;
    colors[2][1] = (2 * g0 + g1) / 3;
    colors[2][2] = (2 * b0 + b1) / 3;
    colors[3][0] = (r0 + 2 * r1) / 3;
    colors[3][1] = (g0 + 2 * g1) / 3;
    colors[3][2] = (b0 + 2 * b1) / 3;

    const uint32_t code = uint32_t(colorBlock[4]) |
                          (uint32_t(colorBlock[5]) << 8) |
                          (uint32_t(colorBlock[6]) << 16) |
                          (uint32_t(colorBlock[7]) << 24);

    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const int pixelIndex = 4 * j + i;
            const int colorIndex = (code >> (2 * pixelIndex)) & 0x03;
            const int alphaIndex = (alphaBits >> (3 * pixelIndex)) & 0x07;

            uint8_t *dst = rgba + j * stride + i * 4;
            dst[0] = colors[colorIndex][0];
            dst[1] = colors[colorIndex][1];
            dst[2] = colors[colorIndex][2];
            dst[3] = alphaTable[alphaIndex];
        }
    }
}

static bool decodeCompressedToImageData(Texture *texture) {
    if (texture == nullptr) {
        return false;
    }
    if (texture->compressedData.isEmpty()) {
        return false;
    }

    const int width = texture->width;
    const int height = texture->height;
    if (width <= 0 || height <= 0) {
        return false;
    }

    const int blocksWide = (width + 3) / 4;
    const int blocksHigh = (height + 3) / 4;
    const int blockBytes = dxtBlockBytes(texture->compressedGLFormat);
    if (blockBytes == 0) {
        return false;
    }
    const int expectedSize = blocksWide * blocksHigh * blockBytes;
    if (texture->compressedData.size() < expectedSize) {
        return false;
    }

    const int outBpp = (texture->type == GL_RGBA) ? 4 : 3;
    texture->bytesPerPixel = outBpp;
    texture->imageSize = outBpp * width * height;

    if (texture->imageData != nullptr) {
        delete[] texture->imageData;
        texture->imageData = nullptr;
    }
    texture->imageData = new unsigned char[size_t(width) * size_t(height) * size_t(outBpp)];

    const uint8_t *blockPtr = reinterpret_cast<const uint8_t*>(texture->compressedData.constData());
    for (int by = 0; by < blocksHigh; ++by) {
        for (int bx = 0; bx < blocksWide; ++bx) {
            uint8_t tile[4 * 4 * 4];

            if (texture->compressedGLFormat == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT) {
                decodeDXT3Block(blockPtr, tile, 4 * 4);
            } else if (texture->compressedGLFormat == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) {
                decodeDXT5Block(blockPtr, tile, 4 * 4);
            } else {
                decodeDXT1Block(blockPtr, tile, 4 * 4);
            }

            const int x0 = bx * 4;
            const int y0 = by * 4;
            for (int j = 0; j < 4; ++j) {
                const int y = y0 + j;
                if (y >= height) {
                    break;
                }
                for (int i = 0; i < 4; ++i) {
                    const int x = x0 + i;
                    if (x >= width) {
                        break;
                    }

                    const uint8_t *srcPixel = &tile[(j * 4 + i) * 4];
                    unsigned char *dstPixel = texture->imageData + (y * width + x) * outBpp;
                    dstPixel[0] = srcPixel[0];
                    dstPixel[1] = srcPixel[1];
                    dstPixel[2] = srcPixel[2];
                    if (outBpp == 4) {
                        dstPixel[3] = srcPixel[3];
                    }
                }
            }

            blockPtr += blockBytes;
        }
    }
    return true;
}

} // namespace

Texture::Texture() {
}

Texture::Texture(QString pathid) {
    this->pathid = pathid;
    this->hashid.push_back(pathid);
    //temp fix for dds/ace loading
    // Openrails uses .dds textures instead of .ace
    QString tType = pathid.toLower().split(".").last();
    if(tType == "dds"){
        hashid.push_back(pathid.left(pathid.length() - 3)+"ace");
    }
}
    
Texture::Texture(int x, int y, int bpp, Brush* brush){
    width = x;
    height = y;
    bpp = bpp;
    bytesPerPixel = (bpp / 8);
    imageSize = (bytesPerPixel * width * height);
    imageData = new unsigned char[imageSize];
    std::fill(imageData, imageData+imageSize, 255);
    if (bpp == 24) {
        type = GL_RGB;
    } else {
        type = GL_RGBA;
    }

    editable = true;
    loaded = true;
}

Texture::Texture(const Texture* orig) {
    qDebug() << "clone tex" << orig->pathid;
    //QOpenGLFunctions_3_2_Core *f = QOpenGLContext::currentContext()->functions();
    width = orig->width;
    height = orig->height;
    bpp = orig->bpp;
    type = orig->type;
    bytesPerPixel = orig->bytesPerPixel;
    
    //QOpenGLFunctions_3_2_Core *f = new QOpenGLFunctions_3_2_Core();
    if(orig->editable){
        imageData = new unsigned char[bytesPerPixel*width*height];
        memcpy(imageData, orig->imageData, bytesPerPixel*width*height);
        this->editable = true;
    } else {
        imageData = new unsigned char[bytesPerPixel*width*height];
        glBindTexture(GL_TEXTURE_2D, orig->tex[0]);
        glGetTexImage(GL_TEXTURE_2D, 0, orig->type, GL_UNSIGNED_BYTE, imageData);
        this->editable = true;
    }

    tex = new unsigned int[1];
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, type, width, height, 0, type, GL_UNSIGNED_BYTE, imageData);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //delete imageData;
    glLoaded = true;

    /*unsigned int* pex = new unsigned int[1];
    f->glGenBuffers(1, pex);
    f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pex[0]);
    f->glBufferData(GL_PIXEL_UNPACK_BUFFER, bytesPerPixel*width*height, 0, GL_STREAM_DRAW_ARB);
    imageData = (unsigned byte*)f->glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
    
    
    
    f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                type, GL_UNSIGNED_BYTE, 0);
    
    f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);*/
    loaded = true;
}

void Texture::setEditable(){
    if(editable)
        return;
    if(!loaded)
        return;
    if(imageData != nullptr){
        this->editable = true;
        return;
    }
    if(!glLoaded){
        if(!GLTextures())
            return;
    }

    imageData = new unsigned char[bytesPerPixel*width*height];

    //QOpenGLFunctions_3_2_Core *f = QOpenGLContext::currentContext()-> functions();
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glGetTexImage(GL_TEXTURE_2D, 0, type, GL_UNSIGNED_BYTE, imageData);
    this->editable = true;
}

unsigned char * Texture::getImageData(int width, int height){
    if(!editable) 
        setEditable();
    
    //qDebug() << width << height << bytesPerPixel;
    unsigned char * out = new unsigned char[width*height*bytesPerPixel];
    
    float scalew = (float)this->width/width;
    float scaleh = (float)this->height/height;
    
    qDebug() << this->width <<" "<< this->height;
    
    int lineWidth = (this->width*bytesPerPixel);
    //if( lineWidth%4 !=0) 
    //    lineWidth = lineWidth + 4 - lineWidth%4;
    //lineWidth /= 4;
    //if(lineWidth*4 < this->width*bytesPerPixel)
    //    lineWidth = lineWidth*4+4;
    //else
    //    lineWidth = lineWidth*4;
    
    for(int i = 0; i < height; i++ )
        for(int j = 0; j < width; j++ ){
            int wsi = scaleh*i;
            int hsi = scalew*j;
            out[i*width*bytesPerPixel + j*bytesPerPixel+0] = imageData[wsi*lineWidth + hsi*bytesPerPixel+0];
            out[i*width*bytesPerPixel + j*bytesPerPixel+1] = imageData[wsi*lineWidth + hsi*bytesPerPixel+1];
            out[i*width*bytesPerPixel + j*bytesPerPixel+2] = imageData[wsi*lineWidth + hsi*bytesPerPixel+2];
            if(bytesPerPixel == 4)
                out[i*width*bytesPerPixel + j*bytesPerPixel+3] = imageData[wsi*lineWidth + hsi*bytesPerPixel+3];
        }
    
    return out;
}

void Texture::advancedCrop(float* texCoords, int w, int h){
    if(!editable)
        setEditable();
    
    if(w == 0) 
        w = width;
    if(h == 0) 
        h = height;
    float texCoords2[7];
    texCoords2[1] = texCoords[1]*w;
    texCoords2[2] = texCoords[2]*h;
    texCoords2[3] = texCoords[3]*16.0;//*width;
    texCoords2[4] = texCoords[4]*16.0;//*height;
    texCoords2[5] = texCoords[5]*16.0;//*width;
    texCoords2[6] = texCoords[6]*16.0;//*height;
    
    qDebug() << width << height << bytesPerPixel << "--" << w << h;
    qDebug() << texCoords2[1] << texCoords2[2] << texCoords2[3] << texCoords2[4] << texCoords2[5] << texCoords2[6];
    qDebug() << texCoords[1] << texCoords[2] << texCoords[3] << texCoords[4] << texCoords[5] << texCoords[6];
    
    unsigned char* newData = new unsigned char[w*h*this->bytesPerPixel];    
    
    float ii, jj;
    float widthRatio = (float)width/w;
    float heightTatio = (float)height/h;
    for(int i = 0; i < w; i++)
        for(int j = 0; j < h; j++){
            jj = texCoords2[1] + texCoords2[3]*j + texCoords2[4]*i;
            ii = texCoords2[2] + texCoords2[5]*j + texCoords2[6]*i;
            ii *= widthRatio;
            jj *= heightTatio;

            while(ii >= width)
                ii -= width;
            while(jj >= height)
                jj -= height;
            
            while(ii < 0)
                ii += width;
            while(jj < 0)
                jj += height;
                            
            newData[i*w*bytesPerPixel+j*bytesPerPixel+0] = imageData[(int)ii*width*bytesPerPixel+(int)jj*bytesPerPixel+0];
            newData[i*w*bytesPerPixel+j*bytesPerPixel+1] = imageData[(int)ii*width*bytesPerPixel+(int)jj*bytesPerPixel+1];
            newData[i*w*bytesPerPixel+j*bytesPerPixel+2] = imageData[(int)ii*width*bytesPerPixel+(int)jj*bytesPerPixel+2];
            if(this->bytesPerPixel == 4)
                newData[i*width*bytesPerPixel+j*bytesPerPixel+3] = imageData[(int)ii*width*bytesPerPixel+(int)jj*bytesPerPixel+3];            
        }
        
    qDebug() << "advanced rot finished";
    delete[] this->imageData;
    this->imageData = newData;
    this->width = w;
    this->height = h;
    this->update();
}

void Texture::crop(float x1, float y1, float x2, float y2){
    if(!editable) 
        setEditable();

    qDebug() << x1 <<" "<<y1<<" "<<x2<<" "<<y2;

    if(x1 < x2 && y1 < y2)
        return;   
    
    unsigned char* newData = new unsigned char[this->width*this->height*this->bytesPerPixel];    
    
    if(x1 > x2 && y1 > y2){
        int ii, jj;
        for(int i = 0; i < width; i++)
            for(int j = 0; j < height; j++){
                ii = width - i - 1;
                jj = height - j - 1;
                newData[i*width*bytesPerPixel+j*bytesPerPixel+0] = imageData[ii*width*bytesPerPixel+jj*bytesPerPixel+0];
                newData[i*width*bytesPerPixel+j*bytesPerPixel+1] = imageData[ii*width*bytesPerPixel+jj*bytesPerPixel+1];
                newData[i*width*bytesPerPixel+j*bytesPerPixel+2] = imageData[ii*width*bytesPerPixel+jj*bytesPerPixel+2];
                if(this->bytesPerPixel == 4)
                    newData[i*width*bytesPerPixel+j*bytesPerPixel+3] = imageData[ii*width*bytesPerPixel+jj*bytesPerPixel+3];            
            }
    }
    
    if(x1 > x2 && y1 < y2){
        int ii, jj;
        for(int i = 0; i < width; i++)
            for(int j = 0; j < height; j++){
                ii = j;
                jj = width - i - 1;
                newData[i*width*bytesPerPixel+j*bytesPerPixel+0] = imageData[ii*width*bytesPerPixel+jj*bytesPerPixel+0];
                newData[i*width*bytesPerPixel+j*bytesPerPixel+1] = imageData[ii*width*bytesPerPixel+jj*bytesPerPixel+1];
                newData[i*width*bytesPerPixel+j*bytesPerPixel+2] = imageData[ii*width*bytesPerPixel+jj*bytesPerPixel+2];
                if(this->bytesPerPixel == 4)
                    newData[i*width*bytesPerPixel+j*bytesPerPixel+3] = imageData[ii*width*bytesPerPixel+jj*bytesPerPixel+3];            
            }
        ii = this->height;
        this->height = this->width;
        this->width = ii;
    }

    if(x1 < x2 && y1 > y2){
        int ii, jj;
        for(int i = 0; i < width; i++)
            for(int j = 0; j < height; j++){
                ii = height - j - 1;
                jj = i;
                newData[i*width*bytesPerPixel+j*bytesPerPixel+0] = imageData[ii*width*bytesPerPixel+jj*bytesPerPixel+0];
                newData[i*width*bytesPerPixel+j*bytesPerPixel+1] = imageData[ii*width*bytesPerPixel+jj*bytesPerPixel+1];
                newData[i*width*bytesPerPixel+j*bytesPerPixel+2] = imageData[ii*width*bytesPerPixel+jj*bytesPerPixel+2];
                if(this->bytesPerPixel == 4)
                    newData[i*width*bytesPerPixel+j*bytesPerPixel+3] = imageData[ii*width*bytesPerPixel+jj*bytesPerPixel+3];
            }
        ii = this->height;
        this->height = this->width;
        this->width = ii;
    }
    
    delete[] this->imageData;
    this->imageData = newData;
    
    this->update();
}

void Texture::sendToUndo(int id){
    if(!editable) 
        setEditable();
    Undo::PushTextureData(id, imageData, bytesPerPixel*width*height);
}

void Texture::fillData(unsigned char* data){
    if(imageData == NULL)
        return;
    memcpy(imageData, data, bytesPerPixel*width*height);
    update();
}

void Texture::paint(Brush* brush, float x, float z){
    if(!editable) 
        setEditable();
    
    Texture* tex = brush->tex;
    
    if(tex != NULL){
        if(!tex->loaded) tex = NULL;
        else if(!tex->editable)
            tex->setEditable();
    }
    
    int tx = x*width;
    int tz = z*height;
    
    int txi, tzj;
    
    float talpha = 0;
    
    int size = (brush->size*this->width)/512;
    if(size < 1)
        size = 1;
    //size = (size/512);
    
    for(int i = -size; i < size; i++)
        for(int j = -size; j < size; j++){
            txi = tx+i;
            tzj = tz+j;
            if(tx+i >= height) continue;
            if(tz+j >= width) continue;
            if(tx+i < 0) continue;
            if(tz+j < 0) continue;
            //if(sqrt(i*i + j*j) > size) continue;
            
            talpha = (brush->alpha)*brush->getAlpha(i, j, size);
            //talpha = (brush->alpha)*(1.0-(float)sqrt(i*i + j*j)/size);
            txi*=1;
            tzj*=1;
            
            if(tex != NULL && brush->useTexture){
                
                if(tzj >= tex->width){
                    tzj = tzj%tex->width;
                }
                if(txi >= tex->height){
                    txi = txi%tex->height;
                }
                
                imageData[(tx+i)*width*bytesPerPixel + (tz + j)*bytesPerPixel] 
                        = (1-talpha)*imageData[(tx+i)*width*bytesPerPixel + (tz + j)*bytesPerPixel] + (talpha)*tex->imageData[(txi)*tex->width*tex->bytesPerPixel + (tzj)*tex->bytesPerPixel];
                imageData[(tx+i)*width*bytesPerPixel + (tz + j)*bytesPerPixel+1] 
                        = (1-talpha)*imageData[(tx+i)*width*bytesPerPixel + (tz + j)*bytesPerPixel+1] + (talpha)*tex->imageData[(txi)*tex->width*tex->bytesPerPixel + (tzj)*tex->bytesPerPixel+1];
                imageData[(tx+i)*width*bytesPerPixel + (tz + j)*bytesPerPixel+2] 
                        = (1-talpha)*imageData[(tx+i)*width*bytesPerPixel + (tz + j)*bytesPerPixel+2] + (talpha)*tex->imageData[(txi)*tex->width*tex->bytesPerPixel + (tzj)*tex->bytesPerPixel+2];
            } else {
                imageData[(tx+i)*width*bytesPerPixel + (tz + j)*bytesPerPixel] 
                        = (1-talpha)*imageData[(tx+i)*width*bytesPerPixel + (tz + j)*bytesPerPixel] + (talpha)*(brush->color[0]);
                imageData[(tx+i)*width*bytesPerPixel + (tz + j)*bytesPerPixel+1] 
                        = (1-talpha)*imageData[(tx+i)*width*bytesPerPixel + (tz + j)*bytesPerPixel+1] + (talpha)*(brush->color[1]);
                imageData[(tx+i)*width*bytesPerPixel + (tz + j)*bytesPerPixel+2] 
                        = (1-talpha)*imageData[(tx+i)*width*bytesPerPixel + (tz + j)*bytesPerPixel+2] + (talpha)*(brush->color[2]);
            }
        }
}

void Texture::update(){
    //QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, type, width, height, 0, type, GL_UNSIGNED_BYTE, imageData);
}

Texture::~Texture() {
}

bool Texture::GLTextures(bool mipmaps) {
    if(!loaded) return false;

    tex = new unsigned int[1];
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, tex[0]);

    bool uploadedCompressed = false;
    if (!compressedData.isEmpty() && compressedGLFormat != 0) {
        const int blocksWide = (width + 3) / 4;
        const int blocksHigh = (height + 3) / 4;
        const int expectedSize = blocksWide * blocksHigh * dxtBlockBytes(compressedGLFormat);

        if (expectedSize > 0 &&
                (width % 4 == 0) && (height % 4 == 0) &&
                (compressedData.size() >= expectedSize) &&
                supportsCompressedFormat(compressedGLFormat)) {
            f->glCompressedTexImage2D(GL_TEXTURE_2D, 0, compressedGLFormat, width, height, 0,
                                      expectedSize, compressedData.constData());
            uploadedCompressed = true;
        } else if (imageData == nullptr) {
            if (!decodeCompressedToImageData(this)) {
                return false;
            }
        }
    }

    if (!uploadedCompressed) {
        if (imageData == nullptr) {
            return false;
        }

        if(Game::AASamples > 0 && Game::AARemoveBorder)
            if(type == GL_RGBA){
                for (int i = 0; i < height; i++)
                    imageData[i*width*bytesPerPixel + (width-1)*bytesPerPixel + 3] = 0;
                for (int i = 0; i < height; i++)
                    imageData[i*width*bytesPerPixel + 3] = 0;
                for (int i = 0; i < width; i++)
                    imageData[(height-1)*width*bytesPerPixel + i*bytesPerPixel + 3] = 0;
                for (int i = 0; i < width; i++)
                    imageData[i*bytesPerPixel + 3] = 0;
            }

        glTexImage2D(GL_TEXTURE_2D, 0, type, width, height, 0, type, GL_UNSIGNED_BYTE, imageData);
    }
    
    //f->glTexStorage2D(GL_TEXTURE_2D, 4, GL_RGBA8, width, height);
    //f->glTexSubImage2D(GL_TEXTURE_2D, 0​, 0, 0, width​, height​, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    if(mipmaps){
        f->glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,  GL_LINEAR_MIPMAP_LINEAR );
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,  GL_LINEAR );
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    delete[] imageData;
    imageData = NULL;
    this->editable = false;
    compressedData.clear();
    compressedGLFormat = 0;
    glLoaded = true;
    return true;
}


void Texture::delVBO() {
    //System.out.println("==== usuwam texture!");
    glLoaded = false;
    loaded = false;
    editable = false;
    missing = false;
    error = false;
    //gl.glDeleteTextures(1, tex, 0);
}

