/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TERRAIN_H
#define	TERRAIN_H
#include <QString>
#include <tsre/ogl/GLUU.h>
#include <tsre/world/TFile.h>
#include <tsre/world/TerrainGridLayout.h>
#include <tsre/world/TerrainLod.h>
#include <tsre/world/TerrainAdjacentEdge.h>
#include <tsre/math3d/Vector3f.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/GameObj.h>
#include <array>

class Brush;
class TerrainInfo;
class FileBuffer;
class QDataStream;
class TerrainMeshBackend;
class TerrainMeshLegacy;
class TerrainMeshPaged;

class Terrain : public GameObj {
    Q_OBJECT
public:
    static Brush* DefaultBrush;
    
    int loaded = false;
    float **terrainData = NULL;
    bool inUse = true;
    bool showBlob = false;
    float mojex = 0;
    float mojez = 0;
    QString name;
    bool lowTile = false;
    std::array<TerrainAdjacentEdge, 4> adjacentEdges;
    Terrain();
    Terrain(TerrainInfo *ti);
    Terrain(float x, float y);
    Terrain(const Terrain& orig);
    virtual void saveTfileToStream(QDataStream &out);
    virtual void saveRAWfileToStream(QDataStream &out);
    virtual void saveRAWfileToStreamFloat(QDataStream &out);
    virtual void saveFfileToStream(QDataStream &out);
    virtual void loadTFile(FileBuffer *data);
    virtual void loadRAWFile(FileBuffer *data);
    virtual void loadFFile(FileBuffer *data);
    virtual void updateTFile();
    virtual ~Terrain();
    static bool SaveEmpty(QString name, int samples = 256, int sampleSize = 8,
                          int patches = TerrainGridLayout::SupportedPatchesPerSide,
                          bool low = false, bool overwrite = false);
    static QString getTileName(int x, int y);
    static QString getTileNameExperimental(int x, int y);
    static QString getTileNameExperimental2(int x, int y);
    QString getTileName();
    bool isXYinside(int x, int y);
    int getPatchSize();
    int getSampleSize();
    void getLocalCoords(int x, int z, float &posx, float &posz);
    void getPatchCoords(int &x, int &z, float &posx, float &posz);
    void getCornerCoordsXY(int &x, int &z, int ox, int oz);
    void fillTerrainDataX();
    void fillTerrainDataY();
    void fillTerrainDataXY();
    void save();
    void refresh();
    void refreshAll();
    void refreshModified();
    void invalidatePatch(int patchId, unsigned int reasons);
    void invalidateAll(unsigned int reasons);
    void invalidateSamples(int minX, int minZ, int maxX, int maxZ,
                           unsigned int reasons);
    void invalidateSynthesizedSamples(int minX, int minZ,
                                      int maxX, int maxZ,
                                      unsigned int reasons);
    bool isModified();
    void setModified(bool value = true);
    void getLowCornerTileXY(int &X, int &Y);
    int getSampleCount();
    int getPatchResolution() const;
    const TerrainGridLayout& getGridLayout() const;
    QVector<quint8> getPatchGapState();
    bool isEditable() const;
    float setHeight(int x, int z, float posx, float posz, float val, bool add = false);
    void setFixedHeight(float val);
    void paintTexture(Brush* brush, int x, int z, float posx, float posz);
    void lockTexture(Brush* brush, int x, int z, float posx, float posz);
    void setTexture(Brush* brush, int x, int z, float posx, float posz);
    void setTexture(Brush* brush, int u);
    void setTexture(QString textureName, int x, int z, float posx, float posz, QString transformation = "");
    void toggleWaterDraw(int x, int z, float posx, float posz, float direction);
    void setWaterLevelGui();
    void setWaterLevel(float nw, float ne, float sw, float se);
    float getAvgVaterLevel();
    void getWaterLevels(float *w);
    float getWaterLevelNW();
    float getWaterLevelNE();
    float getWaterLevelSW();
    float getWaterLevelSE();
    void setWaterLevelNW(float val);
    void setWaterLevelNE(float val);
    void setWaterLevelSW(float val);
    void setWaterLevelSE(float val);
    void setAvgWaterLevel(float val);
    void getAdjacentWaterLevels(float *w);
    void setAdjacentWaterLevels(float *w);
    void toggleDraw(int x, int z, float posx, float posz);
    void setWaterDraw();
    void setDraw();
    void hideWaterDraw();
    void hideDraw();
    void toggleDraw();
    void toggleWaterDraw();
    void setDrawAdjacent();
    void rotatePatchTexture();
    void mirrorXPatchTexture();
    void mirrorYPatchTexture();
    float getPatchScaleTex();
    float getPatchScaleTexX();
    float getPatchScaleTexY();
    QString getPatchRotationName();
    void scalePatchTexCoords(float val);
    void scalePatchTexCoordsX(float val);
    void scalePatchTexCoordsY(float val);
    QString getPatchTexTransformString();
    QString getPatchTexTransformString(int u);
    QString getPatchTexTransformString(int x, int z, float posx, float posz);
    void setPatchTexTransform(QString val);
    void setPatchTexTransform(QString val, int u);
    void removeAllGaps();
    void toggleGaps(int x, int z, float posx, float posz, float direction);
    void setErrorBias(int x, int z, float posx, float posz, float val);
    void setPatchErrorBias(int patchId, float value);
    float getErrorBias();
    void getWTileIds(QSet<int> &ids);
    void setErrorBias(float val);
    void setAllErrorBias(float val);
    void setTileBlob();
    void makeTextureFromMap();
    void removeTextureFromMap();
    void fillHeightMap(float *data);
    int getTexture(int x, int z, float posx, float posz);
    int getSelectedPathId();
    int getSelectedShaderId();
    QString getPatchMainTextureName();
    QString getPatchMainTextureName(int u);
    QString getPatchMainTextureName(int x, int z, float posx, float posz);
    int getPatchFlags(int x, int z, float posx, float posz);
    void setPatchFlags(int x, int z, float posx, float posz, int val);
    bool select(int value);
    bool select(int value, bool oneMore);
    bool unselect();
    void resetPatchTexCoords(int uu = -1);
    void pushContextMenuActions(QMenu *menu);
    void pushRenderItem(float lodx, float lodz, int tileX, int tileY, float* playerW, float* target, float fov, quint32 selectionId);
    void pushRenderItemWater(float lodx, float lodz, float tileX, float tileY, float* playerW, float* target, float fov, int layer, quint32 selectionId = 0);
    void render(float lodx, float lodz, int tileX, int tileY, float* playerW, float* target, float fov, quint32 selectionId);
    void renderWater(float lodx, float lodz, float tileX, float tileY, float* playerW, float* target, float fov, int layer, quint32 selectionId = 0);
    void refreshWaterShapes();
    void getRotation(float *rot, int x, int z, int posx, int posz);
    float getHeight(int x, int z, float posx, float posz, bool addR);
    
public slots:
    void menuToggleWater();
    void menuToggleDraw();
    void menuPutTexture();
    void menuSelectObjects();
    
protected:
    static QString TileDir[2];
    
    unsigned char **fData = NULL;
    bool jestF = false;
    bool modifiedF = false;
    bool isOgl = false;
    bool modified = false;
    bool editable = false;
    QString texturepath;
    QString rootTexturepath;
    Vector3f **vertexData = NULL;
    Vector3f **normalData = NULL;
    bool hidden[TerrainGridLayout::SupportedPatchRecordCount];
    bool uniqueTex[TerrainGridLayout::SupportedPatchRecordCount];
    int texid[TerrainGridLayout::SupportedPatchRecordCount];
    int texid2[TerrainGridLayout::SupportedPatchRecordCount];
    bool texModified[TerrainGridLayout::SupportedPatchRecordCount];
    bool texLocked[TerrainGridLayout::SupportedPatchRecordCount];
    bool selectedPatchs[TerrainGridLayout::SupportedPatchRecordCount];
    QOpenGLBuffer *VBO = NULL;
    QOpenGLVertexArrayObject *VAO = NULL;
    TerrainMeshBackend *meshBackend = NULL;

    OglObj lines;
    OglObj mlines;
    OglObj slines;
    OglObj ulines;
    OglObj lockedlines;
    OglObj selectedlines;
    
    struct WaterTile {
        OglObj w[TerrainGridLayout::SupportedPatchRecordCount];
    };
    QMap<int, WaterTile*> water;
    //OglObj water[256];
    
    
    OglObj terrainBlob;
    //QOpenGLBuffer wVBO[256];
    //QOpenGLVertexArrayObject wVAO[256];
    //bool jestW[256];
    int wTexid = -1;
    TFile* tfile = NULL;
    TerrainGridLayout gridLayout;
    int terrainDataRows = 0;
    int fDataRows = 0;
    struct PatchBounds {
        float centerX = 0.0f;
        float averageY = 0.0f;
        float centerZ = 0.0f;
        float sphereRadius = 0.0f;
        float rangeY = 0.0f;
        float horizontalRadius = 0.0f;
        bool valid = false;
    };
    struct FrustumPlane {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
    };
    struct PatchVisibility {
        std::array<FrustumPlane, 6> planes;
        float cameraLocalX = 0.0f;
        float cameraLocalZ = 0.0f;
        float maximumDistance = 0.0f;
        bool valid = false;
    };
    QVector<PatchBounds> patchBounds;
    QVector<quint8> patchBoundsDirty;
    QVector<quint8> patchGapState;
    bool lodProfileWarningShown = false;
    //int selectedPathId = -1;
    
    void saveRAW(QString name);
    void saveRAW(QDataStream &write);
    void saveRAWFloat(QDataStream &write);
    bool readRAW(QString fSfile);
    void readRAW(FileBuffer *data);
    void readRAWFloat(FileBuffer *data);
    bool readF(QString fSfile);
    void readF(FileBuffer *data);
    void saveF(QString name);
    void saveF(QDataStream &write);
    void newF();
    void vertexInit();
    void normalInit();
    void oglInit();
    void initBlob();
    void rotateTex(int idx);
    void mirrorXTex(int idx);
    void mirrorYTex(int idx);
    void scaleTex(int idx, float val);
    void scaleTexX(int idx, float val);
    void scaleTexY(int idx, float val);
    float getScaleTexY(int idx);
    float getScaleTexX(int idx);
    float getScaleTex(int idx);
    void convertTexToDefaultCoords(int idx);
    void paintTextureOnTile(Brush* brush, int y, int u, float x, float z);
    void reloadLines();
    
    virtual void load();
    bool validateGridLayout(const QString &source);
    bool validatePayload(const FileBuffer *data, std::size_t bytesPerCell,
                         const QString &kind) const;
    void releaseHeightData();
    void releaseFData();
    TerrainMeshBackend *ensureMeshBackend();
    int ensureMapTexture();
    void initializePatchBounds();
    void markPatchBoundsDirty(int patchId);
    void markPatchBoundsDirtyForSamples(int minX, int minZ,
                                        int maxX, int maxZ);
    void invalidateSamplesLocal(int minX, int minZ, int maxX, int maxZ,
                                unsigned int reasons);
    void refreshPatchBounds(bool updateDescriptor);
    PatchBounds calculatePatchBounds(int patchId) const;
    PatchVisibility buildPatchVisibility(const float *modelMatrix,
                                         const float *cameraPosition) const;
    bool isPatchVisible(int patchId,
                        const PatchVisibility &visibility) const;
    bool patchContainsGap(int patchId);
    void markPatchGapStateDirty(int patchId);
    void markPatchGapStateDirtyForSamples(int minX, int minZ,
                                          int maxX, int maxZ);
    QVector<TerrainPatchLodState> buildPatchLodState(
            const PatchVisibility &visibility);

    friend class TerrainMeshLegacy;
    friend class TerrainMeshPaged;
};

#endif	/* TERRAIN_H */

