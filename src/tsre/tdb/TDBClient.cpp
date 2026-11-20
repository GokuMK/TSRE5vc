/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tdb/TDBClient.h>
#include <routeEditor/RouteEditorClient.h>
#include <tsre/fileFunctions/NetworkMessage.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TSectionDAT.h>

TDBClient::TDBClient(TSectionDAT* tsection, bool road) : TDB(tsection, road){

}

TDBClient::TDBClient(const TDBClient& o) : TDB(o){

}

void TDBClient::updateTrNode(int nid){
    // Recorded as any change (task tracks 16), then sent.
    TDB::updateTrNode(nid);
    if(nid < 0)
        return;
    Game::serverClient->updateTrackNodeData(nid, this->tdbId, this->trackNodes[nid]);
}

void TDBClient::updateTrItem(int iid){
    TDB::updateTrItem(iid);
    if(iid < 0)
        return;
    Game::serverClient->updateTrackItemData(iid, this->tdbId, this->trackItems[iid]);
}

void TDBClient::updateTrackSection(int id){
    auto found = this->tsection->sekcja.find(id);
    if(found != this->tsection->sekcja.end())
        Game::serverClient->updateTrackSectionData(found->second);
}

void TDBClient::updateTrackShape(int id){
    if(id < 0)
        return;
    if(this->tsection == NULL)
        return;
    if(this->tsection->shape[id] == NULL)
        return;
    Game::serverClient->updateTrackShapeData(this->tsection->shape[id]);
}
// New IDs from this client's blocks (task server 02): two users never take
// the same one. The server hands out free item slots (emptyitems) first.
// Without a block (the server not answering), the next local one: the
// server refuses it and puts it right.
int TDBClient::getNextItrNode(){
    int id = Game::serverClient->takeId(road ? NetworkMessage::IdKind::RdbNode : NetworkMessage::IdKind::TdbNode);
    if(id < 0)
        id = this->iTRnodes + 1;
    if(id > this->iTRnodes)
        this->iTRnodes = id;
    return id;
}

int TDBClient::getNewTRitemId(){
    int id = Game::serverClient->takeId(road ? NetworkMessage::IdKind::RdbItem : NetworkMessage::IdKind::TdbItem);
    if(id < 0)
        id = this->iTRitems;
    if(id >= this->iTRitems)
        this->iTRitems = id + 1;
    return id;
}

TDBClient::~TDBClient() {
    //TDB::~TDB();
}

