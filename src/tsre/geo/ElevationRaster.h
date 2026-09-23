#pragma once

#include <tsre/geo/CrsTransform.h>
#include <QByteArray>
#include <QBitArray>
#include <QString>
#include <QVector>
#include <array>
#include <atomic>
#include <limits>

namespace Elevation {

using Point = Geo::GeographicPoint;
using XY = Geo::ProjectedPoint;
enum class SampleStatus { Valid, Outside, NoData, Unavailable };
struct Sample {
    float height = 0;
    SampleStatus status = SampleStatus::Unavailable;
    bool valid() const { return status == SampleStatus::Valid; }
};
struct Raster {
    int width = 0, height = 0, epsg = 0;
    // Pixel corner -> projected x/y, including rotation terms.
    std::array<double, 6> transform{{0, 1, 0, 0, 0, -1}};
    QVector<float> values;
    bool hasNoData = false;
    float noData = std::numeric_limits<float>::quiet_NaN();

    Sample sample(XY position, bool zeroIsNoData = false) const;
};

// Grow neighbouring valid heights into NoData in simultaneous layers. An optional
// availability mask prevents filling missing downloads or crossing their cells.
int fillNoData(Raster &raster, bool zeroIsNoData, std::atomic_bool &cancel,
               const QBitArray &available = {});

// Bounded numeric readers; unsupported image encodings are rejected.
bool readGeoTiff(const QByteArray &bytes, Raster &raster, QString &error);
// Bare GeoTIFF or a WCS multipart TIFF with its accompanying GML CRS.
bool readWcsTiff(const QByteArray &bytes, int expectedEpsg, Raster &raster, QString &error);
bool readAsciiGrid(const QByteArray &bytes, int epsg, Raster &raster, QString &error);
// Convert a regular grid whose file X/Y axes are the CRS's second/first axes
// into TSRE's conventional projected X=easting, Y=northing layout.
bool swapRasterAxes(Raster &raster, QString &error);
// Read an unheaded regular XYZ point grid. axisSwap accepts formal CRS axis
// order (northing, easting, height) used by some national download products.
bool readXyzGrid(const QByteArray &bytes, int epsg, double resolution,
                 bool axisSwap, Raster &raster, QString &error);
bool readHgt(const QByteArray &bytes, int latitude, int longitude,
             Raster &raster, QString &error);
// Preserve the existing HGT interpolation for valid samples while detecting voids.
Sample sampleLegacyHgt(const Raster &raster, Point point);

}
