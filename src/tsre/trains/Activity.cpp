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
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/trains/Consist.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/Game.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/renderer/SelectionId.h>
#include <QDebug>
#include <QFile>
#include <tsre/ogl/GLUU.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Traffic.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Path.h>
#include <tsre/sim/SimulationController.h>
#include <algorithm>

Activity::Activity() {
}

void Activity::init(QString route, QString name) {
    serial = 1;
    header = new ActivityHeader(route, name);
    nextServiceUID = 1;
    nextActivityObjectUID = 32768;
    playerServiceDefinition = new ActivityServiceDefinition();
    playerServiceDefinition->routePath = ContentPath::parentDirectory(path);
    playerServiceDefinition->player = true;
}

/*void Activity::setRouteContent(QVector<Path*>* p, QVector<Service*>* s, QVector<Traffic*>* t){
    Paths = p;
    Services = s;
    Traffics = t;
}*/

Activity::ActivityHeader::ActivityHeader(){
    
}

Activity::ActivityHeader::ActivityHeader(QString route, QString hname){
    routeid = route;
    name = hname;
    description = "";
    briefing = "";
    completeActivity = 0;
    type = 0;
    mode = 2;
    startTime[0] = 12;
    startTime[1] = 0;
    startTime[2] = 0;
    season = 1;
    weather = 0;
    startingSpeed = 0;
    duration[0] = 0;
    duration[1] = 0;
    difficulty = 0;
    fuelWater = 100;
    fuelCoal = 100;
    fuelDiesel = 100;
    animals = 0;
    workers = 0;
}

Activity::~Activity() {
}

Activity::Activity(QString p, QString n, bool isnew) {
    pathid = p + "/" + n;
    pathid = ContentPath::normalize(pathid);
    hashid = ContentPath::key(pathid);
    path = p;
    routeHashid = ContentPath::key(ContentPath::parentDirectory(path));
    name = n;
    nameid = n.section(".", 0, -2);
    if(!isnew){
        loaded = -1;
        load();
    } else {
        nowe = true;
        loaded = 1;
        serial = 1;
        modified = true;
    }
}

Activity::Activity(QString src, QString p, QString n, bool isnew) {
    pathid = src;
    hashid = ContentPath::key(pathid);
    path = p;
    routeHashid = ContentPath::key(ContentPath::parentDirectory(path));
    name = n;
    nameid = n.section(".",0,-2);
    loaded = -1;
    if(!isnew){
        loaded = -1;
        load();
    } else {
        nowe = true;
        loaded = 1;
        serial = 1;
        modified = true;
    }
}

void Activity::load() {

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

void Activity::parse(FileBuffer *data) {
    QString sh;
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        if (sh == ("simisa@@@@@@@@@@jinx0a0t______")) {
            continue;
        }
        if (sh == ("include")) {
            QString incPath = ParserX::GetStringInside(data);
            ParserX::SkipToken(data);
            data->insertFile(path + "/" + incPath);
            continue;
        }
        if (sh == ("tr_activity")) {
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {

                if (sh == ("include")) {
                    QString incPath = ParserX::GetStringInside(data);
                    ParserX::SkipToken(data);
                    data->insertFile(path + "/" + incPath);
                    continue;
                }
                if (sh == ("serial")) {
                    serial = ParserX::GetNumber(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("tr_activity_header")) {
                    header = new ActivityHeader();
                    header->load(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("tr_activity_file")) {

                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {

                        if (sh == ("player_service_definition")) {
                            playerServiceDefinition = new ActivityServiceDefinition();
                            playerServiceDefinition->routePath = ContentPath::parentDirectory(path);
                            playerServiceDefinition->player = true;
                            playerServiceDefinition->load(data);
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("nextserviceuid")) {
                            nextServiceUID = ParserX::GetNumber(data);
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("nextactivityobjectuid")) {
                            nextActivityObjectUID = ParserX::GetNumber(data);
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("ortsaihornatcrossings")) {
                            ortsAIHornAtCrossings = ParserX::GetNumber(data);
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("ortsaicrossinghornpattern")) {
                            ortsAICrossingHornPattern = ParserX::GetStringInside(data);
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("traffic_definition")) {
                            traffic = new TrafficDefinition();
                            traffic->name = ParserX::GetStringInside(data);
                            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {

                                if (sh == ("service_definition")) {
                                    traffic->service.push_back(ActivityServiceDefinition());
                                    traffic->service.back().routePath = ContentPath::parentDirectory(path);
                                    traffic->service.back().load(data);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                traffic->kept.push_back(ParserX::KeepBlock(data, sh));
                                continue;
                            }
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("events")) {
                            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {

                                if (sh == ("eventcategoryaction")) {
                                    event.push_back(new ActivityEvent());
                                    event.back()->category = ActivityEvent::CategoryAction;
                                    event.back()->load(data);
                                    if(event.back()->id > nextEventUID)
                                        nextEventUID = event.back()->id;
                                    event.back()->setParentActivity(this);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                if (sh == ("eventcategorylocation")) {
                                    event.push_back(new ActivityEvent());
                                    event.back()->category = ActivityEvent::CategoryLocation;
                                    event.back()->load(data);
                                    if(event.back()->id > nextEventUID)
                                        nextEventUID = event.back()->id;
                                    event.back()->setParentActivity(this);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                if (sh == ("eventcategorytime")) {
                                    event.push_back(new ActivityEvent());
                                    event.back()->category = ActivityEvent::CategoryTime;
                                    event.back()->load(data);
                                    if(event.back()->id > nextEventUID)
                                        nextEventUID = event.back()->id;
                                    event.back()->setParentActivity(this);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                keptEvents.push_back(ParserX::KeepBlock(data, sh));
                                continue;
                            }
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("platformnumpassengerswaiting")) {
                            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                                if (sh == ("platformdata")) {
                                    // The platform, then the passengers: two
                                    // statements, read in this order.
                                    const int platform = ParserX::GetNumber(data);
                                    const int passengers = ParserX::GetNumber(data);
                                    platformNumPassengersWaiting.push_back(qMakePair(platform, passengers));
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                keptPassengers.push_back(ParserX::KeepBlock(data, sh));
                                continue;
                            }
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("activityobjects")) {
                            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {

                                if (sh == ("activityobject")) {
                                    activityObjects.push_back(new ActivityObject());
                                    activityObjects.back()->load(data);
                                    activityObjects.back()->setParentActivity(this);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                keptObjects.push_back(ParserX::KeepBlock(data, sh));
                                continue;
                            }
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("activityrestrictedspeedzones")) {
                            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                                if (sh == ("activityrestrictedspeedzone")) {
                                    //restrictedSpeedZone.push_back(RestrictedSpeedZone());
                                    restrictedSpeedZone.push_back(new ActivityObject());
                                    restrictedSpeedZone.back()->setSpeedZoneData();
                                    restrictedSpeedZone.back()->load(data);
                                    restrictedSpeedZone.back()->id = restrictedSpeedZone.size()-1;
                                    restrictedSpeedZone.back()->setParentActivity(this);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                keptSpeedZones.push_back(ParserX::KeepBlock(data, sh));
                                continue;
                            }
                            ParserX::SkipToken(data);
                            continue;
                        }
                        if (sh == ("activityfailedsignals")) {
                            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                                if (sh == ("activityfailedsignal")) {
                                    activityFailedSignal.push_back(new ActivityObject());
                                    activityFailedSignal.back()->setFailedSignalData(ParserX::GetNumber(data));
                                    activityFailedSignal.back()->id = activityFailedSignal.size()-1;
                                    activityFailedSignal.back()->setParentActivity(this);
                                    ParserX::SkipToken(data);
                                    continue;
                                }
                                keptFailedSignals.push_back(ParserX::KeepBlock(data, sh));
                                continue;
                            }
                            ParserX::SkipToken(data);
                            continue;
                        }
                        keptFile.push_back(ParserX::KeepBlock(data, sh));
                        continue;
                    }
                    ParserX::SkipToken(data);
                    continue;
                }
                kept.push_back(ParserX::KeepBlock(data, sh));
                continue;
            }
            ParserX::SkipToken(data);
            continue;
        }
        qDebug() << "#ACT - undefined token: " << sh;
        ParserX::SkipToken(data);
        continue;
    }
}

void Activity::save() {
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
        qDebug() << "Cannot save activity file!";
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << toText();
    out.flush();
    file.close();
    setSaved();
    nowe = false;
    ++saves;
    // A merged overlay is kept, renamed so that Open Rails stops adding it.
    if(!mergedOverlay.isEmpty() && QFile::exists(mergedOverlay)){
        QString target = mergedOverlay + ".merged";
        for(int i = 2; QFile::exists(target); i++)
            target = mergedOverlay + ".merged." + QString::number(i);
        if(QFile::rename(mergedOverlay, target))
            emit ActivityNotifier::instance()->overlayRenamed(mergedOverlay);
    }
    mergedOverlay.clear();
    
    tpath = pathid;
    tpath.remove(tpath.length()-4, 4);
    tpath += ".asv";
    tpath = ContentPath::normalize(tpath);
    qDebug() << tpath;
    QFile::remove(tpath);
}

void Activity::setSaved() {
    modified = false;
    for(ActivityObject *o : activityObjects)
        o->setModified(false);
    for(ActivityObject *o : restrictedSpeedZone)
        o->setModified(false);
    for(ActivityObject *o : activityFailedSignal)
        o->setModified(false);
    for(ActivityEvent *e : event)
        e->setModified(false);
    if(playerServiceDefinition != NULL)
        playerServiceDefinition->setModified(false);
    if(traffic != NULL)
        for(ActivityServiceDefinition &s : traffic->service)
            s.setModified(false);
}

std::shared_ptr<TextSnapshot> Activity::snapshot() {
    auto copy = std::make_shared<TextSnapshot>();
    copy->text = toText();
    copy->unsaved = isUnSaved();
    copy->saves = saves;
    copy->pending = mergedOverlay;
    return copy;
}

void Activity::restore(const TextSnapshot &snapshot) {
    mergedOverlay = snapshot.pending;
    clearContent();
    FileBuffer *data = FileBuffer::fromText(snapshot.text);
    parse(data);
    delete data;
    setSaved();
    modified = snapshot.unsaved || snapshot.saves != saves;
}

void Activity::clearContent() {
    currentEventSelected = NULL;
    currentOutcomeSelected = NULL;
    qDeleteAll(event);
    event.clear();
    qDeleteAll(activityObjects);
    activityObjects.clear();
    qDeleteAll(restrictedSpeedZone);
    restrictedSpeedZone.clear();
    qDeleteAll(activityFailedSignal);
    activityFailedSignal.clear();
    platformNumPassengersWaiting.clear();
    delete header;
    header = NULL;
    delete playerServiceDefinition;
    playerServiceDefinition = NULL;
    delete traffic;
    traffic = NULL;
    for(QStringList *list : {&kept, &keptFile, &keptEvents, &keptObjects, &keptSpeedZones,
                             &keptFailedSignals, &keptPassengers})
        list->clear();
    serial = -1;
    nextServiceUID = -1;
    nextActivityObjectUID = -1;
    nextEventUID = 0;
    ortsAIHornAtCrossings = -9999;
    ortsAICrossingHornPattern.clear();
    isInitActivityObjects = false;
}

QString Activity::toText() {
    QString text;
    QTextStream out(&text);
    out << "SIMISA@@@@@@@@@@JINX0a0t______\n";
    out << "\n";
    out << "Tr_Activity (\n";
    if (serial >= 0) {
        out << "	Serial ( " << serial << " )\n";
    }

    if (header != NULL) {
        header->save(&out);
    }

    out << "	Tr_Activity_File (\n";
    if (playerServiceDefinition != NULL) {
        playerServiceDefinition->save(&out);
    }
    if (nextServiceUID != -1)
        out << "		NextServiceUID ( " << nextServiceUID << " )\n";
    if (nextActivityObjectUID != -1)
        out << "		NextActivityObjectUID ( " << nextActivityObjectUID << " )\n";
    if (ortsAIHornAtCrossings != -9999)
        out << "		ORTSAIHornAtCrossings ( " << ortsAIHornAtCrossings << " )\n";
    if (!ortsAICrossingHornPattern.isEmpty())
        out << "		ORTSAICrossingHornPattern ( " << ParserX::AddComIfReq(ortsAICrossingHornPattern) << " )\n";
    if (traffic != NULL){
        out << "		Traffic_Definition ( " << ParserX::AddComIfReq(traffic->name) <<"\n";
        for(int i = 0; i<traffic->service.size(); i++)
            traffic->service[i].save(&out);
        ParserX::WriteKept(&out, traffic->kept, "			");
        out << "		)\n";
    }

    if (event.size() > 0 || keptEvents.size() > 0){
        out << "		Events (\n";
        for(int i = 0; i<event.size(); i++)
            event[i]->save(&out);
        ParserX::WriteKept(&out, keptEvents, "			");
        out << "		)\n";
    }

    if (activityObjects.size() > 0 || keptObjects.size() > 0){
        out << "		ActivityObjects (\n";
        for(int i = 0; i<activityObjects.size(); i++)
            activityObjects[i]->save(&out);
        ParserX::WriteKept(&out, keptObjects, "			");
        out << "		)\n";
    }

    if (restrictedSpeedZone.size() > 0 || keptSpeedZones.size() > 0){
        out << "		ActivityRestrictedSpeedZones (\n";
        for(int i = 0; i<restrictedSpeedZone.size(); i++)
            restrictedSpeedZone[i]->save(&out);
        ParserX::WriteKept(&out, keptSpeedZones, "			");
        out << "		)\n";
    }
    if (activityFailedSignal.size() > 0 || keptFailedSignals.size() > 0){
        out << "		ActivityFailedSignals (\n";
        for(int i = 0; i<activityFailedSignal.size(); i++)
            activityFailedSignal[i]->save(&out);
        ParserX::WriteKept(&out, keptFailedSignals, "			");
        out << "		)\n";
    }
    if (platformNumPassengersWaiting.size() > 0 || keptPassengers.size() > 0){
        out << "		PlatformNumPassengersWaiting (\n";
        for(int i = 0; i<platformNumPassengersWaiting.size(); i++)
            out << "			PlatformData ( " << platformNumPassengersWaiting[i].first << " " << platformNumPassengersWaiting[i].second << " )\n";
        ParserX::WriteKept(&out, keptPassengers, "			");
        out << "		)\n";
    }
    ParserX::WriteKept(&out, keptFile, "		");
    out << "	)\n";
    ParserX::WriteKept(&out, kept, "	");

    out << ")";

    out.flush();
    return text;
}

void ActivityServiceDefinition::reloadTimetable(){
    if(trafficDefinition == NULL)
        return;
    trafficDefinition->routePath = routePath;
    trafficDefinition->reloadTimetable();
    
    reloadDefinition();
}

bool ActivityServiceDefinition::isModified(){
    if(trafficDefinition != NULL)
        if(trafficDefinition->isModified())
            return true;
    return modified;
}

void ActivityServiceDefinition::calculateTimetable(){
    ActivityTimetable *t = trafficDefinition;

    Service *srv = ActLib::GetServiceByName(name, routePath);
    if(srv == NULL)
        return;
    Consist *con = ConLib::con[ConLib::addCon(Game::root+"/TRAINS/CONSISTS/", srv->trainConfig+".con")];
    if(con == NULL)
        return;
    qDebug() << con->maxVelocity[0] << con->maxVelocity[1];
    float speed = con->maxVelocity[0];
    float acceleration = con->maxVelocity[1];
    float tdist = 0, ldist = 0, performance = 1;
    unsigned int sTime = t->time;
    
    for(int i = 0; i < t->platformStartID.size(); i++){
        tdist = t->distanceDownPath[i] - ldist;
        ldist = t->distanceDownPath[i];
        performance = efficiency[i];
        
        float accelerationTime = speed/(acceleration*performance);
        float accelerationDistance = 0.5*accelerationTime*accelerationTime*acceleration*performance;
        tdist /= 2.0;
        
        if(tdist <= accelerationDistance){
            sTime += 2*sqrt((2*tdist)/(acceleration*performance));
        } else {
            sTime += 2*accelerationTime + 2*(tdist-accelerationDistance)/(speed);
        }
        
        //sTime += tdist/(speed*performance);
        if(t->arrivalTime[i] > sTime)
            sTime = t->arrivalTime[i];
        t->setArrival(i, sTime);
        if(t->departTime[i] > sTime)
            sTime = t->departTime[i];
        t->setDepart(i, sTime);
        
    }
    modified = true;
}

void ActivityServiceDefinition::setTimetableEfficiency(int id, float val){
    if(id < 0)
        return;
    if(efficiency.size() <= id)
        return;
    efficiency[id] = val;
    modified = true;
}

void ActivityServiceDefinition::reloadDefinition(){
    Service *s = ActLib::GetServiceByName(name, routePath);
    if(s == NULL)
        return;
    ActivityTimetable *t = trafficDefinition;
    efficiency.clear();
    skipCount.clear();
    distanceDownPath.clear();
    platformStartId.clear();
    for(int i = 0; i < t->platformStartID.size(); i++){
        efficiency.push_back(s->efficiency);
        skipCount.push_back(t->skipCount[i]);
        distanceDownPath.push_back(t->distanceDownPath[i]);
        platformStartId.push_back(t->platformStartID[i]);
    }
}

void ActivityServiceDefinition::load(FileBuffer* data) {
    QString sh;
    name = ParserX::GetStringInside(data);
    if(!player)
        time = ParserX::GetNumber(data);
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        empty = false;
        if (sh == ("player_traffic_definition")) {
            trafficDefinition = new ActivityTimetable();
            trafficDefinition->routePath = routePath;
            trafficDefinition->actTimetable = true;
            trafficDefinition->load(data);
            trafficDefinition->name = name;
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("uid")) {
            this->uid = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("efficiency")) {
            this->efficiency.push_back(ParserX::GetNumber(data));
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("skipcount")) {
            this->skipCount.push_back(ParserX::GetNumber(data));
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("distancedownpath")) {
            this->distanceDownPath.push_back(ParserX::GetNumber(data));
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("platformstartid")) {
            this->platformStartId.push_back(ParserX::GetNumber(data));
            ParserX::SkipToken(data);
            continue;
        }
        kept.push_back(ParserX::KeepBlock(data, sh));
        continue;
    }
}

void ActivityServiceDefinition::save(QTextStream* out) {
    QString woff;
    
    if(empty){
        if (player){
            woff = "";
            *out << "		Player_Service_Definition ( )\n";
        } else {
            woff = "	";
            *out << woff << "		Service_Definition ( )\n";
        }
        return;
    }
    
    if (player){
        woff = "";
        *out << "		Player_Service_Definition ( "<< ParserX::AddComIfReq(name) <<"\n";
    } else {
        woff = "	";
        *out << woff << "		Service_Definition ( "<< ParserX::AddComIfReq(name) <<" "<<time<<"\n";
    }
    if(trafficDefinition != NULL && player){
        QString off = "			";
        trafficDefinition->save(out, off);
    }
    
    if(uid != -1)
        *out << woff << "			UiD ( "<<uid<<" )\n";
    // Stop by stop; a field the file lacks for a stop stays missing, so the
    // file reads back as it was read.
    const int stops = std::max({efficiency.size(), skipCount.size(),
                                distanceDownPath.size(), platformStartId.size()});
    for(int i = 0; i<stops; i++){
        if(i < efficiency.size())
            *out << woff << "			Efficiency ( "<<efficiency[i]<<" )\n";
        if(i < skipCount.size())
            *out << woff << "			SkipCount ( "<<skipCount[i]<<" )\n";
        if(i < distanceDownPath.size())
            *out << woff << "			DistanceDownPath ( "<<distanceDownPath[i]<<" )\n";
        if(i < platformStartId.size())
            *out << woff << "			PlatformStartID ( "<<platformStartId[i]<<" )\n";
    }
    ParserX::WriteKept(out, kept, woff + "			");
    *out << woff << "		)\n";
}

void ActivityServiceDefinition::setModified(bool val){
    modified = val;
    if(trafficDefinition != NULL)
        trafficDefinition->setModified(val);
}

void ActivityServiceDefinition::pushRenderItems(RenderQueue &queue, float* playerT, int renderMode) {
    quint32 selectionId = 0;
    if (renderMode == RenderQueue::RENDER_SELECTION) {
        selectionId = SelectionIdCodec::activityService(0);
    }
    if (servicePointer == NULL)
        return;
    servicePointer->pushRenderItems(queue, playerT, selectionId);
}

void ActivityServiceDefinition::updateSim(float *playerT, float deltaTime){
    if(servicePointer == NULL)
        return;
    servicePointer->updateSim(playerT, deltaTime);
}

void Activity::ActivityHeader::load(FileBuffer* data) {
    QString sh;
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {

        if (sh == ("routeid")) {
            routeid = ParserX::GetStringInside(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("name")) {
            name = ParserX::GetStringInside(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("description")) {
            description = ParserX::GetStringInside(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("briefing")) {
            briefing = ParserX::GetStringInside(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("completeactivity")) {
            completeActivity = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("type")) {
            type = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("mode")) {
            mode = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("starttime")) {
            startTime[0] = ParserX::GetNumber(data);
            startTime[1] = ParserX::GetNumber(data);
            startTime[2] = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("season")) {
            season = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("weather")) {
            weather = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("pathid")) {
            this->pathid = ParserX::GetStringInside(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("startingspeed")) {
            startingSpeed = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("duration")) {
            duration[0] = ParserX::GetNumber(data);
            duration[1] = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("difficulty")) {
            difficulty = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("animals")) {
            animals = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("workers")) {
            workers = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("fuelwater")) {
            fuelWater = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("fuelcoal")) {
            fuelCoal = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("fueldiesel")) {
            fuelDiesel = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("fuelwood")) {
            fuelWood = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("fuelsand")) {
            fuelSand = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("ortsloadstationspopulation")) {
            loadStationsPopulation = ParserX::GetStringInside(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("voltage")) {
            voltage = new int[2];
            voltage[0] = ParserX::GetNumber(data);
            voltage[1] = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        kept.push_back(ParserX::KeepBlock(data, sh));
        continue;
    }
}

void Activity::ActivityHeader::save(QTextStream* out) {
    *out << "	Tr_Activity_Header (\n";
    if (routeid.length() > 0)
    *out << "		RouteID ( " << ParserX::AddComIfReq(routeid) << " )\n";
    if (name.length() > 0)
    *out << "		Name ( " << ParserX::AddComIfReq(name) << " )\n";
    if (description.length() > 0){
    *out << "		Description ( ";
    *out << ParserX::SplitToMultiline(description, "			 ");
    *out << " )\n";
    } else {
    *out << "		Description ( )\n";
    }
    if (briefing.length() > 0){
    *out << "		Briefing ( ";
    *out << ParserX::SplitToMultiline(briefing, "			 ");
    *out << " )\n";
    } else {
    *out << "		Briefing ( )\n";
    }
    *out << "		CompleteActivity ( " << completeActivity << " )\n";
    if (type != -1)
        *out << "		Type ( " << type << " )\n";
    if (mode != -1)
        *out << "		Mode ( " << mode << " )\n";
    *out << "		StartTime ( " << startTime[0] << " " << startTime[1] << " " << startTime[2] << " )\n";
    if (season != -1)
        *out << "		Season ( " << season << " )\n";
    if (weather != -1)
        *out << "		Weather ( " << weather << " )\n";
    if (pathid.length() > 0)
    *out << "		PathID ( " << ParserX::AddComIfReq(pathid) << " )\n";
    if (startingSpeed != -1)
        *out << "		StartingSpeed ( " << startingSpeed << " )\n";
    *out << "		Duration ( " << duration[0] << " " << duration[1] << " )\n";
    if (difficulty != -1)
        *out << "		Difficulty ( " << difficulty << " )\n";
    if (animals != -1)
        *out << "		Animals ( " << animals << " )\n";
    if (workers != -1)
        *out << "		Workers ( " << workers << " )\n";
    if (voltage != NULL)
        *out << "		Voltage ( " << voltage[0] << " "<< voltage[1] <<" )\n";
    if (fuelWater != -1)
        *out << "		FuelWater ( " << fuelWater << " )\n";
    if (fuelCoal != -1)
        *out << "		FuelCoal ( " << fuelCoal << " )\n";
    if (fuelDiesel != -1)
        *out << "		FuelDiesel ( " << fuelDiesel << " )\n";
    if (fuelWood != -1)
        *out << "		FuelWood ( " << fuelWood << " )\n";
    if (fuelSand != -1)
        *out << "		FuelSand ( " << fuelSand << " )\n";
    if (!loadStationsPopulation.isEmpty())
        *out << "		ORTSLoadStationsPopulation ( " << ParserX::AddComIfReq(loadStationsPopulation) << " )\n";
    ParserX::WriteKept(out, kept, "		");
    *out << "	)\n";
}

bool Activity::isNew(){
    return nowe;
}

void Activity::initToPlay(){
    if(playerServiceDefinition == NULL)
        return;
    playerServiceDefinition->servicePointer = ActLib::GetServiceByName(playerServiceDefinition->name, ContentPath::parentDirectory(path));
    if(playerServiceDefinition->servicePointer != NULL)
        playerServiceDefinition->servicePointer->initToPlay();
}

bool Activity::isUnSaved(){
    for(int i = 0; i < activityObjects.size(); i++){
        if(activityObjects[i]->isUnSaved())
            return true;
    }
    for(int i = 0; i < event.size(); i++){
        if(event[i]->isModified())
            return true;
    }
    if(playerServiceDefinition != NULL)
        if(playerServiceDefinition->isModified())
            return true;
    if(traffic != NULL)
        for(int i = 0; i < traffic->service.size(); i++){
            if(traffic->service[i].isModified())
                return true;
        }
    for(int i = 0; i < activityFailedSignal.size(); i++){
        if(activityFailedSignal[i]->isUnSaved())
            return true;
    }
    for(int i = 0; i < restrictedSpeedZone.size(); i++){
        if(restrictedSpeedZone[i]->isUnSaved())
            return true;
    }
    return modified;
}
void Activity::setFileName(QString val){
    if(!isNew())
        return;
    nameid = val;
    name = val+".act";
    pathid = path + "/" + name;
    pathid = ContentPath::normalize(pathid);
    hashid = ContentPath::key(pathid);
    modified = true;
}

void Activity::setDisplayName(QString val){
    header->name = val;
    modified = true;
}

void Activity::setOrtsHornAtCrossigns(bool val){
    if(val)
        ortsAIHornAtCrossings = 1;
    else 
        ortsAIHornAtCrossings = -9999;
    modified = true;
}

bool Activity::isOrtsHornAtCrossigns(){
    if(ortsAIHornAtCrossings == 1)
        return true;
    return false;
}

void Activity::setDifficulty(int val){
    header->difficulty = val;
    modified = true;
}

void Activity::setDuration(int h, int m){
    header->duration[0] = h;
    header->duration[1] = m;
    modified = true;
}

// The player's timetable moves with the start (task editor 07, decided).
void Activity::setStartTime(int h, int m, int s){
    const int before = header->startTime[0]*3600 + header->startTime[1]*60 + header->startTime[2];
    header->startTime[0] = h;
    header->startTime[1] = m;
    header->startTime[2] = s;
    const int delta = h*3600 + m*60 + s - before;
    ActivityServiceDefinition *player = playerServiceDefinition;
    if(delta != 0 && player != NULL && player->trafficDefinition != NULL)
        player->trafficDefinition->setTime(player->trafficDefinition->time + delta);
    modified = true;
}

void Activity::setSeason(int val){
    header->season = val;
    modified = true;
}

void Activity::setWeather(int val){
    header->weather = val;
    modified = true;
}
    
void Activity::updateSim(float* playerT, float deltaTime){
    if(playerServiceDefinition != NULL ){
        playerServiceDefinition->updateSim(playerT, deltaTime);
    }
}

void Activity::pushRenderItems(RenderQueue &queue, float *playerT, float playerRot, int renderMode) {
    for (int i = 0; i < activityObjects.size(); i++) {
        activityObjects[i]->pushRenderItems(queue, playerT, renderMode, i);
    }

    for (int i = 0; i < activityFailedSignal.size(); i++) {
        activityFailedSignal[i]->pushRenderItems(queue, playerT, renderMode, i + 3500);
    }

    for (int i = 0; i < restrictedSpeedZone.size(); i++) {
        restrictedSpeedZone[i]->pushRenderItems(queue, playerT, renderMode, i + 3000);
    }

    for (int i = 0; i < event.size(); i++) {
        event[i]->pushRenderItem(queue, playerT, playerRot, renderMode);
    }

    // While a simulation runs the activity, its trains are where the
    // simulation has them (SimulationController), not at their starts.
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    const bool atStart = !simulation->shows(this);
    if (Service *player = playerService()) {
        const quint32 selectionId = renderMode == RenderQueue::RENDER_SELECTION
                ? SelectionIdCodec::activityService(0) : 0;
        player->pushRenderItems(queue, playerT, selectionId, 0, atStart);
    }

    for (int i : shownAITrains()) {
        Service *service = aiTrainService(i);
        const quint32 selectionId = renderMode == RenderQueue::RENDER_SELECTION
                ? SelectionIdCodec::activityService(i + 1) : 0;
        service->pushRenderItems(queue, playerT, selectionId,
                                 i == shownAITrain ? Path::LineChosenTrain : Path::LineAITrain, atStart);
    }
    if (!atStart)
        simulation->pushRenderItems(queue, playerT, renderMode);
}

QVector<int> Activity::shownAITrains(){
    QVector<int> shown;
    if(Game::playerMode || traffic == NULL || (!showAllAITrains && shownAITrain < 0))
        return shown;
    // AI trains of one service share its path and consist: drawn once, the
    // chosen train first so its style wins. One of the player's service is
    // drawn too, in its own style.
    QVector<Service*> drawn;
    QVector<int> order;
    if(shownAITrain >= 0 && shownAITrain < traffic->service.size())
        order.push_back(shownAITrain);
    if(showAllAITrains)
        for(int i = 0; i < traffic->service.size(); i++)
            if(i != shownAITrain)
                order.push_back(i);
    for(int i : order){
        Service *service = aiTrainService(i);
        if(service == NULL || drawn.contains(service))
            continue;
        drawn.push_back(service);
        shown.push_back(i);
    }
    return shown;
}
/*
void Activity::initActivityObjects(){
    Consist *e;
    for (int i = 0; i < activityObjects.size(); i++){
        e = activityObjects[i].con;
        if(e == NULL) continue;
        
        e->initOnTrack(activityObjects[i].tile, activityObjects[i].direction);
    }
}*/

void Activity::pickNewEventLocation(float* tdbPos){
    float *drawPosition = new float[7];
    TDB *tdb = Game::trackDB;
    bool ok = tdb->getDrawPositionOnTrNode(drawPosition, tdbPos[0], tdbPos[1]);
    if(!ok)
        return;
    
    if(currentEventSelected == NULL)
        return;
    
    currentEventSelected->setLocation(drawPosition[5], drawPosition[6], drawPosition[0], drawPosition[2]);

}

void Activity::pickSoundLocation(float *tdbPos){
    if(currentEventSelected == NULL || currentOutcomeSelected == NULL
            || !currentEventSelected->outcomes.contains(currentOutcomeSelected)
            || currentOutcomeSelected->category != ActivityEvent::Outcome::CategorySoundFile)
        return;
    float drawPosition[7];
    if(Game::trackDB == NULL || !Game::trackDB->getDrawPositionOnTrNode(drawPosition, tdbPos[0], tdbPos[1]))
        return;
    currentOutcomeSelected->setSoundLocation(true, drawPosition[5], drawPosition[6],
                                             drawPosition[0], drawPosition[1], drawPosition[2]);
}

bool Activity::newFailedSignalFromSelected(){
    if(Game::currentSelectedGameObj == NULL)
        return false;
    if(Game::currentSelectedGameObj->typeObj != GameObj::worldobj)
        return false;
    WorldObj *w = (WorldObj*)Game::currentSelectedGameObj;
    if(w->typeID != WorldObj::signal)
        return false;
    int tid = ((SignalObj*)w)->getTrItemId();
    if(tid < 0)
        return false;
    activityFailedSignal.push_back(new ActivityObject());
    activityFailedSignal.back()->setFailedSignalData(tid);
    activityFailedSignal.back()->id = activityFailedSignal.size()-1;
    activityFailedSignal.back()->setParentActivity(this);
    modified = true;
    return true;
}

void Activity::newSpeedZone(float *tdbPos){
    float *drawPosition = new float[7];
    TDB *tdb = Game::trackDB;
    bool ok = tdb->getDrawPositionOnTrNode(drawPosition, tdbPos[0], tdbPos[1]);
    if(!ok)
        return;
    
    restrictedSpeedZone.push_back(new ActivityObject());
    restrictedSpeedZone.back()->setSpeedZoneData(drawPosition);
    restrictedSpeedZone.back()->id = restrictedSpeedZone.size()-1;
    restrictedSpeedZone.back()->setParentActivity(this);
    modified = true;
}

void Activity::newLooseConsist(float *tdbPos){
    float *drawPosition = new float[7];
    TDB *tdb = Game::trackDB;
    //qDebug() << "tdbPos[0], tdbPos[1]" << tdbPos[0] << tdbPos[1];
    bool ok = tdb->getDrawPositionOnTrNode(drawPosition, tdbPos[0], tdbPos[1]);
    if(!ok)
        return;
    
    activityObjects.push_back(new ActivityObject(nextActivityObjectUID++));
    ActivityObject *ao = activityObjects.back();

    ao->objectType = "WagonsList";
    ao->objectTypeId = ao->WAGONLIST;
    ao->direction = 1;
    ao->tile[0] = drawPosition[5];
    ao->tile[1] = drawPosition[6];
    ao->tile[2] = drawPosition[0];
    ao->tile[3] = drawPosition[2];
    int conId = ConLib::addCon(editorConListSelected.section("/", 0, -2), editorConListSelected.section("/", -1));
    ao->con = new Consist(ConLib::con[conId]);
    ao->setParentActivity(this);
    modified = true;
}

unsigned int Activity::getSelectedCarId(){
    for(int i = 0; i < activityObjects.size(); i++){
        if(activityObjects[i]->isSelected()){
            if(activityObjects[i]->con != NULL){
                return activityObjects[i]->id*65536 + activityObjects[i]->con->engItems[activityObjects[i]->con->selectedIdx].uid;
            }
        }
    }
    return -1;
}

bool Activity::isPlayerServiceInUse(QString n){
    if(playerServiceDefinition != NULL)
        if(playerServiceDefinition->name == n)
            return true;
    return false;
}

bool Activity::isServiceInUse(QString n){
    if(playerServiceDefinition != NULL)
        if(playerServiceDefinition->name == n)
            return true;
    if(traffic != NULL)
        for(int i = 0; i < traffic->service.size(); i++){
            if(traffic->service[i].name == n)
                return true;
        }
    return false;
}

bool Activity::isTrafficInUse(QString n){
    if(traffic != NULL)
        if(traffic->name == n)
                return true;
    return false;
}

bool Activity::getCarPosition(int oid, int eid, float *posTW){
    int uid = -1;
    for(int i = 0; i < activityObjects.size(); i++)
        if(oid == activityObjects[i]->id )
            uid = i;
    if(uid < 0)
        return false;
    return activityObjects[uid]->getElementPosition(eid, posTW);
}

void Activity::createNewPlayerService(QString sName, int sTime ){
    playerServiceDefinition = new ActivityServiceDefinition();
    playerServiceDefinition->routePath = ContentPath::parentDirectory(path);
    playerServiceDefinition->player = true;
    playerServiceDefinition->empty = false;
    playerServiceDefinition->name = sName;
    playerServiceDefinition->time = sTime;
    playerServiceDefinition->uid = 0;
    playerServiceDefinition->trafficDefinition = new ActivityTimetable(sName, sTime);
    playerServiceDefinition->trafficDefinition->actTimetable = true;
    playerServiceDefinition->reloadTimetable();
    
    // Fill pathid in act header info:
    Service *s = ActLib::GetServiceByName(playerServiceDefinition->name, ContentPath::parentDirectory(path));
    if(s != NULL)
        header->pathid = s->pathId;
    
    modified = true;
}

void Activity::clearPlayerService(){
    playerServiceDefinition = NULL;
    modified = true;
}

void Activity::clearTraffic(){
    traffic = NULL;
    modified = true;
}

void Activity::createNewTrafficService(Traffic *t){
    if(t == NULL)
        return;
    modified = true;
    traffic = new TrafficDefinition();
    traffic->name = t->nameId;
    for(int i = 0; i < t->service.size(); i++){
        traffic->service.push_back(ActivityServiceDefinition());
        traffic->service.back().routePath = ContentPath::parentDirectory(path);
        traffic->service[i].empty = false;
        traffic->service[i].name = t->service[i]->name;
        traffic->service[i].time = t->service[i]->time;
        traffic->service[i].uid = nextServiceUID++;
        traffic->service[i].trafficDefinition = t->service[i];
        traffic->service[i].reloadDefinition();
    }
}

bool Activity::addAITrain(ActivityTimetable *run){
    if(run == NULL || traffic == NULL)
        return false;
    if(nextServiceUID < 0){
        nextServiceUID = playerServiceDefinition != NULL ? playerServiceDefinition->uid + 1 : 1;
        for(const ActivityServiceDefinition &train : traffic->service)
            nextServiceUID = std::max(nextServiceUID, train.uid + 1);
    }
    ActivityServiceDefinition train;
    train.routePath = ContentPath::parentDirectory(path);
    train.empty = false;
    train.name = run->name;
    train.time = run->time;
    train.uid = nextServiceUID++;
    train.trafficDefinition = run;
    train.reloadDefinition();
    traffic->service.push_back(train);
    modified = true;
    return true;
}

bool Activity::removeAITrain(int index){
    if(traffic == NULL || index < 0 || index >= traffic->service.size())
        return false;
    traffic->service.remove(index);
    if(shownAITrain == index)
        shownAITrain = -1;
    else if(shownAITrain > index)
        shownAITrain--;
    modified = true;
    return true;
}

void Activity::setTrafficName(const QString &nameId){
    if(traffic == NULL || traffic->name == nameId)
        return;
    traffic->name = nameId;
    for(ActivityServiceDefinition &train : traffic->service)
        train.trafficDefinition = NULL;
    modified = true;
}

Service *Activity::playerService(){
    if(playerServiceDefinition == NULL || playerServiceDefinition->empty)
        return NULL;
    if(playerServiceDefinition->servicePointer != NULL)
        return playerServiceDefinition->servicePointer;
    return ActLib::GetServiceByName(playerServiceDefinition->name, ContentPath::parentDirectory(path));
}

Service *Activity::aiTrainService(int index){
    if(traffic == NULL || index < 0 || index >= traffic->service.size())
        return NULL;
    return ActLib::GetServiceByName(traffic->service[index].name, ContentPath::parentDirectory(path));
}

void Activity::setFuelCoal(int val){
    header->fuelCoal = val;
    modified = true;
}

void Activity::setFuelWater(int val){
    header->fuelWater = val;
    modified = true;
}

void Activity::setFuelDiesel(int val){
    header->fuelDiesel = val;
    modified = true;
}

void Activity::setFuelWood(int val){
    header->fuelWood = val;
    modified = true;
}

void Activity::setFuelSand(int val){
    header->fuelSand = val;
    modified = true;
}

void Activity::setLoadStationsPopulation(const QString &val){
    header->loadStationsPopulation = val;
    modified = true;
}

void Activity::setOrtsHornPattern(const QString &val){
    ortsAICrossingHornPattern = val;
    modified = true;
}

void Activity::setHazardWorkers(int val){
    header->workers = val;
    modified = true;
}

void Activity::setHazardAnimals(int val){
    header->animals = val;
    modified = true;
}

void Activity::setDescription(QString val){
    header->description = val;
    modified = true;
}

void Activity::setBriefing(QString val){
    header->briefing = val;
    modified = true;
}

ActivityObject* Activity::getObjectById(int id){
    if(id < 3000){
        if(activityObjects.size() > id)
            return activityObjects[id];
    }
    id -= 3000;
    if(id < 500){
        if(restrictedSpeedZone.size() > id)
            return restrictedSpeedZone[id];
    }
    id -= 500;
    if(id < 500){
        if(activityFailedSignal.size() > id)
            return activityFailedSignal[id];
    }
    return NULL;
}

Consist* Activity::getServiceConsistById(int id){
    if(id > 0){
        Service *service = aiTrainService(id - 1);
        return service != NULL ? service->getConsistPointer() : NULL;
    }
    Service *player = playerService();
    return player != NULL ? player->getConsistPointer() : NULL;
}

QMap<int, QString> Activity::getEventIdNameList(){
    QMap<int, QString> data;
            
    for(int i = 0; i < event.size(); i++){
        data[event[i]->id] = event[i]->name;
    }
    
    return data;
}

QMap<int, QString> Activity::getServiceStationStopNameList(){
    QMap<int, QString> data; 
    if(playerServiceDefinition == NULL)
        return data;
    
    return playerServiceDefinition->getStationStopNameList();
}

QMap<int, QString> ActivityServiceDefinition::getStationStopNameList(){
    QMap<int, QString> data;
            
    TDB *tdb = Game::trackDB;
    if(tdb == NULL)
        return data;
    
    for(int i = 0; i < platformStartId.size(); i++){
        int trid = platformStartId[i];
        if(tdb->trackItems[trid] == NULL)
            return data;
        data[i] = tdb->trackItems[trid]->platformName;
    }
    return data;
}

void Activity::deleteObject(int id){
    for(int i = 0; i < activityObjects.size(); i++){
        if(activityObjects[i]->id == id){
            activityObjects.remove(i);
            modified = true;
            return;
        }
    }
}

// Failed signals and speed zones are numbered by their place in the list
// (their selection ids are 3500 + and 3000 + it): numbered again after one
// goes, so the ids stay unique.
void Activity::deleteObjectFailedSignal(int id){
    for(int i = 0; i < activityFailedSignal.size(); i++){
        if(activityFailedSignal[i]->id == id){
            activityFailedSignal.remove(i);
            break;
        }
    }
    for(int i = 0; i < activityFailedSignal.size(); i++)
        activityFailedSignal[i]->id = i;
    modified = true;
}

void Activity::deleteObjectSpeedZone(int id){
    for(int i = 0; i < restrictedSpeedZone.size(); i++){
        if(restrictedSpeedZone[i]->id == id){
            restrictedSpeedZone.remove(i);
            break;
        }
    }
    for(int i = 0; i < restrictedSpeedZone.size(); i++)
        restrictedSpeedZone[i]->id = i;
    modified = true;
}

void Activity::removeAllFailedSignals(){
    activityFailedSignal.clear();
    modified = true;
}

void Activity::removeAllSpeedZones(){
     restrictedSpeedZone.clear();
     modified = true;
}

void Activity::deleteCurrentEvent(){
    deleteEvent(currentEventSelected);
}

void Activity::deleteEvent(ActivityEvent *e){
    if(e == NULL)
        return;
    const int i = event.indexOf(e);
    if(i < 0)
        return;
    if(currentEventSelected == e){
        currentEventSelected = NULL;
        currentOutcomeSelected = NULL;
    }
    event.remove(i);
    delete e;
    modified = true;
}

// Event IDs are never reused while the activity is open: Open Rails'
// overlay files name events by ID.
ActivityEvent *Activity::newEvent(ActivityEvent::EventCategory category){
    ActivityEvent *e = new ActivityEvent(++nextEventUID, category);
    e->name = "New Event";
    e->setParentActivity(this);
    event.push_back(e);
    modified = true;
    return e;
}

QVector<ActivityServiceDefinition*> Activity::getServiceList(){
    QVector<ActivityServiceDefinition*> s;
    if(playerServiceDefinition != NULL)
        s.push_back(playerServiceDefinition);
    if(traffic == NULL)
        return s;
    Traffic *t = ActLib::GetTrafficByName(traffic->name, ContentPath::parentDirectory(path));
    if(t == NULL){
        qDebug() << "t == NULL";
        return s;
    }
    for(int i = 0; i < traffic->service.size(); i++){
        if(traffic->service[i].trafficDefinition == NULL){
            qDebug() << "trafficDefinition == NULL";
            traffic->service[i].trafficDefinition = t->getInstance(traffic->service[i].name, traffic->service[i].time);
        }
        if(traffic->service[i].trafficDefinition != NULL){
            qDebug() << "s.push_back";
            s.push_back(&traffic->service[i]);
        }
    }
    return s;
}

