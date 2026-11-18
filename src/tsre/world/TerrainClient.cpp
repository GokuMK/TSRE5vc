/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */


#include <tsre/world/TerrainClient.h>
#include <routeEditor/RouteEditorClient.h>
#include <tsre/fileFunctions/RouteFiles.h>
#include <tsre/world/TerrainInfo.h>
#include <tsre/world/TerrainLib.h>
#include <QDebug>

TerrainClient::TerrainClient(TerrainInfo *ti, bool complete){
    mojex = ti->cx;
    mojez = -ti->cy;
    name = ti->name;
    lowTile = ti->low;
    typeObj = this->terrainobj;
    loaded = false;
    descriptorLoaded = false;
    ask(complete);
}

void TerrainClient::ask(bool complete){
    if (Game::serverClient == NULL)
        return;
    // A complete answer brings the descriptor too.
    if (asked == Asked::Complete || (asked == Asked::Descriptor && !complete))
        return;
    asked = complete ? Asked::Complete : Asked::Descriptor;
    Game::serverClient->sendUtf16Message(QString("request_terrain_files ( %1 %2 %3 %4 )\n")
            .arg(int(mojex)).arg(int(mojez)).arg(lowTile ? 1 : 0).arg(complete ? 0 : 1));
}

bool TerrainClient::completeLoad(){
    if (loaded)
        return true;
    if (!descriptorLoaded || failed)
        return false;
    ask(true);
    return false;
}

void TerrainClient::filesArrived(const QHash<QString, QByteArray> &files, bool descriptorOnly){
    if (descriptorOnly) {
        if (asked == Asked::Descriptor)
            asked = Asked::None;
    } else {
        asked = Asked::None;
    }
    if (files.isEmpty()) {
        failed = true;
        qWarning().noquote() << "Terrain tile" << name << "is not on the server";
        return;
    }
    if (loaded)
        return;
    // The local loader, on the server's files.
    for (auto it = files.constBegin(); it != files.constEnd(); ++it)
        RouteFiles::putServed(it.key(), it.value());
    if (!descriptorLoaded)
        loadDescriptor();
    if (descriptorLoaded && !descriptorOnly)
        Terrain::completeLoad();
    for (auto it = files.constBegin(); it != files.constEnd(); ++it)
        RouteFiles::dropServed(it.key());
    if (!descriptorLoaded || (!descriptorOnly && !loaded)) {
        failed = true;
        qWarning().noquote() << "Terrain tile" << name << "from the server did not load";
        return;
    }
    if (Game::terrainLib != nullptr)
        Game::terrainLib->terrainAvailabilityChanged(this);
}

void TerrainClient::updateTFile(){
    Game::serverClient->updateTerrainTFile(this);
}

void TerrainClient::updateFFile(){
    Game::serverClient->updateTerrainFFile(this);
}

TerrainClient::~TerrainClient() {
}
