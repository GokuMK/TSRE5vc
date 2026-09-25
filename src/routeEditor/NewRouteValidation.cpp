/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <routeEditor/NewRouteValidation.h>

#include <QDir>
#include <QRegularExpression>
#include <tsre/geo/GeoCoordinateText.h>

NewRouteValidation::NameError NewRouteValidation::validateName(
        const QString &name, const QString &simulatorRoot) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return NameError::Empty;
    if (trimmed.size() < 2) return NameError::TooShort;
    if (trimmed.size() > 64) return NameError::TooLong;

    static const QRegularExpression allowed(
            QStringLiteral("^[A-Za-z0-9_-](?:[A-Za-z0-9 _-]*[A-Za-z0-9_-])?$"));
    if (!allowed.match(trimmed).hasMatch()) return NameError::InvalidCharacter;

    static const QRegularExpression reserved(
            QStringLiteral("^(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$"),
            QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(trimmed).hasMatch()) return NameError::Reserved;

    if (!simulatorRoot.isEmpty()) {
        const QDir routes(QDir(simulatorRoot).filePath(QStringLiteral("ROUTES")));
        const QStringList directories = routes.entryList(
                QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &directory : directories) {
            if (directory.compare(trimmed, Qt::CaseInsensitive) == 0)
                return NameError::AlreadyExists;
        }
    }
    return NameError::None;
}

bool NewRouteValidation::coordinatePair(const QString &text,
                                        double &latitude, double &longitude) {
    return GeoCoordinateText::parseLatitudeLongitude(
            text, latitude, longitude);
}
