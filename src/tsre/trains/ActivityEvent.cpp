/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/trains/ActivityEvent.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/Game.h>
#include <tsre/map/MapFeatures.h>
#include <tsre/renderer/Renderer.h>
#include <QDebug>
#include <tsre/ogl/GLUU.h>
#include <tsre/tdb/TDB.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/ogl/TextObj.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/world/OrtsWeatherChange.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/trains/Activity.h>
#include <tsre/world/objects/PlatformObj.h>

OglObj *ActivityEvent::simpleMarkerObj = NULL;

QMap<ActivityEvent::EventType, QString> ActivityEvent::EventTypeDescription = {
    //% "None."
    { ActivityEvent::EventTypeNone, QT_TRID_NOOP("activity.event.type.none") },
    //% "Time Event."
    { ActivityEvent::EventTypeTime, QT_TRID_NOOP("activity.event.type.time") },
    //% "Location Event."
    { ActivityEvent::EventTypeLocation, QT_TRID_NOOP("activity.event.type.location") },
    //% "Stop at final station."
    { ActivityEvent::EventTypeAllstops, QT_TRID_NOOP("activity.event.type.all.stops") },
    //% "Pick up Wagons."
    { ActivityEvent::EventTypePickupWagons, QT_TRID_NOOP("activity.event.type.pickup.wagons") },
    //% "Assemble Train"
    { ActivityEvent::EventTypeAssembleTrain, QT_TRID_NOOP("activity.event.type.assemble.train") },
    //% "Assemble Train at location."
    { ActivityEvent::EventTypeAssembleTrainAtLocation, QT_TRID_NOOP("activity.event.type.assemble.at.location") },
    //% "Drop off cars at location."
    { ActivityEvent::EventTypeDropoffWagonsAtLocation, QT_TRID_NOOP("activity.event.type.dropoff.at.location") },
    //% "Pick up Passengers."
    { ActivityEvent::EventTypePickupPassengers, QT_TRID_NOOP("activity.event.type.pickup.passengers") },
    //% "Reach Speed."
    { ActivityEvent::EventTypeReachSpeed, QT_TRID_NOOP("activity.event.type.reach.speed") },
    //% "Make a pickup."
    { ActivityEvent::EventTypePickUp, QT_TRID_NOOP("activity.event.type.make.pickup") }
};

QMap<ActivityEvent::EventType, QString> ActivityEvent::EventTypeName = {
    { ActivityEvent::EventTypeNone ,"" },
    { ActivityEvent::EventTypeTime ,"EventTypeTime" },
    { ActivityEvent::EventTypeLocation ,"EventTypeLocation" },
    { ActivityEvent::EventTypeAllstops ,"EventTypeAllStops" },
    { ActivityEvent::EventTypePickupWagons ,"EventTypePickUpWagons" },
    { ActivityEvent::EventTypeAssembleTrain ,"EventTypeAssembleTrain" },
    { ActivityEvent::EventTypeAssembleTrainAtLocation ,"EventTypeAssembleTrainAtLocation" },
    { ActivityEvent::EventTypeDropoffWagonsAtLocation ,"EventTypeDropOffWagonsAtLocation" },
    { ActivityEvent::EventTypePickupPassengers ,"EventTypePickUpPassengers" },
    { ActivityEvent::EventTypeReachSpeed ,"EventTypeReachSpeed" },
    { ActivityEvent::EventTypePickUp ,"EventTypePickUp" }
};

QMap<QString, ActivityEvent::EventType> ActivityEvent::EventNameType = {
    { "", ActivityEvent::EventTypeNone },
    { "eventtypetime", ActivityEvent::EventTypeTime },
    { "eventtypelocation", ActivityEvent::EventTypeLocation },
    { "eventtypeallstops", ActivityEvent::EventTypeAllstops },
    { "eventtypepickupwagons", ActivityEvent::EventTypePickupWagons },
    { "eventtypeassembletrain", ActivityEvent::EventTypeAssembleTrain },
    { "eventtypeassembletrainatlocation", ActivityEvent::EventTypeAssembleTrainAtLocation },
    { "eventtypedropoffwagonsatlocation", ActivityEvent::EventTypeDropoffWagonsAtLocation },
    { "eventtypepickuppassengers", ActivityEvent::EventTypePickupPassengers },
    { "eventtypereachspeed", ActivityEvent::EventTypeReachSpeed },
    { "eventtypepickup", ActivityEvent::EventTypePickUp }
};

QMap<ActivityEvent::Outcome::OutcomeType, QString> ActivityEvent::Outcome::OutcomeTypeDescription = {
    //% "None."
    { ActivityEvent::Outcome::TypeNone, QT_TRID_NOOP("activity.outcome.type.none") },
    //% "Display a message."
    { ActivityEvent::Outcome::TypeDisplayMessage, QT_TRID_NOOP("activity.outcome.type.display.message") },
    //% "Complete Activity successfully."
    { ActivityEvent::Outcome::TypeActivitySuccess, QT_TRID_NOOP("activity.outcome.type.activity.success") },
    //% "End Activity without success."
    { ActivityEvent::Outcome::TypeActivityFail, QT_TRID_NOOP("activity.outcome.type.activity.fail") },
    //% "Increase an event's activation level."
    { ActivityEvent::Outcome::TypeIncActLevel, QT_TRID_NOOP("activity.outcome.type.increase.activation") },
    //% "Decrease an event's activation level."
    { ActivityEvent::Outcome::TypeDecActLevel, QT_TRID_NOOP("activity.outcome.type.decrease.activation") },
    //% "Restore an event's activation level."
    { ActivityEvent::Outcome::TypeRestorAactLevel, QT_TRID_NOOP("activity.outcome.type.restore.activation") },
    //% "Activate an event."
    { ActivityEvent::Outcome::TypeActivateEvent, QT_TRID_NOOP("activity.outcome.type.activate.event") },
    //% "Start ignoring speed limits."
    { ActivityEvent::Outcome::TypeStartIgnoringSpeedLimits, QT_TRID_NOOP("activity.outcome.type.start.ignore.speed.limits") },
    //% "Stop ignoring speed limits."
    { ActivityEvent::Outcome::TypeStopIgnoringSpeedLimits, QT_TRID_NOOP("activity.outcome.type.stop.ignore.speed.limits") },
    //% "Play sound from file."
    { ActivityEvent::Outcome::TypeORTSActSoundFile, QT_TRID_NOOP("activity.outcome.type.play.sound") },
    //% "Change the Weather."
    { ActivityEvent::Outcome::TypeORTSWeatherChange, QT_TRID_NOOP("activity.outcome.type.change.weather") },
    //% "Restart a waiting AI train."
    { ActivityEvent::Outcome::TypeORTSRestartWaitingTrain, QT_TRID_NOOP("activity.outcome.type.restart.waiting.train") }
};

QMap<ActivityEvent::Outcome::OutcomeType, QString> ActivityEvent::Outcome::OutcomeTypeName = {
    { ActivityEvent::Outcome::TypeNone ,"" },
    { ActivityEvent::Outcome::TypeDisplayMessage ,"DisplayMessage" },
    { ActivityEvent::Outcome::TypeActivitySuccess ,"ActivitySuccess" },
    { ActivityEvent::Outcome::TypeActivityFail ,"ActivityFail" },
    { ActivityEvent::Outcome::TypeIncActLevel ,"IncActLevel" },
    { ActivityEvent::Outcome::TypeDecActLevel ,"DecActLevel" },
    { ActivityEvent::Outcome::TypeRestorAactLevel ,"RestoreActLevel" },
    { ActivityEvent::Outcome::TypeActivateEvent ,"ActivateEvent" },
    { ActivityEvent::Outcome::TypeStartIgnoringSpeedLimits ,"StartIgnoringSpeedLimits" },
    { ActivityEvent::Outcome::TypeStopIgnoringSpeedLimits ,"StopIgnoringSpeedLimits" },
    { ActivityEvent::Outcome::TypeORTSActSoundFile , "ORTSActivitySound" },
    { ActivityEvent::Outcome::TypeORTSWeatherChange , "ORTSWeatherChange" },
    { ActivityEvent::Outcome::TypeORTSRestartWaitingTrain , "ORTSRestartWaitingTrain" }
};

QMap<ActivityEvent::Outcome::OutcomeType, ActivityEvent::Outcome::OutcomeCategory> ActivityEvent::Outcome::OutcomeTypeCategory = {
    { ActivityEvent::Outcome::TypeNone , ActivityEvent::Outcome::CategoryNone },
    { ActivityEvent::Outcome::TypeDisplayMessage, ActivityEvent::Outcome::CategoryInfo },
    { ActivityEvent::Outcome::TypeActivitySuccess, ActivityEvent::Outcome::CategoryNone },
    { ActivityEvent::Outcome::TypeActivityFail, ActivityEvent::Outcome::CategoryInfo },
    { ActivityEvent::Outcome::TypeIncActLevel, ActivityEvent::Outcome::CategoryEvent },
    { ActivityEvent::Outcome::TypeDecActLevel, ActivityEvent::Outcome::CategoryEvent },
    { ActivityEvent::Outcome::TypeRestorAactLevel, ActivityEvent::Outcome::CategoryEvent },
    { ActivityEvent::Outcome::TypeActivateEvent, ActivityEvent::Outcome::CategoryEvent },
    { ActivityEvent::Outcome::TypeStartIgnoringSpeedLimits, ActivityEvent::Outcome::CategoryNone },
    { ActivityEvent::Outcome::TypeStopIgnoringSpeedLimits, ActivityEvent::Outcome::CategoryNone },
    { ActivityEvent::Outcome::TypeORTSActSoundFile , ActivityEvent::Outcome::CategorySoundFile },
    { ActivityEvent::Outcome::TypeORTSWeatherChange , ActivityEvent::Outcome::CategoryWeatherChange },
    { ActivityEvent::Outcome::TypeORTSRestartWaitingTrain , ActivityEvent::Outcome::CategoryRestartTrain }
};

QMap<QString, ActivityEvent::Outcome::OutcomeType> ActivityEvent::Outcome::OutcomeNameType = {
    { "", ActivityEvent::Outcome::TypeNone },
    { "displaymessage", ActivityEvent::Outcome::TypeDisplayMessage },
    { "activitysuccess", ActivityEvent::Outcome::TypeActivitySuccess },
    { "activityfail", ActivityEvent::Outcome::TypeActivityFail },
    { "incactlevel", ActivityEvent::Outcome::TypeIncActLevel },
    { "decactlevel", ActivityEvent::Outcome::TypeDecActLevel },
    { "restoreactlevel", ActivityEvent::Outcome::TypeRestorAactLevel },
    { "activateevent", ActivityEvent::Outcome::TypeActivateEvent },
    { "startignoringspeedlimits", ActivityEvent::Outcome::TypeStartIgnoringSpeedLimits },
    { "stopignoringspeedlimits", ActivityEvent::Outcome::TypeStopIgnoringSpeedLimits },
    { "ortsactivitysound", ActivityEvent::Outcome::TypeORTSActSoundFile },
    // TSRE's older form, read and written as ORTSActivitySound.
    { "ortsactsoundfile", ActivityEvent::Outcome::TypeORTSActSoundFile },
    { "ortsweatherchange", ActivityEvent::Outcome::TypeORTSWeatherChange },
    { "ortsrestartwaitingtrain", ActivityEvent::Outcome::TypeORTSRestartWaitingTrain }
};

ActivityEvent::ActivityEvent() {
}

ActivityEvent::ActivityEvent(int uid, EventCategory c){
    category = c;
    id = uid;
    activationLevel = 1;
    if(category == CategoryAction){
        eventType = EventTypeAllstops;
        reversableEvent = false;
    }
    if(category == CategoryLocation){
        eventType = EventTypeLocation;
        location = new float[5];
        location[0] = 0;
        location[1] = 0;
        location[2] = 0;
        location[3] = 0;
        location[4] = 10;
    }
    if(category == CategoryTime){
        eventType = EventTypeTime;
        time = 0;
    }
    
    modified = true;
}

ActivityEvent::~ActivityEvent() {
}

int ActivityEvent::newOutcome(){
    Outcome *o = new Outcome(Outcome::TypeNone);
    outcomes.push_back(o);
    modified = true;
    return outcomes.size() - 1;
}

void ActivityEvent::removeOutcome(int id){
    if(id < 0)
        return;
    if(outcomes.size() <= id)
        return;
    modified = true;
    outcomes.remove(id);
}

bool ActivityEvent::isModified(){
    for(int i = 0; i < outcomes.size(); i++)
        if(outcomes[i]->isModified())
            return true;
    return modified;
}

bool ActivityEvent::isSelected(){
    return selected;
}

bool ActivityEvent::setSelected(bool val){
    selected = val;
    return selected;
}

bool ActivityEvent::select(){
    selected = true;
    return true;
}

bool ActivityEvent::unselect(){
    selected = false;
    return false;
}

void ActivityEvent::setModified(bool val){
    for(int i = 0; i < outcomes.size(); i++)
        outcomes[i]->setModified(val);
    modified = val;
}

void ActivityEvent::setActivationLevel(int val){
    activationLevel = val;
    modified = true;
}

void ActivityEvent::setName(QString val){
    name = val;
    modified = true;
}

void ActivityEvent::setTriggeredText(QString val){
    textToDisplayOnCompletionIfTriggered = val;
    modified = true;
}

void ActivityEvent::setUntriggeredText(QString val){
    textToDisplayOnCompletionIfNotTriggered = val;
    modified = true;
}

void ActivityEvent::setLocation(int X, int Z, float x, float z){
    if(category != CategoryLocation)
        return;
    location[0] = X;
    location[1] = Z;
    location[2] = x;
    location[3] = z;
    modified = true;
}

void ActivityEvent::setNotes(QString val){
    textToDisplayDescriptionOfTask = val;
    modified = true;
}

void ActivityEvent::setSpeed(float val){
    speed = val;
    modified = true;
}

void ActivityEvent::setLocationRadius(int val){
    if(category != CategoryLocation)
        return;
    location[4] = val;
    modified = true;
}

void ActivityEvent::setLocationStop(bool val){
    if(category != CategoryLocation)
        return;
    if(val)
        triggerOnStop = 1;
    else 
        triggerOnStop = 0;
    modified = true;
}

void ActivityEvent::setReversable(bool val){
    if(category != CategoryAction)
        return;
    if(val)
        reversableEvent = true;
    else 
        reversableEvent = false;
    modified = true;
}

void ActivityEvent::setAutoContinue(int val){
    //if(val >= 0)
    ortsContinue = val;
    modified = true;
}

void ActivityEvent::setTime(int val){
    if(category != CategoryTime)
        return;
    time = val;
    modified = true;
}

int ActivityEvent::getWagonListSize(){
    return wagonList.size();
}

QString ActivityEvent::getWagonListDescription(int i){
    QString desc;
    if(i < 0 || i >= wagonList.size())
        return desc;
    if(wagonList[i].sidingItem > 1000000){
        desc += wagonList[i].description;
    } else if(Game::trackDB != NULL){
        const auto found = Game::trackDB->trackItems.find(wagonList[i].sidingItem);
        if(found != Game::trackDB->trackItems.end() && found->second != NULL)
            desc += "Siding: " + found->second->platformName;
    }
        
    return desc;
}

QString ActivityEvent::getWagonListIdDescription(int i){
    if(i < 0 || i >= wagonList.size())
        return "";
    int consistId = wagonList[i].uid / 65536;
    int wagonId = wagonList[i].uid - consistId*65536;
    QString desc = QString::number(consistId) + "-" + QString::number(wagonId);
    return desc;
}

bool ActivityEvent::Outcome::isModified(){
    return modified;
}

void ActivityEvent::Outcome::setModified(bool val){
    modified = val;
}

void ActivityEvent::pushRenderItem(RenderQueue &queue, float *playerT, float playerRot, int renderMode) {
    if (category == CategoryLocation) {
        if (!Game::viewInteractives)
            return;

        if (simpleMarkerObj == NULL) {
            simpleMarkerObj = new OglObj();
            float *punkty = new float[3 * 2];
            int ptr = 0;

            punkty[ptr++] = 0;
            punkty[ptr++] = 0;
            punkty[ptr++] = 0;
            punkty[ptr++] = 0;
            punkty[ptr++] = 30;
            punkty[ptr++] = 0;

            simpleMarkerObj->setMaterial(1.0, 0.0, 0.0);
            simpleMarkerObj->setLineWidth(4);
            simpleMarkerObj->init(punkty, ptr, RenderItem::V, GL_LINES);
            delete[] punkty;
        }

        if (txtMarkerObj == NULL) {
            txtMarkerObj = new TextObj(name, 16, 2.0);
            txtMarkerObj->setColor(0, 0, 0);
        }

        queue.pushTransform();
        float h = Game::terrainLib->getHeight(location[0], -location[1], location[2], -location[3]);
        Mat4::translate(queue.transform(), queue.transform(), location[2] + 2048 * (location[0] - playerT[0]), h, -location[3] + 2048 * (-location[1] - playerT[1]));
        simpleMarkerObj->pushRenderItem(queue);
        Mat4::translate(queue.transform(), queue.transform(), 0, 30, 0);
        txtMarkerObj->pushRenderItem(queue, playerRot);
        queue.popTransform();
    }
}

void ActivityEvent::getMapFeatures(MapFeatures &features, int tileX, int tileZ) {
    if (category != CategoryLocation || location == NULL)
        return;
    const float x = location[2] + 2048 * (location[0] - tileX);
    const float z = -location[3] + 2048 * (-location[1] - tileZ);
    features.points.insert(features.points.end(), {x, 0.0f, z});
    features.areas.insert(features.areas.end(), {x, z, location[4]});
}

void ActivityEvent::load(FileBuffer* data) {
    QString sh;
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        if(EventNameType[sh] != EventTypeNone){
            eventType = EventNameType[sh];
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("reversable_event") || sh == ("reversible_event")) {
            reversableEvent = true;
            reversibleSpelling = sh == ("reversible_event");
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("ortstriggeringtrain")) {
            triggeringService = ParserX::GetStringInside(data);
            const QString time = ParserX::GetStringInside(data);
            triggeringTime = time.isEmpty() ? -1 : time.toInt();
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("id")) {
            id = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("activation_level")) {
            activationLevel = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("outcomes")) {
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                if(Outcome::OutcomeNameType[sh] != Outcome::TypeNone){
                    outcomes.push_back(new Outcome(Outcome::OutcomeNameType[sh]));
                    outcomes.back()->load(data, sh);
                    ParserX::SkipToken(data);
                    continue;
                }

                keptOutcomes.push_back(ParserX::KeepBlock(data, sh));
                continue;
            }
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("name")) {
            name = ParserX::GetStringInside(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("time")) {
            time = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("location")) {
            location = new float[5];
            location[0] = ParserX::GetNumber(data);
            location[1] = ParserX::GetNumber(data);
            location[2] = ParserX::GetNumber(data);
            location[3] = ParserX::GetNumber(data);
            location[4] = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("triggeronstop")) {
            triggerOnStop = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("ortscontinue")) {
            ortsContinue = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("sidingitem")) {
            sidingItem = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("speed")) {
            speed = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("stationstop")) {
            stationStop = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("wagon_list")) {
            isWagonList = true;
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                if (sh == ("uid")) {
                    wagonList.push_back(WagonList());
                    wagonList.back().uid = ParserX::GetUInt(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("sidingitem")) {
                    wagonList.back().sidingItem = ParserX::GetUInt(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("description")) {
                    wagonList.back().description = ParserX::GetStringInside(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                keptWagonList.push_back(ParserX::KeepBlock(data, sh));
                continue;
            }
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("texttodisplayoncompletioniftriggered")) {
            textToDisplayOnCompletionIfTriggered = ParserX::GetStringInside(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("texttodisplayoncompletionifnottriggered")) {
            textToDisplayOnCompletionIfNotTriggered = ParserX::GetStringInside(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("texttodisplaydescriptionoftask")) {
            textToDisplayDescriptionOfTask = ParserX::GetStringInside(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("pickupidandamount")) {
            pickupIdAndAmount.push_back(ParserX::GetNumber(data));
            pickupIdAndAmount.push_back(ParserX::GetNumber(data));
            pickupIdAndAmount.push_back(ParserX::GetNumber(data));
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == "ortsactsoundfile"){
            ortsActSoundFileDeprecated.push_back(ParserX::GetStringInside(data));
            ortsActSoundFileDeprecated.push_back(ParserX::GetStringInside(data));
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == "ortsweatherchange"){
            ortsWeatherChangeDeprecated = new OrtsWeatherChange();
            ortsWeatherChangeDeprecated->load(data);
            ParserX::SkipToken(data);
            continue;
        }
        
        kept.push_back(ParserX::KeepBlock(data, sh));
        continue;
    }
    // TSRE wrote the event-level forms (Open Rails 1.2) beside its own
    // outcomes: those copies go, a weather outcome taking its values from
    // its copy when its preset is not known here.
    for(Outcome *outcome : outcomes){
        if(outcome->category == Outcome::CategorySoundFile && ortsActSoundFileDeprecated.size() >= 2
                && outcome->value.toStringList().value(0) == ortsActSoundFileDeprecated[0]
                && outcome->value.toStringList().value(1) == ortsActSoundFileDeprecated[1])
            ortsActSoundFileDeprecated.clear();
        if(outcome->category == Outcome::CategoryWeatherChange && ortsWeatherChangeDeprecated != NULL
                && !outcome->value.toString().isEmpty()
                && outcome->value.toString() == ortsWeatherChangeDeprecated->name){
            if(!outcome->applyWeatherPreset(outcome->value.toString())){
                ortsWeatherChangeDeprecated->values(outcome->weather, outcome->weatherTime);
                outcome->value.setValue(QString());
            }
            delete ortsWeatherChangeDeprecated;
            ortsWeatherChangeDeprecated = NULL;
        }
    }
}

void ActivityEvent::setTriggeringTrain(const QString &service, int time){
    triggeringService = service;
    triggeringTime = service.isEmpty() ? -1 : time;
    modified = true;
}

void ActivityEvent::save(QTextStream* out) {
    if(EventTypeName[eventType] == "")
        return;
    
    if(category == CategoryAction)
        *out << "			EventCategoryAction (\n";
    else if(category == CategoryLocation)
        *out << "			EventCategoryLocation (\n";
    else if(category == CategoryTime)
        *out << "			EventCategoryTime (\n";
    else 
        return;

    *out << "				" << EventTypeName[eventType] << " ( )\n";
    
    if(id != -99999)
        *out << "				ID ( "<<id<<" )\n";
    if(activationLevel != -99999)
        *out << "				Activation_Level ( "<<activationLevel<<" )\n";

    if(outcomes.size() > 0 || keptOutcomes.size() > 0){
        *out << "				Outcomes (\n";
        for(int i = 0; i < outcomes.size(); i++){
            outcomes[i]->save(out);
        }
        ParserX::WriteKept(out, keptOutcomes, "					");
        *out << "				)\n";
    } else {
        *out << "				Outcomes ( )\n";
    }
        

    if(reversableEvent)
        *out << "				" << (reversibleSpelling ? "Reversible_Event" : "Reversable_Event") << " ( )\n";
    
    if (textToDisplayOnCompletionIfTriggered.length() > 0){
        *out << "				TextToDisplayOnCompletionIfTriggered ( ";
        *out << ParserX::SplitToMultiline(textToDisplayOnCompletionIfTriggered, "					 ");
        *out << " )\n";
    }
    if (textToDisplayOnCompletionIfNotTriggered.length() > 0){
        *out << "				TextToDisplayOnCompletionIfNotTriggered ( ";
        *out << ParserX::SplitToMultiline(textToDisplayOnCompletionIfNotTriggered, "					 ");
        *out << " )\n";
    }
    if (textToDisplayDescriptionOfTask.length() > 0){
        *out << "				TextToDisplayDescriptionOfTask ( ";
        *out << ParserX::SplitToMultiline(textToDisplayDescriptionOfTask, "					 ");
        *out << " )\n";
    }
    
    
    if(name.length() > 0)
        *out << "				Name ( "<<ParserX::AddComIfReq(name)<<" )\n";
    if(location != NULL)
        *out << "				Location ( "<<location[0]<<" "<<location[1]<<" "<<location[2]<<" "<<location[3]<<" "<<location[4]<<" )\n";
    if(time != -99999)
        *out << "				Time ( "<<time<<" )\n";
    if(triggerOnStop != -99999)
        *out << "				TriggerOnStop ( "<<triggerOnStop<<" )\n";
    if(ortsContinue  != -99999)
        *out << "				ORTSContinue ( "<<ortsContinue<<" )\n";
    if(!triggeringService.isEmpty()){
        *out << "				ORTSTriggeringTrain ( "<<ParserX::AddComIfReq(triggeringService);
        if(triggeringTime >= 0)
            *out << " " << triggeringTime;
        *out << " )\n";
    }
    if(speed != -99999)
        *out << "				Speed ( "<<speed<<" )\n";
    if(stationStop != -99999)
        *out << "				StationStop ( "<<stationStop<<" )\n";

    if(isWagonList || wagonList.size() > 0){
        *out << "				Wagon_List (\n";
        for(int i=0; i < wagonList.size(); i++){
            *out << "					UiD ( "<<wagonList[i].uid<<" )\n";
            *out << "					SidingItem ( "<<wagonList[i].sidingItem<<" )\n";
            if(wagonList[i].sidingItem > 1000000)
            *out << "					Description ( "<<ParserX::AddComIfReq(wagonList[i].description)<<" )\n";
        }
        ParserX::WriteKept(out, keptWagonList, "					");
        *out << "				)\n";
    }
    if(sidingItem != -99999)
        *out << "				SidingItem ( "<<sidingItem<<" )\n";
    
    if(pickupIdAndAmount.size() == 3){
        *out << "				PickupIdAndAmount (\n";
        *out << "					TrItemId ( "<< pickupIdAndAmount[0] << " "<< pickupIdAndAmount[1] <<" ) "<< pickupIdAndAmount[2] << "\n";
        *out << "				)\n";
    }
    if (ortsActSoundFileDeprecated.size() > 0){
        *out << "				ORTSActSoundFile ( "<< ParserX::AddComIfReq(ortsActSoundFileDeprecated[0]) <<" "<< ParserX::AddComIfReq(ortsActSoundFileDeprecated[1]) <<" )\n";
    }
    if (ortsWeatherChangeDeprecated != NULL){
        QString off = "				";
        ortsWeatherChangeDeprecated->save(out, off);
    }
    ParserX::WriteKept(out, kept, "				");
    
    *out << "			)\n";
}

void ActivityEvent::Outcome::load(FileBuffer* data, const QString &token){
    if(category == CategoryInfo){
        value.setValue(ParserX::GetStringInside(data));
    }
    if(category == CategoryEvent){
        value.setValue(ParserX::GetNumber(data));
    }
    QString sh;
    if(category == CategorySoundFile){
        QStringList values = {QString(), QString("Everywhere")};
        if(token == "ortsactsoundfile"){
            // TSRE's older form: the file and type in the outcome.
            values[0] = ParserX::GetStringInside(data);
            values[1] = ParserX::GetStringInside(data);
        } else {
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                if(sh == "ortsactsoundfile"){
                    values[0] = ParserX::GetStringInside(data);
                    values[1] = ParserX::GetStringInside(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if(sh == "ortssoundlocation"){
                    soundAt = true;
                    soundTile[0] = ParserX::GetNumber(data);
                    soundTile[1] = ParserX::GetNumber(data);
                    for(int i = 0; i < 3; i++)
                        soundPosition[i] = ParserX::GetNumber(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                kept.push_back(ParserX::KeepBlock(data, sh));
            }
        }
        value.setValue(values);
    }
    if(category == CategoryWeatherChange){
        // Open Rails' form holds the values; TSRE's older one names a
        // preset.
        const int start = data->off;
        const QString block = ParserX::KeepBlock(data, token);
        data->off = start;
        if(block.count('(') > 1){
            static const char *names[4] = {"ortsovercast", "ortsfog", "ortsprecipitationintensity",
                                           "ortsprecipitationliquidity"};
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                int which = -1;
                for(int i = 0; i < 4; i++)
                    if(sh == names[i])
                        which = i;
                if(which < 0){
                    kept.push_back(ParserX::KeepBlock(data, sh));
                    continue;
                }
                weather[which] = ParserX::GetNumber(data);
                weatherTime[which] = ParserX::GetNumber(data);
                ParserX::SkipToken(data);
            }
        } else {
            const QString preset = ParserX::GetStringInside(data);
            if(!applyWeatherPreset(preset))
                value.setValue(preset);
        }
    }
    if(category == CategoryRestartTrain){
        while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
            if(sh == "ortswaitingtraintorestart"){
                restartService = ParserX::GetStringInside(data);
                const QString time = ParserX::GetStringInside(data);
                restartTime = time.isEmpty() ? -1 : time.toInt();
                ParserX::SkipToken(data);
                continue;
            }
            if(sh == "ortsdelaytorestart"){
                restartDelay = ParserX::GetNumber(data);
                ParserX::SkipToken(data);
                continue;
            }
            if(sh == "ortsmatchingwpdelay"){
                restartWaitingPoint = ParserX::GetNumber(data);
                ParserX::SkipToken(data);
                continue;
            }
            kept.push_back(ParserX::KeepBlock(data, sh));
        }
    }
}

bool ActivityEvent::Outcome::applyWeatherPreset(const QString &name){
    OrtsWeatherChange *preset = OrtsWeatherChange::OrtsWeatherChanges.value(name);
    if(preset == NULL)
        return false;
    preset->values(weather, weatherTime);
    value.setValue(QString());
    modified = true;
    return true;
}

void ActivityEvent::Outcome::setSoundLocation(bool at, int tileX, int tileZ, float x, float y, float z){
    soundAt = at;
    soundTile[0] = tileX;
    soundTile[1] = tileZ;
    soundPosition[0] = x;
    soundPosition[1] = y;
    soundPosition[2] = z;
    modified = true;
}

void ActivityEvent::Outcome::setWeather(int which, float val, int seconds){
    if(which < 0 || which > 3)
        return;
    weather[which] = val;
    weatherTime[which] = val < 0 ? -1 : seconds;
    value.setValue(QString());
    modified = true;
}

void ActivityEvent::Outcome::setRestartTrain(const QString &service, int time){
    restartService = service;
    restartTime = time;
    modified = true;
}

void ActivityEvent::Outcome::setRestartDelay(int seconds){
    restartDelay = seconds;
    modified = true;
}

void ActivityEvent::Outcome::setRestartWaitingPoint(int val){
    restartWaitingPoint = val;
    modified = true;
}

void ActivityEvent::Outcome::save(QTextStream* out) {
    if(type == TypeNone)
        return;
    
    QString tabOffset = "						 ";
    
    if(category == CategoryNone){
        *out << "					"<< Outcome::OutcomeTypeName[type] <<" ( )\n";
    }
    if(category == CategoryInfo){
        QString val = value.toString();
        if(val.length() < 1)
            *out << "					"<< Outcome::OutcomeTypeName[type] <<" ( )\n";
        else
            *out << "					"<< Outcome::OutcomeTypeName[type] <<" ( "<<ParserX::SplitToMultiline(val, tabOffset)<<" )\n";
    }
    if(category == CategoryEvent){
        *out << "					"<< Outcome::OutcomeTypeName[type] <<" ( "<<value.toInt()<<" )\n";
    }
    const QString in = "						";
    if(category == CategorySoundFile){
        const QStringList sound = value.toStringList();
        *out << "					ORTSActivitySound (\n";
        *out << in << "ORTSActSoundFile ( " << ParserX::AddComIfReq(sound.value(0)) << " "
             << (sound.value(1).isEmpty() ? QString("Everywhere") : sound.value(1)) << " )\n";
        if(soundAt)
            *out << in << "ORTSSoundLocation ( " << soundTile[0] << " " << soundTile[1] << " "
                 << soundPosition[0] << " " << soundPosition[1] << " " << soundPosition[2] << " )\n";
        ParserX::WriteKept(out, kept, in);
        *out << "					)\n";
    }
    if(category == CategoryWeatherChange){
        // A preset that could not be filled in: as it was read.
        if(!value.toString().isEmpty()){
            *out << "					ORTSWeatherChange ( "<<ParserX::AddComIfReq(value.toString())<<" )\n";
        } else {
            static const char *names[4] = {"ORTSOvercast", "ORTSFog", "ORTSPrecipitationIntensity",
                                           "ORTSPrecipitationLiquidity"};
            *out << "					ORTSWeatherChange (\n";
            for(int i = 0; i < 4; i++)
                if(weather[i] >= 0)
                    *out << in << names[i] << " ( " << weather[i] << " " << weatherTime[i] << " )\n";
            ParserX::WriteKept(out, kept, in);
            *out << "					)\n";
        }
    }
    if(category == CategoryRestartTrain){
        *out << "					ORTSRestartWaitingTrain (\n";
        if(!restartService.isEmpty()){
            *out << in << "ORTSWaitingTrainToRestart ( " << ParserX::AddComIfReq(restartService);
            if(restartTime >= 0)
                *out << " " << restartTime;
            *out << " )\n";
        }
        *out << in << "ORTSDelayToRestart ( " << restartDelay << " )\n";
        *out << in << "ORTSMatchingWPDelay ( " << restartWaitingPoint << " )\n";
        ParserX::WriteKept(out, kept, in);
        *out << "					)\n";
    }
}

void ActivityEvent::Outcome::setMessage(QString val){
    if(category != CategoryInfo)
        return;
    value.setValue(val);
    modified = true;
}

void ActivityEvent::Outcome::setEventLinkId(int id){
    value.setValue(id);
    modified = true;
}

void ActivityEvent::Outcome::setSoundFileName(QString val){
    if(category != CategorySoundFile)
        return;
    QStringList l = value.toStringList();
    l[0] = val;
    value.setValue(l);
    modified = true;
}

void ActivityEvent::Outcome::setSoundType(QString val){
    if(category != CategorySoundFile)
        return;
    QStringList l = value.toStringList();
    l[1] = val;
    value.setValue(l);
    modified = true;
}

// A preset's values, written in Open Rails' form; a name not found stays.
void ActivityEvent::Outcome::setWeatherName(QString val){
    if(category != CategoryWeatherChange)
        return;
    if(!applyWeatherPreset(val))
        value.setValue(val);
    modified = true;
}
       
void ActivityEvent::setStationStop(int tid){
    stationStop = tid;
    modified = true;
}

void ActivityEvent::setActionToNewType(EventType newType){
    eventType = newType;
    speed = -99999;
    stationStop = -99999;
    wagonList.clear();
    sidingItem = -99999;
    pickupIdAndAmount.clear();
    isWagonList = false;
    
    if(eventType == EventTypeReachSpeed)
        speed = 0;
    if(eventType == EventTypePickupPassengers)
        stationStop = 0;
    if(eventType == EventTypeAssembleTrainAtLocation
    || eventType == EventTypeDropoffWagonsAtLocation)
        sidingItem = 0;
    if(eventType == EventTypeAssembleTrain
    || eventType == EventTypeAssembleTrainAtLocation
    || eventType == EventTypeDropoffWagonsAtLocation
    || eventType == EventTypePickupWagons)
        isWagonList = true;
}

void ActivityEvent::Outcome::setToNewType(OutcomeType newType){
    type = newType;
    category = OutcomeTypeCategory[type];
    
    if(category == CategoryInfo){
        value.clear();
        value.setValue(QString(""));
    }
    if(category == CategoryEvent){
        value.clear();
        value.setValue(0);
    }
    if(category == CategorySoundFile){
        value.clear();
        QStringList v;
        v.push_back("");
        v.push_back("Everywhere");
        value.setValue(v);
    }
    if(category == CategoryWeatherChange){
        value.clear();
        value.setValue(QString(""));
        for(int i = 0; i < 4; i++){
            weather[i] = -1;
            weatherTime[i] = -1;
        }
    }
    if(category == CategoryRestartTrain){
        value.clear();
        restartService.clear();
        restartTime = -1;
        restartDelay = 0;
        restartWaitingPoint = 0;
    }
    soundAt = false;
    kept.clear();
    modified = true;
}

void ActivityEvent::setParentActivity(Activity* a){
    parentActivity = a;
}

void ActivityEvent::removeWagonListItem(int id){
    if(id >= wagonList.size())
        return;
    if(id < 0)
        return;
    wagonList.remove(id);
}

QString ActivityEvent::getWagonListItemDescription(int id){
    if(id >= wagonList.size())
        return "";
    if(id < 0)
        return ""; 
    return wagonList[id].description;
}

void ActivityEvent::setWagonListItemDescription(int id, QString val){
    if(id >= wagonList.size())
        return;
    if(id < 0)
        return;
    wagonList[id].description = val;
}

void ActivityEvent::addSelectedWagonToList(){
    if(parentActivity == NULL)
        return;
    
    unsigned int uid = parentActivity->getSelectedCarId();
    if(uid == -1)
        return;
    
    for(int i = 0; i < wagonList.size(); i++)
        if(wagonList[i].uid == uid)
            return;
    
    wagonList.push_back(WagonList());
    wagonList.back().uid = uid;
    wagonList.back().sidingItem = 4294967295;
}

bool ActivityEvent::getWagonListItemPosition(int id, float *posTW){
    if(id >= wagonList.size())
        return false;
    if(id < 0)
        return false;
    if(parentActivity == NULL)
        return false;
    
    int oid = wagonList[id].uid / 65536;
    int eid = wagonList[id].uid - oid*65536;
    
    return parentActivity->getCarPosition(oid, eid, posTW);
}

QString ActivityEvent::getSidingDescription(){
    if(sidingItem < 0)
        return "";
    if(Game::trackDB == NULL)
        return "";
    const auto found = Game::trackDB->trackItems.find(sidingItem);
    if(found == Game::trackDB->trackItems.end() || found->second == NULL)
        return "";
    return found->second->platformName;
}

bool ActivityEvent::setSidingFromSelected(){
    if(Game::currentSelectedGameObj == NULL)
        return false;
    if(Game::currentSelectedGameObj->typeObj != GameObj::worldobj)
        return false;
    WorldObj *w = (WorldObj*)Game::currentSelectedGameObj;
    if(w->typeID != WorldObj::siding)
        return false;
    int tid = ((PlatformObj*)w)->getTrackBegItemId();
    if(tid < 0)
        return false;
    sidingItem = tid;
    
    return true;
}
