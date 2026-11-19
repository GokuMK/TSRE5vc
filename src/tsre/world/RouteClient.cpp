/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/RouteClient.h>
#include <routeEditor/RouteEditorClient.h>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/world/Tile.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/world/TerrainClient.h>
#include "TerrainLibQtClient.h"
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include <tsre/world/objects/TrackObj.h>
#include <tsre/trains/Path.h>
#include <tsre/world/Terrain.h>
#include <tsre/fileFunctions/FileFunctions.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/world/objects/DynTrackObj.h>
#include <tsre/world/objects/PlatformObj.h>
#include <tsre/world/objects/CarSpawnerObj.h>
#include <tsre/math3d/Flex.h>
#include <tsre/world/objects/ForestObj.h>
#include <tsre/coords/Coords.h>
#include <tsre/coords/CoordsMkr.h>
#include <tsre/coords/CoordsKml.h>
#include <tsre/coords/CoordsGpx.h>
#include <tsre/coords/CoordsRoutePlaces.h>
#include <tsre/world/SoundList.h>
#include <tsre/trains/ActLib.h>
#include <tsre/world/Trk.h>
#include <routeEditor/AboutWindow.h>
#include <routeEditor/TrkWindow.h>
#include <tsre/world/objects/PlatformObj.h>
#include <tsre/world/objects/GroupObj.h>
#include <tsre/Undo.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Traffic.h>
#include <tsre/trains/Path.h>
#include <tsre/world/Environment.h>
#include <tsre/world/OrtsWeatherChange.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/trains/Consist.h>
#include <tsre/world/Skydome.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/gui/ActionChooseDialog.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/ErrorMessage.h>
#include <tsre/texture/AceLib.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/renderer/RenderItem.h>
#include <QDateTime>

RouteClient::RouteClient() {
    
}

quint32 RouteClient::refreshFromServer(){
    refreshingFromServer = true;
    missingTiles.clear();
    missingTileNoticeAt.clear();
    RouteEditorClient *server = Game::serverClient;
    // In this order: the track's data before the objects using it.
    quint32 last = server->sendUtf16Message("request_tsection ( )\n");
    last = server->sendUtf16Message("request_tdb ( )\n");
    last = server->sendUtf16Message("request_rdb ( )\n");
    // Loaded tiles, and those still waiting (the request or its answer may
    // have been lost with the link; a tile the server had not got may be
    // there now).
    for (Tile *t : std::as_const(tile))
        if (t != NULL && (t->loaded == 1 || t->loaded == -2))
            last = server->sendUtf16Message("request_tile ( " + QString::number(t->x) + " " + QString::number(t->z) + " )\n");
    TerrainLibQt *terrain = dynamic_cast<TerrainLibQt*>(terrainLib);
    if (terrain != NULL)
        for (Terrain *t : terrain->allTerrains()) {
            TerrainClient *waiting = dynamic_cast<TerrainClient*>(t);
            if (waiting != NULL && waiting->waiting())
                waiting->askAgain();
        }
    if (terrain != NULL)
        for (Terrain *t : terrain->loadedTerrains()) {
            const QString where = QString::number(int(t->mojex)) + " " + QString::number(int(t->mojez));
            server->sendUtf16Message("request_terrain_tfile ( " + where + " )\n");
            server->sendUtf16Message("request_terrain_yfile ( " + where + " )\n");
            last = server->sendUtf16Message("request_terrain_ffile ( " + where + " )\n");
        }
    return last;
}

void RouteClient::load(){
    switch(loadingProgress){
        case 0:
            Game::currentRoute = this;
            trkName = Game::trkName;
            trkFileName = Game::trkFileName;
            routeDir = Game::route;

            qDebug() << "# Load Remote Route";

            // Remote routes use the server's terrain index, never local disk recovery.
            terrainLib = new TerrainLibQtClient();
            Game::terrainLib = terrainLib;

            // The route's data by kind first (task server 03): every loader
            // after reads the server's files (RouteFiles).
            Game::serverClient->sendUtf16Message("request_route_files ( ) \n");
            Game::serverClient->sendUtf16Message("request_trk ( ) \n");
            break;
            
        case 1:

            qDebug() << "Geo Projection:" << GeoProjectionTypeToString(trk->geoProjectionType);
            Game::GeoCoordConverter = GeoWorldCoordinateConverter::Create(
                    trk->geoProjectionType,
                    trk->geoProjection.has_value()
                        ? &*trk->geoProjection : nullptr);
            if (Game::GeoCoordConverter == nullptr) {
                qWarning() << "Remote route loading aborted: invalid geographic projection";
                return;
            }

            env = new Environment(Game::root + "/ROUTES/" + Game::route + "/ENVFILES/editor.env");
            Game::routeName = trk->routeName;
            routeName = Game::routeName;
            qDebug() << Game::routeName;

            this->tsection = new TSectionDAT(false, false);
            Game::serverClient->sendUtf16Message("request_tsection ( ) \n");
            break;
            
        case 2:
            Game::serverClient->sendUtf16Message("request_tdb ( ) \n request_rdb ( ) \n ");
            Game::serverClient->sendUtf16Message("request_terrain_qt ( ) \n ");
            break;

        case 6:
            this->ref = new Ref("");
            Game::serverClient->sendUtf16Message("request_addons ( ) \n ");
            //loadAddons();

            //loadMkrList();
            //createMkrPlaces();
            //loadServices();
            //loadTraffic();
            //loadPaths();
            //loadActivities();

            // As a local load reads them, from the server's files.
            loadRouteLists();

            loaded = true;

            Vec3::set(placementAutoTranslationOffset, 0, 0, 0);
            Vec3::set(placementAutoRotationOffset, 0, 0, 0);

            skydome = new Skydome();

            emit initDone();
            break;
        default:
            break;
    }
}

RouteClient::RouteClient(const RouteClient& orig) {
}

RouteClient::~RouteClient() {
}

Tile * RouteClient::requestTile(int x, int z, bool allowNew){
    const int key = x*10000 + z;
    Tile *tTile = tile.value(key, NULL);
    if (tTile != NULL && tTile->loaded == 1)
        return tTile;
    if (missingTiles.contains(key)) {
        if (allowNew) {
            // Placing on it: refused, and said (at most every 5 s a tile).
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - missingTileNoticeAt.value(key, 0) > 5000) {
                missingTileNoticeAt[key] = now;
                Game::serverClient->notice(QString("World tile %1 %2 is not on the server; a client cannot make tiles yet.")
                                           .arg(x).arg(-z));
            }
        }
        return NULL;
    }
    if (tTile == NULL) {
        // Asked for once; until its data comes, a tile that is not loaded.
        Game::serverClient->sendUtf16Message("request_tile ( "+QString::number(x)+" "+QString::number(z)+" )");
        tTile = new Tile();
        tTile->x = x;
        tTile->z = z;
        tile[key] = tTile;
    }
    return NULL;
}

void RouteClient::markTileMissing(int x, int z){
    missingTiles.insert(x*10000 + z);
}

bool RouteClient::tileMissing(int x, int z) const{
    return missingTiles.contains(x*10000 + z);
}

void RouteClient::save(){
    
}

void RouteClient::getUnsavedInfo(QVector<QString> &items){
    
}
