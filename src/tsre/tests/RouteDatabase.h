#ifndef TSRE_TESTS_ROUTE_DATABASE_H
#define TSRE_TESTS_ROUTE_DATABASE_H

#include <QDir>
#include <QFileInfo>
#include <tsre/Game.h>
#include <tsre/world/Trk.h>

namespace TsreTestRoute {

// The route's TDB (Game::route under Game::root), by its name without the
// extension: its only one, else the one its TRK names, as the app takes it
// (E30-Podkarpacie keeps a second "&" copy beside its own); empty for none.
inline QString databaseName() {
    const QDir route(Game::root + "/ROUTES/" + Game::route);
    const QFileInfoList databases = route.entryInfoList({"*.tdb"}, QDir::Files);
    if (databases.size() == 1)
        return databases.first().completeBaseName();
    if (databases.size() > 1 && Game::checkRoute(Game::route)) {
        Trk trk;
        trk.load();
        for (const QFileInfo &file : databases)
            if (file.completeBaseName() == trk.routeName)
                return trk.routeName;
    }
    return QString();
}

}

#endif
