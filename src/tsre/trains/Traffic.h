/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TRAFFIC_H
#define	TRAFFIC_H

#include <QString>
#include <QStringList>
#include <QVector>

class ActivityTimetable;

class Traffic {
public:
    
    QVector<ActivityTimetable*> service;
    // Blocks TSRE does not read, as they were in the file; written at the
    // end of the traffic definition.
    QStringList kept;
    QString displayName;
    QString name;
    QString nameId;
    QString path;
    QString pathid;  
    QString hashid; // Cached logical key; pathid preserves the filename used for I/O.
    QString routeHashid;
    int loaded = -1;
    int ref = 0;
    
    Traffic(QString p, QString n, bool nowe = false);
    Traffic(const Traffic& orig);
    virtual ~Traffic();
    void load();
    void save();
    bool isModified();
    // The instance of the service starting at the time, as Open Rails
    // matches them (the pair, not a joined text: "a1" at 23 is not "a" at
    // 123).
    ActivityTimetable* getInstance(const QString &serviceName, int time);
    void addInstance(ActivityTimetable *instance);
    // Takes the instance out of the traffic; the object stays, as
    // activities may point at it.
    void removeInstance(int index);
private:
    bool modified = false;
    int serial = -1;
};

#endif	/* TRAFFIC_H */

