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
#include <tsre/world/TerrainInfo.h>
#include <tsre/world/TerrainLib.h>

TerrainClient::TerrainClient(){
    
}

TerrainClient::TerrainClient(TerrainInfo *ti){
    mojex = ti->cx;
    mojez = -ti->cy;
    name = ti->name;
    lowTile = ti->low;
    load();
}

TerrainClient::TerrainClient(float x, float y){
    mojex = x;
    mojez = y;
    name = getTileName((int) x, (int) -y);
    load();
}

TerrainClient::TerrainClient(const TerrainClient& orig) {
    
}

// The server's answers while the tile loads (a T file, heights, flags).
bool TerrainClient::loadTFile(FileBuffer *data){
    TFile *read = new TFile();
    if (!read->load(data)) {
        delete read;
        return false;
    }
    // A descriptor replaced before the tile was ever drawn is freed (B8).
    if (!loaded)
        delete this->tfile;
    this->tfile = read;
    if (!validateGridLayout("network terrain descriptor " + name))
        return false;
    loadingProgress++;
    load();
    return true;
}

bool TerrainClient::loadRAWFile(FileBuffer *data){
    if (!validatePayload(data, sizeof(float), "network float height RAW"))
        return false;
    loadingProgress++;
    this->readRAWFloat(data);
    load();
    return true;
}

void TerrainClient::updateTFile(){
    Game::serverClient->updateTerrainTFile(this);
}

bool TerrainClient::loadFFile(FileBuffer *data){
    loadingProgress++;
    // No flags: the server's tile has none.
    if(data->off == data->length){
        load();
        return true;
    }
    if (!validatePayload(data, sizeof(unsigned char), "network terrain F RAW"))
        return false;
    jestF = true;
    this->readF(data);
    load();
    return true;
}

void TerrainClient::updateFFile(){
    Game::serverClient->updateTerrainFFile(this);
}

void TerrainClient::load(){
    QString path;
    
    switch(loadingProgress){
        case 0:
            typeObj = this->terrainobj;
            loaded = false;
            isOgl = false;
            modified = false;
            editable = false;
            wTexid = -1;
            for (int i = 0; i < TerrainGridLayout::SupportedPatchRecordCount; i++) {
                texid[i] = -1;
                texid2[i] = -1;
                hidden[i] = false;
                texModified[i] = false;
                texLocked[i] = false;
                uniqueTex[i] = false;
                selectedPatchs[i] = false;
            }

            configureTerrainSeason();
            path = Game::root + "/ROUTES/" + Game::route + "/" + TileDir[(int)lowTile] + "/";

            Game::serverClient->sendUtf16Message("request_terrain_tfile( "+QString::number(mojex)+" "+QString::number(mojez)+" )");
            break;
            //tfile = new TFile();

            //QString filename = getTileName((int) x, (int) -y);
            //QString filename = getTileNameExperimental2((int) x, (int) -y);
            //qDebug() << filename << x << -y;
            //if (!tfile->readT((path + name + ".t"))) {
            //    //qDebug() << " t fail" << name;
            //    return;
            //}
        case 1:
            if(tfile->samples.y.has_value() == false)
                return;
            Game::serverClient->sendUtf16Message("request_terrain_yfile( "+QString::number(mojex)+" "+QString::number(mojez)+" )");
            if(tfile->samples.f.has_value()){
                Game::serverClient->sendUtf16Message("request_terrain_ffile( "+QString::number(mojex)+" "+QString::number(mojez)+" )");
            } else {
                loadingProgress++;
            }break;
        case 2:
            break;
        case 3:
            //qDebug() << " ok";

            //QString name = this->getTileName(mojex, -mojez);
            QString name2;
            //int samples = 
            int patches = tfile->patchCount();
            for (int u = 0; u < patches; u++){
                for (int y = 0; y < patches; y++) {
                    name2 = name + "_" + QString::number(y) + "_" + QString::number(u) + ".ace";
                    //qDebug() << name2 << (int) tfile->patches()[(y * 16 + u)].shaderIndex;
                    //qDebug() << tfile->materialCount();
                    //qDebug() << tfile->material((int) tfile->patches()[(y * 16 + u)].shaderIndex).textures[0].filename;
                    //qDebug() << name << patches << tfile->materialCount() << (int) tfile->patches()[(y * patches + u)].shaderIndex;
                    if (tfile->materialCount() <= (int) tfile->patches()[(y * patches + u)].shaderIndex)
                        continue;

                    if (name2 == tfile->textureName(tfile->patches()[y*patches+u].shaderIndex))
                        this->uniqueTex[y*patches+u] = true;
                }
            }

            loaded = true;
            if (Game::terrainLib != nullptr)
                Game::terrainLib->terrainAvailabilityChanged(this);
            break;
    }
}

TerrainClient::~TerrainClient() {
}
