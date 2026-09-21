#pragma once

#include <array>
#include <vector>

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
    bool setHorizontalShiftGrid(double originEasting, double originNorthing,
                                double spacing, int width, int height,
                                std::vector<std::array<double,2>> shifts);
    bool hasHorizontalShiftGrid() const { return !horizontalShifts.empty(); }

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
    void configureTransverseMercator(double semiMajorAxis,
                                     double inverseFlattening,
                                     double latitudeOriginDegrees,
                                     double centralMeridianDegrees,
                                     double scaleFactor,
                                     double eastingOffset,
                                     double northingOffset,
                                     double minimumLatitude,
                                     double maximumLatitude,
                                     double minimumLongitude,
                                     double maximumLongitude);
    void transverseMercatorRaw(double latitude, double longitudeDelta,
                               double &easting, double &northing) const;
    bool etrs89ToLuref(GeographicPoint point, double &latitude,
                       double &longitude) const;
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
    double originNorthing = 0;
    double semiMajorAxis = 0;
    double rectifyingRadius = 0;
    std::array<double,4> alpha{{0,0,0,0}};
    bool useLuref2020 = false;
    double gridOriginEasting = 0;
    double gridOriginNorthing = 0;
    double gridSpacing = 0;
    int gridWidth = 0;
    int gridHeight = 0;
    std::vector<std::array<double,2>> horizontalShifts;

    double laeaQp = 0;
    double laeaBeta0 = 0;
    double laeaRadius = 0;
    double laeaD = 0;
};

}
