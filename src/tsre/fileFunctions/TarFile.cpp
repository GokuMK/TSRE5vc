/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "TarFile.h"
#include <tsre/fileFunctions/ReadFile.h>
#include <QtMath>
#include <QDir>
#include <QFileInfo>

TarFile::TarFile() {
}

TarFile::TarFile(QString path) {
    filePath = path;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)){
        qDebug() <<"#TAR file not found: "<< filePath;
        return;
    }
    data = ReadFile::readRAW(&file);
}

TarFile::TarFile(FileBuffer *oData, bool copy) {
    if(copy){
        data = new FileBuffer(oData);
    } else {
        data = oData;
    }
}

bool TarFile::extractTo(QString destination){
    if(data == NULL)
        return false;
    
    qDebug() << "#TarFile Extracting "<< filePath << data->length;
    
    PosixHeader *tFileHeader;

    while(data->off < data->length - 512){
        tFileHeader = (PosixHeader*)(&data->data[data->off]);
        //qDebug() << tFileHeader->name;
        //qDebug() << tFileHeader->size;
        unsigned int tFilesize = QString(tFileHeader->size).toInt(NULL, 8);
        //qDebug() << tFilesize;
        data->off += 512;
        QString tFileName = QString(tFileHeader->name);
        if(tFileHeader->typeflag == '5' && tFileName.length() > 0){
            qDebug() << "MkDir: " << tFileName;
            QDir().mkdir(destination + tFileName);
        } else if(tFileName.length() > 0){
            QFile ttFile(destination+tFileName);
            qDebug() << "Write: " << tFileName << tFilesize;
            if (!ttFile.open(QIODevice::WriteOnly)){
                qDebug() << "Write error - can't open file.";
            } else {
                ttFile.write((const char*)&data->data[data->off], tFilesize);
                ttFile.close();
            }
        }
        data->off += tFilesize + (512 - (tFilesize % 512)) % 512;
    }
    return true;
}

TarFile::TarFile(const TarFile& orig) {
}

bool TarFile::extractResourceTo(const QString &destination, const QString &requiredRoot) {
    if (!data || data->length<1024 || data->length%512 || requiredRoot.isEmpty()
            || requiredRoot.contains('/') || requiredRoot.contains('\\')
            || requiredRoot=="." || requiredRoot=="..") return false;
    for (qint64 offset=0; offset+512<=data->length;) {
        const QByteArray header(reinterpret_cast<const char*>(data->data+offset),512);
        if (header==QByteArray(512,'\0')) {
            for (qint64 i=offset; i<data->length; ++i) if (data->data[i]) return false;
            return QDir(QDir(destination).filePath(requiredRoot)).exists();
        }
        auto field=[&](int start,int size) { return header.mid(start,size).split('\0').first(); };
        bool checksumOk=false, sizeOk=false;
        const auto checksum=field(148,8).trimmed().toULongLong(&checksumOk,8);
        quint64 sum=0;
        for (int i=0;i<512;++i) sum+=(i>=148 && i<156)?32:quint8(header[i]);
        const auto size=field(124,12).trimmed().toULongLong(&sizeOk,8);
        if (!checksumOk || checksum!=sum || !sizeOk || size>quint64(data->length-offset-512)) return false;
        QString name=QString::fromUtf8(field(0,100));
        const auto prefix=QString::fromUtf8(field(345,155));
        if (!prefix.isEmpty()) name=prefix+"/"+name;
        if (name.startsWith("./")) name.remove(0,2);
        if (name.endsWith('/')) name.chop(1);
        if (name.isEmpty() || name.contains('\\') || name.contains(':') || name.startsWith('/')
                || name.split('/').contains("..") || QDir::cleanPath(name)!=name
                || (name!=requiredRoot && !name.startsWith(requiredRoot+"/"))) return false;
        // Windows normalizes trailing spaces/dots, including disguised "..".
        for (const auto &part : name.split('/'))
            if (part.trimmed()!=part || part.endsWith('.')) return false;
        const QString target=QDir(destination).filePath(name);
        const char type=header[156];
        if (type=='5') {
            if (size || !QDir().mkpath(target)) return false;
        } else if (type=='0' || type=='\0') {
            if (name==requiredRoot || !QDir().mkpath(QFileInfo(target).absolutePath())) return false;
            QFile file(target);
            if (!file.open(QIODevice::WriteOnly)
                    || file.write(reinterpret_cast<const char*>(data->data+offset+512),size)!=qint64(size)
                    || !file.flush()) return false;
        } else return false; // No links/devices/extensions in resource bundles.
        offset+=512+((size+511)/512)*512;
        if (offset>data->length) return false;
    }
    return false; // Missing tar end marker.
}

TarFile::~TarFile() {
    delete data;
}

