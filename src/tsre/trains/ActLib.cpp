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
bool sameRoute(const QString &contentDirectory, QString routePath) {
    if(routePath.isEmpty()) routePath = Game::root + "/ROUTES/" + Game::route;
    const QString owner = ContentPath::parentDirectory(contentDirectory);
    return owner == QDir(routePath).absolutePath() || ContentPath::sameLocation(owner, routePath);
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
    //qDebug() << pathid;
    QHashIterator<int, Activity*> i(Act);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL) continue;
        if (i.value()->loaded == 1)
            if (ContentPath::canReuse(pathid, i.value()->pathid)
                    || (i.value()->isUnSaved() && pathid == i.value()->pathid)) {
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
    //qDebug() << pathid;
    QHashIterator<int, Activity*> i(Act);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL) continue;
        if (i.value()->loaded == 1)
            if (ContentPath::canReuse(pathid, i.value()->pathid)
                    || (i.value()->isUnSaved() && pathid == i.value()->pathid)) {
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
    QHashIterator<int, Activity*> i(Act);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(!sameRoute(i.value()->path, routePath)) continue;
        i.value()->updateService(serviceNameId);
    }
}

int ActLib::AddService(QString path, QString name, bool nowe) {
    QString pathid = (path + "/" + name);
    pathid.replace("\\", "/");
    pathid = ContentPath::normalize(pathid);
    //qDebug() << pathid;
    QHashIterator<int, Service*> i(Services);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL) continue;
        if (i.value()->loaded == 1)
            if (ContentPath::canReuse(pathid, i.value()->pathid)
                    || (i.value()->isModified() && pathid == i.value()->pathid)) {
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
    //qDebug() << pathid;
    QHashIterator<int, Traffic*> i(Traffics);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL) continue;
        if (i.value()->loaded == 1)
            if (ContentPath::canReuse(pathid, i.value()->pathid)
                    || (i.value()->isModified() && pathid == i.value()->pathid)) {
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
    QHashIterator<int, Activity*> i(Act);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(!sameRoute(i.value()->path, routePath)) continue;
        if(i.value()->isServiceInUse(n))
            return true;
    }
    return false;
}

bool ActLib::IsTrafficInUse(QString name, QString routePath){
    QHashIterator<int, Traffic*> i(Traffics);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(!sameRoute(i.value()->path, routePath)) continue;
        if(i.value()->nameId.toLower() == name.toLower())
            return true;
    }
    return false;
}

QVector<QString> ActLib::GetServiceInUseList(QString n, QString routePath){
    QVector<QString> list;
    QHashIterator<int, Activity*> i(Act);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(!sameRoute(i.value()->path, routePath)) continue;
        if(i.value()->isServiceInUse(n)){
            list.push_back(QString("Activity:") + i.value()->header->name);
        }
    }
    return list;
}
Service* ActLib::GetServiceByName(QString name, QString routePath){
    QHashIterator<int, Service*> i(Services);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(!sameRoute(i.value()->path, routePath)) continue;
        if(i.value()->nameId.toLower() == name.toLower())
            return i.value();
    }
    return NULL;
}

Traffic* ActLib::GetTrafficByName(QString name, QString routePath){
    QHashIterator<int, Traffic*> i(Traffics);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(!sameRoute(i.value()->path, routePath)) continue;
        if(i.value()->nameId.toLower() == name.toLower())
            return i.value();
    }
    return NULL;
}

Path* ActLib::GetPathByName(QString name, QString routePath){
    QHashIterator<int, Path*> i(Paths);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL || i.value()->loaded != 1) continue;
        if(!sameRoute(i.value()->path, routePath)) continue;
        if(i.value()->trPathName.toLower() == name.toLower())
            return i.value();
    }
    return NULL;
}

int ActLib::AddPath(QString path, QString name) {
    QString pathid = (path + "/" + name);
    pathid.replace("\\", "/");
    pathid = ContentPath::normalize(pathid);
    //qDebug() << pathid;
    QHashIterator<int, Path*> i(Paths);
    while (i.hasNext()) {
        i.next();
        if(i.value() == NULL) continue;
        ///qDebug() << i.value()->pathid << pathid; 
        if (i.value()->loaded == 1)
            if (ContentPath::canReuse(pathid, i.value()->pathid)
                    || (i.value()->isModified() && pathid == i.value()->pathid)) {
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
            //p->save();
        }
    }
}