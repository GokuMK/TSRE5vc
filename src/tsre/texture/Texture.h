/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TEXTURE_H
#define TEXTURE_H

#include <QString>
#include <QByteArray>
#include <QVector>
#include <QtGlobal>
#include <memory>
#include <atomic>

class Brush;
class AceDocument;
struct AceMetadata;
struct TextureMip {
    int width = 0, height = 0;
    int compressedFormat = 0; // Zero: interleaved RGB/RGBA, same components as base.
    QByteArray data;
};

class Texture {
  public:
    Texture();
    Texture(QString pathid);
    Texture(const Texture *orig);
    Texture(int x, int y, int bpp, Brush *brush = NULL);
    virtual ~Texture();

    unsigned char *imageData = nullptr;
    int bpp = 0;
    int imageSize = 0;
    int bytesPerPixel = 0;
    int compressed = 0;
    int width = 0;
    int height = 0;
    int texID = 0;
    int type = 0;
    int typk = 0;
    QByteArray compressedData;
    int compressedGLFormat = 0;
    int gpuInternalFormat = 0;
    QVector<TextureMip> sourceMipmaps;        // Levels 1+, freed after upload.
    std::shared_ptr<AceMetadata> aceMetadata; // Small by default; no source pixels.
    std::shared_ptr<AceDocument> aceDocument; // Only requested document preservation.
    bool gpuMipmaps = false;
    int gpuMipLevels = 1;
    QString errorMessage;
    unsigned int *tex = nullptr;
    QString pathid;
    QVector<QString> hashid;
    std::atomic<bool> loaded{false}; // Worker publishes fully prepared content last.
    int ref = 0;
    bool glLoaded = false;
    bool editable = false;
    bool missing = false;
    bool error = false;

    void setEditable();
    // Decode retained ACE/DDS blocks without uploading or reading back a GL texture.
    bool decodeToCpu();
    // Moves content, NOT cache identity/ref count. Source remains safely empty.
    void takeContentFrom(Texture &other);
    void pixelsChanged();
    bool GLTextures(bool mipmaps = false);
    qint64 estimatedCpuBytes() const;
    qint64 estimatedVramBytes() const;
    bool gpuIsCompressed() const;
    void update();
    void advancedCrop(float *texCoords, int w = 0, int h = 0);
    void crop(float x1, float y1, float x2, float y2);
    void paint(Brush *brush, float x, float z);
    void sendToUndo(int id);
    void fillData(unsigned char *data);
    unsigned char *getImageData(int width, int height);
    void delVBO();

  private:
};

#endif /* TEXTURE_H */
