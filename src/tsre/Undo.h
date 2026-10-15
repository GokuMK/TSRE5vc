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
#include <memory>

class TDB;
class TSectionDAT;
class WorldObj;
class GameObj;
class Terrain;
class Path;
class Activity;
class Service;
class Traffic;
struct UndoSnapshot;
struct PathSnapshot;
struct TextSnapshot;

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
    QMap<Terrain*, std::shared_ptr<UndoSnapshot>> terrainMaterials;
    QMap<long long int, WorldObjInfo*> objData;
    QMap<Path*, std::shared_ptr<PathSnapshot>> pathData;
    QMap<Activity*, std::shared_ptr<TextSnapshot>> activityData;
    QMap<Service*, std::shared_ptr<TextSnapshot>> serviceData;
    QMap<Traffic*, std::shared_ptr<TextSnapshot>> trafficData;
    TDB* trackDB = NULL;
    TDB* roadDB = NULL;
    TSectionData tsectionData;
};

class Undo {

public:
    static bool UndoEnabled;
    // Undoes the last step; false when there is none, or it changes
    // activity data while activity editing is locked (a simulation
    // exists, ActivityNotifier): it stays for later.
    static bool UndoLast();
    // The step undo takes next changes activities, services, traffic or
    // paths.
    static bool LastChangesActivity();
    static void Clear();
    static void StateBegin();
    static void StateBeginIfNotExist();
    static bool StateBeginIndependent();
    static void StateEndIndependent();
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
    // An activity path's nodes before the path tool changes them.
    static void PushPathData(Path *path);
    // Activity data before the activity windows or tools change it (the
    // file's text; the first push of a state counts).
    static void PushActivityData(Activity *activity);
    static void PushServiceData(Service *service);
    static void PushTrafficData(Traffic *traffic);
    static bool IsStateOpen();
    static bool NeedsTerrainMaterialSnapshot(Terrain *terrain);
    static bool PushTerrainMaterial(Terrain *terrain);
    //static void PushTerrainTexture(int x, int z, int uu, unsigned char* data);
    
private:
    static QVector<UndoState*> undoStates;
    static UndoState* currentState;
    static UndoState* independentParentState;
    static bool independentStateOpen;
    static unsigned long long int independentParentUndoTime;
    static unsigned long long int undoTime;
    
    static void PushWorldObjDataInfo(WorldObj* obj);
};

#endif	/* UNDO_H */

