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
#include <tsre/trains/Traffic.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/Game.h>
#include <QDebug>
#include <QFile>
#include <tsre/ogl/GLUU.h>
#include <tsre/tdb/TDB.h>
#include <tsre/trains/ActivityTimetable.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>

Traffic::Traffic(QString p, QString n, bool nowe) {
    pathid = p + "/" + n;
    pathid = ContentPath::normalize(pathid);
    hashid = ContentPath::key(pathid);
    path = p;
    routeHashid = ContentPath::key(ContentPath::parentDirectory(path));
    name = n;
    nameId = n.section(".", 0, -2);
    if(!nowe){
        loaded = -1;
        load();
    } else {
        loaded = 1;
        serial = 1;
        modified = true;
    }
}

Traffic::Traffic(const Traffic& orig) {
}

Traffic::~Traffic() {
}

void Traffic::load(){
    pathid = ContentPath::normalize(pathid);
    hashid = ContentPath::key(pathid);
    qDebug() << pathid;
    QFile *file = new QFile(pathid);
    if (!file->open(QIODevice::ReadOnly)) {
        qDebug() << pathid << "not exist";
        return;
    }

    FileBuffer* data = ReadFile::read(file);
    file->close();
    data->toUtf16();
    data->skipBOM();
    parse(data);
    delete data;
    loaded = 1;
}

void Traffic::parse(FileBuffer *data){
    QString sh;
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        //qDebug() << sh;
        if (sh == ("simisa@@@@@@@@@@jinx0f0t______")) {
            continue;
        }
        if (sh == ("traffic_definition")) {
            displayName = ParserX::GetStringInside(data);
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                if (sh == ("serial")) {
                    serial = ParserX::GetNumber(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("service_definition")) {
                    service.push_back(new ActivityTimetable());
                    service.back()->routePath = ContentPath::parentDirectory(path);
                    service.back()->load(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                kept.push_back(ParserX::KeepBlock(data, sh));
            }
            ParserX::SkipToken(data);
            continue;
        }
        qDebug() << "#TRF - undefined token: " << sh;
        ParserX::SkipToken(data);
    }
}

std::shared_ptr<TextSnapshot> Traffic::snapshot(){
    auto copy = std::make_shared<TextSnapshot>();
    copy->text = toText();
    copy->unsaved = isModified();
    copy->saves = saves;
    return copy;
}

void Traffic::restore(const TextSnapshot &snapshot){
    ActLib::ForgetInstances(this);
    qDeleteAll(service);
    service.clear();
    kept.clear();
    displayName.clear();
    serial = -1;
    FileBuffer *data = FileBuffer::fromText(snapshot.text);
    parse(data);
    delete data;
    modified = snapshot.unsaved || snapshot.saves != saves;
}

ActivityTimetable* Traffic::getInstance(const QString &serviceName, int time){
    for(int i = 0; i < service.size(); i++){
        if(service[i]->time == time && service[i]->name.compare(serviceName, Qt::CaseInsensitive) == 0)
            return service[i];
    }
    return NULL;
}

void Traffic::addInstance(ActivityTimetable *instance){
    service.push_back(instance);
    modified = true;
}

void Traffic::removeInstance(int index){
    if(index < 0 || index >= service.size())
        return;
    service.remove(index);
    modified = true;
}

bool Traffic::isModified(){
    for(int i = 0; i < service.size(); i++){
        if(service[i]->isModified())
            return true;
    }
    return modified;
}

void Traffic::save(){
    QString tpath;
    
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    tpath = path+"/"+name;
    tpath = ContentPath::normalize(tpath);
    qDebug() << tpath;
    QFile file(tpath);

    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)){
        qDebug() << "Cannot save traffic file!";
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << toText();
    out.flush();
    file.close();
    modified = false;
    for(ActivityTimetable *instance : service)
        instance->setModified(false);
    ++saves;
}

QString Traffic::toText(){
    QString text;
    QTextStream out(&text);
    out << "SIMISA@@@@@@@@@@JINX0f0t______\n";
    out << "\n";
    out << "Traffic_Definition ( " << ParserX::AddComIfReq(displayName.isEmpty() ? nameId : displayName) << "\n";
    if (serial >= 0) {
        out << "	Serial ( " << serial << " )\n";
    }
    QString off = "	";
    for(int i = 0; i < service.size(); i++){
        if(!service[i]->empty)
            service[i]->save(&out, off);
    }
    ParserX::WriteKept(&out, kept, off);
    out << ")\n";
    out.flush();    return text;}