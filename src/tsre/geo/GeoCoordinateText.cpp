/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/geo/GeoCoordinateText.h>

#include <QRegularExpression>
#include <cmath>

bool GeoCoordinateText::parseLatitudeLongitude(
        const QString &text, double &latitude, double &longitude) {
    static const QRegularExpression expression(QStringLiteral(
        "^\\s*([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+))\\s*[,;]\\s*"
        "([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+))\\s*$"));
    const QRegularExpressionMatch match = expression.match(text);
    if (!match.hasMatch()) return false;
    bool latitudeOk = false;
    bool longitudeOk = false;
    latitude = match.captured(1).toDouble(&latitudeOk);
    longitude = match.captured(2).toDouble(&longitudeOk);
    return latitudeOk && longitudeOk && std::isfinite(latitude)
            && std::isfinite(longitude) && latitude >= -90.0
            && latitude <= 90.0 && longitude >= -180.0
            && longitude <= 180.0;
}
