/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  This file is conversion of:
 *  Jgr.Msts library, part of MSTS Editors & Tools 
 *  (http://jgrmsts.codeplex.com/).
 * 
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/geo/GeoCoordinates.h>
#include <QDebug>

QString GeoProjectionTypeToString(GeoProjectionType type) {
    switch(type) {
    case GeoProjectionType::InterruptedGoodeHomolosine:
        return "InterruptedGoodeHomolosine";
    case GeoProjectionType::LocalEllipsoidalEquirectangular:
        return "LocalEllipsoidalEquirectangular";
    case GeoProjectionType::TransverseMercator:
        return "TransverseMercator";
    case GeoProjectionType::Undefined:
    default:
        return "Undefined";
    }
}

GeoProjectionType GeoProjectionTypeFromString(const QString &value) {
    if(value.compare("InterruptedGoodeHomolosine", Qt::CaseInsensitive) == 0)
        return GeoProjectionType::InterruptedGoodeHomolosine;

    if(value.compare("LocalEllipsoidalEquirectangular", Qt::CaseInsensitive) == 0)
        return GeoProjectionType::LocalEllipsoidalEquirectangular;

    if(value.compare("TransverseMercator", Qt::CaseInsensitive) == 0)
        return GeoProjectionType::TransverseMercator;

    return GeoProjectionType::Undefined;
}

constexpr double GeoMstsCoordinateConverter::IghLongitudeCenter[12];

GeoWorldCoordinateConverter* GeoWorldCoordinateConverter::Create(
        GeoProjectionType type,
        double *projection) {

    switch(type) {
        case GeoProjectionType::InterruptedGoodeHomolosine:
            return new GeoMstsCoordinateConverter();

        case GeoProjectionType::LocalEllipsoidalEquirectangular:
            return new GeoTsreCoordinateConverter(projection);

        case GeoProjectionType::TransverseMercator:
            return new GeoTsreTransverseMercatorCoordinateConverter(projection);

        case GeoProjectionType::Undefined:
        default:
            qWarning() << "Cannot create undefined geo projection";
            return nullptr;
    }
}

GeoWorldCoordinateConverter::GeoWorldCoordinateConverter(double tileOffsetX, double tileOffsetZ, int tileZDirection)
    : tileOffsetX(tileOffsetX), tileOffsetZ(tileOffsetZ), tileZDirection(tileZDirection) {
}

IghCoordinate* GeoWorldCoordinateConverter::ConvertToInternal(PreciseTileCoordinate* coordinates, IghCoordinate* out){
    return ConvertToInternal(coordinates->TileX, coordinates->TileZ, coordinates->X, coordinates->Z, out);
}

IghCoordinate* GeoWorldCoordinateConverter::ConvertToInternal(int tilex, int tilez, double x, double z, IghCoordinate* out){
    double line = tileZDirection * 2048.0 * (tilez + (1.0 - z)) + tileOffsetZ;
    double sample = 2048.0 * (tilex + x) + tileOffsetX;
    if(out == 0)
        return new IghCoordinate(line, sample);
    out->set(line, sample);
    return out;
}

PreciseTileCoordinate* GeoWorldCoordinateConverter::ConvertToTile(
        IghCoordinate* coordinates, PreciseTileCoordinate* out) {

    double tileX = (coordinates->Sample - tileOffsetX) / 2048.0;
    double tileZ = tileZDirection * (coordinates->Line - tileOffsetZ) / 2048.0;

    int tileXi = (int)floor(tileX);
    double x = tileX - tileXi;

    int tileZi = (int)ceil(tileZ) - 1;
    double z = (tileZi + 1) - tileZ;

    if(out == 0)
        return new PreciseTileCoordinate(tileXi, tileZi, x, z);

    out->set(tileXi, tileZi, x, z);
    return out;
}

LatitudeLongitudeCoordinate* GeoWorldCoordinateConverter::ConvertToLatLon(IghCoordinate* coordinates, LatitudeLongitudeCoordinate* out){
    return NULL;
}

IghCoordinate* GeoWorldCoordinateConverter::ConvertToInternal(LatitudeLongitudeCoordinate* coordinates, IghCoordinate* out){
    return NULL;
}

IghCoordinate* GeoWorldCoordinateConverter::ConvertToInternal(double lat, double lon, IghCoordinate* out){
    return NULL;
}

GeoMstsCoordinateConverter::GeoMstsCoordinateConverter()
    : GeoWorldCoordinateConverter(2048.0 * 16384.0, 2048.0 * 16384.0, -1) {
}

LatitudeLongitudeCoordinate* GeoMstsCoordinateConverter::ConvertToLatLon(IghCoordinate* coordinates, LatitudeLongitudeCoordinate* out) {
    // Line/Sample -> Latitude/Longitude Algorithm
    // Based on C code provided by the USGS, available at ftp://edcftp.cr.usgs.gov/pub/software/misc/gihll2ls.c.
    // By D. Steinwand, HSTX/EROS Data Center, June, 1993.

    //Debug.Assert(coordinates.Line >= 0, "line is off the top");
    //Debug.Assert(coordinates.Line <= IghImageHeight, "line is off the bottom");
    //Debug.Assert(coordinates.Sample >= 0, "line is off the left");
    //Debug.Assert(coordinates.Sample <= IghImageWidth, "line is off the right");

    double y = (IghImageTop - coordinates->Line) / IghRadius;
    double x = (IghImageLeft + coordinates->Sample) / IghRadius;

    //Debug.Assert(y >= -M_PI / 2, "y is off the bottom");
    //Debug.Assert(y <= +M_PI / 2, "y is off the top");
    //Debug.Assert(x >= -M_PI, "x is off the left");
    //Debug.Assert(x <= +M_PI, "x is off the right");

    int region = -1;
    if (y >= IghParallel41) { /* If on or above 40 44' 11.8" */
        if (x <= -IghMeridian40) { /* If to the left of -40 */
            region = 0;
        } else {
            region = 2;
        }
    } else if (y >= 0.0) { /* Between 0.0 and 40 44' 11.8" */
        if (x <= -IghMeridian40) { /* If to the left of -40 */
            region = 1;
        } else {
            region = 3;
        }
    } else if (y >= -IghParallel41) { /* Between 0.0 & -40 44' 11.8" */
        if (x <= -IghMeridian100) { /* If between -180 and -100 */
            region = 4;
        } else if (x <= -IghMeridian20) { /* If between -100 and -20 */
            region = 5;
        } else if (x <= IghMeridian80) { /* If between -20 and 80 */
            region = 8;
        } else { /* If between 80 and 180 */
            region = 9;
        }
    } else { /* Below -40 44' 11.8" */
        if (x <= -IghMeridian100) { /* If between -180 and -100 */
            region = 6;
        } else if (x <= -IghMeridian20) { /* If between -100 and -20 */
            region = 7;
        } else if (x <= IghMeridian80) { /* If between -20 and 80 */
            region = 10;
        } else { /* If between 80 and 180 */
            region = 11;
        }
    }
    x = x - IghLongitudeCenter[region];

    double lat = 0;
    double lon = 0;
    if ((region == 1) || (region == 3) || (region == 4) || (region == 5) || (region == 8) || (region == 9)) {
        lat = y;
        if (fabs(lat) > M_PI / 2) {
            // giherror("Input data error", "Goode-inverse");
            return 0;
        }
        double temp = fabs(lat) - M_PI / 2;
        if (fabs(temp) > 0.00000001) {
            temp = IghLongitudeCenter[region] + x / cos(lat);
            lon = adjust_lon(temp);
        } else {
            lon = IghLongitudeCenter[region];
        }
    } else {
        double arg = (y + 0.0528035274542 * sign(y)) / 1.4142135623731;
        if (fabs(arg) > 1.0) {
            return 0;
        }
        double theta = asin(arg);
        lon = IghLongitudeCenter[region] + (x / (0.900316316158 * cos(theta)));
        if (lon < -M_PI) {
            return 0;
        }
        arg = (2.0 * theta + sin(2.0 * theta)) / M_PI;
        if (fabs(arg) > 1.0) {
            return 0;
        }
        lat = asin(arg);
    }

    ///////////////////////////////////////////////////////////////////
    if(out == 0)
        return new LatitudeLongitudeCoordinate(lat * 180 / M_PI, lon * 180 / M_PI);
    out->set(lat * 180 / M_PI, lon * 180 / M_PI);
    return out;
}

// Lat/Lon -> MSTS IGH
IghCoordinate* GeoMstsCoordinateConverter::ConvertToInternal(LatitudeLongitudeCoordinate* coordinates, IghCoordinate* out) {
    return ConvertToInternal(coordinates->Latitude, coordinates->Longitude, out);
}

IghCoordinate* GeoMstsCoordinateConverter::ConvertToInternal(double lat, double lon, IghCoordinate* out) {
    // Latitude/Longitude -> Line/Sample Algorithm
    // Based on C code provided by the USGS, available at ftp://edcftp.cr.usgs.gov/pub/software/misc/gihll2ls.c.
    // By D. Steinwand, HSTX/EROS Data Center, June, 1993.

    //Debug.Assert(coordinates.Latitude >= -90, "latitude is off the bottom");
    //Debug.Assert(coordinates.Latitude <= 90, "latitude is off the top");
    //Debug.Assert(coordinates.Longitude >= -180, "longitude is off the left");
    //Debug.Assert(coordinates.Longitude <= 180, "longitude is off the right");

    lat = lat * M_PI / 180;
    lon = lon * M_PI / 180;

    int region = -1;
    if (lat >= IghParallel41) { /* If on or above 40 44' 11.8" */
        if (lon <= -IghMeridian40) { /* If to the left of -40 */
            region = 0;
        } else {
            region = 2;
        }
    } else if (lat >= 0.0) { /* Between 0.0 and 40 44' 11.8" */
        if (lon <= -IghMeridian40) { /* If to the left of -40 */
            region = 1;
        } else {
            region = 3;
        }
    } else if (lat >= -IghParallel41) { /* Between 0.0 & -40 44' 11.8" */
        if (lon <= -IghMeridian100) { /* If between -180 and -100 */
            region = 4;
        } else if (lon <= -IghMeridian20) { /* If between -100 and -20 */
            region = 5;
        } else if (lon <= IghMeridian80) { /* If between -20 and 80 */
            region = 8;
        } else { /* If between 80 and 180 */
            region = 9;
        }
    } else { /* Below -40 44' 11.8" */
        if (lon <= -IghMeridian100) { /* If between -180 and -100 */
            region = 6;
        } else if (lon <= -IghMeridian20) { /* If between -100 and -20 */
            region = 7;
        } else if (lon <= IghMeridian80) { /* If between -20 and 80 */
            region = 10;
        } else { /* If between 80 and 180 */
            region = 11;
        }
    }

    double y = 0;
    double x = 0;
    if ((region == 1) || (region == 3) || (region == 4) || (region == 5) || (region == 8) || (region == 9)) {
        double delta_lon = adjust_lon(lon - IghLongitudeCenter[region]);
        y = lat;
        x = IghLongitudeCenter[region] + delta_lon * cos(lat);
    } else {
        double delta_lon = adjust_lon(lon - IghLongitudeCenter[region]);
        double theta = lat;
        double constant = M_PI * sin(lat);

        /* Iterate using the Newton-Raphson method to find theta
          -----------------------------------------------------*/
        for (int i = 0;; i++) {
            double delta_theta = -(theta + sin(theta) - constant) / (1.0 + cos(theta));
            theta += delta_theta;
            if (fabs(delta_theta) < 0.00000000001) break;
            if (i >= 30) {
                //giherror("Iteration failed to converge", "Goode-forward");
                return 0;
            }
        }
        theta /= 2.0;
        y = 1.4142135623731 * sin(theta) - 0.0528035274542 * sign(lat);
        x = IghLongitudeCenter[region] + 0.900316316158 * delta_lon * cos(theta);
    }

    //Debug.Assert(y >= -M_PI / 2, "y is off the bottom");
    //Debug.Assert(y <= +M_PI / 2, "y is off the top");
    //Debug.Assert(x >= -M_PI, "x is off the left");
    //Debug.Assert(x <= +M_PI, "x is off the right");

    //IghCoordinate* igh = new IghCoordinate(IghImageTop - y * IghRadius, x * IghRadius - IghImageLeft);

    //Debug.Assert(igh.Line >= 0, "line is off the top");
    //Debug.Assert(igh.Line <= IghImageHeight, "line is off the bottom");
    //Debug.Assert(igh.Sample >= 0, "line is off the left");
    //Debug.Assert(igh.Sample <= IghImageWidth, "line is off the right");

    if(out == 0)
        return new IghCoordinate(IghImageTop - y * IghRadius, x * IghRadius - IghImageLeft);
    out->set(IghImageTop - y * IghRadius, x * IghRadius - IghImageLeft);
    return out;
}

double GeoMstsCoordinateConverter::adjust_lon(double temp) {
    if (fabs(temp) >= M_PI) {
        if (temp > 0)
            return temp - 2 * M_PI;
        if (temp < 0)
            return temp + 2 * M_PI;
    }
    return temp;
}

double GeoMstsCoordinateConverter::sign(double a) {
    if (a < 0) return -1;
    return 1;
}

GeoTsreCoordinateConverter::GeoTsreCoordinateConverter(double *latLonXY)
    : GeoWorldCoordinateConverter(-latLonXY[2], -latLonXY[3], 1) {
    centerLat = latLonXY[0];
    centerLon = latLonXY[1];

    double centerLatRad = (centerLat*M_PI)/180.0;
    stepLat = 111132.92 - 559.82 * cos( 2 * centerLatRad ) + 1.175 * cos( 4 * centerLatRad) - 0.0023 * cos( 6 * centerLatRad);
    stepLon = 111412.84 * cos ( centerLatRad ) - 93.5 * cos ( 3*centerLatRad ) ;
    qDebug() << "Projection "<<centerLat << centerLon;
    qDebug() << "Projection step "<<stepLat << stepLon;
}

LatitudeLongitudeCoordinate* GeoTsreCoordinateConverter::ConvertToLatLon(IghCoordinate* coordinates, LatitudeLongitudeCoordinate* out){
    double lat = centerLat + coordinates->Line / stepLat;
    double lon = centerLon + coordinates->Sample / stepLon;
    if(out == 0)
        return new LatitudeLongitudeCoordinate(lat, lon);
    out->set(lat, lon);
    return out;
}

IghCoordinate* GeoTsreCoordinateConverter::ConvertToInternal(LatitudeLongitudeCoordinate* coordinates, IghCoordinate* out){
    return ConvertToInternal(coordinates->Latitude, coordinates->Longitude, out);
}

IghCoordinate* GeoTsreCoordinateConverter::ConvertToInternal(double lat, double lon, IghCoordinate* out){
    double line = (lat - centerLat) * stepLat;
    double sample = (lon - centerLon) * stepLon;
    if(out == 0)
        return new IghCoordinate(line, sample);
    out->set(line, sample);
    return out;
}

GeoTsreTransverseMercatorCoordinateConverter::GeoTsreTransverseMercatorCoordinateConverter(double *latLonXY)
    : GeoWorldCoordinateConverter(-latLonXY[2], -latLonXY[3], 1)
{
    // GRS80 ellipsoid. TM scale factor is 1.0 and false easting/northing are 0.
    constexpr double semiMajorAxis = 6378137.0;
    constexpr double inverseFlattening = 298.257222101;

    const double flattening = 1.0 / inverseFlattening;
    eccentricitySquared = flattening * (2.0 - flattening);
    eccentricity = sqrt(eccentricitySquared);
    const double thirdFlattening = flattening / (2.0 - flattening);
    centerLongitudeRad = latLonXY[1] * M_PI / 180.0;

    const double n = thirdFlattening;
    const double n2 = n * n;
    const double n3 = n2 * n;
    const double n4 = n2 * n2;
    const double n5 = n4 * n;
    const double n6 = n3 * n3;

    // Krueger series, order 6 (Engsager/Poder formulation).
    rectifyingRadius = semiMajorAxis / (1.0 + n)
        * (1.0 + n2 / 4.0 + n4 / 64.0 + n6 / 256.0);

    alpha[0] = 0.0;
    alpha[1] = 1.0/2.0 * n - 2.0/3.0 * n2 + 5.0/16.0 * n3
             + 41.0/180.0 * n4 - 127.0/288.0 * n5 + 7891.0/37800.0 * n6;
    alpha[2] = 13.0/48.0 * n2 - 3.0/5.0 * n3 + 557.0/1440.0 * n4
             + 281.0/630.0 * n5 - 1983433.0/1935360.0 * n6;
    alpha[3] = 61.0/240.0 * n3 - 103.0/140.0 * n4
             + 15061.0/26880.0 * n5 + 167603.0/181440.0 * n6;
    alpha[4] = 49561.0/161280.0 * n4 - 179.0/168.0 * n5
             + 6601661.0/7257600.0 * n6;
    alpha[5] = 34729.0/80640.0 * n5 - 3418889.0/1995840.0 * n6;
    alpha[6] = 212378941.0/319334400.0 * n6;

    beta[0] = 0.0;
    beta[1] = 1.0/2.0 * n - 2.0/3.0 * n2 + 37.0/96.0 * n3
            - 1.0/360.0 * n4 - 81.0/512.0 * n5 + 96199.0/604800.0 * n6;
    beta[2] = 1.0/48.0 * n2 + 1.0/15.0 * n3 - 437.0/1440.0 * n4
            + 46.0/105.0 * n5 - 1118711.0/3870720.0 * n6;
    beta[3] = 17.0/480.0 * n3 - 37.0/840.0 * n4
            - 209.0/4480.0 * n5 + 5569.0/90720.0 * n6;
    beta[4] = 4397.0/161280.0 * n4 - 11.0/504.0 * n5
            - 830251.0/7257600.0 * n6;
    beta[5] = 4583.0/161280.0 * n5 - 108847.0/3991680.0 * n6;
    beta[6] = 20648693.0/638668800.0 * n6;

    // Raw TM northing is measured from the equator. Subtract the raw
    // northing of the TSRE projection centre so centerLat/centerLon maps to 0,0.
    double unusedEasting = 0.0;
    forwardRaw(latLonXY[0] * M_PI / 180.0, 0.0, unusedEasting, originNorthing);

    qDebug() << "Projection TransverseMercator" << latLonXY[0] << latLonXY[1];
}

double GeoTsreTransverseMercatorCoordinateConverter::tauPrime(double tau) const {
    const double tauHypot = hypot(1.0, tau);
    const double sigma = sinh(eccentricity * atanh(eccentricity * tau / tauHypot));
    return hypot(1.0, sigma) * tau - sigma * tauHypot;
}

double GeoTsreTransverseMercatorCoordinateConverter::inverseTauPrime(double tauPrimeValue) const {
    const double oneMinusEccentricitySquared = 1.0 - eccentricitySquared;
    double tau = tauPrimeValue / oneMinusEccentricitySquared;

    for (int i = 0; i < 10; i++) {
        const double calculatedTauPrime = tauPrime(tau);
        const double deltaTau = (tauPrimeValue - calculatedTauPrime)
            * (1.0 + oneMinusEccentricitySquared * tau * tau)
            / (oneMinusEccentricitySquared * hypot(1.0, tau) * hypot(1.0, calculatedTauPrime));

        tau += deltaTau;
        if (fabs(deltaTau) < 1e-14 * fmax(1.0, fabs(tau)))
            break;
    }

    return tau;
}

void GeoTsreTransverseMercatorCoordinateConverter::forwardRaw(
        double latitudeRad, double longitudeDeltaRad,
        double &easting, double &northing) const {
    const double tau = tan(latitudeRad);
    const double taup = tauPrime(tau);
    const double cosLambda = cos(longitudeDeltaRad);

    const double xiPrime = atan2(taup, cosLambda);
    const double etaPrime = asinh(sin(longitudeDeltaRad) / hypot(taup, cosLambda));

    double xi = xiPrime;
    double eta = etaPrime;

    for (int j = 1; j <= 6; j++) {
        const double angle = 2.0 * j;
        xi += alpha[j] * sin(angle * xiPrime) * cosh(angle * etaPrime);
        eta += alpha[j] * cos(angle * xiPrime) * sinh(angle * etaPrime);
    }

    easting = rectifyingRadius * eta;
    northing = rectifyingRadius * xi;
}

void GeoTsreTransverseMercatorCoordinateConverter::inverseRaw(
        double easting, double northing,
        double &latitudeRad, double &longitudeDeltaRad) const {
    const double eta = easting / rectifyingRadius;
    const double xi = northing / rectifyingRadius;

    double xiPrime = xi;
    double etaPrime = eta;

    for (int j = 1; j <= 6; j++) {
        const double angle = 2.0 * j;
        xiPrime -= beta[j] * sin(angle * xi) * cosh(angle * eta);
        etaPrime -= beta[j] * cos(angle * xi) * sinh(angle * eta);
    }

    const double sinhEtaPrime = sinh(etaPrime);
    const double taup = sin(xiPrime) / hypot(sinhEtaPrime, cos(xiPrime));
    const double tau = inverseTauPrime(taup);

    latitudeRad = atan(tau);
    longitudeDeltaRad = atan2(sinhEtaPrime, cos(xiPrime));
}

LatitudeLongitudeCoordinate* GeoTsreTransverseMercatorCoordinateConverter::ConvertToLatLon(
        IghCoordinate* coordinates, LatitudeLongitudeCoordinate* out){
    double latitudeRad = 0.0;
    double longitudeDeltaRad = 0.0;

    // TSRE internal convention: Line = northing, Sample = easting.
    inverseRaw(coordinates->Sample, coordinates->Line + originNorthing,
               latitudeRad, longitudeDeltaRad);

    double longitudeRad = centerLongitudeRad + longitudeDeltaRad;
    longitudeRad = remainder(longitudeRad, 2.0 * M_PI);

    const double lat = latitudeRad * 180.0 / M_PI;
    const double lon = longitudeRad * 180.0 / M_PI;

    if(out == 0)
        return new LatitudeLongitudeCoordinate(lat, lon);
    out->set(lat, lon);
    return out;
}

IghCoordinate* GeoTsreTransverseMercatorCoordinateConverter::ConvertToInternal(
        LatitudeLongitudeCoordinate* coordinates, IghCoordinate* out){
    return ConvertToInternal(coordinates->Latitude, coordinates->Longitude, out);
}

IghCoordinate* GeoTsreTransverseMercatorCoordinateConverter::ConvertToInternal(
        double lat, double lon, IghCoordinate* out){
    const double latitudeRad = lat * M_PI / 180.0;
    double longitudeDeltaRad = lon * M_PI / 180.0 - centerLongitudeRad;
    longitudeDeltaRad = remainder(longitudeDeltaRad, 2.0 * M_PI);

    double easting = 0.0;
    double northing = 0.0;
    forwardRaw(latitudeRad, longitudeDeltaRad, easting, northing);
    northing -= originNorthing;

    // TSRE internal convention: Line = northing, Sample = easting.
    if(out == 0)
        return new IghCoordinate(northing, easting);
    out->set(northing, easting);
    return out;
}

float LatitudeLongitudeCoordinate::distanceTo(LatitudeLongitudeCoordinate *c){
    double centerLat = (Latitude + c->Latitude)/2;
    double centerLon = (Longitude + c->Longitude)/2;
    
    double centerLatRad = (centerLat*M_PI)/180.0;
    double stepLat = 111132.92 - 559.82 * cos( 2 * centerLatRad ) + 1.175 * cos( 4 * centerLatRad) - 0.0023 * cos( 6 * centerLatRad);
    double stepLon = 111412.84 * cos ( centerLatRad ) - 93.5 * cos ( 3*centerLatRad ) ;
    //qDebug() << "Projection "<<Latitude << c->Latitude << Longitude<<c->Longitude;
    //qDebug() << "Projection "<<centerLat << centerLon;
    //qDebug() << "Projection step "<<stepLat << stepLon;
    
    double deltaLat = fabs(Latitude - c->Latitude);
    double deltaLon = fabs(Longitude - c->Longitude);
    return sqrt(pow(deltaLat * stepLat, 2) + pow(deltaLon * stepLon, 2));
}