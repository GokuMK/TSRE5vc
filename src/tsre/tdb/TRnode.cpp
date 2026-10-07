/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tdb/TRnode.h>
#include <tsre/math3d/GLMatrix.h>
#include <math.h>
#include <tsre/Game.h>
#include <QString>
#include <QDebug>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>

TRnode::TRnode() {
    typ = -1;
    inputPinCount = 0;
    outputPinCount = 0;
    iTri = 0;
    trItemRef = nullptr;
    junction.unknown0 = junction.shapeIndex = junction.unknown2 = 0;
    pins[0].link = pins[1].link = pins[2].link = 0;
    pins[0].direction = pins[1].direction = pins[2].direction = 0;
}

TRnode::TRnode(const TRnode& o) {
    typ = o.typ;
    junction = o.junction;
    endNodeValue = o.endNodeValue;
    uid = o.uid;
    iTrv = o.iTrv;
    if(iTrv > 0){
        trVectorSection = new TrackVectorSection[iTrv];
        for(int i = 0; i < iTrv; i++){
            trVectorSection[i] = o.trVectorSection[i];
        }
    }
    iTri = o.iTri;
    if(iTri > 0){
        trItemRef = new int[iTri];
        memcpy(trItemRef, o.trItemRef, sizeof(int[iTri]));
    }
    inputPinCount = o.inputPinCount;
    outputPinCount = o.outputPinCount;
    pins = o.pins;
}

TRnode::~TRnode() {
    if(trVectorSection != NULL)
        delete[] trVectorSection;
        
    if(trItemRef != NULL)
        delete[] trItemRef;
}

void TRnode::loadUtf16Data(FileBuffer *data){
    bool ok = false;
    QString sh = "";
    int i = 0, j = 0, ii = 0, uu = 0;
    float xx = 0;
    int t = TrackNodeText::readInt(data); // odczytanie numeru sciezki
                            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                                if(sh == "trendnode"){
                                    typ = 0; //typ endnode
                                    endNodeValue = TrackNodeText::readUInt(data);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                if(sh == "trvectornode"){
                                    typ = 1; //typ vector 
                                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                                        if(sh == "trvectorsections"){
                                            int uu = (int) ParserX::GetNumberInside(data, &ok);
                                            if(ok){
                                                iTrv = uu;
                                                trVectorSection = new TrackVectorSection[uu]; // przydzielenie pamieci dla sciezki
                                                for (j = 0; j < uu; j++) {
                                                    trVectorSection[j].load(data);
                                                }
                                            }
                                            ParserX::SkipToken(data);
                                            continue;
                                        }
                                        if(sh == "tritemrefs"){
                                            uu = (int) ParserX::GetNumber(data);
                                            iTri = uu;
                                            trItemRef = new int[uu]; // przydzielenie pamieci dla sciezki
                                            if(uu > 0){
                                                for (j = 0; j < uu; j++) {
                                                    trItemRef[j] = TrackNodeText::readInt(data);
                                                }
                                                ParserX::SkipToken(data);
                                            }
                                            ParserX::SkipToken(data);
                                            continue;
                                        }
                                        qDebug() << "#TDB TrVectorNode - undefined token " << sh;
                                        ParserX::SkipToken(data);
                                    }
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                if(sh == "trjunctionnode"){
                                    typ = 2; //typ rozjazd
                                    junction.unknown0 = TrackNodeText::readUInt(data);
                                    junction.shapeIndex = TrackNodeText::readUInt(data);
                                    junction.unknown2 = TrackNodeText::readUInt(data, true);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                if(sh == "trpins"){
                                    inputPinCount = TrackNodeText::readInt(data);
                                    outputPinCount = TrackNodeText::readInt(data);

                                    for (int i = 0; i < inputPinCount + outputPinCount; i++) {
                                        pins[i].link = TrackNodeText::readInt(data);
                                        pins[i].direction = TrackNodeText::readInt(data);
                                    }
                                    ParserX::SkipToken(data);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                if(sh == "uid"){
                                    uid.load(data);
                                    ParserX::SkipToken(data);
                                    continue;              
                                }
                                qDebug() << "#TDB TrackNode - undefined token " << sh;
                                //trackNodes[t] = NULL;
                                ParserX::SkipToken(data);
                            }
    return;
}

void TRnode::saveToStream(QTextStream &out, int nid){
    out << "TrackNode ( " << nid << "\n";
        switch (typ) {
            case 0:
                out << "	TrEndNode ( " << endNodeValue << " )\n";
                out << "	UiD ( ";
                uid.save(out);
                out << ")\n";
                out << "	TrPins ( 1 0\n";
                out << "		TrPin ( " << pins[0].link << " " << pins[0].direction << " )\n";
                out << "	)\n";
                break;
            case 1:
                out << "	TrVectorNode (\n";
                out << "		TrVectorSections ( " << iTrv << "";
                for (int j = 0; j < iTrv; j++) {
                    trVectorSection[j].save(out);
                    if (j % 11 == 0 && j > 0 && j < iTrv - 1)
                        out << "\n					";
                }
                out << " )\n";
                if(trItemRef != 0 && iTri > 0){
                    out << "		TrItemRefs ( "<<iTri<<"\n";
                    for(int j = 0; j<iTri; j++){
                        out << "			TrItemRef ( "<<trItemRef[j]<<" )\n";
                    }
                    out << "		)\n";
                }
                out << "	)\n";
                out << "	TrPins ( 1 1\n";
                out << "		TrPin ( " << pins[0].link << " " << pins[0].direction << " )\n";
                out << "		TrPin ( " << pins[1].link << " " << pins[1].direction << " )\n";
                out << "	)\n";
                break;
            case 2:
                out << "	TrJunctionNode ( " << junction.unknown0 << " " << junction.shapeIndex << " " << junction.unknown2 << " )\n";
                out << "	UiD ( ";
                uid.save(out);
                out << ")\n";
                out << "	TrPins ( 1 2\n";
                out << "		TrPin ( " << pins[0].link << " " << pins[0].direction << " )\n";
                out << "		TrPin ( " << pins[1].link << " " << pins[1].direction << " )\n";
                out << "		TrPin ( " << pins[2].link << " " << pins[2].direction << " )\n";
                out << "	)\n";
                break;
        }

        out << ")\n";
}

Vector2i* TRnode::getTile() {
    if (typ == 1) return new Vector2i((int) trVectorSection[0].tileX, (int) trVectorSection[0].tileZ);
    else return new Vector2i((int) uid.tileX, -(int) uid.tileZ);
}

bool TRnode::isEnd() {
    if (typ == 1) return false;
    return true;
}

bool TRnode::equals(TRnode* r) {
    if (typ != r->typ)
        return false;
    if (typ == 0) {
        if (uid.tileX != r->uid.tileX)
            return false;
        if (uid.tileZ != r->uid.tileZ)
            return false;
        float len[3];
        len[0] = uid.x - r->uid.x;
        len[1] = uid.y - r->uid.y;
        len[2] = uid.z - r->uid.z;
        
        if (fabs(Vec3::length(len)) < 0.17)
            return true;
    }
    return false;
}

bool TRnode::equalsIgnoreType(TRnode* r) {
    if (typ == 1)
        return false;
    else {
        if (uid.tileX != r->uid.tileX)
            return false;
        if (uid.tileZ != r->uid.tileZ)
            return false;
        float len[3];
        len[0] = uid.x - r->uid.x;
        len[1] = uid.y - r->uid.y;
        len[2] = uid.z - r->uid.z;
        
        if (fabs(Vec3::length(len)) < 0.17)
            return true;
    }
    return false;
}

int TRnode::podmienTrPin(int stare, int nowe) {
        for(int j = 0; j < 3; j++)
            if(pins[j].link == stare){
                pins[j].link = nowe;
                return j;
            }
        return 0;
    }

bool TRnode::isLikedTo(int id) {
        for(int j = 0; j < 3; j++)
            if(pins[j].link == id){
                return true;
            }
        return false;
    }

int TRnode::setTrPinK(int id, int nowe) {
        for(int j = 0; j < 3; j++)
            if(pins[j].link == id){
                pins[j].direction = nowe;
                return j;
            }
        return 0;
    }

float TRnode::getVectorSectionXRot(int id){
    if(id >= iTrv - 1)
        return trVectorSection[id].ax;
    float pos1[3], pos2[3];
    pos1[0] = trVectorSection[id].x;
    pos1[1] = trVectorSection[id].y;
    pos1[2] = trVectorSection[id].z;
    pos2[0] = trVectorSection[id+1].x;
    pos2[1] = trVectorSection[id+1].y;
    pos2[2] = trVectorSection[id+1].z;
    
    pos2[0] += 2048*(trVectorSection[id+1].tileX-trVectorSection[id].tileX);
    pos2[2] += 2048*(trVectorSection[id+1].tileZ-trVectorSection[id].tileZ);

    float dlugosc = Vec3::distance(pos1, pos2);
    return (float)(asin((pos1[1]-pos2[1])/(dlugosc))); 
}

void TRnode::addPositionOffset(float offsetXYZ[]){
    if(this->typ == 0 || this->typ == 2){
        int x = uid.tileX, z = uid.tileZ;
        float pos[3];
        pos[0] = uid.x + offsetXYZ[0];
        pos[1] = uid.y + offsetXYZ[1];
        pos[2] = uid.z + offsetXYZ[2];
        //qDebug() << "old tile" << wObj->x << wObj->y;
        while(pos[0] > 1024 || pos[0] < -1024 || pos[2] > 1024 || pos[2] < -1024 ){
            Game::check_coords(x, z, pos);
        }
        uid.tileX = x;
        uid.tileZ = z;
        uid.x = pos[0];
        uid.y = pos[1];
        uid.z = pos[2];
    } else if(this->typ == 1){
        for(int i = 0; i < iTrv; i++){
            int x = this->trVectorSection[i].tileX, z = this->trVectorSection[i].tileZ;
            float pos[3];
            pos[0] = this->trVectorSection[i].x + offsetXYZ[0];
            pos[1] = this->trVectorSection[i].y + offsetXYZ[1];
            pos[2] = this->trVectorSection[i].z + offsetXYZ[2];
            //qDebug() << "old tile" << wObj->x << wObj->y;
            while(pos[0] > 1024 || pos[0] < -1024 || pos[2] > 1024 || pos[2] < -1024 ){
                Game::check_coords(x, z, pos);
            }
            this->trVectorSection[i].tileX = x;
            this->trVectorSection[i].tileZ = z;
            this->trVectorSection[i].x = pos[0];
            this->trVectorSection[i].y = pos[1];
            this->trVectorSection[i].z = pos[2];
        }
    }
}

void TRnode::addTrackNodeItemOffset(unsigned int trackNodeOffset, unsigned int trackItemOffset){
    for(int i = 0; i < this->inputPinCount+this->outputPinCount; i++)
        this->pins[i].link += trackNodeOffset;
    
    if(trItemRef == NULL)
        return;
    for(int i = 0; i < this->iTri; i++)
        trItemRef[i] += trackItemOffset;
    

    
}