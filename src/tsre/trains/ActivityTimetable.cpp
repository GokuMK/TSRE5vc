/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/trains/ActivityTimetable.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <QDebug>
#include <QTextStream>
#include <algorithm>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Path.h>

ActivityTimetable::ActivityTimetable() {
}

ActivityTimetable::ActivityTimetable(QString n, int t){
    name = n;
    time = t;
    empty = false;
}

ActivityTimetable::ActivityTimetable(const ActivityTimetable& orig) {
}

ActivityTimetable::~ActivityTimetable() {
}

void ActivityTimetable::setService(QString sn){
    name = sn;
    reloadTimetable();
    modified = true;
}

// A timetable moves with its start; stops timed 0 (untimed, as MSTS writes
// them) stay so. Without stops yet, it takes the service's.
void ActivityTimetable::setTime(int t){
    if(arrivalTime.isEmpty() && departTime.isEmpty()){
        time = t;
        reloadTimetable();
        modified = true;
        return;
    }
    const int delta = t - time;
    for(int i = 0; i < arrivalTime.size(); i++){
        const bool untimed = arrivalTime[i] == 0 && i < departTime.size() && departTime[i] == 0;
        if(!untimed)
            arrivalTime[i] += delta;
    }
    for(int i = 0; i < departTime.size(); i++){
        const bool untimed = departTime[i] == 0 && i < arrivalTime.size() && arrivalTime[i] == 0;
        if(!untimed)
            departTime[i] += delta;
    }
    time = t;
    modified = true;
}

void ActivityTimetable::setArrival(int id, int t){
    if(id < 0)
        return;
    if(arrivalTime.size() <= id)
        return;
    arrivalTime[id] = t;
    modified = true;
}

void ActivityTimetable::setDepart(int id, int t){
    if(id < 0)
        return;
    if(departTime.size() <= id)
        return;
    departTime[id] = t;
    modified = true;
}

void ActivityTimetable::reloadTimetable(){
    clear();

    Service *s = ActLib::GetServiceByName(name, routePath);
    if(s == NULL){
        qDebug() << "s == NILL " << name;
        return;
    }
    empty = false;
    for(int i = 0; i < s->stationStop.size(); i++ ){
        arrivalTime.push_back(time);
        departTime.push_back(time);
        skipCount.push_back(s->stationStop[i].skipCount);
        distanceDownPath.push_back(s->stationStop[i].distanceDownPath);
        platformStartID.push_back(s->stationStop[i].platformStartID);
    }
    modified = true;
}

void ActivityTimetable::clear(){
    empty = true;
    arrivalTime.clear();
    departTime.clear();
    skipCount.clear();
    distanceDownPath.clear();
    platformStartID.clear();
    modified = true;
}

bool ActivityTimetable::isModified(){
    return modified;
}

void ActivityTimetable::load(FileBuffer* data) {
    QString sh;
    if(actTimetable){
        name = "";
        time = ParserX::GetNumber(data);
    } else {
        name = ParserX::GetStringInside(data);
        time = ParserX::GetNumber(data);
    } 
    empty = false;
    
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {

        if (sh == ("arrivaltime")) {
            arrivalTime.push_back(ParserX::GetNumber(data));
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("departtime")) {
            departTime.push_back(ParserX::GetNumber(data));
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("skipcount")) {
            skipCount.push_back(ParserX::GetNumber(data));
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("distancedownpath")) {
            distanceDownPath.push_back(ParserX::GetNumber(data));
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("platformstartid")) {
            platformStartID.push_back(ParserX::GetNumber(data));
            ParserX::SkipToken(data);
            continue;
        }
        kept.push_back(ParserX::KeepBlock(data, sh));
        continue;
    }
}

void ActivityTimetable::setModified(bool val){
    modified = val;
}

void ActivityTimetable::save(QTextStream* out, QString off) {
    if(empty)
        return;
    if(actTimetable)
        *out << off << "Player_Traffic_Definition ( "<< time <<"\n";
    else
        *out << off << "Service_Definition ( "<< ParserX::AddComIfReq(name) << " "<< time <<"\n";

    // Stop by stop; a field the file lacks for a stop stays missing, so the
    // file reads back as it was read.
    const int stops = std::max({arrivalTime.size(), departTime.size(), skipCount.size(),
                                distanceDownPath.size(), platformStartID.size()});
    for(int i = 0; i<stops; i++){
        if(i < arrivalTime.size())
            *out << off << "	ArrivalTime ( "<<arrivalTime[i]<<" )\n";
        if(i < departTime.size())
            *out << off << "	DepartTime ( "<<departTime[i]<<" )\n";
        if(i < skipCount.size())
            *out << off << "	SkipCount ( "<<skipCount[i]<<" )\n";
        if(i < distanceDownPath.size())
            *out << off << "	DistanceDownPath ( "<<distanceDownPath[i]<<" )\n";
        if(i < platformStartID.size())
            *out << off << "	PlatformStartID ( "<<platformStartID[i]<<" )\n";
    }
    ParserX::WriteKept(out, kept, off + "	");
    *out << off << ")\n";
}