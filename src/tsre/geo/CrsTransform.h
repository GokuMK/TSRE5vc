#pragma once

#include <array>

namespace Geo {

struct GeographicPoint {
    double latitude = 0;
    double longitude = 0;
};

struct ProjectedPoint {
    double x = 0;
    double y = 0;
};

// Small, deterministic CRS conversion used by TSRE elevation sources.
// Definitions are compiled in deliberately; this is not an EPSG database.
class CrsTransform {
public:
    explicit CrsTransform(int epsg);

    bool valid() const { return method != Method::Unsupported; }
    int epsg() const { return code; }
    bool forward(GeographicPoint point, ProjectedPoint &result) const;

    static bool supports(int epsg);

private:
    enum class Method {
        Unsupported,
        Geographic,
        WebMercator,
        SwissLv95,
        LambertAzimuthalEqualArea,
        TransverseMercator
    };

    void configureTransverseMercator(double centralMeridianDegrees,
                                     double scaleFactor,
                                     double falseNorthing,
                                     double minimumLatitude,
                                     double maximumLatitude,
                                     double minimumLongitude,
                                     double maximumLongitude);
    double authalicQ(double latitude) const;

    int code = 0;
    Method method = Method::Unsupported;
    double minLatitude = -90;
    double maxLatitude = 90;
    double minLongitude = -180;
    double maxLongitude = 180;

    double eccentricitySquared = 0;
    double eccentricity = 0;
    double centralMeridian = 0;
    double falseEasting = 0;
    double falseNorthing = 0;
    double rectifyingRadius = 0;
    std::array<double,4> alpha{{0,0,0,0}};

    double laeaQp = 0;
    double laeaBeta0 = 0;
    double laeaRadius = 0;
    double laeaD = 0;
};

}
