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

namespace TerrainFile { struct PatchUv; }
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
    // A cutout colour texture's cutoff (0..1): its mipmaps keep as many
    // texels at or above it as the full size has (QRhi). Negative: plain
    // mipmaps.
    float coverageCutoff = -1.0f;
    QString errorMessage;
    unsigned int *tex = nullptr;
    QString pathid;
    QVector<QString> hashid; // Logical lookup keys only; pathid retains I/O spelling.
    std::atomic<bool> loaded{false}; // Worker publishes fully prepared content last.
    // TextureAlpha::Class of level 0, set by the loaders before loaded.
    std::atomic<unsigned char> alphaClass{0};
    int ref = 0;
    bool glLoaded = false;
    // Textures uploaded so far (glLoaded set), for caches drawn from them:
    // a cut-out finishing its upload changes a shadow (task renderer 37).
    static unsigned uploads;
    bool editable = false;
    bool missing = false;
    bool error = false;
    // Memory residency (docs/tasks/textures/04-texture-memory.md): content
    // loaded from its file and unchanged since, which TexLib may release over
    // the memory budget and load again; released, waiting for a use to load
    // it again; when it was last asked for (TexLib's clock), for a texture
    // not drawn yet.
    bool reloadable = false;
    bool released = false;
    qint64 requested = 0;
    // Its requesters only display it (TexLib::Display): the loader prepares
    // its GPU levels on its thread and drops the source pixels, and it is
    // never edited (setEditable fails; getImageData reads a copy).
    std::atomic<bool> displayOnly{false};
    // GPU-ready levels (QRhi), made on the loader's thread: block-compressed
    // in preparedFormat (a GL compressed format), or RGBA8 (0).
    bool prepared = false;
    int preparedFormat = 0;
    QVector<QByteArray> preparedLevels;
    // Builds them from the source (pixels or blocks) and drops the source;
    // false when it cannot (before a renderer said which formats it takes):
    // the upload builds them then.
    bool prepareForRhi();

    // Mipmaps on shape textures (core.rendering.shapeTextures.*), read once
    // on the main thread (readMipmapSettings) before textures load: whether
    // shapes sample them, where their levels come from (the file's own, ours
    // made from level 0, or the file's where they are a fair downsample),
    // and the shaders' sharpness bias (QRhi).
    enum class MipmapSource { File, Regenerate, Improve };
    static std::atomic<bool> shapeMipmaps;
    static std::atomic<int> mipmapSource;
    static float lodBias;
    static void readMipmapSettings();

    void setEditable();
    // Decode retained ACE/DDS blocks without uploading or reading back a GL texture.
    bool decodeToCpu();
    // Finds alphaClass from the loaded data (compressed blocks or pixels).
    void classifyAlpha();
    // Moves content, NOT cache identity/ref count. Source remains safely empty.
    void takeContentFrom(Texture &other);
    void pixelsChanged();
    bool GLTextures(bool mipmaps = false);
    // GLTextures for the QRhi renderer.
    bool uploadForRhi(bool mipmaps);
    qint64 estimatedCpuBytes() const;
    qint64 estimatedVramBytes() const;
    bool gpuIsCompressed() const;
    void update();
    // Replaces a rectangle of a loaded RGBA texture's first level (both renderers);
    // pixels are tightly packed rows of width * 4 bytes.
    void updateRegion(int x, int y, int w, int h, const unsigned char *pixels);
    void advancedCrop(const TerrainFile::PatchUv &uv, int patchSamples, int w = 0, int h = 0);
    void crop(float x1, float y1, float x2, float y2);
    void paint(Brush *brush, float x, float z);
    void sendToUndo(int id);
    void fillData(unsigned char *data);
    unsigned char *getImageData(int width, int height);
    // Its level 0 as bytesPerPixel pixels, rows packed, without editing it:
    // the CPU pixels, the prepared level, or a read back from the GPU.
    bool readPixels(QByteArray &pixels);
    void delVBO();
    // Drops the content: the GPU texture (QRhi) and the CPU pixels and
    // blocks; size, sampling and alpha class stay. keepHandle: the GPU handle
    // stays for the next upload, so code holding it gets the texture back.
    void releaseContent(bool keepHandle);

  private:
};

#endif /* TEXTURE_H */
