#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace Elevation {

struct Raster;

// Decode one complete TIFF strip/tile into numeric elevation samples.
// The narrow codec profile is shared by complete GeoTIFF and COG-window reads.
bool decodeTiffBlock(const QByteArray &encoded, int compression, int predictor,
                     bool littleEndian, int bitsPerSample, int sampleFormat,
                     int width, int height, QVector<float> &values, QString &error);

// Small local-cache writer: classic little-endian tiled Deflate Float32 GeoTIFF.
bool encodeGeoTiff(const Raster &raster, QByteArray &bytes, QString &error);

}
