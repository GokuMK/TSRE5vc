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

class DdsLib : public QThread
 {
     Q_OBJECT

public:
    static bool IsThread;
    DdsLib();
    Texture* texture = nullptr;
    //static void save(QString path, Texture* t);
    void run();
private:
    
protected:
    
};

#endif	/* DDSLIB_H */

