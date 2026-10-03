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

class OglObj;
class GLUU;
class Ruch;
class SoundVariables;
class TrainNetworkEng;
class ContentHierarchyInfo;

class Eng {
public:
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

    QString engName;
    QString displayName;
    QString searchKeywords;
    QString engType;
    QString typeHash;
    QString type;
    QString brakeSystemType;
    int wagonTypeId = 0;
    float mass;
    float sizex;
    float sizey;
    float sizez;
    float maxSpeed = 0;
    float maxForce = 0;
    float maxPower = 0;
    float maxCurrent = 0;
    QString souncCabFile;
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
    void pushDrawBorder();
    void pushDrawBorder3d();
    bool engFilter(QString q);
    bool couplingFilter(QString q);
    bool searchFilter(QString q);
    void initOnTrack(float *tpos, int direction, QMap<int, int>* junctionDirections);
    void getCameraPosition(float *out = NULL);
    void pushRenderItemOnTrack(float* playerT, quint32 selectionId, bool selected = false);
    void move(float m);
    float getCurrentElevation();
    float getTotalDistanceDownPath();
    void reload();
    bool isBroken();
    void updateSim(float deltaTime);
    float getCurrentSpeed();
    void pushRenderItems(quint32 selectionId = 0);
    float *getCurrentPositionOnTrack();
    void fillContentHierarchyInfo(QVector<ContentHierarchyInfo*>& list, int parent);
private:
    long long int resolveShapeIds();
    void initBorder();
    bool selected = false;
    OglObj *borderObj = NULL;
    OglObj *borderObj3d = NULL;
    Ruch *ruch1 = NULL;
    Ruch *ruch2 = NULL;
    void addToFileList(QString val);
    
    int camSoundSourceId = -1;
    SoundVariables* soundVariables = NULL;
    float currentSpeed = 0;
    
    TrainNetworkEng* networkEng = NULL;
};

#endif	/* ENG_H */

