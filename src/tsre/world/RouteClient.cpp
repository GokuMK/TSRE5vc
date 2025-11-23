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
#include <tsre/world/TerrainLibSimple.h>
#include "TerrainLibQtClient.h"
#include <tsre/Game.h>
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

RouteClient::RouteClient() {
    
}

void RouteClient::load(){
    switch(loadingProgress){
        case 0:
            Game::currentRoute = this;
            trkName = Game::trkName;
            routeDir = Game::route;

            qDebug() << "# Load Remote Route";

            if(!Game::useQuadTree)
                terrainLib = new TerrainLibSimple();
            else
                terrainLib = new TerrainLibQtClient();
            Game::terrainLib = terrainLib;

            Game::serverClient->sendUtf16Message("request_trk ( ) \n");
            break;
            
        case 1:
            Game::useSuperelevation = trk->tsreSuperelevation;

            if(trk->tsreProjection != NULL){
                qDebug() << "TSRE Geo Projection";
                Game::GeoCoordConverter = new GeoTsreCoordinateConverter(trk->tsreProjection);
            } else {
                qDebug() << "MSTS Geo Projection";
                Game::GeoCoordConverter = new GeoMstsCoordinateConverter();
            }
            env = new Environment(Game::root + "/routes/" + Game::route + "/ENVFILES/editor.env");
            Game::routeName = trk->routeName.toLower();
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

            soundList = new SoundList();
            soundList->loadSoundSources(Game::root + "/routes/" + Game::route + "/ssource.dat");
            soundList->loadSoundRegions(Game::root + "/routes/" + Game::route + "/ttype.dat");
            Game::soundList = soundList;/**/

            //Game::terrainLib->loadQuadTree();
            //OrtsWeatherChange::LoadList();
            ForestObj::LoadForestList();
            ForestObj::ForestClearDistance = trk->forestClearDistance;
            CarSpawnerObj::LoadCarSpawnerList();

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
    Tile *tTile;
    tTile = tile[((x)*10000 + z)];
    if (tTile != NULL)
        return tTile;

    Game::serverClient->sendUtf16Message("request_tile ( "+QString::number(x)+" "+QString::number(z)+" )");
    tile[((x)*10000 + z)] = new Tile();
    //tile[(x)*10000 + z] = new Tile(x, z);
    /*tTile = tile[((x)*10000 + z)];
    if (tTile->loaded == -2) {
        if (Game::terrainLib->isLoaded(x, z)) {
            tTile->initNew();
        } else {
            return NULL;
        }
    }*/
    return NULL;
}

void RouteClient::save(){
    
}

void RouteClient::getUnsavedInfo(QVector<QString> &items){
    
}

