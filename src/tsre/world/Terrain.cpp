/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/Terrain.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <QDebug>
#include <tsre/Game.h>
#include <QFile>
#include <QFileInfo>
#include <QOpenGLExtraFunctions>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/texture/TexLib.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/texture/Brush.h>
#include <routeEditor/TerrainWaterWindow.h>
#include <tsre/geo/MapWindow.h>
#include <tsre/world/Route.h>
#include <tsre/world/Trk.h>
#include <tsre/world/Environment.h>
#include <routeEditor/AboutWindow.h>
#include <tsre/world/TerrainInfo.h>
#include <tsre/world/TerrainMeshBackend.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/renderer/SelectionId.h>

QString Terrain::TileDir[2] = {"tiles", "lo_tiles"};
Brush* Terrain::DefaultBrush = NULL;

static float editorDefaultPatchTextureScale(const TerrainGridLayout &layout) {
    return 0.998f / static_cast<float>(layout.patchResolution);
}

// Keep the stepped radial patch boundary behind the projection far plane.
static constexpr float TerrainPatchCullMargin = 256.0f;

Terrain::Terrain(){

}

Terrain::Terrain(TerrainInfo *ti){
    mojex = ti->cx;
    mojez = -ti->cy;
    name = ti->name;
    lowTile = ti->low;
    load();
}

Terrain::Terrain(float x, float y) {
    mojex = x;
    mojez = y;
    name = getTileName((int) x, (int) -y);
    load();
}

void Terrain::load(){
    typeObj = this->terrainobj;
    loaded = false;
    isOgl = false;
    modified = false;
    editable = false;
    lodProfileWarningShown = false;
    wTexid = -1;
    for (int i = 0; i < TerrainGridLayout::SupportedPatchRecordCount; i++) {
        texid[i] = -1;
        texid2[i] = -1;
        hidden[i] = false;
        texModified[i] = false;
        texLocked[i] = false;
        uniqueTex[i] = false;
        selectedPatchs[i] = false;
    }
    if (Game::terrainMeshMode == Game::TERRAIN_MESH_LEGACY) {
        VBO = new QOpenGLBuffer();
        VAO = new QOpenGLVertexArrayObject();
    }

    int esdAlternativeTexture = 0x01;
    QString seasonPath;
    if((esdAlternativeTexture & Game::TextureFlags[Game::season]) != 0)
        seasonPath = Game::season.toLower() + "/";

    if(Game::season == "Winter" || Game::season == "AutumnSnow" || Game::season == "WinterSnow" || Game::season == "SpringSnow" ){
        if(esdAlternativeTexture & Game::TextureFlags["Snow"] != 0)
            seasonPath = "snow/";
        if(esdAlternativeTexture & Game::TextureFlags["SnowTrack"] != 0)
            seasonPath = "snow/";
    }
    
    texturepath = Game::root + "/routes/" + Game::route + "/terrtex/"+seasonPath;
    rootTexturepath = Game::root + "/routes/" + Game::route + "/terrtex/";
    QString path = Game::root + "/routes/" + Game::route + "/" + TileDir[(int)lowTile] + "/";
    tfile = new TFile();

    //QString filename = getTileName((int) x, (int) -y);
    //QString filename = getTileNameExperimental2((int) x, (int) -y);
    //qDebug() << filename << x << -y;
    if (!tfile->readT((path + name + ".t"))) {
        //qDebug() << " t fail" << name;
        return;
    }
    if (!validateGridLayout(path + name + ".t"))
        return;
    if(tfile->sampleYbuffer == NULL)
        return;
    if (!readRAW((path + *tfile->sampleYbuffer/* + "_y.raw"*/))) {
        //qDebug() << " y fail" << name;
        return;
    }
    if(tfile->sampleFbuffer != NULL)
        jestF = readF(path + *tfile->sampleFbuffer/* + "_f.raw"*/);
    modifiedF = false;
    //qDebug() << " ok";
    
    //QString name = this->getTileName(mojex, -mojez);
    QString name2;
    //int samples = 
    int patches = tfile->patchsetNpatches;
    for (int u = 0; u < patches; u++)
        for (int y = 0; y < patches; y++) {
            name2 = name + "_" + QString::number(y) + "_" + QString::number(u) + ".ace";
            //qDebug() << name2 << (int) tfile->tdata[(y * 16 + u)*13 + 0 + 6];
            //qDebug() << tfile->materialsCount;
            //qDebug() << tfile->materials[(int) tfile->tdata[(y * 16 + u)*13 + 0 + 6]].tex[0];
            //qDebug() << name << patches << tfile->materialsCount << (int) tfile->tdata[(y * patches + u)*13 + 0 + 6];
            if (tfile->materialsCount <= (int) tfile->tdata[(y * patches + u)*13 + 0 + 6])
                continue;
            
            if (name2 == *tfile->materials[(int) tfile->tdata[(y * patches + u)*13 + 0 + 6]].tex[0])
                this->uniqueTex[y*patches+u] = true;
            
            if(Game::seasonalEditing && Game::season.length() > 0){
                // copy missing season textures
                QFile file(texturepath + *tfile->materials[(int) tfile->tdata[(y * patches + u)*13 + 0 + 6]].tex[0]);
                if (!file.exists()){
                    QFile::copy(rootTexturepath + *tfile->materials[(int) tfile->tdata[(y * patches + u)*13 + 0 + 6]].tex[0], texturepath + *tfile->materials[(int) tfile->tdata[(y * patches + u)*13 + 0 + 6]].tex[0]);
                }
            }
        }
    
    loaded = true;
    //save();
}

bool Terrain::isModified() {
    return this->modified;
}

void Terrain::setModified(bool value) {
    if (value && !editable)
        return;
    this->modified = value;
}

float Terrain::setHeight(int x, int z, float posx, float posz, float val, bool add){
    if (!editable)
        return getHeight(x, z, posx, posz, false);
    setErrorBias(x, z, posx, posz, 0);
    getLocalCoords(x, z, posx, posz);
    const int sampleSize = gridLayout.sampleSpacing;
    const int sx = std::clamp(static_cast<int>(std::floor(posx / sampleSize)),
                              0, gridLayout.sampleCount);
    const int sz = std::clamp(static_cast<int>(std::floor(posz / sampleSize)),
                              0, gridLayout.sampleCount);
    
    if(add)
        terrainData[sz][sx] += val;
    else
        terrainData[sz][sx] = val;

    invalidateSamples(sx, sz, sx, sz,
                      TerrainDirtyHeight | TerrainDirtyNormals);
    setModified(true);
    return terrainData[sz][sx];
}

void Terrain::getLowCornerTileXY(int& X, int& Y){
    int samples = *tfile->nsamples;
    int sampleSize = *tfile->sampleSize;
    int tileSize = sampleSize*samples;
    int level = tileSize/TerrainGridLayout::WorldTileSize;
    
    X = mojex;// - level + 1;
    Y = mojez - level + 1;
}


void Terrain::getCornerCoordsXY(int &x, int &z, int ox, int oz){
    x = mojex;
    z = mojez;
    int samples = *tfile->nsamples;
    int sampleSize = *tfile->sampleSize;
    int tileSize = sampleSize*samples;
    int level = tileSize/TerrainGridLayout::WorldTileSize;
    
    //if(level == 1){
        x += level*ox;
        z += level*oz;
    //    return;
    //}
    /*if(ox == -1)
        x -= level;
    if(ox == 1)
        x += level+1;
    if(oz == -1)
        z -= level;
    if(oz == 1)
        z += level;*/
    return;
}

void Terrain::fillTerrainDataX(){
    if (Game::terrainLib == NULL)
        return;
    const int samples = gridLayout.sampleCount;
    const int spacing = gridLayout.sampleSpacing;
    // World-local coordinates are half-open: the exact positive boundary is
    // normalized into the neighboring World cell without moving the sample.
    const float boundaryX = gridLayout.terrainWorldSize
            - TerrainGridLayout::WorldTileHalfSize;
    for (int i = 0; i < samples; ++i) {
        const float boundaryZ = i * spacing - gridLayout.terrainWorldSize
                + TerrainGridLayout::WorldTileHalfSize;
        float height = 0.0f;
        if (Game::terrainLib->tryGetHeight(
                    static_cast<int>(mojex), static_cast<int>(mojez),
                    boundaryX, boundaryZ, height, false, true))
            terrainData[i][samples] = height;
    }
}

void Terrain::fillTerrainDataY(){
    if (Game::terrainLib == NULL)
        return;
    const int samples = gridLayout.sampleCount;
    const int spacing = gridLayout.sampleSpacing;
    const float boundaryZ = TerrainGridLayout::WorldTileHalfSize;
    for (int i = 0; i < samples; ++i) {
        const float boundaryX = i * spacing
                - TerrainGridLayout::WorldTileHalfSize;
        float height = 0.0f;
        if (Game::terrainLib->tryGetHeight(
                    static_cast<int>(mojex), static_cast<int>(mojez),
                    boundaryX, boundaryZ, height, false, true))
            terrainData[samples][i] = height;
    }
}

void Terrain::fillTerrainDataXY(){
    if (Game::terrainLib == NULL)
        return;
    float height = 0.0f;
    if (Game::terrainLib->tryGetHeight(
                static_cast<int>(mojex), static_cast<int>(mojez),
                gridLayout.terrainWorldSize
                    - TerrainGridLayout::WorldTileHalfSize,
                TerrainGridLayout::WorldTileHalfSize,
                height, false, true))
        terrainData[gridLayout.sampleCount][gridLayout.sampleCount] = height;
}
    
int Terrain::getSampleCount(){
    return *tfile->nsamples;
}

int Terrain::getPatchResolution() const {
    return gridLayout.patchResolution;
}

const TerrainGridLayout& Terrain::getGridLayout() const {
    return gridLayout;
}

bool Terrain::isEditable() const {
    return editable;
}

void Terrain::setFixedHeight(float val){
    if (loaded == false || !editable)
        return;
    int samples = *tfile->nsamples;
    for (int i = 0; i < samples; i++)
        for (int j = 0; j < samples; j++) {
            terrainData[i][j] = val;
        }
    invalidateSamples(0, 0, samples, samples,
                      TerrainDirtyHeight | TerrainDirtyNormals);
    setModified(true);
}

Terrain::Terrain(const Terrain& orig) {
}

void Terrain::saveTfileToStream(QDataStream &out){
    this->tfile->save(out);
}

void Terrain::saveRAWfileToStream(QDataStream &out){
    this->saveRAW(out);
}

void Terrain::saveRAWfileToStreamFloat(QDataStream &out){
    this->saveRAWFloat(out);
}

void Terrain::saveFfileToStream(QDataStream &out){
    if(jestF)
        this->saveF(out);
}

void Terrain::loadTFile(FileBuffer *data){
    //qDebug() << "aaa";
    //this->tfile = new TFile();
    this->tfile->load(data);
    validateGridLayout("network terrain descriptor " + name);
    for (int i = 0; i < TerrainGridLayout::SupportedPatchRecordCount; i++) {
        texid[i] = -1;
        texid2[i] = -1;
        hidden[i] = false;
        texModified[i] = false;
        texLocked[i] = false;
        uniqueTex[i] = false;
        //selectedPatchs[i] = false;
    }
}
    
void Terrain::loadRAWFile(FileBuffer *data){
    this->readRAWFloat(data);
}

void Terrain::loadFFile(FileBuffer *data){
    if(data->off == data->length)
        return;
    jestF = true;
    this->readF(data);
}

Terrain::~Terrain() {
    long timeNow1 = QDateTime::currentMSecsSinceEpoch();
    releaseHeightData();
    releaseFData();
    if (this->loaded) {
        //for (int i = 0; i < 256; i++) {
        //    //delete VBO[i];
        //    //delete VAO[i];
        //    //GC::VBO.push_back(VBO[i]);
        //    //GC::VAO.push_back(VAO[i]);
        //}
        //delete[] VBO;
        //delete[] VAO;
    }
    delete meshBackend;
    meshBackend = NULL;
    delete VBO;
    VBO = NULL;
    delete VAO;
    VAO = NULL;
    delete tfile;
    tfile = NULL;
    long timeNow2 = QDateTime::currentMSecsSinceEpoch();
    qDebug() << "= release terrain "<< timeNow2 - timeNow1;
}

bool Terrain::SaveEmpty(QString name, int samples, int sampleSize, int patches,
                        bool low, bool overwrite) {
    if (!Game::writeEnabled) {
        qWarning() << "Terrain creation is disabled because route writing is disabled";
        return false;
    }
    qDebug() << "New terrain tile";
    TerrainGridLayout layout;
    QString layoutError;
    if (!TerrainGridLayout::tryCreate(samples, static_cast<float>(sampleSize),
                                      patches, 0.0f, layout, layoutError)) {
        qWarning() << "Unsupported terrain layout" << name << layoutError;
        return false;
    }
    QString path;
    if(low){
        if(!QDir(Game::root + "/routes/" + Game::route + "/lo_tiles/").exists())
            QDir().mkdir(Game::root + "/routes/" + Game::route + "/lo_tiles/");
        path = Game::root + "/routes/" + Game::route + "/lo_tiles/" + name + "_y.raw";
    } else {
        path = Game::root + "/routes/" + Game::route + "/tiles/" + name + "_y.raw";
    }
    QFile file(path);
    if (overwrite && file.exists() && !file.remove()) {
        qWarning() << "Unable to replace terrain elevation file" << path;
        return false;
    }
    if (!file.exists()){
        if(!file.open(QIODevice::WriteOnly)){
            qDebug() << "Error creating raw file!";
            return false;
        }
        QDataStream write(&file);
        write.setByteOrder(QDataStream::BigEndian);
        unsigned short value = 128;
        for (int i = 0; i < samples; i++)
            for (int j = 0; j < samples; j++)
                write << value;
        file.close();
        std::size_t expectedBytes = 0;
        layout.expectedPayloadBytes(sizeof(quint16), expectedBytes);
        if (static_cast<std::size_t>(QFileInfo(path).size()) != expectedBytes) {
            qWarning() << "Incomplete terrain elevation file" << path
                       << "expected" << expectedBytes << "actual" << QFileInfo(path).size();
            QFile::remove(path);
            return false;
        }
    }

    const QString directory = QFileInfo(path).absolutePath() + "/";
    if (overwrite) {
        // Discard stale derived data, but keep the E/N resource names in the
        // replacement descriptor so MSTS Route Editor can regenerate them.
        QFile::remove(directory + name + "_e.raw");
        QFile::remove(directory + name + "_n.raw");
    }

    TFile *tfile = new TFile();
    tfile->initNew(name, samples, sampleSize, patches);
    if(low){
        if(!QDir(Game::root + "/routes/" + Game::route + "/lo_tiles/").exists())
            QDir().mkdir(Game::root + "/routes/" + Game::route + "/lo_tiles/");
        tfile->save(Game::root + "/routes/" + Game::route + "/lo_tiles/" + name + ".t");
    } else {
        tfile->save(Game::root + "/routes/" + Game::route + "/tiles/" + name + ".t");
    }
    delete tfile;
    return true;
}

QString Terrain::getTileName(){
    return name;
    //return getTileName(mojex, -mojez);
}

QString Terrain::getTileNameExperimental(int x, int y){
    int offset = 16384;
    x += offset;
    y += offset;
    unsigned int numer = 0;
    for (int tx = offset, ty = tx, i = tx, sign_xs, sign_ys; i > 0; i /= 2, tx += i, ty += i, numer <<= 2) {
        sign_xs = ((unsigned)(x-tx)>>31);
        sign_ys = ((unsigned)(y-ty)>>31);
        numer |= (sign_ys*2+!(sign_xs^sign_ys));
        tx -= sign_xs*i;
        ty -= sign_ys*i;
    }
    QString name = QString::number(numer, 16);
    int len = 8 - name.length();
    for (int i = 0; i < len; i++)
        name = "0" + name;
    //qDebug() << name;
    return "-" + name;
}

QString Terrain::getTileNameExperimental2(int x, int y){
    int o = 16384;
    x += o;
    y += o;
    unsigned int n = 0;
    for (int tx=o, ty=tx, i=tx, sx, sy; i>0; sx=((unsigned)(x-tx)>>31), sy=((unsigned)(y-ty)>>31), n|=(sy*2+!(sx^sy)), tx-=sx*i-i/2, ty-=sy*i-i/2, i/=2, n<<=2);
    QString name = QString::number(n, 16);
    int len = 8 - name.length();
    for (int i = 0; i < len; i++)
        name = "0" + name;
    return "-" + name;
}

QString Terrain::getTileName(int x, int y) {
    int offset = 16384;
    int xs = offset;
    int ys = offset;
    x += offset;
    y += offset;
    unsigned int numer = 0;
    
    for (int i = offset / 2, j = 30; j > 0; i /= 2, j -= 2) {
        if (x < xs && y < ys) {
            numer = numer | (3 << j);
            xs -= i;
            ys -= i;
        } else if (x < xs) {
            xs -= i;
            ys += i;
        } else if (y < ys) {
            numer = numer | (2 << j);
            xs += i;
            ys -= i;
        } else {
            numer = numer | (1 << j);
            xs += i;
            ys += i;
        }
    }

    QString name = QString::number(numer, 16);
    int len = 8 - name.length();
    for (int i = 0; i < len; i++)
        name = "0" + name;
    //qDebug() << name;
    return "-" + name;
}

void Terrain::refresh() {
    refreshAll();
}

void Terrain::refreshAll() {
    if (!loaded) return;
    patchBoundsDirty.fill(1, gridLayout.patchRecordCount());
    if (meshBackend != NULL)
        meshBackend->invalidateAll();
    else
        isOgl = false;
    lines.loaded = false;
    //reloadLines();
}

void Terrain::refreshModified() {
    refreshPatchBounds(true);
    if (meshBackend != NULL)
        meshBackend->refreshModified();
}

void Terrain::invalidatePatch(int patchId, unsigned int reasons) {
    if (!loaded)
        return;
    if (reasons & TerrainDirtyHeight)
        markPatchBoundsDirty(patchId);
    if (reasons & TerrainDirtyGaps)
        markPatchGapStateDirty(patchId);
    if (meshBackend != NULL)
        meshBackend->invalidatePatch(patchId, reasons);
    else
        isOgl = false;
}

void Terrain::invalidateAll(unsigned int reasons) {
    if (!loaded)
        return;
    if (reasons & TerrainDirtyHeight)
        patchBoundsDirty.fill(1, gridLayout.patchRecordCount());
    if (reasons & TerrainDirtyGaps)
        patchGapState.fill(2, gridLayout.patchRecordCount());
    if (meshBackend != NULL)
        meshBackend->invalidateAll(reasons);
    else
        isOgl = false;
}

void Terrain::invalidateSamples(int minX, int minZ, int maxX, int maxZ,
                                 unsigned int reasons) {
    invalidateSamplesLocal(minX, minZ, maxX, maxZ, reasons);
    if (loaded && (reasons & TerrainDirtyHeight)) {
        // Missing-neighbour fallback repeats the last persistent row/column.
        // Editing that row must schedule edge fill even if sample N wasn't edited.
        const int n = gridLayout.sampleCount;
        if (minX <= n - 1 && maxX >= n - 1)
            invalidateSynthesizedSamples(n, std::max(0, minZ), n, std::min(n, maxZ + 1), reasons);
        if (minZ <= n - 1 && maxZ >= n - 1)
            invalidateSynthesizedSamples(std::max(0, minX), n, std::min(n, maxX + 1), n, reasons);
    }
    if ((reasons & TerrainDirtyHeight) && Game::terrainLib != NULL)
        Game::terrainLib->terrainSamplesChanged(this, minX, minZ,
                                                maxX, maxZ, reasons);
}

void Terrain::invalidateSynthesizedSamples(int minX, int minZ,
                                           int maxX, int maxZ,
                                           unsigned int reasons) {
    invalidateSamplesLocal(minX, minZ, maxX, maxZ, reasons);
}

void Terrain::invalidateSamplesLocal(int minX, int minZ,
                                     int maxX, int maxZ,
                                     unsigned int reasons) {
    if (!loaded)
        return;
    if (reasons & TerrainDirtyHeight)
        markPatchBoundsDirtyForSamples(minX, minZ, maxX, maxZ);
    if (reasons & TerrainDirtyGaps)
        markPatchGapStateDirtyForSamples(minX, minZ, maxX, maxZ);
    if (meshBackend != NULL)
        meshBackend->invalidateSamples(minX, minZ, maxX, maxZ, reasons);
    else
        isOgl = false;
    lines.loaded = false;
}

void Terrain::initializePatchBounds() {
    const int count = gridLayout.patchRecordCount();
    patchBounds.resize(count);
    patchBoundsDirty.fill(0, count);
    for (int patchId = 0; patchId < count; ++patchId)
        patchBounds[patchId] = calculatePatchBounds(patchId);
}

void Terrain::markPatchGapStateDirty(int patchId) {
    if (patchGapState.size() != gridLayout.patchRecordCount())
        patchGapState.fill(2, gridLayout.patchRecordCount());
    if (gridLayout.isPatchIndexValid(patchId))
        patchGapState[patchId] = 2;
}

void Terrain::markPatchGapStateDirtyForSamples(int minX, int minZ,
                                                int maxX, int maxZ) {
    if (patchGapState.size() != gridLayout.patchRecordCount())
        patchGapState.fill(2, gridLayout.patchRecordCount());
    minX = std::max(0, minX);
    minZ = std::max(0, minZ);
    maxX = std::min(gridLayout.sampleCount, maxX);
    maxZ = std::min(gridLayout.sampleCount, maxZ);
    const int resolution = gridLayout.patchResolution;
    for (int patchId = 0; patchId < patchGapState.size(); ++patchId) {
        const int patchX = gridLayout.patchColumn(patchId) * resolution;
        const int patchZ = gridLayout.patchRow(patchId) * resolution;
        if (patchX <= maxX && patchX + resolution >= minX
                && patchZ <= maxZ && patchZ + resolution >= minZ)
            patchGapState[patchId] = 2;
    }
}

bool Terrain::patchContainsGap(int patchId) {
    if (!jestF || fData == NULL || !gridLayout.isPatchIndexValid(patchId))
        return false;
    if (patchGapState.size() != gridLayout.patchRecordCount())
        patchGapState.fill(2, gridLayout.patchRecordCount());
    if (patchGapState[patchId] != 2)
        return patchGapState[patchId] != 0;
    const int resolution = gridLayout.patchResolution;
    const int firstX = gridLayout.patchColumn(patchId) * resolution;
    const int firstZ = gridLayout.patchRow(patchId) * resolution;
    bool hasGap = false;
    for (int z = firstZ; z <= firstZ + resolution && !hasGap; ++z) {
        for (int x = firstX; x <= firstX + resolution; ++x) {
            if (fData[z][x] & 0x04) {
                hasGap = true;
                break;
            }
        }
    }
    patchGapState[patchId] = hasGap ? 1 : 0;
    return hasGap;
}

QVector<quint8> Terrain::getPatchGapState() {
    QVector<quint8> gaps(gridLayout.patchRecordCount(), 0);
    if (jestF) {
        for (int patchId = 0; patchId < gaps.size(); ++patchId)
            gaps[patchId] = patchContainsGap(patchId) ? 1 : 0;
    }
    return gaps;
}

QVector<TerrainPatchLodState> Terrain::buildPatchLodState(
        const PatchVisibility &visibility) {
    if (!lowTile && Game::terrainLib != nullptr) {
        const auto *prepared = Game::terrainLib->preparedPatchLod(this);
        if (prepared != nullptr)
            return *prepared;
    }
    const auto gaps = getPatchGapState();
    if (lowTile || Game::currentRoute == NULL
            || Game::currentRoute->trk == NULL || !visibility.valid)
        return TerrainLod::buildTileState(gridLayout, {},
                                          visibility.cameraLocalX,
                                          visibility.cameraLocalZ, gaps);
    bool profileViolation = false;
    QVector<TerrainPatchLodState> result = TerrainLod::buildTileState(
                gridLayout,
                Game::currentRoute->trk->effectiveTerrainLodLevels(),
                visibility.cameraLocalX, visibility.cameraLocalZ, gaps,
                &profileViolation);
    if (profileViolation && !lodProfileWarningShown) {
        qWarning() << "Terrain LOD profile creates a greater-than-2:1 patch "
                      "transition; applying conservative refinement"
                   << name << "patch size" << gridLayout.patchWorldSize
                   << "profile" << Game::currentRoute->trk->terrainLodSummary();
        lodProfileWarningShown = true;
    }
    return result;
}

void Terrain::markPatchBoundsDirty(int patchId) {
    if (gridLayout.isPatchIndexValid(patchId)
            && patchId < patchBoundsDirty.size())
        patchBoundsDirty[patchId] = 1;
}

void Terrain::markPatchBoundsDirtyForSamples(int minX, int minZ,
                                             int maxX, int maxZ) {
    if (patchBoundsDirty.size() != gridLayout.patchRecordCount())
        patchBoundsDirty.fill(1, gridLayout.patchRecordCount());
    minX = std::max(0, minX);
    minZ = std::max(0, minZ);
    maxX = std::min(gridLayout.sampleCount, maxX);
    maxZ = std::min(gridLayout.sampleCount, maxZ);
    const int resolution = gridLayout.patchResolution;
    for (int patchId = 0; patchId < patchBoundsDirty.size(); ++patchId) {
        const int patchX = gridLayout.patchColumn(patchId) * resolution;
        const int patchZ = gridLayout.patchRow(patchId) * resolution;
        // Patch boundary samples are shared, so inclusive interval overlap is
        // intentional here.
        if (patchX <= maxX && patchX + resolution >= minX
                && patchZ <= maxZ && patchZ + resolution >= minZ)
            patchBoundsDirty[patchId] = 1;
    }
}

Terrain::PatchBounds Terrain::calculatePatchBounds(int patchId) const {
    PatchBounds bounds;
    if (terrainData == NULL || !gridLayout.isPatchIndexValid(patchId))
        return bounds;

    const int resolution = gridLayout.patchResolution;
    const int firstX = gridLayout.patchColumn(patchId) * resolution;
    const int firstZ = gridLayout.patchRow(patchId) * resolution;
    float minY = std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();
    for (int z = firstZ; z <= firstZ + resolution; ++z) {
        for (int x = firstX; x <= firstX + resolution; ++x) {
            const float height = terrainData[z][x];
            if (!std::isfinite(height))
                return bounds;
            minY = std::min(minY, height);
            maxY = std::max(maxY, height);
        }
    }

    const float patchSize = gridLayout.patchWorldSize;
    bounds.centerX = (gridLayout.patchColumn(patchId) + 0.5f) * patchSize;
    bounds.averageY = 0.5f * (minY + maxY);
    // The descriptor's Z axis advances negatively, while TSRE's terrain mesh
    // vertices advance in positive local Z.
    bounds.centerZ = -(gridLayout.patchRow(patchId) + 0.5f) * patchSize;
    bounds.rangeY = 0.5f * (maxY - minY);
    bounds.horizontalRadius = 0.5f * patchSize;
    const float flatRadius = 99.48125458f * patchSize / 128.0f;
    bounds.sphereRadius = std::sqrt(flatRadius * flatRadius
                                    + bounds.rangeY * bounds.rangeY);
    bounds.valid = std::isfinite(bounds.sphereRadius);
    return bounds;
}

void Terrain::refreshPatchBounds(bool updateDescriptor) {
    if (patchBounds.size() != gridLayout.patchRecordCount()
            || patchBoundsDirty.size() != gridLayout.patchRecordCount()) {
        initializePatchBounds();
        return;
    }
    for (int patchId = 0; patchId < patchBoundsDirty.size(); ++patchId) {
        if (!patchBoundsDirty[patchId])
            continue;
        const PatchBounds bounds = calculatePatchBounds(patchId);
        patchBounds[patchId] = bounds;
        patchBoundsDirty[patchId] = 0;
        if (!updateDescriptor || !bounds.valid || tfile == NULL
                || tfile->tdata == NULL)
            continue;
        tfile->setPatchValue(patchId, TFile::PatchField::CenterX,
                             bounds.centerX);
        tfile->setPatchValue(patchId, TFile::PatchField::AverageY,
                             bounds.averageY);
        tfile->setPatchValue(patchId, TFile::PatchField::CenterZ,
                             bounds.centerZ);
        tfile->setPatchValue(patchId, TFile::PatchField::FactorY,
                             bounds.sphereRadius);
        tfile->setPatchValue(patchId, TFile::PatchField::RangeY,
                             bounds.rangeY);
        tfile->setPatchValue(patchId, TFile::PatchField::RadiusM,
                             bounds.horizontalRadius);
    }
}

Terrain::PatchVisibility Terrain::buildPatchVisibility(
        const float *modelMatrix, const float *cameraPosition) const {
    PatchVisibility visibility;
    GLUU *gluu = GLUU::get();
    if (gluu == NULL || gluu->pMatrix == NULL || modelMatrix == NULL
            || cameraPosition == NULL)
        return visibility;
    float clip[16];
    Mat4::multiply(clip, gluu->pMatrix, const_cast<float *>(modelMatrix));
    const int signs[6][2] = {
        {0, 1}, {0, -1}, {1, 1}, {1, -1}, {2, 1}, {2, -1}
    };
    for (int i = 0; i < 6; ++i) {
        const int axis = signs[i][0];
        const float sign = static_cast<float>(signs[i][1]);
        FrustumPlane &plane = visibility.planes[i];
        plane.x = clip[3] + sign * clip[axis];
        plane.y = clip[7] + sign * clip[4 + axis];
        plane.z = clip[11] + sign * clip[8 + axis];
        plane.w = clip[15] + sign * clip[12 + axis];
        const float length = std::sqrt(plane.x * plane.x
                                       + plane.y * plane.y
                                       + plane.z * plane.z);
        if (!(length > 0.0f) || !std::isfinite(length))
            return PatchVisibility{};
        plane.x /= length;
        plane.y /= length;
        plane.z /= length;
        plane.w /= length;
    }
    // Terrain model matrices contain translations only. Expressing the camera
    // in terrain-local coordinates makes the radial test independent of World
    // tile and terrain-tile size.
    visibility.cameraLocalX = cameraPosition[0] - modelMatrix[12];
    visibility.cameraLocalZ = cameraPosition[2] - modelMatrix[14];
    visibility.maximumDistance = std::max(
                0.0f, static_cast<float>(lowTile
                                         ? Game::distantLod
                                         : Game::objectLod)
                + TerrainPatchCullMargin);
    if (!std::isfinite(visibility.cameraLocalX)
            || !std::isfinite(visibility.cameraLocalZ)
            || !std::isfinite(visibility.maximumDistance))
        return PatchVisibility{};
    visibility.valid = true;
    return visibility;
}

bool Terrain::isPatchVisible(int patchId,
                             const PatchVisibility &visibility) const {
    if (!visibility.valid || patchId < 0 || patchId >= patchBounds.size()
            || (patchId < patchBoundsDirty.size()
                && patchBoundsDirty[patchId]))
        return true;
    const PatchBounds &bounds = patchBounds[patchId];
    if (!bounds.valid)
        return true;
    const float centerZ = -bounds.centerZ;
    const float dx = bounds.centerX - visibility.cameraLocalX;
    const float dz = centerZ - visibility.cameraLocalZ;
    const float planarRadius = 1.41421356237f * bounds.horizontalRadius;
    const float radialLimit = visibility.maximumDistance + planarRadius;
    if (dx * dx + dz * dz > radialLimit * radialLimit)
        return false;
    for (const FrustumPlane &plane : visibility.planes) {
        const float distance = plane.x * bounds.centerX
                + plane.y * bounds.averageY + plane.z * centerZ + plane.w;
        if (distance < -bounds.sphereRadius)
            return false;
    }
    return true;
}

TerrainMeshBackend *Terrain::ensureMeshBackend() {
    if (meshBackend == NULL) {
        if (Game::terrainMeshMode == Game::TERRAIN_MESH_PAGED)
            meshBackend = new TerrainMeshPaged(*this);
        else
            meshBackend = new TerrainMeshLegacy(*this);
    }
    return meshBackend;
}

int Terrain::ensureMapTexture() {
    if (wTexid == -2)
        return -1;
    if (wTexid == -1) {
        int X = 0;
        int Y = 0;
        getLowCornerTileXY(X, Y);
        wTexid = TexLib::addTex(QString::number(X * 10000 + Y) + ".:maptex");
    }
    if (wTexid < 0 || TexLib::mtex[wTexid] == NULL
            || !TexLib::mtex[wTexid]->loaded)
        return -1;
    if (!TexLib::mtex[wTexid]->glLoaded)
        TexLib::mtex[wTexid]->GLTextures();
    return TexLib::mtex[wTexid]->glLoaded
            ? static_cast<int>(TexLib::mtex[wTexid]->tex[0]) : -1;
}

void Terrain::toggleGaps(int x, int z, float posx, float posz, float direction){
    if (!editable)
        return;
    if(!jestF)
        newF();
    getLocalCoords(x, z, posx, posz);
    int tx = static_cast<int>(std::floor(posz / gridLayout.sampleSpacing));
    int tz = static_cast<int>(std::floor(posx / gridLayout.sampleSpacing));
    if(tx > gridLayout.sampleCount || tx < 0)
        return;
    if(tz > gridLayout.sampleCount || tz < 0)
        return;
    if(direction == 0)
        fData[tx][tz] ^= 0x04;
    if(direction == 1)
        fData[tx][tz] |= 0x04;
    if(direction == -1){
        for(int j = -1; j <= 1; j++)
            for(int i = -1; i <= 1; i++){
                if(tx+j > gridLayout.sampleCount || tx+j < 0)
                    continue;
                if(tz+i > gridLayout.sampleCount || tz+i < 0)
                    continue;
                fData[tx+j][tz+i] &= ~0x04;
            }
    }
    modifiedF = true;
    modified = true;
    invalidateSamples(tz - (direction == -1 ? 1 : 0),
                      tx - (direction == -1 ? 1 : 0),
                      tz + (direction == -1 ? 1 : 0),
                      tx + (direction == -1 ? 1 : 0), TerrainDirtyGaps);
}

int Terrain::getTexture(int x, int z, float posx, float posz) {
    getPatchCoords(x, z, posx, posz);
    int patches = tfile->patchsetNpatches;

    return texid[z * patches + x];
}

void Terrain::convertTexToDefaultCoords(int idx) {
    const float defaultTextureScale = editorDefaultPatchTextureScale(gridLayout);
    /*float x11 = (0) * tfile->tdata[(idx)*13 + 3 + 6] + (0) * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
    float y11 = (0) * tfile->tdata[(idx)*13 + 5 + 6] + (0) * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
    //qDebug() << x11 << " " <<y11;
    float x21 = (16) * tfile->tdata[(idx)*13 + 3 + 6] + (0) * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
    float y21 = (16) * tfile->tdata[(idx)*13 + 5 + 6] + (0) * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
    //qDebug() << x21 << " " <<y21;
    float x12 = (0) * tfile->tdata[(idx)*13 + 3 + 6] + (16) * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
    float y12 = (0) * tfile->tdata[(idx)*13 + 5 + 6] + (16) * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
    //qDebug() << x12 << " " <<y12;
    float x22 = (16) * tfile->tdata[(idx)*13 + 3 + 6] + (16) * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
    float y22 = (16) * tfile->tdata[(idx)*13 + 5 + 6] + (16) * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
    //qDebug() << x22 << " " <<y22;
    float t;*/
    /*if ((x11 < x21) && (y11 == y21)) {
        qDebug() << "rot1 - ok";
    } else {
        qDebug() << "rot";
        TexLib::mtex[texid[idx]]->crop(x11, y11, x22, y22);
        TexLib::mtex[texid[idx]]->advancedCrop((float*)&tfile->tdata[(idx)*13 + 6]);
    }*/
    if((fabs(tfile->tdata[(idx)*13 + 1 + 6] - 0.001)
      +fabs(tfile->tdata[(idx)*13 + 2 + 6] - 0.001)
      +fabs(tfile->tdata[(idx)*13 + 3 + 6] - defaultTextureScale)
      +fabs(tfile->tdata[(idx)*13 + 4 + 6] - 0.0)
      +fabs(tfile->tdata[(idx)*13 + 5 + 6] - 0.0)
      +fabs(tfile->tdata[(idx)*13 + 6 + 6] - defaultTextureScale)
      ) < 0.01){
        qDebug() << "rot ok";
        return;
    }
    qDebug() << "rot ->";
    
    TexLib::mtex[texid[idx]]->advancedCrop((float*)&tfile->tdata[(idx)*13 + 6], 512, 512);
    tfile->tdata[(idx)*13 + 1 + 6] = 0.001;
    tfile->tdata[(idx)*13 + 2 + 6] = 0.001;
    tfile->tdata[(idx)*13 + 3 + 6] = defaultTextureScale;
    tfile->tdata[(idx)*13 + 4 + 6] = 0.0;
    tfile->tdata[(idx)*13 + 5 + 6] = 0.0;
    tfile->tdata[(idx)*13 + 6 + 6] = defaultTextureScale;
    invalidatePatch(idx, TerrainDirtyUvParams);
}

void Terrain::resetPatchTexCoords(int uu){
    if (!editable)
        return;
    const float defaultTextureScale = editorDefaultPatchTextureScale(gridLayout);
    if(gridLayout.isPatchIndexValid(uu)){
        tfile->tdata[(uu)*13 + 1 + 6] = 0.001;
        tfile->tdata[(uu)*13 + 2 + 6] = 0.001;
        tfile->tdata[(uu)*13 + 3 + 6] = defaultTextureScale;
        tfile->tdata[(uu)*13 + 4 + 6] = 0.0;
        tfile->tdata[(uu)*13 + 5 + 6] = 0.0;
        tfile->tdata[(uu)*13 + 6 + 6] = defaultTextureScale;
        invalidatePatch(uu, TerrainDirtyUvParams);
    } else {
        for (uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
            if(selectedPatchs[uu]){
                tfile->tdata[(uu)*13 + 1 + 6] = 0.001;
                tfile->tdata[(uu)*13 + 2 + 6] = 0.001;
                tfile->tdata[(uu)*13 + 3 + 6] = defaultTextureScale;
                tfile->tdata[(uu)*13 + 4 + 6] = 0.0;
                tfile->tdata[(uu)*13 + 5 + 6] = 0.0;
                tfile->tdata[(uu)*13 + 6 + 6] = defaultTextureScale;
                invalidatePatch(uu, TerrainDirtyUvParams);
            }
        }
    }
    modified = true;
}

void Terrain::rotateTex(int idx) {
    const float uvSpan = static_cast<float>(gridLayout.patchResolution);
    float x11 = (0) * tfile->tdata[(idx)*13 + 3 + 6] + (0) * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
    float y11 = (0) * tfile->tdata[(idx)*13 + 5 + 6] + (0) * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
    qDebug() << x11 << " " << y11;
    float x21 = uvSpan * tfile->tdata[(idx)*13 + 3 + 6] + (0) * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
    float y21 = uvSpan * tfile->tdata[(idx)*13 + 5 + 6] + (0) * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
    qDebug() << x21 << " " << y21;
    float x12 = (0) * tfile->tdata[(idx)*13 + 3 + 6] + uvSpan * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
    float y12 = (0) * tfile->tdata[(idx)*13 + 5 + 6] + uvSpan * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
    qDebug() << x12 << " " << y12;
    float x22 = uvSpan * tfile->tdata[(idx)*13 + 3 + 6] + uvSpan * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
    float y22 = uvSpan * tfile->tdata[(idx)*13 + 5 + 6] + uvSpan * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
    qDebug() << x22 << " " << y22;
    float t;
    if ((x11 < x21) && (y11 == y21)) {
        qDebug() << "rot1";
        tfile->tdata[(idx)*13 + 1 + 6] = x12;
        t = tfile->tdata[(idx)*13 + 4 + 6];
        tfile->tdata[(idx)*13 + 4 + 6] = tfile->tdata[(idx)*13 + 3 + 6];
        tfile->tdata[(idx)*13 + 3 + 6] = t;
        t = tfile->tdata[(idx)*13 + 5 + 6];
        tfile->tdata[(idx)*13 + 5 + 6] = -tfile->tdata[(idx)*13 + 6 + 6];
        tfile->tdata[(idx)*13 + 6 + 6] = -t;
    } else if ((x11 == x21) && (y11 < y21)) {
        qDebug() << "rot2";
        tfile->tdata[(idx)*13 + 2 + 6] = y21;
        t = tfile->tdata[(idx)*13 + 4 + 6];
        tfile->tdata[(idx)*13 + 4 + 6] = -tfile->tdata[(idx)*13 + 3 + 6];
        tfile->tdata[(idx)*13 + 3 + 6] = -t;
        t = tfile->tdata[(idx)*13 + 5 + 6];
        tfile->tdata[(idx)*13 + 5 + 6] = tfile->tdata[(idx)*13 + 6 + 6];
        tfile->tdata[(idx)*13 + 6 + 6] = t;
    } else if ((x11 > x21) && (y11 == y21)) {
        qDebug() << "rot3";
        tfile->tdata[(idx)*13 + 1 + 6] = x12;
        t = tfile->tdata[(idx)*13 + 4 + 6];
        tfile->tdata[(idx)*13 + 4 + 6] = tfile->tdata[(idx)*13 + 3 + 6];
        tfile->tdata[(idx)*13 + 3 + 6] = t;
        t = tfile->tdata[(idx)*13 + 5 + 6];
        tfile->tdata[(idx)*13 + 5 + 6] = -tfile->tdata[(idx)*13 + 6 + 6];
        tfile->tdata[(idx)*13 + 6 + 6] = -t;
    } else if ((x11 == x21) && (y11 > y21)) {
        qDebug() << "rot4";
        tfile->tdata[(idx)*13 + 2 + 6] = y21;
        t = tfile->tdata[(idx)*13 + 4 + 6];
        tfile->tdata[(idx)*13 + 4 + 6] = -tfile->tdata[(idx)*13 + 3 + 6];
        tfile->tdata[(idx)*13 + 3 + 6] = -t;
        t = tfile->tdata[(idx)*13 + 5 + 6];
        tfile->tdata[(idx)*13 + 5 + 6] = tfile->tdata[(idx)*13 + 6 + 6];
        tfile->tdata[(idx)*13 + 6 + 6] = t;
    }
    invalidatePatch(idx, TerrainDirtyUvParams);
}

void Terrain::mirrorXTex(int idx){
    const float uvSpan = static_cast<float>(gridLayout.patchResolution);
    float x21 = uvSpan * tfile->tdata[(idx)*13 + 3 + 6] + (0) * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
    float y21 = uvSpan * tfile->tdata[(idx)*13 + 5 + 6] + (0) * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
    tfile->tdata[(idx)*13 + 1 + 6] = x21;
    tfile->tdata[(idx)*13 + 2 + 6] = y21;
    tfile->tdata[(idx)*13 + 3 + 6] = -tfile->tdata[(idx)*13 + 3 + 6];
    tfile->tdata[(idx)*13 + 5 + 6] = -tfile->tdata[(idx)*13 + 5 + 6];
    modified = true;
    invalidatePatch(idx, TerrainDirtyUvParams);
}

void Terrain::mirrorYTex(int idx){
    const float uvSpan = static_cast<float>(gridLayout.patchResolution);
    float x12 = (0) * tfile->tdata[(idx)*13 + 3 + 6] + uvSpan * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
    float y12 = (0) * tfile->tdata[(idx)*13 + 5 + 6] + uvSpan * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
    tfile->tdata[(idx)*13 + 1 + 6] = x12;
    tfile->tdata[(idx)*13 + 2 + 6] = y12;
    tfile->tdata[(idx)*13 + 4 + 6] = -tfile->tdata[(idx)*13 + 4 + 6];
    tfile->tdata[(idx)*13 + 6 + 6] = -tfile->tdata[(idx)*13 + 6 + 6];
    modified = true;
    invalidatePatch(idx, TerrainDirtyUvParams);
}

float Terrain::getScaleTexX(int idx){
    return fabs(tfile->tdata[(idx)*13 + 3 + 6]
                + tfile->tdata[(idx)*13 + 4 + 6])
            / editorDefaultPatchTextureScale(gridLayout);
}

float Terrain::getScaleTexY(int idx){
    return fabs(tfile->tdata[(idx)*13 + 5 + 6]
                + tfile->tdata[(idx)*13 + 6 + 6])
            / editorDefaultPatchTextureScale(gridLayout);
}

float Terrain::getScaleTex(int idx){
    return (getScaleTexX(idx) + getScaleTexY(idx)) / 2.0;
}

void Terrain::scaleTex(int idx, float val){
    float s = getScaleTexX(idx);
    float val1 = val/s;
    tfile->tdata[(idx)*13 + 3 + 6] *= val1;
    tfile->tdata[(idx)*13 + 4 + 6] *= val1;
    s = getScaleTexY(idx);
    val1 = val/s;
    tfile->tdata[(idx)*13 + 5 + 6] *= val1;
    tfile->tdata[(idx)*13 + 6 + 6] *= val1;
    modified = true;
    invalidatePatch(idx, TerrainDirtyUvParams);
}

void Terrain::scaleTexX(int idx, float val){
    float s = getScaleTexX(idx);
    val = val/s;
    tfile->tdata[(idx)*13 + 3 + 6] *= val;
    tfile->tdata[(idx)*13 + 4 + 6] *= val;
    modified = true;
    invalidatePatch(idx, TerrainDirtyUvParams);
}

void Terrain::scaleTexY(int idx, float val){
    float s = getScaleTexY(idx);
    val = val/s;
    tfile->tdata[(idx)*13 + 5 + 6] *= val;
    tfile->tdata[(idx)*13 + 6 + 6] *= val;
    modified = true;
    invalidatePatch(idx, TerrainDirtyUvParams);
}

void Terrain::setTileBlob(){
    if(this->showBlob){
        this->showBlob = false;
        return;
    } 
    int X, Y;
    getLowCornerTileXY(X, Y);
    int hash = (X*10000+Y);
    qDebug() << hash;
    if(MapWindow::mapTileImages[hash] != NULL)
        this->showBlob = true;
    else {
        if(MapWindow::LoadMapFromDisk(X, Y)){
            this->showBlob = true;
        } else {
            qDebug() << "load map first!";
        }
    }
}

void Terrain::makeTextureFromMap(){
    if (!editable)
        return;
    if(Game::seasonalEditing && Game::season.length() > 0)
        return;
    
    int X, Y;
    getLowCornerTileXY(X, Y);
    int hash = (X*10000+Y);
    qDebug() << hash;
    if(MapWindow::mapTileImages[hash] == NULL){
        qDebug() << "mat tex not found";
        return;
    }
    QString path = QString::number(hash)+".:maptex";
    QString tname = name + "_map.ace";
    int mapTexid = TexLib::addTex(path);
    if(!TexLib::mtex[mapTexid]->loaded){
        qDebug() << "mat tex not loaded";
        return;
    }
    
    int newMat = tfile->getMatByTexture(tname);
    if(newMat >= 0){
        qDebug() << "material already exist";
    } else {
        newMat = tfile->newMat();
    }
    *tfile->materials[newMat].tex[0] = tname;
    *tfile->amaterials[newMat].tex[0] = tname;
    float *texmult = (float*)&tfile->materials[newMat].itex[1][3];
    *texmult = 32*16;

    int newTexture = TexLib::cloneTex(mapTexid);
    TexLib::mtex[newTexture]->pathid = tname;
    TexLib::save("ace", texturepath + tname, newTexture);
    int patches = tfile->patchsetNpatches;
    float texstep = 1.0/patches;
    for(int i = 0; i < patches; i++)
        for(int j = 0; j < patches; j++){
            texid[j * patches + i] = newTexture;
            tfile->tdata[(j * patches + i)*13 + 0 + 6] = newMat;
            tfile->tdata[(j * patches + i)*13 + 1 + 6] = texstep*i;
            tfile->tdata[(j * patches + i)*13 + 2 + 6] = texstep*j;
            tfile->tdata[(j * patches + i)*13 + 3 + 6] =
                    texstep / static_cast<float>(gridLayout.patchResolution);
            tfile->tdata[(j * patches + i)*13 + 4 + 6] = 0;
            tfile->tdata[(j * patches + i)*13 + 5 + 6] = 0;
            tfile->tdata[(j * patches + i)*13 + 6 + 6] =
                    texstep / static_cast<float>(gridLayout.patchResolution);
            //TexLib::mtex[texid[j * 16 + i]]->pathid = name;
        }

    invalidateAll(TerrainDirtyUvParams);
    this->modified = true;
}

void Terrain::removeTextureFromMap(){
    if (!editable)
        return;
    QString name = this->getTileName(mojex, -mojez) + "_map.ace";
    int newMat = tfile->getMatByTexture(name);
    if(newMat <= 0){
        return;
    }
    tfile->removeMat(newMat);
    for (int i = 0; i < gridLayout.patchRecordCount(); i++) {
        texid[i] = -1;
    }    
    invalidateAll(TerrainDirtyUvParams);
    this->modified = true;
}

void Terrain::setWaterDraw() {
    if (!editable)
        return;
    int patches = tfile->patchsetNpatches;
    for (int uu = 0; uu < patches*patches; uu++) {
        if(selectedPatchs[uu]){
            tfile->flags[uu] = tfile->flags[uu] | 0x10000c0;
            this->setModified(true);
        }
    }
    updateTFile();
}

void Terrain::toggleWaterDraw() {
    if (!editable)
        return;
    int patches = tfile->patchsetNpatches;
    for (int uu = 0; uu < patches*patches; uu++) {
        if(selectedPatchs[uu]){
            tfile->flags[uu] = tfile->flags[uu] ^ 0x10000c0;
            this->setModified(true);
        }
    }
    updateTFile();
}

void Terrain::hideWaterDraw() {
    if (!editable)
        return;
    int patches = tfile->patchsetNpatches;
    for (int uu = 0; uu < patches*patches; uu++) {
        if(selectedPatchs[uu]){
            tfile->flags[uu] = tfile->flags[uu] & ~0x10000c0;
            this->setModified(true);
        }
    }
    updateTFile();
}

void Terrain::updateTFile(){
    
}

void Terrain::toggleWaterDraw(int x, int z, float posx, float posz, float direction) {
    if (!editable)
        return;
    getPatchCoords(x, z, posx, posz);
    int patches = tfile->patchsetNpatches;
    if(direction == 0)
        tfile->flags[z * patches + x] = tfile->flags[z * patches + x] ^ 0x10000c0;
    if(direction == 1)
        tfile->flags[z * patches + x] = tfile->flags[z * patches + x] | 0x10000c0;
    if(direction == -1)
        tfile->flags[z * patches + x] = tfile->flags[z * patches + x] & ~0x10000c0;
    this->setModified(true);
    updateTFile();
}

void Terrain::setDrawAdjacent(){
    if (!editable)
        return;
    const int patches = gridLayout.patchesPerSide;
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            int u = gridLayout.patchRow(uu);
            int y = gridLayout.patchColumn(uu);
            for(int i = u - 1; i <= u+1; i++)
                for(int j = y - 1; j <= y+1; j++){
                    if(i < 0 || j < 0 || i >= patches || j >= patches)
                        continue;
                    const int adjacent = gridLayout.patchIndex(i, j);
                    tfile->flags[adjacent] = tfile->flags[adjacent] & ~(0x1);
                }
            this->setModified(true);
        }
    }
    updateTFile();
}

void Terrain::rotatePatchTexture(){
    if (!editable)
        return;
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            rotateTex(uu);
        }
    }
}

void Terrain::mirrorXPatchTexture(){
    if (!editable)
        return;
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            mirrorXTex(uu);
        }
    }
}

void Terrain::mirrorYPatchTexture(){
    if (!editable)
        return;
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            mirrorYTex(uu);
        }
    }
}

float Terrain::getPatchScaleTex(){
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            return getScaleTex(uu);
        }
    }
    return 0;
}

float Terrain::getPatchScaleTexX(){
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            return getScaleTexX(uu);
        }
    }
    return 0;
}

float Terrain::getPatchScaleTexY(){
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            return getScaleTexY(uu);
        }
    }
    return 0;
}

QString Terrain::getPatchRotationName(){
    const float uvSpan = static_cast<float>(gridLayout.patchResolution);
    for (int idx = 0; idx < gridLayout.patchRecordCount(); idx++)
        if(selectedPatchs[idx]){
            float x11 = (0) * tfile->tdata[(idx)*13 + 3 + 6] + (0) * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
            float y11 = (0) * tfile->tdata[(idx)*13 + 5 + 6] + (0) * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
            float x21 = uvSpan * tfile->tdata[(idx)*13 + 3 + 6] + (0) * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
            float y21 = uvSpan * tfile->tdata[(idx)*13 + 5 + 6] + (0) * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
            float x12 = (0) * tfile->tdata[(idx)*13 + 3 + 6] + uvSpan * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
            float y12 = (0) * tfile->tdata[(idx)*13 + 5 + 6] + uvSpan * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
            float x22 = uvSpan * tfile->tdata[(idx)*13 + 3 + 6] + uvSpan * tfile->tdata[(idx)*13 + 4 + 6] + tfile->tdata[(idx)*13 + 1 + 6];
            float y22 = uvSpan * tfile->tdata[(idx)*13 + 5 + 6] + uvSpan * tfile->tdata[(idx)*13 + 6 + 6] + tfile->tdata[(idx)*13 + 2 + 6];
            if ((x11 < x21) && (y11 == y21)) {
                return QString("0°");
            } else if ((x11 == x21) && (y11 < y21)) {
                return QString("270°");
            } else if ((x11 > x21) && (y11 == y21)) {
                return QString("180°");
            } else if ((x11 == x21) && (y11 > y21)) {
                return QString("90°");
            } else {
                return QString("UNDEFINED");
            }
        }
    return QString("UNDEFINED");
}

void Terrain::scalePatchTexCoords(float val){
    if (!editable)
        return;
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            scaleTex(uu, val);
        }
    }
}

void Terrain::scalePatchTexCoordsX(float val){
    if (!editable)
        return;
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            scaleTexX(uu, val);
        }
    }
}

void Terrain::scalePatchTexCoordsY(float val){
    if (!editable)
        return;
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            scaleTexY(uu, val);
        }
    }
}

QString Terrain::getPatchTexTransformString(){
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            return QString::number(tfile->tdata[(uu)*13 + 1 + 6]) + " "+
                   QString::number(tfile->tdata[(uu)*13 + 2 + 6]) + " "+
                   QString::number(tfile->tdata[(uu)*13 + 3 + 6]) + " "+
                   QString::number(tfile->tdata[(uu)*13 + 4 + 6]) + " "+
                   QString::number(tfile->tdata[(uu)*13 + 5 + 6]) + " "+
                   QString::number(tfile->tdata[(uu)*13 + 6 + 6]);
        }
    }
    return "";
}
QString Terrain::getPatchTexTransformString(int x, int z, float posx, float posz){
    getPatchCoords(x, z, posx, posz);
    int patches = tfile->patchsetNpatches;

    return getPatchTexTransformString(z * patches + x);
}

QString Terrain::getPatchTexTransformString(int u){
    if(!gridLayout.isPatchIndexValid(u))
        return "";
        return QString::number(tfile->tdata[(u)*13 + 1 + 6]) + " "+
                   QString::number(tfile->tdata[(u)*13 + 2 + 6]) + " "+
                   QString::number(tfile->tdata[(u)*13 + 3 + 6]) + " "+
                   QString::number(tfile->tdata[(u)*13 + 4 + 6]) + " "+
                   QString::number(tfile->tdata[(u)*13 + 5 + 6]) + " "+
                   QString::number(tfile->tdata[(u)*13 + 6 + 6]);
    //return "";
}

void Terrain::setPatchTexTransform(QString val){
    if (!editable)
        return;
    QStringList list = val.split(" ");
    if(list.size() != 6)
        return;
    
    float t[6];
    bool ok;
    for (int i = 0; i < 6; i++){
        t[i] = list[i].toFloat(&ok);
        if(!ok)
            return;
    }
    
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            for (int i = 0; i < 6; i++)
                tfile->tdata[(uu)*13 + i + 1 + 6] = t[i];
            invalidatePatch(uu, TerrainDirtyUvParams);
        }
    }
    modified = true;
}

void Terrain::setPatchTexTransform(QString val, int u){
    if (!editable)
        return;
    if (!gridLayout.isPatchIndexValid(u))
        return;
    QStringList list = val.split(" ");
    if(list.size() != 6)
        return;
    
    float t[6];
    bool ok;
    for (int i = 0; i < 6; i++){
        t[i] = list[i].toFloat(&ok);
        if(!ok)
            return;
    }
    
    for (int i = 0; i < 6; i++)
        tfile->tdata[(u)*13 + i + 1 + 6] = t[i];

    modified = true;
    invalidatePatch(u, TerrainDirtyUvParams);
}
    
void Terrain::removeAllGaps(){
    if (!editable)
        return;
    if(!jestF)
        return;
    int patches = tfile->patchsetNpatches;
    for (int uu = 0; uu < patches*patches; uu++) {
        if(selectedPatchs[uu]){
            int u = uu / patches;
            int y = uu - u*patches;
            const int patchRes = gridLayout.patchResolution;
            for(int i = 0; i < patchRes; i++)
                for(int j = 0; j < patchRes; j++)
                    fData[u*patchRes+i][y*patchRes+j] &= ~(0x04);
            modifiedF = true;
            modified = true;
            invalidatePatch(uu, TerrainDirtyGaps);
        }
    }
}
    
void Terrain::setDraw() {
    if (!editable)
        return;
    int patches = tfile->patchsetNpatches;
    for (int uu = 0; uu < patches*patches; uu++) {
        if(selectedPatchs[uu]){
            tfile->flags[uu] = tfile->flags[uu] & ~0x1;
            this->setModified(true);
        }
    }
}

void Terrain::toggleDraw() {
    if (!editable)
        return;
    int patches = tfile->patchsetNpatches;
    for (int uu = 0; uu < patches*patches; uu++) {
        if(selectedPatchs[uu]){
            tfile->flags[uu] = tfile->flags[uu] ^ 0x1;
            this->setModified(true);
        }
    }
    updateTFile();
}

void Terrain::hideDraw() {
    if (!editable)
        return;
    int patches = tfile->patchsetNpatches;
    for (int uu = 0; uu < patches*patches; uu++) {
        if(selectedPatchs[uu]){
            tfile->flags[uu] = tfile->flags[uu] | 0x1;
            this->setModified(true);
        }
    }
    updateTFile();
}

void Terrain::toggleDraw(int x, int z, float posx, float posz) {
    if (!editable)
        return;
    getPatchCoords(x, z, posx, posz);
    int patches = tfile->patchsetNpatches;
    tfile->flags[z * patches + x] = tfile->flags[z * patches + x] ^ 0x1;
    this->setModified(true);
    updateTFile();
}

float Terrain::getErrorBias(){
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            return tfile->errorBias[uu];
        }
    }
    return -1;
}

void Terrain::getWTileIds(QSet<int> &ids){
    const int worldTilesPerSide = gridLayout.terrainWorldSize
            / TerrainGridLayout::WorldTileSize;
    for (int x = 0; x < worldTilesPerSide; ++x)
        for (int z = 0; z < worldTilesPerSide; ++z)
            ids.insert((static_cast<int>(mojex) + x) * 10000
                       + static_cast<int>(mojez) - z);
}

void Terrain::setErrorBias(float val){
    if (!editable)
        return;
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            tfile->errorBias[uu] = val;
            this->setModified(true);
        }
    }
}

void Terrain::setPatchFlags(int x, int z, float posx, float posz, int val){
    if (!editable)
        return;
    this->getPatchCoords(x, z, posx, posz);
    int patches = tfile->patchsetNpatches;
    int u = x;
    int y = z;
    int uu = y * patches + u;
    if(uu < 0 || uu >= patches*patches){
        qDebug() << "flags fail" << u << y << uu;
        return;
    }
    tfile->flags[uu] = val;
}

int Terrain::getPatchFlags(int x, int z, float posx, float posz){
    this->getPatchCoords(x, z, posx, posz);
    int patches = tfile->patchsetNpatches;
    int u = x;
    int y = z;
    int uu = y * patches + u;
    if(uu < 0 || uu >= patches*patches){
        qDebug() << "flags fail" << u << y << uu;
        return 0;
    }
    return tfile->flags[uu];
}

void Terrain::setErrorBias(int x, int z, float posx, float posz, float val){
    if (!editable)
        return;
    this->getPatchCoords(x, z, posx, posz);
    int patches = tfile->patchsetNpatches;
    int u = x;
    int y = z;
    int uu = y * patches + u;
    if(uu < 0 || uu >= patches*patches){
        qDebug() << "ebias fail" << u << y << uu;
        return;
    }
    if(tfile->errorBias != NULL)
        tfile->errorBias[y * patches + u] = val;
}

float Terrain::getAvgVaterLevel(){
    return (tfile->WNE + tfile->WSE + tfile->WNW + tfile->WSW)/4.0;
}

void Terrain::getWaterLevels(float* w){
    w[0] = tfile->WNW;
    w[1] = tfile->WNE;
    w[2] = tfile->WSW;
    w[3] = tfile->WSE;
}

float Terrain::getWaterLevelNW(){
    return tfile->WNW;
}

float Terrain::getWaterLevelNE(){
    return tfile->WNE;
}

float Terrain::getWaterLevelSW(){
    return tfile->WSW;
}

float Terrain::getWaterLevelSE(){
    return tfile->WSE;
}

void Terrain::setWaterLevelNW(float val){
    if (!editable)
        return;
    tfile->waterLevel = true;
    tfile->WNW = val;
}

void Terrain::setWaterLevelNE(float val){
    if (!editable)
        return;
    tfile->waterLevel = true;
    tfile->WNE = val;
}

void Terrain::setWaterLevelSW(float val){
    if (!editable)
        return;
    tfile->waterLevel = true;
    tfile->WSW = val;
}

void Terrain::setWaterLevelSE(float val){
    if (!editable)
        return;
    tfile->waterLevel = true;
    tfile->WSE = val;
}

void Terrain::setAvgWaterLevel(float val){
    if (!editable)
        return;
    tfile->waterLevel = true;
    tfile->WNE = val;
    tfile->WSE = val;
    tfile->WNW = val;
    tfile->WSW = val;    
    refreshWaterShapes();
}

void Terrain::getAdjacentWaterLevels(float* w){
    Game::terrainLib->fillWaterLevels(w, mojex, mojez);
}

void Terrain::setAdjacentWaterLevels(float* w){
    if (!editable)
        return;
    Game::terrainLib->setWaterLevels(w, mojex, mojez);
}

void Terrain::setWaterLevelGui(){
    if (!editable)
        return;
    TerrainWaterWindow waterWindow;
    waterWindow.setWindowTitle("Water Level");
    waterWindow.WNE = tfile->WNE;
    waterWindow.WSE = tfile->WSE;
    waterWindow.WNW = tfile->WNW;
    waterWindow.WSW = tfile->WSW;    
    waterWindow.setWater();
    waterWindow.exec();
    //qDebug() << waterWindow->changed;
    if(waterWindow.changed){
        tfile->waterLevel = true;
        tfile->WNE = waterWindow.WNE;
        tfile->WSE = waterWindow.WSE;
        tfile->WNW = waterWindow.WNW;
        tfile->WSW = waterWindow.WSW;
        refreshWaterShapes();
    }
}

void Terrain::setWaterLevel(float nw, float ne, float sw, float se){
    if (!editable)
        return;
    tfile->waterLevel = true;
    tfile->WNE = ne;
    tfile->WSE = se;
    tfile->WNW = nw;
    tfile->WSW = sw;
    refreshWaterShapes();
}

void Terrain::getRotation(float* rot, int x, int z, int posx, int posz){
    float localX = posx;
    float localZ = posz;
    getLocalCoords(x, z, localX, localZ);
    const int sampleSize = gridLayout.sampleSpacing;
    const int sx = std::clamp(static_cast<int>(std::floor(localX / sampleSize)),
                              0, gridLayout.sampleCount - 1);
    const int sz = std::clamp(static_cast<int>(std::floor(localZ / sampleSize)),
                              0, gridLayout.sampleCount - 1);
    float tx = terrainData[sz + 1][sx] - terrainData[sz][sx];
    float tz = terrainData[sz][sx + 1] - terrainData[sz][sx];
    
    rot[0] = atan(tx/sampleSize);
    rot[1] = atan(tz/sampleSize);
}

float Terrain::getHeight(int x, int z, float posx, float posz, bool addR){
    //if ((posx + 1024) / 8 + 1 > 256 || (posz + 1024) / 8 + 1 > 256)
    //    return terrainData[(int) (posz + 1024) / 8][(int) (posx + 1024) / 8];
    
    const int samples = gridLayout.sampleCount;
    const int sampleSize = gridLayout.sampleSpacing;
    getLocalCoords(x, z, posx, posz);
    posx = std::clamp(posx, 0.0f, static_cast<float>(gridLayout.terrainWorldSize));
    posz = std::clamp(posz, 0.0f, static_cast<float>(gridLayout.terrainWorldSize));
    const int sx = std::clamp(static_cast<int>(std::floor(posx / sampleSize)), 0, samples - 1);
    const int sz = std::clamp(static_cast<int>(std::floor(posz / sampleSize)), 0, samples - 1);
    const float tx = (posx - sx * sampleSize) / sampleSize;
    const float tz = (posz - sz * sampleSize) / sampleSize;
    
    float roznica = 0;
    //return 0;
    if (addR) {
        roznica = 0.25 * (terrainData[sz][sx] + terrainData[sz + 1][sx + 1]
                + terrainData[sz + 1][sx] + terrainData[sz][sx + 1])
                - 0.5f * (terrainData[sz][sx] + terrainData[sz + 1][sx + 1]);
    }
    return (
            terrainData[sz][sx]*(1.0 - tx)*(1.0 - tz) +
            terrainData[sz][sx + 1]*(tx)*(1.0 - tz) +
            terrainData[sz + 1][sx]*(1.0 - tx)*(tz) +
            terrainData[sz + 1][sx + 1]*(tx)*(tz)
            + fabs(roznica));
}

void Terrain::refreshWaterShapes(){
    for (WaterTile* wt : water){
        if(wt == NULL)
            continue;
        for (int uu = 0; uu < gridLayout.patchesPerSide; uu++) {
            for (int yy = 0; yy < gridLayout.patchesPerSide; yy++) {
                wt->w[uu * gridLayout.patchesPerSide + yy].loaded = false;
            }
        }
    }
    setModified(true);
}

bool Terrain::isXYinside(int x, int y){
    float posx = 0, posz = 0;
    getLocalCoords(x, y, posx, posz);

    if(posx < 0 || posz < 0 || posx > gridLayout.terrainWorldSize
            || posz > gridLayout.terrainWorldSize)
        return false;
    return true;
}

int Terrain::getPatchSize(){
    return gridLayout.patchWorldSize;
}

void Terrain::setAllErrorBias(float val){
    if (!editable)
        return;
    const int patchCount = gridLayout.patchesPerSide * gridLayout.patchesPerSide;
    for (int patch = 0; patch < patchCount; ++patch)
        tfile->errorBias[patch] = val;
    setModified(true);
}

int Terrain::getSampleSize(){
    return gridLayout.sampleSpacing;
}

void Terrain::getLocalCoords(int x, int z, float &posx, float &posz){
    posx -= TerrainGridLayout::WorldTileSize * (mojex-x)
            - TerrainGridLayout::WorldTileHalfSize;
    posz -= TerrainGridLayout::WorldTileSize * (mojez-z)
            + TerrainGridLayout::WorldTileHalfSize;
    posz = gridLayout.terrainWorldSize + posz;
}

void Terrain::getPatchCoords(int &x, int &z, float &posx, float &posz){
    getLocalCoords(x, z, posx, posz);
    x = std::clamp(static_cast<int>(std::floor(posx / gridLayout.patchWorldSize)),
                   0, gridLayout.patchesPerSide - 1);
    z = std::clamp(static_cast<int>(std::floor(posz / gridLayout.patchWorldSize)),
                   0, gridLayout.patchesPerSide - 1);
}

void Terrain::setTexture(QString textureName, int x, int z, float posx, float posz, QString transformation){
    if (!editable)
        return;
    if(Game::seasonalEditing && Game::season.length() > 0)
        return;
    
    getPatchCoords(x, z, posx, posz);
    int patches = tfile->patchsetNpatches;
    int u = z*patches + x;
    int mid = tfile->getMatByTexture(textureName);
        if(mid < 0){
            tfile->tdata[(u)*13 + 0 + 6] = tfile->cloneMat(tfile->tdata[(u)*13 + 0 + 6]);
            *tfile->materials[(int) tfile->tdata[(u)*13 + 0 + 6]].tex[0] = textureName;
            *tfile->amaterials[(int) tfile->tdata[(u)*13 + 0 + 6]].tex[0] = textureName;
            //qDebug() << *tfile->materials[(int) tfile->tdata[(u)*13 + 0 + 6]].tex[0];
            //qDebug() << "new material";
        } else {
            tfile->tdata[(u)*13 + 0 + 6] = mid;
            //qDebug() << "existed material";
        }
    if(transformation.length() > 0){
        this->setPatchTexTransform(transformation, u);
        
    }
}

void Terrain::setTexture(Brush* brush, int x, int z, float posx, float posz) {
    if (!editable)
        return;
    if(Game::seasonalEditing && Game::season.length() > 0)
        return;
    
    getPatchCoords(x, z, posx, posz);

    int patches = tfile->patchsetNpatches;
    setTexture(brush, z*patches + x);
}

void Terrain::setTexture(Brush* brush, int u) {
    if (!editable)
        return;
    if(!gridLayout.isPatchIndexValid(u))
        return;
    if(Game::seasonalEditing && Game::season.length() > 0)
        return;
    
    if (brush->texId < 0)
        return;
    bool autoRot = false;
    if (brush->texId == texid[u]) {
        qDebug() << "same tex";
        //this->getPatchRotationName()
        //rotateTex(u);
        autoRot = true;
    } else {
        QString path = texturepath + brush->tex->pathid.section("/", -1);
        qDebug() << path;
        if (!QFile::exists(path)){
            int newTidx = TexLib::getTex(path);
            if(newTidx >= 0){
                qDebug() << "ref tex";
                texid[u] = newTidx;
            } else {
                qDebug() << "new tex";
                texid[u] = TexLib::cloneTex(brush->texId);
                path = path.section(".", 0, -2) + ".ace";
                TexLib::mtex[texid[u]]->pathid = path;
                texModified[u] = true;
            }
            brush->texId = texid[u];
            brush->tex = TexLib::mtex[texid[u]];
        }
        texid[u] = brush->texId;
        uniqueTex[u] = false;
        QString tname = TexLib::mtex[brush->texId]->pathid.section("/", -1);
        qDebug() << TexLib::mtex[brush->texId]->pathid;
        qDebug() << tname;
        int mid = tfile->getMatByTexture(tname);
        if(mid < 0){
            tfile->tdata[(u)*13 + 0 + 6] = tfile->cloneMat(tfile->tdata[(u)*13 + 0 + 6]);
            *tfile->materials[(int) tfile->tdata[(u)*13 + 0 + 6]].tex[0] = tname;
            *tfile->amaterials[(int) tfile->tdata[(u)*13 + 0 + 6]].tex[0] = tname;
            qDebug() << *tfile->materials[(int) tfile->tdata[(u)*13 + 0 + 6]].tex[0];
            qDebug() << "new material";
        } else {
            tfile->tdata[(u)*13 + 0 + 6] = mid;
            qDebug() << "existed material";
        }
        reloadLines();
    }
    if(brush->texTransformation == brush->RANDOM && autoRot){
        rotateTex(u);
    } else if(brush->texTransformation == brush->PRESENT){
        
    } else {
        int count = 0;
        resetPatchTexCoords(u);
        if(brush->texTransformation == brush->ROT90)
            count = 1;
        if(brush->texTransformation == brush->ROT180)
            count = 2;
        if(brush->texTransformation == brush->ROT270)
            count = 3;
        qDebug() << count;
        for(int i = 0; i < count; i++)
            rotateTex(u);
    }    
    
    /*QString name = this->getTileName(mojex, -mojez)+"_"+QString::number(y)+"_"+QString::number(u)+".ace";
    
    if(name != *tfile->materials[(int)tfile->tdata[(y * 16 + u)*13+0+6]].tex[0]){
        tfile->tdata[(y * 16 + u)*13+0+6] = tfile->cloneMat(tfile->tdata[(y * 16 + u)*13+0+6]);
     *tfile->materials[(int)tfile->tdata[(y * 16 + u)*13+0+6]].tex[0] = name;
        qDebug() << *tfile->materials[(int)tfile->tdata[(y * 16 + u)*13+0+6]].tex[0];
        texid[y * 16 + u] = TexLib::cloneTex(texid[y * 16 + u]);
        
        TexLib::save("ace", texturepath+name, texid[y * 16 + u]);
        //TexLib::mtex[texid[y * 16 + u]]->GLTextures();
    }*/

    this->modified = true;
}

void Terrain::paintTexture(Brush* brush, int x, int z, float posx, float posz) {
    if (!editable)
        return;
    //if(Game::seasonalEditing)
    //    return;
    
    int samples = *tfile->nsamples;
    int sampleSize = *tfile->sampleSize;
    float tileSize = sampleSize*samples;
    int patches = tfile->patchsetNpatches;
    int patchSize = tileSize/patches;
    getPatchCoords(x, z, posx, posz);
    
    int u = x;
    int y = z;


    float size = (float) (brush->size) / (512);
    //qDebug() << "size " << size << " " << tx / 128 << " " << tz / 128;

    for (int i = u - 1; i < u + 2; i++)
        for (int j = y - 1; j < y + 2; j++) {
            if (i < 0 || j < 0 || i >= patches || j >= patches)
                continue;
            float tx = posx - i * patchSize;
            float tz = posz - j * patchSize;
            tx /= patchSize;
            tz /= patchSize;
            //qDebug() << tx << " " << tz;
            if ((tx < 0.0 - size) || (tx > 1.0 + size) || (tz < 0.0 - size) || (tz > 1.0 + size))
                continue;
            if(!texLocked[j * patches + i])
                this->paintTextureOnTile(brush, j, i, tx, tz);
        }

}

void Terrain::lockTexture(Brush* brush, int x, int z, float posx, float posz) {
    if (!editable)
        return;
    getPatchCoords(x, z, posx, posz);
    int patches = tfile->patchsetNpatches;
    texLocked[z * patches + x] = !texLocked[z * patches + x];
    reloadLines();
}

void Terrain::paintTextureOnTile(Brush* brush, int y, int u, float x, float z) {
    //qDebug() << "painttile " << x << " " << z;
    int patches = tfile->patchsetNpatches;
    if (y > patches-1 || u > patches-1 || y < 0 || u < 0) return;
    //qDebug() << "painttile " << y << " " << u;
    QString name = this->getTileName(mojex, -mojez) + "_" + QString::number(y) + "_" + QString::number(u) + ".ace";
    if (texid[y * patches + u] < 0)
        return;
    
    if (name != *tfile->materials[(int) tfile->tdata[(y * patches + u)*13 + 0 + 6]].tex[0]) {
        if(Game::seasonalEditing && Game::season.length() > 0)
            return;
        
        tfile->tdata[(y * patches + u)*13 + 0 + 6] = tfile->cloneMat(tfile->tdata[(y * patches + u)*13 + 0 + 6]);
        tfile->materials[tfile->tdata[(y * patches + u)*13 + 0 + 6]].itex[1][3] = 1107296256;
        *tfile->materials[(int) tfile->tdata[(y * patches + u)*13 + 0 + 6]].tex[0] = name;
        *tfile->amaterials[(int) tfile->tdata[(y * patches + u)*13 + 0 + 6]].tex[0] = name;
        qDebug() << *tfile->materials[(int) tfile->tdata[(y * patches + u)*13 + 0 + 6]].tex[0];
        texid[y * patches + u] = TexLib::cloneTex(texid[y * patches + u]);
        TexLib::mtex[texid[y * patches + u]]->pathid = texturepath + name;
        uniqueTex[y * patches + u] = true;
        reloadLines();
        //TexLib::save("ace", texturepath+name, texid[y * 16 + u]);
        //TexLib::mtex[texid[y * 16 + u]]->GLTextures();
    }
    convertTexToDefaultCoords(y * patches + u);

    TexLib::mtex[texid[y * patches + u]]->sendToUndo(texid[y * patches + u]);
    TexLib::mtex[texid[y * patches + u]]->paint(brush, z, x);
    TexLib::mtex[texid[y * patches + u]]->update();
    this->texModified[y * patches + u] = true;
    this->modified = true;
}

void Terrain::pushRenderItem(float lodx, float lodz, int tileX, int tileY, float* playerW, float* target, float fov, quint32 selectionId){
    if (!loaded)
        return;
    TerrainMeshBackend *backend = ensureMeshBackend();
    if (backend == NULL || !backend->ensureInitialized())
        return;

    if (!lines.loaded) {
        reloadLines();
    }
    
    int samples = *tfile->nsamples;
    int sampleSize = *tfile->sampleSize;
    int patches = tfile->patchsetNpatches;
    int patchRes = samples/patches;
    
    if(mojex-tileX != 0 || mojez-tileY != 0){
        Mat4::translate(Game::currentRenderer->mvMatrix, Game::currentRenderer->mvMatrix, 2048 * (mojex-tileX) , 0, 2048 * (mojez-tileY) );
    }
    Mat4::translate(Game::currentRenderer->mvMatrix, Game::currentRenderer->mvMatrix, -1024, 0, 1024-sampleSize*samples);
    refreshPatchBounds(true);
    const PatchVisibility patchVisibility = buildPatchVisibility(
                Game::currentRenderer->mvMatrix, playerW);
    const QVector<TerrainPatchLodState> patchLod = backend->isPaged()
            ? buildPatchLodState(patchVisibility)
            : QVector<TerrainPatchLodState>();
    if(Game::viewWorldGrid && selectionId == 0)
        lines.pushRenderItem();
    if(Game::viewTileGrid && selectionId == 0){
        slines.pushRenderItem();
        ulines.pushRenderItem();
        lockedlines.pushRenderItem();
        selectedlines.pushRenderItem();
    }
    
    float lod = 0;
    float size = 512;

    RenderItem *r;
    if(Game::viewTerrainShape && (!(showBlob && MapWindow::isAlpha == 0) || selectionId != 0)){
        float shaderSecondTexUV = 0;
        for (int yy = 0; yy < patches; yy++) {
            for (int uu = 0; uu < patches; uu++) {
                const int patchId = yy * patches + uu;
                if (hidden[patchId]) continue;
                if ((tfile->flags[patchId] & 1) != 0) continue;
                if (!isPatchVisible(patchId, patchVisibility)) continue;
                /*float lodxx = lodx + uu * 128 - 1024;
                float lodzz = lodz + yy * 128 - 1024;
                lod = sqrt(lodxx * lodxx + lodzz * lodzz);
                //System.out.println("-- "+lodxx+" "+lodzz);
                if (lod > Game::objectLod) continue;

                if ((lod > size)) {
                    float v1[2];
                    v1[0] = playerW[0] - (target[0]);
                    v1[1] = playerW[2] - (target[2]);
                    float v2[2];
                    v2[0] = lodxx;
                    v2[1] = lodzz;
                    float iloczyn = v1[0] * v2[0] + v1[1] * v2[1];
                    float d1 = sqrt(v1[0] * v1[0] + v1[1] * v1[1]);
                    float d2 = sqrt(v2[0] * v2[0] + v2[1] * v2[1]);
                    float zz = iloczyn / (d1 * d2);
                    if (zz > 0) continue;

                    float ccos = cos(fov) + zz;
                    float xxx = sqrt(2 * d2 * d2 * (1 - ccos));
                    if ((ccos > 0) && (xxx > size)) continue;
                }*/
                
                r = new RenderItem();
                if(selectionId != 0){
                    r->setSelectionId(SelectionIdCodec::withTerrainPatch(
                                          selectionId, patchId));
                } else {
                    if (texid[yy * patches + uu] == -2) {
                    } else {
                        if (texid[yy * patches + uu] == -1) {
                            //texid[uu*16+yy] = TexLib.addTex(texturepath,"nasyp-k.ace", gl);
                            //qDebug() << texturepath << " "<<tfile->tdata[(yy * 16 + uu)*7+0] <<" "<< tfile->materials[(int)tfile->tdata[(yy * 16 + uu)*7+0]].tex[0];
                            if (tfile->materialsCount <= (int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6]){
                                texid[yy * patches + uu] = -2;
                                return;
                            } else {
                                texid[yy * patches + uu] = TexLib::addTex(texturepath, *tfile->materials[(int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6]].tex[0]);
                            }//System.out.println(tfile.materials[tfile.tdata[uu*16+yy]].tex[0]);
                            //texid = TexLib.addTex(texturepath,"nasyp-k.ace", gl);
                            //    gl.glDisable(GL2.GL_TEXTURE_2D);
                        }
                        if (TexLib::mtex[texid[yy * patches + uu]]->loaded) {
                            if (!TexLib::mtex[texid[yy * patches + uu]]->glLoaded)
                                TexLib::mtex[texid[yy * patches + uu]]->GLTextures();
                            r->enableTextures(TexLib::mtex[texid[yy * patches + uu]]->tex[0]);
                        } else {
                        }
                    }
                    /*if (texid2[yy * patches + uu] == -2) {
                    } else if (tfile->materials[(int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6]].count153 < 2){
                            texid2[yy * patches + uu] = -2;
                    } else {
                        if (texid2[yy * patches + uu] == -1) {
                            if (tfile->materialsCount <= (int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6])
                                texid[yy * patches + uu] = -2;
                            else
                                texid2[yy * patches + uu] = TexLib::addTex(texturepath, *tfile->materials[(int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6]].tex[1]);
                        }
                        if (TexLib::mtex[texid2[yy * patches + uu]]->loaded) {
                            if (!TexLib::mtex[texid2[yy * patches + uu]]->glLoaded)
                                TexLib::mtex[texid2[yy * patches + uu]]->GLTextures(true);
                            r->enableTextures(TexLib::mtex[texid2[yy * patches + uu]]->tex[0]);
                            if(shaderSecondTexUV != *(float*)&tfile->materials[(int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6]].itex[1][3]){
                                shaderSecondTexUV = *(float*)&tfile->materials[(int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6]].itex[1][3];
                                gluu->currentShader->setUniformValue(gluu->currentShader->shaderSecondTexEnabled, shaderSecondTexUV);
                            }
                        } else {
                        }
                    }*/
                }
                r->itemType = GL_TRIANGLES;
                const TerrainPatchLodState lodState = patchId < patchLod.size()
                        ? patchLod[patchId] : TerrainPatchLodState{};
                backend->configureRenderItem(*r, patchId,
                                             false, true,
                                             lodState.sourceStep,
                                             lodState.edgeMask);
                r->msMatrix = Game::currentRenderer->objStrMatrix;
                r->setVertexAttributes(r->VNT);
                Game::currentRenderer->pushItem(r, Game::currentRenderer->mvMatrix);
            }
        }
    }
    if(Game::viewTerrainGrid || !Game::viewTerrainShape){
        Game::currentRenderer->mvPushMatrix();
        Mat4::translate(Game::currentRenderer->mvMatrix, Game::currentRenderer->mvMatrix, 0, 0.05, 0);

        for (int yy = 0; yy < patches; yy++) {
            for (int uu = 0; uu < patches; uu++) {
                const int patchId = yy * patches + uu;
                if (hidden[patchId]) continue;
                if ((tfile->flags[patchId] & 1) != 0) continue;
                if (!isPatchVisible(patchId, patchVisibility)) continue;
                const float lodxx = lodx
                        + TerrainGridLayout::WorldTileSize * (mojex - tileX)
                        - TerrainGridLayout::WorldTileHalfSize
                        + uu * gridLayout.patchWorldSize;
                const float lodzz = lodz
                        + TerrainGridLayout::WorldTileSize * (mojez - tileY)
                        + TerrainGridLayout::WorldTileHalfSize
                        - gridLayout.terrainWorldSize
                        + yy * gridLayout.patchWorldSize;
                lod = sqrt(lodxx * lodxx + lodzz * lodzz);
                if(Game::viewTerrainShape)
                    if (lod > 300) continue;
                r = new RenderItem();
                r->disableTextures(0.7,0.7,0.7,1.0);
                r->itemType = GL_TRIANGLES;
                const TerrainPatchLodState lodState = patchId < patchLod.size()
                        ? patchLod[patchId] : TerrainPatchLodState{};
                backend->configureRenderItem(*r, patchId,
                                             false, true,
                                             lodState.sourceStep,
                                             lodState.edgeMask);
                r->setVertexAttributes(r->VNT);
                r->polygonMode = 1;
                r->msMatrix = Game::currentRenderer->objStrMatrix;
                Game::currentRenderer->pushItem(r, Game::currentRenderer->mvMatrix);
            }
        }
        Game::currentRenderer->mvPopMatrix();
    }
    
    if(showBlob && selectionId == 0){
        if (backend->isPaged()) {
            const int mapTexture = ensureMapTexture();
            if (mapTexture >= 0) {
                if(MapWindow::isAlpha != 0){
                    Game::currentRenderer->mvPushMatrix();
                    Mat4::translate(Game::currentRenderer->mvMatrix,
                                    Game::currentRenderer->mvMatrix,
                                    0, 0.35, 0);
                }
                for (int patchId = 0; patchId < gridLayout.patchRecordCount(); ++patchId) {
                    if (!isPatchVisible(patchId, patchVisibility))
                        continue;
                    r = new RenderItem();
                    r->enableTextures(static_cast<unsigned int>(mapTexture));
                    r->itemType = GL_TRIANGLES;
                    r->msMatrix = Game::currentRenderer->objStrMatrix;
                    r->setVertexAttributes(r->VNTA);
                    const TerrainPatchLodState lodState = patchId < patchLod.size()
                            ? patchLod[patchId] : TerrainPatchLodState{};
                    backend->configureRenderItem(*r, patchId, true, false,
                                                 lodState.sourceStep,
                                                 lodState.edgeMask);
                    Game::currentRenderer->pushItem(r,
                                                    Game::currentRenderer->mvMatrix);
                }
                if(MapWindow::isAlpha != 0)
                    Game::currentRenderer->mvPopMatrix();
            }
        } else if(MapWindow::isAlpha == 0){
            terrainBlob.pushRenderItem();
        }else{
            Game::currentRenderer->mvPushMatrix();
            Mat4::translate(Game::currentRenderer->mvMatrix, Game::currentRenderer->mvMatrix, 0, 0.35, 0);
            terrainBlob.pushRenderItem();
            Game::currentRenderer->mvPopMatrix();
        }
    }
}

void Terrain::pushRenderItemWater(float lodx, float lodz, float tileX, float tileY, float* playerW, float* target, float fov, int layer, quint32 selectionId){
    if(showBlob)
        return;
    float alpha = 0;
    int samples = *tfile->nsamples;
    int sampleSize = *tfile->sampleSize;
    int patches = tfile->patchsetNpatches;
    int tileSize = (sampleSize*samples);
    int patchSize = tileSize/patches;
    
    if(mojex-tileX != 0 || mojez-tileY != 0){
        Mat4::translate(Game::currentRenderer->mvMatrix, Game::currentRenderer->mvMatrix, 2048 * (mojex-tileX) , 0, 2048 * (mojez-tileY) );
    }
    Mat4::translate(Game::currentRenderer->mvMatrix, Game::currentRenderer->mvMatrix, -1024, 0, 1024-sampleSize*samples);
    
    if(water[layer] == NULL)
        water[layer] = new WaterTile();
    OglObj *w = water[layer]->w;
    
    quint32 tselectionId = 0;
    for (int uu = 0; uu < patches; uu++) {
        for (int yy = 0; yy < patches; yy++) {
            if (hidden[yy * patches + uu]) continue;
            if ((tfile->flags[yy * patches + uu] & 0xc0) != 0) {
                if (!w[uu * patches + yy].loaded) {

                    float x1 = (uu)*patchSize;
                    float x2 = (uu + 1)*patchSize;
                    float z1 = (yy)*patchSize;
                    float z2 = (yy + 1)*patchSize;
                    float x1z1 = (((x1)*(z1)) / (tileSize * tileSize)) * tfile->WSE +
                            (((tileSize - x1)*(z1)) / (tileSize * tileSize)) * tfile->WSW +
                            (((tileSize - x1)*(tileSize - z1)) / (tileSize * tileSize)) * tfile->WNW +
                            (((x1)*(tileSize - z1)) / (tileSize * tileSize)) * tfile->WNE;
                    float x2z1 = (((x2)*(z1 )) / (tileSize * tileSize)) * tfile->WSE +
                            (((tileSize - x2)*(z1 )) / (tileSize * tileSize)) * tfile->WSW +
                            (((tileSize - x2)*(tileSize - z1)) / (tileSize * tileSize)) * tfile->WNW +
                            (((x2)*(tileSize - z1)) / (tileSize * tileSize)) * tfile->WNE;
                    float x1z2 = (((x1 )*(z2 )) / (tileSize * tileSize)) * tfile->WSE +
                            (((tileSize - x1)*(z2)) / (tileSize * tileSize)) * tfile->WSW +
                            (((tileSize - x1)*(tileSize - z2)) / (tileSize * tileSize)) * tfile->WNW +
                            (((x1)*(tileSize - z2)) / (tileSize * tileSize)) * tfile->WNE;
                    float x2z2 = (((x2)*(z2)) / (tileSize * tileSize)) * tfile->WSE +
                            (((tileSize - x2)*(z2)) / (tileSize * tileSize)) * tfile->WSW +
                            (((tileSize - x2)*(tileSize - z2)) / (tileSize * tileSize)) * tfile->WNW +
                            (((x2)*(tileSize - z2)) / (tileSize * tileSize)) * tfile->WNE;

                    float *punkty = new float[54];
                    int ptr = 0;

                    punkty[ptr++] = x2;
                    punkty[ptr++] = x2z2;
                    punkty[ptr++] = z2;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 4;
                    punkty[ptr++] = 4;
                    punkty[ptr++] = alpha;

                    punkty[ptr++] = x2;
                    punkty[ptr++] = x2z1;
                    punkty[ptr++] = z1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 4;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = alpha;

                    punkty[ptr++] = x1;
                    punkty[ptr++] = x1z1;
                    punkty[ptr++] = z1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = alpha;

                    punkty[ptr++] = x1;
                    punkty[ptr++] = x1z2;
                    punkty[ptr++] = z2;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 4;
                    punkty[ptr++] = alpha;

                    punkty[ptr++] = x2;
                    punkty[ptr++] = x2z2;
                    punkty[ptr++] = z2;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 4;
                    punkty[ptr++] = 4;
                    punkty[ptr++] = alpha;

                    punkty[ptr++] = x1;
                    punkty[ptr++] = x1z1;
                    punkty[ptr++] = z1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = alpha;
                    //QString *texturePath = new QString("resources/woda.ace");
                    //water[uu * 16 + yy].setMaterial(texturePath);
                    w[uu * patches + yy].setMaterial(&Game::currentRoute->env->water[layer].tex);
                    w[uu * patches + yy].init(punkty, ptr, RenderItem::VNTA, GL_TRIANGLES);
                    delete punkty;
                }
                if(selectionId != 0)
                    tselectionId = SelectionIdCodec::withTerrainPatch(
                                selectionId, yy * patches + uu);
                w[uu * patches + yy].pushRenderItem(tselectionId);
            }
        }
    }
}

void Terrain::render(float lodx, float lodz, int tileX, int tileY, float* playerW, float* target, float fov, quint32 selectionId) {
    if (!loaded)
        return;
    TerrainMeshBackend *backend = ensureMeshBackend();
    if (backend == NULL || !backend->ensureInitialized())
        return;

    GLUU* gluu = GLUU::get();
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    if (!lines.loaded) {
        reloadLines();
    }
    
    int samples = *tfile->nsamples;
    int sampleSize = *tfile->sampleSize;
    int patches = tfile->patchsetNpatches;
    int patchRes = samples/patches;
    
    //qDebug() << 2048 * (mojex-tileX) << 2048 * (mojez-tileY);
    if(mojex-tileX != 0 || mojez-tileY != 0){
        //qDebug() << mojex << mojez;
        //qDebug() << mojex-tileX << mojez-tileY;
        Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, 2048 * (mojex-tileX) , 0, 2048 * (mojez-tileY) );
    }
    Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, -1024, 0, 1024-sampleSize*samples);
    refreshPatchBounds(true);
    const PatchVisibility patchVisibility = buildPatchVisibility(
                gluu->mvMatrix, playerW);
    const QVector<TerrainPatchLodState> patchLod = backend->isPaged()
            ? buildPatchLodState(patchVisibility)
            : QVector<TerrainPatchLodState>();
    gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->mvMatrix));
    gluu->currentShader->setUniformValue(gluu->currentShader->msMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->objStrMatrix));
    gluu->currentMsMatrinxHash = 0;//gluu->getMatrixHash(gluu->objStrMatrix);
    if(Game::viewWorldGrid && selectionId == 0)
        lines.render();
    if(Game::viewTileGrid && selectionId == 0){
        slines.render();
        ulines.render();
        lockedlines.render();
        selectedlines.render();
    }

    gluu->enableTextures();  
    gluu->enableNormals();
    
    float lod = 0;
    float size = 512;

    QOpenGLVertexArrayObject::Binder *legacyVaoBinder = backend->isPaged()
            ? NULL : new QOpenGLVertexArrayObject::Binder(VAO);
    
    if(Game::viewTerrainShape && (!(showBlob && MapWindow::isAlpha == 0) || selectionId != 0)){
        float shaderSecondTexUV = 0;
        for (int yy = 0; yy < patches; yy++) {
            for (int uu = 0; uu < patches; uu++) {
                const int patchId = yy * patches + uu;
                if (hidden[patchId]) continue;
                if ((tfile->flags[patchId] & 1) != 0) continue;
                if (!isPatchVisible(patchId, patchVisibility)) continue;
                /*float lodxx = lodx + uu * 128 - 1024;
                float lodzz = lodz + yy * 128 - 1024;
                lod = sqrt(lodxx * lodxx + lodzz * lodzz);
                //System.out.println("-- "+lodxx+" "+lodzz);
                if (lod > Game::objectLod) continue;

                if ((lod > size)) {
                    float v1[2];
                    v1[0] = playerW[0] - (target[0]);
                    v1[1] = playerW[2] - (target[2]);
                    float v2[2];
                    v2[0] = lodxx;
                    v2[1] = lodzz;
                    float iloczyn = v1[0] * v2[0] + v1[1] * v2[1];
                    float d1 = sqrt(v1[0] * v1[0] + v1[1] * v1[1]);
                    float d2 = sqrt(v2[0] * v2[0] + v2[1] * v2[1]);
                    float zz = iloczyn / (d1 * d2);
                    if (zz > 0) continue;

                    float ccos = cos(fov) + zz;
                    float xxx = sqrt(2 * d2 * d2 * (1 - ccos));
                    if ((ccos > 0) && (xxx > size)) continue;
                }*/

                if(selectionId != 0){
                    gluu->setSelectionId(SelectionIdCodec::withTerrainPatch(
                                             selectionId, patchId));
                } else {
                    if (texid[yy * patches + uu] == -2) {
                    } else {
                        if (texid[yy * patches + uu] == -1) {
                            //texid[uu*16+yy] = TexLib.addTex(texturepath,"nasyp-k.ace", gl);
                            //qDebug() << texturepath << " "<<tfile->tdata[(yy * 16 + uu)*7+0] <<" "<< tfile->materials[(int)tfile->tdata[(yy * 16 + uu)*7+0]].tex[0];
                            if (tfile->materialsCount <= (int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6]){
                                texid[yy * patches + uu] = -2;
                                return;
                            } else {
                                texid[yy * patches + uu] = TexLib::addTex(texturepath, *tfile->materials[(int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6]].tex[0]);
                            }//System.out.println(tfile.materials[tfile.tdata[uu*16+yy]].tex[0]);
                            //texid = TexLib.addTex(texturepath,"nasyp-k.ace", gl);
                            //    gl.glDisable(GL2.GL_TEXTURE_2D);
                        }
                        if (TexLib::mtex[texid[yy * patches + uu]]->loaded) {
                            if (!TexLib::mtex[texid[yy * patches + uu]]->glLoaded)
                                TexLib::mtex[texid[yy * patches + uu]]->GLTextures();
                            f->glActiveTexture(GL_TEXTURE0);
                            //f->glBindTexture(GL_TEXTURE_2D, TexLib::mtex[texid[yy * 16 + uu]]->tex[0]);
                            gluu->bindTexture(f, TexLib::mtex[texid[yy * patches + uu]]->tex[0]);
                        } else {
                        }
                    }
                    if (texid2[yy * patches + uu] == -2) {
                    } else if (tfile->materials[(int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6]].count153 < 2){
                            texid2[yy * patches + uu] = -2;
                    } else {
                        if (texid2[yy * patches + uu] == -1) {
                            if (tfile->materialsCount <= (int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6])
                                texid[yy * patches + uu] = -2;
                            else
                                texid2[yy * patches + uu] = TexLib::addTex(texturepath, *tfile->materials[(int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6]].tex[1]);
                        }
                        if (TexLib::mtex[texid2[yy * patches + uu]]->loaded) {
                            if (!TexLib::mtex[texid2[yy * patches + uu]]->glLoaded)
                                TexLib::mtex[texid2[yy * patches + uu]]->GLTextures(true);
                            f->glActiveTexture(GL_TEXTURE1);
                            f->glBindTexture(GL_TEXTURE_2D, TexLib::mtex[texid2[yy * patches + uu]]->tex[0]);
                            if(shaderSecondTexUV != *(float*)&tfile->materials[(int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6]].itex[1][3]){
                                shaderSecondTexUV = *(float*)&tfile->materials[(int) tfile->tdata[(yy * patches + uu)*13 + 0 + 6]].itex[1][3];
                                gluu->currentShader->setUniformValue(gluu->currentShader->shaderSecondTexEnabled, shaderSecondTexUV);
                            }
                        } else {
                        }
                    }
                }
                
                const TerrainPatchLodState lodState = patchId < patchLod.size()
                        ? patchLod[patchId] : TerrainPatchLodState{};
                backend->drawPatch(patchId, false, true,
                                   lodState.sourceStep, lodState.edgeMask);
            }
        }
        f->glActiveTexture(GL_TEXTURE0);
        gluu->currentShader->setUniformValue(gluu->currentShader->shaderSecondTexEnabled, 0.0f);
    }
        
    if(Game::viewTerrainGrid || !Game::viewTerrainShape){
        gluu->disableTextures(0.7,0.7,0.7,1.0);
        gluu->mvPushMatrix();
        Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, 0, 0.05, 0);
        gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->mvMatrix));
        glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
        for (int yy = 0; yy < patches; yy++) {
            for (int uu = 0; uu < patches; uu++) {
                const int patchId = yy * patches + uu;
                if (hidden[patchId]) continue;
                if ((tfile->flags[patchId] & 1) != 0) continue;
                if (!isPatchVisible(patchId, patchVisibility)) continue;
                const float lodxx = lodx
                        + TerrainGridLayout::WorldTileSize * (mojex - tileX)
                        - TerrainGridLayout::WorldTileHalfSize
                        + uu * gridLayout.patchWorldSize;
                const float lodzz = lodz
                        + TerrainGridLayout::WorldTileSize * (mojez - tileY)
                        + TerrainGridLayout::WorldTileHalfSize
                        - gridLayout.terrainWorldSize
                        + yy * gridLayout.patchWorldSize;
                lod = sqrt(lodxx * lodxx + lodzz * lodzz);
                if(Game::viewTerrainShape)
                    if (lod > 300) continue;
                const TerrainPatchLodState lodState = patchId < patchLod.size()
                        ? patchLod[patchId] : TerrainPatchLodState{};
                backend->drawPatch(patchId, false, true,
                                   lodState.sourceStep, lodState.edgeMask);
            }
        }
        gluu->mvPopMatrix();
        glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
    }
    
    if(showBlob && selectionId == 0){
        if (backend->isPaged()) {
            const int mapTexture = ensureMapTexture();
            if (mapTexture >= 0) {
                gluu->enableTextures();
                gluu->bindTexture(f, static_cast<unsigned int>(mapTexture));
                if(MapWindow::isAlpha != 0){
                    gluu->mvPushMatrix();
                    Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, 0, 0.35, 0);
                    gluu->currentShader->setUniformValue(
                                gluu->currentShader->mvMatrixUniform,
                                *reinterpret_cast<float(*)[4][4]>(gluu->mvMatrix));
                }
                for (int patchId = 0; patchId < gridLayout.patchRecordCount(); ++patchId) {
                    if (isPatchVisible(patchId, patchVisibility)) {
                        const TerrainPatchLodState lodState = patchId < patchLod.size()
                                ? patchLod[patchId] : TerrainPatchLodState{};
                        backend->drawPatch(patchId, true, false,
                                           lodState.sourceStep,
                                           lodState.edgeMask);
                    }
                }
                if(MapWindow::isAlpha != 0)
                    gluu->mvPopMatrix();
            }
        } else if(MapWindow::isAlpha == 0){
            gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->mvMatrix));
            terrainBlob.render();
        }else{
            gluu->mvPushMatrix();
            Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, 0, 0.35, 0);
            gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->mvMatrix));
            terrainBlob.render();
            gluu->mvPopMatrix();
        }
    }

    delete legacyVaoBinder;
    backend->endDirectRender();
    
    gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->mvMatrix));
            
    //if(!showBlob && selectionId == 0)
    //    renderWater(lodx, lodz, playerT, playerW, target, fov);
}

void Terrain::renderWater(float lodx, float lodz, float tileX, float tileY, float* playerW, float* target, float fov, int layer, quint32 selectionId) {
    float lod;
    if(showBlob)
        return;
    float alpha = 0;
    
    GLUU* gluu = GLUU::get();
    
    int samples = *tfile->nsamples;
    int sampleSize = *tfile->sampleSize;
    int patches = tfile->patchsetNpatches;
    int patchRes = samples/patches;
    int tileSize = (sampleSize*samples);
    int patchSize = tileSize/patches;
    
    if(mojex-tileX != 0 || mojez-tileY != 0){
        Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, 2048 * (mojex-tileX) , 0, 2048 * (mojez-tileY) );
    }
    Mat4::translate(gluu->mvMatrix, gluu->mvMatrix, -1024, 0, 1024-sampleSize*samples);
    gluu->currentShader->setUniformValue(gluu->currentShader->mvMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->mvMatrix));
    gluu->currentShader->setUniformValue(gluu->currentShader->msMatrixUniform, *reinterpret_cast<float(*)[4][4]> (gluu->objStrMatrix));

    
    if(water[layer] == NULL)
        water[layer] = new WaterTile();
    OglObj *w = water[layer]->w;
    
    quint32 tselectionId = 0;
    for (int uu = 0; uu < patches; uu++) {
        for (int yy = 0; yy < patches; yy++) {
            if (hidden[yy * patches + uu]) continue;
            /*float lodxx = lodx + uu * 128 - 1024;
            float lodzz = lodz + yy * 128 - 1024;
            lod = sqrt(lodxx * lodxx + lodzz * lodzz);
            //System.out.println("-- "+lodxx+" "+lodzz);
            if (lod > Game::objectLod) continue;*/
            
            if ((tfile->flags[yy * patches + uu] & 0xc0) != 0) {

                if (!w[uu * patches + yy].loaded) {

                    float x1 = (uu)*patchSize;
                    float x2 = (uu + 1)*patchSize;
                    float z1 = (yy)*patchSize;
                    float z2 = (yy + 1)*patchSize;
                    float x1z1 = (((x1)*(z1)) / (tileSize * tileSize)) * tfile->WSE +
                            (((tileSize - x1)*(z1)) / (tileSize * tileSize)) * tfile->WSW +
                            (((tileSize - x1)*(tileSize - z1)) / (tileSize * tileSize)) * tfile->WNW +
                            (((x1)*(tileSize - z1)) / (tileSize * tileSize)) * tfile->WNE;
                    float x2z1 = (((x2)*(z1 )) / (tileSize * tileSize)) * tfile->WSE +
                            (((tileSize - x2)*(z1 )) / (tileSize * tileSize)) * tfile->WSW +
                            (((tileSize - x2)*(tileSize - z1)) / (tileSize * tileSize)) * tfile->WNW +
                            (((x2)*(tileSize - z1)) / (tileSize * tileSize)) * tfile->WNE;
                    float x1z2 = (((x1 )*(z2 )) / (tileSize * tileSize)) * tfile->WSE +
                            (((tileSize - x1)*(z2)) / (tileSize * tileSize)) * tfile->WSW +
                            (((tileSize - x1)*(tileSize - z2)) / (tileSize * tileSize)) * tfile->WNW +
                            (((x1)*(tileSize - z2)) / (tileSize * tileSize)) * tfile->WNE;
                    float x2z2 = (((x2)*(z2)) / (tileSize * tileSize)) * tfile->WSE +
                            (((tileSize - x2)*(z2)) / (tileSize * tileSize)) * tfile->WSW +
                            (((tileSize - x2)*(tileSize - z2)) / (tileSize * tileSize)) * tfile->WNW +
                            (((x2)*(tileSize - z2)) / (tileSize * tileSize)) * tfile->WNE;

                    float *punkty = new float[54];
                    int ptr = 0;

                    punkty[ptr++] = x2;
                    punkty[ptr++] = x2z2;
                    punkty[ptr++] = z2;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 4;
                    punkty[ptr++] = 4;
                    punkty[ptr++] = alpha;

                    punkty[ptr++] = x2;
                    punkty[ptr++] = x2z1;
                    punkty[ptr++] = z1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 4;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = alpha;

                    punkty[ptr++] = x1;
                    punkty[ptr++] = x1z1;
                    punkty[ptr++] = z1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = alpha;

                    punkty[ptr++] = x1;
                    punkty[ptr++] = x1z2;
                    punkty[ptr++] = z2;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 4;
                    punkty[ptr++] = alpha;

                    punkty[ptr++] = x2;
                    punkty[ptr++] = x2z2;
                    punkty[ptr++] = z2;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 4;
                    punkty[ptr++] = 4;
                    punkty[ptr++] = alpha;

                    punkty[ptr++] = x1;
                    punkty[ptr++] = x1z1;
                    punkty[ptr++] = z1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 1;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = 0;
                    punkty[ptr++] = alpha;
                    //QString *texturePath = new QString("resources/woda.ace");
                    //water[uu * 16 + yy].setMaterial(texturePath);
                    w[uu * patches + yy].setMaterial(&Game::currentRoute->env->water[layer].tex);
                    w[uu * patches + yy].init(punkty, ptr, RenderItem::VNTA, GL_TRIANGLES);
                    delete punkty;
                }
                if(selectionId != 0)
                    tselectionId = SelectionIdCodec::withTerrainPatch(
                                selectionId, yy * patches + uu);
                w[uu * patches + yy].render(tselectionId);
            }
        }
    }
}

void Terrain::reloadLines() {
    // tile lines
    int samples = *tfile->nsamples;
    int sampleSize = gridLayout.sampleSpacing;
    int patches = gridLayout.patchesPerSide;
    int patchRes = gridLayout.patchResolution;
    float *punkty = new float[static_cast<std::size_t>(samples) * 6 * 4];
    int ptr = 0;
    int i = 0;
    int patchSize = (samples*sampleSize)/patches;
    int min = 0;
    int max = sampleSize * samples;
    
    for (i = 0; i < samples; i++) {
        punkty[ptr++] = min;
        punkty[ptr++] = 1 + terrainData[i][0];
        punkty[ptr++] = min + i *sampleSize;
        punkty[ptr++] = min;
        punkty[ptr++] = 1 + terrainData[i + 1][0];
        punkty[ptr++] = min + i *sampleSize +sampleSize;
    }

    for (i = 0; i < samples; i++) {
        punkty[ptr++] = min + i *sampleSize;
        punkty[ptr++] = 1 + terrainData[0][i];
        punkty[ptr++] = min;
        punkty[ptr++] = min + i *sampleSize +sampleSize;
        punkty[ptr++] = 1 + terrainData[0][i + 1];
        punkty[ptr++] = min;
    }

    for (i = 0; i < samples; i++) {
        punkty[ptr++] = max;
        punkty[ptr++] = 1 + terrainData[i][samples];
        punkty[ptr++] = min + i *sampleSize;
        punkty[ptr++] = max;
        punkty[ptr++] = 1 + terrainData[i + 1][samples];
        punkty[ptr++] = min + i *sampleSize +sampleSize;
    }

    for (i = 0; i < samples; i++) {
        punkty[ptr++] = min + i *sampleSize;
        punkty[ptr++] = 1 + terrainData[samples][i];
        punkty[ptr++] = max;
        punkty[ptr++] = min + i *sampleSize +sampleSize;
        punkty[ptr++] = 1 + terrainData[samples][i + 1];
        punkty[ptr++] = max;
    }

    lines.setMaterial(1.0, 0.0, 0.0);
    lines.init(punkty, ptr, RenderItem::V, GL_LINES);
    delete[] punkty;
    //s tile lines
    punkty = new float[static_cast<std::size_t>(samples) * patches * 12];
    ptr = 0;
    i = 0;

    for (int j = 0; j < samples; j += patchRes) {
        for (i = 0; i < samples; i++) {
            punkty[ptr++] = min + j *sampleSize;
            punkty[ptr++] = 0.9 + terrainData[i][j];
            punkty[ptr++] = min + i *sampleSize;
            punkty[ptr++] = min + j *sampleSize;
            punkty[ptr++] = 0.9 + terrainData[i + 1][j];
            punkty[ptr++] = min + i *sampleSize +sampleSize;
        }

        for (i = 0; i < samples; i++) {
            punkty[ptr++] = min + i *sampleSize;
            punkty[ptr++] = 0.9 + terrainData[j][i];
            punkty[ptr++] = min + j *sampleSize;
            punkty[ptr++] = min + i *sampleSize +sampleSize;
            punkty[ptr++] = 0.9 + terrainData[j][i + 1];
            punkty[ptr++] = min + j *sampleSize;
        }
    }
    slines.setMaterial(0.5, 0.5, 0.5);
    slines.init(punkty, ptr, RenderItem::V, GL_LINES);
    delete[] punkty;
    
    //////////////////////
    
    int ui = 0;
    for (int uu = 0; uu < patches; uu++)
        for (int yy = 0; yy < patches; yy++)
            if(this->uniqueTex[uu*patches+yy]) ui++;
    
    punkty = new float[static_cast<std::size_t>(patches) * samples * 24];
    ptr = 0;
    i = 0;
    
    for (int uu = 0; uu < patches; uu++)
        for (int yy = 0; yy < patches; yy++){
            if(!this->uniqueTex[yy*patches+uu]) continue;
            
            for (i = 0; i < patchRes; i++) {
                 punkty[ptr++] = min + uu*patchSize;
                 punkty[ptr++] = 0.95 + terrainData[yy*patchRes+i][uu*patchRes];
                 punkty[ptr++] = min + yy*patchSize + i *sampleSize;
                 punkty[ptr++] = min + uu*patchSize;
                 punkty[ptr++] = 0.95 + terrainData[yy*patchRes+i+1][uu*patchRes];
                 punkty[ptr++] = min + yy*patchSize + i *sampleSize +sampleSize;
            }
            for (i = 0; i < patchRes; i++) {
                 punkty[ptr++] = min + uu*patchSize + i *sampleSize;
                 punkty[ptr++] = 0.95 + terrainData[yy*patchRes][uu*patchRes+i];
                 punkty[ptr++] = min + yy*patchSize;
                 punkty[ptr++] = min + uu*patchSize + i *sampleSize +sampleSize;
                 punkty[ptr++] = 0.95 + terrainData[yy*patchRes][uu*patchRes+i+1];
                 punkty[ptr++] = min + yy*patchSize;
            }
            for (i = 0; i < patchRes; i++) {
                 punkty[ptr++] = min + uu*patchSize+patchSize;
                 punkty[ptr++] = 0.95 + terrainData[yy*patchRes+i][(uu+1)*patchRes];
                 punkty[ptr++] = min + yy*patchSize + i *sampleSize;
                 punkty[ptr++] = min + uu*patchSize+patchSize;
                 punkty[ptr++] = 0.95 + terrainData[yy*patchRes+i+1][(uu+1)*patchRes];
                 punkty[ptr++] = min + yy*patchSize + i *sampleSize +sampleSize;
            }
            for (i = 0; i < patchRes; i++) {
                 punkty[ptr++] = min + uu*patchSize + i *sampleSize;
                 punkty[ptr++] = 0.95 + terrainData[(yy+1)*patchRes][uu*patchRes+i];
                 punkty[ptr++] = min + yy*patchSize+patchSize;
                 punkty[ptr++] = min + uu*patchSize + i *sampleSize +sampleSize;
                 punkty[ptr++] = 0.95 + terrainData[(yy+1)*patchRes][uu*patchRes+i+1];
                 punkty[ptr++] = min + yy*patchSize+patchSize;
            }
        }
    
    ulines.setMaterial(0.8, 0.8, 0.8);
    ulines.init(punkty, ptr, RenderItem::V, GL_LINES);
    delete[] punkty;

    //////////////////////
    
    ui = 0;
    for (int uu = 0; uu < patches; uu++)
        for (int yy = 0; yy < patches; yy++)
            if(this->texLocked[uu*patches+yy]) ui++;
    
    punkty = new float[static_cast<std::size_t>(patches) * samples * 24];
    ptr = 0;
    i = 0;
    
    for (int uu = 0; uu < patches; uu++)
        for (int yy = 0; yy < patches; yy++){
            if(!this->texLocked[yy*patches+uu]) continue;
            
            for (i = 0; i < patchRes; i++) {
                 punkty[ptr++] = min + uu*patchSize;
                 punkty[ptr++] = 0.99 + terrainData[yy*patchRes+i][uu*patchRes];
                 punkty[ptr++] = min + yy*patchSize + i *sampleSize;
                 punkty[ptr++] = min + uu*patchSize;
                 punkty[ptr++] = 0.99 + terrainData[yy*patchRes+i+1][uu*patchRes];
                 punkty[ptr++] = min + yy*patchSize + i *sampleSize +sampleSize;
            }
            for (i = 0; i < patchRes; i++) {
                 punkty[ptr++] = min + uu*patchSize + i *sampleSize;
                 punkty[ptr++] = 0.99 + terrainData[yy*patchRes][uu*patchRes+i];
                 punkty[ptr++] = min + yy*patchSize;
                 punkty[ptr++] = min + uu*patchSize + i *sampleSize +sampleSize;
                 punkty[ptr++] = 0.99 + terrainData[yy*patchRes][uu*patchRes+i+1];
                 punkty[ptr++] = min + yy*patchSize;
            }
            for (i = 0; i < patchRes; i++) {
                 punkty[ptr++] = min + uu*patchSize+patchSize;
                 punkty[ptr++] = 0.99 + terrainData[yy*patchRes+i][(uu+1)*patchRes];
                 punkty[ptr++] = min + yy*patchSize + i *sampleSize;
                 punkty[ptr++] = min + uu*patchSize+patchSize;
                 punkty[ptr++] = 0.99 + terrainData[yy*patchRes+i+1][(uu+1)*patchRes];
                 punkty[ptr++] = min + yy*patchSize + i *sampleSize +sampleSize;
            }
            for (i = 0; i < patchRes; i++) {
                 punkty[ptr++] = min + uu*patchSize + i *sampleSize;
                 punkty[ptr++] = 0.99 + terrainData[(yy+1)*patchRes][uu*patchRes+i];
                 punkty[ptr++] = min + yy*patchSize+patchSize;
                 punkty[ptr++] = min + uu*patchSize + i *sampleSize +sampleSize;
                 punkty[ptr++] = 0.99 + terrainData[(yy+1)*patchRes][uu*patchRes+i+1];
                 punkty[ptr++] = min + yy*patchSize+patchSize;
            }
        }
    
    lockedlines.setMaterial(0.1, 0.1, 0.1);
    lockedlines.init(punkty, ptr, RenderItem::V, GL_LINES);
    delete[] punkty;
    
    //////////////////////
    
    ui = 0;
    for (int uu = 0; uu < patches; uu++)
        for (int yy = 0; yy < patches; yy++)
            if(this->selectedPatchs[uu*patches+yy]) ui++;
    
    punkty = new float[static_cast<std::size_t>(patches) * samples * 24];
    ptr = 0;
    i = 0;
    
    for (int uu = 0; uu < patches; uu++)
        for (int yy = 0; yy < patches; yy++){
            if(!this->selectedPatchs[yy*patches+uu]) continue;
            
            for (i = 0; i < patchRes; i++) {
                 punkty[ptr++] = min + uu*patchSize;
                 punkty[ptr++] = 0.99 + terrainData[yy*patchRes+i][uu*patchRes];
                 punkty[ptr++] = min + yy*patchSize + i *sampleSize;
                 punkty[ptr++] = min + uu*patchSize;
                 punkty[ptr++] = 0.99 + terrainData[yy*patchRes+i+1][uu*patchRes];
                 punkty[ptr++] = min + yy*patchSize + i *sampleSize +sampleSize;
            }
            for (i = 0; i < patchRes; i++) {
                 punkty[ptr++] = min + uu*patchSize + i *sampleSize;
                 punkty[ptr++] = 0.99 + terrainData[yy*patchRes][uu*patchRes+i];
                 punkty[ptr++] = min + yy*patchSize;
                 punkty[ptr++] = min + uu*patchSize + i *sampleSize +sampleSize;
                 punkty[ptr++] = 0.99 + terrainData[yy*patchRes][uu*patchRes+i+1];
                 punkty[ptr++] = min + yy*patchSize;
            }
            for (i = 0; i < patchRes; i++) {
                 punkty[ptr++] = min + uu*patchSize+patchSize;
                 punkty[ptr++] = 0.99 + terrainData[yy*patchRes+i][(uu+1)*patchRes];
                 punkty[ptr++] = min + yy*patchSize + i *sampleSize;
                 punkty[ptr++] = min + uu*patchSize+patchSize;
                 punkty[ptr++] = 0.99 + terrainData[yy*patchRes+i+1][(uu+1)*patchRes];
                 punkty[ptr++] = min + yy*patchSize + i *sampleSize +sampleSize;
            }
            for (i = 0; i < patchRes; i++) {
                 punkty[ptr++] = min + uu*patchSize + i *sampleSize;
                 punkty[ptr++] = 0.99 + terrainData[(yy+1)*patchRes][uu*patchRes+i];
                 punkty[ptr++] = min + yy*patchSize+patchSize;
                 punkty[ptr++] = min + uu*patchSize + i *sampleSize +sampleSize;
                 punkty[ptr++] = 0.99 + terrainData[(yy+1)*patchRes][uu*patchRes+i+1];
                 punkty[ptr++] = min + yy*patchSize+patchSize;
            }
        }
    
    selectedlines.setMaterial(0.0, 0.0, 0.8);
    selectedlines.init(punkty, ptr, RenderItem::V, GL_LINES);
    delete[] punkty;
}

void Terrain::vertexInit() {
    // this.vertexData[0] = new Array();
    int samples = *tfile->nsamples;
    int sampleSize = *tfile->sampleSize;

    vertexData = new Vector3f*[samples+1];
    for (int i = 0; i < samples+1; i++)
        vertexData[i] = new Vector3f[samples+1];

    //qDebug() << "min" << sampleSize << samples;
    for (int j = 0, jj = 0; jj < samples; j += sampleSize, jj++) {
        for (int i = 0, ii = 0; ii < samples; i += sampleSize, ii++) {
            vertexData[jj][ii].set(j, terrainData[ii][jj], i);
            vertexData[jj][ii + 1].set(j, terrainData[ii + 1][jj], (i + sampleSize));
            vertexData[(jj + 1)][ii + 1].set((j + sampleSize), terrainData[(ii + 1)][jj + 1], (i + sampleSize));
            vertexData[(jj + 1)][ii].set((j + sampleSize), terrainData[(ii)][jj + 1], i);
        }
    }
}

void Terrain::normalInit() {
    int samples = *tfile->nsamples;
    int sampleSize = *tfile->sampleSize;
    normalData = new Vector3f*[samples+1];
    for (int i = 0; i < samples+1; i++)
        normalData[i] = new Vector3f[samples+1];
    Vector3f U, V, O;
    for (int jj = 0; jj < samples; jj++) {
        for (int ii = 0; ii < samples; ii++) {
            U.setFromSub( vertexData[jj][ii], vertexData[jj + 1][ii]);
            V.setFromSub( vertexData[jj][ii], vertexData[jj][ii + 1]);
            O.setFromCross(V, U);
            normalData[jj][ii].add( O );
            normalData[jj + 1][ii].add( O );
            normalData[jj][ii + 1].add( O );
            U.setFromSub( vertexData[jj + 1][ii + 1], vertexData[jj + 1][ii]);
            V.setFromSub( vertexData[jj + 1][ii + 1], vertexData[jj][ii + 1]);
            O.setFromCross(U, V);
            normalData[jj + 1][ii + 1].add( O );
            normalData[jj + 1][ii].add( O );
            normalData[jj][ii + 1].add( O );
        }
    }

    for (int jj = 0; jj < samples+1; jj++) {
        for (int ii = 0; ii < samples+1; ii++) {
            normalData[jj][ii].normalize();
        }
    }
};
/*
void Terrain::oglInit() {
    if(!VAO->isCreated()){
       VAO->create();
       VBO->create();
    }
    QOpenGLVertexArrayObject::Binder vaoBinder(VAO);
    VBO->bind();
    VBO->allocate(256 * 16 * 16 * 6 * 5 * sizeof (GLfloat));
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glEnableVertexAttribArray(0);
    f->glEnableVertexAttribArray(1);
    f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), 0);
    f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), reinterpret_cast<void *> (3 * sizeof (GLfloat)));
    
    int ilosc = 16 * 16;
    int suma;
    float * punkty = new float[16 * 16 * 30];
    //  var punkty = Terrain.punkty;
    for (int uu = 0; uu < 16; uu++) {
        for (int yy = 0; yy < 16; yy++) {
            int ptr = 0;

            for (int ii = 0; ii < 16; ii++) {
                for (int jj = 0; jj < 16; jj++) {
                    if (jestF) {
                        suma = ((fData[yy * 16 + ii][uu * 16 + jj]) & 0x04);
                        suma += ((fData[yy * 16 + ii][uu * 16 + jj + 1]) & 0x04);
                        suma += ((fData[yy * 16 + ii + 1][uu * 16 + jj + 1]) & 0x04);
                        suma += ((fData[yy * 16 + ii + 1][uu * 16 + jj]) & 0x04);
                    }

                    if (!jestF || (suma < 4)) {
                        punkty[ptr++] = vertexData[(uu * 16 + jj)][yy * 16 + ii].x;
                        punkty[ptr++] = vertexData[(uu * 16 + jj)][yy * 16 + ii].y;
                        punkty[ptr++] = vertexData[(uu * 16 + jj)][yy * 16 + ii].z;
                        punkty[ptr++] = jj * tfile->tdata[(yy * 16 + uu)*13 + 3 + 6] + ii * tfile->tdata[(yy * 16 + uu)*13 + 4 + 6] + tfile->tdata[(yy * 16 + uu)*13 + 1 + 6];
                        punkty[ptr++] = jj * tfile->tdata[(yy * 16 + uu)*13 + 5 + 6] + ii * tfile->tdata[(yy * 16 + uu)*13 + 6 + 6] + tfile->tdata[(yy * 16 + uu)*13 + 2 + 6];

                        punkty[ptr++] = vertexData[(uu * 16 + jj)][yy * 16 + ii + 1].x;
                        punkty[ptr++] = vertexData[(uu * 16 + jj)][yy * 16 + ii + 1].y;
                        punkty[ptr++] = vertexData[(uu * 16 + jj)][yy * 16 + ii + 1].z;
                        punkty[ptr++] = (jj) * tfile->tdata[(yy * 16 + uu)*13 + 3 + 6] + (ii + 1) * tfile->tdata[(yy * 16 + uu)*13 + 4 + 6] + tfile->tdata[(yy * 16 + uu)*13 + 1 + 6];
                        punkty[ptr++] = (jj) * tfile->tdata[(yy * 16 + uu)*13 + 5 + 6] + (ii + 1) * tfile->tdata[(yy * 16 + uu)*13 + 6 + 6] + tfile->tdata[(yy * 16 + uu)*13 + 2 + 6];

                        punkty[ptr++] = vertexData[(uu * 16 + jj + 1)][yy * 16 + ii + 1].x;
                        punkty[ptr++] = vertexData[(uu * 16 + jj + 1)][yy * 16 + ii + 1].y;
                        punkty[ptr++] = vertexData[(uu * 16 + jj + 1)][yy * 16 + ii + 1].z;
                        punkty[ptr++] = (jj + 1) * tfile->tdata[(yy * 16 + uu)*13 + 3 + 6] + (ii + 1) * tfile->tdata[(yy * 16 + uu)*13 + 4 + 6] + tfile->tdata[(yy * 16 + uu)*13 + 1 + 6];
                        punkty[ptr++] = (jj + 1) * tfile->tdata[(yy * 16 + uu)*13 + 5 + 6] + (ii + 1) * tfile->tdata[(yy * 16 + uu)*13 + 6 + 6] + tfile->tdata[(yy * 16 + uu)*13 + 2 + 6];

                        punkty[ptr++] = vertexData[(uu * 16 + jj)][yy * 16 + ii].x;
                        punkty[ptr++] = vertexData[(uu * 16 + jj)][yy * 16 + ii].y;
                        punkty[ptr++] = vertexData[(uu * 16 + jj)][yy * 16 + ii].z;
                        punkty[ptr++] = jj * tfile->tdata[(yy * 16 + uu)*13 + 3 + 6] + ii * tfile->tdata[(yy * 16 + uu)*13 + 4 + 6] + tfile->tdata[(yy * 16 + uu)*13 + 1 + 6];
                        punkty[ptr++] = jj * tfile->tdata[(yy * 16 + uu)*13 + 5 + 6] + ii * tfile->tdata[(yy * 16 + uu)*13 + 6 + 6] + tfile->tdata[(yy * 16 + uu)*13 + 2 + 6];

                        punkty[ptr++] = vertexData[(uu * 16 + jj + 1)][yy * 16 + ii + 1].x;
                        punkty[ptr++] = vertexData[(uu * 16 + jj + 1)][yy * 16 + ii + 1].y;
                        punkty[ptr++] = vertexData[(uu * 16 + jj + 1)][yy * 16 + ii + 1].z;
                        punkty[ptr++] = (jj + 1) * tfile->tdata[(yy * 16 + uu)*13 + 3 + 6] + (ii + 1) * tfile->tdata[(yy * 16 + uu)*13 + 4 + 6] + tfile->tdata[(yy * 16 + uu)*13 + 1 + 6];
                        punkty[ptr++] = (jj + 1) * tfile->tdata[(yy * 16 + uu)*13 + 5 + 6] + (ii + 1) * tfile->tdata[(yy * 16 + uu)*13 + 6 + 6] + tfile->tdata[(yy * 16 + uu)*13 + 2 + 6];

                        punkty[ptr++] = vertexData[(uu * 16 + jj + 1)][yy * 16 + ii].x;
                        punkty[ptr++] = vertexData[(uu * 16 + jj + 1)][yy * 16 + ii].y;
                        punkty[ptr++] = vertexData[(uu * 16 + jj + 1)][yy * 16 + ii].z;
                        punkty[ptr++] = (jj + 1) * tfile->tdata[(yy * 16 + uu)*13 + 3 + 6] + (ii) * tfile->tdata[(yy * 16 + uu)*13 + 4 + 6] + tfile->tdata[(yy * 16 + uu)*13 + 1 + 6];
                        punkty[ptr++] = (jj + 1) * tfile->tdata[(yy * 16 + uu)*13 + 5 + 6] + (ii) * tfile->tdata[(yy * 16 + uu)*13 + 6 + 6] + tfile->tdata[(yy * 16 + uu)*13 + 2 + 6];
                    }
                }
            }
            VBO->write((uu * 16 + yy) * 16 * 16 * 6 * 5 * sizeof (GLfloat), punkty, 16 * 16 * 6 * 5 * sizeof (GLfloat));
        }
    }

    VBO->release();
    delete[] punkty;

    initBlob();
    //for (int i = 0; i < 257; i++)
    //    delete normalData[i];
    //delete normalData;
    for (int i = 0; i < 257; i++)
        delete[] vertexData[i];
    delete[] vertexData;
}*/

void Terrain::oglInit() {
    if(!VAO->isCreated()){
       VAO->create();
       VBO->create();
    }
    QOpenGLVertexArrayObject::Binder vaoBinder(VAO);
    VBO->bind();
    VBO->allocate(static_cast<int>(gridLayout.terrainVboBytes));
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glEnableVertexAttribArray(0);
    f->glEnableVertexAttribArray(1);
    f->glEnableVertexAttribArray(2);
    f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof (GLfloat), 0);
    f->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof (GLfloat), reinterpret_cast<void *> (3 * sizeof (GLfloat)));
    f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof (GLfloat), reinterpret_cast<void *> (6 * sizeof (GLfloat)));
    
    //int ilosc = 16 * 16;
    //int suma;
    const std::size_t patchFloatCount = gridLayout.patchVboBytes / sizeof(GLfloat);
    float * punkty = new float[patchFloatCount]();
    //  var punkty = Terrain.punkty;
    int samples = *tfile->nsamples;
    int patches = tfile->patchsetNpatches;
    int patchRes = samples/patches;
    // Patch texture matrices consume raw sample coordinates (0..patchRes),
    // matching the interpretation used by Open Rails.
    constexpr float texRes = 1.0f;
    
    for (int uu = 0; uu < patches; uu++) {
        for (int yy = 0; yy < patches; yy++) {
            int ptr = 0;
            std::fill(punkty, punkty + patchFloatCount, 0.0f);
            bool fi0j0 = true, fi1j0 = true, fi0j1 = true, fi1j1 = true;

            for (int ii = 0; ii < patchRes; ii++) {
                for (int jj = 0; jj < patchRes; jj++) {
                    if (jestF) {
                        fi0j0 = !(((fData[yy * patchRes + ii][uu * patchRes + jj]) & 0x04) >> 2);
                        fi0j1 = !(((fData[yy * patchRes + ii][uu * patchRes + jj + 1]) & 0x04) >> 2);
                        fi1j1 = !(((fData[yy * patchRes + ii + 1][uu * patchRes + jj + 1]) &0x04) >> 2);
                        fi1j0 = !(((fData[yy * patchRes + ii + 1][uu * patchRes + jj]) & 0x04) >> 2);
                    }
                    /*if (jestF) {
                        suma = ((fData[yy * 16 + ii][uu * 16 + jj]) & 0x04);
                        suma += ((fData[yy * 16 + ii][uu * 16 + jj + 1]) & 0x04);
                        suma += ((fData[yy * 16 + ii + 1][uu * 16 + jj + 1]) & 0x04);
                        suma += ((fData[yy * 16 + ii + 1][uu * 16 + jj]) & 0x04);
                    }*/

                    //if (!jestF || (suma < 4)) {
                        if(((ii+jj) % 2 == 0)){
                            if(fi0j0 && fi1j0 && fi1j1){
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii].x;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii].y;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii].z;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii].x;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii].y;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii].z;
                            punkty[ptr++] = texRes * (jj * tfile->tdata[(yy * patches + uu)*13 + 3 + 6] + ii * tfile->tdata[(yy * patches + uu)*13 + 4 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 1 + 6];
                            punkty[ptr++] = texRes * (jj * tfile->tdata[(yy * patches + uu)*13 + 5 + 6] + ii * tfile->tdata[(yy * patches + uu)*13 + 6 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 2 + 6];

                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii + 1].x;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii + 1].y;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii + 1].z;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii + 1].x;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii + 1].y;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii + 1].z;
                            punkty[ptr++] = texRes * ((jj) * tfile->tdata[(yy * patches + uu)*13 + 3 + 6] + (ii + 1) * tfile->tdata[(yy * patches + uu)*13 + 4 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 1 + 6];
                            punkty[ptr++] = texRes * ((jj) * tfile->tdata[(yy * patches + uu)*13 + 5 + 6] + (ii + 1) * tfile->tdata[(yy * patches + uu)*13 + 6 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 2 + 6];

                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].x;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].y;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].z;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].x;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].y;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].z;
                            punkty[ptr++] = texRes * ((jj + 1) * tfile->tdata[(yy * patches + uu)*13 + 3 + 6] + (ii + 1) * tfile->tdata[(yy * patches + uu)*13 + 4 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 1 + 6];
                            punkty[ptr++] = texRes * ((jj + 1) * tfile->tdata[(yy * patches + uu)*13 + 5 + 6] + (ii + 1) * tfile->tdata[(yy * patches + uu)*13 + 6 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 2 + 6];
                            }
                            ///////////////////////////////////////////////////////////
                            if(fi0j0 && fi0j1 && fi1j1){
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii].x;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii].y;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii].z;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii].x;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii].y;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii].z;
                            punkty[ptr++] = texRes * (jj * tfile->tdata[(yy * patches + uu)*13 + 3 + 6] + ii * tfile->tdata[(yy * patches + uu)*13 + 4 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 1 + 6];
                            punkty[ptr++] = texRes * (jj * tfile->tdata[(yy * patches + uu)*13 + 5 + 6] + ii * tfile->tdata[(yy * patches + uu)*13 + 6 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 2 + 6];

                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].x;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].y;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].z;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].x;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].y;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].z;
                            punkty[ptr++] = texRes * ((jj + 1) * tfile->tdata[(yy * patches + uu)*13 + 3 + 6] + (ii + 1) * tfile->tdata[(yy * patches + uu)*13 + 4 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 1 + 6];
                            punkty[ptr++] = texRes * ((jj + 1) * tfile->tdata[(yy * patches + uu)*13 + 5 + 6] + (ii + 1) * tfile->tdata[(yy * patches + uu)*13 + 6 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 2 + 6];

                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii].x;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii].y;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii].z;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii].x;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii].y;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii].z;
                            punkty[ptr++] = texRes * ((jj + 1) * tfile->tdata[(yy * patches + uu)*13 + 3 + 6] + (ii) * tfile->tdata[(yy * patches + uu)*13 + 4 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 1 + 6];
                            punkty[ptr++] = texRes * ((jj + 1) * tfile->tdata[(yy * patches + uu)*13 + 5 + 6] + (ii) * tfile->tdata[(yy * patches + uu)*13 + 6 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 2 + 6];
                            }
                        }
                        if(((ii+jj) % 2 == 1)){
                            if(fi0j1 && fi1j1 && fi1j0){
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii + 1].x;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii + 1].y;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii + 1].z;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii + 1].x;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii + 1].y;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii + 1].z;
                            punkty[ptr++] = texRes * ((jj) * tfile->tdata[(yy * patches + uu)*13 + 3 + 6] + (ii + 1) * tfile->tdata[(yy * patches + uu)*13 + 4 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 1 + 6];
                            punkty[ptr++] = texRes * ((jj) * tfile->tdata[(yy * patches + uu)*13 + 5 + 6] + (ii + 1) * tfile->tdata[(yy * patches + uu)*13 + 6 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 2 + 6];
                            
                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].x;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].y;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].z;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].x;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].y;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii + 1].z;
                            punkty[ptr++] = texRes * ((jj + 1) * tfile->tdata[(yy * patches + uu)*13 + 3 + 6] + (ii + 1) * tfile->tdata[(yy * patches + uu)*13 + 4 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 1 + 6];
                            punkty[ptr++] = texRes * ((jj + 1) * tfile->tdata[(yy * patches + uu)*13 + 5 + 6] + (ii + 1) * tfile->tdata[(yy * patches + uu)*13 + 6 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 2 + 6];

                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii].x;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii].y;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii].z;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii].x;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii].y;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii].z;
                            punkty[ptr++] = texRes * ((jj + 1) * tfile->tdata[(yy * patches + uu)*13 + 3 + 6] + (ii) * tfile->tdata[(yy * patches + uu)*13 + 4 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 1 + 6];
                            punkty[ptr++] = texRes * ((jj + 1) * tfile->tdata[(yy * patches + uu)*13 + 5 + 6] + (ii) * tfile->tdata[(yy * patches + uu)*13 + 6 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 2 + 6];
                            }
                            ///////////////////////////////////////////////////////////
                            if(fi0j0 && fi0j1 && fi1j0){
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii].x;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii].y;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii].z;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii].x;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii].y;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii].z;
                            punkty[ptr++] = texRes * (jj * tfile->tdata[(yy * patches + uu)*13 + 3 + 6] + ii * tfile->tdata[(yy * patches + uu)*13 + 4 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 1 + 6];
                            punkty[ptr++] = texRes * (jj * tfile->tdata[(yy * patches + uu)*13 + 5 + 6] + ii * tfile->tdata[(yy * patches + uu)*13 + 6 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 2 + 6];

                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii + 1].x;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii + 1].y;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj)][yy * patchRes + ii + 1].z;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii + 1].x;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii + 1].y;
                            punkty[ptr++] = normalData[(uu * patchRes + jj)][yy * patchRes + ii + 1].z;
                            punkty[ptr++] = texRes * ((jj) * tfile->tdata[(yy * patches + uu)*13 + 3 + 6] + (ii + 1) * tfile->tdata[(yy * patches + uu)*13 + 4 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 1 + 6];
                            punkty[ptr++] = texRes * ((jj) * tfile->tdata[(yy * patches + uu)*13 + 5 + 6] + (ii + 1) * tfile->tdata[(yy * patches + uu)*13 + 6 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 2 + 6];

                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii].x;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii].y;
                            punkty[ptr++] = vertexData[(uu * patchRes + jj + 1)][yy * patchRes + ii].z;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii].x;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii].y;
                            punkty[ptr++] = normalData[(uu * patchRes + jj + 1)][yy * patchRes + ii].z;
                            punkty[ptr++] = texRes * ((jj + 1) * tfile->tdata[(yy * patches + uu)*13 + 3 + 6] + (ii) * tfile->tdata[(yy * patches + uu)*13 + 4 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 1 + 6];
                            punkty[ptr++] = texRes * ((jj + 1) * tfile->tdata[(yy * patches + uu)*13 + 5 + 6] + (ii) * tfile->tdata[(yy * patches + uu)*13 + 6 + 6]) + tfile->tdata[(yy * patches + uu)*13 + 2 + 6];
                            }
                        }
                    //}
                }
            }
            /*for(var jj = 0; jj<16; jj++){
                for(var ii = 0; ii<16; ii++){
                    punkty.put(normalData[uu*16+jj][yy*16+ii].toFloat());
                    punkty.put(normalData[uu*16+jj][yy*16+ii+1].toFloat());
                    punkty.put(normalData[uu*16+jj+1][yy*16+ii+1].toFloat());

                    punkty.put(normalData[uu*16+jj][yy*16+ii].toFloat());
                    punkty.put(normalData[uu*16+jj+1][yy*16+ii+1].toFloat());
                    punkty.put(normalData[uu*16+jj+1][yy*16+ii].toFloat());
                }
            }*/

            //QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
            //if(!VBO[0]->isCreated()){
                //VAO[uu * 16 + yy]->create();
           //     VBO[0]->create();
            //}
            
            //QOpenGLVertexArrayObject::Binder vaoBinder(VAO[uu * 16 + yy]);
            
            //VBO[0]->bind();
            //VBO[0]->
            const int patchBytes = static_cast<int>(gridLayout.patchVboBytes);
            VBO->write((uu * patches + yy) * patchBytes, punkty, patchBytes);
            //VBO[0]->allocate(punkty, 16 * 16 * 6 * 5 * sizeof (GLfloat));
            //f->glEnableVertexAttribArray(0);
            //f->glEnableVertexAttribArray(1);
            //f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), 0);
            //f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), reinterpret_cast<void *> (3 * sizeof (GLfloat)));
            //VBO[0]->release();
        }
    }

    VBO->release();
    delete[] punkty;

    initBlob();
    //for (int i = 0; i < 257; i++)
    //    delete normalData[i];
    //delete normalData;

    for (int i = 0; i < samples+1; i++){
        delete[] vertexData[i];
        delete[] normalData[i];
    }
    delete[] vertexData;
    delete[] normalData;
}

void Terrain::initBlob(){
    
    GLUU* gluu = GLUU::get();
    float alpha = -0.01;
    int samples = *tfile->nsamples;
    int patches = tfile->patchsetNpatches;
    int patchRes = samples/patches;
    float *punkty = new float[gridLayout.storedCellCount * 54];
    int ptr = 0;
    float step = 1.0/samples;
    for (int jj = 0; jj < samples; jj++) {
        for (int ii = 0; ii < samples; ii++) {
            punkty[ptr++] = vertexData[jj][ii].x;
            punkty[ptr++] = vertexData[jj][ii].y+0.00;
            punkty[ptr++] = vertexData[jj][ii].z;
            punkty[ptr++] = normalData[jj][ii].x;
            punkty[ptr++] = normalData[jj][ii].y;
            punkty[ptr++] = normalData[jj][ii].z;
            punkty[ptr++] = (jj)*step;
            punkty[ptr++] = (ii)*step;
            punkty[ptr++] = alpha;
            punkty[ptr++] = vertexData[jj][ii+1].x;
            punkty[ptr++] = vertexData[jj][ii+1].y+0.00;
            punkty[ptr++] = vertexData[jj][ii+1].z;
            punkty[ptr++] = normalData[jj][ii+1].x;
            punkty[ptr++] = normalData[jj][ii+1].y;
            punkty[ptr++] = normalData[jj][ii+1].z;
            punkty[ptr++] = (jj)*step;
            punkty[ptr++] = (ii+1)*step;
            punkty[ptr++] = alpha;
            punkty[ptr++] = vertexData[jj+1][ii+1].x;
            punkty[ptr++] = vertexData[jj+1][ii+1].y+0.00;
            punkty[ptr++] = vertexData[jj+1][ii+1].z;
            punkty[ptr++] = normalData[jj+1][ii+1].x;
            punkty[ptr++] = normalData[jj+1][ii+1].y;
            punkty[ptr++] = normalData[jj+1][ii+1].z;
            punkty[ptr++] = (jj+1)*step;
            punkty[ptr++] = (ii+1)*step;
            punkty[ptr++] = alpha;
            punkty[ptr++] = vertexData[jj][ii].x;
            punkty[ptr++] = vertexData[jj][ii].y+0.00;
            punkty[ptr++] = vertexData[jj][ii].z;
            punkty[ptr++] = normalData[jj][ii].x;
            punkty[ptr++] = normalData[jj][ii].y;
            punkty[ptr++] = normalData[jj][ii].z;
            punkty[ptr++] = (jj)*step;
            punkty[ptr++] = (ii)*step;
            punkty[ptr++] = alpha;
            punkty[ptr++] = vertexData[jj+1][ii+1].x;
            punkty[ptr++] = vertexData[jj+1][ii+1].y+0.00;
            punkty[ptr++] = vertexData[jj+1][ii+1].z;
            punkty[ptr++] = normalData[jj+1][ii+1].x;
            punkty[ptr++] = normalData[jj+1][ii+1].y;
            punkty[ptr++] = normalData[jj+1][ii+1].z;
            punkty[ptr++] = (jj+1)*step;
            punkty[ptr++] = (ii+1)*step;
            punkty[ptr++] = alpha;
            punkty[ptr++] = vertexData[jj+1][ii].x;
            punkty[ptr++] = vertexData[jj+1][ii].y+0.00;
            punkty[ptr++] = vertexData[jj+1][ii].z;
            punkty[ptr++] = normalData[jj+1][ii].x;
            punkty[ptr++] = normalData[jj+1][ii].y;
            punkty[ptr++] = normalData[jj+1][ii].z;
            punkty[ptr++] = (jj+1)*step;
            punkty[ptr++] = (ii)*step;
            punkty[ptr++] = alpha;
        }
    }
    QString* path = new QString;
    int X, Y;
    this->getLowCornerTileXY(X, Y);
    *path += QString::number((int)(X)*10000+(int)(Y))+".:maptex";
    //qDebug() << *path;
    terrainBlob.setMaterial(path);
    terrainBlob.init(punkty, ptr, RenderItem::VNTA, GL_TRIANGLES);
    delete[] punkty;
}

bool Terrain::readRAW(QString fSfile) {
    fSfile.replace("//", "/");
    //qDebug() << fSfile;
    //qDebug() << "Wczytam teren RAW: " << fSfile;
    QFile file(fSfile);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    FileBuffer* data = ReadFile::readRAW(&file);
    if (!validatePayload(data, sizeof(quint16), "height RAW")) {
        delete data;
        return false;
    }
    readRAW(data);
    delete data;
    return true;
}

void Terrain::readRAW(FileBuffer* data) {
    if (!validatePayload(data, sizeof(quint16), "height RAW"))
        return;
    releaseHeightData();
    int samples = gridLayout.sampleCount;
    //qDebug() << data->length;
    terrainData = new float*[samples+1];
    terrainDataRows = samples + 1;
    //int u = 0;
    for (int i = 0; i < samples+1; i++) {
        terrainData[i] = new float[samples+1];
        for (int j = 0; j < samples+1; j++) {
            if (i == samples && j == samples) {
                terrainData[i][j] = terrainData[(i - 1)][j - 1];
            } else if (i == samples) {
                terrainData[i][j] = terrainData[(i - 1)][j];
            } else if (j == samples) {
                terrainData[i][j] = terrainData[i][j - 1];
            } else {
                terrainData[i][j] = tfile->floor + tfile->scale * (data->get() + 256 * data->get());
                //terrainData[i][j] = tfile->floor + tfile->scale * (data->data[u++] + 256*data->data[u++]);
            }
        }
    }
    initializePatchBounds();
}

void Terrain::readRAWFloat(FileBuffer* data) {
    if (!validatePayload(data, sizeof(float), "float height RAW"))
        return;
    releaseHeightData();
    int samples = gridLayout.sampleCount;
    //qDebug() << data->length;
    terrainData = new float*[samples+1];
    terrainDataRows = samples + 1;
    //int u = 0;
    for (int i = 0; i < samples+1; i++) {
        terrainData[i] = new float[samples+1];
        for (int j = 0; j < samples+1; j++) {
            if (i == samples && j == samples) {
                terrainData[i][j] = terrainData[(i - 1)][j - 1];
            } else if (i == samples) {
                terrainData[i][j] = terrainData[(i - 1)][j];
            } else if (j == samples) {
                terrainData[i][j] = terrainData[i][j - 1];
            } else {
                terrainData[i][j] = data->getFloat();
            }
        }
    }
    initializePatchBounds();
    for (auto &edge : adjacentEdges)
        edge.dirty = true;
    if (loaded) {
        invalidateSamples(0, 0, samples, samples,
                          TerrainDirtyHeight | TerrainDirtyNormals);
        if (Game::terrainLib != nullptr)
            Game::terrainLib->terrainAvailabilityChanged(this);
    }
}

void Terrain::fillHeightMap(float* data){
    if (!editable)
        return;
    int samples = *tfile->nsamples + 1;
    for (int i = 0; i < samples; i++)
        for (int j = 0; j < samples; j++) {
            terrainData[i][j] = data[i*samples+j];
        }
    invalidateSamples(0, 0, gridLayout.sampleCount, gridLayout.sampleCount,
                      TerrainDirtyHeight | TerrainDirtyNormals);
}

void Terrain::save() {
    if (!editable)
        return;
    refreshPatchBounds(true);
    QString path = Game::root + "/routes/" + Game::route + "/" + TileDir[(int)lowTile] + "/";
    QString filename = name;
    if(this->tfile->sampleYbuffer == NULL)
        this->tfile->sampleYbuffer = new QString(filename + "_y.raw");
    saveRAW(path + *this->tfile->sampleYbuffer );
    if(jestF && modifiedF){
        if(this->tfile->sampleFbuffer == NULL)
            this->tfile->sampleFbuffer = new QString(filename + "_f.raw");
        saveF(path + *this->tfile->sampleFbuffer);
    }
    qDebug() << "writing t start";
    this->tfile->save(path + filename + ".t");
    qDebug() << "writing t end";
    int patches = tfile->patchsetNpatches;
    for (int u = 0; u < patches; u++)
        for (int y = 0; y < patches; y++) {
            if (this->texModified[y * patches + u] == false) continue;
            //QString name = this->getTileName(mojex, -mojez) + "_" + QString::number(y) + "_" + QString::number(u) + ".ace";
            TexLib::save("ace", TexLib::mtex[texid[y * patches + u]]->pathid, texid[y * patches + u]);
            this->texModified[y * patches + u] = false;
        }
}

void Terrain::saveRAW(QString name) {
    name.replace("//", "/");
    QFile *file = new QFile(name);
    qDebug() << "zapis " << name;
    if (!file->open(QIODevice::WriteOnly))
        return;
    qDebug() << "w";
    QDataStream write(file);
    write.setByteOrder(QDataStream::LittleEndian);
    saveRAW(write);
    file->close();
    return;
}

void Terrain::saveRAW(QDataStream &write){
    int samples = *tfile->nsamples;
    float min = 999999, max = -999999;
    for (int i = 0; i < samples; i++) {
        for (int j = 0; j < samples; j++) {
            if(terrainData[i][j] < min) min = terrainData[i][j];
            if(terrainData[i][j] > max) max = terrainData[i][j];
            //terrainData[i][j] = tfile->floor + tfile->scale * (data->get() + 256*data->get());
        }
    }
    min -= 10;
    max += 10;
    // new tfile floor/scale
    tfile->floor = min;
    tfile->scale = (max - min)/65535;
    
    float fvalue;
    unsigned short value;
    for (int i = 0; i < samples; i++) {
        for (int j = 0; j < samples; j++) {
            fvalue = (terrainData[i][j] - tfile->floor) / tfile->scale;
            if (fvalue > 65535) value = 65535;
            else if(fvalue < 0) value = 0;
            else value = fvalue;
            write << value;
            //terrainData[i][j] = tfile->floor + tfile->scale * (data->get() + 256*data->get());
        }
    }
}

void Terrain::saveRAWFloat(QDataStream &write){
    int samples = *tfile->nsamples;
    for (int i = 0; i < samples; i++) {
        for (int j = 0; j < samples; j++) {
            write << (float)terrainData[i][j];
        }
    }
}

void Terrain::newF(){
    releaseFData();
    const int side = gridLayout.sampleCount + 1;
    fData = new unsigned char*[side];
    fDataRows = side;
    for (int j = 0; j < side; j++) {
        fData[j] = new unsigned char[side];
        for (int i = 0; i < side; i++) {
            fData[j][i] = 0;
        }
    }
    jestF = true;
    modifiedF = true;
    modified = true;
}

bool Terrain::readF(QString fSfile) {
    fSfile.replace("//", "/");
    QFile file(fSfile);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    FileBuffer* data = ReadFile::readRAW(&file);
    if (!validatePayload(data, sizeof(unsigned char), "terrain F RAW")) {
        delete data;
        return false;
    }
    readF(data);
    delete data;
    return true;
}

void Terrain::readF(FileBuffer *data){
    //qDebug() << "Wczytam teren F: " << fSfile << data->length;

    if (!validatePayload(data, sizeof(unsigned char), "terrain F RAW"))
        return;
    releaseFData();
    int samples = gridLayout.sampleCount;
    int u = 0;
    fData = new unsigned char*[samples+1];
    fDataRows = samples + 1;
    for (int j = 0; j < samples+1; j++) {
        fData[j] = new unsigned char[samples+1];
        for (int i = 0; i < samples+1; i++) {
            if (i == samples || j == samples)
                fData[j][i] = 0;
            else
                fData[j][i] = data->data[u++];
        }
    }
    jestF = true;

}

void Terrain::saveF(QString name) {
    name.replace("//", "/");
    QFile *file = new QFile(name);
    qDebug() << "zapis " << name;
    if (!file->open(QIODevice::WriteOnly))
        return;
    qDebug() << "w";
    QDataStream write(file);
    write.setByteOrder(QDataStream::LittleEndian);
    saveF(write);
    modifiedF = false;
    file->close();
    return;
}

void Terrain::saveF(QDataStream &write) {

    int samples = *tfile->nsamples;
    for (int i = 0; i < samples; i++) {
        for (int j = 0; j < samples; j++) {
            write << fData[i][j];
        }
    }

}

int Terrain::getSelectedPathId(){
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            return uu;
        }
    }
    return -1;
}

int Terrain::getSelectedShaderId(){
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            return tfile->tdata[(uu)*13 + 0 + 6];
        }
    }
    return -1;
}

QString Terrain::getPatchMainTextureName(){
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            return *tfile->materials[(int) tfile->tdata[(uu)*13 + 0 + 6]].tex[0];
        }
    }
    return "UNDEFINED";
}

QString Terrain::getPatchMainTextureName(int x, int z, float posx, float posz){
    getPatchCoords(x, z, posx, posz);
    int patches = tfile->patchsetNpatches;

    return getPatchMainTextureName(z * patches + x);
}

QString Terrain::getPatchMainTextureName(int u){
    if(!gridLayout.isPatchIndexValid(u))
        return "UNDEFINED";
    return *tfile->materials[(int) tfile->tdata[(u)*13 + 0 + 6]].tex[0];

    //return "UNDEFINED";
}

bool Terrain::select(int value, bool oneMore){
    if (!editable)
        return false;
    if (!gridLayout.isPatchIndexValid(value))
        return false;
    if(oneMore){
        selected = true;
        selectedPatchs[value] = !selectedPatchs[value];
        reloadLines();
        return true;
    }
    return select(value);
}

bool Terrain::select(int value){
    if (!editable)
        return false;
    if (!gridLayout.isPatchIndexValid(value))
        return false;
    if(selected){
        int selectedId = 0;
        bool hasAnchor = false;
        for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
            if(selectedPatchs[uu]){
                selectedId = uu;
                hasAnchor = true;
                break;
            }
        }
        if (!hasAnchor) {
            selectedPatchs[value] = true;
            reloadLines();
            return true;
        }
        int u1 = gridLayout.patchRow(selectedId);
        int y1 = gridLayout.patchColumn(selectedId);
        int u2 = gridLayout.patchRow(value);
        int y2 = gridLayout.patchColumn(value);
        if(u1 > u2){
            int temp = u2;
            u2 = u1;
            u1 = temp;
        }
        if(y1 > y2){
            int temp = y2;
            y2 = y1;
            y1 = temp;
        }
        for(int i = u1; i <= u2; i++)
            for(int j = y1; j <= y2; j++){
                if(i < 0 || j < 0 || i >= gridLayout.patchesPerSide
                        || j >= gridLayout.patchesPerSide)
                    continue;
                selectedPatchs[gridLayout.patchIndex(i, j)] = true;
            }
    } else {
        selected = true;
        selectedPatchs[value] = true;
    }
    reloadLines();
    return true;
}

bool Terrain::unselect(){
    for (int i = 0; i < gridLayout.patchRecordCount(); i++) {
        selectedPatchs[i] = false;
    }
    reloadLines();
    this->selected = false;
    return false;
}

void Terrain::pushContextMenuActions(QMenu *menu){
    if (!editable)
        return;
    if(contextMenuActions["togglewater"] == NULL){
        contextMenuActions["togglewater"] = new QAction(tr("&Toggle Water")); 
        QObject::connect(contextMenuActions["togglewater"], SIGNAL(triggered()), this, SLOT(menuToggleWater()));
    }
    if(contextMenuActions["puttexture"] == NULL){
        contextMenuActions["puttexture"] = new QAction(tr("&Put Texture")); 
        QObject::connect(contextMenuActions["puttexture"], SIGNAL(triggered()), this, SLOT(menuPutTexture()));
    }
    if(contextMenuActions["toggledraw"] == NULL){
        contextMenuActions["toggledraw"] = new QAction(tr("&Toggle Draw")); 
        QObject::connect(contextMenuActions["toggledraw"], SIGNAL(triggered()), this, SLOT(menuToggleDraw()));
    }
    if(contextMenuActions["selectobjects"] == NULL){
        contextMenuActions["selectobjects"] = new QAction(tr("&Select Objects")); 
        QObject::connect(contextMenuActions["selectobjects"], SIGNAL(triggered()), this, SLOT(menuSelectObjects()));
    }
    menu->addAction(contextMenuActions["puttexture"]);
    menu->addAction(contextMenuActions["togglewater"]);
    menu->addAction(contextMenuActions["toggledraw"]);
    menu->addAction(contextMenuActions["selectobjects"]);
}

void Terrain::menuToggleWater(){
    toggleWaterDraw();
}

void Terrain::menuToggleDraw(){
    toggleDraw();
}

void Terrain::menuPutTexture(){
    if(DefaultBrush == NULL)
        return;
    for (int uu = 0; uu < gridLayout.patchRecordCount(); uu++) {
        if(selectedPatchs[uu]){
            setTexture(DefaultBrush, uu);
        }
    }
}

void Terrain::menuSelectObjects(){
    const int patches = gridLayout.patchesPerSide;
    const int patchSize = gridLayout.patchWorldSize;
    int minx = gridLayout.terrainWorldSize;
    int maxx = 0;
    int minz = gridLayout.terrainWorldSize;
    int maxz = 0;
    for (int i = 0; i < patches; i++)
        for (int j = 0; j < patches; j++) {
            int uu = j * patches + i;
            if(selectedPatchs[uu]){
                if(i * patchSize < minx) minx = i * patchSize;
                if((i + 1) * patchSize > maxx) maxx = (i + 1) * patchSize;
                if(j * patchSize < minz) minz = j * patchSize;
                if((j + 1) * patchSize > maxz) maxz = (j + 1) * patchSize;
            }
        }
    if (minx >= maxx || minz >= maxz)
        return;
    if (gridLayout.terrainWorldSize != TerrainGridLayout::WorldTileSize) {
        qWarning() << "Selecting World objects from terrain patches is unsupported for"
                   << gridLayout.terrainWorldSize << "m terrain tiles";
        return;
    }
    qDebug() << minx << maxx << minz << maxz;
    qDebug() << minx - TerrainGridLayout::WorldTileHalfSize
             << maxx - TerrainGridLayout::WorldTileHalfSize
             << minz - TerrainGridLayout::WorldTileHalfSize
             << maxz - TerrainGridLayout::WorldTileHalfSize;
    if(Game::currentRoute != NULL)
        Game::currentRoute->selectObjectsByXYRange(
                    mojex, mojez,
                    minx - TerrainGridLayout::WorldTileHalfSize,
                    maxx - TerrainGridLayout::WorldTileHalfSize,
                    minz - TerrainGridLayout::WorldTileHalfSize,
                    maxz - TerrainGridLayout::WorldTileHalfSize);
}

bool Terrain::validateGridLayout(const QString &source) {
    if (tfile == NULL || tfile->nsamples == NULL || tfile->sampleSize == NULL) {
        qWarning() << "Unsupported terrain descriptor" << source
                   << "missing required sample metadata"
                   << "samples present" << (tfile != NULL && tfile->nsamples != NULL)
                   << "spacing present" << (tfile != NULL && tfile->sampleSize != NULL);
        gridLayout = TerrainGridLayout{};
        return false;
    }
    // MSTS accepts omitted sample rotation as the default unrotated grid.
    // Preserve the omission when saving instead of inserting a new token.
    const float rotation = tfile->sampleRotation == NULL
            ? 0.0f : *tfile->sampleRotation;
    QString error;
    if (!TerrainGridLayout::tryCreate(*tfile->nsamples, *tfile->sampleSize,
                                      tfile->patchsetNpatches,
                                      rotation,
                                      gridLayout, error)) {
        qWarning() << "Unsupported terrain descriptor" << source
                   << "samples" << *tfile->nsamples
                   << "spacing" << *tfile->sampleSize
                   << "patches" << tfile->patchsetNpatches
                   << "rotation" << rotation
                   << error;
        editable = false;
        return false;
    }
    editable = gridLayout.supportsEditing();
    patchGapState.fill(2, gridLayout.patchRecordCount());
    return true;
}

bool Terrain::validatePayload(const FileBuffer *data, std::size_t bytesPerCell,
                              const QString &kind) const {
    if (data == NULL || gridLayout.sampleCount == 0) {
        qWarning() << "Cannot load" << kind << "for terrain" << name
                   << "before a supported descriptor";
        return false;
    }
    std::size_t expectedBytes = 0;
    if (!gridLayout.expectedPayloadBytes(bytesPerCell, expectedBytes)) {
        qWarning() << "Cannot calculate" << kind << "size for terrain" << name;
        return false;
    }
    const int remaining = data->length - data->off;
    if (remaining < 0 || static_cast<std::size_t>(remaining) != expectedBytes) {
        qWarning() << "Invalid" << kind << "for terrain" << name
                   << "samples" << gridLayout.sampleCount
                   << "spacing" << gridLayout.sampleSpacing
                   << "patches" << gridLayout.patchesPerSide
                   << "expected bytes" << expectedBytes
                   << "actual bytes" << remaining;
        return false;
    }
    return true;
}

void Terrain::releaseHeightData() {
    if (terrainData != NULL) {
        for (int i = 0; i < terrainDataRows; ++i)
            delete[] terrainData[i];
        delete[] terrainData;
    }
    terrainData = NULL;
    terrainDataRows = 0;
}

void Terrain::releaseFData() {
    if (fData != NULL) {
        for (int i = 0; i < fDataRows; ++i)
            delete[] fData[i];
        delete[] fData;
    }
    fData = NULL;
    fDataRows = 0;
    jestF = false;
}
