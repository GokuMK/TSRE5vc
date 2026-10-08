/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TERRAINLIB_H
#define	TERRAINLIB_H

#include <unordered_map>
#include <QString>
#include <tsre/ogl/GLUU.h>
#include <tsre/world/TerrainGridLayout.h>
#include <tsre/world/TerrainAdjacentEdge.h>
#include <tsre/world/TerrainLod.h>


class RenderQueue;
class Terrain;
class Brush;
class HeightWindow;
class QuadTree;
class TerrainInfo;
class QTextStream;
class FileBuffer;

class TerrainLib {
public:
    TerrainLib();
    TerrainLib(const TerrainLib& orig);
    virtual ~TerrainLib();
    virtual void setDetailedAsCurrent();
    virtual void setDistantAsCurrent();
    // Whether the distant tiles are the ones being edited (current).
    virtual bool distantIsCurrent() const { return false; }
    virtual void loadQuadTreeDetailed(FileBuffer *data);
    virtual void loadQuadTreeDistant(FileBuffer *data);
    virtual QuadTree* getQuadTreeDetailed();
    virtual QuadTree* getQuadTreeDistant();
    virtual void saveQtLoToStream(QTextStream &out);
    virtual void saveQtToStream(QTextStream &out);
    virtual Terrain* getTerrainByXY(int x, int y, bool load = false);
    // A tile with at least its tile file read (Terrain::descriptorLoaded),
    // without reading heights: what map mode draws. A complete tile is
    // returned as it is; getTerrainByXY(x, y, true) completes the others.
    virtual Terrain* getTerrainDescriptor(int x, int y);
    // The same for the distant terrain tile covering a world tile.
    virtual Terrain* getDistantDescriptor(int x, int y);
    // The detailed (or distant) terrain tile covering a world tile, without
    // loading it: 0 for none, the same for every world tile it covers. info,
    // when given, gets its corner and size (level, in world tiles).
    virtual unsigned int terrainTileId(int x, int y, bool distant = false,
                                       TerrainInfo *info = nullptr);
    virtual void fillRaw(Terrain *cTerr, int mojex, int mojez);
    const TerrainAdjacentEdge &resolveAdjacentEdge(Terrain &terrain,
                        TerrainEdgeSide side, TerrainEdgeDiscovery mode);
    void fillCachedRaw(Terrain &terrain);
    // Prepare once before a render traversal, then clear after submission.
    void prepareTerrainLod(const QVector<Terrain*> &terrains,
                           const QVector<TerrainLodLevel> &levels,
                           double cameraX, double cameraZ);
    const QVector<TerrainPatchLodState> *preparedPatchLod(Terrain *terrain) const {
        const auto it = preparedTerrainLod.constFind(terrain);
        return it == preparedTerrainLod.constEnd() ? nullptr : &it.value();
    }
    void clearPreparedTerrainLod() { preparedTerrainLod.clear(); }
    virtual void terrainAvailabilityChanged(Terrain *terrain);
    virtual void terrainSamplesChanged(Terrain *source,
                                       int minX, int minZ,
                                       int maxX, int maxZ,
                                       unsigned int reasons);
    virtual float getHeight(int x, int z, float posx, float posz);
    virtual float getHeight(int x, int z, float posx, float posz, bool addR);
    virtual bool tryGetHeight(int x, int z, float posx, float posz,
                              float &height, bool addR = false,
                              bool loadIfNeeded = false);
    virtual void getRotation(float *rot, int x, int z, float posx, float posz);
    virtual void setHeight(int x, int z, float posx, float posz, float h);
    virtual void fillHeightMap(int x, int z, float *data);
    virtual void fillWaterLevels(float *w, int mojex, int mojez);
    virtual void setWaterLevels(float *w, int mojex, int mojez);
    virtual void setHeightFromGeoGui(int x, int z, float* p);
    virtual void setHeightFromGeo(int x, int z, float* p);
    virtual void setDetailedTerrainAsCurrent();
    virtual void setLowTerrainAsCurrent();
    virtual bool isLoaded(int x, int z);
    virtual QSet<Terrain*> paintHeightMap(Brush* brush, int x, int z, float* p);
    virtual void paintTexture(Brush* brush, int x, int z, float* p);
    // operation follows TerrainMaterialMap::EditOperation; fills affect one tile.
    void paintProceduralTexture(Brush* brush, int x, int z, float* p, int operation = 0);
    virtual void lockTexture(Brush* brush, int x, int z, float* p);
    virtual void setTerrainTexture(Brush* brush, int x, int z, float* p);
    virtual void toggleGaps(int x, int z, float* p, float direction);
    virtual void toggleWaterDraw(int x, int z, float* p, float direction);
    virtual void setWaterLevelGui(int x, int z, float* p);
    virtual void makeTextureFromMap(int x, int z, float* p);
    virtual void removeTileTextureFromMap(int x, int z, float* p);
    virtual void setFixedTileHeight(Brush* brush, int x, int z, float* p);
    virtual void toggleDraw(int x, int z, float* p);
    virtual void setTileBlob(int x, int z, float* p);
    virtual void setTextureToTrackObj(Brush* brush, float* punkty, int length, int x, int z);
    virtual void setTerrainToTrackObj(Brush* brush, float* punkty, int length,
                                      int x, int z, float* matrix,
                                      float offsetY = 0,
                                      bool connectedPath = false);
    virtual int getTexture(int x, int z, float* p);
    virtual bool load(int x, int z);
    virtual void getUnsavedInfo(QVector<QString> &items);
    virtual void save();
    virtual void reloadProceduralBakeMetadata() {}
    virtual void refresh(int x, int z);
    virtual bool reload(int x, int z);
    virtual void loadQuadTree();
    virtual bool createNewRouteTerrain(int x, int z);
    virtual void saveEmpty(int x, int z);
    virtual bool saveEmpty(int x, int z, TerrainHeightProfile profile,
                           int patches,
                           bool overwrite = false);
    virtual bool hasDetailedTerrain(int x, int z);
    virtual void fillTerrainData(Terrain *tTile, float *offsetXYZ);
    virtual void updateTerrainHeightmap(Terrain *t);
    virtual void updateTerrainTFile(Terrain *t);
    virtual void pushRenderItems(RenderQueue &queue, float* playerT, float* playerW, float* target, float fov, int renderMode);
    // Distant terrain, water and distant water.
    virtual void pushRenderItemsLo(RenderQueue &queue, float* playerT, float* playerW, float* target, float fov, int renderMode);
    virtual void pushRenderItemsWater(RenderQueue &queue, float* playerT, float* playerW, float* target, float fov, int renderMode, int layer);
    virtual void pushRenderItemsWaterLo(RenderQueue &queue, float* playerT, float* playerW, float* target, float fov, int renderMode, int layer);
    
protected:
    QHash<Terrain*, QVector<TerrainPatchLodState>> preparedTerrainLod;
    QSet<QString> terrainLodWarnings;
    virtual Terrain *edgeTerrainAt(int worldX, int worldZ, bool low, bool load);
    HeightWindow* heightWindow = NULL;
};

#endif	/* TERRAINLIB_H */

