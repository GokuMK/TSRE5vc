/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef CONSIST_H
#define	CONSIST_H

#include <QString>
#include <QStringList>
#include <unordered_map>
#include <QVector>
#include <tsre/GameObj.h>
#include <tsre/trains/EngLights.h>

class RenderQueue;

class Eng;
class TextObj;
class FileBuffer;
class QTextStream;
class GLUU;
class Activity;
class SimpleHud;
class ContentHierarchyInfo;

struct MapFeatures;

class Consist : public GameObj {
public:
    // The vehicles' footprints for the map mode.
    void getMapFeatures(MapFeatures &features, int tileX, int tileZ);
    static std::unordered_map<int, TextObj*> txtNumbers;
    static int lastTxtNumbersColor;
    static TextObj * txtEngineE;
    static TextObj * txtEngineD;
    static TextObj * txtEngineS;
    static TextObj * txtEngineF;
    static TextObj * txtEngineW;
    static TextObj * txtEngineT;
    struct EngItem{
        bool flip = false;
        int uid = 0;
        int eng = -1;
        Eng *engPointer = NULL;
        float pos = 0;
        float conLength = 0;
        int type = 0;
        TextObj * txt = NULL;
        QString epath;
        QString ename;
        // Blocks TSRE does not read (Open Rails' LoadData and others), as
        // they were in the file; written at the end of the car.
        QStringList kept;
    };
    Consist();
    Consist(Consist * con, bool fullCopy = true);
    virtual ~Consist();
    int lastTxtColor;
    QString name;
    QString path;
    QString pathid;
    QString hashid; // Cached logical key; pathid preserves the filename used for I/O.
    QString conName;
    QString displayName;
    QString showName;
    unsigned int serial = 0;
    float maxVelocity[2];
    // MaxVelocity's speed as the file gives it (m/s), which the editor's
    // value above replaces with its engines'; -1 without one.
    float fileMaxVelocity = -1;
    int nextWagonUID = 0;
    float durability = 0;
    float conLength = 0;
    // The vehicles' mass and the engines' among it, kg.
    float massKg = 0;
    float engineMassKg = 0;
    int loaded = -1;
    bool isOnTrack = false;
    bool kierunek = false;
    int ref = 0;
    int posInit = false;
    int selectedIdx = -1;
    float textColor[3];
    QVector<EngItem> engItems;
    // Blocks TSRE does not read, as they were in the file: in the train
    // (Train or Train_Config) and in its TrainCfg; written at the end.
    QStringList kept;
    QStringList keptCfg;
    Consist(QString p, QString n);
    Consist(QString src, QString p, QString n);
    void load();
    bool load(FileBuffer* data);
    void refreshEngData();
    void save();
    void save(QString woff, QTextStream *out);
    bool select(int idx);
    bool unselect();
    void appendEngItem(int id, int pos = 2, bool flip = false);
    int getSelectedEngId();
    void replaceEngItemSelected(int id);
    void replaceEngItemById(int oldId, int newId);
    void deteleSelected();
    void flipSelected();
    void moveLeftSelected();
    void moveRightSelected();
    bool isNewConsist();
    bool isBroken();
    bool isUnSaved();
    void setModified(bool val);
    void setNewConsistFlag();
    void setFileName(QString n);
    void setDisplayName(QString n);
    void reverse();
    void setTextColor(float *bgColor);
    void setDurability(float val);
    // The vehicles behind the position, the first one across it. A
    // service's train (fitOnTrack) with less track behind its start than
    // it is long moves ahead until its rear is 1 m before the track's end.
    void initOnTrack(float *posTXZ, int direction, QMap<int, int> *junctionDirections = NULL, bool fitOnTrack = false);
    // The track behind the track position (node, distance) in the
    // direction, up to `wanted` metres: less where the track ends.
    static float roomBehind(const float *tpos, int direction, QMap<int, int> *junctionDirections, float wanted);
    bool getWagonWorldPosition(int id, float *posTW);
    void updateSim(float deltaTime);
    void pushRenderHud(RenderQueue &queue);
    // Where a camera riding the consist is: on vehicle cameraVehicle (the
    // first by default).
    void getCameraPosition(float *out);
    int cameraVehicle = 0;
    // Every vehicle outlined when `outlined` (the simulation's chosen train),
    // and the selected one; neither without showBorder (the Consist
    // properties' Show border, for every consist, not saved).
    static bool showBorder;
    void pushRenderItemsOnTrack(RenderQueue &queue, float *playerT, quint32 selectionId, bool outlined = false);
    // The state vehicle's lights show (task renderer 34): its place in the
    // consist (first, last, flipped, coupled, the leading locomotive), the
    // scene's day and weather, the rest the test state until the
    // simulation sets it (a train in service, headlights at Bright).
    EngLights::Train lightsTrain(int vehicle) const;
    // A train of the simulation (task simulation 01), its vehicles placed by
    // the simulation's frames (Eng::placeOnTrack): its speed, the distance
    // it has travelled and its driver's throttle and brake (0 to 1) as the
    // frame has them; its sounds follow them (updateSimulatedSounds).
    void setSimulated(float speed, float distance, float throttle, float brake);
    void updateSimulatedSounds(float seconds);
    // Its vehicles' sounds stopped (out of hearing), made again by the next
    // update.
    void silenceSounds();
    bool isSimulated() const { return simulated; }
    void pushRenderItems(RenderQueue &queue, quint32 selectionId = 0, bool renderText = false);
    void initPos();
    void setMaxVelocityFixed(bool val);
    bool isMaxVelocityFixed();
    QString getFirstEngName();
    void setTrainSpeed(float val);
    float getTrainSpeed();
    float getTrainDistanceTravelled();
    void fillContentHierarchyInfo(QVector<ContentHierarchyInfo*>& list, int parent);
    
private:
    TextObj *nameLabel(int i);
    TextObj *numberLabel(int i);
    TextObj *typeLabel(int wagonType);
    bool newConsist = false;
    bool modified = false;
    bool defaultValue = false;
    bool maxVelocityFixed = false;
    float trainSpeed = 0.0;
    float trainTotalDistance = 0.0;
    // Set by the simulation (setSimulated); the distance its sounds last
    // heard.
    bool simulated = false;
    float simulatedThrottle = 0.0f;
    float simulatedBrake = 1.0f;
    float soundsHeardAt = -1.0f;
    SimpleHud *hud = NULL;
    // Track sounds: the type the leading car is in, from the sound regions
    // around it (searched again once it passes the next one), and the
    // distances along the way where it changed, which the cars behind
    // reach later.
    int soundRegionType = 0;
    float soundRegionSearchedAt = -1;
    float soundRegionAhead = -1;
    QVector<QPair<float, int>> soundRegionChanges;
    void updateSounds(float move, float seconds);
    
    void replaceEngItem(int id, int pos);
};

#endif	/* CONSIST_H */

