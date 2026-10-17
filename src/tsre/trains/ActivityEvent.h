/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <QString>
#include <QStringList>
#include <QVector>
#include <QVariant>

class RenderQueue;

#ifndef ACTIVITYEVENT_H
#define	ACTIVITYEVENT_H

class FileBuffer;
class QTextStream;
class GLUU;
class OglObj;
class TextObj;
class OrtsWeatherChange;
class Activity;

struct MapFeatures;

class ActivityEvent {

public:
    class Outcome {
    public:
        enum OutcomeCategory {
            CategoryNone = 0,
            CategoryEvent = 1,
            CategoryInfo = 2,
            CategorySoundFile = 3,
            CategoryWeatherChange = 4,
            CategoryRestartTrain = 5
        };
        
        enum OutcomeType {
            TypeNone = 0,
            TypeDisplayMessage = 1,
            TypeActivitySuccess = 2,
            TypeActivityFail = 3,
            TypeIncActLevel = 4,
            TypeDecActLevel = 5,
            TypeRestorAactLevel = 6,
            TypeActivateEvent = 7,
            TypeStartIgnoringSpeedLimits = 8,
            TypeStopIgnoringSpeedLimits = 9,
            TypeORTSActSoundFile = 10,
            TypeORTSWeatherChange = 11,
            TypeORTSRestartWaitingTrain = 12
        };
        // Open Rails' weather values: overcast factor, fog distance,
        // precipitation intensity, liquidity.
        enum Weather { Overcast = 0, Fog = 1, Intensity = 2, Liquidity = 3 };
        
        static QMap<OutcomeType, QString> OutcomeTypeDescription;
        static QMap<QString, OutcomeType> OutcomeNameType;
        static QMap<OutcomeType, QString> OutcomeTypeName;
        static QMap<OutcomeType, OutcomeCategory> OutcomeTypeCategory;
        
        OutcomeType type;
        OutcomeCategory category;
        QVariant value;
        // Play a sound (ORTSActivitySound): the file and type are in value;
        // where a Location sound plays.
        bool soundAt = false;
        int soundTile[2] = {0, 0};
        float soundPosition[3] = {0, 0, 0};
        // Change the weather (ORTSWeatherChange): each value with its
        // transition seconds, -1 not changed. A TSRE preset name read from
        // an older file and not found stays in value.
        float weather[4] = {-1, -1, -1, -1};
        int weatherTime[4] = {-1, -1, -1, -1};
        // Restart a waiting train (ORTSRestartWaitingTrain): the AI train
        // (service, start time), after a delay, waiting at the waiting
        // point of this value.
        QString restartService;
        int restartTime = -1;
        int restartDelay = 0;
        int restartWaitingPoint = 0;
        // Blocks in the outcome TSRE does not read, written back in it.
        QStringList kept;
        
        Outcome(OutcomeType t){
            type = t;
            category = OutcomeTypeCategory[type];
        }
        
        // Its values, the outcome's token (`token`) already read.
        void load(FileBuffer* data, const QString &token = QString());
        void save(QTextStream* out);
        void setToNewType(OutcomeType newType);
        void setSoundLocation(bool at, int tileX, int tileZ, float x, float y, float z);
        void setWeather(int which, float value, int seconds);
        // A TSRE weather preset (weathertransitions.dat): its values.
        bool applyWeatherPreset(const QString &name);
        void setRestartTrain(const QString &service, int time);
        void setRestartDelay(int seconds);
        void setRestartWaitingPoint(int value);
        void setMessage(QString val);
        void setEventLinkId(int id);
        void setSoundFileName(QString val);
        void setSoundType(QString val);
        void setWeatherName(QString val);
        void setModified(bool val);
        bool isModified();
    protected:
        bool modified = false;
    };

    enum EventType {
        EventTypeNone = 0,
        EventTypeTime = 1,
        EventTypeLocation = 2,
        EventTypeAllstops = 3,
        EventTypePickupWagons = 4,
        EventTypeAssembleTrain = 5,
        EventTypeAssembleTrainAtLocation = 6,
        EventTypeDropoffWagonsAtLocation = 7,
        EventTypePickupPassengers = 8,
        EventTypeReachSpeed = 9,
        EventTypePickUp = 10
    };
    
    static QMap<EventType, QString> EventTypeDescription;
    static QMap<QString, EventType> EventNameType;
    static QMap<EventType, QString> EventTypeName;
    
    enum EventCategory {
        CategoryUndefined = 0,
        CategoryAction = 1,
        CategoryLocation = 2,
        CategoryTime = 3
    };
    
    struct WagonList {
        unsigned int uid;
        unsigned int sidingItem = -1;
        QString description;
        
        WagonList(){
        }
        WagonList(WagonList *o){
            uid = o->uid;
            sidingItem = o->sidingItem;
            description = o->description;
        }
    };
    
    EventCategory category = CategoryUndefined;
    EventType eventType = EventTypeNone;
    QVector<WagonList> wagonList;

    int id = -99999;
    bool reversableEvent = false;
    int activationLevel = -99999;
    QString name;
    int time = -99999;
    int stationStop = -99999;
    float* location = NULL;
    int sidingItem = -99999;
    float speed = -99999;
    int triggerOnStop = -99999;
    int ortsContinue = -99999;
    // Open Rails: the AI train (service, start time, -1 left out) whose
    // passing triggers a location event.
    QString triggeringService;
    int triggeringTime = -1;
    // Read as Open Rails' Reversible_Event: written back so.
    bool reversibleSpelling = false;
    QVector<int> pickupIdAndAmount;
    QString textToDisplayOnCompletionIfTriggered;
    QString textToDisplayOnCompletionIfNotTriggered;
    QString textToDisplayDescriptionOfTask;
    QVector<Outcome*> outcomes;
    // Blocks TSRE does not read, as they were in the file, by the block
    // they were in; written at the end of that block.
    QStringList kept;
    QStringList keptOutcomes;
    QStringList keptWagonList;
    // deprecated - use only for OpenRails 1.2;
    OrtsWeatherChange* ortsWeatherChangeDeprecated = NULL;
    QVector<QString> ortsActSoundFileDeprecated;
    //Outcome* outcome = NULL;
    
    ActivityEvent();
    ActivityEvent(int uid, EventCategory c);
    
    ~ActivityEvent();
    
    int newOutcome();
    void removeOutcome(int id);
    void setModified(bool val);
    void setActivationLevel(int val);
    void setName(QString val);
    void setTriggeredText(QString val);
    void setUntriggeredText(QString val);
    void setLocation(int X, int Z, float x, float z);
    void setNotes(QString val);
    // m/s.
    void setSpeed(float val);
    void setLocationRadius(int val);
    void setLocationStop(bool val);
    void setReversable(bool val);
    void setTriggeringTrain(const QString &service, int time);
    void setAutoContinue(int val);
    void setActionToNewType(EventType newType);
    void setStationStop(int tid);
    void setTime(int val);
    int getWagonListSize();
    QString getWagonListDescription(int i);
    QString getWagonListIdDescription(int i);
    bool isModified();
    bool isSelected();
    bool setSelected(bool val);
    bool unselect();
    bool select();
    void load(FileBuffer* data);
    void save(QTextStream* out);
    void addSelectedWagonToList();
    void removeWagonListItem(int id);
    QString getWagonListItemDescription(int id);
    void setWagonListItemDescription(int id, QString val);
    bool getWagonListItemPosition(int id, float *posTW);
    QString getSidingDescription();
    bool setSidingFromSelected();
    void setParentActivity(Activity* a);
    Activity *getParentActivity() const { return parentActivity; }
    void pushRenderItem(RenderQueue &queue, float *playerT, float playerRot, int renderMode);
    // A location event for the map mode: its place as a point and its
    // trigger area.
    void getMapFeatures(MapFeatures &features, int tileX, int tileZ);
    
protected:
    Activity *parentActivity = NULL;
    static OglObj *simpleMarkerObj;
    TextObj* txtMarkerObj = NULL;
    bool modified = false;
    bool isWagonList = false;
    bool selected = false;
};

#endif	/* ACTIVITYEVENT_H */

