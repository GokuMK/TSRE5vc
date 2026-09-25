/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine or Editors.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef ROUTECREATOR_H
#define ROUTECREATOR_H

#include <QString>
#include <QStringList>
#include <memory>

class Trk;

struct RouteCreationOptions {
    QString countryCode;
    bool generateCountryPlaces = false;
    double startLatitude = 0.0;
    double startLongitude = 0.0;
};

class RouteCreator {
public:
    static bool templateResourcesAvailable(QString *error = nullptr);
    static bool create(const QString &routeDirectoryName,
                       std::unique_ptr<Trk> routeTemplate,
                       QString *error = nullptr,
                       const RouteCreationOptions &options = {},
                       QStringList *warnings = nullptr);

private:
    static QString templateDirectory();
};

#endif /* ROUTECREATOR_H */
