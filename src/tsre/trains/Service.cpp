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
#include <tsre/trains/Service.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/Game.h>
#include <QDebug>
#include <QFile>
#include <QSet>
#include <tsre/ogl/GLUU.h>
#include <tsre/tdb/TDB.h>
#include <routeEditor/AboutWindow.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Consist.h>
#include <tsre/map/MapFeatures.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/ServiceStops.h>
#include <tsre/renderer/RenderQueue.h>

Service::Service(QString p, QString n, bool nowe) {
    pathid = p + "/" + n;
    pathid = ContentPath::normalize(pathid);
    hashid = ContentPath::key(pathid);
    path = p;
    routeHashid = ContentPath::key(ContentPath::parentDirectory(path));
    name = n;
    nameId = n.section(".", 0, -2);
    if(!nowe){
        loaded = -1;
        load();
    } else {
        maxWheelAcceleration = 0;
        efficiency = 0.75;
        startingSpeed = 0;
        endingSpeed = 0;
        startInWorld = 0;
        endInWorld = 0;
        loaded = 1;
        serial = 1;
        displayName = nameId;
        modified = true;
    }
}

Service::Service(const Service& orig) {
}

Service::~Service() {
}

void Service::load(){
    pathid = ContentPath::normalize(pathid);
    hashid = ContentPath::key(pathid);
    qDebug() << pathid;
    QFile *file = new QFile(pathid);
    if (!file->open(QIODevice::ReadOnly)) {
        qDebug() << pathid << "not exist";
        return;
    }

    FileBuffer* data = ReadFile::read(file);
    file->close();
    data->toUtf16();
    data->skipBOM();
    parse(data);
    delete data;
    loaded = 1;
}

void Service::parse(FileBuffer *data){
    QString sh;
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        if (sh == ("simisa@@@@@@@@@@jinx0v0t______")) {
            continue;
        }
        if (sh == ("service_definition")) {
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                if (sh == ("serial")) {
                    serial = ParserX::GetNumber(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("name")) {
                    displayName = ParserX::GetStringInside(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("train_config")) {
                    trainConfig = ParserX::GetStringInside(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("pathid")) {
                    pathId = ParserX::GetStringInside(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("maxwheelacceleration")) {
                    maxWheelAcceleration = ParserX::GetNumber(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("efficiency")) {
                    efficiency = ParserX::GetNumber(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("timetable")) {
                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                        if (sh == ("startingspeed")) {
                            startingSpeed = ParserX::GetNumber(data);
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("endingspeed")) {
                            endingSpeed = ParserX::GetNumber(data);
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("startinworld")) {
                            startInWorld = ParserX::GetNumber(data);
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("endinworld")) {
                            endInWorld = ParserX::GetNumber(data);
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("stationstop")) {
                            stationStop.push_back(StationStop());
                            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                                if (sh == ("platformstartid")) {
                                    stationStop.back().platformStartID = ParserX::GetNumber(data);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                if (sh == ("distancedownpath")) {
                                    stationStop.back().distanceDownPath = ParserX::GetNumber(data);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                if (sh == ("skipcount")) {
                                    stationStop.back().skipCount = ParserX::GetNumber(data);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                stationStop.back().kept.push_back(ParserX::KeepBlock(data, sh));
                            }
                            ParserX::SkipToken(data);
                            continue;
                        }
                        keptTimetable.push_back(ParserX::KeepBlock(data, sh));
                    }
                    ParserX::SkipToken(data);
                    continue;
                }
                kept.push_back(ParserX::KeepBlock(data, sh));
            }
            ParserX::SkipToken(data);
            continue;
        }
        qDebug() << "#SRV - undefined token: " << sh;
        ParserX::SkipToken(data);
    }
}

std::shared_ptr<TextSnapshot> Service::snapshot(){
    auto copy = std::make_shared<TextSnapshot>();
    copy->text = toText();
    copy->unsaved = isModified();
    copy->saves = saves;
    return copy;
}

void Service::restore(const TextSnapshot &snapshot){
    const QString oldPath = pathId, oldConsist = trainConfig;
    displayName.clear();
    trainConfig.clear();
    pathId.clear();
    maxWheelAcceleration = efficiency = startingSpeed = endingSpeed = startInWorld = endInWorld = 0;
    stationStop.clear();
    kept.clear();
    keptTimetable.clear();
    serial = -1;
    FileBuffer *data = FileBuffer::fromText(snapshot.text);
    parse(data);
    delete data;
    // The train shown on its path is made again when they changed.
    if(pathId != oldPath || trainConfig != oldConsist){
        pathPointer = NULL;
        conPointer = NULL;
    }
    modified = snapshot.unsaved || snapshot.saves != saves;
}

void Service::save(){
    QString tpath;
    
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    tpath = path+"/"+name;
    tpath = ContentPath::normalize(tpath);
    qDebug() << tpath;
    QFile file(tpath);

    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)){
        qDebug() << "Cannot save service file!";
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << toText();
    out.flush();
    file.close();
    modified = false;
    ++saves;
}

QString Service::toText(){
    QString text;
    QTextStream out(&text);
    out << "SIMISA@@@@@@@@@@JINX0v0t______\n";
    out << "\n";
    out << "Service_Definition (\n";
    if (serial >= 0) {
        out << "	Serial ( " << serial << " )\n";
    }
    out << "	Name ( "<<ParserX::AddComIfReq(displayName)<<" )\n";
    out << "	Train_Config ( "<<ParserX::AddComIfReq(this->trainConfig)<<" )\n";
    out << "	PathID ( "<<ParserX::AddComIfReq(this->pathId)<<" )\n";
    out << "	MaxWheelAcceleration ( "<<this->maxWheelAcceleration<<" )\n";
    out << "	Efficiency ( "<<this->efficiency<<" )\n";
    out << "	TimeTable (\n";
    out << "		StartingSpeed ( "<<this->startingSpeed<<" )\n";
    out << "		EndingSpeed ( "<<this->endingSpeed<<" )\n";
    out << "		StartInWorld ( "<<this->startInWorld<<" )\n";
    out << "		EndInWorld ( "<<this->endInWorld<<" )\n";
    for(int i = 0; i < stationStop.size(); i++){
        out << "		StationStop (\n";
        out << "			PlatformStartID ( "<<stationStop[i].platformStartID<<" )\n";
        out << "			DistanceDownPath ( "<<stationStop[i].distanceDownPath<<" )\n";
        out << "			SkipCount ( "<<stationStop[i].skipCount<<" )\n";
        ParserX::WriteKept(&out, stationStop[i].kept, "			");
        out << "		)\n";
    }
    ParserX::WriteKept(&out, keptTimetable, "		");
    out << "	)\n";
    ParserX::WriteKept(&out, kept, "	");
    out << ")\n";
    out.flush();
    return text;
}

void Service::pushRenderItems(RenderQueue &queue, float* playerT, quint32 selectionId, int pathStyle, bool consist) {
    initToPlay();
    if (pathPointer != NULL)
        pathPointer->pushRenderItems(queue, playerT, selectionId, pathStyle);
    if (consist && conPointer != NULL)
        conPointer->pushRenderItemsOnTrack(queue, playerT, selectionId);
}

void Service::getMapFeatures(MapFeatures &consist, int tileX, int tileZ) {
    initToPlay();
    if (conPointer != NULL)
        conPointer->getMapFeatures(consist, tileX, tileZ);
}

Path *Service::getPathPointer() {
    initToPlay();
    return pathPointer;
}

void Service::initToPlay(){
    if(pathPointer == NULL)
        pathPointer = ActLib::GetPathByName(pathId, ContentPath::parentDirectory(path));
    if(pathPointer == NULL){
        QDir dir(Game::root + "/ROUTES/" + Game::route + "/PATHS");
        int pathPointerId;
        qDebug() << "pathid" << (pathPointerId = ActLib::AddPath(dir.path(), pathId+".pat"));
        pathPointer = ActLib::Paths[pathPointerId];
    }
    if(conPointer == NULL){
        QDir dir(Game::root + "/TRAINS/CONSISTS/");
        int conPointerId;
        qDebug() << "conid" << (conPointerId =  ConLib::addCon(dir.path(), trainConfig+".con"));
        conPointer = new Consist(ConLib::con[conPointerId], true);
        conPointer->initOnTrack(pathPointer->getStartPositionTXZ(), pathPointer->getStartDirection(), pathPointer->getJunctionDirections(), true);
    }
}

void Service::pathChanged(Path *path){
    if(path == NULL || pathPointer != path || conPointer == NULL)
        return;
    float start[4];
    conPointer->initOnTrack(pathPointer->getStartPositionTXZ(start), pathPointer->getStartDirection(), pathPointer->getJunctionDirections(), true);
}

Consist *Service::getConsistPointer(){
    return conPointer;
}
void Service::updateSim(float* playerT, float deltaTime){
    if(pathPointer == NULL || conPointer == NULL){
        return;
    }
    conPointer->updateSim(deltaTime);
}

bool Service::isModified(){
    return modified;
}

void Service::setNameId(QString val){
    nameId = val;
    name = val+".srv";
    pathid = ContentPath::join(path, name);
    hashid = ContentPath::key(pathid);
    modified = true;
}

void Service::setNewPath(QString pathName){
    const QVector<StationStop> before = stationStop;
    stationStop.clear();
    pathId = pathName;
    modified = true;
    // Every platform of the new path a stop; a stop at the same pass of a
    // platform as before keeps its times in the activities.
    const auto follow = [&]{
        QVector<int> from(stationStop.size(), -1);
        QVector<bool> used(before.size(), false);
        QHash<int, int> seenAfter;
        for(int j = 0; j < stationStop.size(); j++){
            const int platform = ServiceStops::Platform(stationStop[j].platformStartID);
            const int pass = seenAfter[platform]++;
            int seenBefore = 0;
            for(int i = 0; i < before.size(); i++){
                if(ServiceStops::Platform(before[i].platformStartID) != platform)
                    continue;
                if(seenBefore++ == pass && !used[i]){
                    from[j] = i;
                    used[i] = true;
                    break;
                }
            }
        }
        ServiceStops::Follow(this, before, from);
    };
    if(pathId.length() <= 0){
        follow();
        return;
    }
    qDebug() << "new path:" << pathId;
    Path *p = ActLib::GetPathByName(pathId, ContentPath::parentDirectory(path));
    if(p == NULL){
        qDebug() << "null path";
        follow();
        return;
    }
    p->init3dShapes(false);

    // A stop at each platform, at the first of its passes (a switchback
    // passes one twice); its SkipCount the pass's place.
    QSet<int> stopped;
    for(int ii = 0; ii < p->pathObjects.size(); ii++){
        const Path::PathObject *i = p->pathObjects[ii];
        if(i == NULL || stopped.contains(i->platform))
            continue;
        stopped.insert(i->platform);
        stationStop.push_back(StationStop());
        stationStop.back().platformStartID = i->trItemId;
        stationStop.back().distanceDownPath = i->distanceDownPath;
        stationStop.back().skipCount = ii;
    }
    modified = true;
    follow();
}

void Service::disableStationStop(int count){
    for(int ii = 0; ii < stationStop.size(); ii++ ){
        if(stationStop[ii].skipCount == count){
            const QVector<StationStop> before = stationStop;
            stationStop.remove(ii);
            modified = true;
            QVector<int> from;
            for(int i = 0; i < before.size(); i++)
                if(i != ii)
                    from.push_back(i);
            ServiceStops::Follow(this, before, from);
            return;
        }
    }
}

void Service::setDisplayName(QString val){
    displayName = val;
    modified = true;
}
void Service::setEfficiency(float val){
    efficiency = val;
    modified = true;
}

void Service::setStartSpeed(float val){
    startingSpeed = val;
    modified = true;
}
    
void Service::setTrainConfig(QString val){
    trainConfig = val;
    modified = true;
}

void Service::setEndSpeed(float val){
    endingSpeed = val;
    modified = true;
}
    
void Service::enableStationStop(int count){
    for(int i = 0; i < stationStop.size(); i++ ){
        if(stationStop[i].skipCount == count){
            return;            
        }
    }
    Path *p = ActLib::GetPathByName(pathId, ContentPath::parentDirectory(path));
    if(p == NULL){
        return;
    }
    p->init3dShapes(false);
    if(count < 0 || count >= p->pathObjects.size() || p->pathObjects[count] == NULL)
        return;
    
    const QVector<StationStop> before = stationStop;
    float dist = p->pathObjects[count]->distanceDownPath;
    int i;
    for(i = 0; i < stationStop.size(); i++ ){
        if(stationStop[i].distanceDownPath > dist){
            break;            
        }
    }
    stationStop.insert(i, StationStop());
    stationStop[i].distanceDownPath = p->pathObjects[count]->distanceDownPath;
    stationStop[i].platformStartID = p->pathObjects[count]->trItemId;
    stationStop[i].skipCount = count;
    modified = true;
    QVector<int> from;
    for(int j = 0; j < stationStop.size(); j++)
        from.push_back(j < i ? j : (j == i ? -1 : j - 1));
    ServiceStops::Follow(this, before, from);
}
QStringList Service::followPath(Path *path){
    QStringList report;
    if(path == NULL || !path->isNamed(pathId) || stationStop.isEmpty())
        return report;
    const QVector<Path::PathObject> &old = path->previousPasses();
    QVector<Path::PathObject> now;
    for(const Path::PathObject *pass : path->pathObjects)
        now.push_back(*pass);
    const QVector<int> oldPass = ServiceStops::Passes(stationStop, old);
    const QVector<StationStop> before = stationStop;
    QVector<StationStop> after;
    QVector<int> from;
    QStringList removed;
    for(int i = 0; i < before.size(); i++){
        StationStop stop = before[i];
        const int p = oldPass[i];
        if(p < 0){
            // Not at a platform the path passed: left as it is.
            after.push_back(stop);
            from.push_back(i);
            continue;
        }
        // Which pass of its platform the stop is at, before and now.
        int pass = 0;
        for(int v = 0; v < p; v++)
            pass += old[v].platform == old[p].platform ? 1 : 0;
        int q = -1;
        for(int v = 0, seen = 0; v < now.size() && q < 0; v++)
            if(now[v].platform == old[p].platform && seen++ == pass)
                q = v;
        if(q < 0){
            removed << old[p].name;
            continue;
        }
        float distance = now[q].distanceDownPath;
        if(now[q].meets(stop.platformStartID, &distance)){
            stop.distanceDownPath = distance;
        } else {
            stop.platformStartID = now[q].trItemId;
            stop.distanceDownPath = now[q].distanceDownPath;
        }
        stop.skipCount = q;
        after.push_back(stop);
        from.push_back(i);
    }
    // In the order the path passes them.
    QVector<int> order(after.size());
    for(int j = 0; j < order.size(); j++)
        order[j] = j;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b){
        return after[a].distanceDownPath < after[b].distanceDownPath;
    });
    QVector<StationStop> sorted;
    QVector<int> sortedFrom;
    for(int j : order){
        sorted.push_back(after[j]);
        sortedFrom.push_back(from[j]);
    }
    bool same = sorted.size() == before.size();
    for(int j = 0; same && j < sorted.size(); j++)
        same = sorted[j].platformStartID == before[j].platformStartID
                && sorted[j].skipCount == before[j].skipCount
                && std::fabs(sorted[j].distanceDownPath - before[j].distanceDownPath) < 0.01f;
    if(same)
        return report;
    stationStop = sorted;
    modified = true;
    if(removed.isEmpty())
        report << QString(
            //% "%1: its %2 stops follow the path."
            qtTrId("activity.follow.service.stops")).arg(nameId).arg(sorted.size());
    else
        report << QString(
            //% "%1: its stops follow the path; the path no longer passes %2, those stops are removed."
            qtTrId("activity.follow.service.removed")).arg(nameId, removed.join(", "));
    const QStringList users = ServiceStops::Follow(this, before, sortedFrom);
    if(!users.isEmpty())
        report << QString(
            //% "%1: the timetables of %2 follow."
            qtTrId("activity.follow.users")).arg(nameId, users.join(", "));
    return report;
}
