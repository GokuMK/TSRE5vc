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
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/Service.h>
#include <tsre/trains/Traffic.h>
#include <tsre/trains/Path.h>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <tsre/Game.h>
#include <QProgressDialog>
#include <QCoreApplication>

namespace {
QString routeKey(QString routePath) {
    if(routePath.isEmpty()) routePath = Game::root + "/ROUTES/" + Game::route;
    return ContentPath::key(routePath);
}
}

int ActLib::jestact = 0;
int ActLib::jestservice = 0;
int ActLib::jesttraffic = 0;
int ActLib::jestpath = 0;
QHash<int, Activity*> ActLib::Act;
QHash<int, Service*> ActLib::Services;
QHash<int, Traffic*> ActLib::Traffics;
QHash<int, Path*> ActLib::Paths;
QHash<QString, QVector<int>> ActLib::route;
 
ActLib::ActLib() {
}

ActLib::~ActLib() {
}

int ActLib::GetAct(QString path, QString name){
    QString pathid = (path + "/" + name);
    pathid.replace("\\", "/");
    pathid = ContentPath::normalize(pathid);
    const QString hashid = ContentPath::key(pathid);
    //qDebug() << pathid;
    QHashIterator<int, Activity*> i(Act);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL) continue;
        if (i.value()->loaded == 1)
            if (hashid == i.value()->hashid) {
                i.value()->ref++;
                qDebug() <<"actid "<< pathid;
                return (int)i.key();
            }
    }
    return -1;
}

int ActLib::AddAct(QString path, QString name, bool nowe) {
    QString pathid = (path + "/" + name);
    pathid.replace("\\", "/");
    pathid = ContentPath::normalize(pathid);
    const QString hashid = ContentPath::key(pathid);
    //qDebug() << pathid;
    QHashIterator<int, Activity*> i(Act);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL) continue;
        if (i.value()->loaded == 1)
            if (hashid == i.value()->hashid) {
                i.value()->ref++;
                qDebug() <<"actid "<< pathid;
                return (int)i.key();
            }
    }
    qDebug() << "Nowy " << jestact << " act: " << pathid;
    Act[jestact] = new Activity(pathid, path, name, nowe);
    if(!nowe && Act[jestact]->loaded == 1 && Act[jestact]->header)
        route[Act[jestact]->header->routeid].push_back(jestact);
    return jestact++;
}

void ActLib::UpdateServiceChanges(QString serviceNameId, QString routePath){
    const QString ownerKey = routeKey(routePath);
    QHashIterator<int, Activity*> i(Act);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(i.value()->routeHashid != ownerKey) continue;
        i.value()->updateService(serviceNameId);
    }
}

int ActLib::AddService(QString path, QString name, bool nowe) {
    QString pathid = (path + "/" + name);
    pathid.replace("\\", "/");
    pathid = ContentPath::normalize(pathid);
    const QString hashid = ContentPath::key(pathid);
    //qDebug() << pathid;
    QHashIterator<int, Service*> i(Services);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL) continue;
        if (i.value()->loaded == 1)
            if (hashid == i.value()->hashid) {
                //i.value()->ref++;
                qDebug() <<"serviceid "<< pathid;
                return (int)i.key();
            }
    }
    qDebug() << "Nowy " << jestservice << " service: " << pathid;
    Services[jestservice] = new Service(path, name, nowe);
    return jestservice++;
}

int ActLib::AddTraffic(QString path, QString name, bool nowe) {
    QString pathid = (path + "/" + name);
    pathid.replace("\\", "/");
    pathid = ContentPath::normalize(pathid);
    const QString hashid = ContentPath::key(pathid);
    //qDebug() << pathid;
    QHashIterator<int, Traffic*> i(Traffics);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL) continue;
        if (i.value()->loaded == 1)
            if (hashid == i.value()->hashid) {
                i.value()->ref++;
                qDebug() <<"trafficid "<< pathid;
                return (int)i.key();
            }
    }
    qDebug() << "Nowy " << jesttraffic << " traffic: " << pathid;
    Traffics[jesttraffic] = new Traffic(path, name, nowe);
    return jesttraffic++;
}

bool ActLib::IsServiceInUse(QString n, QString routePath){
    const QString ownerKey = routeKey(routePath);
    QHashIterator<int, Activity*> i(Act);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(i.value()->routeHashid != ownerKey) continue;
        if(i.value()->isServiceInUse(n))
            return true;
    }
    return false;
}

bool ActLib::IsTrafficInUse(QString name, QString routePath){
    const QString ownerKey = routeKey(routePath);
    QHashIterator<int, Traffic*> i(Traffics);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(i.value()->routeHashid != ownerKey) continue;
        if(i.value()->nameId.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QVector<QString> ActLib::GetServiceInUseList(QString n, QString routePath){
    const QString ownerKey = routeKey(routePath);
    QVector<QString> list;
    QHashIterator<int, Activity*> i(Act);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(i.value()->routeHashid != ownerKey) continue;
        if(i.value()->isServiceInUse(n)){
            list.push_back(QString("Activity:") + i.value()->header->name);
        }
    }
    return list;
}
Service* ActLib::GetServiceByName(QString name, QString routePath){
    const QString ownerKey = routeKey(routePath);
    QHashIterator<int, Service*> i(Services);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(i.value()->routeHashid != ownerKey) continue;
        if(i.value()->nameId.compare(name, Qt::CaseInsensitive) == 0)
            return i.value();
    }
    return NULL;
}

Traffic* ActLib::GetTrafficByName(QString name, QString routePath){
    const QString ownerKey = routeKey(routePath);
    QHashIterator<int, Traffic*> i(Traffics);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(i.value()->routeHashid != ownerKey) continue;
        if(i.value()->nameId.compare(name, Qt::CaseInsensitive) == 0)
            return i.value();
    }
    return NULL;
}

// A PathID is the path's file name (as MSTS opens it); TrPathName is
// tried after, for content where the two differ.
Path* ActLib::GetPathByName(QString name, QString routePath){
    const QString ownerKey = routeKey(routePath);
    Path *byTrPathName = NULL;
    QHashIterator<int, Path*> i(Paths);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(i.value()->routeHashid != ownerKey) continue;
        if(i.value()->nameId.compare(name, Qt::CaseInsensitive) == 0)
            return i.value();
        if(byTrPathName == NULL && i.value()->trPathName.compare(name, Qt::CaseInsensitive) == 0)
            byTrPathName = i.value();
    }
    return byTrPathName;
}

void ActLib::PathChanged(Path *path) {
    for (Service *service : Services)
        if (service != NULL)
            service->pathChanged(path);
}

int ActLib::AddNewPath(Path *path) {
    Paths[jestpath] = path;
    return jestpath++;
}

void ActLib::RemovePath(Path *path) {
    for (auto it = Paths.begin(); it != Paths.end(); ++it)
        if (it.value() == path)
            it.value() = NULL;
}

void ActLib::RemoveService(Service *service) {
    for (auto it = Services.begin(); it != Services.end(); ++it)
        if (it.value() == service)
            it.value() = NULL;
}

void ActLib::RemoveTraffic(Traffic *traffic) {
    for (auto it = Traffics.begin(); it != Traffics.end(); ++it)
        if (it.value() == traffic)
            it.value() = NULL;
}

QStringList ActLib::ServiceUsers(Service *service) {
    QStringList users;
    if (service == NULL)
        return users;
    for (Activity *activity : Act)
        if (activity != NULL && activity->loaded == 1 && activity->routeHashid == service->routeHashid
                && activity->isServiceInUse(service->nameId))
            users << activity->name;
    for (Traffic *traffic : Traffics) {
        if (traffic == NULL || traffic->loaded != 1 || traffic->routeHashid != service->routeHashid)
            continue;
        for (ActivityTimetable *instance : traffic->service)
            if (instance != NULL && instance->name.compare(service->nameId, Qt::CaseInsensitive) == 0) {
                users << traffic->name;
                break;
            }
    }
    return users;
}

QStringList ActLib::TrafficUsers(Traffic *traffic) {
    QStringList users;
    if (traffic == NULL)
        return users;
    for (Activity *activity : Act)
        if (activity != NULL && activity->loaded == 1 && activity->routeHashid == traffic->routeHashid
                && activity->traffic != NULL && activity->traffic->name.compare(traffic->nameId, Qt::CaseInsensitive) == 0)
            users << activity->name;
    return users;
}

namespace {
// A file of the folder by this name, in any case.
bool fileInFolder(const QString &folder, const QString &name) {
    QDir dir(folder);
    for (const QString &entry : dir.entryList(QDir::Files))
        if (entry.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}
}

bool ActLib::ServiceExists(QString nameId, QString routePath) {
    if (routePath.isEmpty())
        routePath = Game::root + "/ROUTES/" + Game::route;
    for (Service *service : Services)
        if (service != NULL && service->routeHashid == routeKey(routePath)
                && service->nameId.compare(nameId, Qt::CaseInsensitive) == 0)
            return true;
    return fileInFolder(routePath + "/SERVICES", nameId + ".srv");
}

bool ActLib::TrafficExists(QString nameId, QString routePath) {
    if (routePath.isEmpty())
        routePath = Game::root + "/ROUTES/" + Game::route;
    for (Traffic *traffic : Traffics)
        if (traffic != NULL && traffic->routeHashid == routeKey(routePath)
                && traffic->nameId.compare(nameId, Qt::CaseInsensitive) == 0)
            return true;
    return fileInFolder(routePath + "/TRAFFIC", nameId + ".trf");
}

QStringList ActLib::PathUsers(Path *path) {
    QStringList users;
    if (path == NULL)
        return users;
    for (Service *service : Services)
        if (service != NULL && service->routeHashid == path->routeHashid && path->isNamed(service->pathId))
            users << service->name;
    for (Activity *activity : Act)
        if (activity != NULL && activity->header != NULL && activity->routeHashid == path->routeHashid
                && path->isNamed(activity->header->pathid))
            users << activity->name;
    return users;
}

int ActLib::AddPath(QString path, QString name) {
    QString pathid = (path + "/" + name);
    pathid.replace("\\", "/");
    pathid = ContentPath::normalize(pathid);
    const QString hashid = ContentPath::key(pathid);
    //qDebug() << pathid;
    QHashIterator<int, Path*> i(Paths);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL) continue;
        ///qDebug() << i.value()->pathid << pathid; 
        if (i.value()->loaded == 1)
            if (hashid == i.value()->hashid) {
                i.value()->ref++;
                qDebug() <<"pathid "<< pathid;
                return (int)i.key();
            }
    }
    qDebug() << "Nowy " << jestpath << " path: " << pathid;
    Paths[jestpath] = new Path(path, name);
    return jestpath++;
}

int ActLib::LoadAllAct(QString gameRoot, bool gui){
    QString path;
    path = gameRoot + "/ROUTES";
    QDir dir(path);
    qDebug() << path;
    dir.setFilter(QDir::Dirs);
    
    QStringList dirPaths;
    QStringList actPaths;
    
    foreach(QString dirFile, dir.entryList()){
        if(dirFile == "." || dirFile == "..")   
            continue;
        
        dirFile += "/ACTIVITIES";
        //qDebug() <<dirFile;
        QDir aDir(path+"/"+dirFile);
        if(!aDir.exists()) continue;
        
        aDir.setFilter(QDir::Files);
        aDir.setNameFilters(QStringList()<<"*.act");
        foreach(QString actfile, aDir.entryList()){
            //ActLib::AddAct(path+"/"+dirFile, actfile);
            dirPaths.push_back(path+"/"+dirFile);
            actPaths.push_back(actfile);
        }
    }
    
    QProgressDialog* progress = NULL;
    if(gui){
        progress = new QProgressDialog("Loading ACTIVITIES...", "", 0, dirPaths.size());
        progress->setWindowModality(Qt::WindowModal);
        progress->setCancelButton(NULL);
        progress->setWindowFlags(Qt::CustomizeWindowHint);
    }
    for(int i = 0; i < dirPaths.size(); i++){
        //qDebug() << path << dirFile <<"/"<< engfile;
        ActLib::AddAct(dirPaths[i],actPaths[i]);
        if(progress != NULL){
            progress->setValue(i+1);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    }
    
    qDebug() << "loaded";
    delete progress;
    return 0;
}

void ActLib::GetUnsavedInfo(QVector<QString>& items){
    Activity * e;
    for (int i = 0; i < ActLib::jestact; i++){
        e = ActLib::Act[i];
        if(e == NULL) continue;
        if(e->loaded != 1) continue;
        if(e->isUnSaved()){
            items.push_back("[A] " + e->name );
        }
    }
    Service * s;
    for (int i = 0; i < ActLib::jestservice; i++){
        s = ActLib::Services[i];
        if(s == NULL) continue;
        if(s->loaded != 1) continue;
        if(s->isModified()){
            items.push_back("[S] " + s->name );
        }
    }
    Traffic * t;
    for (int i = 0; i < ActLib::jesttraffic; i++){
        t = ActLib::Traffics[i];
        if(t == NULL) continue;
        if(t->loaded != 1) continue;
        if(t->isModified()){
            items.push_back("[T] " + t->name );
        }
    }
    Path * p;
    for (int i = 0; i < ActLib::jestpath; i++){
        p = ActLib::Paths[i];
        if(p == NULL) continue;
        if(p->loaded != 1) continue;
        if(p->isModified()){
            items.push_back("[P] " + p->name );
        }
    }
}

void ActLib::SaveAll(){
    Activity * e;
    for (int i = 0; i < ActLib::jestact; i++){
        e = ActLib::Act[i];
        if(e == NULL) continue;
        if(e->loaded != 1) continue;
        if(e->isUnSaved()){
            e->save();
        }
    }
    Service * s;
    for (int i = 0; i < ActLib::jestservice; i++){
        s = ActLib::Services[i];
        if(s == NULL) continue;
        if(s->loaded != 1) continue;
        if(s->isModified()){
            s->save();
        }
    }
    Traffic * t;
    for (int i = 0; i < ActLib::jesttraffic; i++){
        t = ActLib::Traffics[i];
        if(t == NULL) continue;
        if(t->loaded != 1) continue;
        if(t->isModified()){
            t->save();
        }
    }
    Path * p;
    for (int i = 0; i < ActLib::jestpath; i++){
        p = ActLib::Paths[i];
        if(p == NULL) continue;
        if(p->loaded != 1) continue;
        if(p->isModified()){
            p->save();
        }
    }
}