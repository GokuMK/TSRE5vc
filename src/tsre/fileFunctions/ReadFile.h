/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef READFILE_H
#define	READFILE_H
#include <QtCore>
#include <iostream>
#include <tsre/fileFunctions/FileBuffer.h>

class ReadFile {
public:
    static FileBuffer* read(QFile* file);
    static FileBuffer* readRAW(QFile* file);
    // The same from a file's bytes (a server client's route files).
    static FileBuffer* read(const QByteArray &bytes);
    static FileBuffer* readRAW(const QByteArray &bytes);
private:
    
};

#endif	/* READFILE_H */

