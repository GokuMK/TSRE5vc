/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ENG_H
#define	ENG_H
#include <QString>
#include <QVector>
#include <QHash>
#include <memory>
#include <tsre/trains/EngLights.h>

class RenderQueue;

class OglObj;
class GLUU;
class Ruch;
class SoundSource;
class TrackSound;
class PlayDriver;
struct SoundValues;
class TrainNetworkEng;
class ContentHierarchyInfo;
class CarLights;

struct MapFeatures;

class Eng {
public:
    // The vehicle's footprint on the track for the map mode.
    void getMapFeatures(MapFeatures &features, int tileX, int tileZ);
    struct Coupling {
        QString type;
	float r0[2];
	float velocity;
    };
    
    struct EngShape {
        QString name;
        float x = 0;
        float y = 0;
        float z = 0;
        QHash<long long int, int> id;// = -2;
    };
    
    QVector<Coupling> coupling;
    QString name;
    QString path;
    QString pathid;
    QString hashid; // Cached logical key; pathid preserves the filename used for I/O.
    QString orpathid;
    QString orpath;
    QVector<QString> filePaths;
    
    EngShape shape;
    QVector<EngShape> freightanimShape;
    // Its Lights block (task renderer 34), kept as its text by load() and
    // read on first use (the Consist Editor loads every car and needs no
    // lights), shared by its copies; null without one.
    const EngLights *getLights();
    // The state its lights show, set by its consist.
    EngLights::Train lightsTrain;

    QString engName;
    QString displayName;
    QString searchKeywords;
    QString engType;
    QString typeHash;
    QString type;
    QString brakeSystemType;
    int wagonTypeId = 0;
    float mass = 0;
    float sizex;
    float sizey;
    float sizez;
    float maxSpeed = 0;
    float maxForce = 0;
    float maxPower = 0;
    float maxCurrent = 0;
    // For the simulation's driving (task simulation 03), in SI units: the
    // mass (kg), the engine's force (N), power (W) and top speed (m/s);
    // the resistance as Open Rails' Davis A, B, C (N, N/(m/s), N/(m/s)²)
    // where given, else MSTS's Friction (C1, E1, V2, C2, E2); a passenger
    // capacity (an EMU's power car).
    double massKg = 0.0;
    double forceN = 0.0;
    double powerW = 0.0;
    double maxSpeedMps = 0.0;
    double davis[3] = {0.0, 0.0, 0.0};
    bool hasDavis = false;
    double friction[5] = {0.0, 0.0, -1.0, 0.0, 0.0};
    bool hasFriction = false;
    bool passengerCapacity = false;
    QString souncCabFile;
    // The outside .sms ("Sound" of the Wagon section) and the wheels' radii
    // (the Engine section's are the driving wheels), in metres.
    QString soundFile;
    float wheelRadius = 0;
    float driverWheelRadius = 0;
    int flip = flip;
    int loaded = -1;
    bool kierunek = false;
    int ref = 0;
    Eng();
    Eng(Eng *o);
    virtual ~Eng();
    Eng(QString p, QString n);
    Eng(QString src, QString p, QString n);
    void load();
    float getFullWidth();
    QString getCouplingsName();
    void select();
    void unselect();
    bool isSelected();
    void pushDrawBorder(RenderQueue &queue);
    void pushDrawBorder3d(RenderQueue &queue);
    bool engFilter(QString q);
    bool couplingFilter(QString q);
    bool searchFilter(QString q);
    // On the track at the position (node, distance), moved by offset
    // metres along it (behind when negative) in one go.
    void initOnTrack(float *tpos, int direction, QMap<int, int>* junctionDirections, float offset = 0);
    // Placed by the simulation (task simulation 01): its front and rear (as
    // the consist lists it) at those distances along those vector nodes,
    // each facing the node's way (direction 0) or the other (1).
    void placeOnTrack(int frontNode, float frontDistance, int frontDirection,
                      int rearNode, float rearDistance, int rearDirection);
    void getCameraPosition(float *out = NULL);
    // What its sounds read (speed, throttle...); NULL without sound.
    const SoundValues *soundValues();
    void pushRenderItemOnTrack(RenderQueue &queue, float* playerT, quint32 selectionId, bool selected = false);
    void move(float m);
    float getCurrentElevation();
    float getTotalDistanceDownPath();
    void reload();
    bool isBroken();
    void updateSim(float deltaTime);
    float getCurrentSpeed();
    void pushRenderItems(RenderQueue &queue, quint32 selectionId = 0);
    float *getCurrentPositionOnTrack();
    void fillContentHierarchyInfo(QVector<ContentHierarchyInfo*>& list, int parent);
    // The nearest sound region item ahead of (or behind) the car on its
    // way within 8 km: its distance and track type, or -1.
    float soundRegion(bool ahead, int *trackType);
    // Its sounds: the outside .sms, the cab's when it leads (heard riding
    // it), the wheels' for the track type; their values from its motion and
    // the leading car's throttle and brake (0 to 1). cutoff: how far its
    // sounds are heard (the train's length and more).
    void updateSounds(bool lead, int trackType, float speed, float move, float throttle, float brake,
                      float cutoff, float seconds);
    // The stand-in driver's throttle and brake (the leading car's).
    float soundThrottle() const;
    float soundBrake() const;
    // Its sounds stopped, made again by the next updateSounds (a train
    // gone out of hearing).
    void silenceSounds();
private:
    std::shared_ptr<EngLightsBlock> lightsBlock;
    CarLights *carLights = NULL;
    long long int resolveShapeIds();
    // Drawn between two places on the track (as getDrawPositionOnTrNode
    // gives them: x, y, z, angle, slope, tile x, tile z), its front and rear
    // as the consist lists it, leaning by roll (superelevation), turned when
    // flipped; its footprint for the map between two such places.
    void pushRenderItemBetween(RenderQueue &queue, const float *playerT, quint32 selectionId, bool selected,
                               const float *front, const float *rear, float roll, bool flipped);
    void getMapFeaturesBetween(MapFeatures &features, int tileX, int tileZ, const float *front,
                               const float *rear) const;
    void initBorder();
    bool selected = false;
    OglObj *borderObj = NULL;
    OglObj *borderObj3d = NULL;
    Ruch *ruch1 = NULL;
    Ruch *ruch2 = NULL;
    void addToFileList(QString val);
    
    // The cab .sms ("Sound" of the Engine section), heard riding it.
    SoundSource *cabSound = NULL;
    bool cabSoundTried = false;
    TrackSound *trackSound = NULL;
    SoundSource *outsideSound = NULL;
    bool outsideSoundTried = false;
    SoundValues *soundData = NULL;
    float curveForceHeard = 0;
    float angleOfAttackHeard = 0;
    float variable2Heard = 0;
    PlayDriver *driver = NULL;
    float currentSpeed = 0;
    
    TrainNetworkEng* networkEng = NULL;
};

#endif	/* ENG_H */

