#ifndef ACELIB_H
#define ACELIB_H

#include "AceDocument.h"
#include <QThread>

class Texture;
class QImage;
struct AceLoadOptions {
    bool cpuPixels = false;        // Decode immediately for CPU-only consumers; no GL call.
    bool stageMipmaps = true;      // Transient staging only; GLTextures(false) discards them.
    bool preserveDocument = false; // Explicit content-editor opt-in, never the default.
    int quality = 1; // Rendering downsample divisor; source document remains independent.
    AceReadOptions reader;
};

// Drop-in TSRE worker adapter around the independent AceDocument library.
class AceLib : public QThread {
    Q_OBJECT
  public:
    static bool IsThread;
    Texture *texture = nullptr;
    void run() override;
    static bool load(const QString &path, Texture &texture, const AceLoadOptions &options,
                     QString &error);
    static bool save(QString path, Texture *texture);
    static bool save(QString path, Texture *texture, const AceWriteOptions &options,
                     QString &error);
    static bool save(const QString &path, const QImage &image, const AceWriteOptions &options,
                     QString &error);
};
#endif
