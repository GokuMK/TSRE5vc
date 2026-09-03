/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/TerrainLibQt.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainActionRaster.h>
#include <tsre/world/TerrainMeshBackend.h>
#include <tsre/math3d/GLMatrix.h>
#include <QOpenGLShaderProgram>
#include <set>
#include <algorithm>
#include <cmath>
#include <limits>
#include <math.h>
#include <tsre/Game.h>
#include <tsre/texture/Brush.h>
#include <tsre/geo/HeightWindow.h>
#include <tsre/world/QuadTree.h>
#include <tsre/Undo.h>
#include <tsre/world/Route.h>
#include <tsre/world/Environment.h>
#include <tsre/world/TerrainInfo.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/texture/TexLib.h>
#include <QFile>
#include <QRect>

namespace {

void includeDirtySample(QHash<Terrain*, QRect> &dirtySampleBounds,
                        Terrain *terrain, int sampleX, int sampleZ) {
    const QRect sampleRect(sampleX, sampleZ, 1, 1);
    auto bounds = dirtySampleBounds.find(terrain);
    if (bounds == dirtySampleBounds.end())
        dirtySampleBounds.insert(terrain, sampleRect);
    else
        *bounds = bounds->united(sampleRect);
}

}

TerrainLibQt::TerrainLibQt() {
}

TerrainLibQt::TerrainLibQt(const TerrainLibQt& orig) {
}

TerrainLibQt::~TerrainLibQt() {
}

void TerrainLibQt::setDetailedAsCurrent(){
    currentQt = &terrainQt;
    currentQuadTree = quadTree;
}

void TerrainLibQt::setDistantAsCurrent(){
    currentQt = &terrainQtLo;
    currentQuadTree = quadTreeLo;
}

Terrain* TerrainLibQt::getTerrainByXY(int x, int y, bool load) {
    if(currentQuadTree == NULL)
        currentQuadTree = quadTree;
    if(currentQt == NULL)
        currentQt = &terrainQt;
    
    
    //QString terrainName = currentQuadTree->getMyName((int) x, -y);
    unsigned int terrainNameId = currentQuadTree->getMyNameId((int) x, -y);

    if (terrainNameId == 0)
        return NULL;
    if ((*currentQt)[terrainNameId] != NULL) {
        if((*currentQt)[terrainNameId]->t != NULL)
            return (*currentQt)[terrainNameId]->t;
    }
    if (load) {
        (*currentQt)[terrainNameId] = new TerrainInfo();
        currentQuadTree->fillTerrainInfo(x, -y, (*currentQt)[terrainNameId]);
        //qDebug() << terrainNameId;
        (*currentQt)[terrainNameId]->t = new Terrain((*currentQt)[terrainNameId]);
        return (*currentQt)[terrainNameId]->t;
    }

    return NULL;
}

QuadTree* TerrainLibQt::getQuadTreeDetailed(){
    return quadTree;
}

QuadTree* TerrainLibQt::getQuadTreeDistant(){
    return quadTreeLo;
}

void TerrainLibQt::saveQtToStream(QTextStream &out){
    quadTree->save(out);
}

void TerrainLibQt::saveQtLoToStream(QTextStream &out){
    quadTreeLo->save(out);
}

void TerrainLibQt::loadQuadTree() {
    quadTree = new QuadTree();
    quadTree->load();

    quadTreeLo = new QuadTree(true);
    quadTreeLo->load();

    //currentQt = &terrainQtLo;
    //currentQuadTree = quadTreeLo;
    //quadTree->listNames();
}

void TerrainLibQt::loadQuadTreeDetailed(FileBuffer *data) {
    quadTree = new QuadTree();
    quadTree->load(data, false);
}

void TerrainLibQt::loadQuadTreeDistant(FileBuffer *data) {
    quadTreeLo = new QuadTree();
    quadTreeLo->load(data, false);
}

void TerrainLibQt::createNewRouteTerrain(int x, int z) {
    currentQuadTree = new QuadTree();
    currentQuadTree->createNew(x, z);
    QString name = currentQuadTree->getMyName(x, z);
    Terrain::SaveEmpty(name);
}

void TerrainLibQt::saveEmpty(int x, int z) {
    if (!Game::writeEnabled)
        return;
    qDebug() << "#new tile add to QT ";
    currentQuadTree->addTile(x, z);
    qDebug() << "#new tile get name ";
    QString name = currentQuadTree->getMyName(x, z);
    qDebug() << "#new tile Gen "<<name;
    if(currentQuadTree->isLow())
        Terrain::SaveEmpty(name, 256, 128, 16, true);
    else
        Terrain::SaveEmpty(name);
}

bool TerrainLibQt::isLoaded(int x, int z) {
    unsigned int terrainNameId = quadTree->getMyNameId((int) x, -z);
    if (terrainNameId == 0)
        return false;
    if ((*currentQt)[terrainNameId] == NULL)
        return false;
    if ((*currentQt)[terrainNameId]->t == NULL)
        return false;
    if ((*currentQt)[terrainNameId]->t->loaded == false)
        return false;
    return true;
}

bool TerrainLibQt::load(int x, int z) {
    Terrain *t = getTerrainByXY(x, z, true);
    if (t == NULL)
        return false;
    if (t->loaded == false)
        return false;
    return true;
}

void TerrainLibQt::getUnsavedInfo(QVector<QString> &items) {
    if (!Game::writeEnabled) return;
    QHashIterator<unsigned int, TerrainInfo*> i(terrainQt);
    while (i.hasNext()) {
        i.next();
        if (i.value() == NULL) continue;
        Terrain* tTile = (Terrain*) i.value()->t;
        if (tTile == NULL) continue;
        if (tTile->loaded && tTile->isModified()) {
            items.push_back("[T] "+QString::number(tTile->mojex)+" "+QString::number(-tTile->mojez));
        }
    }
    QHashIterator<unsigned int, TerrainInfo*> i2(terrainQtLo);
    while (i2.hasNext()) {
        i2.next();
        if (i2.value() == NULL) continue;
        Terrain* tTile = (Terrain*) i2.value()->t;
        if (tTile == NULL) continue;
        if (tTile->loaded && tTile->isModified()) {
            items.push_back("[T] "+QString::number(tTile->mojex)+" "+QString::number(-tTile->mojez));
        }
    }
}

void TerrainLibQt::save() {
    if (!Game::writeEnabled) return;
    qDebug() << "save terrain";
    QHashIterator<unsigned int, TerrainInfo*> i(terrainQt);
    while (i.hasNext()) {
        i.next();
        if (i.value() == NULL) continue;
        Terrain* tTile = (Terrain*) i.value()->t;
        if (tTile == NULL) continue;
        if (tTile->loaded && tTile->isModified()) {
            tTile->save();
            tTile->setModified(false);
        }
    }
    qDebug() << "save lo terrain";
    QHashIterator<unsigned int, TerrainInfo*> i2(terrainQtLo);
    while (i2.hasNext()) {
        i2.next();
        if (i2.value() == NULL) continue;
        Terrain* tTile = (Terrain*) i2.value()->t;
        if (tTile == NULL) continue;
        if (tTile->loaded && tTile->isModified()) {
            tTile->save();
            tTile->setModified(false);
        }
    }
}

bool TerrainLibQt::reload(int x, int z) {
    unsigned int terrainNameId = currentQuadTree->getMyNameId((int) x, -z);
    if (terrainNameId == 0)
        return false;

    (*currentQt)[terrainNameId] = new TerrainInfo();
    currentQuadTree->fillTerrainInfo(x, -z, (*currentQt)[terrainNameId]);
    (*currentQt)[terrainNameId]->t = new Terrain((*currentQt)[terrainNameId]);
    if ((*currentQt)[terrainNameId]->t->loaded)
        return true;
    return false;
}

float TerrainLibQt::getHeight(int x, int z, float posx, float posz) {
    return TerrainLibQt::getHeight(x, z, posx, posz, false);
}

void TerrainLibQt::refresh(int x, int z) {
    Terrain *terr = this->getTerrainByXY(x, z);

    if (terr == NULL) return;
    if (terr->loaded == false) return;
    terr->refresh();
}

void TerrainLibQt::setHeight(int x, int z, float posx, float posz, float h) {
    Game::check_coords(x, z, posx, posz);
    Terrain *terr = this->getTerrainByXY(x, z);

    if (terr == NULL) return;
    if (terr->loaded == false) return;
    
    terr->setHeight(x, z, posx, posz, h);
}

Terrain* TerrainLibQt::setHeight256(int x, int z, int posx, int posz, float h) {
    return setHeight256(x, z, posx, posz, h, 0, 0);
}

Terrain* TerrainLibQt::setHeight256(int x, int z, int posx, int posz, float h, float diffC, float diffE) {
    Game::check_coords(x, z, posx, posz);
    const int worldPosX = posx;
    const int worldPosZ = posz;
    Terrain *terr = getTerrainByXY(x, z);

    if (terr == NULL) return NULL;
    if (terr->loaded == false) return NULL;
    if (!terr->isEditable()) return NULL;

    float lx = posx, lz = posz;
    terr->getLocalCoords(x, z, lx, lz);
    int sampleSize = terr->getSampleSize();
    posx = std::clamp(static_cast<int>(std::floor(lx / sampleSize)),
                      0, terr->getSampleCount());
    posz = std::clamp(static_cast<int>(std::floor(lz / sampleSize)),
                      0, terr->getSampleCount());
    
    if(diffC == 0 && diffE == 0){
        terr->terrainData[(posz)][(posx)] = h;
    } else {
        if(terr->terrainData[(posz)][(posx)] < h)
            if(terr->terrainData[(posz)][(posx)] < h - diffE) 
                terr->terrainData[(posz)][(posx)] = h - diffE;
        if(terr->terrainData[(posz)][(posx)] > h)
            if(terr->terrainData[(posz)][(posx)] > h + diffC) 
                terr->terrainData[(posz)][(posx)] = h + diffC;
    }
    terr->invalidateSamples(posx, posz, posx, posz,
                            TerrainDirtyHeight | TerrainDirtyNormals);
    terr->setErrorBias(x, z, worldPosX, worldPosZ, 0);
    terr->setModified(true);
    
    return terr;
}

float TerrainLibQt::getHeight(int x, int z, float posx, float posz, bool addR) {
    float height = -1.0f;
    tryGetHeight(x, z, posx, posz, height, addR, false);
    return height;
}

bool TerrainLibQt::tryGetHeight(int x, int z, float posx, float posz,
                                float &height, bool addR,
                                bool loadIfNeeded) {
    if (!TerrainGridLayout::normalizeWorldPosition(x, z, posx, posz))
        return false;
    Terrain *terr = getTerrainByXY(x, z, loadIfNeeded);
    if (terr == NULL || !terr->loaded)
        return false;
    height = terr->getHeight(x, z, posx, posz, addR);
    return true;
}

void TerrainLibQt::fillHeightMap(int x, int z, float* data) {
    Terrain *terr = getTerrainByXY(x, z, false);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    terr->fillHeightMap(data);
}

void TerrainLibQt::getRotation(float* rot, int x, int z, float posx, float posz) {
    Game::check_coords(x, z, posx, posz);
    rot[0] = 0;
    rot[1] = 0;
    
    Terrain *terr = getTerrainByXY(x, z, false);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    terr->getRotation(rot, x, z, posx, posz);
    return;
}

void TerrainLibQt::setHeightFromGeoGui(int x, int z, float* p) {
    if(heightWindow == NULL)
        heightWindow = new HeightWindow();
    
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr = getTerrainByXY(x, z);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    if (!terr->isEditable()) return;

    int X, Y;
    terr->getLowCornerTileXY(X, Y);
    heightWindow->tileX = X;
    heightWindow->tileZ = -Y;
    heightWindow->ok = false;
    int samples = terr->getSampleCount();
    heightWindow->terrainResolution = samples;
    heightWindow->terrainSize = terr->getSampleCount()*terr->getSampleSize();
    heightWindow->exec();
    if(heightWindow->ok){
        qDebug() << "ok";
        for (int i = 0; i < samples; i++) {
            for (int j = 0; j < samples; j++) {
                terr->terrainData[i][j] = heightWindow->terrainData[j][i];
            }
        }
        terr->setAllErrorBias(0);
        terr->setModified(true);
        int X, Y;
        for(int i = -1; i <= 1; i++)
            for(int j = -1; j<= 1; j++){
                terr->getCornerCoordsXY(X, Y, i, j);
                Terrain* tterr  = getTerrainByXY(X, Y);
                if (tterr != NULL) 
                    tterr->refresh();
            }
        updateTerrainHeightmap(terr);
    }
}

void TerrainLibQt::setHeightFromGeo(int x, int z, float* p) {
    if(heightWindow == NULL)
        heightWindow = new HeightWindow();
    
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr = getTerrainByXY(x, z);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    if (!terr->isEditable()) return;
    int X, Y;
    terr->getLowCornerTileXY(X, Y);
    heightWindow->tileX = X;
    heightWindow->tileZ = -Y;
    heightWindow->ok = false;
    int samples = terr->getSampleCount();
    heightWindow->terrainResolution = samples;
    heightWindow->terrainSize = terr->getSampleCount()*terr->getSampleSize();
    heightWindow->load(false);
    if(heightWindow->ok){
        qDebug() << "ok";
        for (int i = 0; i < samples; i++) {
            for (int j = 0; j < samples; j++) {
                terr->terrainData[i][j] = heightWindow->terrainData[j][i];
            }
        }
        terr->setAllErrorBias(0);
        terr->setModified(true);
        terr->refresh();
        int X, Y;
        terr->getCornerCoordsXY(X, Y, 0, 1);
        Terrain* tterr;
        tterr = getTerrainByXY(X, Y);
        if (tterr != NULL) 
            tterr->refresh();
        terr->getCornerCoordsXY(X, Y, 0, -1);
        tterr = getTerrainByXY(X, Y);
        if (tterr != NULL) 
            tterr->refresh();
        terr->getCornerCoordsXY(X, Y, 1, 0);
        tterr = getTerrainByXY(X, Y);
        if (tterr != NULL) 
            tterr->refresh();
        terr->getCornerCoordsXY(X, Y, -1, 0);
        tterr = getTerrainByXY(X, Y);
        if (tterr != NULL) 
            tterr->refresh();
        updateTerrainHeightmap(terr);
    }
}

void TerrainLibQt::setTextureToTrackObj(Brush* brush, float* punkty, int length, int tx, int tz) {
    float posx, posz;
    int ttx, ttz;
    for(int i = 0; i < length; i+=3 ){
        posx = punkty[i];
        posz = punkty[i+2];
        ttx = tx;
        ttz = tz;
        Game::check_coords(ttx, ttz, posx, posz);
        Terrain *terr = this->getTerrainByXY(ttx, ttz);
        if (terr == NULL)
            continue;
        if (terr->loaded == false) continue;
        terr->paintTexture(brush, ttx, ttz, posx, posz);
    }
}

void TerrainLibQt::setTerrainToTrackObj(Brush* brush, float* punkty, int length,
                                        int tx, int tz, float* matrix,
                                        float offsetY, bool connectedPath) {
    if (brush == NULL || punkty == NULL || matrix == NULL || length < 3)
        return;

    float minimumX = std::numeric_limits<float>::infinity();
    float minimumZ = std::numeric_limits<float>::infinity();
    float maximumX = -std::numeric_limits<float>::infinity();
    float maximumZ = -std::numeric_limits<float>::infinity();
    for (int point = 0; point + 2 < length; point += 3) {
        if (!std::isfinite(punkty[point])
                || !std::isfinite(punkty[point + 1])
                || !std::isfinite(punkty[point + 2]))
            return;
        minimumX = std::min(minimumX, punkty[point]);
        minimumZ = std::min(minimumZ, punkty[point + 2]);
        maximumX = std::max(maximumX, punkty[point]);
        maximumZ = std::max(maximumZ, punkty[point + 2]);
    }

    const float radiusMetres = std::max(
                0.0f, static_cast<float>(brush->eRadius
                                         * Brush::TerrainAdjustmentUnitMetres));
    const float bedRadiusMetres = std::max(
                0.0f, static_cast<float>(brush->eSize
                                         * Brush::TerrainAdjustmentUnitMetres));
    const float cutSlope = Brush::terrainSlopeRatio(brush->eCut);
    const float embankmentSlope = Brush::terrainSlopeRatio(brush->eEmb);
    minimumX -= radiusMetres;
    minimumZ -= radiusMetres;
    maximumX += radiusMetres;
    maximumZ += radiusMetres;

    int minimumTileX = tx;
    int minimumTileZ = tz;
    int maximumTileX = tx;
    int maximumTileZ = tz;
    float normalizedMinimumX = minimumX;
    float normalizedMinimumZ = minimumZ;
    float normalizedMaximumX = maximumX;
    float normalizedMaximumZ = maximumZ;
    if (!TerrainGridLayout::normalizeWorldPosition(
                minimumTileX, minimumTileZ,
                normalizedMinimumX, normalizedMinimumZ)
            || !TerrainGridLayout::normalizeWorldPosition(
                maximumTileX, maximumTileZ,
                normalizedMaximumX, normalizedMaximumZ))
        return;
    if (minimumTileX > maximumTileX)
        std::swap(minimumTileX, maximumTileX);
    if (minimumTileZ > maximumTileZ)
        std::swap(minimumTileZ, maximumTileZ);

    QSet<Terrain*> targetTerrains;
    int rasterSpacing = std::numeric_limits<int>::max();
    for (qint64 tileX = minimumTileX; tileX <= maximumTileX; ++tileX) {
        for (qint64 tileZ = minimumTileZ; tileZ <= maximumTileZ; ++tileZ) {
            Terrain *terrain = getTerrainByXY(static_cast<int>(tileX),
                                              static_cast<int>(tileZ));
            if (terrain == NULL || !terrain->loaded || !terrain->isEditable())
                continue;
            targetTerrains.insert(terrain);
            rasterSpacing = std::min(rasterSpacing, terrain->getSampleSize());
        }
    }
    if (targetTerrains.isEmpty()
            || rasterSpacing == std::numeric_limits<int>::max())
        return;

    TerrainActionRaster actionRaster(minimumX, minimumZ, maximumX, maximumZ,
                                     rasterSpacing);
    if (!actionRaster.isValid()) {
        qWarning() << "Unable to allocate terrain action raster";
        return;
    }
    if (connectedPath && length >= 6) {
        // TSection currently emits points at about 4 m intervals. Refuse to
        // bridge a larger discontinuity because one flat point array may
        // contain more than one vector section without explicit strip breaks.
        constexpr float MaximumConnectedPointDistance = 8.0f;
        constexpr float MaximumConnectedPointDistanceSquared =
                MaximumConnectedPointDistance * MaximumConnectedPointDistance;
        for (int point = 0; point + 5 < length; point += 3) {
            const float differenceX = punkty[point + 3] - punkty[point];
            const float differenceY = punkty[point + 4] - punkty[point + 1];
            const float differenceZ = punkty[point + 5] - punkty[point + 2];
            const float pointDistanceSquared = differenceX * differenceX
                    + differenceY * differenceY + differenceZ * differenceZ;
            if (pointDistanceSquared <= MaximumConnectedPointDistanceSquared) {
                actionRaster.stampSegment(
                            punkty[point], punkty[point + 2],
                            punkty[point + 3], punkty[point + 5],
                            radiusMetres, bedRadiusMetres);
            } else {
                actionRaster.stampSegment(
                            punkty[point], punkty[point + 2],
                            punkty[point], punkty[point + 2],
                            radiusMetres, bedRadiusMetres);
                actionRaster.stampSegment(
                            punkty[point + 3], punkty[point + 5],
                            punkty[point + 3], punkty[point + 5],
                            radiusMetres, bedRadiusMetres);
            }
        }
    } else if (connectedPath) {
        actionRaster.stampSegment(punkty[0], punkty[2],
                                  punkty[0], punkty[2],
                                  radiusMetres, bedRadiusMetres);
    } else {
        for (int point = 0; point + 2 < length; point += 3)
            actionRaster.stampLegacyPoint(punkty[point], punkty[point + 2],
                                          radiusMetres, bedRadiusMetres);
    }

    // calculating plane equation
    float p1[3];
    float p2[3];
    float p3[3];
    
    p1[0] = punkty[0];
    p1[1] = punkty[1];
    p1[2] = punkty[2];
    p2[0] = punkty[length-3];
    p2[1] = punkty[length-2];
    p2[2] = punkty[length-1];
    p3[0] = 10;
    p3[1] = 0;
    p3[2] = 10;
    Vec3::transformMat4(p3, p3, matrix);
    Vector3f vec1, vec2, vec3;
    vec1.x = p2[0] - p1[0]; vec1.y = p2[1] - p1[1]; vec1.z = p2[2] - p1[2];
    vec2.x = p3[0] - p1[0]; vec2.y = p3[1] - p1[1]; vec2.z = p3[2] - p1[2];

    //Vector3f::cross(vec3, vec1, vec2);
    vec3.x = vec1.y * vec2.z - vec1.z * vec2.y;
    vec3.y = vec1.z * vec2.x - vec1.x * vec2.z;
    vec3.z = vec1.x * vec2.y - vec1.y * vec2.x;
    if (!std::isfinite(vec3.y)
            || std::abs(vec3.y) <= std::numeric_limits<float>::epsilon())
        return;
    float vec3d = vec3.x*p1[0] + vec3.y*p1[1] + vec3.z*p1[2];
    vec3.x /= vec3.y;
    vec3.z /= vec3.y;
    vec3d /= vec3.y;
    
    // end of calculating plane equation
    
    // Apply the accumulated action once at every native terrain sample. A
    // lower-resolution tile samples the finest action raster without first
    // inventing an interpolated high-resolution terrain height field.
    foreach (Terrain *terrain, targetTerrains) {
        const TerrainGridLayout &layout = terrain->getGridLayout();
        const double terrainMinimumX =
                (static_cast<double>(terrain->mojex) - tx)
                * TerrainGridLayout::WorldTileSize
                - TerrainGridLayout::WorldTileHalfSize;
        const double terrainMinimumZ =
                (static_cast<double>(terrain->mojez) - tz)
                * TerrainGridLayout::WorldTileSize
                + TerrainGridLayout::WorldTileHalfSize
                - layout.terrainWorldSize;
        const int firstSampleX = std::clamp(static_cast<int>(std::ceil(
                    (actionRaster.minimumX() - terrainMinimumX)
                    / layout.sampleSpacing)), 0, layout.sampleCount);
        const int firstSampleZ = std::clamp(static_cast<int>(std::ceil(
                    (actionRaster.minimumZ() - terrainMinimumZ)
                    / layout.sampleSpacing)), 0, layout.sampleCount);
        const int lastSampleX = std::clamp(static_cast<int>(std::floor(
                    (actionRaster.maximumX() - terrainMinimumX)
                    / layout.sampleSpacing)), 0, layout.sampleCount);
        const int lastSampleZ = std::clamp(static_cast<int>(std::floor(
                    (actionRaster.maximumZ() - terrainMinimumZ)
                    / layout.sampleSpacing)), 0, layout.sampleCount);
        if (firstSampleX > lastSampleX || firstSampleZ > lastSampleZ)
            continue;

        QRect dirtySampleBounds;
        bool hasDirtySamples = false;
        bool hasAffectedSamples = false;
        for (int sampleZ = firstSampleZ; sampleZ <= lastSampleZ; ++sampleZ) {
            const float worldZ = static_cast<float>(terrainMinimumZ
                    + static_cast<double>(sampleZ) * layout.sampleSpacing);
            for (int sampleX = firstSampleX; sampleX <= lastSampleX; ++sampleX) {
                const float worldX = static_cast<float>(terrainMinimumX
                        + static_cast<double>(sampleX) * layout.sampleSpacing);
                float distanceOutsideBedMetres = 0.0f;
                if (!actionRaster.sampleNearest(worldX, worldZ,
                                                distanceOutsideBedMetres))
                    continue;
                if (!hasAffectedSamples) {
                    Undo::PushTerrainHeightMap(terrain->mojex, terrain->mojez,
                                               terrain->terrainData,
                                               terrain->getSampleCount());
                    hasAffectedSamples = true;
                }

                terrain->setErrorBias(tx, tz, worldX, worldZ, 0);
                float &terrainHeight = terrain->terrainData[sampleZ][sampleX];
                const float oldHeight = terrainHeight;
                const float targetHeight = vec3d - vec3.x * worldX
                        - vec3.z * worldZ + offsetY;
                const float cutDifference = distanceOutsideBedMetres * cutSlope;
                const float embankmentDifference = distanceOutsideBedMetres
                        * embankmentSlope;
                if (cutDifference == 0.0f && embankmentDifference == 0.0f) {
                    terrainHeight = targetHeight;
                } else {
                    if (terrainHeight < targetHeight
                            && terrainHeight < targetHeight - embankmentDifference)
                        terrainHeight = targetHeight - embankmentDifference;
                    if (terrainHeight > targetHeight
                            && terrainHeight > targetHeight + cutDifference)
                        terrainHeight = targetHeight + cutDifference;
                }
                if (terrainHeight != oldHeight) {
                    const QRect changedSample(sampleX, sampleZ, 1, 1);
                    if (!hasDirtySamples) {
                        dirtySampleBounds = changedSample;
                        hasDirtySamples = true;
                    } else {
                        dirtySampleBounds = dirtySampleBounds.united(changedSample);
                    }
                }
            }
        }
        if (!hasAffectedSamples)
            continue;
        terrain->setModified(true);
        if (hasDirtySamples) {
            terrain->invalidateSamples(dirtySampleBounds.left(),
                                       dirtySampleBounds.top(),
                                       dirtySampleBounds.right(),
                                       dirtySampleBounds.bottom(),
                                       TerrainDirtyHeight | TerrainDirtyNormals);
            terrain->refreshModified();
        }
        updateTerrainHeightmap(terrain);
    }
}

void TerrainLibQt::setTerrainTexture(Brush* brush, int x, int z, float* p) {
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr = this->getTerrainByXY(x, z);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    terr->setTexture(brush, x, z, posx, posz);
    updateTerrainTFile(terr);
}

void TerrainLibQt::toggleWaterDraw(int x, int z, float* p, float direction) {
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr = this->getTerrainByXY(x, z);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    terr->toggleWaterDraw(x, z, posx, posz, direction);
}

void TerrainLibQt::makeTextureFromMap(int x, int z, float* p) {
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr = this->getTerrainByXY(x, z);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    terr->makeTextureFromMap();
}

void TerrainLibQt::removeTileTextureFromMap(int x, int z, float* p) {
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr = this->getTerrainByXY(x, z);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    terr->removeTextureFromMap();
}

void TerrainLibQt::setTileBlob(int x, int z, float* p) {
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr = this->getTerrainByXY(x, z);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    terr->setTileBlob();
}

void TerrainLibQt::setWaterLevelGui(int x, int z, float* p) {
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr = this->getTerrainByXY(x, z);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    terr->setWaterLevelGui();
}

void TerrainLibQt::toggleDraw(int x, int z, float* p) {
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr = this->getTerrainByXY(x, z);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    terr->toggleDraw(x, z, posx, posz);
    updateTerrainTFile(terr);
}

int TerrainLibQt::getTexture(int x, int z, float* p) {
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr = this->getTerrainByXY(x, z);
    if (terr == NULL) return -1;
    if (terr->loaded == false) return -1;
    return terr->getTexture(x, z, posx, posz);
}

void TerrainLibQt::paintTexture(Brush* brush, int x, int z, float* p) {
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr = this->getTerrainByXY(x, z);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    terr->paintTexture(brush, x, z, posx, posz);
}

void TerrainLibQt::lockTexture(Brush* brush, int x, int z, float* p) {
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr = this->getTerrainByXY(x, z);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    terr->lockTexture(brush, x, z, posx, posz);
}

void TerrainLibQt::toggleGaps(int x, int z, float* p, float direction) {
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr = this->getTerrainByXY(x, z);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    terr->toggleGaps(x, z, posx, posz, direction);
}

void TerrainLibQt::setFixedTileHeight(Brush* brush, int x, int z, float* p) {
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    
    Terrain *terr = this->getTerrainByXY(x, z);
    if (terr == NULL) return;
    if (terr->loaded == false) return;
    if (!terr->isEditable()) return;
    Undo::PushTerrainHeightMap(terr->mojex, terr->mojez, terr->terrainData, terr->getSampleCount());
    terr->setFixedHeight(brush->hFixed);
    terr->setAllErrorBias(0);
    updateTerrainHeightmap(terr);
}

QSet<Terrain*> TerrainLibQt::paintHeightMap(Brush* brush, int x, int z, float* p) {

    QSet<Terrain*> uterr;
    QHash<Terrain*, QRect> dirtySampleBounds;
    float posx = p[0];
    float posz = p[2];
    Game::check_coords(x, z, posx, posz);
    qDebug() << x << " " << z << " " << posx << " " << posz;
    
    Terrain *terr;
    terr = getTerrainByXY(x, z);
    if (terr == NULL) return uterr;
    if (terr->loaded == false) return uterr;
    if (!terr->isEditable()) return uterr;

    const int editingSpacing = terr->getSampleSize();
    posx = std::round(posx / editingSpacing) * editingSpacing;
    posz = std::round(posz / editingSpacing) * editingSpacing;

    int px = posx;
    int pz = posz;
    const float radiusMetres = brush->size * 8.0f;
    const int radiusCells = static_cast<int>(std::ceil(radiusMetres / editingSpacing));
    float h = 0;
    float rd = 0;
    float hAvg = 0;
    int tx, tz;
    int tpx, tpz;
    int count = 0;
    
    // add tiles that can be modified to undo;
    Undo::PushTerrainHeightMap(terr->mojex, terr->mojez, terr->terrainData, terr->getSampleCount());
    
    for(int i = -radiusCells; i <= radiusCells; i++)
        for(int j = -radiusCells; j <= radiusCells; j++){
            if (std::sqrt(static_cast<float>(i*i + j*j)) * editingSpacing > radiusMetres)
                continue;
            tpx = px+i*editingSpacing;
            tpz = pz+j*editingSpacing;
            tx = x;
            tz = z;
            Game::check_coords(tx, tz, tpx, tpz);
            if(terr != getTerrainByXY(tx, tz)){
                terr = getTerrainByXY(tx, tz);
                if (terr == NULL) continue;
                if (!terr->loaded) continue;
                if (!terr->isEditable()) continue;
                if (terr->getSampleSize() != editingSpacing) {
                    qWarning() << "Skipping mixed-resolution terrain brush seam";
                    continue;
                }
                Undo::PushTerrainHeightMap(terr->mojex, terr->mojez, terr->terrainData, terr->getSampleCount());
            }
        }
    //
    
    terr = getTerrainByXY(x, z);
    h = brush->alpha*brush->direction*10.0;
    if(brush->hType == 1){
        terr->setErrorBias(x, z, posx, posz, 0);
        float lx = posx, lz = posz;
        terr->getLocalCoords(x, z, lx, lz);
        const int sampleSize = terr->getSampleSize();
        const int sampleX = std::clamp(static_cast<int>(std::floor(lx / sampleSize)),
                                       0, terr->getSampleCount());
        const int sampleZ = std::clamp(static_cast<int>(std::floor(lz / sampleSize)),
                                       0, terr->getSampleCount());
        const float oldHeight = terr->terrainData[sampleZ][sampleX];
        terr->terrainData[sampleZ][sampleX] += h;
        rd = terr->terrainData[sampleZ][sampleX];
        uterr.insert(terr);
        if (terr->terrainData[sampleZ][sampleX] != oldHeight)
            includeDirtySample(dirtySampleBounds, terr, sampleX, sampleZ);
    }
    if(brush->hType == 2){
        hAvg = brush->hFixed;
    }
    if(brush->hType == 3){
        for(int i = -radiusCells; i <= radiusCells; i++)
            for(int j = -radiusCells; j <= radiusCells; j++){
                if (std::sqrt(static_cast<float>(i*i + j*j)) * editingSpacing > radiusMetres)
                    continue;
                tpx = px+i*editingSpacing;
                tpz = pz+j*editingSpacing;
                tx = x;
                tz = z;
                Game::check_coords(tx, tz, tpx, tpz);
                terr = getTerrainByXY(tx, tz);
                if (terr == NULL) continue;
                if (!terr->loaded) continue;
                if (!terr->isEditable()) continue;
                if (terr->getSampleSize() != editingSpacing) continue;
                float lx = tpx, lz = tpz;
                terr->getLocalCoords(tx, tz, lx, lz);
                int sampleSize = terr->getSampleSize();
                tpx = std::clamp(static_cast<int>(std::floor(lx / sampleSize)),
                                 0, terr->getSampleCount());
                tpz = std::clamp(static_cast<int>(std::floor(lz / sampleSize)),
                                 0, terr->getSampleCount());
                hAvg += terr->terrainData[tpz][tpx];
                count++;
            }
        if (count > 0)
            hAvg /= count;
    }
    
    for(int i = -radiusCells; i <= radiusCells; i++)
        for(int j = -radiusCells; j <= radiusCells; j++){
            if(brush->hType == 1)
                if(i == 0 && j == 0) continue;
            tx = x;
            tz = z;
            tpx = px+i*editingSpacing;
            tpz = pz+j*editingSpacing;
            Game::check_coords(tx, tz, tpx, tpz);
            terr = getTerrainByXY(tx, tz);
            if (terr == NULL) continue;
            if (!terr->loaded) continue;
            if (!terr->isEditable()) continue;
            if (terr->getSampleSize() != editingSpacing) continue;

            const float distanceMetres = std::sqrt(static_cast<float>(i*i + j*j))
                    * editingSpacing;
            if(distanceMetres > radiusMetres) continue;
            uterr.insert(terr);

            int sampleSize = terr->getSampleSize();
            h = radiusMetres > 0.0f ? (radiusMetres - distanceMetres) / radiusMetres : 1.0f;
            h = h*brush->alpha*brush->direction*10.0;
            
            terr->setErrorBias(tx, tz, tpx, tpz, 0);
            float lx = tpx, lz = tpz;
            terr->getLocalCoords(tx, tz, lx, lz);
            //qDebug() << tpx << lx << tpz << lz;
            
            tpx = std::clamp(static_cast<int>(std::floor(lx / sampleSize)),
                             0, terr->getSampleCount());
            tpz = std::clamp(static_cast<int>(std::floor(lz / sampleSize)),
                             0, terr->getSampleCount());
            const float oldHeight = terr->terrainData[tpz][tpx];
            if(brush->hType == 0){
                    terr->terrainData[tpz][tpx] += h;
            } else if(brush->hType == 1){
                if(h < 0){
                    if(terr->terrainData[tpz][tpx] > rd)
                        terr->terrainData[tpz][tpx] += h;
                }
                if(h > 0){
                    if(terr->terrainData[tpz][tpx] < rd)
                        terr->terrainData[tpz][tpx] += h;
                }
            } else if(brush->hType == 2 || brush->hType == 3){
                if(terr->terrainData[tpz][tpx] >hAvg){
                    terr->terrainData[tpz][tpx] -= h*brush->direction;
                    if(terr->terrainData[tpz][tpx] < hAvg)
                        terr->terrainData[tpz][tpx] = hAvg;
                }
                if(terr->terrainData[tpz][tpx] < hAvg){
                    terr->terrainData[tpz][tpx] += h*brush->direction;
                    if(terr->terrainData[tpz][tpx] > hAvg)
                        terr->terrainData[tpz][tpx] = hAvg;
                }
            }
            if (terr->terrainData[tpz][tpx] != oldHeight)
                includeDirtySample(dirtySampleBounds, terr, tpx, tpz);
        }

    foreach (Terrain *value, uterr){
        value->setModified(true);
        const auto bounds = dirtySampleBounds.constFind(value);
        if (bounds != dirtySampleBounds.constEnd()) {
            value->invalidateSamples(bounds->left(), bounds->top(),
                                     bounds->right(), bounds->bottom(),
                                     TerrainDirtyHeight | TerrainDirtyNormals);
            value->refreshModified();
        }
        updateTerrainHeightmap(value);
    }
    return uterr;
}

void TerrainLibQt::fillWaterLevels(float *w, int mojex, int mojez) {
    Terrain *cTile = getTerrainByXY(mojex, mojez);
    Terrain *tTile;
    int X, Y;
    
    cTile->getCornerCoordsXY(X, Y, -1, -1);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL)
        if(tTile->loaded){
            w[0] = tTile->getWaterLevelSE();
        }
    cTile->getCornerCoordsXY(X, Y, 0, -1);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL)
        if(tTile->loaded){
            w[1] = tTile->getWaterLevelSW();
            w[2] = tTile->getWaterLevelSE();
        }
    cTile->getCornerCoordsXY(X, Y, 1, -1);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL)
        if(tTile->loaded){
            w[3] = tTile->getWaterLevelSW();
        }
    cTile->getCornerCoordsXY(X, Y, -1, 0);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL) 
        if(tTile->loaded){
            w[4] = tTile->getWaterLevelNE();
            w[6] = tTile->getWaterLevelSE();
        }
    cTile->getCornerCoordsXY(X, Y, 1, 0);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL)
        if(tTile->loaded){
            w[5] = tTile->getWaterLevelNW();
            w[7] = tTile->getWaterLevelSW();
        }
    cTile->getCornerCoordsXY(X, Y, -1, 1);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL)
        if(tTile->loaded){
            w[8] = tTile->getWaterLevelNE();
        }
    cTile->getCornerCoordsXY(X, Y, 0, 1);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL) 
        if(tTile->loaded){
            w[9] = tTile->getWaterLevelNW();
            w[10] = tTile->getWaterLevelNE();
        }
    cTile->getCornerCoordsXY(X, Y, 1, 1);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL)
        if(tTile->loaded){
            w[11] = tTile->getWaterLevelNW();
        }
}

void TerrainLibQt::setWaterLevels(float *w, int mojex, int mojez) {
    Terrain *cTile = getTerrainByXY(mojex, mojez);
    if(!cTile->loaded)
        return;
    Terrain *tTile;
    int X, Y;
    
    cTile->getCornerCoordsXY(X, Y, -1, -1);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL)
        if(tTile->loaded){
            tTile->setWaterLevelSE(w[0]);
            tTile->refreshWaterShapes();
        }
    cTile->getCornerCoordsXY(X, Y, 0, -1);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL)
        if(tTile->loaded){
            tTile->setWaterLevelSW(w[1]);
            tTile->setWaterLevelSE(w[2]);
            tTile->refreshWaterShapes();
        }
    cTile->getCornerCoordsXY(X, Y, 1, -1);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL)
        if(tTile->loaded){
            tTile->setWaterLevelSW(w[3]);
            tTile->refreshWaterShapes();
        }
    cTile->getCornerCoordsXY(X, Y, -1, 0);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL) 
        if(tTile->loaded){
            tTile->setWaterLevelNE(w[4]);
            tTile->setWaterLevelSE(w[6]);
            tTile->refreshWaterShapes();
        }
    cTile->getCornerCoordsXY(X, Y, 1, 0);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL)
        if(tTile->loaded){
            tTile->setWaterLevelNW(w[5]);
            tTile->setWaterLevelSW(w[7]);
            tTile->refreshWaterShapes();
        }
    cTile->getCornerCoordsXY(X, Y, -1, 1);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL)
        if(tTile->loaded){
            tTile->setWaterLevelNE(w[8]);
            tTile->refreshWaterShapes();
        }
    cTile->getCornerCoordsXY(X, Y, 0, 1);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL) 
        if(tTile->loaded){
            tTile->setWaterLevelNW(w[9]);
            tTile->setWaterLevelNE(w[10]);
            tTile->refreshWaterShapes();
        }
    cTile->getCornerCoordsXY(X, Y, 1, 1);
    tTile = getTerrainByXY(X, Y);
    if (tTile != NULL)
        if(tTile->loaded){
            tTile->setWaterLevelNW(w[11]);
            tTile->refreshWaterShapes();
        }
}


void TerrainLibQt::setDetailedTerrainAsCurrent(){
    currentQuadTree = quadTree;
    currentQt = &terrainQt;
}

void TerrainLibQt::setLowTerrainAsCurrent(){
    currentQuadTree = quadTreeLo;
    currentQt = &terrainQtLo;
}

void TerrainLibQt::fillTerrainData(Terrain* tTile, float* offsetXYZ){
    ///QuadTree* tQuadTree = currentQuadTree;
    //tTile->mojex;
    //tTile->mojez;
    //QHash<int, Terrain*> tt;
    //Terrain *t;
    int x, z, xx, zz;
    float position[3];
    position[1] = offsetXYZ[1];
    
    float h = 0;
    tTile->setFixedHeight(200);
    const TerrainGridLayout &destinationLayout = tTile->getGridLayout();
    const int startX = -TerrainGridLayout::WorldTileHalfSize;
    const int endX = startX + destinationLayout.terrainWorldSize;
    const int endZ = TerrainGridLayout::WorldTileHalfSize;
    const int startZ = endZ - destinationLayout.terrainWorldSize;
    for(int i = startX; i < endX; i += destinationLayout.sampleSpacing)
        for(int j = startZ; j < endZ; j += destinationLayout.sampleSpacing){
            x = tTile->mojex;
            z = tTile->mojez;
            position[0] = i - offsetXYZ[0];
            position[2] = j + offsetXYZ[2];
            while(position[0] > TerrainGridLayout::WorldTileHalfSize
                    || position[0] < -TerrainGridLayout::WorldTileHalfSize
                    || position[2] > TerrainGridLayout::WorldTileHalfSize
                    || position[2] < -TerrainGridLayout::WorldTileHalfSize ){
                Game::check_coords(x, z, position);
            }
            if(position[0] == TerrainGridLayout::WorldTileHalfSize){
                x++;
                position[0] = -TerrainGridLayout::WorldTileHalfSize;
            }
            if(position[2] == TerrainGridLayout::WorldTileHalfSize){
                z++;
                position[2] = -TerrainGridLayout::WorldTileHalfSize;
            }
            //qDebug() << tTile->mojex << tTile->mojez << x << z << i << j << position[0] << position[2];
            Terrain *terr = getTerrainByXY(x, z, false);
            if (terr == NULL){
                //qDebug() << "NULL" << tTile->mojex << tTile->mojez << x << z;
            } else if(terr->loaded){
                //qDebug() << "fail not loaded" << x << z;
                h = terr->getHeight(x, z, position[0], position[2], false);
            }
            tTile->setHeight(tTile->mojex, tTile->mojez, i, j, h + offsetXYZ[1], false);
        }
    
    Brush *brush = new Brush();
    const int halfPatch = destinationLayout.patchWorldSize / 2;
    for(int i = startX + halfPatch; i < endX; i += destinationLayout.patchWorldSize)
        for(int j = startZ + halfPatch; j < endZ; j += destinationLayout.patchWorldSize){
            x = tTile->mojex;
            z = tTile->mojez;
            position[0] = i - offsetXYZ[0];
            position[2] = j + offsetXYZ[2];
            while(position[0] > TerrainGridLayout::WorldTileHalfSize
                    || position[0] < -TerrainGridLayout::WorldTileHalfSize
                    || position[2] > TerrainGridLayout::WorldTileHalfSize
                    || position[2] < -TerrainGridLayout::WorldTileHalfSize ){
                Game::check_coords(x, z, position);
            }
            if(position[0] == TerrainGridLayout::WorldTileHalfSize){
                x++;
                position[0] = -TerrainGridLayout::WorldTileHalfSize;
            }
            if(position[2] == TerrainGridLayout::WorldTileHalfSize){
                z++;
                position[2] = -TerrainGridLayout::WorldTileHalfSize;
            }
            Terrain *terr = getTerrainByXY(x, z, false);
            if (terr == NULL){
            } else if(terr->loaded){
                QString tex = terr->getPatchMainTextureName(x, z, position[0], position[2]);
                tTile->setTexture(tex, tTile->mojex, tTile->mojez, i, j, terr->getPatchTexTransformString(x, z, position[0], position[2]));
                int flags = terr->getPatchFlags(x, z, position[0], position[2]);
                tTile->setPatchFlags(tTile->mojex, tTile->mojez, i, j, flags);
                if ((flags & 0xc0) != 0){
                    tTile->setAvgWaterLevel(terr->getAvgVaterLevel() + offsetXYZ[1]);
                }
            }
        }
    delete brush;
}

void TerrainLibQt::fillRaw(Terrain *cTerr, int mojex, int mojez) {
    QuadTree* tQuadTree = currentQuadTree;
    QHash<unsigned int, TerrainInfo*> *tterrainQt = currentQt;

    if(cTerr->lowTile){
        currentQuadTree = quadTreeLo;
        currentQt = &terrainQtLo;
    } else {
        currentQuadTree = quadTree;
        currentQt = &terrainQt;
    }
    
    cTerr->fillTerrainDataX();
    cTerr->fillTerrainDataY();
    cTerr->fillTerrainDataXY();
    
    currentQuadTree = tQuadTree;
    currentQt = tterrainQt;
}

void TerrainLibQt::terrainSamplesChanged(Terrain *source,
                                         int minX, int minZ,
                                         int maxX, int maxZ,
                                         unsigned int reasons) {
    QuadTree *savedQuadTree = currentQuadTree;
    QHash<unsigned int, TerrainInfo*> *savedTerrainQt = currentQt;
    if (source != NULL && source->lowTile) {
        currentQuadTree = quadTreeLo;
        currentQt = &terrainQtLo;
    } else {
        currentQuadTree = quadTree;
        currentQt = &terrainQt;
    }
    TerrainLib::terrainSamplesChanged(source, minX, minZ,
                                      maxX, maxZ, reasons);
    currentQuadTree = savedQuadTree;
    currentQt = savedTerrainQt;
}

void TerrainLibQt::renderWater(GLUU* gluu, float* playerT, float* playerW, float* target, float fov, int renderMode, int layer) {
    int mintile = -Game::tileLod;
    int maxtile = Game::tileLod;

    if (renderMode == gluu->RENDER_SELECTION) {
        mintile = -1;
        maxtile = 1;
    }

    gluu->currentShader->setUniformValue(gluu->currentShader->shaderAlpha, 0.0f);
    gluu->enableNormals();

    Terrain *tTile;
    int selectionColor = 0;
    int i = 0, j = 0;
    QHash<QString, bool> rendered;
    for (int n = -1; n < (Game::tileLod * 2 + 1)*(Game::tileLod * 2 + 1) - 1; n++) {
        if (n != -1)
            spiralLoop(n, i, j);

        tTile = getTerrainByXY((int) playerT[0] + i, (int) playerT[1] + j, true);
        if(tTile == NULL)
            continue;
        if (tTile->loaded == false)
            continue;
        if (rendered[tTile->name])
            continue;
        rendered[tTile->name] = true;

        if (tTile->loaded) {
            float lodx = 2048 * i - playerW[0];
            float lodz = 2048 * j - playerW[2];
            gluu->mvPushMatrix();
            Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, 2048 * i, Game::currentRoute->env->water[layer].height, 2048 * j);
            gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->mvMatrix));
            if (renderMode == gluu->RENDER_SELECTION) {
                selectionColor = 10 << 20;
                selectionColor |= ((i + 1) << 10);
                selectionColor |= ((j + 1) << 8);
                tTile->updateSelectionWindow(
                        static_cast<int>(playerT[0]), static_cast<int>(playerT[1]),
                        playerW[0], playerW[2]);
            }
            tTile->renderWater(lodx, lodz, playerT[0] + i, playerT[1] + j, playerW, target, fov, layer, selectionColor);
            gluu->mvPopMatrix();
        }
    }
}

void TerrainLibQt::renderWaterLo(GLUU* gluu, float* playerT, float* playerW, float* target, float fov, int renderMode, int layer) {
    int renderCount = 90*90 ;
    if (renderMode == gluu->RENDER_SELECTION) {
        renderCount = 9;
    }

    gluu->currentShader->setUniformValue(gluu->currentShader->shaderAlpha, 0.0f);
    gluu->enableNormals();

    Terrain *tTile;
    int selectionColor = 0;
    unsigned int terrainNameId;
    for (int n = -1, i = 0, j = 0; n < renderCount; n+=16) {
        if (n != -1)
            spiralLoop(n, i, j);

            terrainNameId = quadTreeLo->getMyNameId((int) playerT[0] + i, -(int) playerT[1] - j);
            if (terrainNameId == 0)
                continue;
            if (terrainQtLo[terrainNameId] == NULL) {
                terrainQtLo[terrainNameId] = new TerrainInfo();
                quadTreeLo->fillTerrainInfo((int) playerT[0] + i, -(int) playerT[1] - j, terrainQtLo[terrainNameId]);
                qDebug() << terrainNameId;
                terrainQtLo[terrainNameId]->t = new Terrain(terrainQtLo[terrainNameId]);
            }
            if (terrainQtLo[terrainNameId]->rendered)
                continue;
            terrainQtLo[terrainNameId]->rendered = true;
            tTile = terrainQtLo[terrainNameId]->t;

            if (tTile->loaded == false)
                continue;

            if (tTile->loaded) {
                float lodx = 2048 * i - playerW[0];
                float lodz = 2048 * j - playerW[2];
                gluu->mvPushMatrix();
                Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, 2048 * i, Game::currentRoute->env->water[layer].height, 2048 * j);
                gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->mvMatrix));
                if (renderMode == gluu->RENDER_SELECTION) {
                    selectionColor = 10 << 20;
                    selectionColor |= ((i + 1) << 10);
                    selectionColor |= ((j + 1) << 8);
                    tTile->updateSelectionWindow(
                            static_cast<int>(playerT[0]), static_cast<int>(playerT[1]),
                            playerW[0], playerW[2]);
                }
                tTile->renderWater(lodx, lodz, playerT[0] + i, playerT[1] + j, playerW, target, fov, layer, selectionColor);
                gluu->mvPopMatrix();
            }
        }

    QHashIterator<unsigned int, TerrainInfo*> i(terrainQtLo);
    while (i.hasNext()) {
        i.next();
        if (i.value() == NULL) continue;
        i.value()->rendered = false;
    }
}

void TerrainLibQt::renderShadowMap(GLUU *gluu, float * playerT, float* playerW, float* target, float fov) {
    gluu->currentShader->setUniformValue(gluu->currentShader->shaderAlpha, 0.0f);
    gluu->enableNormals();

    Terrain *tTile;
    int i = 0, j = 0;
    QHash<QString, bool> rendered;
    for (int n = -1; n < 9 - 1; n++) {
        if (n != -1)
            spiralLoop(n, i, j);

        tTile = getTerrainByXY((int) playerT[0] + i, (int) playerT[1] + j, true);
        if(tTile == NULL)
            continue;
        if (tTile->loaded == false)
            continue;
        if (rendered[tTile->name])
            continue;
        rendered[tTile->name] = true;
        
        if (tTile->loaded) {
            float lodx = 2048 * i - playerW[0];
            float lodz = 2048 * j - playerW[2];
            gluu->mvPushMatrix();
            Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, 2048 * i, 0, 2048 * j);
            gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->mvMatrix));
            tTile->render(lodx, lodz, playerT[0]+i, playerT[1]+j, playerW, target, fov, 1);
            gluu->mvPopMatrix();
        }
    }
}

void TerrainLibQt::renderEmpty(GLUU *gluu, float * playerT, float* playerW, float* target, float fov) {
    int i = 0, j = 0;
    for (int n = -1; n < 9 - 1; n++) {
        if (n != -1)
            spiralLoop(n, i, j);
        getTerrainByXY((int) playerT[0] + i, (int) playerT[1] + j, true);
    }
}

void TerrainLibQt::pushRenderItems(float * playerT, float* playerW, float* target, float fov, int renderMode) {
    int renderCount = (Game::tileLod * 2 + 1)*(Game::tileLod * 2 + 1);
    if (renderMode == Game::currentRenderer->RENDER_SELECTION)
        renderCount = 9;

    //gluu->currentShader->setUniformValue(gluu->currentShader->shaderAlpha, 0.0f);
    //gluu->enableNormals();

    Terrain *tTile;
    int selectionColor = 0;
    QHash<QString, bool> rendered;
    
    for (int n = -1, i = 0, j = 0; n < renderCount - 1; n++) {
        if (n != -1)
            spiralLoop(n, i, j);

        tTile = getTerrainByXY((int) playerT[0] + i, (int) playerT[1] + j, true);
        if(tTile == NULL)
            continue;
        
        tTile->inUse = true;
        if (tTile->loaded == false)
            continue;
        if (rendered[tTile->name])
            continue;
        rendered[tTile->name] = true;

        if (tTile->loaded) {
            float lodx = 2048 * i - playerW[0];
            float lodz = 2048 * j - playerW[2];
            Game::currentRenderer->mvPushMatrix();
            Mat4::translate(Game::currentRenderer->mvMatrix, Game::currentRenderer->mvMatrix, 2048 * i, 0, 2048 * j);
            if (renderMode == Game::currentRenderer->RENDER_SELECTION) {
                selectionColor = 10 << 20;
                selectionColor |= ((i + 1) << 10);
                selectionColor |= ((j + 1) << 8);
                tTile->updateSelectionWindow(
                        static_cast<int>(playerT[0]), static_cast<int>(playerT[1]),
                        playerW[0], playerW[2]);
            }
            tTile->pushRenderItem(lodx, lodz, playerT[0] + i, playerT[1] + j, playerW, target, fov, selectionColor);
            Game::currentRenderer->mvPopMatrix();
        }
    }
    
    if(renderMode == Game::currentRenderer->RENDER_SELECTION)
        return;
    
    QHashIterator<unsigned int, TerrainInfo*> i(terrainQt);
    /*while (i.hasNext()) {
        i.next();
        if (i.value() == NULL) continue;
        Terrain* obj = (Terrain*) i.value()->t;
        if(obj == NULL) continue;
        if(!obj->inUse && obj->loaded && !obj->isModified() && !obj->isSelected()){
           delete obj;
           i.value()->t = NULL;
       } else {
           obj->inUse = false;
       }
    }*/
}

void TerrainLibQt::render(GLUU *gluu, float * playerT, float* playerW, float* target, float fov, int renderMode) {
    int renderCount = (Game::tileLod * 2 + 1)*(Game::tileLod * 2 + 1);
    if (renderMode == gluu->RENDER_SELECTION)
        renderCount = 9;

    gluu->currentShader->setUniformValue(gluu->currentShader->shaderAlpha, 0.0f);
    gluu->enableNormals();

    Terrain *tTile;
    int selectionColor = 0;
    QHash<QString, bool> rendered;
    
    for (int n = -1, i = 0, j = 0; n < renderCount - 1; n++) {
        if (n != -1)
            spiralLoop(n, i, j);

        tTile = getTerrainByXY((int) playerT[0] + i, (int) playerT[1] + j, true);
        if(tTile == NULL)
            continue;
        
        tTile->inUse = true;
        if (tTile->loaded == false)
            continue;
        if (rendered[tTile->name])
            continue;
        rendered[tTile->name] = true;

        if (tTile->loaded) {
            float lodx = 2048 * i - playerW[0];
            float lodz = 2048 * j - playerW[2];
            gluu->mvPushMatrix();
            Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, 2048 * i, 0, 2048 * j);
            if (renderMode == gluu->RENDER_SELECTION) {
                selectionColor = 10 << 20;
                selectionColor |= ((i + 1) << 10);
                selectionColor |= ((j + 1) << 8);
                tTile->updateSelectionWindow(
                        static_cast<int>(playerT[0]), static_cast<int>(playerT[1]),
                        playerW[0], playerW[2]);
            }
            tTile->render(lodx, lodz, playerT[0] + i, playerT[1] + j, playerW, target, fov, selectionColor);
            gluu->mvPopMatrix();
        }
    }
    
    if(renderMode == gluu->RENDER_SELECTION)
        return;
    
    QHashIterator<unsigned int, TerrainInfo*> i(terrainQt);
    while (i.hasNext()) {
        i.next();
        if (i.value() == NULL) continue;
        Terrain* obj = (Terrain*) i.value()->t;
        if(obj == NULL) continue;
        if(!obj->inUse && obj->loaded && !obj->isModified() && !obj->isSelected()){
           delete obj;
           i.value()->t = NULL;
       } else {
           obj->inUse = false;
       }
    }

}

void TerrainLibQt::renderLo(GLUU *gluu, float * playerT, float* playerW, float* target, float fov, int renderMode) {
    int distantCount = Game::distantLod/1000 - 10;
    int renderCount = distantCount*distantCount ;
    if (renderMode == gluu->RENDER_SELECTION) {
        renderCount = 9;
    }

    gluu->currentShader->setUniformValue(gluu->currentShader->shaderAlpha, 0.0f);
    gluu->enableNormals();

    Terrain *tTile;
    int selectionColor = 0;
    unsigned int terrainNameId;
    for (int n = -1, i = 0, j = 0; n < renderCount; n+=16) {
        if (n != -1)
            spiralLoop(n, i, j);

            terrainNameId = quadTreeLo->getMyNameId((int) playerT[0] + i, -(int) playerT[1] - j);
            if (terrainNameId == 0)
                continue;
            if (terrainQtLo[terrainNameId] == NULL) {
                terrainQtLo[terrainNameId] = new TerrainInfo();
                quadTreeLo->fillTerrainInfo((int) playerT[0] + i, -(int) playerT[1] - j, terrainQtLo[terrainNameId]);
                qDebug() << terrainNameId;
                terrainQtLo[terrainNameId]->t = new Terrain(terrainQtLo[terrainNameId]);
            }
            if (terrainQtLo[terrainNameId]->rendered)
                continue;
            terrainQtLo[terrainNameId]->rendered = true;
            tTile = terrainQtLo[terrainNameId]->t;

            if (tTile->loaded == false)
                continue;

            if (tTile->loaded) {
                float lodx = 2048 * i - playerW[0];
                float lodz = 2048 * j - playerW[2];
                gluu->mvPushMatrix();
                Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, 2048 * i, 0, 2048 * j);
                if (renderMode == gluu->RENDER_SELECTION) {
                    selectionColor = 10 << 20;
                    selectionColor |= ((i + 1) << 10);
                    selectionColor |= ((j + 1) << 8);
                    tTile->updateSelectionWindow(
                            static_cast<int>(playerT[0]), static_cast<int>(playerT[1]),
                            playerW[0], playerW[2]);
                }
                tTile->render(lodx, lodz, playerT[0] + i, playerT[1] + j, playerW, target, fov, selectionColor);
                gluu->mvPopMatrix();
            }
        }

    QHashIterator<unsigned int, TerrainInfo*> i(terrainQtLo);
    while (i.hasNext()) {
        i.next();
        if (i.value() == NULL) continue;
        i.value()->rendered = false;
    }
}

void TerrainLibQt::spiralLoop(int n, int &x, int &y) {
    int r = floor((sqrt(n + 1) - 1) / 2) + 1;
    int p = (8 * r * (r - 1)) / 2;
    int en = r * 2;
    int a = (1 + n - p) % (r * 8);

    switch ((int) (floor((float) a / (r * 2)))) {
        case 0:
            x = a - r;
            y = -r;
            break;
        case 1:
            x = r;
            y = (a % en) - r;
            break;
        case 2:
            x = r - (a % en);
            y = r;
            break;
        case 3:
            x = -r;
            y = r - (a % en);
            break;
    }
}

bool TerrainLibQt::saveEmpty(int x, int z, TerrainHeightProfile profile,
                             int patches,
                             bool overwrite) {
    if (!Game::writeEnabled || quadTree == NULL)
        return false;
    currentQuadTree = quadTree;
    currentQt = &terrainQt;
    const TerrainGridLayout layout = TerrainGridLayout::profile(profile, patches);
    if (layout.sampleCount == 0)
        return false;
    currentQuadTree->addTile(x, z);
    const QString name = currentQuadTree->getMyName(x, z);
    return Terrain::SaveEmpty(name, layout.sampleCount, layout.sampleSpacing,
                              layout.patchesPerSide, false, overwrite);
}

bool TerrainLibQt::hasDetailedTerrain(int x, int z) {
    if (quadTree == NULL || quadTree->getMyNameId(x, z) == 0)
        return false;
    const QString name = quadTree->getMyName(x, z);
    return QFile::exists(Game::root + "/routes/" + Game::route
                         + "/tiles/" + name + ".t");
}
