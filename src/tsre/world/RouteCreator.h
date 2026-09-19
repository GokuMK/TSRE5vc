/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine or Editors.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef ROUTECREATOR_H
#define ROUTECREATOR_H

#include <QString>
#include <memory>

class Trk;

class RouteCreator {
public:
    static bool templateResourcesAvailable(QString *error = nullptr);
    static bool create(const QString &routeDirectoryName,
                       std::unique_ptr<Trk> routeTemplate,
                       QString *error = nullptr);

private:
    static QString templateDirectory();
};

#endif /* ROUTECREATOR_H */
