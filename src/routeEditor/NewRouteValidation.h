/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef NEWROUTEVALIDATION_H
#define NEWROUTEVALIDATION_H

#include <QString>

namespace NewRouteValidation {

enum class NameError {
    None,
    Empty,
    TooShort,
    TooLong,
    InvalidCharacter,
    Reserved,
    AlreadyExists
};

NameError validateName(const QString &name, const QString &simulatorRoot);
bool coordinatePair(const QString &text, double &latitude, double &longitude);

} // namespace NewRouteValidation

#endif /* NEWROUTEVALIDATION_H */
