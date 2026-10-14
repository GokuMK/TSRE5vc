/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SERVICE_H
#define	SERVICE_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <tsre/trains/TextSnapshot.h>

class RenderQueue;

class GLUU;
class Path;
class Consist;
class FileBuffer;

struct MapFeatures;

class Service {
public:
    struct StationStop{
        int platformStartID = 0;
        float distanceDownPath = 0;
        int skipCount = 0;
        QStringList kept;
    };
    
    QString displayName;
    QString name;
    QString nameId;
    QString path;
    QString pathid;  
    QString hashid; // Cached logical key; pathid preserves the filename used for I/O.
    QString routeHashid;
    QString trainConfig;
    QString pathId;
    float maxWheelAcceleration;
    float efficiency;
    float startingSpeed;
    float endingSpeed;
    float startInWorld;
    float endInWorld;
    QVector<StationStop> stationStop;
    // Blocks TSRE does not read, as they were in the file, by the block
    // they were in (stops have their own); written at the end of it.
    QStringList kept;
    QStringList keptTimetable;
    int loaded = -1;
    int ref = 0;
    
    Service(QString p, QString n, bool nowe = false);
    Service(const Service& orig);
    virtual ~Service();
    bool isModified();
    void setNameId(QString val);
    void setNewPath(QString pathName);
    void disableStationStop(int count);
    void enableStationStop(int count);
    void setDisplayName(QString val);
    void setEfficiency(float val);
    void setStartSpeed(float val);
    void setEndSpeed(float val);
    void setTrainConfig(QString val);
    void load();
    void save();
    // The file's text, as save writes it.
    QString toText();
    // Undo: the service as text, and back.
    std::shared_ptr<TextSnapshot> snapshot();
    void restore(const TextSnapshot &snapshot);
    Consist *getConsistPointer();
    void updateSim(float *playerT, float deltaTime);
    void pushRenderItems(RenderQueue &queue, float* playerT, quint32 selectionId);
    // The service's consist for the map mode, and its path.
    void getMapFeatures(MapFeatures &consist, int tileX, int tileZ);
    Path *getPathPointer();
    void initToPlay();
    // The consist goes to the start of its path again after the path was
    // edited.
    void pathChanged(Path *path);
    // The path was edited (or undone): each stop stays at its platform's
    // pass (the same platform, the same pass of it), taking its new place
    // and distance, or goes when the path no longer passes it so; the
    // activities and traffic running the service follow. What changed, for
    // the report.
    QStringList followPath(Path *path);
private:
    bool modified = false;
    int serial = -1;
    // Times saved: a state restored from before a save is not on disk.
    int saves = 0;
    Path *pathPointer = NULL;
    Consist *conPointer = NULL;
    void parse(FileBuffer *data);
};

#endif	/* SERVICE_H */

