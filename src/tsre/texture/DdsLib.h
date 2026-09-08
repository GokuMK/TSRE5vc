/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef DDSLIB_H
#define	DDSLIB_H

#include <QThread>
#include <tsre/texture/Texture.h>

class QImage;
struct DdsImageInfo {
    QString encoding;
    int mipCount = 1;
    quint32 fourCC = 0;
    int bitCount = 0;
    int alphaBits = 0;
    int redBits = 0, greenBits = 0, blueBits = 0;
};

class DdsLib : public QThread
 {
     Q_OBJECT

public:
    static bool IsThread;
    DdsLib();
    // Bounded CPU import of the base image, independent of Texture/OpenGL state.
    static bool loadImage(const QString &path, QImage &image, DdsImageInfo &info,
                          QString &error);
    Texture* texture = nullptr;
    //static void save(QString path, Texture* t);
    void run();
private:
    
protected:
    
};

#endif	/* DDSLIB_H */

