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
#include <tsre/environment/EnvFile.h>

// The route's environment file: its water layers for drawing, and the whole
// file with its weather (task environment 01).
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
    // The file as read, and its weather (its block, or derived from it and
    // the TRK slot it fills).
    Env::File file;
    Weather weather;
    Env::SlotWeather slot = Env::SlotWeather::Unknown;
    QString path;
    Environment(QString path, Env::SlotWeather slot = Env::SlotWeather::Unknown);
    virtual ~Environment();
private:
    bool loaded;
    QString texturePath;
};

#endif	/* ENVIRONMENT_H */

