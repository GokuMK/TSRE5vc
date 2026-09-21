#include <tsre/geo/CrsTransform.h>

#include <algorithm>
#include <cmath>

namespace Geo {
namespace {
constexpr double Pi = 3.14159265358979323846;
constexpr double DegreesToRadians = Pi / 180.0;
constexpr double Grs80SemiMajorAxis = 6378137.0;
constexpr double Grs80InverseFlattening = 298.257222101;

int transverseMercatorZone(int epsg) {
    // ETRS89 / UTM zones 28N..38N.
    if (epsg >= 25828 && epsg <= 25838)
        return epsg - 25800;

    // ETRS89 / UTM zone 33N with northing/easting axis declaration.
    if (epsg == 3045)
        return 33;

    // EUREF-FIN / TM35FIN uses the same TM parameters as UTM zone 35N.
    if (epsg == 3067)
        return 35;

    return 0;
}
}

CrsTransform::CrsTransform(int epsg)
    : code(epsg)
{
    if (epsg == 4326) {
        method = Method::Geographic;
        return;
    }

    if (epsg == 3857) {
        method = Method::WebMercator;
        minLatitude = -85.0511287798066;
        maxLatitude = 85.0511287798066;
        return;
    }

    if (epsg == 2056) {
        method = Method::SwissLv95;
        minLatitude = 45.5;
        maxLatitude = 48;
        minLongitude = 5.5;
        maxLongitude = 11;
        return;
    }

    if (epsg == 3035) {
        method = Method::LambertAzimuthalEqualArea;
        minLatitude = 24;
        maxLatitude = 72;
        minLongitude = -35;
        maxLongitude = 45;

        const double flattening = 1.0 / Grs80InverseFlattening;
        eccentricitySquared = flattening * (2.0 - flattening);
        eccentricity = std::sqrt(eccentricitySquared);
        const double phi0 = 52 * DegreesToRadians;
        laeaQp = authalicQ(Pi / 2);
        laeaBeta0 = std::asin(authalicQ(phi0) / laeaQp);
        laeaRadius = Grs80SemiMajorAxis * std::sqrt(laeaQp / 2);
        const double sine = std::sin(phi0);
        const double m0 = std::cos(phi0)
                / std::sqrt(1.0 - eccentricitySquared * sine * sine);
        laeaD = Grs80SemiMajorAxis * m0
                / (laeaRadius * std::cos(laeaBeta0));
        centralMeridian = 10 * DegreesToRadians;
        falseEasting = 4321000;
        falseNorthing = 3210000;
        return;
    }

    if (epsg == 2180) {
        configureTransverseMercator(19, .9993, -5300000,
                                    48, 57, 13, 25);
        return;
    }

    if (epsg == 3794) {
        configureTransverseMercator(15, .9999, -5000000,
                                    45, 47.5, 13, 17);
        return;
    }

    const int zone = transverseMercatorZone(epsg);
    if (zone != 0) {
        const double meridian = zone * 6.0 - 183.0;
        // Retain the existing bounded implementation: wider than a nominal
        // UTM zone because some national services use one grid beyond it.
        configureTransverseMercator(meridian, .9996, 0,
                                    0, 84, meridian - 30, meridian + 30);
    }
}

bool CrsTransform::supports(int epsg) {
    return CrsTransform(epsg).valid();
}

void CrsTransform::configureTransverseMercator(
        double centralMeridianDegrees, double scaleFactor,
        double northingOffset, double minimumLatitude,
        double maximumLatitude, double minimumLongitude,
        double maximumLongitude) {
    method = Method::TransverseMercator;
    minLatitude = minimumLatitude;
    maxLatitude = maximumLatitude;
    minLongitude = minimumLongitude;
    maxLongitude = maximumLongitude;
    centralMeridian = centralMeridianDegrees * DegreesToRadians;
    falseEasting = 500000;
    falseNorthing = northingOffset;

    const double flattening = 1.0 / Grs80InverseFlattening;
    eccentricitySquared = flattening * (2.0 - flattening);
    eccentricity = std::sqrt(eccentricitySquared);
    const double n = flattening / (2.0 - flattening);
    const double n2 = n * n;
    const double n3 = n2 * n;
    const double n4 = n2 * n2;
    rectifyingRadius = scaleFactor * Grs80SemiMajorAxis / (1.0 + n)
            * (1.0 + n2 / 4.0 + n4 / 64.0);
    alpha = {{
        n / 2.0 - 2.0 * n2 / 3.0 + 5.0 * n3 / 16.0 + 41.0 * n4 / 180.0,
        13.0 * n2 / 48.0 - 3.0 * n3 / 5.0 + 557.0 * n4 / 1440.0,
        61.0 * n3 / 240.0 - 103.0 * n4 / 140.0,
        49561.0 * n4 / 161280.0
    }};
}

double CrsTransform::authalicQ(double latitude) const {
    const double sine = std::sin(latitude);
    return (1.0 - eccentricitySquared)
            * (sine / (1.0 - eccentricitySquared * sine * sine)
               - std::log((1.0 - eccentricity * sine)
                          / (1.0 + eccentricity * sine))
                 / (2.0 * eccentricity));
}

bool CrsTransform::forward(GeographicPoint point, ProjectedPoint &out) const {
    if (!valid() || !std::isfinite(point.latitude)
            || !std::isfinite(point.longitude)
            || point.latitude < -90 || point.latitude > 90
            || point.longitude < -180 || point.longitude > 180
            || point.latitude < minLatitude || point.latitude > maxLatitude
            || point.longitude < minLongitude || point.longitude > maxLongitude)
        return false;

    if (method == Method::Geographic) {
        out = {point.longitude, point.latitude};
        return true;
    }

    if (method == Method::WebMercator) {
        const double longitude = point.longitude * DegreesToRadians;
        const double latitude = point.latitude * DegreesToRadians;
        out.x = Grs80SemiMajorAxis * longitude;
        out.y = Grs80SemiMajorAxis
                * std::log(std::tan(Pi / 4.0 + latitude / 2.0));
        return std::isfinite(out.x) && std::isfinite(out.y);
    }

    if (method == Method::SwissLv95) {
        // Official swisstopo WGS84 -> LV95 approximation. Its published
        // horizontal accuracy is better than one metre across Switzerland.
        const double phi = (point.latitude * 3600.0 - 169028.66) / 10000.0;
        const double lambda = (point.longitude * 3600.0 - 26782.5) / 10000.0;
        out.x = 2600072.37 + 211455.93 * lambda - 10938.51 * lambda * phi
                - .36 * lambda * phi * phi - 44.54 * lambda * lambda * lambda;
        out.y = 1200147.07 + 308807.95 * phi + 3745.25 * lambda * lambda
                + 76.63 * phi * phi - 194.56 * lambda * lambda * phi
                + 119.79 * phi * phi * phi;
        return std::isfinite(out.x) && std::isfinite(out.y);
    }

    if (method == Method::LambertAzimuthalEqualArea) {
        const double latitude = point.latitude * DegreesToRadians;
        const double longitude = point.longitude * DegreesToRadians;
        const double beta = std::asin(std::clamp(
                authalicQ(latitude) / laeaQp, -1.0, 1.0));
        const double deltaLongitude = longitude - centralMeridian;
        const double denominator = 1.0
                + std::sin(laeaBeta0) * std::sin(beta)
                + std::cos(laeaBeta0) * std::cos(beta)
                  * std::cos(deltaLongitude);
        if (denominator <= 0)
            return false;
        const double b = laeaRadius * std::sqrt(2.0 / denominator);
        out.x = falseEasting
                + b * laeaD * std::cos(beta) * std::sin(deltaLongitude);
        out.y = falseNorthing
                + b / laeaD
                  * (std::cos(laeaBeta0) * std::sin(beta)
                     - std::sin(laeaBeta0) * std::cos(beta)
                       * std::cos(deltaLongitude));
        return std::isfinite(out.x) && std::isfinite(out.y);
    }

    const double latitude = point.latitude * DegreesToRadians;
    const double longitude = point.longitude * DegreesToRadians
            - centralMeridian;
    const double t = std::sinh(
            std::asinh(std::tan(latitude))
            - eccentricity * std::atanh(eccentricity * std::sin(latitude)));
    const double xi = std::atan2(t, std::cos(longitude));
    const double eta = std::asinh(
            std::sin(longitude) / std::hypot(t, std::cos(longitude)));
    double north = xi;
    double east = eta;
    for (int j = 1; j <= 4; ++j) {
        north += alpha[j - 1] * std::sin(2.0 * j * xi)
                * std::cosh(2.0 * j * eta);
        east += alpha[j - 1] * std::cos(2.0 * j * xi)
                * std::sinh(2.0 * j * eta);
    }
    out = {
        falseEasting + rectifyingRadius * east,
        falseNorthing + rectifyingRadius * north
    };
    return std::isfinite(out.x) && std::isfinite(out.y);
}

}
