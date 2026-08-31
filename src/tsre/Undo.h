/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UNDO_H
#define	UNDO_H
#include <QMap>
#include <QVector>

class TDB;
class TSectionDAT;
class WorldObj;
class GameObj;

struct UndoState {
    ~UndoState();
    struct TerrainData {
        int x;
        int z;
        int samples = 0;
        bool low = false;
        QVector<float> data;
    };
    struct WorldObjInfo {
        WorldObj * obj;
        WorldObj * data;
        QString action;
        int x;
        int z;
    };
    struct TSectionData {
        TSectionDAT *data = NULL;
        int routeMaxIdxBefore = 0;
        int routeShapesBefore = 0;
        int routeMaxIdxAfter = 0;
        int routeShapesAfter = 0;
    };
    unsigned long long id;
    bool modified = false;
    QMap<int, TerrainData*> terrainData;
    QMap<int, unsigned char*> texData;
    QMap<long long int, WorldObjInfo*> objData;
    TDB* trackDB = NULL;
    TDB* roadDB = NULL;
    TSectionData tsectionData;
};

class Undo {

public:
    static bool UndoEnabled;
    static void UndoLast();
    static void Clear();
    static void StateBegin();
    static void StateBeginIfNotExist();
    static void StateEnd();
    static void StateCancel();
    static void StateEndIfLongTime();
    static void PushTerrainHeightMap(int x, int z, float **data, int samples,
                                     bool low = false);
    static void PushTextureData(int id, unsigned char *data, unsigned int size);
    static void PushGameObjData(GameObj* obj);
    static void PushWorldObjData(WorldObj* obj);
    static void PushWorldObjRemoved(WorldObj* obj);
    static void PushWorldObjPlaced(WorldObj* obj);
    static void SinglePushWorldObjData(WorldObj* obj);
    static void PushTrackDB(TDB *tdb, bool road = false);
    static void PushTSectionData(TSectionDAT *tsection);
    static bool IsStateOpen();
    //static void PushTerrainTexture(int x, int z, int uu, unsigned char* data);
    
private:
    static QVector<UndoState*> undoStates;
    static UndoState* currentState;
    static unsigned long long int undoTime;
    
    static void PushWorldObjDataInfo(WorldObj* obj);
};

#endif	/* UNDO_H */

