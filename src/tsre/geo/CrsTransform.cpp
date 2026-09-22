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

    // SWEREF 99 TM uses the UTM zone 33N Transverse Mercator parameters.
    if (epsg == 3006)
        return 33;

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

    if (epsg == 3763) {
        // ETRS89 / Portugal TM06. DGT publishes this GRS80 Transverse
        // Mercator definition for mainland Portuguese mapping.
        configureTransverseMercator(Grs80SemiMajorAxis, Grs80InverseFlattening,
                                    39.6682583333333, -8.13310833333333,
                                    1.0, 0, 0,
                                    36.5, 42.5, -10, -5.5);
        return;
    }

    if (epsg == 2169) {
        // ETRS89 -> LUREF2020 Molodensky-Badekas transformation followed by
        // Luxembourg TM on the International 1924 ellipsoid. Parameters are
        // published by Luxembourg ACT (July 2024).
        useLuref2020 = true;
        configureTransverseMercator(6378388.0, 297.0,
                                    49.8333333333333, 6.16666666666667,
                                    1.0, 80000, 100000,
                                    49.44, 50.19, 5.73, 6.53);
        return;
    }

    if (epsg == 2154) {
        // RGF93 v1 / Lambert-93. RGF93 and ETRS89 use the GRS80 ellipsoid;
        // no horizontal datum grid is required for this elevation workflow.
        configureLambertConformalConic(
            Grs80SemiMajorAxis, Grs80InverseFlattening,
            46.5, 3.0, 49.0, 44.0, 700000, 6600000,
            41, 52, -6, 10);
        return;
    }

    if (epsg == 2180) {
        configureTransverseMercator(19, .9993, -5300000,
                                    48, 57, 13, 25);
        return;
    }

    if (epsg == 27700) {
        // First project ETRS89 on the GRS80 ellipsoid to the National Grid
        // pseudo-grid. OSTN15 horizontal shifts are applied when a grid has
        // been supplied by the caller.
        configureTransverseMercator(Grs80SemiMajorAxis, Grs80InverseFlattening,
                                    49, -2, .9996012717, 400000, -100000,
                                    49, 61.5, -9, 3.5);
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

bool CrsTransform::setHorizontalShiftGrid(
        double originEasting, double originNorthing, double spacing,
        int width, int height, std::vector<std::array<double,2>> shifts) {
    if (!(spacing > 0) || width < 2 || height < 2
            || size_t(width)*size_t(height) != shifts.size())
        return false;
    for (const auto &shift : shifts)
        if (!std::isfinite(shift[0]) || !std::isfinite(shift[1]))
            return false;
    gridOriginEasting = originEasting;
    gridOriginNorthing = originNorthing;
    gridSpacing = spacing;
    gridWidth = width;
    gridHeight = height;
    horizontalShifts = std::move(shifts);
    return true;
}

void CrsTransform::configureTransverseMercator(
        double centralMeridianDegrees, double scaleFactor,
        double northingOffset, double minimumLatitude,
        double maximumLatitude, double minimumLongitude,
        double maximumLongitude) {
    configureTransverseMercator(Grs80SemiMajorAxis, Grs80InverseFlattening,
                                0, centralMeridianDegrees, scaleFactor,
                                500000, northingOffset,
                                minimumLatitude, maximumLatitude,
                                minimumLongitude, maximumLongitude);
}

void CrsTransform::configureTransverseMercator(
        double axis, double inverseFlattening, double latitudeOriginDegrees,
        double centralMeridianDegrees, double scaleFactor,
        double eastingOffset, double northingOffset,
        double minimumLatitude, double maximumLatitude,
        double minimumLongitude, double maximumLongitude) {
    method = Method::TransverseMercator;
    minLatitude = minimumLatitude;
    maxLatitude = maximumLatitude;
    minLongitude = minimumLongitude;
    maxLongitude = maximumLongitude;
    centralMeridian = centralMeridianDegrees * DegreesToRadians;
    falseEasting = eastingOffset;
    falseNorthing = northingOffset;

    semiMajorAxis = axis;
    const double flattening = 1.0 / inverseFlattening;
    eccentricitySquared = flattening * (2.0 - flattening);
    eccentricity = std::sqrt(eccentricitySquared);
    const double n = flattening / (2.0 - flattening);
    const double n2 = n * n;
    const double n3 = n2 * n;
    const double n4 = n2 * n2;
    rectifyingRadius = scaleFactor * semiMajorAxis / (1.0 + n)
            * (1.0 + n2 / 4.0 + n4 / 64.0);
    alpha = {{
        n / 2.0 - 2.0 * n2 / 3.0 + 5.0 * n3 / 16.0 + 41.0 * n4 / 180.0,
        13.0 * n2 / 48.0 - 3.0 * n3 / 5.0 + 557.0 * n4 / 1440.0,
        61.0 * n3 / 240.0 - 103.0 * n4 / 140.0,
        49561.0 * n4 / 161280.0
    }};
    double unusedEasting = 0;
    transverseMercatorRaw(latitudeOriginDegrees * DegreesToRadians, 0,
                          unusedEasting, originNorthing);
}

void CrsTransform::transverseMercatorRaw(
        double latitude, double longitude, double &east, double &north) const {
    const double t = std::sinh(
            std::asinh(std::tan(latitude))
            - eccentricity * std::atanh(eccentricity * std::sin(latitude)));
    const double xi = std::atan2(t, std::cos(longitude));
    const double eta = std::asinh(
            std::sin(longitude) / std::hypot(t, std::cos(longitude)));
    north = xi;
    east = eta;
    for (int j = 1; j <= 4; ++j) {
        north += alpha[j - 1] * std::sin(2.0 * j * xi)
                * std::cosh(2.0 * j * eta);
        east += alpha[j - 1] * std::cos(2.0 * j * xi)
                * std::sinh(2.0 * j * eta);
    }
    east *= rectifyingRadius;
    north *= rectifyingRadius;
}

double CrsTransform::conformalT(double latitude) const {
    const double sine = std::sin(latitude);
    return std::tan(Pi / 4.0 - latitude / 2.0)
            / std::pow((1.0 - eccentricity * sine)
                       / (1.0 + eccentricity * sine), eccentricity / 2.0);
}

void CrsTransform::configureLambertConformalConic(
        double axis, double inverseFlattening,
        double latitudeOriginDegrees, double centralMeridianDegrees,
        double firstParallelDegrees, double secondParallelDegrees,
        double eastingOffset, double northingOffset,
        double minimumLatitude, double maximumLatitude,
        double minimumLongitude, double maximumLongitude) {
    method = Method::LambertConformalConic;
    minLatitude = minimumLatitude;
    maxLatitude = maximumLatitude;
    minLongitude = minimumLongitude;
    maxLongitude = maximumLongitude;
    centralMeridian = centralMeridianDegrees * DegreesToRadians;
    falseEasting = eastingOffset;
    falseNorthing = northingOffset;
    semiMajorAxis = axis;

    const double flattening = 1.0 / inverseFlattening;
    eccentricitySquared = flattening * (2.0 - flattening);
    eccentricity = std::sqrt(eccentricitySquared);
    const double first = firstParallelDegrees * DegreesToRadians;
    const double second = secondParallelDegrees * DegreesToRadians;
    const auto m = [&](double latitude) {
        const double sine = std::sin(latitude);
        return std::cos(latitude)
                / std::sqrt(1.0 - eccentricitySquared * sine * sine);
    };
    const double firstT = conformalT(first);
    const double secondT = conformalT(second);
    lccExponent = (std::log(m(first)) - std::log(m(second)))
            / (std::log(firstT) - std::log(secondT));
    lccFactor = m(first)
            / (lccExponent * std::pow(firstT, lccExponent));
    lccOriginRadius = semiMajorAxis * lccFactor
            * std::pow(conformalT(latitudeOriginDegrees * DegreesToRadians),
                       lccExponent);
}

bool CrsTransform::etrs89ToLuref(
        GeographicPoint point, double &latitude, double &longitude) const {
    constexpr double sourceAxis = Grs80SemiMajorAxis;
    constexpr double sourceFlattening = 1.0 / Grs80InverseFlattening;
    constexpr double sourceEccentricitySquared =
            sourceFlattening * (2.0 - sourceFlattening);
    const double phi = point.latitude * DegreesToRadians;
    const double lambda = point.longitude * DegreesToRadians;
    const double sine = std::sin(phi);
    const double primeVertical = sourceAxis
            / std::sqrt(1.0 - sourceEccentricitySquared * sine * sine);
    double x = primeVertical * std::cos(phi) * std::cos(lambda);
    double y = primeVertical * std::cos(phi) * std::sin(lambda);
    double z = primeVertical * (1.0 - sourceEccentricitySquared) * sine;

    // Official ETRF2000 -> LUREF2020 Molodensky-Badekas parameters.
    // ACT publishes coordinate-frame rotations; their signs are reversed in
    // the position-vector matrix below.
    constexpr double x0 = 4101567.0943;
    constexpr double y0 = 440245.0881;
    constexpr double z0 = 4848681.4115;
    constexpr double arcSecondsToRadians = DegreesToRadians / 3600.0;
    constexpr double rx = 0.48171 * arcSecondsToRadians;
    constexpr double ry = 3.09948 * arcSecondsToRadians;
    constexpr double rz = -2.68639 * arcSecondsToRadians;
    constexpr double scale = 1.0 - 0.46346e-6;
    x -= x0;
    y -= y0;
    z -= z0;
    const double targetX = x0 + 265.9196 + scale * x - rz * y + ry * z;
    const double targetY = y0 - 76.9506 + rz * x + scale * y - rx * z;
    const double targetZ = z0 - 20.2222 - ry * x + rx * y + scale * z;

    longitude = std::atan2(targetY, targetX);
    const double horizontal = std::hypot(targetX, targetY);
    latitude = std::atan2(targetZ,
                          horizontal * (1.0 - eccentricitySquared));
    for (int i = 0; i < 10; ++i) {
        const double targetSine = std::sin(latitude);
        const double radius = semiMajorAxis / std::sqrt(
                1.0 - eccentricitySquared * targetSine * targetSine);
        const double height = horizontal / std::cos(latitude) - radius;
        const double next = std::atan2(
                targetZ, horizontal * (1.0
                    - eccentricitySquared * radius / (radius + height)));
        if (std::abs(next - latitude) < 1e-15) {
            latitude = next;
            break;
        }
        latitude = next;
    }
    return std::isfinite(latitude) && std::isfinite(longitude);
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

    if (method == Method::LambertConformalConic) {
        const double latitude = point.latitude * DegreesToRadians;
        const double longitude = point.longitude * DegreesToRadians;
        const double radius = semiMajorAxis * lccFactor
                * std::pow(conformalT(latitude), lccExponent);
        const double theta = lccExponent * (longitude - centralMeridian);
        out.x = falseEasting + radius * std::sin(theta);
        out.y = falseNorthing + lccOriginRadius - radius * std::cos(theta);
        return std::isfinite(out.x) && std::isfinite(out.y);
    }

    double latitude = point.latitude * DegreesToRadians;
    double longitude = point.longitude * DegreesToRadians;
    if (useLuref2020 && !etrs89ToLuref(point, latitude, longitude))
        return false;
    double east = 0;
    double north = 0;
    transverseMercatorRaw(latitude, longitude - centralMeridian, east, north);
    out = {
        falseEasting + east,
        falseNorthing + north - originNorthing
    };
    if (!horizontalShifts.empty()) {
        const double column = (out.x-gridOriginEasting)/gridSpacing;
        const double row = (out.y-gridOriginNorthing)/gridSpacing;
        if (column < 0 || row < 0 || column > gridWidth-1 || row > gridHeight-1)
            return false;
        const int x = std::min(int(std::floor(column)),gridWidth-2);
        const int y = std::min(int(std::floor(row)),gridHeight-2);
        const double fx = column-x, fy = row-y;
        const auto interpolate = [&](int component) {
            const double a = horizontalShifts[y*gridWidth+x][component];
            const double b = horizontalShifts[y*gridWidth+x+1][component];
            const double c = horizontalShifts[(y+1)*gridWidth+x][component];
            const double d = horizontalShifts[(y+1)*gridWidth+x+1][component];
            return (a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy;
        };
        out.x += interpolate(0);
        out.y += interpolate(1);
    }
    return std::isfinite(out.x) && std::isfinite(out.y);
}

}
