/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */


#ifndef ENVIRONMENT_H
#define	ENVIRONMENT_H

#include <QString>
#include <QVector>

class FileBuffer;

class Environment {
public:
    struct WaterLayer {
        float height = 0;
        QString tex;
    };
    QVector<WaterLayer> water;
    int waterCount = 0;
    // The highest water layer (the last of equal ones), drawn as the
    // surface of shaded water; -1 without layers.
    int surfaceWaterLayer() const;
    // The other layers from the lowest up, at most count of them.
    QVector<int> lowerWaterLayers(int count) const;
    Environment(QString path);
    virtual ~Environment();
private:
    bool loaded;
    QString texturePath;
    void loadWaterLayer(FileBuffer* data);
};

#endif	/* ENVIRONMENT_H */

