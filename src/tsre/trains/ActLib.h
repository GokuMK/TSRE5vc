/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ACTLIB_H
#define	ACTLIB_H

#include <QHash>
#include <QString>
#include <QStringList>

class Activity;
class Service;
class Traffic;
class Path;

class ActLib {
public:
    static int jestact;
    static int jestservice;
    static int jesttraffic;
    static int jestpath;
    static QHash<int, Activity*> Act;
    static QHash<int, Service*> Services;
    static QHash<int, Traffic*> Traffics;
    static QHash<int, Path*> Paths;
    static QHash<QString, QVector<int>> route;
    ActLib();
    virtual ~ActLib();
    static int GetAct(QString path, QString name);
    static int AddAct(QString path, QString name, bool nowe = false);
    static int AddService(QString path, QString name, bool nowe = false);
    static int AddTraffic(QString path, QString name, bool nowe = false);
    static Service* GetServiceByName(QString name, QString routePath = {});
    static Traffic* GetTrafficByName(QString name, QString routePath = {});
    static Path* GetPathByName(QString name, QString routePath = {});
    static bool IsServiceInUse(QString n, QString routePath = {});
    static bool IsTrafficInUse(QString n, QString routePath = {});
    static QVector<QString> GetServiceInUseList(QString n, QString routePath = {});
    static int AddPath(QString path, QString name);
    // After an edit of a path (task editor 06): the services running on it
    // place their consists again.
    static void PathChanged(Path *path);
    // A path made in the editor (New, Clone), kept with the paths read.
    static int AddNewPath(Path *path);
    // Takes a deleted path out of the library. The object stays, as services
    // may still point at it.
    static void RemovePath(Path *path);
    // The services and activities of the path's route that name it.
    static QStringList PathUsers(Path *path);
    static int LoadAllAct(QString gameRoot, bool gui = false);
    static void GetUnsavedInfo(QVector<QString> &items);
    static void UpdateServiceChanges(QString serviceNameId, QString routePath = {});
    static void SaveAll();
private:

};

#endif	/* ACTLIB_H */

