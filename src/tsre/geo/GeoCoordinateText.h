/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef GEOCOORDINATETEXT_H
#define GEOCOORDINATETEXT_H

#include <QString>

namespace GeoCoordinateText {

bool parseLatitudeLongitude(const QString &text,
                            double &latitude, double &longitude);

} // namespace GeoCoordinateText

#endif /* GEOCOORDINATETEXT_H */
