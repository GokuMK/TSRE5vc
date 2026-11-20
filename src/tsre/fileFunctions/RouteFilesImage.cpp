/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// RouteFiles' images: apart, so that its reading needs QtCore only (the
// test programs that build the readers).

#include <tsre/fileFunctions/RouteFiles.h>

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

namespace RouteFiles {

QImage image(const QString &path) {
    QByteArray bytes;
    switch (Internal::lookup(path, &bytes)) {
    case Internal::Source::Missing: return QImage();
    case Internal::Source::Held: {
        QBuffer device(&bytes);
        device.open(QIODevice::ReadOnly);
        // The format from the name, as QImage(path) takes it.
        QImageReader reader(&device, QFileInfo(path).suffix().toLatin1());
        QImage image = reader.read();
        if (image.isNull())
            image.loadFromData(bytes);
        return image;
    }
    default: return QImage(path);
    }
}

}
