/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tests/RouteLoadTestSuite.h>

#include <QDebug>
#include <QTextStream>

#include <algorithm>
#include <iostream>
#include <vector>

#include <tsre/Game.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/trains/EngLib.h>
#include <tsre/world/Route.h>
#include <tsre/world/Tile.h>
#include <tsre/world/objects/TrWatermarkObj.h>
#include <tsre/world/objects/WorldObj.h>

namespace {

static QString routeNameForTest() {
    QString routeName = Game::route.trimmed();
    if (routeName.isEmpty())
        routeName = "test_group_z_1";
    return routeName;
}

static bool loadRouteHeadless(Route *&route, QString &error) {
    const QString routeName = routeNameForTest();

    if (!Game::checkRoot(Game::root)) {
        error = QString("invalid MSTS root: %1").arg(Game::root);
        return false;
    }
    if (!Game::checkRoute(routeName)) {
        error = QString("route not found: %1").arg(routeName);
        return false;
    }

    Game::route = routeName;
    Game::gui = false;
    Game::consoleOutput = true;
    Game::loadAllWFiles = true;

    Game::currentShapeLib = new ShapeLib();
    Game::currentEngLib = new EngLib();

    route = new Route();
    route->load();
    if (!route->loaded) {
        error = QString("route failed to load: %1").arg(routeName);
        return false;
    }

    return true;
}

static Tile *currentTileFromPreload(Route *route, QString &error) {
    if (route == NULL) {
        error = "route pointer is null";
        return NULL;
    }

    int tileX = route->getStartTileX();
    int tileZ = -route->getStartTileZ();
    if (Game::start == 2) {
        tileX = Game::startTileX;
        tileZ = -Game::startTileY;
    }

    Tile *tile = route->requestTile(tileX, tileZ, false);
    if (tile == NULL) {
        error = QString("current tile %1 %2 was not preloaded").arg(tileX).arg(tileZ);
        return NULL;
    }
    if (tile->loaded != 1) {
        error = QString("current tile %1 %2 is not fully loaded").arg(tileX).arg(tileZ);
        return NULL;
    }

    return tile;
}

static QString serializeWorldObject(WorldObj *obj) {
    QString text;
    QTextStream out(&text);
    obj->save(&out);
    out.flush();
    return text;
}

} // namespace

int TsreTests::runRouteLoadSuite(const TestRunOptions &opts) {
    Q_UNUSED(opts);

    Route *route = NULL;
    QString error;
    if (!loadRouteHeadless(route, error)) {
        qWarning() << "[tests:route-load]" << error;
        return 1;
    }

    Tile *tile = currentTileFromPreload(route, error);
    if (tile == NULL) {
        qWarning() << "[tests:route-load]" << error;
        return 1;
    }

    std::vector<WorldObj*> objects;
    objects.reserve(tile->obiekty.size());
    for (const auto &entry : tile->obiekty) {
        WorldObj *obj = entry.second;
        if (obj == NULL)
            continue;
        if (!obj->loaded)
            continue;
        if (dynamic_cast<TrWatermarkObj*>(obj) != NULL)
            continue;
        objects.push_back(obj);
    }

    std::sort(objects.begin(), objects.end(), [](WorldObj *a, WorldObj *b) {
        if (a->UiD != b->UiD)
            return a->UiD < b->UiD;
        if (a->typeID != b->typeID)
            return a->typeID < b->typeID;
        return a->fileName < b->fileName;
    });

    int tileX = route->getStartTileX();
    int tileZ = -route->getStartTileZ();
    if (Game::start == 2) {
        tileX = Game::startTileX;
        tileZ = -Game::startTileY;
    }

    std::cout << "[tests:route-load] route=" << Game::route.toStdString()
              << " tile=" << tileX << " " << tileZ
              << " objects=" << objects.size() << "\n";
    for (WorldObj *obj : objects) {
        std::cout << serializeWorldObject(obj).toStdString();
    }

    if (objects.empty()) {
        qWarning() << "[tests:route-load] no loaded world objects found on current tile";
        return 1;
    }

    return 0;
}
