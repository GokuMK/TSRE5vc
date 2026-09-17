#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <array>
#include <limits>

namespace Elevation {

struct Point { double latitude = 0, longitude = 0; };
struct XY { double x = 0, y = 0; };
enum class SampleStatus { Valid, Outside, NoData, Unavailable };
struct Sample {
    float height = 0;
    SampleStatus status = SampleStatus::Unavailable;
    bool valid() const { return status == SampleStatus::Valid; }
};
// Forward horizontal conversion only: CS92, Web Mercator,
// Slovenia D96/TM, ETRS89 / UTM zones 28N-38N
// and EUREF-FIN / TM35FIN.
// Internal XY is always easting/northing, including EPSG:3045 (N-E axes).
// Geographic input is treated as ETRS89; no epoch or vertical shift.
bool project(Point point, int epsg, XY &result);
bool supportedCrs(int epsg);

struct Raster {
    int width = 0, height = 0, epsg = 0;
    // Pixel corner -> projected x/y, including rotation terms.
    std::array<double, 6> transform{{0, 1, 0, 0, 0, -1}};
    QVector<float> values;
    bool hasNoData = false;
    float noData = std::numeric_limits<float>::quiet_NaN();

    Sample sample(XY position, bool zeroIsNoData = false) const;
};

// Bounded numeric readers; unsupported image encodings are rejected.
bool readGeoTiff(const QByteArray &bytes, Raster &raster, QString &error);
// Bare GeoTIFF or a WCS multipart TIFF with its accompanying GML CRS.
bool readWcsTiff(const QByteArray &bytes, int expectedEpsg, Raster &raster, QString &error);
bool readAsciiGrid(const QByteArray &bytes, int epsg, Raster &raster, QString &error);
bool readHgt(const QByteArray &bytes, int latitude, int longitude,
             Raster &raster, QString &error);
// Preserve the existing HGT interpolation for valid samples while detecting voids.
Sample sampleLegacyHgt(const Raster &raster, Point point);

}
