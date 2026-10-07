/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/tdb/TDB.h>
#include <QDebug>
#include <functional>
#include <tsre/Game.h>
#include <settings/SettingsAccess.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/fileFunctions/MstsTextFileValidation.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/world/Ref.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/world/objects/DynTrackObj.h>
#include <tsre/tdb/TrackShape.h>
#include <tsre/math3d/Intersections.h>

#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/SignalShape.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/tdb/SpeedPostDAT.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/world/objects/SignalObj.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/ErrorMessage.h>
#include <tsre/world/Route.h>

std::unordered_map<int, TRitem*>* TDB::StaticTrackItems;

namespace {

float wrapTdbAngle(float angle) {
    while(angle > (float)M_PI)
        angle -= (float)(2.0 * M_PI);
    while(angle <= (float)-M_PI)
        angle += (float)(2.0 * M_PI);
    return angle;
}

void matrixToTdbFrame(const float *matrix, float *frame) {
    frame[0] = std::asin(qBound(-1.0f, -matrix[9], 1.0f));
    frame[1] = wrapTdbAngle(
            (float)M_PI - std::atan2(matrix[8], matrix[10]));
    frame[2] = -std::atan2(matrix[1], matrix[5]);
}

void tdbFrameToMatrix(const float *frame, float *matrix) {
    Mat4::identity(matrix);
    Mat4::rotateY(matrix, matrix, -(float)M_PI - frame[1]);
    Mat4::rotateX(matrix, matrix, frame[0]);
    Mat4::rotate(matrix, matrix, -frame[2], 0.0f, 0.0f, 1.0f);
}

void advanceTdbFrame(const float *startFrame, float sectionAngle,
        float *endFrame) {
    if(sectionAngle == 0.0f) {
        endFrame[0] = startFrame[0];
        endFrame[1] = startFrame[1];
        endFrame[2] = startFrame[2];
        return;
    }
    float matrix[16];
    tdbFrameToMatrix(startFrame, matrix);
    Mat4::rotateY(matrix, matrix, -sectionAngle);
    matrixToTdbFrame(matrix, endFrame);
}

void transformTdbSectionDelta(Vector3f &delta, const float *frame) {
    if(frame[2] != 0.0f)
        delta.rotateZ(frame[2], 0.0f);
    delta.rotateX(frame[0], 0.0f);
    delta.rotateY((float)M_PI + frame[1], 0.0f);
}

}

TDB::TDB(TSectionDAT* tsection, bool road) {
    loaded = false;
    this->road = road;
    if(this->road){
        tdbId = 1;
        tdbName = ErrorMessage::Source_RDB;
    }
    serial = 0;
    wysokoscSieci = 4;
    iTRitems = 0;
    iTRnodes = 0;

    if(tsection == NULL)
        this->tsection = new TSectionDAT();
    else
        this->tsection = tsection;
    
    return;
}

void TDB::loadTdb(){
    loaded = false;
    sourceFileExists = false;
    if(!this->road) {
        if(speedPostDAT == NULL)
            speedPostDAT = new SpeedPostDAT();
        if(sigCfg == NULL)
            sigCfg = new SigCfg();
    }

    QString sh;
    QString extension = "tdb";
    if(this->road) extension = "rdb";
    QString path = Game::root + "/ROUTES/" + Game::route + "/" + Game::routeName + "." + extension;
    path = ContentPath::normalize(path);
    qDebug() << "Wczytywanie pliku tdb: " << path;
    QFile file(path);
    sourceFileExists = file.exists();
    if(!sourceFileExists) {
        qDebug() << "Database file does not exist; using an empty"
                 << (road ? "RDB" : "TDB");
        loaded = true;
        return;
    }
    auto reportLoadFailure = [&](const QString &reason) {
        qWarning() << "Failed to load existing" << (road ? "RDB" : "TDB")
                   << path << reason;
        ErrorMessagesLib::PushErrorMessage(new ErrorMessage(
            ErrorMessage::Type_Error,
            tdbName,
            QString("Failed to load existing %1 file: %2")
                .arg(road ? "road database" : "track database", path),
            reason + " The existing file will not be overwritten."
        ));
    };
    if(!file.open(QIODevice::ReadOnly)) {
        reportLoadFailure(file.errorString());
        return;
    }
    if(file.size() < 34) {
        reportLoadFailure("File is too short to be a valid MSTS database.");
        file.close();
        return;
    }
    FileBuffer* data = ReadFile::read(&file);
    file.close();
    data->toUtf16();
    QString validationError;
    if(!MstsTextFileValidation::validate(data, validationError)) {
        reportLoadFailure(validationError);
        delete data;
        return;
    }
    data->skipBOM();
    ParserX::NextLine(data);
    iTRnodes = 0;
    bool trackDbFound = false;
    
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        if (sh == "trackdb") {
            trackDbFound = true;
            try {
                loadUtf16Data(data);
            } catch(const FileBuffer::ParseError &error) {
                reportLoadFailure(QString::fromUtf8(error.what()));
                delete data;
                return;
            }
            ParserX::SkipToken(data);
            continue;
            
        }
        qDebug() << "#TDB undefined token " << sh;
        ParserX::SkipToken(data);
    }
    delete data;

    if(!trackDbFound) {
        reportLoadFailure("The TrackDB block is missing.");
        return;
    }
    
    if(tsection->updateSectionDataRequired){
        this->updateSectionAndShapeIds(tsection->autoFixedSectionIds, tsection->autoFixedShapeIds);
    }
    /*for(int i = 1; i <= iTRnodes; i++){
        int old;
        for(int j = 0; j < trackNodes[i]->iTri; j++){
            if(j > 0 && old > trackItems[trackNodes[i]->trItemRef[j]]->getTrackPosition())
                qDebug() << "--fail!--"<< old << trackItems[trackNodes[i]->trItemRef[j]]->getTrackPosition();
            old = trackItems[trackNodes[i]->trItemRef[j]]->getTrackPosition();
        }
    }*/
    if(!this->road){
        loadTit();
        checkTrSignalRDirs();
        //checkSignals();
    }
    //save();
    loaded = true;
    if(!this->road)
        printVectorParamStats();
}

void TDB::printVectorParamStats(){
    int vectorNodeCount = 0;
    int vectorSectionCount = 0;
    int param13NonZeroCount = 0;
    int param15NonZeroCount = 0;
    int anyNonZeroCount = 0;

    for (int i = 1; i <= iTRnodes; i++) {
        TRnode *node = trackNodes[i];
        if (node == NULL || node->typ != 1 || node->trVectorSection == NULL) {
            continue;
        }

        vectorNodeCount++;
        for (int j = 0; j < node->iTrv; j++) {
            const float param13 = node->trVectorSection[j].ax;
            const float param15 = node->trVectorSection[j].az;

            vectorSectionCount++;
            if (param13 != 0.0f) {
                param13NonZeroCount++;
            }
            if (param15 != 0.0f) {
                param15NonZeroCount++;
            }
            if (param13 != 0.0f || param15 != 0.0f) {
                anyNonZeroCount++;
            }
        }
    }

    qDebug() << (road ? "RDB" : "TDB")
             << "vector param stats:"
             << "vectorNodes =" << vectorNodeCount
             << "vectorSections =" << vectorSectionCount
             << "param[13] != 0:" << param13NonZeroCount
             << "param[15] != 0:" << param15NonZeroCount
             << "param[13] || param[15] != 0:" << anyNonZeroCount;
}

void TDB::loadUtf16Data(FileBuffer *data){
    int i, j, ii, uu;
    float xx;
    int t;
    bool ok;
    QString sh;
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                if(sh == "tracknodes"){
                    iTRnodes = (int) ParserX::GetNumber(data); //odczytanie ilosci sciezek
                    qDebug() << "TDB TrackNodes count " << iTRnodes;

                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                        if(sh == "tracknode"){
                            t = TrackNodeText::readInt(data); // odczytanie numeru sciezki
                            trackNodes[t] = new TRnode();
                            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                                if(sh == "trendnode"){
                                    trackNodes[t]->typ = 0; //typ endnode
                                    trackNodes[t]->endNodeValue = TrackNodeText::readUInt(data);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                if(sh == "trvectornode"){
                                    trackNodes[t]->typ = 1; //typ vector 
                                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                                        if(sh == "trvectorsections"){
                                            uu = (int) ParserX::GetNumberInside(data, &ok);
                                            if(ok){
                                                trackNodes[t]->iTrv = uu;
                                                trackNodes[t]->trVectorSection = new TrackVectorSection[uu]; // przydzielenie pamieci dla sciezki
                                                for (j = 0; j < uu; j++) {
                                                    trackNodes[t]->trVectorSection[j].load(data);
                                                }
                                            }
                                            ParserX::SkipToken(data);
                                            continue;
                                        }
                                        if(sh == "tritemrefs"){
                                            uu = (int) ParserX::GetNumber(data);
                                            trackNodes[t]->iTri = uu;
                                            trackNodes[t]->trItemRef = new int[uu]; // przydzielenie pamieci dla sciezki
                                            if(uu > 0){
                                                for (j = 0; j < uu; j++) {
                                                    trackNodes[t]->trItemRef[j] = TrackNodeText::readInt(data);
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
                                    trackNodes[t]->typ = 2; //typ rozjazd
                                    trackNodes[t]->junction.unknown0 = TrackNodeText::readUInt(data);
                                    trackNodes[t]->junction.shapeIndex = TrackNodeText::readUInt(data);
                                    trackNodes[t]->junction.unknown2 = TrackNodeText::readUInt(data, true);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                if(sh == "trpins"){
                                    trackNodes[t]->inputPinCount = TrackNodeText::readInt(data);
                                    trackNodes[t]->outputPinCount = TrackNodeText::readInt(data);

                                    for (int i = 0; i < (trackNodes[t]->inputPinCount + trackNodes[t]->outputPinCount); i++) {
                                        trackNodes[t]->pins[i].link = TrackNodeText::readInt(data);
                                        trackNodes[t]->pins[i].direction = TrackNodeText::readInt(data);
                                    }
                                    ParserX::SkipToken(data);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                if(sh == "uid"){
                                    trackNodes[t]->uid.load(data);
                                    ParserX::SkipToken(data);
                                    continue;              
                                }
                                qDebug() << "#TDB TrackNode - undefined token " << sh;
                                //trackNodes[t] = NULL;
                                ParserX::SkipToken(data);
                            }
                            ParserX::SkipToken(data);
                            continue;
                        }
                        qDebug() << "#TDB TrackNodes - undefined token " << sh;
                        ParserX::SkipToken(data);
                    }
                    ParserX::SkipToken(data);
                    continue;
                }
                if(sh == "serial"){
                    this->serial = (int) ParserX::GetNumber(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if(sh == "tritemtable"){
                    iTRitems = (int) ParserX::GetNumber(data); //odczytanie ilosci sciezek
                    TRitem* nowy;
                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                        //qDebug() <<"ssh1 "<< sh;
                        nowy = new TRitem();
                        if(this->road)
                            nowy->tdbId = 1;
                        
                        if(!nowy->init(sh)){
                            qDebug() << "#TDB TrItemTable undefined token " << sh;
                            ParserX::SkipToken(data);
                            continue;
                        }

                        while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                            nowy->set(sh, data);
                            ParserX::SkipToken(data);
                        }

                        this->trackItems[nowy->trItemId] = nowy;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    ParserX::SkipToken(data);
                    continue;
                }
                qDebug() << "#TDB trackdb undefined token " << sh;
                ParserX::SkipToken(data);
            }
}

void TDB::updateUiDs(QVector<int*> &trackObjUpdates, int startNode){
        for(int i = startNode; i <= iTRnodes; i++){
            TRnode *n = trackNodes[i];
            if(n == NULL)
                continue;
            
            for(int j = 0; j < trackObjUpdates.size(); j++){
                if(n->typ == 2){
                    if(n->uid.worldTileX == trackObjUpdates[j][0])
                        if(n->uid.worldTileZ == -trackObjUpdates[j][1])
                            if(n->uid.worldObjectId == trackObjUpdates[j][2]){
                                n->uid.worldTileX = trackObjUpdates[j][3];
                                n->uid.worldTileZ = -trackObjUpdates[j][4];
                                n->uid.worldObjectId = trackObjUpdates[j][5];
                    }
                }
                if(n->typ == 1){
                    for(int jj = 0; jj < n->iTrv; jj++){
                        if(n->trVectorSection[jj].worldTileX == trackObjUpdates[j][0])
                            if(n->trVectorSection[jj].worldTileZ == -trackObjUpdates[j][1])
                                if(n->trVectorSection[jj].worldObjectId == trackObjUpdates[j][2]){
                                    n->trVectorSection[jj].worldTileX = trackObjUpdates[j][3];
                                    n->trVectorSection[jj].worldTileZ = -trackObjUpdates[j][4];
                                    n->trVectorSection[jj].worldObjectId = trackObjUpdates[j][5];
                        }
                    }
                }
            }
        }
}

void TDB::updateSectionAndShapeIds(QHash<unsigned int,unsigned int>& fixedSectionIds, QHash<unsigned int,unsigned int>& fixedShapeIds){
        for(int i = 1; i <= iTRnodes; i++){
            TRnode *n = trackNodes[i];
            if(n == NULL)
                continue;
            if(n->typ == 2){
                if(fixedShapeIds[n->junction.shapeIndex] > 0)
                    n->junction.shapeIndex = fixedShapeIds[n->junction.shapeIndex];
            }
            if(n->typ == 1)
                for(int j = 0; j < n->iTrv; j++){
                    if(fixedShapeIds[n->trVectorSection[j].shapeIndex] > 0)
                        n->trVectorSection[j].shapeIndex = fixedShapeIds[n->trVectorSection[j].shapeIndex];
                    if(fixedSectionIds[n->trVectorSection[j].sectionIndex] > 0)
                        n->trVectorSection[j].sectionIndex = fixedSectionIds[n->trVectorSection[j].sectionIndex];
                }
        }
}

void TDB::loadTit(){
    QString sh;
    QString extension = "tit";
    if(this->road) extension = "rit";
    QString path = Game::root + "/ROUTES/" + Game::route + "/" + Game::routeName + "." + extension;
    path = ContentPath::normalize(path);
    qDebug() << path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    FileBuffer* bufor = ReadFile::read(&file);
    file.close();
    bufor->toUtf16();
    bufor->skipBOM();
    ParserX::NextLine(bufor);
    
    while (!((sh = ParserX::NextTokenInside(bufor).toLower()) == "")) {
        if(sh == "tritemtable"){
            int iiTRitems = (int) ParserX::GetNumber(bufor); //odczytanie ilosci sciezek
            TRitem* nowy = new TRitem();
            nowy->titLoading = true;
            
            while (!((sh = ParserX::NextTokenInside(bufor).toLower()) == "")) {
                //qDebug() <<"ssh2 "<< sh;
                if(!nowy->init(sh)){
                    qDebug() << "#TIT TrItemTable undefined token " << sh;
                    ParserX::SkipToken(bufor);
                    continue;
                }

                while (!((sh = ParserX::NextTokenInside(bufor).toLower()) == "")) {
                    nowy->set(sh, bufor);
                    ParserX::SkipToken(bufor);
                }
                
                if(this->trackItems[nowy->trItemId] == NULL){
                    qDebug() << "#TIT tdb fail" << nowy->trItemId;
                } else {
                    if(nowy->trSignalDirs > 0){
                        this->trackItems[nowy->trItemId]->trSignalRDir = new float[nowy->trSignalDirs * 6];
                        memcpy(this->trackItems[nowy->trItemId]->trSignalRDir, nowy->trSignalRDir, sizeof(float[nowy->trSignalDirs * 6]));
                    }
                }
                ParserX::SkipToken(bufor);
            }
            ParserX::SkipToken(bufor);
            continue;
        }
        qDebug() << "#TIT undefined token " << sh;
        ParserX::SkipToken(bufor);
    }
    return;
}

void TDB::mergeTDB(TDB *secondTDB, float offsetXYZ[3], unsigned int& trackNodeOffset, unsigned int& trackItemOffset, QHash<unsigned int,unsigned int>& fixedSectionIds, QHash<unsigned int,unsigned int>& fixedShapeIds){
    trackNodeOffset = this->iTRnodes;
    trackItemOffset = this->iTRitems;
    
    // Merge TrackSection file
    if(!road){
        qDebug() << "merge tsc";
        this->tsection->mergeTSection(secondTDB->tsection, fixedSectionIds, fixedShapeIds);
        qDebug() << "update tdb";
        secondTDB->updateSectionAndShapeIds(fixedSectionIds, fixedShapeIds);
    }
    
    // Add new trackNodes
    qDebug() << "merge tdb";
    for(int i = 1; i <= secondTDB->iTRnodes; i++){
        TRnode *n = secondTDB->trackNodes[i];
        if(n == NULL){
            qDebug() << "TRnode NULL" << i;
            continue;
        }
        //qDebug() << "o";
        n->addPositionOffset(offsetXYZ);
        //qDebug() << "i";
        n->addTrackNodeItemOffset(trackNodeOffset, trackItemOffset);
        this->trackNodes[++iTRnodes] = n;
    }
    qDebug() << "new tracknodes" << secondTDB->iTRnodes;
    
    // Add new trackItems
    for(int i = 0; i < secondTDB->iTRitems; i++){
        //qDebug() << i;
        TRitem *n = secondTDB->trackItems[i];
        if(n == NULL){
            qDebug() << "TRitem NULL" << i;
            continue;
        }
        //qDebug() << "addPositionOffset";
        n->addPositionOffset(offsetXYZ);
        //qDebug() << "addTrackNodeItemOffset";
        n->addTrackNodeItemOffset(trackNodeOffset, trackItemOffset);
        this->trackItems[iTRitems++] = n;
    }
    qDebug() << "tdb end";
}

void TDB::checkTrSignalRDirs(){
    for (int i = 0; i <= this->iTRitems; i++) {
        TRitem *it = this->trackItems[i];
        if(it == NULL)
            continue;
        if (it->type != "signalitem") 
            continue;
        if (it->trSignalDirs == 0) 
            continue;
        
        if (it->trSignalDirs > 1){
            qDebug() << "# WARNING - signal dirs more than 1" << i;
        }
        
        if (it->trSignalDir == NULL){ 
            //FAIL, remove link
            qDebug() << "# FAIL - remove link" << i;
            it->trSignalDirs = 0;
            continue;
        }
        
        if (it->trSignalRDir == NULL){
            // Regen trSignalRDir
            qDebug() << "# Regen trSignalRDir " << i;
            int jid = it->trSignalDir[0];
            TRnode *n = trackNodes[jid];
            if(n == NULL){
                //FAIL, remove link
                qDebug() << "# FAIL - remove link" << i;
                it->trSignalDirs = 0;
                it->trSignalDir = NULL;
            }
            if(n->typ == 1){
                //FAIL, remove link
                qDebug() << "# FAIL - remove link" << i;
                it->trSignalDirs = 0;
                it->trSignalDir = NULL;
            }
            
            it->trSignalRDir = new float[it->trSignalDirs * 6];
            it->trSignalRDir[0 + 0] = n->uid.x;
            it->trSignalRDir[0 + 1] = n->uid.y;
            it->trSignalRDir[0 + 2] = n->uid.z;
            it->trSignalRDir[0 + 3] = n->uid.tileX;
            it->trSignalRDir[0 + 4] = n->uid.tileZ;
            it->trSignalRDir[0 + 5] = n->uid.ay;
            
        }
    }

}

int TDB::getNewTRitemId(){
    for (int i = 0; i < this->iTRitems; i++) {
        if(this->trackItems[i] == NULL)
            continue;
        if(Game::useTdbEmptyItems)
            if(this->trackItems[i]->type == "emptyitem"){
                return i;
            }
    }
    return this->iTRitems++;
}

void TDB::fillDynTrack(DynTrackObj* track){
    int tType[5];
    float tAngle[5];
    float tRadius[5];
    int count = 0;
    
    int foundIdx = -1;
    TrackShape::SectionIdx* newRShape = NULL;
    for (int i = 0; i < 5; i++) {
        if(track->sections[i].sectIdx > 1000000) {
            continue;
        }
        tType[count] = track->sections[i].type;
        tAngle[count] = track->sections[i].a;
        tRadius[count] = track->sections[i].r;
        count++;
    }
    
    bool success;
    for(int i = tsection->tsectionShapes; i < tsection->routeShapes; i++){
        success = true;
        if(tsection->shape[i] == NULL) continue;
        
        if(tsection->shape[i]->path[0].n == count){
            for(int j = 0; j<count; j++){
                if(!(tsection->sekcja[tsection->shape[i]->path[0].sect[j]]->type == tType[j])){
                    success = false;
                    break;
                }
                if(tType[j] == 1){
                    if(!(tsection->sekcja[tsection->shape[i]->path[0].sect[j]]->angle == tAngle[j])){
                        success = false;
                        break;
                    }
                    if(!(tsection->sekcja[tsection->shape[i]->path[0].sect[j]]->radius == tRadius[j])){
                        success = false;
                        break;
                    }
                } 
                else if(tType[j] == 0){
                    if(!(tsection->sekcja[tsection->shape[i]->path[0].sect[j]]->size == tAngle[j])){
                        success = false;
                        break;
                    }
                    if(!(tsection->sekcja[tsection->shape[i]->path[0].sect[j]]->val1 == tRadius[j])){
                        success = false;
                        break;
                    }
                }
            }
            ////
            if(success){
                foundIdx = i;
                break;
            }
        }
    }
    qDebug() << "foundIdx "<<foundIdx;
    if(foundIdx == -1){
        newRShape = new TrackShape::SectionIdx[1];
        newRShape->n = count;
        
        for(int j = 0; j<count; j++){
            TSection* newSect0 = new TSection(tsection->routeMaxIdx);
            TSection* newSect1 = new TSection(tsection->routeMaxIdx+1);
            
            if(tType[j] == 1){
                newSect0->type = 1;
                newSect0->angle = -fabs(tAngle[j]);
                newSect0->radius = tRadius[j];
                newSect1->type = 1;
                newSect1->angle = fabs(tAngle[j]);
                newSect1->radius = tRadius[j];
                tsection->sekcja[newSect0->id] = newSect0;
                tsection->sekcja[newSect1->id] = newSect1;
                this->updateTrackSection(newSect0->id);
                this->updateTrackSection(newSect1->id);
                
                if(tAngle[j]<0) 
                    newRShape[0].sect[j] = newSect0->id;
                else 
                    newRShape[0].sect[j] = newSect1->id;
            } 
            else if(tType[j] == 0){
                newSect0->type = 0;
                newSect0->size = tAngle[j];
                newSect0->val1 = tRadius[j];
                
                tsection->sekcja[newSect0->id] = newSect0;
                this->updateTrackSection(newSect0->id);
                newRShape[0].sect[j] = newSect0->id;
            }
            //qDebug() << "sid "<< newSect0->id;
            //qDebug() << "sid "<< newSect1->id;
            tsection->routeMaxIdx+=2;
        }
        foundIdx = tsection->routeShapes;
        TrackShape* newShape = new TrackShape(tsection->routeShapes);
        newShape->dyntrack = true;
        newShape->path = newRShape;
        newRShape->pos[0] = 0;
        newRShape->pos[1] = 0;
        newRShape->pos[2] = 0;
        newShape->numpaths = 1;
        tsection->shape[tsection->routeShapes++] = newShape;
        this->updateTrackShape(newShape->id);
    }
    //qDebug() << "foundIdx "<<foundIdx;
    track->sectionIdx = foundIdx;
    for (int i = 0, j = 0; i < 5; i++) {
        if(track->sections[i].sectIdx > 1000000) {
            continue;
        }
        track->sections[i].sectIdx = tsection->shape[track->sectionIdx]->path[0].sect[j];
        j++;
    }
}

int TDB::findVectorNodeBetweenTwoNodes(int first, int second){
    for(int i = 0; i < trackNodes[first]->inputPinCount + trackNodes[first]->outputPinCount; i++)
        for(int j = 0; j < trackNodes[second]->inputPinCount + trackNodes[second]->outputPinCount; j++){
            if(trackNodes[first]->pins[i].link == trackNodes[second]->pins[j].link)
                return trackNodes[first]->pins[i].link;
        }
    
    return -1;
}

int TDB::findNearestNode(int &x, int &z, float* p, float* q, float maxD, bool updatePosition) {
    int nearestID = -1;
    float nearestD = 999;
    for (int j = 1; j <= iTRnodes; j++) {
        TRnode* n = trackNodes[j];
        if(n == NULL) continue;
        if (n->typ == 0 || n->typ == 2) {
            float lenx = ((n->uid.tileX - x)*2048 + n->uid.x - p[0]);
            float leny = (n->uid.y) - p[1];
            float lenz = ((-n->uid.tileZ - z)*2048 - n->uid.z - p[2]);
            float dist = fabs(lenx) + fabs(leny) + fabs(lenz);
            if(dist < nearestD && dist < maxD){
                nearestID = j;
                nearestD = dist;
            }
        }
    }
    if ((nearestD < maxD) && updatePosition) {
        //qDebug() << ":"<<len;
        x = trackNodes[nearestID]->uid.tileX;
        z = -trackNodes[nearestID]->uid.tileZ;
        p[0] = trackNodes[nearestID]->uid.x;
        p[1] = trackNodes[nearestID]->uid.y;
        p[2] = -trackNodes[nearestID]->uid.z;

        q[0] = 0;//n->uid.ax; //fix ??????????
        q[1] = trackNodes[nearestID]->uid.ay;
        q[2] = trackNodes[nearestID]->uid.az;
                //Quat::rotateY(q, q, n->uid.ay);
    }
    
    return nearestID;
}

int TDB::appendTrack(int id, int* ends, int r, int sect, int uid) {
    TRnode* endNode = trackNodes[id];
    float p[3];
    if (endNode->typ == 0) {
        int kierunek = endNode->pins[0].direction;
        TRnode* n = trackNodes[endNode->pins[0].link];
        if (n->typ != 1) {
            qDebug() << "tdb error";
            return -1;
        }

        qDebug() << kierunek;
        n->iTrv++;
        TrackVectorSection *newV = new TrackVectorSection[n->iTrv];

        if (kierunek == 1) {
            std::copy(n->trVectorSection, n->trVectorSection + n->iTrv - 1, newV + 1);
        } else {
            std::copy(n->trVectorSection, n->trVectorSection + n->iTrv - 1, newV);
        }
        delete[] n->trVectorSection;
        n->trVectorSection = newV;
        //qDebug() <<"sect"<< sect;
        float dlugosc = this->tsection->sekcja[sect]->getDlugosc();
        //qDebug() <<"dlugosc"<< dlugosc;
        Vector3f aa;
        this->tsection->sekcja[sect]->getDrawPosition(&aa, dlugosc);
        float startFrame[3] = {
            endNode->uid.ax, endNode->uid.ay, endNode->uid.az
        };
        transformTdbSectionDelta(aa, startFrame);
        float angle = this->tsection->sekcja[sect]->getAngle();
        float endFrame[3];
        advanceTdbFrame(startFrame, angle, endFrame);
        int sid = sect;

        p[0] = endNode->uid.x + aa.x;
        p[1] = endNode->uid.y + aa.y;
        p[2] = endNode->uid.z - aa.z;
        int x = endNode->uid.tileX;
        int z = endNode->uid.tileZ;
        int xx = endNode->uid.tileX;
        int zz = endNode->uid.tileZ;
        float pp[3];
        pp[0] = endNode->uid.x;
        pp[1] = endNode->uid.y;
        pp[2] = endNode->uid.z;
        Game::check_coords(x, z, p);

        float vangle = 0;
        TrackVectorSection *vector = &n->trVectorSection[n->iTrv - 1];
        if (kierunek == 1) {
            vector = &n->trVectorSection[0];
            int tmp = ends[0];
            ends[0] = ends[1];
            ends[1] = tmp;
            if (angle != 0) sid++;
            vangle = angle + M_PI;
            xx = x;
            zz = z;
            pp[0] = p[0];
            pp[1] = p[1];
            pp[2] = p[2];
        }

        vector->sectionIndex = sid;
        vector->shapeIndex = r;
        vector->worldTileX = endNode->uid.worldTileX;
        vector->worldTileZ = endNode->uid.worldTileZ;
        vector->worldObjectId = uid;
        vector->startEndpointIndex = ends[0];
        vector->endEndpointIndex = ends[1];
        vector->opaqueByte = 0;
        vector->tileX = xx;
        vector->tileZ = zz;
        vector->x = pp[0];
        vector->y = pp[1];
        vector->z = pp[2];
        if(kierunek == 1) {
            float reverseFrame[3];
            advanceTdbFrame(startFrame, angle + (float)M_PI, reverseFrame);
            vector->ax = reverseFrame[0];
            vector->ay = reverseFrame[1];
            vector->az = reverseFrame[2];
        } else {
            vector->ax = startFrame[0];
            vector->ay = startFrame[1] + vangle;
            vector->az = startFrame[2];
        }

        //endNode->uid.worldTileX = endNode->uid.worldTileX;
        //endNode->uid.worldTileZ = endNode->uid.worldTileZ;
        endNode->uid.worldObjectId = uid;
        endNode->uid.worldEndpointIndex = ends[1];
        endNode->uid.tileX = x;
        endNode->uid.tileZ = z;
        endNode->uid.x = p[0];
        endNode->uid.y = p[1];
        endNode->uid.z = p[2];
        endNode->uid.ax = endFrame[0];
        endNode->uid.ay = endFrame[1];
        endNode->uid.az = endFrame[2];
    }
    updateTrNode(id);
    updateTrNode(endNode->pins[0].link);
    return id;
}

int TDB::newTrack(int x, int z, float* p, float* qe, int* ends, int r, int sect, int uid) {
    return newTrack(x, z,  p,  qe, ends, r, sect, uid, NULL);
}

int TDB::newTrack(int x, int z, float* p, float* qe, int* ends, int r, int sect, int uid, int* start) {

    //TrackShape* shp = this->tsection->shape[r->value];
    //qDebug() << shp->filename;

    int end1Id = getNextItrNode();//++this->iTRnodes;
    int vecId = getNextItrNode();//++this->iTRnodes;
    int end2Id = getNextItrNode();//++this->iTRnodes;

    int xx = x;
    int zz = z;
    float pp[3];
    pp[0] = p[0]; pp[1] = p[1]; pp[2] = p[2];
    Game::check_coords(xx, zz, pp);
    z = -z;
    zz = -zz;
    ////////////////////////////////////
    this->trackNodes[end1Id] = new TRnode();
    TRnode *newNode = this->trackNodes[end1Id];
    newNode->typ = 0;
    newNode->uid.worldTileX = x;
    newNode->uid.worldTileZ = z;
    newNode->uid.worldObjectId = uid;
    newNode->uid.worldEndpointIndex = ends[0];
    newNode->uid.tileX = xx;
    newNode->uid.tileZ = zz;
    newNode->uid.x = pp[0];
    newNode->uid.y = pp[1];
    newNode->uid.z = -pp[2];
    newNode->uid.ax = qe[0];
    newNode->uid.ay = qe[1] + M_PI;
    newNode->uid.az = qe[2];

    newNode->inputPinCount = 1;
    newNode->pins[0].link = vecId;
    newNode->pins[0].direction = 1;

    /////////////////////////////////////////////////////
    this->trackNodes[vecId] = new TRnode();
    newNode = this->trackNodes[vecId];
    qDebug() << vecId;
    newNode->typ = 1;
    newNode->iTrv = 1;
    newNode->trVectorSection = new TrackVectorSection[newNode->iTrv];
    newNode->trVectorSection[0].sectionIndex = sect;
    newNode->trVectorSection[0].shapeIndex = r;
    newNode->trVectorSection[0].worldTileX = x;
    newNode->trVectorSection[0].worldTileZ = z;
    newNode->trVectorSection[0].worldObjectId = uid;
    newNode->trVectorSection[0].startEndpointIndex = ends[0];
    newNode->trVectorSection[0].endEndpointIndex = ends[1];
    newNode->trVectorSection[0].opaqueByte = 0;
    newNode->trVectorSection[0].tileX = xx;
    newNode->trVectorSection[0].tileZ = zz;
    newNode->trVectorSection[0].x = pp[0];
    newNode->trVectorSection[0].y = pp[1];
    newNode->trVectorSection[0].z = -pp[2];
    newNode->trVectorSection[0].ax = qe[0];
    newNode->trVectorSection[0].ay = qe[1];
    newNode->trVectorSection[0].az = qe[2];

    newNode->inputPinCount = 1;
    newNode->outputPinCount = 1;
    newNode->pins[0].link = end1Id;
    newNode->pins[0].direction = 1;
    newNode->pins[1].link = end2Id;
    newNode->pins[1].direction = 1;
    /////////////////////////////////////////////////////
    qDebug() << sect;
    float dlugosc = this->tsection->sekcja[sect]->getDlugosc();
    qDebug() << dlugosc;
    Vector3f aa;
    this->tsection->sekcja[sect]->getDrawPosition(&aa, dlugosc);
    //if(qe[1] > M_PI)
    //    aa->rotateX(-qe[0], 0);
    //else
    
    transformTdbSectionDelta(aa, qe);
    
    
    float angle = this->tsection->sekcja[sect]->getAngle();
    float endFrame[3];
    advanceTdbFrame(qe, angle, endFrame);
    //Quat::
    //float pp[3];
    pp[0] = pp[0] + aa.x;
    pp[1] = pp[1] + aa.y;
    pp[2] = -pp[2] - aa.z;
    Game::check_coords(xx, zz, pp);

    this->trackNodes[end2Id] = new TRnode();
    newNode = this->trackNodes[end2Id];
    newNode->typ = 0;
    newNode->uid.worldTileX = x;
    newNode->uid.worldTileZ = z;
    newNode->uid.worldObjectId = uid;
    newNode->uid.worldEndpointIndex = ends[1];
    newNode->uid.tileX = xx;
    newNode->uid.tileZ = zz;
    newNode->uid.x = pp[0];
    newNode->uid.y = pp[1];
    qDebug() << "uid7" << newNode->uid.y;
    newNode->uid.z = pp[2];
    newNode->uid.ax = endFrame[0];
    newNode->uid.ay = endFrame[1];
    newNode->uid.az = endFrame[2];

    newNode->inputPinCount = 1;
    newNode->pins[0].link = vecId;
    newNode->pins[0].direction = 0;
    
    updateTrNode(end1Id);
    updateTrNode(vecId);
    updateTrNode(end2Id);
    
    if(start != NULL)
        *start = end1Id;
    return end2Id;
}

int TDB::newJunction(int x, int z, float* p, float* qe, int r, int uid, int end) {

    //TrackShape* shp = this->tsection->shape[r->value];
    //qDebug() << shp->filename;

    int junction = getNextItrNode();//++this->iTRnodes;
    
    int xx = x;
    int zz = z;
    float pp[3];
    pp[0] = p[0]; pp[1] = p[1]; pp[2] = p[2];
    Game::check_coords(xx, zz, pp);
    z = -z;
    zz = -zz;
    ////////////////////////////////////
    this->trackNodes[junction] = new TRnode();
    TRnode *newNode = this->trackNodes[junction];
    newNode->typ = 2;
    newNode->uid.worldTileX = x;
    newNode->uid.worldTileZ = z;
    newNode->uid.worldObjectId = uid;
    newNode->uid.worldEndpointIndex = end;
    newNode->uid.tileX = xx;
    newNode->uid.tileZ = zz;
    newNode->uid.x = pp[0];
    newNode->uid.y = pp[1];
    newNode->uid.z = -pp[2];
    newNode->uid.ax = qe[0];
    newNode->uid.ay = qe[1] + M_PI;
    newNode->uid.az = qe[2];

    newNode->inputPinCount = 1;
    newNode->outputPinCount = 2;
    newNode->pins[0].link = 0;
    newNode->pins[0].direction = 0;
    newNode->pins[1].link = 0;
    newNode->pins[1].direction = 0;
    newNode->pins[2].link = 0;
    newNode->pins[2].direction = 0;

    newNode->junction.shapeIndex = r;
    
    return junction;
}

int TDB::joinTracks(int iendp) {
    TRnode* endp = trackNodes[iendp];

    if(endp->typ == 0){
        for (int j = 1; j <= iTRnodes; j++) {
            TRnode* n = trackNodes[j];
            if(n == NULL) continue;
            if (n->typ == 0) {
                if (j == iendp)
                    continue;
                if (endp->equals(n)) {
                    qDebug() << "polacze " << iendp << " " << j;
                    qDebug() << n->pins[0].link << " " << n->pins[0].direction;
                    qDebug() << endp->pins[0].link << " " << endp->pins[0].direction;
                    joinVectorSections(endp->pins[0].link, n->pins[0].link);
                    return 0;
                }
            }
        }
        for (int j = 1; j <= iTRnodes; j++) {
            TRnode* n = trackNodes[j];
            if(n == NULL) continue;
            if (n->typ == 2) {
                if (j == iendp)
                    continue;
                if (endp->equalsIgnoreType(n)) {
                    qDebug() << "polacze rozjazd " << iendp << " " << j;
                    appendToJunction(j, iendp, 0);
                    return 0;
                }
            }
        }
    }

    if(endp->typ == 2){
        for (int j = 1; j <= iTRnodes; j++) {
            TRnode* n = trackNodes[j];
            if(n == NULL) continue;
            if (n->typ == 0) {
                if (j == iendp)
                    continue;
                if (endp->equalsIgnoreType(n)) {
                    qDebug() << "polacze rozjazd " << iendp << " " << j;
                    appendToJunction(iendp, j, 0);
                    return 0;
                }
            }
        }
    }
    return 0;
}

int TDB::fillJunction(int id){
    
    return 0;
}

int TDB::joinVectorSections(int id1, int id2) {
    TRnode* section1 = trackNodes[id1];
    TRnode* section2 = trackNodes[id2];
    if(section1 == section2)
        return 0;
    int endpk1 = section1->pins[1].link;
    int endpk2 = section2->pins[0].link;
    int endpk11 = section1->pins[0].link;
    int endpk22 = section2->pins[1].link;
    TRnode* section1e1 = trackNodes[section1->pins[0].link];
    TRnode* section1e2 = trackNodes[section1->pins[1].link];
    TRnode* section2e1 = trackNodes[section2->pins[0].link];
    TRnode* section2e2 = trackNodes[section2->pins[1].link];
    
    if (section1e2->equals(section2e1)) {
        qDebug() << "ok";
    }
    else if (section2e2->equals(section1e1)) {
        qDebug() << "switch";
        return joinVectorSections(id2, id1);
    }
    else if (section1e2->equals(section2e2)) {
        qDebug() << "rot2";
        if(rotate(id2) < 0)
            return -1;
        return joinVectorSections(id1, id2);
    }
    else if (section1e1->equals(section2e1)) {
        qDebug() << "rot1";
        if(rotate(id1) < 0)
            return -1;
        return joinVectorSections(id1, id2);
    }

    if(section2->iTri > 0){
        qDebug() << "przeniose " << section2->iTri << " items z " << id2 << " do " << id1;
    }
    moveItemsFrom2to1(id2, id1);
    
    TrackVectorSection *newV = new TrackVectorSection[section1->iTrv + section2->iTrv];

    std::copy(section1->trVectorSection, section1->trVectorSection + section1->iTrv, newV);
    std::copy(section2->trVectorSection, section2->trVectorSection + section2->iTrv, newV + section1->iTrv);
    section1->iTrv = section1->iTrv + section2->iTrv;
    
    delete[] section1->trVectorSection;
    delete[] section2->trVectorSection;
    
    
    section1->trVectorSection = newV;
    section1->pins[1].link = section2->pins[1].link;
    section1->pins[1].direction = section2->pins[1].direction;
    section2e2->podmienTrPin(id2, id1);
    //section2e2->pins[0].link = section1e2->pins[0].link;
    
    trackNodes[id2] = NULL;
    trackNodes[endpk1] = NULL;
    trackNodes[endpk2] = NULL;
    
    updateTrNode(id1);
    updateTrNode(id2);
    updateTrNode(endpk1);
    updateTrNode(endpk2);
    updateTrNode(endpk11);
    updateTrNode(endpk22);
    return 0;
}

void TDB::moveItemsFrom2to1(int id2, int id1){
    TRnode* section1 = trackNodes[id1];
    TRnode* section2 = trackNodes[id2];
    if(section1 == section2)
        return;
    
    // join arrays
    int iTri = section1->iTri + section2->iTri;
    int* array = new int[iTri];
    std::copy(section1->trItemRef, section1->trItemRef + section1->iTri, array);
    std::copy(section2->trItemRef, section2->trItemRef + section2->iTri, array + section1->iTri);
    
    // update items
    float d = getVectorSectionLength(id1);
    qDebug() << "d: " << d;
    TRitem* trit;
    for(int i = 0; i < section2->iTri; i++){
        trit = this->trackItems[section2->trItemRef[i]];
        if(trit == NULL){
            qDebug() << "NULL Item: " << i << " " << section2->trItemRef[i];
        } else {
            trit->addToTrackPos(d);
        }
    }
    
    section1->trItemRef = array;
    section1->iTri = iTri;
}

float TDB::getVectorSectionLength(int id){
    TRnode* n = trackNodes[id];
    float dlugosc = 0;
    TSection* sect;
    for (int i = 0; i < n->iTrv; i++) {
        sect = tsection->sekcja[(int)n->trVectorSection[i].sectionIndex];
        if(sect != NULL)
            dlugosc += sect->getDlugosc();
    }
    return dlugosc;
}

float TDB::getVectorSectionLengthToIdx(int id, int idx){
    TRnode* n = trackNodes[id];
    float dlugosc = 0;
    TSection* sect;
    for (int i = 0; i < idx; i++) {
        sect = tsection->sekcja[(int)n->trVectorSection[i].sectionIndex];
        if(sect != NULL)
            dlugosc += sect->getDlugosc();
    }
    return dlugosc;
}

int TDB::splitVectorSection(int id, int j){
    TRnode* vect = trackNodes[id];
    TRnode* end2 = trackNodes[vect->pins[1].link];
    TRnode* newNode;
    
    int end1Id = getNextItrNode();//++this->iTRnodes;
    int vecId = getNextItrNode();//++this->iTRnodes;
    int end2Id = getNextItrNode();//++this->iTRnodes;
    end2->podmienTrPin(id, vecId);
    
    updateTrNode(vect->pins[1].link);
    
    this->trackNodes[vecId] = new TRnode();
    newNode = this->trackNodes[vecId];
    newNode->typ = 1;
    newNode->iTrv = vect->iTrv - j;
    
    // slpit items
    if(vect->iTri > 0){
        float vectDlugosc = this->getVectorSectionLengthToIdx(id, j);
        // calculate
        int ivecItems = 0;
        int inewItems = 0;
        TRitem* trit; 
        for(int i = 0; i < vect->iTri; i++){
            trit = this->trackItems[vect->trItemRef[i]];
            if(trit == NULL) continue;
            if(trit->getTrackPosition() < vectDlugosc) ivecItems++;
            else inewItems++;
        }
        int* vecItems = new int[ivecItems];
        int* newItems = new int[inewItems];
        // fill
        ivecItems = 0;
        inewItems = 0;
        for(int i = 0; i < vect->iTri; i++){
            trit = this->trackItems[vect->trItemRef[i]];
            if(trit == NULL) 
                continue;
            if(trit->getTrackPosition() < vectDlugosc) 
                vecItems[ivecItems++] = vect->trItemRef[i];
            else {
                trit->trackPositionAdd(-vectDlugosc);
                newItems[inewItems++] = vect->trItemRef[i];
            }
        }
        vect->iTri = ivecItems;
        newNode->iTri = inewItems;
        delete[] vect->trItemRef;
        vect->trItemRef = vecItems;
        newNode->trItemRef = newItems;
    }
    
    TrackVectorSection *newV = new TrackVectorSection[newNode->iTrv];
    std::copy(vect->trVectorSection + j, vect->trVectorSection + vect->iTrv, newV);
    newNode->trVectorSection = newV;
    
    newNode->inputPinCount = 1;
    newNode->outputPinCount = 1;
    newNode->pins[0].link = end2Id;
    newNode->pins[0].direction = 1;
    newNode->pins[1].link = vect->pins[1].link;
    newNode->pins[1].direction = vect->pins[1].direction;
    
    newV = new TrackVectorSection[j];
    std::copy(vect->trVectorSection, vect->trVectorSection + j, newV);
    
    vect->iTrv = j;
    vect->pins[1].link = end1Id;
    vect->pins[1].direction = 1;
    
    
    ////////////////////
    this->trackNodes[end1Id] = new TRnode();
    newNode = this->trackNodes[end1Id];
    newNode->typ = 0;
    newNode->uid.worldTileX = vect->trVectorSection[j-1].worldTileX;
    newNode->uid.worldTileZ = vect->trVectorSection[j-1].worldTileZ;
    newNode->uid.worldObjectId = vect->trVectorSection[j-1].worldObjectId;
    newNode->uid.worldEndpointIndex = vect->trVectorSection[j-1].endEndpointIndex;
    newNode->uid.tileX = vect->trVectorSection[j].tileX;
    newNode->uid.tileZ = vect->trVectorSection[j].tileZ;
    newNode->uid.x = vect->trVectorSection[j].x;
    newNode->uid.y = vect->trVectorSection[j].y;
    newNode->uid.z = vect->trVectorSection[j].z;
    newNode->uid.ax = vect->trVectorSection[j].ax;
    newNode->uid.ay = vect->trVectorSection[j].ay;
    newNode->uid.az = vect->trVectorSection[j].az;

    newNode->inputPinCount = 1;
    newNode->pins[0].link = id;
    newNode->pins[0].direction = 0;
    /////////////////
    this->trackNodes[end2Id] = new TRnode();
    newNode = this->trackNodes[end2Id];
    newNode->typ = 0;
    newNode->uid.worldTileX = vect->trVectorSection[j].worldTileX;
    newNode->uid.worldTileZ = vect->trVectorSection[j].worldTileZ;
    newNode->uid.worldObjectId = vect->trVectorSection[j].worldObjectId;
    newNode->uid.worldEndpointIndex = vect->trVectorSection[j].startEndpointIndex;
    newNode->uid.tileX = vect->trVectorSection[j].tileX;
    newNode->uid.tileZ = vect->trVectorSection[j].tileZ;
    newNode->uid.x = vect->trVectorSection[j].x;
    newNode->uid.y = vect->trVectorSection[j].y;
    newNode->uid.z = vect->trVectorSection[j].z;
    newNode->uid.ax = vect->trVectorSection[j].ax;
    newNode->uid.ay = vect->trVectorSection[j].ay + M_PI;
    newNode->uid.az = vect->trVectorSection[j].az;

    newNode->inputPinCount = 1;
    newNode->pins[0].link = vecId;
    newNode->pins[0].direction = 1;

    delete[] vect->trVectorSection;
    vect->trVectorSection = newV;
    
    updateTrNode(id);
    updateTrNode(end1Id);
    updateTrNode(end2Id);
    updateTrNode(vecId);
    return vecId;
}

void TDB::deleteJunction(int id){
    TRnode* junction = trackNodes[id];
    if(junction->typ != 2) 
        return;
    
    int count = 0;
    int vecId = 0;
    for(int i = 0; i < 3; i++){
        qDebug() << junction->pins[i].link;
        if(junction->pins[i].link != 0) count++;
    }
    qDebug() << count;
    if(count > 1){
        qDebug() << "junction delete fail";
        return;
    }
    if(count == 0){
        delete trackNodes[id];
        trackNodes[id] = NULL;
        updateTrNode(id);
        return;
    }
    if(count == 1){
        vecId = 0;
        for(int i = 0; i < 3; i++){
            if(junction->pins[i].link != 0){
                junction->pins[0].link = junction->pins[i].link;
                junction->pins[0].direction = junction->pins[i].direction;
                vecId = junction->pins[i].link;
                break;
            }
        }
        TRnode* vect = trackNodes[vecId];
        
        if(!vect->isLikedTo(id)){
            qDebug() << "FAIL, TrackNode not linked to this juction!";
            trackNodes[id] = NULL;
            updateTrNode(id);
            return;
        }
        
        vect->setTrPinK(id, 1);

        if(junction->pins[0].direction == 1){
            junction->uid.worldObjectId = vect->trVectorSection[0].worldObjectId;
            junction->uid.worldEndpointIndex = vect->trVectorSection[0].startEndpointIndex;
        } else if(junction->pins[0].direction == 0){
            junction->uid.worldObjectId = vect->trVectorSection[vect->iTrv-1].worldObjectId;
            junction->uid.worldEndpointIndex = vect->trVectorSection[vect->iTrv-1].endEndpointIndex;
        }
        junction->uid.ay += M_PI;
        
        junction->typ = 0;
        junction->endNodeValue = junction->junction.unknown0;
        junction->inputPinCount = 1;
        junction->outputPinCount = 0;
        updateTrNode(vecId);
        updateTrNode(id);
    }
}

void TDB::deleteVectorSection(int id){
    TRnode* vect = trackNodes[id];
    const int end1Id = vect->pins[0].link;
    const int end2Id = vect->pins[1].link;
    TRnode* end1 = trackNodes[end1Id];
    TRnode* end2 = trackNodes[end2Id];
    
    deleteAllTrItemsFromVectorSection(id);
    
    delete trackNodes[id];
    trackNodes[id] = NULL;
    
    if(end1->typ == 0){
        delete trackNodes[end1Id];
        trackNodes[end1Id] = NULL;
    } else if (end1->typ == 2) {
        end1->podmienTrPin(id, 0);
        end1->setTrPinK(0, 0);
    } 
    
    if(end2->typ == 0){
        delete trackNodes[end2Id];
        trackNodes[end2Id] = NULL;
    } else if (end2->typ == 2) {
        end2->podmienTrPin(id, 0);
        end2->setTrPinK(0, 0);
    }
    
    updateTrNode(id);
    updateTrNode(end1Id);
    updateTrNode(end2Id);
    
}

bool TDB::deleteAllTrItemsFromVectorSection(int id){
    TRnode* vect = trackNodes[id];
    if(vect->iTri > 0){
        TRitem* trit;
        for(int i = 0; i < vect->iTri; i++){
            qDebug() << vect->trItemRef[i];
            trit = this->trackItems[vect->trItemRef[i]];
            if(trit == NULL){
                continue;
            }
            // item delete
            qDebug() << "item delete " << vect->trItemRef[i]<< " "<<trit->trItemId;
            deleteTrItem(trit->trItemId);
            i--;
        }
    }
    vect->iTri = 0;
    return true;
}

bool TDB::deleteFromVectorSection(int id, int j){
    TRnode* vect = trackNodes[id];
    if(vect->iTrv == 1){
        deleteVectorSection(id);
        return false;
    }
    int vid = -1;
    if(j > 0 && j < vect->iTrv - 1){
        vid = splitVectorSection(id, j);
        vect = trackNodes[vid];
        j = 0;
    }
    int endNId1 = vect->pins[0].link;
    int endNId2 = vect->pins[1].link;
    TRnode* end1 = trackNodes[vect->pins[0].link];
    TRnode* end2 = trackNodes[vect->pins[1].link];
    //deleteAllTrItemsFromVectorSection(id);
    TrackVectorSection *newV = new TrackVectorSection[vect->iTrv - 1];
    if(j == 0){
        // move & check items
        if(vect->iTri > 0){
            float sectDlugosc = this->tsection->sekcja[vect->trVectorSection[0].sectionIndex]->getDlugosc();
            TRitem* trit;
            for(int i = 0; i < vect->iTri; i++){
                trit = this->trackItems[vect->trItemRef[i]];
                if(trit == NULL) 
                    continue;
                trit->addToTrackPos(-sectDlugosc);
                if(trit->getTrackPosition() < 0){
                    qDebug() << "delete item? - before section";
                    // item delete
                    qDebug() << "item delete " << trit->trItemId;
                    this->deleteTrItem(trit->trItemId);
                    i--;
                    continue; // The reference array has changed; do not index i == -1.
                }
                updateTrItem(vect->trItemRef[i]);
            }
        }
        
        std::copy(vect->trVectorSection + 1, vect->trVectorSection + vect->iTrv, newV);
        
        if(end1->typ == 2){
            end1->podmienTrPin(id, 0);
            end1->setTrPinK(0, 0);
            updateTrNode(endNId1);
            
            endNId1 = getNextItrNode();//++this->iTRnodes;
            this->trackNodes[endNId1] = new TRnode();
            end1 = trackNodes[endNId1];
            end1->typ = 0;
            end1->inputPinCount = 1;
            end1->pins[0].link = id;
            end1->pins[0].direction = 1;
            vect->pins[0].link = endNId1;
            vect->pins[0].direction = 1;
        }
        
            end1->uid.worldTileX = vect->trVectorSection[1].worldTileX;
            end1->uid.worldTileZ = vect->trVectorSection[1].worldTileZ;
            end1->uid.worldObjectId = vect->trVectorSection[1].worldObjectId;
            end1->uid.worldEndpointIndex = vect->trVectorSection[1].startEndpointIndex;
            end1->uid.tileX = vect->trVectorSection[1].tileX;
            end1->uid.tileZ = vect->trVectorSection[1].tileZ;
            end1->uid.x = vect->trVectorSection[1].x;
            end1->uid.y = vect->trVectorSection[1].y;
            end1->uid.z = vect->trVectorSection[1].z;
            end1->uid.ax = vect->trVectorSection[1].ax;
            end1->uid.ay = vect->trVectorSection[1].ay + M_PI;
            end1->uid.az = vect->trVectorSection[1].az;
            updateTrNode(endNId1);
            
            
    } else if(j == vect->iTrv - 1) {
        // check items
        if(vect->iTri > 0){
            float vectDlugosc = this->getVectorSectionLengthToIdx(id, j);
            TRitem* trit;
            for(int i = 0; i < vect->iTri; i++){
                trit = this->trackItems[vect->trItemRef[i]];
                if(trit == NULL) 
                    continue;
                if(trit->getTrackPosition() > vectDlugosc){
                    qDebug() << "delete item? - behind section";
                    // item delete
                    qDebug() << "item delete " << trit->trItemId;
                    this->deleteTrItem(trit->trItemId);
                    i--;
                }
            }
        }
        
        std::copy(vect->trVectorSection , vect->trVectorSection + vect->iTrv - 1, newV);
        if(end2->typ == 2){
            end2->podmienTrPin(id, 0);
            end2->setTrPinK(0, 0);
            updateTrNode(endNId2);
            
            endNId2 = getNextItrNode();//++this->iTRnodes;
            this->trackNodes[endNId2] = new TRnode();
            end2 = trackNodes[endNId2];
            end2->typ = 0;
            end2->inputPinCount = 1;
            end2->pins[0].link = id;
            end2->pins[0].direction = 0;
            vect->pins[1].link = endNId2;
            vect->pins[1].direction = 1;
        }
            end2->uid.worldTileX = vect->trVectorSection[vect->iTrv-2].worldTileX;
            end2->uid.worldTileZ = vect->trVectorSection[vect->iTrv-2].worldTileZ;
            end2->uid.worldObjectId = vect->trVectorSection[vect->iTrv-2].worldObjectId;
            end2->uid.worldEndpointIndex = vect->trVectorSection[vect->iTrv-2].endEndpointIndex;
            end2->uid.tileX = vect->trVectorSection[vect->iTrv-1].tileX;
            end2->uid.tileZ = vect->trVectorSection[vect->iTrv-1].tileZ;
            end2->uid.x = vect->trVectorSection[vect->iTrv-1].x;
            end2->uid.y = vect->trVectorSection[vect->iTrv-1].y;
            end2->uid.z = vect->trVectorSection[vect->iTrv-1].z;
            end2->uid.ax = vect->trVectorSection[vect->iTrv-1].ax;
            end2->uid.ay = vect->trVectorSection[vect->iTrv-1].ay;
            end2->uid.az = vect->trVectorSection[vect->iTrv-1].az;
            updateTrNode(endNId2);
    }
    
    vect->iTrv -= 1;
    delete[] vect->trVectorSection;
    vect->trVectorSection = newV;
    updateTrNode(id);
    if(vid >= 0)
        updateTrNode(vid);
    
    return true;
}

int TDB::rotate(int id){
    auto vectorIt = trackNodes.find(id);
    if(vectorIt == trackNodes.end() || vectorIt->second == NULL)
        return -1;
    TRnode* vect = vectorIt->second;
    if(vect->typ != 1 || vect->iTrv <= 0
            || vect->trVectorSection == NULL)
        return -1;
    auto end1It = trackNodes.find(vect->pins[0].link);
    auto end2It = trackNodes.find(vect->pins[1].link);
    if(end1It == trackNodes.end() || end1It->second == NULL
            || end2It == trackNodes.end() || end2It->second == NULL)
        return -1;
    TRnode* e1 = end1It->second;
    TRnode* e2 = end2It->second;

    TrackVectorSection *reversed = new TrackVectorSection[vect->iTrv];
    for(int oldIndex = 0; oldIndex < vect->iTrv; oldIndex++){
        const TrackVectorSection &oldSection =
                vect->trVectorSection[oldIndex];
        TrackVectorSection &newSection =
                reversed[vect->iTrv - 1 - oldIndex];
        newSection = oldSection;

        // A TDB boundary is authoritative route data. Adjacent sections may
        // have been placed and rotated independently, or may contain legacy
        // rounding/errors which current TSection math cannot reproduce.
        // Reversal must therefore exchange stored starts, not regenerate
        // them from section geometry.
        if(oldIndex < vect->iTrv - 1){
            const auto &nextSection = vect->trVectorSection[oldIndex + 1];
            newSection.tileX = nextSection.tileX;
            newSection.tileZ = nextSection.tileZ;
            newSection.x = nextSection.x;
            newSection.y = nextSection.y;
            newSection.z = nextSection.z;
        } else {
            newSection.tileX = e2->uid.tileX;
            newSection.tileZ = e2->uid.tileZ;
            newSection.x = e2->uid.x;
            newSection.y = e2->uid.y;
            newSection.z = e2->uid.z;
        }

        auto sectionIt = tsection->sekcja.find((int)oldSection.sectionIndex);
        if(sectionIt == tsection->sekcja.end()
                || sectionIt->second == NULL){
            delete[] reversed;
            return -1;
        }
        const float angle = sectionIt->second->getAngle();
        if(angle > 0.0f)
            newSection.sectionIndex--;
        else if(angle < 0.0f)
            newSection.sectionIndex++;
        auto reverseSectionIt =
                tsection->sekcja.find((int)newSection.sectionIndex);
        if(reverseSectionIt == tsection->sekcja.end()
                || reverseSectionIt->second == NULL){
            delete[] reversed;
            return -1;
        }

        // Unlike the stored position, the frame belongs to this individual
        // section. Reverse its complete end tangent so independently rotated
        // neighbors do not lend it an unrelated yaw.
        std::array<float, 3> reversedFrame;
        advanceTdbFrame(oldSection.frame().data(),
                angle + (float)M_PI, reversedFrame.data());
        newSection.setFrame(reversedFrame);

        const auto oldEnd = newSection.startEndpointIndex;
        newSection.startEndpointIndex = newSection.endEndpointIndex;
        newSection.endEndpointIndex = oldEnd;
    }

    delete[] vect->trVectorSection;
    vect->trVectorSection = reversed;
    
    e1->setTrPinK(id, 0);
    e2->setTrPinK(id, 1);
    /*e1->uid.ay += M_PI;
    if(e1->uid.ay > 2*M_PI)
        e1->uid.ay -= 2*M_PI;
    e2->uid.ay += M_PI;
    if(e2->uid.ay > 2*M_PI)
        e2->uid.ay -= 2*M_PI;*/

    int tmp = vect->pins[0].link;
    vect->pins[0].link = vect->pins[1].link;
    vect->pins[1].link = tmp;
    tmp = vect->pins[0].direction;
    vect->pins[0].direction = vect->pins[1].direction;
    vect->pins[1].direction = tmp;
    
    // update items
    float d = getVectorSectionLength(id);
    qDebug() << "d: " << d;
    for(int i = 0; i < vect->iTri; i++){
        this->trackItems[vect->trItemRef[i]]->flipTrackPos(d);
    }
    return 0;
}

int TDB::appendToJunction(int junctionId, int eId, int idx){
    TRnode* junction = trackNodes[junctionId];

    if(idx == 1){
        if(junction->pins[idx].link != 0)
            idx++;
        if(junction->pins[idx].link != 0)
            return 0;
    } else {
        if(junction->pins[idx].link != 0)
            return 0;
    }
    
    TRnode* e1 = trackNodes[eId];
    int trackId = e1->pins[0].link;
    TRnode* track = trackNodes[trackId];
    trackNodes[eId] = NULL;
    
    junction->pins[idx].link = trackId;
    
    
    int j = track->podmienTrPin(eId, junctionId);
    //track->pins[0].link = junctionId;
    if(idx == 0) {
        track->pins[j].direction = 1;
        //junction->pins[idx].direction = 0;
        junction->pins[idx].direction = e1->pins[0].direction;
    } else {
        track->pins[j].direction = 0;
        //junction->pins[idx].direction = 1;
        junction->pins[idx].direction = e1->pins[0].direction;
    }
    updateTrNode(trackId);
    updateTrNode(junctionId);
    updateTrNode(eId);
    return 0;
}

void TDB::setDefaultEnd(int val){
    this->defaultEnd = val;
}

int TDB::getDefaultEnd() const{
    return this->defaultEnd;
}

void TDB::nextDefaultEnd(){
    this->defaultEnd++;
}

bool TDB::findPosition(int &x, int &z, float* p, float* q, float* endp, int sectionIdx){
    if(sectionIdx < 0) {
        return findPosition(x, z, p, q, endp, (TrackShape*)NULL);
    }
    return findPosition(x, z, p, q, endp, this->tsection->shape[sectionIdx]);
}

bool TDB::findPosition(int &x, int &z, float* p, float* q, float* endp, TrackShape* shp){
    float qe[3];
    qe[0] = 0;
    qe[1] = 0;
    qe[2] = 0;
    int findValue = findNearestNode(x, z, p, (float*) &qe);
    if(findValue < 0) return false;
    qDebug() << findValue;
    
    bool b;
    
    if(shp == NULL){
        Quat::fill(q);
        Quat::rotateY(q, q, -qe[1]);
        return true;
    }

    qDebug() << shp->filename;
    float startPos[3];
     
    while(defaultEnd >= shp->numpaths*2){
        defaultEnd -= shp->numpaths*2;
    }
    
    qDebug() << "defaultEnd" << defaultEnd;
    
    int startEnd = defaultEnd/2;
    int endend = defaultEnd - (startEnd)*2;
    
    Vector3f aa;
    Vector3f aa3;
    Vector3f bb;
    Vector3f aa2;
    
    if(endend == 1){
        float angle = -qe[1];
        float angle2 = 0;
        float dlugosc = 0;
        for (int i = shp->path[startEnd].n - 1; i >= 0; i--) {
            dlugosc = this->tsection->sekcja[shp->path[startEnd].sect[i]]->getDlugosc();
            this->tsection->sekcja[shp->path[startEnd].sect[i]]->getDrawPosition(&aa2, dlugosc);
            aa2.rotateY(angle, 0);
            aa.x+=aa2.x;
            aa.z+=aa2.z;
            angle += this->tsection->sekcja[shp->path[startEnd].sect[i]]->getAngle();
            this->tsection->sekcja[shp->path[startEnd].sect[i]]->getDrawPosition(&aa2, dlugosc);
            aa2.rotateY(angle2, 0);
            aa3.x+=aa2.x;
            aa3.z+=aa2.z;
            angle2 += this->tsection->sekcja[shp->path[startEnd].sect[i]]->getAngle();
        }
        endp[0] = -aa3.x;
        endp[1] = 0;
        endp[2] = -aa3.z;
        startPos[0] = aa.x;
        startPos[2] = aa.z;
        qe[1] -= angle + qe[1] - M_PI;
        endp[4] = -qe[1] + shp->path[startEnd].rotDeg*M_PI/180;
    } else {
        endp[0] = aa.x;
        endp[1] = 0;
        endp[2] = aa.z;
        endp[4] = 0;
        startPos[0] = aa.x;
        startPos[2] = aa.z;
    }
    
    bb.x = shp->path[0].pos[0];
    bb.z = shp->path[0].pos[2];
    bb.rotateY(-qe[1], 0);
    aa.set(0 - shp->path[startEnd].pos[0], shp->path[0].pos[1], 0 - shp->path[startEnd].pos[2]);
    aa.rotateY(shp->path[startEnd].rotDeg*M_PI/180, 0);
    aa.x += shp->path[0].pos[0];
    aa.z += shp->path[0].pos[2];
    aa.rotateY(-qe[1], 0);
    p[0] = p[0] + aa.x + startPos[0];
    p[1] = p[1] - shp->path[0].pos[1];
    p[2] = p[2] - aa.z - startPos[2];
    p[0] -= bb.x;
    p[2] += bb.z;
    
    Quat::fill(q);
    Quat::rotateY(q, q, -qe[1] + shp->path[startEnd].rotDeg*M_PI/180);
    
    if(endend == 0)
        endp[3] = 1;
    if(endend == 1)
        endp[3] = -1;
    
    qDebug() << "ccc";
    qDebug() << startPos[0] << " " << startPos[2];
    
    return true;
}

bool TDB::fillJNodePosn(int x, int z, int uid, QVector<std::array<float, 5>> *jNodePosn){
    if(jNodePosn == NULL)
        return false;
    jNodePosn->clear();
    z = -z;
    qDebug() << "fill jnodeposn " << x << " " << z << " " << uid; 
    
    TRnode *n;
    int count = 0;
    for (int i = 1; i <= iTRnodes; i++) {
            n = trackNodes[i];
            if (n == NULL) continue;
            if (n ->typ == -1) continue;
            if (n->typ == 2) {
                if(n->uid.worldTileX == x)
                    if(n->uid.worldTileZ == z)
                        if(n->uid.worldObjectId == uid){
                            qDebug() << "jest j";
                            count++;
                            jNodePosn->push_back(std::array<float,5>());
                            jNodePosn->back()[0] = n->uid.worldTileX;
                            jNodePosn->back()[1] = n->uid.worldTileZ;
                            jNodePosn->back()[2] = n->uid.x;
                            jNodePosn->back()[3] = n->uid.y;
                            jNodePosn->back()[4] = n->uid.z;
                }
            }
        }
    if(count > 0) return true;
    return false;
}

bool TDB::placeTrack(int x, int z, float* p, float* q, int sectionIdx, int uid, QVector<std::array<float, 5>> *jNodePosn) {
    if(sectionIdx < 0)
        return placeTrack(x, z, p, q, (TrackShape*)NULL, uid, sectionIdx, jNodePosn);
    return placeTrack(x, z, p, q, this->tsection->shape[sectionIdx], uid, sectionIdx, jNodePosn);
}

bool TDB::placeTrack(int x, int z, float* p, float* q, TrackShape* shp, int uid, int shapeIdForTdb, QVector<std::array<float, 5>> *jNodePosn) {
    float qe[4];
    float vect[3];
    vect[0] = 0; vect[1] = 0; vect [2] = 10;
    Vec3::transformQuat(vect, vect, q);
    
    float sinv = 2*(q[0]*q[2] - q[1]*q[3]);
    if(sinv > 1.0f)
        sinv = 1.0f;
    if(sinv < -1.0f)
        sinv = -1.0f;
    float pitch = asin(sinv);
    
    if(vect[2] < 0)
        pitch = M_PI - pitch;
    if(vect[2] == 0 && vect[0] < 0)
        pitch = M_PI/2;
    if(vect[2] == 0 && vect[0] > 0)
        pitch = -M_PI/2;

    sinv = (vect[1]/10.0);
    if(sinv > 1.0f)
        sinv = 1.0f;
    if(sinv < -1.0f)
        sinv = -1.0f;
    qe[0] = asin(sinv);
    qe[1] = pitch;
    qe[2] = 0;
    
    if(shp == NULL)
        return false;
    qDebug() << shp->filename;
    float pp[3];
    float qee[3];
    int endp;
    Vector3f aa;
    int start;
    int ends[2];
    
    ////////////////////////////////
    
    int *endsNumbres = new int[shp->numpaths*2];
    std::unordered_map<int, int> endsIds;
    std::unordered_map<int, int> isJunction;
    std::unordered_map<int, int> junctionId;
    int nextNumber = 0;
    
    for (int i = 0; i < shp->numpaths; i++) {
        int posIdx = (float)shp->path[i].pos[0]*100000 + (float)shp->path[i].pos[1]*1000 + (float)shp->path[i].pos[2]*10;
        std::unordered_map<int, int>::iterator iter = endsIds.find(posIdx);
        if(iter == endsIds.end()){
            endsNumbres[i*2] = nextNumber++;
            endsIds[posIdx] = endsNumbres[i*2];
            isJunction[endsNumbres[i*2]] = 0;
        } else {
            endsNumbres[i*2] = endsIds[posIdx];
            isJunction[endsNumbres[i*2]] = 1;
            //qDebug() << "rozjazd";
            //junctions[junctionCount++] = newJunction(x, z, pp, qee, r->value, uid, ends[0]);
        }
        junctionId[endsNumbres[i*2]] = 0;
        endsNumbres[i*2+1] = nextNumber++;
        //qDebug() << "ends: "<< ends[0] <<" "<<ends[1];
    }
    
    ////////////////////////////////

    for (int i = 0; i < shp->numpaths; i++) {
        aa.set(shp->path[i].pos[0], -shp->path[i].pos[1], shp->path[i].pos[2]);
        aa.rotateX(-qe[0], 0);
        aa.rotateY(-qe[1], 0);

        pp[0] = p[0] + aa.x;
        pp[1] = p[1] - aa.y;
        pp[2] = p[2] - aa.z;
        float rootFrame[3] = {qe[0], qe[1], qe[2]};
        const float pathAngle = shp->path[i].rotDeg * (float)M_PI / 180.0f;
        if(pathAngle == 0.0f) {
            qee[0] = rootFrame[0];
            qee[1] = rootFrame[1];
            qee[2] = rootFrame[2];
        } else {
            advanceTdbFrame(rootFrame, pathAngle, qee);
        }
        
        ends[0] = endsNumbres[i*2];
        ends[1] = endsNumbres[i*2+1];
        
        if(isJunction[ends[0]] == 1){
            isJunction[ends[0]] = 0;
            qDebug() << "rozjazd" << jNodePosn;
            if(jNodePosn != NULL){
                jNodePosn->push_back(std::array<float,5>());
                jNodePosn->back()[0] = x;
                jNodePosn->back()[1] = -z;
                jNodePosn->back()[2] = pp[0];
                jNodePosn->back()[3] = pp[1];
                jNodePosn->back()[4] = -pp[2];
            }
            junctionId[ends[0]] = newJunction(x, z, pp, qee, shapeIdForTdb, uid, ends[0]);
        }

        endp = newTrack(x, z, pp, qee, (int*)ends,
                shapeIdForTdb, shp->path[i].sect[0], uid, &start);

        for (int j = 1; j < shp->path[i].n; j++) {
            if (endp > 0) {
                endp = appendTrack(endp, (int*)ends,
                        shapeIdForTdb, shp->path[i].sect[j], uid);
            }
        }
        
        if(endp <= 0 || start <= 0)
            continue;

        if(junctionId[ends[0]] != 0){
            qDebug() << "append to junction";
            appendToJunction(junctionId[ends[0]], start, 1);
            joinTracks(junctionId[ends[0]]);
        } else {
            joinTracks(start);
        }

        joinTracks(endp);
    }
    
    ////////////////////////////////
    
    if(shp->crossovershape){
        QVector<TDB::IntersectionPoint> cPoints;
        float posT[2];
        Vec2::set(posT, x, z);
        Vector3f cPos;
        for(int i = 0; i < shp->xoverpts; i++){
            cPos.set((float*)&shp->xoverpt[i*3]);
            cPos.rotateY(-qe[1], 0);
            cPos.z = - cPos.z;
            cPos.add(p);
            
            this->findNearestPositionsOnTDB(posT, (float*)&cPos, cPoints, 0.2);
            qDebug() << "crossover";
            qDebug() << cPos.x<<cPos.y<<cPos.z;
            if(cPoints.size() != 2)
                continue;
            //qDebug() << cPoints.size();
            qDebug() << cPoints[0].idx << cPoints[0].m << cPoints[1].idx << cPoints[1].m;
            this->newCrossOverObject(cPoints[0].idx, cPoints[0].m, cPoints[1].idx, cPoints[1].m, shapeIdForTdb);
        }
    }

    refresh();
    return true;
}

bool TDB::removeTrackFromTDB(int x, int y, int UiD){
    y = -y;
    qDebug() << "usune Track " << x << " " << y << " " << UiD; 
    
    bool ok = false;
    TRnode *n;
    for (int i = 1; i <= iTRnodes; i++) {
            n = trackNodes[i];
            if (n == NULL) continue;
            if (n ->typ == -1) continue;
            if (n->typ == 1) {
                for(int j = 0; j < n->iTrv; j++)
                    if(n->trVectorSection[j].worldTileX == x)
                        if(n->trVectorSection[j].worldTileZ == y)
                            if(n->trVectorSection[j].worldObjectId == UiD){
                                ok = true;
                                qDebug() << "jest";
                                if(deleteFromVectorSection(i, j))
                                    j = -1;
                                else
                                    break; // The vector and local n have been deleted.
                    }
            }
        }
    for (int i = 1; i <= iTRnodes; i++) {
            n = trackNodes[i];
            if (n == NULL) continue;
            if (n ->typ == -1) continue;
            if (n->typ == 2) {
                if(n->uid.worldTileX == x)
                    if(n->uid.worldTileZ == y)
                        if(n->uid.worldObjectId == UiD){
                            ok = true;
                            qDebug() << "jest j";
                            deleteJunction(i);
                }
            }
        }
    
    if(ok)
        TDB::refresh();
    return ok;
}

void TDB::fillTrackAngles(int x, int z, int UiD, QMap<int, float>& angles){
    TRnode *n;
    for (int i = 1; i <= iTRnodes; i++) {
            n = trackNodes[i];
            if (n == NULL) continue;
            if (n ->typ == -1) continue;
            if (n->typ == 1) {
                for(int j = 0; j < n->iTrv; j++)
                    if(n->trVectorSection[j].worldTileX == x)
                        if(n->trVectorSection[j].worldTileZ == z)
                            if(n->trVectorSection[j].worldObjectId == UiD){
                                int endp1 = n->trVectorSection[j].startEndpointIndex;
                                int endp2 = n->trVectorSection[j].endEndpointIndex;
                                angles[endp1] = fabs(n->trVectorSection[j].az);
                                if(j < n->iTrv - 1)
                                    angles[endp2] = fabs(n->trVectorSection[j+1].az);
                                else
                                    angles[endp2] = 0;
                            
                    }
            }
        }
}

bool TDB::ifTrackExist(int x, int y, int UiD){
    y = -y;
    //qDebug() << "is Track? " << x << " " << y << " " << UiD; 
    
    TRnode *n;
    for (int i = 1; i <= iTRnodes; i++) {
            n = trackNodes[i];
            if (n == NULL) continue;
            if (n ->typ == -1) continue;
            if (n->typ == 1) {
                for(int j = 0; j < n->iTrv; j++)
                    if(n->trVectorSection[j].worldTileX == x)
                        if(n->trVectorSection[j].worldTileZ == y)
                            if(n->trVectorSection[j].worldObjectId == UiD){
                                qDebug() << "jest";
                                return true;
                    }
            }
        }
    return false;
}

int TDB::getNextItrNode(){
    return ++this->iTRnodes;
}

void TDB::refresh() {
    isInitSectLines = false;
    isInitLines = false;
    isInitTrItemsDraw = false;
    collisionLineHash = 0;
}

void TDB::pushRenderAll(RenderQueue &queue, float* playerT, float playerRot) {

    if (!loaded) return;
    int hash = (int)playerT[0] * 10000 + (int)playerT[1];
    if (!isInitLines || lineHash != hash) {
        TRnode *n;
        int lLen = 0, kLen = 0, pLen = 0;
        lineHash = hash;
        isInitLines = true;

        for (auto it = endIdObj.begin(); it != endIdObj.end(); ++it) {
            TextObj* obj = (TextObj*) it->second;
            obj->inUse = false;
        }
        for (auto it = junctIdObj.begin(); it != junctIdObj.end(); ++it) {
            TextObj* obj = (TextObj*) it->second;
            obj->inUse = false;
        }

        for (int i = 1; i <= iTRnodes; i++) {
            n = trackNodes[i];
            if (n == NULL) continue;
            if (n->typ == -1) continue;
            if (n->typ == 1) {
                if (n->iTrv < 1) continue;
                lLen += 6 * (n->iTrv - 1);
                if (n->pins[1].link != 0)
                    lLen += 6;
            } else if (n->typ == 0) {
                kLen += 6;
            } else if (n->typ == 2) {
                pLen += 6;
            }
        }
        float* linie = new float[lLen];
        float* konce = new float[kLen];
        float* punkty = new float[pLen];
        int lPtr = 0, kPtr = 0, pPtr = 0;

        for (int i = 1; i <= iTRnodes; i++) {
            n = trackNodes[i];
            if (n == NULL) continue;
            if (n->typ == -1) continue;
            if (n->typ == 1) {
                if (n->iTrv < 1) continue;
                for (int i = 0; i < n->iTrv - 1; i++) {
                    linie[lPtr++] = ((n->trVectorSection[i].tileX - playerT[0])*2048 + n->trVectorSection[i].x);
                    linie[lPtr++] = (n->trVectorSection[i].y + wysokoscSieci);
                    linie[lPtr++] = (((-n->trVectorSection[i].tileZ - playerT[1])*2048 - n->trVectorSection[i].z));

                    linie[lPtr++] = ((n->trVectorSection[i + 1].tileX - playerT[0])*2048 + n->trVectorSection[i + 1].x);
                    linie[lPtr++] = (n->trVectorSection[i + 1].y + wysokoscSieci);
                    linie[lPtr++] = (((-n->trVectorSection[i + 1].tileZ - playerT[1])*2048 - n->trVectorSection[i + 1].z));
                }
                if (n->pins[1].link != 0) {
                    linie[lPtr++] = ((n->trVectorSection[n->iTrv - 1].tileX - playerT[0])*2048 + n->trVectorSection[n->iTrv - 1].x);
                    linie[lPtr++] = (n->trVectorSection[n->iTrv - 1].y + wysokoscSieci);
                    linie[lPtr++] = (((-n->trVectorSection[n->iTrv - 1].tileZ - playerT[1])*2048 - n->trVectorSection[n->iTrv - 1].z));

                    linie[lPtr++] = ((trackNodes[n->pins[1].link]->uid.tileX - playerT[0])*2048 + trackNodes[n->pins[1].link]->uid.x);
                    linie[lPtr++] = (trackNodes[n->pins[1].link]->uid.y + wysokoscSieci);
                    linie[lPtr++] = (((-trackNodes[n->pins[1].link]->uid.tileZ - playerT[1])*2048 - trackNodes[n->pins[1].link]->uid.z));
                }
            } else if (n->typ == 0) {
                konce[kPtr++] = ((n->uid.tileX - playerT[0])*2048 + n->uid.x);
                konce[kPtr++] = (n->uid.y);
                konce[kPtr++] = ((-n->uid.tileZ - playerT[1])*2048 - n->uid.z);

                konce[kPtr++] = ((n->uid.tileX - playerT[0])*2048 + n->uid.x);
                konce[kPtr++] = (n->uid.y + wysokoscSieci);
                konce[kPtr++] = ((-n->uid.tileZ - playerT[1])*2048 - n->uid.z);

                if (fabs(n->uid.tileX - playerT[0]) > 1) continue;
                if (fabs(-n->uid.tileZ - playerT[1]) > 1) continue;

                if (!road) {
                    if (endIdObj[i] == NULL) {
                        endIdObj[i] = new TextObj(i);
                        endIdObj[i]->setColor(50, 50, 255);
                    }
                    endIdObj[i]->inUse = true;
                    endIdObj[i]->pos[0] = ((n->uid.tileX - playerT[0])*2048 + n->uid.x);
                    endIdObj[i]->pos[1] = n->uid.y + wysokoscSieci;
                    endIdObj[i]->pos[2] = ((-n->uid.tileZ - playerT[1])*2048 - n->uid.z);
                }
            } else if (n->typ == 2) {
                punkty[pPtr++] = ((n->uid.tileX - playerT[0])*2048 + n->uid.x);
                punkty[pPtr++] = (n->uid.y);
                punkty[pPtr++] = ((-n->uid.tileZ - playerT[1])*2048 - n->uid.z);

                punkty[pPtr++] = ((n->uid.tileX - playerT[0])*2048 + n->uid.x);
                punkty[pPtr++] = (n->uid.y + wysokoscSieci);
                punkty[pPtr++] = ((-n->uid.tileZ - playerT[1])*2048 - n->uid.z);

                if (fabs(n->uid.tileX - playerT[0]) > 1) continue;
                if (fabs(-n->uid.tileZ - playerT[1]) > 1) continue;

                if (!road) {
                    if (junctIdObj[i] == NULL) {
                        junctIdObj[i] = new TextObj(i);
                        junctIdObj[i]->setColor(255, 50, 50);
                    }
                    junctIdObj[i]->inUse = true;
                    junctIdObj[i]->pos[0] = ((n->uid.tileX - playerT[0])*2048 + n->uid.x);
                    junctIdObj[i]->pos[1] = n->uid.y + wysokoscSieci;
                    junctIdObj[i]->pos[2] = ((-n->uid.tileZ - playerT[1])*2048 - n->uid.z);
                }
            }
        }
        linieSieci.setMaterial(0.5, 0.5, 0.5);
        linieSieci.init(linie, lPtr, RenderItem::V, GL_LINES);

        konceSieci.setMaterial(0.0, 0.0, 1.0);
        konceSieci.init(konce, kPtr, RenderItem::V, GL_LINES);

        punktySieci.setMaterial(1.0, 0.0, 0.0);
        punktySieci.init(punkty, pPtr, RenderItem::V, GL_LINES);

        delete[] linie;
        delete[] konce;
        delete[] punkty;
    }

    linieSieci.pushRenderItem(queue);
    konceSieci.pushRenderItem(queue);
    punktySieci.pushRenderItem(queue);

    if (!road) {
        for (auto it = endIdObj.begin(); it != endIdObj.end(); ++it) {
            TextObj* obj = (TextObj*) it->second;
            if (obj->inUse) obj->pushRenderItem(queue, playerRot);
        }
        for (auto it = junctIdObj.begin(); it != junctIdObj.end(); ++it) {
            TextObj* obj = (TextObj*) it->second;
            if (obj->inUse) obj->pushRenderItem(queue, playerRot);
        }
    }
}

void TDB::getLines(float * &lineBuffer, int &length, float* playerT){
    if (!loaded) return;
    int hash = (int)playerT[0] * 10000 + (int)playerT[1];
    if (collisionLineHash == hash && this->collisionLineBuffer != NULL){
        length = this->collisionLineLength;
        lineBuffer = this->collisionLineBuffer;
        return;
    }
    Vector3f p;
    Vector3f o;
    collisionLineHash = hash;
    int len = 0;

    for (int j = 1; j <= iTRnodes; j++) {
        TRnode* n = trackNodes[j];
        if (n == NULL) continue;
        if (n->typ == -1) continue;
        if (n->typ == 1) {
            for (int i = 0; i < n->iTrv; i++) {
                if (fabs(n->trVectorSection[i].tileX - playerT[0]) > 1 || fabs(-n->trVectorSection[i].tileZ - playerT[1]) > 1) continue;
                len += getLineBufferSize((int) n->trVectorSection[i].sectionIndex, 6, 0);
            }
        }
    }
    //qDebug() << "len" << len;
    this->collisionLineBuffer = new float[len];
    float* ptr = this->collisionLineBuffer;

    for (int j = 1; j <= iTRnodes; j++) {
        TRnode* n = trackNodes[j];
        if (n == NULL) continue;
        if (n->typ == -1) continue;
        if (n->typ == 1) {
            for (int i = 0; i < n->iTrv; i++) {
                if (fabs(n->trVectorSection[i].tileX - playerT[0]) > 1 || fabs(-n->trVectorSection[i].tileZ - playerT[1]) > 1) continue;
                p.set(
                        (n->trVectorSection[i].tileX - playerT[0])*2048 + n->trVectorSection[i].x,
                        n->trVectorSection[i].y,
                        (-n->trVectorSection[i].tileZ - playerT[1])*2048 - n->trVectorSection[i].z
                        );
                o.set(
                        n->trVectorSection[i].ax,
                        n->trVectorSection[i].ay,
                        n->trVectorSection[i].az
                        );
                getLine(ptr, p, o, (int) n->trVectorSection[i].sectionIndex, j, i);
            }
        }
    }
    this->collisionLineLength = (ptr - this->collisionLineBuffer)/12;
    length = this->collisionLineLength;
    lineBuffer = this->collisionLineBuffer;
}

void TDB::getVectorSectionLine(float * &buffer, int &len, int x, int y, int uid, bool useOffset){
    if (!loaded) return;

    Vector3f p;
    Vector3f o;
    len = 0;

    TRnode* n = trackNodes[uid];
    if (n == NULL) return;
        
    for (int i = 0; i < n->iTrv; i++) {
        len += getLineBufferSize((int) n->trVectorSection[i].sectionIndex, 6, 0, 1);
    }
    //qDebug() << "len" << len;
    buffer = new float[len]; 
    float* ptr = buffer;
    
    float offset = 0;
    float dlugosc = 0;
    for (int i = 0; i < n->iTrv; i++) {
        p.set(
            (n->trVectorSection[i].tileX - x)*2048 + n->trVectorSection[i].x,
            n->trVectorSection[i].y,
            (-n->trVectorSection[i].tileZ - y)*2048 - n->trVectorSection[i].z
            );
        o.set(
            n->trVectorSection[i].ax,
            n->trVectorSection[i].ay,
            n->trVectorSection[i].az
        );

        if(useOffset)
            offset = dlugosc;
        getLine(ptr, p, o, (int) n->trVectorSection[i].sectionIndex, uid, i, offset, 2);
        if(tsection->sekcja[n->trVectorSection[i].sectionIndex] != NULL)
            dlugosc += tsection->sekcja[n->trVectorSection[i].sectionIndex]->getDlugosc();
    }
    len = ptr - buffer;
    //qDebug() << "len" << len;
}

void TDB::pushRenderLines(RenderQueue &queue, float* playerT, float playerRot) {

    if (!loaded) return;
    int hash = (int)playerT[0] * 10000 + (int)playerT[1];
    if (!sectionLines.loaded || sectionHash != hash || !isInitSectLines) {
        Vector3f p;
        Vector3f o;
        sectionHash = hash;
        isInitSectLines = true;

        int len = 0;
        int tileRadius = 1;
        int hOffset = 0;
        for (int j = 1; j <= iTRnodes; j++) {
            TRnode* n = trackNodes[j];
            if (n == NULL) continue;
            if (n->typ == -1) continue;
            if (n->typ == 1) {
                for (int i = 0; i < n->iTrv; i++) {
                    if (fabs(n->trVectorSection[i].tileX - playerT[0]) > tileRadius || fabs(-n->trVectorSection[i].tileZ - playerT[1]) > tileRadius) continue;
                    len += getLineBufferSize((int) n->trVectorSection[i].sectionIndex, 3, 6);
                }
            }
        }
        float* punkty = new float[len];
        float* ptr = punkty;
        for (int j = 1; j <= iTRnodes; j++) {
            TRnode* n = trackNodes[j];
            if (n == NULL) continue;
            if (n->typ == -1) continue;
            if (n->typ == 1) {
                for (int i = 0; i < n->iTrv; i++) {
                    if (fabs(n->trVectorSection[i].tileX - playerT[0]) > tileRadius || fabs(-n->trVectorSection[i].tileZ - playerT[1]) > tileRadius)
                        continue;
                    p.set(
                            (n->trVectorSection[i].tileX - playerT[0])*2048 + n->trVectorSection[i].x,
                            n->trVectorSection[i].y + hOffset,
                            (-n->trVectorSection[i].tileZ - playerT[1])*2048 - n->trVectorSection[i].z
                            );
                    o.set(
                            n->trVectorSection[i].ax,
                            n->trVectorSection[i].ay,
                            n->trVectorSection[i].az
                            );
                    drawLine(NULL, ptr, p, o, (int) n->trVectorSection[i].sectionIndex);
                }
            }
        }
        if (road)
            sectionLines.setMaterial(0.0, 0.0, 1.0);
        else
            sectionLines.setMaterial(1.0, 1.0, 0.0);
        sectionLines.init(punkty, ptr - punkty, RenderItem::V, GL_LINES);
        delete[] punkty;
    }

    sectionLines.pushRenderItem(queue);
}

bool TDB::getDrawPositionOnTrNode(float* out, int id, float metry, float *sElev){
    TRnode* n = trackNodes[id];
    if (n == NULL) 
        return false;
    if (n->typ != 1) 
        return false;
    
    float sectionLength = 0;
    float length = 0;
    int idx = 0;
    Vector3f position;
    for (int i = 0; i < n->iTrv; i++) {
        idx = n->trVectorSection[i].sectionIndex;
        
        if(tsection->sekcja[idx] == NULL){
            //qDebug() << "nie ma sekcji " << idx;
            return false;
        } else {
            sectionLength = tsection->sekcja[idx]->getDlugosc();
        }
        length += sectionLength;
        if(length < metry)
            continue;
        
        float sDistance = metry - length + sectionLength;
        tsection->sekcja.at(idx)->getDrawPosition(&position, sDistance);
        if(n->trVectorSection[i].az != 0.0f)
            position.rotateZ(n->trVectorSection[i].az, 0.0f);
        //qDebug() << "position"<<position.x<<position.y<<position.z;

        float matrix[16];
        float q[4];
        q[0] = q[1] = q[2] = 0; q[3] = 1;
        float rot[3];
        rot[0] = M_PI;
        rot[1] = n->trVectorSection[i].ay;
        rot[2] = 0;//n->trVectorSection[i].az;

        float pos[3];
        pos[0] = n->trVectorSection[i].x;
        pos[1] = n->trVectorSection[i].y;
        pos[2] = n->trVectorSection[i].z;
        
        Quat::fromRotationXYZ(q, rot);
        Mat4::fromRotationTranslation(matrix, q, pos);
        Mat4::rotate(matrix, matrix, -n->trVectorSection[i].ax, 1, 0, 0);

        pos[0] = position.x;
        pos[1] = position.y;
        pos[2] = -position.z;
        Vec3::transformMat4(pos, pos, matrix);

        out[3] = -n->trVectorSection[i].ay - tsection->sekcja.at(idx)->getDrawAngle(sDistance);
        out[4] = n->trVectorSection[i].ax;
        out[5] = n->trVectorSection[i].tileX;
        out[6] = n->trVectorSection[i].tileZ;
        out[0] = pos[0];
        out[1] = pos[1];
        out[2] = pos[2];        
        //position->x += (n->trVectorSection[i].tileX - playerT[0])*2048 + n->trVectorSection[i].x;
        //position->y += n->trVectorSection[i].y;
        //position->z += (-n->trVectorSection[i].tileZ - playerT[1])*2048 - n->trVectorSection[i].z;
        
        if(sElev != NULL)
            if(Game::useSuperelevation){
                if(i < n->iTrv - 1)
                    *sElev = -(n->trVectorSection[i].az*(1.0 - sDistance/sectionLength) + n->trVectorSection[i+1].az*(sDistance/sectionLength));
                else
                    *sElev = -(n->trVectorSection[i].az*(1.0 - sDistance/sectionLength));
            } else {
                *sElev = 0;
            }

    return true;
}
    return false;
}

int TDB::findTrItemNodeId(int id){
    for (int j = 1; j <= iTRnodes; j++) {
        TRnode* n = trackNodes[j];
        if (n == NULL) continue;
        if (n->typ == 1) {
            for (int i = 0; i < n->iTri; i++) {
                if(n->trItemRef[i] == id)
                    return j;
            }
        }
    }
    return -1;
}

int TDB::findTrItemNodeIds(int id, QVector<int>& ids){
    for (int j = 1; j <= iTRnodes; j++) {
        TRnode* n = trackNodes[j];
        if (n == NULL) continue;
        if (n->typ == 1) {
            for (int i = 0; i < n->iTri; i++) {
                if(n->trItemRef[i] == id)
                    ids.push_back(j);
            }
        }
    }
    if(ids.size() == 0)
        return -1;
    return ids[0];
}

int TDB::getEndpointType(int trid, int endp){
    TRnode* n = this->trackNodes[trid];
    if(n->typ !=1 )
        return -1;
    n = this->trackNodes[n->pins[endp].link];
    if(n == NULL) return -1;
    return n->typ;
}

void TDB::pushRenderItems(RenderQueue &queue, float* playerT, float playerRot, int renderMode) {

    quint32 selectionId = 0;
    for (auto it = this->trackItems.begin(); it != this->trackItems.end(); ++it) {
        TRitem* obj = (TRitem*) it->second;
        if (obj != NULL) {
            if (!isInitTrItemsDraw)
                obj->refresh();
            if (renderMode == RenderQueue::RENDER_SELECTION) {
                selectionId = SelectionIdCodec::databaseItem(
                            this->road
                            ? SelectionIdCodec::DatabaseKind::Road
                            : SelectionIdCodec::DatabaseKind::Track,
                            obj->trItemId);
            }
            obj->pushRenderItem(queue, this, playerT, playerRot, selectionId);
        }
    }
    isInitTrItemsDraw = true;
}

int TDB::getLineBufferSize(int idx, int pointSize, int offset, int step) {
    if(tsection->sekcja[idx] == NULL)
        return offset;
    
    return tsection->sekcja[idx]->getLineBufferSize(pointSize, step) + offset + 6;
}

bool TDB::isRoad(){
    return this->road;
}

void TDB::getLine(float* &ptr, Vector3f p, Vector3f o, int idx, int id, int vid, float offset, int step) {

    float matrix[16];
    float q[4];
    q[0] = q[1] = q[2] = 0;
    q[3] = 1;
    float rot[3];
    rot[0] = M_PI;
    rot[1] = -o.y;
    rot[2] = 0;//o.z;

    Quat::fromRotationXYZ(q, rot);
    Mat4::fromRotationTranslation(matrix, q, reinterpret_cast<float *> (&p));
    Mat4::rotate(matrix, matrix, o.x, 1, 0, 0);
    if(o.z != 0.0f)
        Mat4::rotate(matrix, matrix, -o.z, 0, 0, 1);

    if(tsection->sekcja[idx] != NULL){
        tsection->sekcja[idx]->drawSection(ptr, matrix, 0, id, vid, offset, step);
    }
}

void TDB::drawLine(GLUU *gluu, float* &ptr, Vector3f p, Vector3f o, int idx) {

    float matrix[16];
    float q[4];
    q[0] = q[1] = q[2] = 0; q[3] = 1;
    float rot[3];
    rot[0] = M_PI;
    rot[1] = -o.y;
    rot[2] = 0;

    Quat::fromRotationXYZ(q, rot);
    Mat4::fromRotationTranslation(matrix, q, reinterpret_cast<float *> (&p));
    //Mat4::rotate(matrix, matrix, -o.y+M_PI, 0, 1, 0);
    Mat4::rotate(matrix, matrix, o.x, 1, 0, 0);
    if(o.z != 0.0f)
        Mat4::rotate(matrix, matrix, -o.z, 0, 0, 1);
    
    float point1[3];
    point1[0] = 0;
    point1[1] = 0;
    point1[2] = 0;
    float point2[3];
    point2[0] = 0;
    point2[1] = 2;
    point2[2] = 0;
    Vec3::transformMat4(point1, point1, matrix);
    Vec3::transformMat4(point2, point2, matrix);
    *ptr++ = point1[0];
    *ptr++ = point1[1];
    *ptr++ = point1[2];
    *ptr++ = point2[0];
    *ptr++ = point2[1];
    *ptr++ = point2[2];

    if(tsection->sekcja[idx] != NULL){
        tsection->sekcja[idx]->drawSection(ptr, matrix, 2, -1, 0, 0, 0);
    }
}

int TDB::findNearestPositionOnTDB(float* posT, float* pos, float * q, float* tpos){
    float *lineBuffer;
    int length = 0;
    getLines(lineBuffer, length, posT);
    
    //qDebug() << "lines length" << length;

    //qDebug() << ": " << posT[0]<<" "<<posT[1]<<" "<<pos[0]<<" "<<pos[1]<<" "<<pos[2];
    float best[7];
    best[0] = 99999;
    float dist = 0;
    float intersectionPoint[3];
    int uu;
    for(int i = 0; i < length*12; i+=12){
        //qDebug() << i/12;
        //qDebug() << lineBuffer[i+0] << " "<< lineBuffer[i+1] << " " << lineBuffer[i+2] << " "<< lineBuffer[i+3] << " "<< lineBuffer[i+4] << " " << lineBuffer[i+5] ;
        //qDebug() << lineBuffer[i+6] << " "<< lineBuffer[i+7] << " " << lineBuffer[i+8] << " "<< lineBuffer[i+9] << " "<< lineBuffer[i+10] << " " << lineBuffer[i+11] ;
        dist = Intersections::pointSegmentDistance(lineBuffer + i, lineBuffer + i+6, pos, (float*)&intersectionPoint);
        if(dist < best[0]){
            best[0] = dist;
            best[1] = lineBuffer[i+3];
            best[2] = lineBuffer[i+4];

            float dist1 = Vec3::distance(lineBuffer + i, lineBuffer + i+6);
            float dist2 = Vec3::distance(lineBuffer + i, intersectionPoint);
            dist1 = dist2/dist1;
            best[3] = lineBuffer[i+5] + (lineBuffer[i+11] - lineBuffer[i+5])*dist1;
            //best[3] = intersectionPoint[0];
            //best[4] = intersectionPoint[1];
            //best[5] = intersectionPoint[2];
        }
    }
    //qDebug() << "item pos: " << best[0] << " " << best[1] << " " << best[2] << " " << best[3];
    float minDistance = best[0];
    //if(best[0] == 99999 )
    //    return false;
    if(best[0] >= 99999)
        return -1;
    //TRnode* n = trackNodes[(int)best[1]];
    //posT[0] = n->trVectorSection[(int)best[2]].tileX;
    //posT[1] = -n->trVectorSection[(int)best[2]].tileZ;
    

    
    float metry = this->getVectorSectionLengthToIdx(best[1], best[2]);
    if(tpos != NULL){
        tpos[0] = best[1];
        tpos[1] = metry + best[3];
        tpos[2] = best[2];
    }    
    this->getDrawPositionOnTrNode((float*)best, best[1], metry + best[3]);
    pos[0] = best[0];
    pos[1] = best[1];
    pos[2] = -best[2];
    posT[0] = best[5];
    posT[1] = -best[6];
    if(q != NULL){
        q[0] = 0; q[1] = 0; q[2] = 0; q[3] = 1;
        Quat::rotateY(q, q, best[3]);
        Quat::rotateX(q, q, -best[4]);
    }
    
    return minDistance;
}

int TDB::findNearestPositionsOnTDB(float* posT, float * pos, QVector<TDB::IntersectionPoint> &points, float maxDistance){
    float *lineBuffer;
    int length = 0;
    getLines(lineBuffer, length, posT);

    float dist = 0;
    float intersectionPoint[3];
    float minDistance = 99999;
    
    for(int i = 0; i < length*12; i+=12){
        dist = Intersections::pointSegmentDistance(lineBuffer + i, lineBuffer + i+6, pos, (float*)&intersectionPoint);
        if(dist < maxDistance){
            points.push_back(TDB::IntersectionPoint());
            points.back().distance = dist;
            points.back().idx = lineBuffer[i+3];
            points.back().m = this->getVectorSectionLengthToIdx(lineBuffer[i+3], lineBuffer[i+4]);
            
            float dist1 = Vec3::distance(lineBuffer + i, lineBuffer + i+6);
            float dist2 = Vec3::distance(lineBuffer + i, intersectionPoint);
            dist1 = dist2/dist1;
            points.back().m += lineBuffer[i+5] + (lineBuffer[i+11] - lineBuffer[i+5])*dist1;
        }
        if(dist < minDistance)
            minDistance = dist;
    }

    if(minDistance >= 99999)
        return -1;

    return minDistance;
}

void TDB::fillNearestSquaredDistanceToTDBXZ(float* posT, QVector<Vector4f> &points, float* bbox){
    float *lineBuffer;
    int length = 0;
    getLines(lineBuffer, length, posT);
    
    float dist = 0;
    int yyy = 0;
    for(int i = 0; i < length*12; i+=12){
        if(bbox != NULL){
            if((lineBuffer[i] < bbox[0] && lineBuffer[i+6] < bbox[0] ) || (lineBuffer[i] > bbox[1] && lineBuffer[i+6] > bbox[1] )){
                //yyy++;
                continue;
            }
            if((lineBuffer[i+2] < bbox[2] && lineBuffer[i+8] < bbox[2] ) || (lineBuffer[i+2] > bbox[3] && lineBuffer[i+8] > bbox[3] )){
                //yyy++;
                continue;
            }
        }
        for(int j = 0; j < points.size(); j++){
            dist = Intersections::pointSegmentSquaredDistanceXZ(lineBuffer + i, lineBuffer + i+6, (float*)&points[j]);
            if(dist < points[j].c)
                points[j].c = dist;
        }
    }
    //qDebug() << yyy << length;
}

bool TDB::getSegmentIntersectionPositionOnTDB(float* posT, float* segment, float len, float* pos, float * q, float* tpos){
    float *lineBuffer;
    int length = 0;
    getLines(lineBuffer, length, posT);
    
    qDebug() << "lines length" << length;
    float best[7];
    best[0] = 99999;
    float dist = 0;
    float intersectionPoint[3];
    intersectionPoint[1] = pos[1];
    int uu;
    for(int i = 0; i < length*12; i+=12){
        //qDebug() << i/12;
        //qDebug() << lineBuffer[i+0] << " "<< lineBuffer[i+1] << " " << lineBuffer[i+2] << " "<< lineBuffer[i+3] << " "<< lineBuffer[i+4] << " " << lineBuffer[i+5] ;
        //qDebug() << lineBuffer[i+6] << " "<< lineBuffer[i+7] << " " << lineBuffer[i+8] << " "<< lineBuffer[i+9] << " "<< lineBuffer[i+10] << " " << lineBuffer[i+11] ;
        
        for(int j = 0; j < len; j+=12){
            bool ok = Intersections::segmentIntersection(
                lineBuffer[i], lineBuffer[i + 2], 
                lineBuffer[i+6 + 0], lineBuffer[i+6 + 2], 
                segment [ j + 0], segment [ j + 2], 
                segment [ j+6 + 0], segment [ j+6 + 2], 
                intersectionPoint[0], intersectionPoint[2]
            );
            if(!ok) continue;
            qDebug() << "intersection";
            qDebug() <<  lineBuffer[i]<< " " << lineBuffer[i + 2]<< " " <<
                lineBuffer[i+6 + 0]<< " " << lineBuffer[i+6 + 2]<< " -- " <<
                segment [ j + 0]<< " " << segment [ j + 2]<< " " <<
                segment [ j+6 + 0]<< " " << segment [ j+6 + 2]<< " " ;
            dist = Vec3::distance(intersectionPoint, pos);

            if(dist < best[0]){
                best[0] = dist;
                best[1] = lineBuffer[i+3];
                best[2] = lineBuffer[i+4];

                float dist1 = Vec3::distance(lineBuffer + i, lineBuffer + i+6);
                float dist2 = Vec3::distance(lineBuffer + i, intersectionPoint);
                dist1 = dist2/dist1;
                best[3] = lineBuffer[i+5] + (lineBuffer[i+11] - lineBuffer[i+5])*dist1;
                //best[3] = intersectionPoint[0];
                //best[4] = intersectionPoint[1];
                //best[5] = intersectionPoint[2];
            }
        }
    }
    qDebug() << "item pos: " << best[0] << " " << best[1] << " " << best[2] << " " << best[3];
    if(best[0] == 99999) return false;
    
    //TRnode* n = trackNodes[(int)best[1]];
    //posT[0] = n->trVectorSection[(int)best[2]].tileX;
    //posT[1] = -n->trVectorSection[(int)best[2]].tileZ;
    
    float metry = this->getVectorSectionLengthToIdx(best[1], best[2]);
    if(tpos != NULL){
        tpos[0] = best[1];
        tpos[1] = metry + best[3];
    }
    this->getDrawPositionOnTrNode((float*)best, best[1], metry + best[3]);
    pos[0] = best[0];
    pos[1] = best[1];
    pos[2] = -best[2];
    posT[0] = best[5];
    posT[1] = -best[6];
    q[0] = 0; q[1] = 0; q[2] = 0; q[3] = 1;
    Quat::rotateY(q, q, best[3]);
    Quat::rotateX(q, q, -best[4]);
    return true;
}

bool TDB::getSegmentIntersectionPositionOnTDB(QVector<TDB::IntersectionPoint> &ipoints, TDB* segmentTDB, float* posT, float* segment, float len, float* pos){
    float *lineBuffer;
    int length = 0;
    getLines(lineBuffer, length, posT);
    
    qDebug() << "lines length" << length;
    float best[7];
    best[0] = 99999;
    float dist = 0;
    float intersectionPoint[3];
    intersectionPoint[1] = pos[1];
    int uu;
    float metry;
    float dist1, dist2;
    
    IntersectionPoint* p;
    for(int i = 0; i < length*12; i+=12){
        //qDebug() << i/12;
        //qDebug() << lineBuffer[i+0] << " "<< lineBuffer[i+1] << " " << lineBuffer[i+2] << " "<< lineBuffer[i+3] << " "<< lineBuffer[i+4] << " " << lineBuffer[i+5] ;
        //qDebug() << lineBuffer[i+6] << " "<< lineBuffer[i+7] << " " << lineBuffer[i+8] << " "<< lineBuffer[i+9] << " "<< lineBuffer[i+10] << " " << lineBuffer[i+11] ;
        
        for(int j = 0; j < len; j+=12){
            bool ok = Intersections::segmentIntersection(
                lineBuffer[i], lineBuffer[i + 2], 
                lineBuffer[i+6 + 0], lineBuffer[i+6 + 2], 
                segment [ j + 0], segment [ j + 2], 
                segment [ j+6 + 0], segment [ j+6 + 2], 
                intersectionPoint[0], intersectionPoint[2]
            );
            if(!ok) continue;
            qDebug() << "intersection";
            qDebug() <<  lineBuffer[i]<< " " << lineBuffer[i + 2]<< " " <<
                lineBuffer[i+6 + 0]<< " " << lineBuffer[i+6 + 2]<< " -- " <<
                segment [ j + 0]<< " " << segment [ j + 2]<< " " <<
                segment [ j+6 + 0]<< " " << segment [ j+6 + 2]<< " " ;
            dist = Vec3::distance(intersectionPoint, pos);
            
            ipoints.push_back(IntersectionPoint());
            p = &ipoints.back();
            p->distance = dist;
            p->idx = lineBuffer[i+3];
            p->sidx = segment[j+3];

            dist1 = Vec3::distance(lineBuffer + i, lineBuffer + i+6);
            dist2 = Vec3::distance(lineBuffer + i, intersectionPoint);
            dist1 = dist2/dist1;
            metry = this->getVectorSectionLengthToIdx(p->idx, lineBuffer[i+4]);
            p->m = metry + lineBuffer[i+5] + (lineBuffer[i+11] - lineBuffer[i+5])*dist1;
            
            if(segmentTDB != NULL){
                dist1 = Vec3::distance(segment + j, segment + j+6);
                dist2 = Vec3::distance(segment + j, intersectionPoint);
                dist1 = dist2/dist1;
                metry = segmentTDB->getVectorSectionLengthToIdx(p->sidx, segment[j+4]);
                p->sm = metry + segment[j+5] + (segment[j+11] - segment[j+5])*dist1;
            }
            qDebug() << "item p: " << p->distance << " " << p->idx << " " << p->m<< " " << p->sidx << " " << p->sm;
            //ipoints.push_back(p);
        }
    }
    std::sort(ipoints.begin(), ipoints.end());
    return true;
}

void TDB::getVectorSectionPoints(int x, int y, float* pos, QVector<float> &ptr, int mode){
    float posT[2];
    posT[0] = x;
    posT[1] = y;
    float tpos[3];
    int ok = this->findNearestPositionOnTDB((float*)posT, pos, NULL, (float*)&tpos);
    if(ok < 0)
        return;

    y = -y;
    int nid = tpos[0];
    int sid = tpos[2];
    
    if(mode == 0){
        return getVectorSectionPoints(x, y, nid, sid, ptr);
    }
    //All sections
    if(mode == 1){
        if(trackNodes[nid] == NULL)
            return;
        for(int i = 0; i < trackNodes[nid]->iTrv; i++ ){
            //float* oldPtr = ptr;
            getVectorSectionPoints(x, y, nid, i, ptr);
        }
        return;
    }
}

void TDB::getVectorSectionPoints(int x, int y, int uid, QVector<float> &ptr){
    y = -y;
    //qDebug() << "aaa";
        for (int j = 1; j <= iTRnodes; j++) {
            TRnode* n = trackNodes[j];
            if (n == NULL) continue;
            if (n->typ == -1) continue;
            if (n->typ == 1) {
                for (int i = 0; i < n->iTrv; i++) {
                    if(n->trVectorSection[i].worldTileX == x && n->trVectorSection[i].worldTileZ == y && n->trVectorSection[i].worldObjectId == uid ){
                        qDebug() << "mam";
                        getVectorSectionPoints(x, y, j, i, ptr);
                    }
                }
            }
    }
}

void TDB::getVectorSectionPoints(int x, int y, int nId, int sId, QVector<float> &ptr){
    float matrix[16];
    float q[4];
    float p[3];
    q[0] = 0; q[1] = 0; q[2] = 0; q[3] = 1;
    float rot[3];

    TRnode* n = trackNodes[nId];

    rot[0] = M_PI;
    rot[1] = -n->trVectorSection[sId].ay;
    rot[2] = 0;//n->trVectorSection[sId].az;
    p[0] = (n->trVectorSection[sId].tileX - x)*2048 + n->trVectorSection[sId].x;
    p[1] = n->trVectorSection[sId].y;
    p[2] = (-n->trVectorSection[sId].tileZ + y)*2048 - n->trVectorSection[sId].z;

    Quat::fromRotationXYZ(q, rot);
    Mat4::fromRotationTranslation(matrix, q, p);
    Mat4::rotate(matrix, matrix, n->trVectorSection[sId].ax, 1, 0, 0);
    if(n->trVectorSection[sId].az != 0.0f)
        Mat4::rotate(matrix, matrix,
                -n->trVectorSection[sId].az, 0, 0, 1);
    //Mat4::fromRotationTranslation(matrix, q, objMatrix);
    if(tsection->sekcja[(int) n->trVectorSection[sId].sectionIndex] == NULL){
        qDebug() << "nie ma sekcji " << (int) n->trVectorSection[sId].sectionIndex;
    }
    tsection->sekcja[(int) n->trVectorSection[sId].sectionIndex]->getPoints(ptr, matrix);
    return;
}

void TDB::updateTrItemRData(TRitem* tr){
    if(tr == NULL) 
        return;
    float trPosition[7];
    int id = findTrItemNodeId(tr->trItemId);
    if(id < 1) return;
    getDrawPositionOnTrNode((float*)&trPosition, id, tr->getTrackPosition());
    tr->setTrItemRData((float*)&trPosition+5, (float*)&trPosition);
}

void TDB::newPickupObject(int* &itemId, int trNodeId, float metry, int type){
    if(type != WorldObj::pickup) 
        return;
    
    int newTRitemId = getNewTRitemId();
    
    float trPosition[7];
    this->trackItems[newTRitemId] = TRitem::newPickupItem(newTRitemId, metry);
    getDrawPositionOnTrNode((float*)&trPosition, trNodeId, metry);
    this->trackItems[newTRitemId]->setTrItemRData((float*)&trPosition+5, (float*)&trPosition);
    this->trackItems[newTRitemId]->setTrItemPData((float*)&trPosition+5, (float*)&trPosition);
    itemId = new int[2];
    itemId[0] = 0;
    itemId[1] = newTRitemId;
    
    this->addItemToTrNode(trNodeId, itemId[1]);
    updateTrItem(newTRitemId);
    updateTrNode(trNodeId);
}

void TDB::newPlatformObject(int* itemId, int trNodeId, float metry, int type){
    std::function<TRitem*(int, int)> newTRitem;
    if(type == WorldObj::platform) newTRitem = &TRitem::newPlatformItem;
    if(type == WorldObj::siding) newTRitem = &TRitem::newSidingItem;
    if(type == WorldObj::carspawner) newTRitem = &TRitem::newCarspawnerItem;

    int newTRitemId1 = getNewTRitemId();
    int dlugosc = this->getVectorSectionLength(trNodeId);
    float m = metry - 1;
    if(metry < 0) metry = 0;
    float trPosition[7];
    this->trackItems[newTRitemId1] = newTRitem(newTRitemId1, m);
    int newTRitemId2 = getNewTRitemId();
    this->trackItems[newTRitemId1]->platformTrItemData[1] = newTRitemId2;
    getDrawPositionOnTrNode((float*)&trPosition, trNodeId, m);
    this->trackItems[newTRitemId1]->setTrItemRData((float*)&trPosition+5, (float*)&trPosition);
    itemId[0] = newTRitemId1;
    m = metry + 1;
    if(metry > dlugosc) metry = dlugosc;
    this->trackItems[newTRitemId2] = newTRitem(newTRitemId2, m);
    this->trackItems[newTRitemId2]->platformTrItemData[1] = newTRitemId1;
    this->trackItems[newTRitemId2]->platformTrItemData[0] = 0xFFFF0000;
    getDrawPositionOnTrNode((float*)&trPosition, trNodeId, m);
    this->trackItems[newTRitemId2]->setTrItemRData((float*)&trPosition+5, (float*)&trPosition);
    itemId[1] = newTRitemId2;
    
    this->addItemToTrNode(trNodeId, itemId[0]);
    this->addItemToTrNode(trNodeId, itemId[1]);
    updateTrItem(itemId[0]);
    updateTrItem(itemId[1]);
    updateTrNode(trNodeId);
    
}

void TDB::newSignalObject(QString filename, SignalObj::SignalUnit* units, int &signalUnits, int trNodeId, float metry, int type){
    if(type != WorldObj::signal) 
        return;
    SignalShape* sShape = this->sigCfg->findSignalShape(filename);
    if(sShape == NULL)
        return;
    
    //std::vector<int> subObjI;
   // for(int i = 0; i < sShape->iSubObj; i++){
   //     if(sShape->subObj[i].sigSubTypeId == sShape->SIGNAL_HEAD && !sShape->subObj[i].optional){
   //         subObjI.push_back(i);
   //     }
   // }
    
  //  signalUnits = subObjI.size();
    //qDebug() << "tdb signalUnits" << signalUnits;
    //itemId = new int[signalUnits*2];
    float trPosition[7];

    unsigned int flags = 0;
    //if(sShape->isJnLink) flags = 1;
    //int sidx = 0;
    getDrawPositionOnTrNode((float*)&trPosition, trNodeId, metry);
    
    signalUnits = 0;
    int enabledFrontFlag = 0;
    int enabledBackFlag = 0;
    int *enableFlag = NULL;
    for(int i = 0; i < sShape->iSubObj; i++){
        if(sShape->subObj[i].optional && (!sShape->subObj[i].defaultt))
            continue;
        if(sShape->subObj[i].sigSubTypeId == sShape->SIGNAL_HEAD)
            continue;
        if(sShape->subObj[i].backFacing)
            enableFlag = &enabledBackFlag;//|= 1 << (sShape->subObj[i].faceidx+3);
        else
            enableFlag = &enabledFrontFlag;//|= 1 << (sShape->subObj[i].faceidx+3);
        
        if(sShape->subObj[i].sigSubType == "NUMBER_PLATE")
            *enableFlag |= 0b0000010000;
        if(sShape->subObj[i].sigSubType == "GRADIENT_PLATE")
            *enableFlag |= 0b0000100000;
        if(sShape->subObj[i].sigSubType == "USER1")
            *enableFlag |= 0b0001000000;
        if(sShape->subObj[i].sigSubType == "USER2")
            *enableFlag |= 0b0010000000;
        if(sShape->subObj[i].sigSubType == "USER3")
            *enableFlag |= 0b0100000000;
        if(sShape->subObj[i].sigSubType == "USER4")
            *enableFlag |= 0b1000000000;
    }
    
    for(int i = 0; i < sShape->iSubObj; i++){
        //sidx = subObjI[i];
        if(sShape->subObj[i].optional && (!sShape->subObj[i].defaultt))
            continue;
        units[i].enabled = true;
        units[i].head = false;
        if(sShape->subObj[i].sigSubTypeId != sShape->SIGNAL_HEAD)
            continue;
        units[i].head = true;
        signalUnits++;
        if(sShape->subObj[i].backFacing)
            flags = enabledBackFlag;
        else
            flags = enabledFrontFlag;
        if(sShape->subObj[i].isJnLink) 
            flags |= 1;
        int newTRitemId = getNewTRitemId();
        int direction = 1;
        float angle = -trPosition[3];
        if(sShape->subObj[i].backFacing){
            direction = 0;
            angle += M_PI;
        }
        if(angle > 2*M_PI)
            angle -= 2*M_PI;
        if(angle < 0)
            angle += 2*M_PI;
        this->trackItems[newTRitemId] = TRitem::newSignalItem(newTRitemId, metry, direction, flags, sShape->subObj[i].sigSubSType);
        this->trackItems[newTRitemId]->setTrItemRData((float*)&trPosition+5, (float*)&trPosition);
        this->trackItems[newTRitemId]->setSignalRot(angle);
        units[i].tdbId = 0;
        units[i].itemId =  newTRitemId;
        this->addItemToTrNode(trNodeId, units[i].itemId);
        updateTrItem(newTRitemId);
        updateTrNode(trNodeId);
    }
}

void TDB::enableSignalSubObj(QString filename, SignalObj::SignalUnit &unit, int i, int tritemid){
    SignalShape* sShape = this->sigCfg->findSignalShape(filename);
    if(sShape == NULL)
        return;

    //float trPosition[7];
    int newTRitemId = getNewTRitemId();
    unsigned int flags = 0;
    //if(sShape->isJnLink) flags = 1;
    //int sidx = 0;
    TRitem* trit = trackItems[tritemid];
    if(trit == NULL) return;
    int nid = findTrItemNodeId(tritemid);
    if(nid < 0) return;
    
    unit.enabled = true;
    unit.head = false;
    if(sShape->subObj[i].sigSubTypeId != sShape->SIGNAL_HEAD)
        return;
    unit.head = true;
    flags = trit->trSignalType1 & ~1;
    if(sShape->subObj[i].isJnLink) 
        flags |= 1;
    int direction = trit->trSignalType2;
    float angle = trit->trSignalType3;
    if(sShape->subObj[i].backFacing != sShape->subObj[0].backFacing){
        direction = abs(direction - 1);
        angle += M_PI;
        if(angle > 2*M_PI)
            angle -= 2*M_PI;
    }
    this->trackItems[newTRitemId] = TRitem::newSignalItem(newTRitemId, trit->getTrackPosition(), direction, flags, sShape->subObj[i].sigSubSType);
    this->trackItems[newTRitemId]->setTrItemRData((float*)(trit->trItemRData+3), (float*)trit->trItemRData);
    this->trackItems[newTRitemId]->setSignalRot(angle);
    unit.tdbId = 0;
    unit.itemId =  newTRitemId;
    this->addItemToTrNode(nid, unit.itemId);
}

void TDB::newSpeedPostObject(int speedPostType, QVector<int> & itemId, int trNodeId, float metry, int type){
    if(type != WorldObj::speedpost) 
        return;
    //SpeedPost* sShape = this->speedPostDAT->speedPost[speedPostId];
    //if(sShape == NULL)
    //    return;
    
    float trPosition[7];
    getDrawPositionOnTrNode((float*)&trPosition, trNodeId, metry);
    int newTRitemId = getNewTRitemId();
    this->trackItems[newTRitemId] = TRitem::newSpeedPostItem(newTRitemId, metry, speedPostType);
    this->trackItems[newTRitemId]->setTrItemRData((float*)&trPosition+5, (float*)&trPosition);
    this->trackItems[newTRitemId]->setTrItemPData((float*)&trPosition+5, (float*)&trPosition);
    float angle = trPosition[3]-M_PI/2;
    if(angle > 2*M_PI)
        angle -= 2*M_PI;
    if(angle < 0)
        angle += 2*M_PI;
    this->trackItems[newTRitemId]->setSpeedpostRot(angle);
    itemId.push_back(0);
    itemId.push_back(newTRitemId);
    //qDebug() << "tritem size"<<itemId.size();
    this->addItemToTrNode(trNodeId, itemId.last());
    updateTrItem(newTRitemId);
    updateTrNode(trNodeId);
}

void TDB::newLevelCrObject(int* &itemId, int trNodeId, float metry, int type){
    if(type != WorldObj::levelcr) 
        return;
    
    float trPosition[7];
    getDrawPositionOnTrNode((float*)&trPosition, trNodeId, metry);
    int newTRitemId = getNewTRitemId();
    this->trackItems[newTRitemId] = TRitem::newLevelCrItem(newTRitemId, metry);
    this->trackItems[newTRitemId]->setTrItemRData((float*)&trPosition+5, (float*)&trPosition);
    this->trackItems[newTRitemId]->setTrItemPData((float*)&trPosition+5, (float*)&trPosition);
    //qDebug() <<"a2";
    itemId = new int[2];
    itemId[0] = 0;
    if(this->road){
        itemId[0] = 1;
        this->trackItems[newTRitemId]->trItemSData2 = 5;
    }
    itemId[1] = newTRitemId;
    this->addItemToTrNode(trNodeId, itemId[1]);
    updateTrItem(newTRitemId);
    updateTrNode(trNodeId);
}

void TDB::newSoundRegionObject(int soundregionTrackType, QVector<int> &itemId, int trNodeId, float metry, int type){
    if(type != WorldObj::soundregion) 
        return;
    int newTRitemId = getNewTRitemId();
    float trPosition[7];
    this->trackItems[newTRitemId] = TRitem::newSoundRegionItem(newTRitemId, metry);
    getDrawPositionOnTrNode((float*)&trPosition, trNodeId, metry);
    this->trackItems[newTRitemId]->setTrItemRData((float*)&trPosition+5, (float*)&trPosition);
    this->trackItems[newTRitemId]->setTrItemPData((float*)&trPosition+5, (float*)&trPosition);
    this->trackItems[newTRitemId]->setSoundRegionData(-trPosition[3], soundregionTrackType);
    
    itemId.push_back(0);
    itemId.push_back(newTRitemId);
    
    this->addItemToTrNode(trNodeId, itemId.last());
    updateTrItem(newTRitemId);
    updateTrNode(trNodeId);
}

void TDB::newHazardObject(int * &itemId, int trNodeId, float metry, int type){
    if(type != WorldObj::hazard) 
        return;
    int newTRitemId = getNewTRitemId();
    float trPosition[7];
    this->trackItems[newTRitemId] = TRitem::newHazardItem(newTRitemId, metry);
    getDrawPositionOnTrNode((float*)&trPosition, trNodeId, metry);
    this->trackItems[newTRitemId]->setTrItemRData((float*)&trPosition+5, (float*)&trPosition);
    this->trackItems[newTRitemId]->setTrItemPData((float*)&trPosition+5, (float*)&trPosition);
    itemId = new int[2];
    itemId[0] = 0;
    itemId[1] = newTRitemId;
    
    this->addItemToTrNode(trNodeId, itemId[1]);
    updateTrItem(newTRitemId);
    updateTrNode(trNodeId);
}

void TDB::newCrossOverObject(int id1, float m1, int id2, float m2, int shapeIdx){
    int newTRitemId1 = getNewTRitemId();
    trackItems[newTRitemId1] = NULL;
    int newTRitemId2 = getNewTRitemId();
    float trPosition[7];
    
    trackItems[newTRitemId1] = TRitem::newCrossOverItem(newTRitemId1, m1, newTRitemId2, shapeIdx);
    
    getDrawPositionOnTrNode((float*)&trPosition, id1, m1);
    //qDebug() << trPosition[0]<< trPosition[1]<< trPosition[2];
    trackItems[newTRitemId1]->setTrItemRData((float*)&trPosition+5, (float*)&trPosition);
    addItemToTrNode(id1, newTRitemId1);
    
    trackItems[newTRitemId2] = TRitem::newCrossOverItem(newTRitemId2, m2, newTRitemId1, shapeIdx);
    getDrawPositionOnTrNode((float*)&trPosition, id2, m2);
    //qDebug() << trPosition[0]<< trPosition[1]<< trPosition[2];
    trackItems[newTRitemId2]->setTrItemRData((float*)&trPosition+5, (float*)&trPosition);
    addItemToTrNode(id2, newTRitemId2);
    updateTrItem(newTRitemId1);
    updateTrItem(newTRitemId2);
    updateTrNode(id1);
    updateTrNode(id2);
}

void TDB::deleteTrItem(int trid){
    TRitem* trit = this->trackItems[trid];
    
    if (trit == NULL){
        ErrorMessage *e = new ErrorMessage(
                ErrorMessage::Type_Error, 
                ErrorMessage::Source_Editor, 
                "Track Item not found." );
        ErrorMessagesLib::PushErrorMessage(e);
        return;
    }
    
    /*if(wobj != NULL){
        wobj->typeID;
        trit->type;
        
        if(wobj->typeID == WorldObj::siding){
            if (trit->type != "sidingitem"){
                return;
            }
        }
        if(wobj->typeID == WorldObj::platform){
            if (trit->type != "platformitem"){
                ErrorMessage *e = new ErrorMessage(
                        ErrorMessage::Type_Error, 
                        ErrorMessage::Source_TDB, 
                        "Expected 'platformitem' but '"+trit->type+"' found instead." );
                ErrorMessagesLib::PushErrorMessage(e);
                return;
            }
        }
        if(wobj->typeID == WorldObj::signal){
            if (trit->type != "signalitem"){
                return;
            }
        }
        if(wobj->typeID == WorldObj::levelcr){
            if (trit->type != "levelcritem"){
                return;
            }
        }
        if(wobj->typeID == WorldObj::speedpost){
            if (trit->type != "speedpostitem"){
                return;
            }
        }
        if(wobj->typeID == WorldObj::carspawner){
            if (trit->type != "carspawneritem"){
                return;
            }
        }
        if(wobj->typeID == WorldObj::hazard){
            if (trit->type != "hazzarditem"){
                return;
            }
        }
        if(wobj->typeID == WorldObj::pickup){
            if (trit->type != "pickupitem"){
                return;
            }
        }
        if(wobj->typeID == WorldObj::soundregion){
            if (trit->type != "soundregionitem"){
                return;
            }
        }
    }*/
    
    if(trit != NULL){
        trit->type = "emptyitem";
        updateTrItem(trid);
    }
    QVector<int> ids;
    int nid = findTrItemNodeIds(trid, ids);
    if(ids.size() == 0) 
        return;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for(int i = 0; i < ids.size(); i++)
        deleteItemFromTrNode(ids[i], trid);

}

void TDB::addItemToTrNode(int tid, int iid){
    TRnode* n = this->trackNodes[tid];
    if(n == NULL) return;
    int* newVec = new int[n->iTri+1];
    std::copy(n->trItemRef, n->trItemRef+n->iTri, newVec);
    newVec[n->iTri++] = iid;
    delete[] n->trItemRef;
    n->trItemRef = newVec;
}

void TDB::deleteItemFromTrNode(int tid, int iid){
    TRnode* n = this->trackNodes[tid];
    if(n == NULL || n->iTri == 0) return;
    const int matches = std::count(n->trItemRef, n->trItemRef + n->iTri, iid);
    if(matches == 0) return;
    const int remaining = n->iTri - matches;
    int* newVec = remaining ? new int[remaining] : nullptr;
    for(int i = 0, j = 0; i < n->iTri; i++){
        if(n->trItemRef[i] == iid){
            continue;
        }
        newVec[j++] = n->trItemRef[i];
    }
    n->iTri = remaining;
    delete[] n->trItemRef;
    n->trItemRef = newVec;
    updateTrNode(tid);
}

void TDB::fixTDBVectorElevation(int x, int y, int UiD){
    y = -y;
    
    TRnode *n;
    int tid = -1;
    for (int i = 1; i <= iTRnodes; i++) {
            n = trackNodes[i];
            if (n == NULL) continue;
            if (n ->typ == -1) continue;
            if (n->typ == 1) {
                bool found = false;
                for(int j = 0; j < n->iTrv; j++){
                    if(n->trVectorSection[j].worldTileX == x)
                        if(n->trVectorSection[j].worldTileZ == y)
                            if(n->trVectorSection[j].worldObjectId == UiD){
                                found = true;
                                break;
                    }
                }
                if(found){
                    fixTDBVectorElevation(n);
                    continue;
                }
            }
        }
}

void TDB::fixTDBVectorElevation(TRnode *n){
    if (n == NULL) return;
    if (n->typ != 1) return;
    
    n->trVectorSection[0].az = 0;
    n->trVectorSection[n->iTrv - 1].az = 0;
    int sect;
    float angle1, angle2;
    for(int j = 1; j < n->iTrv; j++){
        sect = n->trVectorSection[j-1].sectionIndex;
        angle1 = tsection->sekcja[sect]->getAngle();
        sect = n->trVectorSection[j].sectionIndex;
        angle2 = tsection->sekcja[sect]->getAngle();
        if(angle1 < 0 && angle2 < 0)
            n->trVectorSection[j].az = -0.05;
        else if(angle1 > 0 && angle2 > 0)
            n->trVectorSection[j].az = 0.05;
        else
            n->trVectorSection[j].az = 0;
    }
}

void TDB::deleteVectorSection(int x, int y, int UiD){
    y = -y;
    
    TRnode *n;
    int tid = -1;
    for (int i = 1; i <= iTRnodes; i++) {
            n = trackNodes[i];
            if (n == NULL) continue;
            if (n ->typ == -1) continue;
            if (n->typ == 1) {
                for(int j = 0; j < n->iTrv; j++)
                    if(n->trVectorSection[j].worldTileX == x)
                        if(n->trVectorSection[j].worldTileZ == y)
                            if(n->trVectorSection[j].worldObjectId == UiD){
                                tid = i;
                                break;
                    }
            }
        }
    if(tid > 0){
        qDebug() << "mam id: " << tid;
        deleteVectorSection(tid);
    }
    TDB::refresh();
}

void TDB::deleteTree(int x, int y, int UiD){
    y = -y;
    
    TRnode *n;
    int tid = -1;
    for (int i = 1; i <= iTRnodes; i++) {
            n = trackNodes[i];
            if (n == NULL) continue;
            if (n ->typ == -1) continue;
            if (n->typ == 1) {
                for(int j = 0; j < n->iTrv; j++)
                    if(n->trVectorSection[j].worldTileX == x)
                        if(n->trVectorSection[j].worldTileZ == y)
                            if(n->trVectorSection[j].worldObjectId == UiD){
                                tid = i;
                                break;
                    }
            }
        }
    if(tid > 0){
        qDebug() << "mam id: " << tid;
        deleteTree(tid);
    }
}

void TDB::deleteTree(int d) {
        //if(trackNodes[d].typ!=0){
        //    System.out.println("to nie endpoint");
        //}
        
        int* drzewo = new int[iTRnodes+1];
        for(int i = 1; i <= iTRnodes; i++)
            drzewo[i] = 0;
         
        addToDeletedTree(drzewo, d);
        
        int w = 0;
        for(int i = 1; i <= iTRnodes; i++){
            if(drzewo[i] == 1) w++;
        }
        qDebug() << "Ilosc elementów w tym drzewie: " << w;
        
        if(w > 1000) {
            qDebug() << "Za duzo elementow do usuniecia, lepiej nie usuwac";
            return;
        }

        for(int i = 1; i <= iTRnodes; i++){
            if(drzewo[i] == 1){
                qDebug() << "Usuwam " << i;
                deleteAllTrItemsFromVectorSection(i);
                trackNodes[i] = NULL;
            }
        }
        TDB::refresh();
    }
    
void TDB::addToDeletedTree(int* drzewo, int d){
        drzewo[d] = 1;
        for(int i = 0; i < 3; i++){
            //qDebug() << trackNodes[d]->pins[i].link;
            if(trackNodes[d]->pins[i].link == 0)
                continue;
            //qDebug() << drzewo[trackNodes[d]->pins[i].link];
            if(drzewo[trackNodes[d]->pins[i].link] == 0)
                addToDeletedTree(drzewo, trackNodes[d]->pins[i].link);
        }
    }

bool TDB::deleteNulls() {
        for(int i = 1; i <= iTRnodes; i++){
            if(trackNodes[i] == NULL){
                qDebug() << "Removing NULL TrackNode at: "<<i;
                int stare = findBiggest();
                if(stare <= i) {
                    qDebug() << "There is no more NULL TrackNodes.";
                    iTRnodes = stare;
                    return false;
                }
                trackNodes[i] = trackNodes[stare];
                trackNodes[stare] = NULL;
                qDebug() << i << "Replaced by: " << stare;
                
                for(int j = 0; j < 3; j++){
                    if(trackNodes[i]->pins[j].link == 0)
                        continue;
                    if(trackNodes[trackNodes[i]->pins[j].link] == NULL)
                        qDebug() << "Fail, unexpected NULL TrackNode found!" << trackNodes[i]->pins[j].link;
                    else
                        trackNodes[trackNodes[i]->pins[j].link]->podmienTrPin(stare, i);
                }
                replaceSignalDirJunctionId(stare, i);
                return true;
            }
        }
        qDebug() << "There is no more NULL TrackNodes.";
        return false;
    }

void TDB::sortItemRefs(){
    StaticTrackItems = &trackItems;
    for(int i = 1; i <= iTRnodes; i++){
        if(trackNodes[i] == NULL)
                continue;
        if(trackNodes[i]->iTri > 0){
            int *pointer = trackNodes[i]->trItemRef;
            int len = trackNodes[i]->iTri;
            // Preserve the relative order of items at the same path position.
            std::stable_sort(pointer, pointer + len, SortItemRefsCompare);
        }
    }
}

bool TDB::SortItemRefsCompare(int a, int b){
    return TDB::StaticTrackItems[0][a]->getTrackPosition() < TDB::StaticTrackItems[0][b]->getTrackPosition();
}

void TDB::replaceSignalDirJunctionId(int oldId, int newId){
    for (int i = 0; i <= this->iTRitems; i++) {
        if(trackItems[i] == NULL) continue;
        if(trackItems[i]->trSignalDir != NULL){
            for(int j = 0; j < trackItems[i]->trSignalDirs*4; j+=4){
                if(trackItems[i]->trSignalDir[j+0] == oldId){
                    trackItems[i]->trSignalDir[j+0] = newId;
                    qDebug() << "trSignalDir trndoe id replaced: "<<oldId<<" "<<newId;
                }
            }
        }
    }
}

int TDB::findBiggest() {
        for(int i = iTRnodes; i > 0; i--){
            if(trackNodes[i] != NULL)
                return i;
        }
        return 1;
    }

bool TDB::saveEmpty(bool road, const QString &routeFileStem) {
    if (!Game::writeEnabled)
        return false;
    QString sh;
    QString path;
    QString extension = "tdb";
    if(road) extension = "rdb";
    path = Game::root + "/ROUTES/" + Game::route + "/" + routeFileStem + "." + extension;
    path = ContentPath::normalize(path);
    qDebug() << path;
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)){
        qDebug() << "Error creating empty TDB file!";
        return false;
    }
    QTextStream out(&file);
    out.setRealNumberPrecision(6);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << "SIMISA@@@@@@@@@@JINX0T0t______\n\n";
    out << "TrackDB (\n";
    out << "	Serial ( 0 )\n";
    out << ")";
    out.flush();
    const bool saved = out.status() == QTextStream::Ok
            && file.error() == QFile::NoError;
    file.close();
    return saved && file.error() == QFile::NoError;
}

void TDB::updateTrNode(int nid){
    
}

void TDB::updateTrItem(int iid){
    
}

void TDB::updateTrackSection(int id){
    
}

void TDB::updateTrackShape(int id){
    
}

void TDB::save() {
    if(!Game::writeEnabled) return;
    if(!Game::writeTDB || !Game::writeTDBSessionAllowed) return;
    if(!loaded) {
        qWarning() << "Refusing to save an unloaded"
                   << (road ? "RDB" : "TDB")
                   << "because an existing database may have failed to load";
        return;
    }

    while(deleteNulls());
    sortItemRefs();
    this->isInitLines = false;
    
    QString sh;
    QString path;
    QString extension = "tdb";
    if(this->road) extension = "rdb";
    path = Game::root + "/ROUTES/" + Game::route + "/" + Game::routeName + "." + extension;
    path = ContentPath::normalize(path);
    qDebug() << path;
    QFile file(path);

    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)){
        qDebug() << "Error saving TDB file!";
        return;
    }
    QTextStream out(&file);
    out.setRealNumberPrecision(6);
    //out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << "SIMISA@@@@@@@@@@JINX0T0t______\n\n";
    saveToStream(out);
    file.close();

    saveTit();
    if(!this->road) 
        this->tsection->saveRoute();
    qDebug() << "Route Saved";
}

int TDB::updateTrNodeData(FileBuffer *data){
    QString sh;
    int nid = 0;

    TRnode *nowy = NULL;
    
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        qDebug() << sh;
        if (sh == ("id")) {
            nid = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("remove")) {
            qDebug() << "remove trnode" << nid;
            trackNodes[nid] = NULL;
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("tracknode")) {
            //objloaded = false;
            nowy = new TRnode();
            nowy->loadUtf16Data(data);
            trackNodes[nid] = nowy;
            ParserX::SkipToken(data);
            continue;
        }
        
        ParserX::SkipToken(data);
        continue;
    }
    return nid;
}

TRitem *TDB::updateTrItemData(FileBuffer *data){
    QString sh;
    int nid = 0;

    TRitem *nowy = new TRitem();
    if(this->road)
        nowy->tdbId = 1;
    
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        qDebug() << sh;
        if (sh == ("id")) {
            nid = ParserX::GetNumber(data);
            nowy->trItemId = nid;
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("remove")) {
            qDebug() << "remove tritem" << nid;
            trackItems[nid] = NULL;
            ParserX::SkipToken(data);
            continue;
        }
        if(!nowy->init(sh)){
            qDebug() << "#TDB TrItemTable undefined token " << sh;
            ParserX::SkipToken(data);
            continue;
        } else {
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                nowy->set(sh, data);
                ParserX::SkipToken(data);
            }
            this->trackItems[nowy->trItemId] = nowy;
            ParserX::SkipToken(data);
            continue;
        }
        
        ParserX::SkipToken(data);
        continue;
    }
    return nowy;
}

void TDB::updateTrackShapeData(FileBuffer *data){
    QString sh;
    TrackShape *nowy = new TrackShape();
    
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        qDebug() << sh;
        if (sh == ("trackshape")) {
            nowy->loadUtf16Data(data);
            this->tsection->shape[nowy->id] = nowy;
            if(nowy->id >= tsection->routeShapes)
                tsection->routeShapes = nowy->id + 1;
            ParserX::SkipToken(data);
            continue;
        }
        ParserX::SkipToken(data);
        continue;
    }
}

void TDB::updateTrackSectionData(FileBuffer *data){
    QString sh;
    TSection *nowy = new TSection();
    
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        qDebug() << sh;
        if (sh == ("tracksection")) {
            nowy->loadUtf16Data(data);
            this->tsection->sekcja[nowy->id] = nowy;
            if(nowy->id >= tsection->routeMaxIdx){
                tsection->routeMaxIdx = nowy->id + 1;
                if(tsection->routeMaxIdx % 2 == 1)
                    tsection->routeMaxIdx++;
            }
            ParserX::SkipToken(data);
            continue;
        }
        ParserX::SkipToken(data);
        continue;
    }
}

void TDB::saveToStream(QTextStream &out){
    out << "TrackDB (\n";
    out << "	Serial ( " << this->serial << " )\n";
    if(this->iTRnodes > 0){
    out << "	TrackNodes ( " << (this->iTRnodes) << "\n";

    for (int i = 1; i <= this->iTRnodes; i++) {
        if (trackNodes[i] == NULL) 
            continue;
        out << "		TrackNode ( " << i << "\n";
        switch (trackNodes[i]->typ) {
            case 0:
                out << "			TrEndNode ( " << trackNodes[i]->endNodeValue << " )\n";
                out << "			UiD ( ";
                trackNodes[i]->uid.save(out);
                out << ")\n";
                out << "			TrPins ( 1 0\n";
                out << "				TrPin ( " << trackNodes[i]->pins[0].link << " " << trackNodes[i]->pins[0].direction << " )\n";
                out << "			)\n";
                break;
            case 1:
                out << "			TrVectorNode (\n";
                out << "				TrVectorSections ( " << trackNodes[i]->iTrv << "";
                for (int j = 0; j < trackNodes[i]->iTrv; j++) {
                    trackNodes[i]->trVectorSection[j].save(out);
                    if (j % 11 == 0 && j > 0 && j < trackNodes[i]->iTrv - 1)
                        out << "\n					";
                }
                out << " )\n";
                if(trackNodes[i]->trItemRef != 0 && trackNodes[i]->iTri > 0){
                    out << "				TrItemRefs ( "<<trackNodes[i]->iTri<<"\n";
                    for(int j = 0; j<trackNodes[i]->iTri; j++){
                        out << "					TrItemRef ( "<<trackNodes[i]->trItemRef[j]<<" )\n";
                    }
                    out << "				)\n";
                }
                out << "			)\n";
                out << "			TrPins ( 1 1\n";
                out << "				TrPin ( " << trackNodes[i]->pins[0].link << " " << trackNodes[i]->pins[0].direction << " )\n";
                out << "				TrPin ( " << trackNodes[i]->pins[1].link << " " << trackNodes[i]->pins[1].direction << " )\n";
                out << "			)\n";
                break;
            case 2:
                out << "			TrJunctionNode ( " << trackNodes[i]->junction.unknown0 << " " << trackNodes[i]->junction.shapeIndex << " " << trackNodes[i]->junction.unknown2 << " )\n";
                out << "			UiD ( ";
                trackNodes[i]->uid.save(out);
                out << ")\n";
                out << "			TrPins ( 1 2\n";
                out << "				TrPin ( " << trackNodes[i]->pins[0].link << " " << trackNodes[i]->pins[0].direction << " )\n";
                out << "				TrPin ( " << trackNodes[i]->pins[1].link << " " << trackNodes[i]->pins[1].direction << " )\n";
                out << "				TrPin ( " << trackNodes[i]->pins[2].link << " " << trackNodes[i]->pins[2].direction << " )\n";
                out << "			)\n";
                break;
        }

        out << "		)\n";
    }
    out << "	)\n";
    }
    
    if(this->iTRitems > 0){
        out << "	TrItemTable ( " << (this->iTRitems) << "\n";
        for (int i = 0; i <= this->iTRitems; i++) {
            if(this->trackItems[i] != NULL)
                this->trackItems[i]->save(&out);
        }
        out << "	)\n";
    }

    out << ")";

}

void TDB::saveTit() {
    if(!Game::writeEnabled) return;
    if(!Game::writeTDB || !Game::writeTDBSessionAllowed) return;
    
    QString path;
    QString extension = "tit";
    if(this->road) extension = "rit";
    path = Game::root + "/ROUTES/" + Game::route + "/" + Game::routeName + "." + extension;
    path = ContentPath::normalize(path);
    qDebug() << path;
    QFile file(path);

    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)){
        qDebug() << "Error saving TIT file!";
        return;
    }
    QTextStream out(&file);
    out.setRealNumberPrecision(6);
    //out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << "SIMISA@@@@@@@@@@JINX0T0t______\n\n";

    bool tit = true;
    if(this->iTRitems > 0){
        out << "TrItemTable ( " << (this->iTRitems) << "\n";
        for (int i = 0; i <= this->iTRitems; i++) {
            if(this->trackItems[i] != NULL)
                this->trackItems[i]->save(&out, tit);
        }
        out << ")";
    }
    

    file.close();

    //qDebug() << "TIT Saved";
}

void TDB::checkSignals(){
    int trtype[4];
    trtype[0] = 0;
    trtype[1] = 0;
    trtype[2] = 0;
    trtype[3] = 0;
    if(this->iTRitems > 0){
        int tid = 0;
        TRnode* n;
        for (int i = 0; i < this->iTRitems; i++) {
            if(trackItems[i] == NULL) continue;
            if(trackItems[i]->trSignalDir != NULL){
                for(int j = 0; j < trackItems[i]->trSignalDirs*4; j+=4){
                    tid = trackItems[i]->trSignalDir[j+0];
                    n = trackNodes[tid];
                    if(n == NULL)
                        trtype[3]++;
                    else
                        trtype[n->typ]++;
                }
            }
        }
    }
    qDebug() << "suma: "<<trtype[0]<<" "<<trtype[1]<<" "<<trtype[2]<<" "<<trtype[3];
}

TDB::TDB(const TDB& o) {
    
    // simple copy
    tsection = o.tsection; 
    sigCfg = o.sigCfg;
    speedPostDAT = o.speedPostDAT;
    endIdObj = o.endIdObj;
    junctIdObj = o.junctIdObj;
    
    collisionLineBuffer = o.collisionLineBuffer;
    collisionLineLength = o.collisionLineLength;
    collisionLineHash = o.collisionLineHash;
    
    // deep copy
    loaded = o.loaded;
    sourceFileExists = o.sourceFileExists;
    iTRnodes = o.iTRnodes;
    iTRitems = o.iTRitems;
    serial = o.serial;
    defaultEnd = o.defaultEnd;
    wysokoscSieci = o.wysokoscSieci;
    road = o.road;
    
    for (auto it = o.trackItems.begin(); it != o.trackItems.end(); ++it ){
        if(it->second == NULL)
            continue;
        trackItems[it->first] = new TRitem(*(it->second));
    }
    
    for (auto it = o.trackNodes.begin(); it != o.trackNodes.end(); ++it ){
        if(it->second == NULL)
            continue;
        trackNodes[it->first] = new TRnode(*(it->second));
    }
}

TDB::~TDB() {
    for (auto it = trackItems.begin(); it != trackItems.end(); ++it ){
        if(it->second == NULL)
            continue;
        delete it->second;
    }
    
    for (auto it = trackNodes.begin(); it != trackNodes.end(); ++it ){
        if(it->second == NULL)
            continue;
        delete it->second;
    }
}

void TDB::getUsedTileList(QMap<int, QPair<int, int>*> &tileList, int radius, int step){
    if (!loaded) return;
    
    QMap<int, QPair<int, int>*> tileList2;
    for (int i = 0; i < trackNodes.size(); i++ ) {
        if(trackNodes[i] == NULL)
            continue;
        if(trackNodes[i]->typ == 1)
            continue;
        if(tileList2[trackNodes[i]->uid.tileX*10000 + trackNodes[i]->uid.tileZ] == NULL)
            tileList2[trackNodes[i]->uid.tileX*10000 + trackNodes[i]->uid.tileZ] = new QPair<int, int>(trackNodes[i]->uid.tileX, trackNodes[i]->uid.tileZ);
        }
    
    QMapIterator<int, QPair<int, int>*> i(tileList2);
    int x, z;
    radius *= step;
    qDebug() << "radius" << radius;
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL)
            continue;
        x = i.value()->first;
        z = i.value()->second;
        for(int i = -radius; i <= radius; i+=step)
            for(int j = -radius; j <= radius; j+=step){
                if(tileList[(x+i)*10000+(z+j)] == NULL){
                    tileList[(x+i)*10000+(z+j)] = new QPair<int, int>(x+i, z+j);
                }
            }
    }
}

void TDB::checkDatabase(){
    // Snapshot cold validation policy before this potentially long operation.
    Game::loadAllWFiles = Settings::boolean("core.route.loading.preloadAllWorldFiles");
    Game::autoFix = Settings::boolean("core.route.validation.autoFix");
    // Variables
    QHash<int, QVector<WorldObj*>> objects;

    float *drawPosition = new float[7];
    bool isPosition = false;

    // Build WorldFile data
    if(Game::loadAllWFiles){
        Game::currentRoute->fillWorldObjectsByTrackItemIds(objects, tdbId);
    }
        

    for (int i = 0; i < this->iTRitems; i++) {
        if(trackItems[i] == NULL) 
            continue;
        
        isPosition = false;
        
        if (trackItems[i]->type != "emptyitem"){
            QVector<int> ids;
            int id = findTrItemNodeIds(i, ids);
            if (ids.size() == 0) {
                ErrorMessage *e = new ErrorMessage(
                        ErrorMessage::Type_Error, 
                        tdbName, 
                        QString("Item has no trackNode: ") + QString::number(i) + ". Type: " + trackItems[i]->type,
                        "Interactive item is not placed on any track. Should it be removed?");
                e->setObject((GameObj*)trackItems[i]);
                ErrorMessagesLib::PushErrorMessage(e);
                if(Game::autoFix){
                    e->type = ErrorMessage::Type_AutoFix;
                    e->action += "\nAutoFix: Item removed by TSRE.";
                    this->deleteTrItem(i);
                }
            } else if (ids.size() > 1) {
                ErrorMessage *e = new ErrorMessage(
                        ErrorMessage::Type_Error, 
                        tdbName, 
                        QString("Item referenced in more than one trackNode: ") + QString::number(i) + ". Type: " + trackItems[i]->type,
                        "Interactive item is placed on more than one track.\nMay cause fatal errors and Open Rails crash. ");
                e->setObject((GameObj*)trackItems[i]);
                ErrorMessagesLib::PushErrorMessage(e);
                if(Game::autoFix){
                    e->type = ErrorMessage::Type_AutoFix;
                    e->action += "\nAutoFix: Item removed by TSRE.";
                    this->deleteTrItem(i);
                }
            }
            
            if(id >= 0 ){
                isPosition = getDrawPositionOnTrNode(drawPosition, id, trackItems[i]->getTrackPosition());
                if(!isPosition){
                    ErrorMessage *e = new ErrorMessage(
                            ErrorMessage::Type_Error, 
                            tdbName, 
                            QString("Item has no position: ") + QString::number(i) + ". Type: " + trackItems[i]->type,
                            "Item is placed on a track but at incorrect position. Should it be removed?");
                    e->setObject((GameObj*)trackItems[i]);
                    ErrorMessagesLib::PushErrorMessage(e);
                    if(Game::autoFix){
                        e->type = ErrorMessage::Type_AutoFix;
                        e->action += "\nAutoFix: Item removed by TSRE.";
                        this->deleteTrItem(i);
                    }
                }
            }
        }
        
        if(trackItems[i]->type == "signalitem"){
            if(trackItems[i]->trSignalDirs == 1){
                int jid = trackItems[i]->trSignalDir[0];
                TRnode* n = trackNodes[jid];
                if(n == NULL) { 
                    ErrorMessage *e = new ErrorMessage(
                            ErrorMessage::Type_Error, 
                            tdbName, 
                            QString("Signal Link broken. Must be linked to junction, but no track found: ") + QString::number(i) + ". Type: " + trackItems[i]->type
                            );
                    e->setObject((GameObj*)trackItems[i]);
                    if(isPosition)
                        e->setLocationXYZ(drawPosition[5], drawPosition[6], drawPosition[0], drawPosition[1], drawPosition[2]);
                    ErrorMessagesLib::PushErrorMessage(e);
                    if(Game::autoFix){
                        e->type = ErrorMessage::Type_AutoFix;
                        e->action += "\nAutoFix: Broken Signal Link removed by TSRE.";
                        trackItems[i]->trSignalDirs = 0;
                        trackItems[i]->trSignalDir = NULL;
                        trackItems[i]->trSignalRDir = NULL;
                    }
                }
                if(n->typ != 2) {
                    ErrorMessage *e = new ErrorMessage(
                            ErrorMessage::Type_Error, 
                            tdbName, 
                            QString("Signal Link broken. Id: ") + QString::number(i) + ". Type: " + trackItems[i]->type,
                            "Signal must be linked to a junction, but other trackNode was found. \nFatal error. Causes Open Rails to crash."
                            );
                    e->setObject((GameObj*)trackItems[i]);
                    if(isPosition)
                        e->setLocationXYZ(drawPosition[5], drawPosition[6], drawPosition[0], drawPosition[1], drawPosition[2]);
                    ErrorMessagesLib::PushErrorMessage(e);
                    if(Game::autoFix){
                        e->type = ErrorMessage::Type_AutoFix;
                        e->action += "\nAutoFix: Broken Signal Link removed by TSRE.";
                        trackItems[i]->trSignalDirs = 0;
                        trackItems[i]->trSignalDir = NULL;
                        trackItems[i]->trSignalRDir = NULL;
                    }
                }
            }
        }
        
        if(trackItems[i]->type == "crossoveritem"){
            if(trackItems[i]->crossoverTrItemData == NULL){
                    ErrorMessage *e = new ErrorMessage(
                            ErrorMessage::Type_Error, 
                            tdbName, 
                            QString("CrossoverItem no data: ") + QString::number(i) + ". Type: " + trackItems[i]->type,
                            "Item broken. Should be removed."
                            );
                    e->setObject((GameObj*)trackItems[i]);
                    if(isPosition)
                        e->setLocationXYZ(drawPosition[5], drawPosition[6], drawPosition[0], drawPosition[1], drawPosition[2]);
                    ErrorMessagesLib::PushErrorMessage(e);
                    if(Game::autoFix){
                        e->type = ErrorMessage::Type_AutoFix;
                        e->action += "\nAutoFix: Item removed by TSRE.";
                        this->deleteTrItem(i);
                    }
            } else {
                int iid1 = trackItems[i]->crossoverTrItemData[0];
                if(trackItems[iid1]->crossoverTrItemData == NULL){
                    ErrorMessage *e = new ErrorMessage(
                                ErrorMessage::Type_Error, 
                                tdbName, 
                                QString("CrossoverItem second item does not match: ") + QString::number(i) + ". Type: " + trackItems[i]->type
                                );
                        e->setObject((GameObj*)trackItems[i]);
                        if(isPosition)
                            e->setLocationXYZ(drawPosition[5], drawPosition[6], drawPosition[0], drawPosition[1], drawPosition[2]);
                        ErrorMessagesLib::PushErrorMessage(e);
                    if(Game::autoFix){
                        e->type = ErrorMessage::Type_AutoFix;
                        e->action += "\nAutoFix: Item removed by TSRE.";
                        this->deleteTrItem(i);
                    }
                } else {
                    int iid2 = trackItems[iid1]->crossoverTrItemData[0];
                    if(iid2 != i){
                        ErrorMessage *e = new ErrorMessage(
                                ErrorMessage::Type_Error, 
                                tdbName, 
                                QString("CrossoverItems data does not match: ") + QString::number(i) + ". Type: " + trackItems[i]->type
                                );
                        e->setObject((GameObj*)trackItems[i]);
                        if(isPosition)
                            e->setLocationXYZ(drawPosition[5], drawPosition[6], drawPosition[0], drawPosition[1], drawPosition[2]);
                        ErrorMessagesLib::PushErrorMessage(e);
                    if(Game::autoFix){
                        e->type = ErrorMessage::Type_AutoFix;
                        e->action += "\nAutoFix: Item removed by TSRE.";
                        this->deleteTrItem(i);
                    }
                    }
                }
            }
            
            // delete all crossoveritems
            //this->deleteTrItem(i);
        }
        
        if(Game::loadAllWFiles){
            if (trackItems[i]->type == "crossoveritem" || trackItems[i]->type == "emptyitem"){
                if(objects[i].size() > 0){
                    ErrorMessage *e = new ErrorMessage(
                            ErrorMessage::Type_Error, 
                            tdbName, 
                            QString("Item wrongly referenced in W files. Id: ") + QString::number(i) + ". Type: " + trackItems[i]->type
                            );
                    e->setObject((GameObj*)trackItems[i]);
                    if(isPosition)
                        e->setLocationXYZ(drawPosition[5], drawPosition[6], drawPosition[0], drawPosition[1], drawPosition[2]);
                    ErrorMessagesLib::PushErrorMessage(e);
                    if(Game::autoFix){
                        e->type = ErrorMessage::Type_AutoFix;
                        e->action += "\nAutoFix: Item removed by TSRE.";
                        this->deleteTrItem(i);
                    }
                }
            } else {
                if(objects[i].size() == 0){
                    ErrorMessage *e = new ErrorMessage(
                            ErrorMessage::Type_Error, 
                            tdbName, 
                            QString("Item not referenced in W files. Id: ") + QString::number(i) + ". Type: " + trackItems[i]->type,
                            "Interactive Item was not found in World Tile database. \n"
                            "Enable 'TrackDB Items' View. "
                            "Jump to it's location and check if it should be removed, "
                            "or if World File is broken."
                            );
                    e->setObject((GameObj*)trackItems[i]);
                    if(isPosition)
                        e->setLocationXYZ(drawPosition[5], drawPosition[6], drawPosition[0], drawPosition[1], drawPosition[2]);
                    ErrorMessagesLib::PushErrorMessage(e);
                    if(Game::autoFix){
                        e->type = ErrorMessage::Type_AutoFix;
                        e->action += "\nAutoFix: Item removed by TSRE.";
                        this->deleteTrItem(i);
                    }
                }
            }
        }
    }

        /*if(Game::loadAllWFiles){
            if(Game::currentRoute == NULL)
                continue;
            
            QVector<WorldObj*> objects;
            int tdbId = 0;
            if(this->road)
                tdbId = 1;
            Game::currentRoute->fillWorldObjectsByTrackItemId(objects, tdbId, i);
            
            if (trackItems[i]->type == "crossoveritem" || trackItems[i]->type == "emptyitem"){
                if(objects.size() > 0){
                    ErrorMessage *e = new ErrorMessage("error", "TrackDB", QString("Track Item wrongly referenced in W files: ") + i + " type: " + trackItems[i]->type );
                    ErrorMessagesLib::PushErrorMessage(e);
                }
            } else {
                if(objects.size() == 0){
                    ErrorMessage *e = new ErrorMessage("error", "TrackDB", QString("Track Item not referenced in W files: ") + i + " type: " + trackItems[i]->type );
                    ErrorMessagesLib::PushErrorMessage(e);
                }
            }
        }*/
        /*if(trackItems[i]->trSignalDir != NULL){
            for(int j = 0; j < trackItems[i]->trSignalDirs*4; j+=4){
                tid = trackItems[i]->trSignalDir[j+0];
                n = trackNodes[tid];
                if(n == NULL){
                    
                }
            }
        }*/
    
    for(int i = 1; i <= iTRnodes; i++){
        TRnode* n = trackNodes[i];
        if (n == NULL) 
            continue;
        if (n->typ == -1) 
            continue;
        if (n->typ == 1) {
            if(n->iTrv == 0){
                ErrorMessage *e = new ErrorMessage(
                    ErrorMessage::Type_Error, 
                    tdbName, 
                    QString("TrackNode: ") + QString::number(i) + ". Trvectorsection is empty." 
                );
                ErrorMessagesLib::PushErrorMessage(e);
            }
        }
        if (n->typ == 2) {
            int originId = n->pins[0].link;
            if(originId == 0){
                    ErrorMessage *e = new ErrorMessage(
                        ErrorMessage::Type_Warning, 
                        tdbName, 
                        QString("TrackNode: ") + QString::number(i) + ". Junction origin linked to nothing.",
                        "Junction is not finished. Junction origin must be linked to a track section, otherwise OR simulation will crash."
                    );
                    e->setLocationXYZ(n->uid.tileX, n->uid.tileZ, n->uid.x, n->uid.y, n->uid.z);
                    ErrorMessagesLib::PushErrorMessage(e);
            } else {
                TRnode *origin = trackNodes[originId];
                if(origin == NULL) {
                    ErrorMessage *e = new ErrorMessage(
                        ErrorMessage::Type_Error, 
                        tdbName, 
                        QString("TrackNode: ") + QString::number(i) + ". Junction origin linked to a NULL TrackNode.",
                        "There might be a fatal error inside Track Database."
                    );
                    e->setLocationXYZ(n->uid.tileX, n->uid.tileZ, n->uid.x, n->uid.y, n->uid.z);
                    ErrorMessagesLib::PushErrorMessage(e);
                } else if(!origin->isLikedTo(i)){
                    ErrorMessage *e = new ErrorMessage(
                        ErrorMessage::Type_Error, 
                        tdbName, 
                        QString("TrackNode: ") + QString::number(i) + ". Link Error.",
                        QString("Fatal Track Database error. Junction origin linked to TrackNode ") + QString::number(originId) + ", but this TrackNode is not linked to this Junction."
                    );
                    e->setLocationXYZ(n->uid.tileX, n->uid.tileZ, n->uid.x, n->uid.y, n->uid.z);
                    ErrorMessagesLib::PushErrorMessage(e);
                } else if(origin->typ == 2){
                    ErrorMessage *e = new ErrorMessage(
                        ErrorMessage::Type_Error, 
                        tdbName, 
                        QString("TrackNode: ") + QString::number(i) + ".  Junction linked to another Junction.",
                        QString("Fatal Track Database error. Junction origin linked to another Junction origin. ") + QString::number(originId)
                    );
                    e->setLocationXYZ(n->uid.tileX, n->uid.tileZ, n->uid.x, n->uid.y, n->uid.z);
                    ErrorMessagesLib::PushErrorMessage(e);
                }
            }
        }
        /*for(int j = 0; j < trackNodes[i]->iTri; j++){
            if(j > 0 && old > trackItems[trackNodes[i]->trItemRef[j]]->getTrackPosition())
                qDebug() << "--fail!--"<< old << trackItems[trackNodes[i]->trItemRef[j]]->getTrackPosition();
            old = trackItems[trackNodes[i]->trItemRef[j]]->getTrackPosition();
        }*/
    }
    //
    //ErrorMessage *e = new ErrorMessage();
    //ErrorMessagesLib::PushErrorMessage(e);
}
