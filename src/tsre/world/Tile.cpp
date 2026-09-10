/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/Tile.h>
#include <memory>
#include <tsre/Game.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/world/objects/WorldObj.h>
#include <tsre/world/objects/StaticObj.h>
#include <tsre/world/objects/DynTrackObj.h>
#include <tsre/world/objects/ForestObj.h>
#include <tsre/world/objects/TransferObj.h>
#include <tsre/world/objects/TrackObj.h>
#include <tsre/world/objects/SpeedpostObj.h>
#include <tsre/world/objects/SignalObj.h>
#include <tsre/world/objects/PlatformObj.h>
#include <tsre/world/objects/TrWatermarkObj.h>
#include <tsre/world/objects/LevelCrObj.h>
#include <tsre/world/objects/PickupObj.h>
#include <tsre/ogl/GLUU.h>
#include <QString>
#include <QStringList>
#include <QDebug>
#include <QFile>
#include <tsre/ogl/GLUU.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/fileFunctions/TS.h>
#include <tsre/geo/GeoCoordinates.h>
#include <tsre/world/objects/GroupObj.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/ErrorMessage.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/world/Trk.h>
#include <tsre/world/Route.h>
#include <settings/SettingsAccess.h>

Tile::Tile() {
    modified = false;
    loaded = -2;
    inUse = false;
    x = 0;
    z = 0;
    jestObiektow = 0;
    vDbIdCount = 0;
}

Tile::Tile(int xx, int zz) {
    modified = false;
    loaded = -2;
    inUse = false;
    x = xx;
    z = zz;
    jestObiektow = 0;
    vDbIdCount = 0;
    load();
}

Tile::Tile(int xx, int zz, FileBuffer *data) {
    modified = false;
    loaded = -2;
    inUse = false;
    x = xx;
    z = zz;
    qDebug() << xx << zz;
    jestObiektow = 0;
    vDbIdCount = 0;
    loadUtf16Data(data);
    qDebug() << obiekty.size();
    loaded = 0;
    wczytajObiekty();
}

void Tile::initNew(){
    loaded = 1;
    jestObiektow = 0;
    vDbIdCount = 0;
    inUse = false;
    obiekty.clear();
}

Tile::Tile(const Tile& orig) {
}

Tile::~Tile() {
}

QString Tile::getNameXY(int e) {
    QString n = "+";
    if (e < 0) {
        n = "-";
        e = -e;
    }
    QString s = "000000" + QString::number(e);
    return n + s.right(6);
}

void Tile::wczytajObiekty() {
    for (auto it = obiekty.begin(); it != obiekty.end(); ++it) {
        //console.log(obj.type);
        WorldObj* obj = (WorldObj*) it->second;
        if(obj == NULL) 
            continue;
        //if(obj->typeID != obj->carspawner)
        //    continue;
        obj->load(x, z);
        if(!obj->isSoundItem()){
            if(obj->UiD < 1000000)
                if(obj->UiD > maxUiD) maxUiD = obj->UiD;
        } else {
            if(obj->UiD < 1000000)
                if(obj->UiD > maxUiDWS) maxUiDWS = obj->UiD;
        }
        
        obj->loadingFixes();
    }
    //qDebug() << "ok";
    loaded = 1;
    //save();
}

void Tile::checkForErrors(){
    const bool autoFix = Settings::boolean("core.route.validation.autoFix");
    const QStringList objectsToRemove =
            Settings::stringList("core.editing.objectsToRemove");
    if(Game::trackDB == NULL)
        return;
    
    for (auto it = obiekty.begin(); it != obiekty.end(); ++it) {
        WorldObj* obj = (WorldObj*) it->second;
        if(obj == NULL) 
            continue;
        ErrorMessage* e = obj->checkForErrors();
        if(e != NULL)
            if(autoFix){
                if(e->type == ErrorMessage::Type_Error){
                    e->type = ErrorMessage::Type_AutoFix;
                    e->action += "\nAutoFix: Object removed by TSRE.";
                    obj->loaded = false;
                    obj->modified = true;
                }
            }
        
        // Remove all objects by type
        if(!objectsToRemove.isEmpty()){
            foreach (QString val, objectsToRemove){
                if(val.trimmed().isEmpty())
                    continue;
                if(obj->type == val){
                    obj->loaded = false;
                    modified = true;
                }
            }
        }
    }
    
}

void Tile::selectObjectsByXYRange(QVector<GameObj*>& objects, int minx, int maxx, int minz, int maxz){
    for (auto it = obiekty.begin(); it != obiekty.end(); ++it) {
        WorldObj* obj = (WorldObj*) it->second;
        if(obj == NULL) 
            continue;
        if(!obj->loaded) 
            continue;
        if(obj->position[0] >= minx && obj->position[0] <= maxx && obj->position[2] >= minz && obj->position[2] <= maxz)
            objects.push_back((GameObj*)obj);
    }
}

// Use this function to init W file if loaded before route data.
void Tile::loadInit(){
    for (auto it = obiekty.begin(); it != obiekty.end(); ++it) {
        WorldObj* obj = (WorldObj*) it->second;
        if(obj == NULL) 
            continue;
        obj->loadInit();
    }
    
    checkForErrors();
}

void Tile::updateTrackSectionInfo(QHash<unsigned int, unsigned int> shapes, QHash<unsigned int, unsigned int> sect){
    int count = 0;
    for (auto it = obiekty.begin(); it != obiekty.end(); ++it) {
        WorldObj* obj = (WorldObj*) it->second;
        if(obj == NULL) 
            continue;
        count += obj->updateTrackSectionInfo(shapes, sect);
    }
    if(count > 0)
        modified = true;
}

void Tile::replaceWorldObj(WorldObj *nowy){
    nowy->load(x, z);
    
    if(Game::serverClient == NULL)
        nowy->setModified();
    
    for (int i = 0; i < jestObiektow; i++) {
        //console.log(obj.type);
        WorldObj* obj = obiekty[i];
        if(obj == NULL) 
            continue;
        if(obj->UiD == nowy->UiD && obj->typeID == nowy->typeID){
            //qDebug() << "replace obj";
            obiekty[i] = nowy;
            return;
        }
    }
    if(nowy->isSoundItem()){
        if(nowy->UiD > maxUiDWS)
            maxUiDWS = nowy->UiD;
    } else {
        if(nowy->UiD > maxUiD)
            maxUiD = nowy->UiD;
    }
    obiekty[jestObiektow++] = nowy;
}

void Tile::load() {

    QString sh;
    QString path;
    path = Game::root + "/routes/" + Game::route + "/world/w" + getNameXY(x) + "" + getNameXY(-z) + ".w";
    path.replace("//", "/");
    
    QFile *file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)){
        qDebug() << "W file: not exist " << path;
        return;
    }
    FileBuffer* data = ReadFile::read(file);

    data->off = 32;
    if (!data->isBinarySimis()){
        qDebug() << "w file uncompressed " << path;
        data->off = 0;
        ParserX::NextLine(data);
        while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
            if(sh == "tr_worldfile"){
                loadUtf16Data(data);
                ParserX::SkipToken(data);
                continue;
            }
            qDebug() << "#Tile - undefined token" << sh;
            ParserX::SkipToken(data);
        }
    } else {
        QString error;
        const bool parsed = loadBinaryData(data, false, &error);
        if (!error.isEmpty())
            qWarning() << path << error;
        if (!parsed) {
            delete data;
            delete file;
            return;
        }
    }
    qDebug() << obiekty.size();
    loaded = 0;
    wczytajObiekty();
    checkForErrors();
    loadWS();
    file->close();
    delete data;
    delete file;
}

bool Tile::loadBinaryData(FileBuffer* data, bool sound, QString* error) {
    // Parse object state only. Asset/GL loading is performed by the caller.
    BinaryLoadState& state = sound ? soundBinaryLoadState : worldBinaryLoadState;
    state = BinaryLoadState::Failed;
    if (error)
        error->clear();
    const QString fileKind = sound ? QStringLiteral("WS") : QStringLiteral("W");
    QStringList diagnostics;
    const int firstObject = jestObiektow;

    try {
        data->off = 32;
        const auto root = data->readBlock();
        const auto expected = sound ? TS::Tr_Worldsoundfile : TS::Tr_Worldfile;
        if (root.id != expected)
            throw FileBuffer::ParseError("Unexpected world-file root token");
        FileBuffer::ScopedLimit rootScope(*data, root.end);
        data->skipLabel();
        state = BinaryLoadState::Complete;

        while (data->off < root.end) {
            const int blockStart = data->off;
            FileBuffer::Block block;
            try {
                block = data->readBlock();
            } catch (const FileBuffer::ParseError& failure) {
                state = BinaryLoadState::Recovered;
                diagnostics.push_back(QStringLiteral(
                    "%1 top-level framing error at byte %2; parsing stopped after %3 completed object(s): %4")
                    .arg(fileKind).arg(blockStart).arg(jestObiektow - firstObject)
                    .arg(QString::fromLatin1(failure.what())));
                break;
            }

            try {
                FileBuffer::ScopedLimit blockScope(*data, block.end);
                if (!sound && block.id == TS::ViewDbSphere) {
                    data->skipLabel();
                    ViewDbSphere sphere{};
                    while (data->off < block.end) {
                        const auto id = data->getToken();
                        sphere.set(id, data);
                    }
                    viewDbSphere.push_back(sphere);
                } else if (!sound && block.id == TS::VDbIdCount) {
                    data->skipLabel();
                    const int count = data->getInt();
                    vDbIdCount = count;
                } else if (!sound && block.id == TS::Tr_Watermark) {
                    data->skipLabel();
                    std::unique_ptr<WorldObj> watermark(new TrWatermarkObj(data->getInt()));
                    obiekty[jestObiektow++] = watermark.release();
                } else {
                    std::unique_ptr<WorldObj> object(WorldObj::createObj(block.id));
                    if (object) {
                        data->skipLabel();
                        while (data->off < block.end) {
                            const auto child = data->readBlock();
                            FileBuffer::ScopedLimit childScope(*data, child.end);
                            object->set(child.id, data);
                            data->off = child.end;
                        }
                        obiekty[jestObiektow++] = object.release();
                    }
                }
            } catch (const FileBuffer::ParseError& failure) {
                state = BinaryLoadState::Recovered;
                diagnostics.push_back(QStringLiteral(
                    "%1 block %2 at byte %3 was discarded; parsing continued at byte %4: %5")
                    .arg(fileKind).arg(TS::describe(block.id)).arg(blockStart)
                    .arg(block.end).arg(QString::fromLatin1(failure.what())));
            }
            data->off = block.end;
        }

        if (error)
            *error = diagnostics.join(QStringLiteral("\n"));
        return true;
    } catch (const FileBuffer::ParseError& failure) {
        // Root validation happens before any tile state is published.
        state = BinaryLoadState::Failed;
        if (error)
            *error = QStringLiteral("%1 root parse failed at byte %2: %3")
                    .arg(fileKind).arg(data->off)
                    .arg(QString::fromLatin1(failure.what()));
        return false;
    }
}

Tile::BinaryLoadState Tile::binaryLoadState(bool sound) const {
    return sound ? soundBinaryLoadState : worldBinaryLoadState;
}

void Tile::loadUtf16Data(FileBuffer *data){
    QString sh = "";
    WorldObj* nowy;
                while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                    //qDebug() << sh;
                    if (sh == "tr_watermark") {
                        nowy = (WorldObj*)(new TrWatermarkObj((int)ParserX::GetNumber(data)));
                        obiekty[jestObiektow++] = nowy;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if (sh == "vdbidcount") {
                        vDbIdCount = ParserX::GetNumber(data);
                        //viewDbSphere = new ViewDbSphere[vDbIdCount];
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if (sh == "viewdbsphere") {
                        viewDbSphere.push_back(ViewDbSphere());
                        while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                            viewDbSphere.back().set(sh, data);
                            ParserX::SkipToken(data);
                        }
                        ParserX::SkipToken(data);
                        continue;
                    }
                    if ((nowy = WorldObj::createObj(sh)) != NULL) {
                        //qDebug() << nowy->type;
                        while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                            nowy->set(sh, data);
                            ParserX::SkipToken(data);
                        }
                        obiekty[jestObiektow++] = nowy;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    qDebug() << "#tr_worldfile - undefined token " << sh;
                    ParserX::SkipToken(data);
                }
    return;
}

void Tile::loadWS() {

    QString sh;
    QString path;
    path = Game::root + "/routes/" + Game::route + "/world/w" + getNameXY(x) + "" + getNameXY(-z) + ".ws";
    path.replace("//", "/");
    
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)){
        //qDebug() << "ws file not exist    " << path;
        return;
    }
    FileBuffer* data = ReadFile::read(&file);
    //qDebug() << "Date:" << data->length;
    //data->off = 0;
    //for(int i = 0; i < 64; i++){
    //    data->off = i;
    //    qDebug() << (char)data->get()<<"-"<<data->get();
    //}
    data->off = 32;
    if (!data->isBinarySimis()){
        qDebug() << "ws file uncompressed " << path;
        data->off = 0;
        ParserX::NextLine(data);
    
        QString sh = "";
        while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
            if(sh == "tr_worldsoundfile"){
                WorldObj* nowy;
                while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                    if ((nowy = WorldObj::createObj(sh)) != NULL) {
                        //qDebug() << nowy->type;
                        while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                            nowy->set(sh, data);
                            ParserX::SkipToken(data);
                        }
                        nowy->load(x, z);
                        if(nowy->UiD < 1000000)
                            if(nowy->UiD > maxUiDWS) maxUiDWS = nowy->UiD;
                        obiekty[jestObiektow++] = nowy;
                        ParserX::SkipToken(data);
                        continue;
                    }
                    qDebug() << "#tr_wrldsoundfile - undefined token " << sh;
                    ParserX::SkipToken(data);
                }
                ParserX::SkipToken(data);
                continue;
            }
            qDebug() << "#TileWS - undefined token" << sh;
            ParserX::SkipToken(data);
        }
    } else {
        const int firstSound = jestObiektow;
        QString error;
        const bool parsed = loadBinaryData(data, true, &error);
        if (!error.isEmpty())
            qWarning() << path << error;
        if (parsed) for (int i = firstSound; i < jestObiektow; ++i) {
            WorldObj* object = obiekty[i];
            object->load(x, z);
            if (object->UiD < 1000000 && object->UiD > maxUiDWS)
                maxUiDWS = object->UiD;
        }
    }
    delete data;
    qDebug() <<"WS size: "<< obiekty.size();
}

WorldObj* Tile::getObj(int id) {
    /*for (int i = 0; i < jestObiektow; i++) {
        if(obiekty[i] == NULL) continue;
        if (obiekty[i]->UiD == uid) {
            return obiekty[i];
        }
    }*/
    if(obiekty[id] == NULL)
        return NULL;
    return obiekty[id];
}

WorldObj* Tile::findNearestObj(float* pos) {
    WorldObj *obj = NULL;
    float dist = 99999;
    for(int i = 0; i < this->jestObiektow; i++){
        if(obiekty[i] == NULL) continue;
        float tdist = Vec3::dist(pos, obiekty[i]->position);
        if(tdist < dist){
            obj = obiekty[i];
            dist = tdist;
        }
    }
    return obj;
}

void Tile::ViewDbSphere::set(QString sh, FileBuffer* data){
    if (sh == ("vdbid")) {
        vDbId = ParserX::GetUInt(data);
        return;
    }
    if (sh == ("position")) {
        position[0] = ParserX::GetNumber(data);
        position[1] = ParserX::GetNumber(data);
        position[2] = ParserX::GetNumber(data);
        return;
    }
    if (sh == ("radius")) {
        radius = ParserX::GetNumber(data);
        return;
    }
    if (sh == ("viewdbsphere")) {
        viewDbSphere.push_back(ViewDbSphere());
        while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
            viewDbSphere.back().set(sh, data);
            ParserX::SkipToken(data);
            }
        return;
    }
    qDebug() << "viewdbsphere unknown:" << sh;
}

void Tile::ViewDbSphere::set(TS::TokenId sh, FileBuffer* data){
    const int offset = data->readBlockEnd();
    FileBuffer::ScopedLimit scope(*data, offset);
    data->skipLabel();
    
    if (sh == TS::VDbId) {
        vDbId = data->getUint();
    } else if (sh == TS::Radius) {
        radius = data->getFloat();
    } else if (sh == TS::Position) {
        position[0] = data->getFloat();
        position[1] = data->getFloat();
        position[2] = data->getFloat();
    } else if(sh == TS::ViewDbSphere){
        viewDbSphere.push_back(ViewDbSphere());
        TS::TokenId idx;
        while (data->off < offset) {
            idx = data->getToken();
            viewDbSphere.back().set(idx, data);
        }
    } else {
        qDebug() << "viewdbsphere unknown:" << sh;
    }
    data->off = offset;
}

void Tile::ViewDbSphere::save(QTextStream* out, const QString offset){
*(out) << offset+"ViewDbSphere (\n";
*(out) << offset+"	VDbId ( "<<this->vDbId<<" )\n";
*(out) << offset+"	Position ( "<<this->position[0]<<" "<<this->position[1]<<" "<<this->position[2]<<" )\n";
*(out) << offset+"	Radius ( "<<this->radius<<" )\n";
for(int i = 0; i < this->viewDbSphere.size(); i++){
    this->viewDbSphere[i].save(out, offset + "	");
}
*(out) << offset+")\n";
}

void Tile::transalteObj(float px, float py, float pz, int uid) {
    for (int i = 0; i < jestObiektow; i++) {
        if(obiekty[i] == NULL) continue;
        if (obiekty[i]->UiD == uid) {
            obiekty[i]->translate(px, py, pz);
        }
    }
    modified = true;
}

void Tile::deleteObject(WorldObj* obj){
    for(int i = 0; i < jestObiektow; i++ ){
        if(obiekty[i] == NULL) continue;
        if(obiekty[i] == obj){
            obiekty[i] = NULL;
            if(i == jestObiektow - 1)
                jestObiektow--;
            if(obj->UiD == maxUiD )
                maxUiD--;
            return;
        }
    }
}

WorldObj* Tile::placeObject(WorldObj* obj){
    if(loaded != 1) return NULL;
    obiekty[jestObiektow++] = obj;
    //qDebug() << obiekty[jestObiektow-1]->qDirection[3];
    obj->set("x", x);
    obj->set("z", z);
    if(obj->isSoundItem())
        obj->UiD = ++maxUiDWS;
    else
        obj->UiD = ++maxUiD;
    modified = true;
    obj->setModified();
    obj->setMartix();
    return obj;
}

WorldObj* Tile::placeObject(float* p, Ref::RefItem* itemData) {
    float q[4];
    q[0] = 0; 
    q[1] = 0;
    q[2] = 0;
    q[3] = 1;
    return placeObject(p, (float*)&q, itemData, NULL);
}

WorldObj* Tile::placeObject(float* p, float* q, Ref::RefItem* itemData, float* tpos) {
    if(loaded != 1) return NULL;
    if(itemData == NULL) return NULL;
    //qDebug() << pozW[0] << " " << pozW[1] << " " << pozW[2] << " " << itemData->type << " " << itemData->filename;

    WorldObj* nowy = WorldObj::createObj(itemData->type);
    if(nowy == NULL) return NULL;
    if(!nowy->allowNew()) {
        qDebug() << itemData->type << " <- object not supported yet ";
        return NULL;
    }

    QString itemShapeName = itemData->getNextShapeName();
    nowy->set("ref_class", itemData->clas);
    nowy->set("ref_filename", itemShapeName);
    nowy->set("ref_value", itemData->value);
    if(itemData->staticFlags != 0)
        nowy->set("staticflags", itemData->staticFlags);
    if(itemData->type == "dyntrack"
            && DynTrackObj::isRoadStaticFlags(itemData->staticFlags))
        nowy->setTemplate("default_road_single");

    //Quat::rotateY(q, q, M_PI/2);
    nowy->set("x", x);
    nowy->set("z", z);
    if(nowy->isTrackItem()){
        q[0] = 0;
        q[1] = 0;
        q[2] = 0;
        q[3] = 1;
        nowy->initPQ(p, q);
        nowy->initTrItems(tpos);
    } else {
        nowy->initPQ(p, q);
    }
       
    //qDebug() << maxUiD;
    if(nowy->isSoundItem())
        nowy->UiD = ++maxUiDWS;
    else
        nowy->UiD = ++maxUiD;
    qDebug() << itemData->type << " " << itemShapeName << nowy->UiD;
    //nowy->fileName = itemData->filename;
    nowy->load(x, z);

    if(itemData->randomTransformation != NULL)
        nowy->randomTransform(itemData->randomTransformation);

    obiekty[jestObiektow++] = nowy;
    //qDebug() << obiekty[jestObiektow-1]->qDirection[3];

    modified = true;
    nowy->setModified();
    return nowy;
}

void Tile::saveEmpty(int nx, int nz) {
    QString sh;
    QString path;
    path = Game::root + "/routes/" + Game::route + "/world/w" + getNameXY(nx) + "" + getNameXY(nz) + ".w";
    path.replace("//", "/");
    qDebug() << path;
    QFile file(path);
    if(file.exists()) return;
    
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)){
        qDebug() << "Error creating empty W file " << path;
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << "SIMISA@@@@@@@@@@JINX0w0t______\n";
    out << "\n";
    out << "Tr_Worldfile (\n";
    out << ")";

    file.close(); 
}

void Tile::saveToStream(QTextStream &out){
    out << "Tr_Worldfile (\n";

    for(int i = 0; i < this->jestObiektow; i++){
        if(obiekty[i] == NULL) continue;
        if(!obiekty[i]->loaded) continue;
        this->obiekty[i]->save(&out);
    }
    
    out << ")";
}

bool Tile::save() {
    if (worldBinaryLoadState != BinaryLoadState::Complete) {
        qWarning() << "Refusing to overwrite an incomplete binary W file for tile" << x << z;
        return false;
    }
    const bool deleteViewDbSpheres =
            Settings::boolean("core.editing.deleteViewDbSpheres");
    const bool sortTileObjects =
            Settings::boolean("core.editing.sortTileObjects");
    QString sh;
    QString path;
    path = Game::root + "/routes/" + Game::route + "/world/w" + getNameXY(x) + "" + getNameXY(-z) + ".w";
    path.replace("//", "/");
    qDebug() << path;
    QFile file(path);
    
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)){
        qDebug() << "Error saving W file " << path;
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << "SIMISA@@@@@@@@@@JINX0w0t______\n";
    out << "\n";
    out << "Tr_Worldfile (\n";

    QString offset = "";
    if(!deleteViewDbSpheres && this->vDbIdCount > 0){
        out << "	VDbIdCount ( "<<this->vDbIdCount<<" )\n";
        for(int i = 0; i < this->viewDbSphere.size(); i++){
            this->viewDbSphere[i].save(&out, offset + "	");
        }
    }

    if(!sortTileObjects){
        for(int i = 0; i < this->jestObiektow; i++){
            if(obiekty[i] == NULL) continue;
            if(this->obiekty[i]->isSoundItem()) continue;
                this->obiekty[i]->save(&out);
        }
    } else {
        int count = 0;
        for(int iLevel = -15; iLevel < Game::currentRoute->trk->tsreMaxStaticDetailLevel+1; iLevel++){
            // get current level count;
            count = 0;
            if( iLevel > 0 ){
                for(int i = 0; i < this->jestObiektow; i++){
                    if(obiekty[i] == NULL) continue;
                    if(this->obiekty[i]->isSoundItem()) continue;
                    if(this->obiekty[i]->getCurrentDetailLevel() == iLevel)
                        count++;
                }
                if( count > 0 ){
                    out << "	Tr_Watermark ( "<<iLevel<<" )\n";
                }
            }
            // save current level objects;
            for(int i = 0; i < this->jestObiektow; i++){
                if(obiekty[i] == NULL) continue;
                if(this->obiekty[i]->isSoundItem()) continue;
                if(this->obiekty[i]->getCurrentDetailLevel() == iLevel)
                    this->obiekty[i]->save(&out);
            }
        }
    }
    out << ")";
 
    // optional, as QFile destructor will already do it:
    file.close(); 
    return saveWS();
}

bool Tile::saveWS() {
    if (soundBinaryLoadState != BinaryLoadState::Complete) {
        qWarning() << "Refusing to overwrite an incomplete binary WS file for tile" << x << z;
        return false;
    }
    QString path;
    
    path = Game::root + "/routes/" + Game::route + "/world/w" + getNameXY(x) + "" + getNameXY(-z) + ".ws";
    path.replace("//", "/");
    qDebug() << path;
    QFile file(path);
    
    int countWS = 0;
    for(int i = 0; i < this->jestObiektow; i++){
        if(obiekty[i] == NULL) continue;
        if(this->obiekty[i]->isSoundItem() && this->obiekty[i]->loaded) countWS++;
    }
    qDebug() << countWS;
    if(countWS == 0){
        qDebug() << "delete ws file if exist";
        if (!file.exists())
            return true;
        if (!file.remove()) {
            qWarning() << "Error deleting empty WS file " << path;
            return false;
        }
        return true;
    }
    
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)){
        qDebug() << "Error saving WS file " << path;
        return false;
    }   
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << "SIMISA@@@@@@@@@@JINX0W0t______\n";
    out << "\n";
    out << "Tr_Worldsoundfile (\n";
    for(int i = 0; i < this->jestObiektow; i++){
        if(obiekty[i] == NULL) continue;
        if(!this->obiekty[i]->isSoundItem()) continue;
            this->obiekty[i]->save(&out);
    }
    out << ")";
 
    file.close();
    return true;
}

bool Tile::isModified(){
    bool value = this->modified;
    
    if(value == false)
        for (int i = 0; i < jestObiektow; i++) {
            if(obiekty[i] == NULL) continue;
            if(obiekty[i]->modified)
                return true;
            }
    
    return value;
}


void Tile::setModified(bool value){
    this->modified = value;
    
    if(value == false){
        for (int i = 0; i < jestObiektow; i++) {
            if(obiekty[i] == NULL) continue;
            obiekty[i]->modified = false;
        }
    }
}

void Tile::fillWorldObjectsByTrackItemId(QVector<WorldObj*>& objects, int tdbId, int id){
    for (int i = 0; i < jestObiektow; i++) {
        if(obiekty[i] == NULL) continue;
        
        if(obiekty[i]->containsTrackItem(tdbId, id))
            objects.push_back(obiekty[i]);
    }
}

void Tile::fillWorldObjectsByTrackItemIds(QHash<int,QVector<WorldObj*> > &objects, int tdbId){
    for (int i = 0; i < jestObiektow; i++) {
        if(obiekty[i] == NULL) continue;
        
        QVector<int> ids;
        obiekty[i]->getTrackItemIds(ids, tdbId);
        foreach(int i, ids){
            objects[i].push_back(obiekty[i]);
        }
    }
}

void Tile::findSimilar(WorldObj* obj, GroupObj* group){
    for (int i = 0; i < jestObiektow; i++) {
        if(obiekty[i] == NULL) continue;
        if (obiekty[i]->loaded)
            if(obiekty[i]->isSimilar(obj))
                group->addObject(obiekty[i]);
    }
}

void Tile::render() {
    //render(0, 0);
}

void Tile::updateTerrainObjects(){
    for (int i = 0; i < jestObiektow; i++) {
        if(obiekty[i] == NULL) continue;
        if (obiekty[i]->loaded)
            if(obiekty[i]->typeID == WorldObj::forest || obiekty[i]->typeID == WorldObj::transfer)
               obiekty[i]->deleteVBO();
    }
}

float Tile::getNearestSnapablePosition(float* pos, float *quat, int uid){
    QVector<float> points;
    for (int i = 0; i < jestObiektow; i++) {
        if(obiekty[i] == NULL) continue;
        if (obiekty[i]->loaded && obiekty[i]->snapable) {
            obiekty[i]->insertSnapablePoints(points);                
        }
    }
    float distance = 9999;
    float t;
    float tpos[3];
    float tquat[4];
    int nrp = -1;
    for (int i = 0, j = 0; i < points.size(); i+=11, j++){
        if(points[i+10] == uid) continue;
        //qDebug() << points[i+3] << points[i+4] << points[i+5];
        t = Vec3::dist(pos, (float*)&points[i+3]);
        //qDebug() << t;
        if(t < distance){
            distance = t;
            nrp = j%2;
            Vec3::copy(tpos, (float*)&points[i+3]);
            //Vec3::add(tpos, tpos, (float*)&points[i]);
            //if(j%2 == 0)
            //    Vec3::sub(tpos, tpos, (float*)&points[i+11]);
            //else
            //    Vec3::sub(tpos, tpos, (float*)&points[i-11]);
            Quat::copy(tquat, (float*)&points[i+6]);
        }
    }
    if(distance < Game::snapableRadius){
        Vec3::copy(pos, tpos);
        if(quat != NULL)
            Quat::copy(quat, tquat);
        if(quat != NULL)
            Quat::copy(quat, tquat);
    }
    return nrp;
}

void Tile::updateSim(float deltaTime){
    if (loaded != 1) return;
    for (int i = 0; i < jestObiektow; i++) {
        if(obiekty[i] == NULL) continue;
        if (obiekty[i]->loaded) {
            obiekty[i]->updateSim(deltaTime);
        }
    }
}

void Tile::pushRenderItems(float* playerT, float* playerW, float* target, float fov, int renderMode){
    if (loaded != 1) return;
    quint32 selectionId = 0;
    float lodx, lodz, lod;

    // Transfers first within this tile; preserve object indices for picking.
    for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < jestObiektow; i++) {
        const auto found = obiekty.find(i);
        if (found == obiekty.end() || !found->second) continue;
        WorldObj *obj = found->second;
        if ((obj->typeID == WorldObj::transfer) != (pass == 0)) continue;

        if (obj->loaded) {
            lodx = (x - playerT[0])*2048 + obj->position[0] - playerW[0];
            lodz = (z - playerT[1])*2048 + obj->position[2] - playerW[2];
            //console.log(this.x);
            lod = (float) sqrt(lodx * lodx + lodz * lodz);
            if (lod < Game::objectLod || obj->isInternalLodControl()) {
                Game::currentRenderer->mvPushMatrix();
                //obiekty[i]->render(gluu, lod, x-playerT[0]*2048, z-playerT[1]*2048);
                if (renderMode == Game::currentRenderer->RENDER_SELECTION) {
                    selectionId = SelectionIdCodec::worldObject(
                                x - static_cast<int>(playerT[0]),
                                z - static_cast<int>(playerT[1]), i);
                }
                obj->pushRenderItems(lod, lodx, lodz, playerW, target, fov, selectionId);
                Game::currentRenderer->mvPopMatrix();
            }
        }
    }
}

void Tile::render(float * playerT, float* playerW, float* target, float fov, int renderMode) {
    if (loaded != 1) return;
    GLUU* gluu = GLUU::get();
    //gl.activeTexture(gl.TEXTURE0);
    //gluu->setMatrixUniforms();
    //this.obiekty.forEach(function(obj) {
    quint32 selectionId = 0;
    float lodx, lodz, lod;
    // Same ordering as the queued renderer, without duplicating draw logic.
    for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < jestObiektow; i++) {
        const auto found = obiekty.find(i);
        if (found == obiekty.end() || !found->second) continue;
        WorldObj *obj = found->second;
        if ((obj->typeID == WorldObj::transfer) != (pass == 0)) continue;
        if (obj->loaded) {
            lodx = (x - playerT[0])*2048 + obj->position[0] - playerW[0];
            lodz = (z - playerT[1])*2048 + obj->position[2] - playerW[2];
            //console.log(this.x);
            lod = (float) sqrt(lodx * lodx + lodz * lodz);
            if (lod < Game::objectLod || obj->isInternalLodControl()) {
                gluu->mvPushMatrix();
                //obiekty[i]->render(gluu, lod, x-playerT[0]*2048, z-playerT[1]*2048);
                if (renderMode == gluu->RENDER_SELECTION) {
                    selectionId = SelectionIdCodec::worldObject(
                                x - static_cast<int>(playerT[0]),
                                z - static_cast<int>(playerT[1]), i);
                }
                obj->render(gluu, lod, lodx, lodz, playerW, target, fov, selectionId, renderMode);
                //obiekty[i]->render(gluu);
                gluu->mvPopMatrix();
            }
        }
    }
    
}
/*
Tile.prototype.getObjHash = function(UiD) {
    for(int objK : obiekty.keySet()){
     Obj o = obiekty.get(objK);
     if( o==null || !o.loaded) continue;
     if(o.UiD == UiD) return objK;
     }
    return 0;
};
 */
