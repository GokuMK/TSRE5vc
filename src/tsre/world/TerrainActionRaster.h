/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine / editor
 *  Copyright (C) 2016-2026 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef TERRAINACTIONRASTER_H
#define TERRAINACTIONRASTER_H

#include <QVector>

class TerrainActionRaster {
public:
    TerrainActionRaster(float minimumX, float minimumZ,
                        float maximumX, float maximumZ,
                        int sampleSpacing);

    bool isValid() const;
    int spacing() const;
    int minimumX() const;
    int minimumZ() const;
    int maximumX() const;
    int maximumZ() const;

    void stampLegacyPoint(float pointX, float pointZ,
                          float radiusMetres, float bedRadiusMetres);
    void stampSegment(float startX, float startZ,
                      float endX, float endZ,
                      float radiusMetres, float bedRadiusMetres);
    bool sampleNearest(float worldX, float worldZ,
                       float &distanceOutsideBedMetres) const;

private:
    int originX = 0;
    int originZ = 0;
    int endX = -1;
    int endZ = -1;
    int sampleStep = 0;
    int width = 0;
    int height = 0;
    QVector<float> minimumDistances;
};

#endif
