/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "TerrainLibQtClient.h"
#include <tsre/world/TerrainInfo.h>
#include <tsre/world/TerrainClient.h>
#include <tsre/world/QuadTree.h>
#include <routeEditor/RouteEditorClient.h>

TerrainLibQtClient::TerrainLibQtClient() {
}

TerrainLibQtClient::TerrainLibQtClient(const TerrainLibQtClient& orig) {
}

TerrainLibQtClient::~TerrainLibQtClient() {
}

void TerrainLibQtClient::updateTerrainHeightmap(Terrain *t){
    Game::serverClient->updateTerrainHeightmap(t);
}

void TerrainLibQtClient::updateTerrainTFile(Terrain *t){
    Game::serverClient->updateTerrainTFile(t);
}

namespace {
const char *ClientTilesRefused = "A client cannot make or delete terrain tiles yet; the server has them.";
}

bool TerrainLibQtClient::createQuadTile(const QuadTree::Quad &quad, bool overwrite, QString &error){
    Q_UNUSED(quad);
    Q_UNUSED(overwrite);
    error = ClientTilesRefused;
    return false;
}

bool TerrainLibQtClient::deleteQuadTile(const QuadTree::Quad &quad, QString &error){
    Q_UNUSED(quad);
    error = ClientTilesRefused;
    return false;
}

void TerrainLibQtClient::saveEmpty(int x, int z){
    Q_UNUSED(x);
    Q_UNUSED(z);
    Game::serverClient->notice(ClientTilesRefused);
}

bool TerrainLibQtClient::saveEmpty(int x, int z, TerrainHeightProfile profile, int patches, bool overwrite){
    Q_UNUSED(x);
    Q_UNUSED(z);
    Q_UNUSED(profile);
    Q_UNUSED(patches);
    Q_UNUSED(overwrite);
    Game::serverClient->notice(ClientTilesRefused);
    return false;
}

bool TerrainLibQtClient::createNewRouteTerrain(int x, int z){
    Q_UNUSED(x);
    Q_UNUSED(z);
    return false;
}

void TerrainLibQtClient::save(){
}
Terrain *TerrainLibQtClient::makeTerrain(TerrainInfo *info, bool complete) {
    return new TerrainClient(info, complete);
}