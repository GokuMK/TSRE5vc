/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ACTIVITY_H
#define	ACTIVITY_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <tsre/trains/ActivityEvent.h>
#include <tsre/trains/ActivityObject.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/trains/TextSnapshot.h>
#include <memory>

class RenderQueue;

class FileBuffer;
class Consist;
class Path;
class Service;
class Traffic;
class QTextStream;
class GLUU;

class ActivityServiceDefinition {
public:
    QString name;
    QString routePath;
    Service* servicePointer = NULL;
    int time = -1;
    int uid = -1;
    bool player = false;
    bool empty = true;
    QVector<float> efficiency;
    QVector<int> skipCount;
    QVector<float> distanceDownPath;
    QVector<int> platformStartId;
    ActivityTimetable *trafficDefinition = NULL;
    // Blocks TSRE does not read, as they were in the file; written at the
    // end of the block.
    QStringList kept;
    void load(FileBuffer* data);
    void save(QTextStream* out);
    // Saved: it and its timetable unchanged since.
    void setModified(bool val);
    // Changed (its timetable, a traffic instance, is not).
    void setChanged() { modified = true; }
    void reloadTimetable();
    void reloadDefinition();
    bool isModified();
    void setTimetableEfficiency(int id, float val);
    void updateSim(float *playerT, float deltaTime);
    void pushRenderItems(RenderQueue &queue, float* playerT, int renderMode);
    QMap<int, QString> getStationStopNameList();
private:
    bool modified = false;
};

class Activity {
public:
    Activity();
    virtual ~Activity();
    Activity(QString p, QString n, bool isnew = false);
    Activity(QString src, QString p, QString n, bool isnew = false);

    struct TrafficDefinition {
        QString name;
        QVector<ActivityServiceDefinition> service;
        QStringList kept;
    };

    struct ActivityHeader {
        QString routeid;
        QString name;
        QString description;
        QString briefing;
        QString pathid;
        int completeActivity = 0;
        int type = -1;
        int mode = -1;
        int startTime[3];
        int season = -1;
        int weather = -1;
        int startingSpeed = -1;
        int duration[2];
        int difficulty = -1;
        int animals = -1;
        int workers = -1;
        int fuelWater = -1;
        int fuelCoal = -1;
        int fuelDiesel = -1;
        // Open Rails: wood and sand at the start (%, -1 not set), the loads
        // at stations (OPENRAILS/<name>.load-stations-loads-or).
        int fuelWood = -1;
        int fuelSand = -1;
        QString loadStationsPopulation;
        int* voltage = NULL;
        QStringList kept;
        void load(FileBuffer* data);
        void save(QTextStream* out);
        ActivityHeader();
        ActivityHeader(QString route, QString hname);
    };

    QString name;
    QString nameid;
    QString path;
    QString pathid;
    QString hashid; // Cached logical key; pathid preserves the filename used for I/O.
    QString routeHashid;
    int loaded = -1;
    int ref = 0;

    ActivityServiceDefinition *playerServiceDefinition = NULL;
    ActivityHeader *header = NULL;
    TrafficDefinition *traffic = NULL;
    int nextServiceUID = -1;
    int nextActivityObjectUID = -1;
    int nextEventUID = 0;
    int ortsAIHornAtCrossings = -9999;
    // Open Rails: how AI trains sound the horn at crossings (US, Single;
    // empty not set).
    QString ortsAICrossingHornPattern;
    QVector<ActivityObject*> activityObjects;
    QVector<QPair<int, int>> platformNumPassengersWaiting;
    QVector<ActivityObject*> restrictedSpeedZone;
    QVector<ActivityObject*> activityFailedSignal;
    // Each event is its own object, so windows can hold on to one while
    // events are added; deleteEvent is the only place that frees one.
    QVector<ActivityEvent*> event;
    int serial = -1;
    // Blocks TSRE does not read, as they were in the file, by the block
    // they were in (Tr_Activity, Tr_Activity_File and its lists); written
    // at the end of that block.
    // The Open Rails overlay merged into the activity: renamed to
    // <name>.act.merged when the activity is saved.
    QString mergedOverlay;
    QStringList kept;
    QStringList keptFile;
    QStringList keptEvents;
    QStringList keptObjects;
    QStringList keptSpeedZones;
    QStringList keptFailedSignals;
    QStringList keptPassengers;

    void load();
    void save();
    // The file's text, as save writes it.
    QString toText();
    // Undo: the activity as text, and back. Restoring replaces its events,
    // objects and services with new ones: windows let go of them first.
    std::shared_ptr<TextSnapshot> snapshot();
    void restore(const TextSnapshot &snapshot);
    bool isInitActivityObjects = false;
    //void initActivityObjects();
    void updateSim(float *playerT, float deltaTime);
    void pushRenderItems(RenderQueue &queue, float * playerT, float playerRot, int renderMode);
    // The AI trains shown (shownAITrain, showAllAITrains): their paths and
    // their consists at their starts; a service several run, once.
    // Its loose consists drawn: not while a simulation shows the activity
    // (its standing trains are drawn instead, task simulation 07).
    bool showsLooseConsists();
    QVector<int> shownAITrains();

    QString editorConListSelected;
    ActivityEvent *currentEventSelected = NULL;
    // The outcome shown of that event (a sound's place is picked on the map).
    ActivityEvent::Outcome *currentOutcomeSelected = NULL;
    // What map and 3D show of the AI trains (not saved): the train chosen in
    // the Timetable window (its place in traffic->service, -1 none), and
    // all of them.
    int shownAITrain = -1;
    bool showAllAITrains = false;
    // An AI train's service (by its place in traffic->service), or NULL.
    Service *aiTrainService(int index);
    // The player's service: the one Play prepared, else looked up, so the
    // editor shows the player's train without Play.
    Service *playerService();
    void init(QString route, QString name);
    //void setRouteContent(QVector<Path*>* p, QVector<Service*>* s, QVector<Traffic*>* t);
    //Traffic *getTrafficByName(QString name);
    bool isNew();
    void initToPlay();
    bool isUnSaved();
    void setFileName(QString val);
    void setDisplayName(QString val);
    void setOrtsHornAtCrossigns(bool val);
    bool isOrtsHornAtCrossigns();
    void setDifficulty(int val);
    void setDuration(int h, int m);
    void setStartTime(int h, int m, int s);
    void setSeason(int val);
    void setWeather(int val);
    void pickNewEventLocation(float *tdbPos);
    // The shown sound outcome plays at the track position.
    void pickSoundLocation(float *tdbPos);
    void newLooseConsist(float *tdbPos);
    void newSpeedZone(float *tdbPos);
    bool newFailedSignalFromSelected();
    void createNewPlayerService(QString sName, int sTime);
    void createNewTrafficService(Traffic *t);
    // No player service, no traffic: changes like the others.
    void clearPlayerService();
    void clearTraffic();
    // The activity's AI trains: a run of its traffic pattern joins them
    // (its stops, the service's efficiency), or one leaves (by its place in
    // traffic->service); the pattern keeps its runs.
    bool addAITrain(ActivityTimetable *run);
    bool removeAITrain(int index);
    // The activity names another traffic pattern with the same runs (its
    // own copy): its AI trains find their times there.
    void setTrafficName(const QString &nameId);
    void setFuelCoal(int val);
    void setFuelWater(int val);
    void setFuelDiesel(int val);
    void setFuelWood(int val);
    void setFuelSand(int val);
    void setLoadStationsPopulation(const QString &val);
    void setOrtsHornPattern(const QString &val);
    void setHazardWorkers(int val);
    void setHazardAnimals(int val);
    void setDescription(QString val);
    void setBriefing(QString val);
    void deleteObject(int id);
    void deleteObjectFailedSignal(int id);
    void deleteObjectSpeedZone(int id);
    void removeAllFailedSignals();
    void removeAllSpeedZones();
    void deleteCurrentEvent();
    // Removes and frees the event (which must not be shown any more).
    void deleteEvent(ActivityEvent *e);
    ActivityEvent *newEvent(ActivityEvent::EventCategory category);
    bool isServiceInUse(QString n);
    bool isTrafficInUse(QString n);
    bool isPlayerServiceInUse(QString n);
    QVector<ActivityServiceDefinition*> getServiceList();
    unsigned int getSelectedCarId();
    bool getCarPosition(int oid, int eid, float *posTW);
    QMap<int, QString> getEventIdNameList();
    QMap<int, QString> getServiceStationStopNameList();
    ActivityObject* getObjectById(int id);
    // The consist of the player's train (0) or of an AI train (its place in
    // traffic->service + 1), as map and 3D select them.
    Consist* getServiceConsistById(int id);

private:
    bool modified = false;
    bool nowe = false;
    // Times saved: a state restored from before a save is not on disk.
    int saves = 0;
    void parse(FileBuffer *data);
    // Everything read from the file, gone.
    void clearContent();
    // Saved: nothing in it changed since.
    void setSaved();
    //QVector<Service*>* Services;
    //QVector<Traffic*>* Traffics;
    //QVector<Path*>* Paths;
};

#endif	/* ACTIVITY_H */

