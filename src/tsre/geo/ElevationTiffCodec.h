#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace Elevation {

// Decode one complete TIFF strip/tile into numeric elevation samples.
// The narrow codec profile is shared by complete GeoTIFF and COG-window reads.
bool decodeTiffBlock(const QByteArray &encoded, int compression, int predictor,
                     bool littleEndian, int bitsPerSample, int sampleFormat,
                     int width, int height, QVector<float> &values, QString &error);

}
