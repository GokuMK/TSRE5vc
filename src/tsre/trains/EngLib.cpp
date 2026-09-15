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
#include <tsre/trains/EngLib.h>
#include <tsre/trains/Eng.h>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QProgressDialog>
#include <QCoreApplication>
#include <tsre/Game.h>

//int EngLib::jesteng = 0;
//std::unordered_map<int, Eng*> EngLib::eng;
    
EngLib::EngLib() {
}

EngLib::~EngLib() {
}

int EngLib::getEngByPointer(Eng* pointer){
    for ( auto it = eng.begin(); it != eng.end(); ++it ){
        if(it->second == pointer) 
            return it->first;
    }
    return -1;
}

int EngLib::addEng(QString path, QString name) {
    QString pathid = (path + "/" + name);
    pathid.replace("\\", "/");
    pathid = ContentPath::normalize(pathid);
    const QString hashid = ContentPath::key(pathid);
    const int existing = findEng(hashid);
    if (existing >= 0) {
        eng.at(existing)->ref++;
        return existing;
    }

    const int id = jesteng;
    eng[id] = new Eng(pathid, path, name);
    engIds.insert(hashid, id);
    return jesteng++;
}

int EngLib::removeBroken() {
    for (auto &entry : eng) {
        if (!entry.second || entry.second->loaded == 1) continue;
        const auto indexed = engIds.constFind(entry.second->hashid);
        if (indexed != engIds.cend() && indexed.value() == entry.first)
            engIds.remove(entry.second->hashid);
        entry.second = nullptr;
    }
    return 0;
}

void EngLib::removeAll(){
    engIds.clear();
    eng.clear();
    jesteng = 0;
}

int EngLib::findEng(const QString &hashid) const {
    const auto indexed = engIds.constFind(hashid);
    if (indexed == engIds.cend()) return -1;
    const auto entry = eng.find(indexed.value());
    if (entry == eng.end() || !entry->second || entry->second->loaded != 1
            || entry->second->hashid != hashid) return -1;
    return entry->first;
}

int EngLib::getEngByPathid(QString pathid) {
    return findEng(ContentPath::key(pathid));
}

int EngLib::loadAll(QString gameRoot, bool gui){
    QString path;
    path = gameRoot + "/TRAINS/TRAINSET/";
    QDir dir(path);
    QDir trainDir;
    trainDir.setFilter(QDir::Files);
    trainDir.setNameFilters(QStringList()<<"*.eng"<<"*.wag");
    qDebug() << path;
    if(!dir.exists())
        qDebug() << "not exist";
    dir.setFilter(QDir::Dirs);
    qDebug() << dir.count() <<" dirs";
    unsigned long long timeNow = QDateTime::currentMSecsSinceEpoch();
    QStringList dirPaths;
    QStringList engPaths;
    foreach(QString dirFile, dir.entryList()){
        //qDebug() << dirFile;
        trainDir.setPath(path+dirFile);
        foreach(QString engfile, trainDir.entryList()){
            //qDebug() << path << dirFile <<"/"<< engfile;
            //addEng(path+dirFile,engfile);
            dirPaths.push_back(path+dirFile);
            engPaths.push_back(engfile);
        }
    }
    
    QProgressDialog *progress = NULL;
    if(gui){
        progress = new QProgressDialog("Loading TRAINS...", "", 0, dirPaths.size());
        progress->setWindowModality(Qt::WindowModal);
        progress->setCancelButton(NULL);
        progress->setWindowFlags(Qt::CustomizeWindowHint);
    }
    for(int i = 0; i < dirPaths.size(); i++){
        //qDebug() << path << dirFile <<"/"<< engfile;
        addEng(dirPaths[i],engPaths[i]);
        if(progress != NULL){
            progress->setValue(i+1);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    }
    qDebug() << "loaded" << (QDateTime::currentMSecsSinceEpoch() - timeNow)/1000<< "s";
    delete progress;
    return 0;
}