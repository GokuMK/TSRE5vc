/*
 * This file is part of: Generator Plakatowych Zestawie� Poci�g�w.
 * 
 * Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 * 
 * Licensed under GNU General Public License 3.0 or later. 
 * 
 * See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MZIP_H
#define MZIP_H

#include <QString>
#include <QStringList>
#include <QMap>

class QByteArray;

class MZip {
public:
    QMap<QString, QByteArray*> fileData;
    MZip();
    MZip(QString name);
    MZip(const MZip& orig);
    virtual ~MZip();
    void loadAllFiles();
private:
    QString filename;
    QStringList fileNames;
    
};

#endif /* MZIP_H */

