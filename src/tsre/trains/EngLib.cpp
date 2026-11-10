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
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/trains/VehicleDefinition.h>
#include <tsre/trains/VehiclePreload.h>
#include <settings/SettingsAccess.h>
#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>
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

static QString pathidOf(const QString &path, const QString &name) {
    QString pathid = (path + "/" + name);
    pathid.replace("\\", "/");
    return ContentPath::normalize(pathid);
}

QString EngLib::keyOf(const QString &path, const QString &name) {
    return ContentPath::key(pathidOf(path, name));
}

int EngLib::addEng(QString path, QString name, std::shared_ptr<const VehicleDefinition> read) {
    const QString pathid = pathidOf(path, name);
    const QString hashid = ContentPath::key(pathid);
    const int existing = findEng(hashid);
    if (existing >= 0) {
        eng.at(existing)->ref++;
        return existing;
    }

    const int id = jesteng;
    eng[id] = new Eng(pathid, path, name, std::move(read));
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

QString EngLib::consistFolder(const QString &path) {
    QString folder = path;
    folder.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (folder.endsWith(QLatin1Char('/')))
        folder.chop(1);
    const QString marker = QStringLiteral("/TRAINS/TRAINSET/");
    const qsizetype at = folder.lastIndexOf(marker, -1, Qt::CaseInsensitive);
    return at >= 0 ? folder.mid(at + marker.size()) : folder.section(QLatin1Char('/'), -1);
}

int EngLib::loadAll(QString gameRoot, bool gui){
    QString path;
    path = gameRoot + "/TRAINS/TRAINSET/";
    QDir dir(path);
    qDebug() << path;
    if(!dir.exists())
        qDebug() << "not exist";
    unsigned long long timeNow = QDateTime::currentMSecsSinceEpoch();
    QStringList dirPaths;
    QStringList engPaths;
    // A vehicle folder holds a vehicle's file; a folder without any is a
    // group, its subfolders checked once, no deeper. A folder listed once
    // (from cold, each listing waits for the disk), MaSzyna's check only
    // where an .mmd is.
    struct Listed {
        QStringList vehicles;
        bool maszyna = false;
        bool holdsVehicle() const { return !vehicles.isEmpty() || maszyna; }
    };
    const auto list = [](const QString &folder) {
        Listed listed;
        bool mmd = false;
        for (const QString &file : QDir(folder).entryList(QDir::Files)) {
            if (file.endsWith(QLatin1String(".eng"), Qt::CaseInsensitive)
                    || file.endsWith(QLatin1String(".wag"), Qt::CaseInsensitive))
                listed.vehicles.push_back(file);
            else if (file.endsWith(QLatin1String(".mmd"), Qt::CaseInsensitive))
                mmd = true;
        }
        listed.maszyna = mmd && MaszynaVehicles::holdsVehicle(folder);
        return listed;
    };
    const auto addFolder = [&](const QString &folder, const Listed &listed) {
        for (const QString &engFile : listed.vehicles) {
            dirPaths.push_back(folder);
            engPaths.push_back(engFile);
        }
        if (!listed.maszyna)
            return;
        int unlisted = 0;
        for (const QString &row : MaszynaVehicles::rows(folder, &unlisted)) {
            dirPaths.push_back(folder);
            engPaths.push_back(row);
        }
        if (unlisted > 0)
            ErrorMessagesLib::PushErrorMessage(new ErrorMessage(ErrorMessage::Type_Info, ErrorMessage::Source_Other,
                    //% "%1: %2 of its liveries are not in its textures.txt and are not listed."
                    qtTrId("maszyna.vehicles.unlisted").arg(folder).arg(unlisted), folder));
    };
    foreach(QString dirFile, dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)){
        const QString folder = path + dirFile;
        const Listed listed = list(folder);
        if (listed.holdsVehicle()) {
            addFolder(folder, listed);
            continue;
        }
        foreach(QString subFile, QDir(folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot)){
            const QString sub = folder + "/" + subFile;
            const Listed subListed = list(sub);
            if (subListed.holdsVehicle())
                addFolder(sub, subListed);
        }
    }

    QProgressDialog *progress = NULL;
    if(gui){
        progress = new QProgressDialog("Loading TRAINS...", "", 0, dirPaths.size());
        progress->setWindowModality(Qt::WindowModal);
        progress->setCancelButton(NULL);
        progress->setWindowFlags(Qt::CustomizeWindowHint);
    }
    // The files of those not loaded yet read on workers, all at once
    // (VehiclePreload::readAll): from cold, a file's read is mostly waiting
    // for the disk. The cars made from them after, here, in the listed order;
    // a vehicle loaded already counted again, as before.
    const bool openRails = Settings::boolean("core.content.loading.preferOpenRailsEng");
    QVector<QPair<QString, QString>> toRead;
    QVector<int> readAt(dirPaths.size(), -1);
    for (int i = 0; i < dirPaths.size(); ++i) {
        if (findEng(keyOf(dirPaths[i], engPaths[i])) >= 0)
            continue;
        readAt[i] = toRead.size();
        toRead.push_back({dirPaths[i], engPaths[i]});
    }
    const int loadedBefore = dirPaths.size() - toRead.size();
    const QVector<std::shared_ptr<const VehicleDefinition>> definitions = VehiclePreload::readAll(
            toRead, Game::root, Game::routeFolder(), openRails, [&](int read) {
                if (progress == NULL)
                    return;
                progress->setValue(loadedBefore + read);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            });
    for(int i = 0; i < dirPaths.size(); i++)
        addEng(dirPaths[i], engPaths[i], readAt[i] >= 0 ? definitions[readAt[i]] : nullptr);
    qDebug() << "loaded" << dirPaths.size() << "vehicles," << toRead.size() << "read, in"
             << (QDateTime::currentMSecsSinceEpoch() - timeNow) << "ms";
    delete progress;
    return 0;
}