/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tdb/Ruch.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/Game.h>
#include <tsre/tdb/TRitem.h>

Ruch::Ruch() {

}

void Ruch::set(int nid, int m, int tdirection, QMap<int, int>* jDirections) {
    nodeIdx = nid;
    nodeDist = m;
    kierunek = tdirection;
    direction = -(kierunek - 0.5)*2;
    junctionDirections = jDirections;
    if(junctionDirections == NULL)
        junctionDirections = new QMap<int, int>();

    TDB *tdb = Game::trackDB;
    if (tdb->trackNodes[nodeIdx]->typ == 0 || tdb->trackNodes[nodeIdx]->typ == 2) {
        kierunek = tdb->trackNodes[nodeIdx]->pins[0].direction;
        nodeIdx = tdb->trackNodes[nodeIdx]->pins[0].link;
        nodeLength = Game::trackDB->getVectorSectionLength(nodeIdx);
        if (kierunek == 1) {
            nodeDist = 0;
        } else {
            nodeDist = nodeLength;
        }
        direction = -(kierunek - 0.5)*2;
    } else {
        nodeLength = Game::trackDB->getVectorSectionLength(nodeIdx);
    }
}

void Ruch::next(float m) {
    lastNodeDist = nodeDist;
    if(m < 0){
        back(m);
    } else {
        distanceDownPath += m;
        while(m > 0){
            if(m > 0.5){
                toNext(0.5);
                m -= 0.5;
            } else {
                toNext(m);
                m = 0;
            }
        }
    }
    //nodeDist += m*direction;
    //checkNode();
    if(trackItems)
        checkPassingItems();
}

void Ruch::back(float m) {
    lastNodeDist = nodeDist;
    if(m > 0){
        next(m);
    } else {
        m = -m;
        distanceDownPath += m;
        while(m > 0){
            if(m > 0.5){
                toNext(-0.5);
                m -= 0.5;
            } else {
                toNext(-m);
                m = 0;
            }
        }
    }
    
    if(trackItems)
        checkPassingItems();
}

void Ruch::toNext(float m){
    nodeDist += m*direction;
    int sign = 1;
    if(m < 0)
        sign = -1;
    checkNode(sign);
}

void Ruch::checkNode(int mSign) {
    TDB *tdb = Game::trackDB;
    int kier, nodeId;
    //qDebug() << "modeDist" << nodeDist << nodeLength;
    float nodeDistLeft = 0;
    if (nodeDist >= nodeLength) {
        nodeDistLeft = nodeDist - nodeLength;
        kier = tdb->trackNodes[nodeIdx]->pins[1].direction;
        nodeId = tdb->trackNodes[nodeIdx]->pins[1].link;
    } else if (nodeDist < 0) {
        nodeDistLeft = -nodeDist;
        kier = tdb->trackNodes[nodeIdx]->pins[0].direction;
        nodeId = tdb->trackNodes[nodeIdx]->pins[0].link;
    } else {
        return;
    }

    TRnode *n = tdb->trackNodes[nodeId];
    if (n == NULL)
        return;
    if (n->typ == 2) {
        int u = 0;// n->inputPinCount-1;
        onJunction = 2; // just info
        if (kier == 1){
            onJunction = 1; // just info
            u = 1+(*junctionDirections)[nodeId];//n->inputPinCount;//+n->outputPinCount-1;
        }
        
        // todo if u > 1 allow junction switch
        //if(u > 1)
        //    u++;
        //u = n->inputPinCount*kierunek;
        kierunek = n->pins[u].direction;
        nodeIdx = n->pins[u].link;
        nodeLength = Game::trackDB->getVectorSectionLength(nodeIdx);

    } else if (n->typ == 0) {
        endsPassed++;
        kierunek = n->pins[0].direction;
        nodeIdx = n->pins[0].link;
        nodeLength = Game::trackDB->getVectorSectionLength(nodeIdx);

    } else if (n->typ == 1) {
        nodeIdx = nodeId;
        kierunek = kier;
        nodeLength = Game::trackDB->getVectorSectionLength(nodeIdx);
    }
    
    if (kierunek == 1) {
        nodeDist = nodeDistLeft;
    } else {
        nodeDist = nodeLength-nodeDistLeft;
    }
    lastNodeDist = nodeDist;
    direction = (kierunek - 0.5)*2*mSign;
}

float * Ruch::getCurrentPosition(float *sElev) {
    Game::trackDB->getDrawPositionOnTrNode(drawPosition, nodeIdx, nodeDist, sElev);
    if(kierunek == 0 && sElev != NULL)
        *sElev = -*sElev;
    return drawPosition;
}

int Ruch::getVectorDirection(){
    return kierunek;
}

float Ruch::getDistanceDownPath(){
    return distanceDownPath;
}

void Ruch::trackPassingItems(bool val){
    trackItems = val;
}

QString Ruch::getLastItemName(){
    return lastItemName;
}

float Ruch::curveRadius() const {
    TDB *tdb = Game::trackDB;
    if (tdb == NULL || tdb->tsection == NULL)
        return 0;
    auto node = tdb->trackNodes.find(nodeIdx);
    if (node == tdb->trackNodes.end() || node->second == NULL)
        return 0;
    TRnode *n = node->second;
    float along = 0;
    for (int i = 0; i < n->iTrv; i++) {
        auto section = tdb->tsection->sekcja.find((int)n->trVectorSection[i].sectionIndex);
        if (section == tdb->tsection->sekcja.end() || section->second == NULL)
            continue;
        const float length = section->second->getDlugosc();
        if (nodeDist <= along + length || i == n->iTrv - 1)
            return section->second->type == 1 ? fabs(section->second->radius) : 0;
        along += length;
    }
    return 0;
}

float Ruch::distanceToItem(bool ahead,float maxDistance, const std::function<bool(int)> &wanted, int *itemId) const {
    TDB *tdb = Game::trackDB;
    if (tdb == NULL)
        return -1;
    // A copy walks node by node as the traveller would move.
    Ruch walker = *this;
    walker.trackItems = false;
    const float sign = ahead ? 1.0f : -1.0f;
    float travelled = 0;
    for (int nodes = 0; nodes < 10000 && travelled <= maxDistance; nodes++) {
        auto node = tdb->trackNodes.find(walker.nodeIdx);
        if (node == tdb->trackNodes.end() || node->second == NULL || node->second->typ != 1)
            return -1;
        TRnode *n = node->second;
        // Positive: the walk goes towards the node's end.
        const float dir = walker.direction * sign;
        float nearest = -1;
        int nearestId = -1;
        for (int i = 0; i < n->iTri; i++) {
            const int id = n->trItemRef[i];
            auto item = tdb->trackItems.find(id);
            if (item == tdb->trackItems.end() || item->second == NULL || !wanted(id))
                continue;
            const float along = (item->second->getTrackPosition() - walker.nodeDist) * dir;
            if (along >= 0 && (nearest < 0 || along < nearest)) {
                nearest = along;
                nearestId = id;
            }
        }
        if (nearest >= 0) {
            if (travelled + nearest > maxDistance)
                return -1;
            if (itemId != NULL)
                *itemId = nearestId;
            return travelled + nearest;
        }
        // On to the next node; the end of the track ends the search.
        auto beyond = tdb->trackNodes.find(n->pins[dir > 0 ? 1 : 0].link);
        if (beyond == tdb->trackNodes.end() || beyond->second == NULL || beyond->second->typ == 0)
            return -1;
        const float left = dir > 0 ? walker.nodeLength - walker.nodeDist : walker.nodeDist;
        walker.toNext(sign * (left + 0.01f));
        travelled += left + 0.01f;
    }
    return -1;
}

void Ruch::checkPassingItems(){
    if(lastNodeDist == nodeDist)
        return;
    TDB *tdb = Game::trackDB;
    TRnode *n = tdb->trackNodes[nodeIdx];
    for(int i = 0; i < n->iTri; i++){
        int itemId = n->trItemRef[i];
        if(tdb->trackItems[itemId] == NULL)
            return;
        float dist = tdb->trackItems[itemId]->getTrackPosition();
        if((dist > lastNodeDist && dist < nodeDist) || (dist < lastNodeDist && dist > nodeDist)){
            lastItemId = itemId;
            lastItemName = tdb->trackItems[itemId]->getTrackItemName();
            qDebug() << "!!!!!!!!!!!!!!!!! item Id" << itemId << lastItemName;
        }
    }
    
}
/*
bool Ruch::next(){
        if(aktt==-1) return true;
        
        switch(trackDB->trackNodes[aktt]->typ){
            case 0:
                //System.out.print("byles na endpoint "+aktt);
                
                pozW.x = trackDB->trackNodes[aktt]->uid.x;
                pozW.y = trackDB->trackNodes[aktt]->uid.y;
                pozW.z = trackDB->trackNodes[aktt]->uid.z;
                pozT.x = (int) trackDB->trackNodes[aktt]->uid.tileX;
                pozT.z = (int) trackDB->trackNodes[aktt]->uid.tileZ;
                
                kierunek = trackDB->trackNodes[aktt]->pins[0].direction;
                aktt = trackDB->trackNodes[aktt]->pins[0].link;
                //System.out.println(" przejdziesz do "+aktt);
                
                akticz = 0;
                
                if(kierunek==1) {
                    idx = (int) trackDB->trackNodes[aktt]->trVectorSection[0].sectionIndex;
                    metry = 0;
                } else {
                    idx = (int) trackDB->trackNodes[aktt]->trVectorSection[trackDB->trackNodes[aktt]->iTrv-1].sectionIndex;
                    metry = trackDB->tsection->sekcja.at(idx)->getDlugosc();
                }

                return false;
            case 1:
                u = trackDB->trackNodes[aktt]->iTrv;
                if(kierunek==1){
                    metry+=metrpp;
                    metrpp = 0;
                    if(metry>=trackDB->tsection->sekcja.at(idx)->getDlugosc()){
                         metrpp = metry - trackDB->tsection->sekcja.at(idx)->getDlugosc();
                         akticz++;
                         metry = 0;
                    }
                    i = akticz;
                    
                    if(i==0){
                        //System.out.println("Wjechales na sciezke "+aktt+" z: "+ u +" czesciami. Jedziesz od poczatku do konca");
                    }
                    if(i<u){
                        //System.out.println("czesc" + i);
                        idx = (int) trackDB->trackNodes[aktt]->trVectorSection[i].sectionIndex;
                        pozW.x = trackDB->trackNodes[aktt]->trVectorSection[i].x;
                        pozW.y = trackDB->trackNodes[aktt]->trVectorSection[i].y;
                        pozW.z = trackDB->trackNodes[aktt]->trVectorSection[i].z;
                        pozO.x = trackDB->trackNodes[aktt]->trVectorSection[i].ax;
                        pozO.y = trackDB->trackNodes[aktt]->trVectorSection[i].ay;
                        pozO.z = trackDB->trackNodes[aktt]->trVectorSection[i].az;
                        pozT.x = trackDB->trackNodes[aktt]->trVectorSection[i].tileX;
                        pozT.z = trackDB->trackNodes[aktt]->trVectorSection[i].tileZ;
                        return true;
                    }
                    
                    //rozjazdy[trackDB.trackNodes[aktt].pins[1].link][1] = Math.abs(kierunek-1);
                    //rozjazdy[trackDB.trackNodes[aktt].pins[1].link][0] = aktt;
                            
                    kierunek = trackDB->trackNodes[aktt]->pins[1].direction;
                    aktt = trackDB->trackNodes[aktt]->pins[1].link;
                    //System.out.println("zjezdzasz z toru na " + aktt);
                    return false;
                }
                if(kierunek==0){
                    metry-=metrpp;
                    metrpp = 0;
                         
                    if(metry<=0){
                         metrpp = -metry;
                         akticz++;
                         //System.out.println("++ ");
                         i=u-akticz-1;
                         if(i>=0){
                            idx = trackDB->trackNodes[aktt]->trVectorSection[i].sectionIndex;
                            metry=trackDB->tsection->sekcja.at(idx)->getDlugosc();
                         }
                    }
                    i=u-akticz-1;
                    
                    if(i==u-1){
                        //System.out.println("Wjechales na sciezke "+aktt+" z: "+ u +" czesciami. Jedziesz od konca do poczatku");
                    }
                    
                    if(i>=0){
                        //System.out.println("czesc" + i + " m " + metry);
                        idx = (int) trackDB->trackNodes[aktt]->trVectorSection[i].sectionIndex;
                        pozW.x = trackDB->trackNodes[aktt]->trVectorSection[i].x;
                        pozW.y = trackDB->trackNodes[aktt]->trVectorSection[i].y;
                        pozW.z = trackDB->trackNodes[aktt]->trVectorSection[i].z;
                        pozO.x = trackDB->trackNodes[aktt]->trVectorSection[i].ax;
                        pozO.y = trackDB->trackNodes[aktt]->trVectorSection[i].ay;
                        pozO.z = trackDB->trackNodes[aktt]->trVectorSection[i].az;
                        pozT.x = (int) trackDB->trackNodes[aktt]->trVectorSection[i].tileX;
                        pozT.z = (int) trackDB->trackNodes[aktt]->trVectorSection[i].tileZ;
                        return true;
                    }
                    akticz = 0;
                    metry = 0;
                    
                    //rozjazdy[trackDB.trackNodes[aktt].pins[1].link][1] = Math.abs(kierunek-1);
                    //rozjazdy[trackDB.trackNodes[aktt].pins[1].link][0] = aktt;
                    
                    kierunek = trackDB->trackNodes[aktt]->pins[0].direction;
                    aktt = trackDB->trackNodes[aktt]->pins[0].link;
                    //System.out.println("zjezdzasz z toru na " + aktt);
                    return false;
                }
                return false;
            case 2:
                if(kierunek==1){ u = trackDB->trackNodes[aktt]->outputPinCount;}
                if(kierunek==0){ u = trackDB->trackNodes[aktt]->inputPinCount;}
                akticz = 0;
                metry = 0;
                pozW.x = trackDB->trackNodes[aktt]->uid.x;
                pozW.y = trackDB->trackNodes[aktt]->uid.y;
                pozW.z = trackDB->trackNodes[aktt]->uid.z;
                pozT.x = (int) trackDB->trackNodes[aktt]->uid.tileX;
                pozT.z = (int) trackDB->trackNodes[aktt]->uid.tileZ;
                
                int kt, at;
                
                if(u==1){
                    //System.out.println("zjezdzasz z rozjazdu "+aktt);
                    kt = trackDB->trackNodes[aktt]->pins[trackDB->trackNodes[aktt].direction->inputPinCount*kierunek];
                    at = trackDB->trackNodes[aktt]->pins[trackDB->trackNodes[aktt].link->inputPinCount*kierunek];
                    kierunek=kt; aktt=at;
                    
                    if(kierunek==1) {
                        idx = (int) trackDB->trackNodes[aktt]->trVectorSection[0].sectionIndex;
                        metry = 0;
                    } else {
                        idx = (int) trackDB->trackNodes[aktt]->trVectorSection[trackDB->trackNodes[aktt]->iTrv-1].sectionIndex;
                        metry = trackDB->tsection->sekcja.at(idx)->getDlugosc();
                    }
                    
                    return false;
                }
                //System.out.println("rozjazd "+aktt);
                //if(rozjazdy[aktt][0]!=0){
                //    kt = rozjazdy[aktt][1];
                //    at = rozjazdy[aktt][0];
                //    if(koniec) rozjazdy[aktt][0] = 0;
                //} else {
                    //if(rozjazd){
                        //rozjazdy[aktt][1] = 
                kt = trackDB->trackNodes[aktt]->pins[trackDB->trackNodes[aktt].direction->inputPinCount*kierunek];
                        //rozjazdy[aktt][0] = 
                at = trackDB->trackNodes[aktt]->pins[trackDB->trackNodes[aktt].link->inputPinCount*kierunek];
                    //} else {
                    //    rozjazdy[aktt][1] = kt = trackDB.trackNodes[aktt].pins[trackDB.trackNodes[aktt].direction.inputPinCount*kierunek+1];
                    //    rozjazdy[aktt][0] = at = trackDB.trackNodes[aktt].pins[trackDB.trackNodes[aktt].link.inputPinCount*kierunek+1];
                    //}
                //}
                kierunek = kt; aktt = at;
                if(kierunek==1) {
                    idx = (int) trackDB->trackNodes[aktt]->trVectorSection[0].sectionIndex;
                    metry = 0;
                } else {
                    idx = (int) trackDB->trackNodes[aktt]->trVectorSection[trackDB->trackNodes[aktt]->iTrv-1].sectionIndex;
                    metry = trackDB->tsection->sekcja.at(idx)->getDlugosc();
                    //System.out.println("metry "+metry);
                }
                return false;
        }
        return false;
}
*/