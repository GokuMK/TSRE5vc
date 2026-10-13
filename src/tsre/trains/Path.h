/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef PATH_H
#define	PATH_H


#include <QString>
#include <memory>
#include <tsre/ogl/GLUU.h>
#include <tsre/GameObj.h>
#include <tsre/tdb/TDB.h>
#include <tsre/trains/PathTrack.h>

class RenderQueue;

class OglObj;

struct MapFeatures;

// What undo keeps of a path (Undo::PushPathData).
struct PathSnapshot {
    QVector<PathTrack::Node> nodes;
    struct Passing {
        int from = 0;
        int to = 0;
        QVector<PathTrack::Node> nodes;
    };
    QVector<Passing> passing;
    bool modified = false;
    // Saves of the path when the snapshot was taken.
    int saves = 0;
};

// An activity path (.pat). Its nodes are read once and resolved on the
// track database (task editor 06): a node at every junction the path
// passes, its start, end, reversals, waits and halfway points. The track
// it runs over, the directions services take at junctions and the platforms
// along it come from the nodes.
class Path : public GameObj {
    Q_OBJECT
public:
    static void CreatePaths(TDB * tdb);

    struct PathObject {
        QString name;
        int trItemId = -1;
        float distanceDownPath;
    };

    QVector<PathObject*> pathObjects;

    QString displayName;
    QString name;
    QString nameId;
    QString path;
    QString pathid;
    QString hashid; // Cached logical key; pathid preserves the filename used for I/O.
    QString routeHashid;
    QString pathId;
    QString trPathName;
    // A service's or activity's PathID names this path: its file name, or
    // its TrPathName in content where the two differ.
    bool isNamed(const QString &pathId) const;
    QString trPathStart;
    QString trPathEnd;
    unsigned int trPathFlags = 0;
    int ref = 0;
    int loaded = -1;

    // The file's PDPs and nodes as read.
    QVector<float*> trackPdp;
    QVector<unsigned int*> trPathNode;

    Path();
    Path(QString p, QString n, bool nowe = false);
    Path(const Path& orig);
    virtual ~Path();
    void load();
    float* getStartPositionTXZ(float *out = NULL);
    int getStartDirection();
    QMap<int, int>* getJunctionDirections();
    void initRoute();
    // The path for the map mode: its track as lines, and its start, end,
    // reversal and wait points as points (each kind in its own list).
    void getMapFeatures(MapFeatures &features, int tileX, int tileZ);
    void init3dShapes(bool initShapes = true);
    bool isModified();
    void pushRenderItems(RenderQueue &queue, float * playerT, quint32 selectionId);

    // The nodes on the current track database, and the track between them.
    const QVector<PathTrack::Node> &nodes();
    const QVector<PathTrack::Leg> &legs();
    // Whether a node or the track between nodes is not on the track
    // database.
    bool isBroken();
    // Replaces the nodes after an edit of the path tool. Passing paths at or
    // after the first changed node are dropped.
    void apply(const QVector<PathTrack::Node> &nodes);
    std::shared_ptr<PathSnapshot> snapshot();
    void restore(const PathSnapshot &snapshot);
    // Clone: another path's nodes, names and flags.
    void copyFrom(Path &source);
    // Grows with every change of the path, for panels that show it.
    unsigned int changes() const { return changeCount; }
    bool hasFlags() const { return hasPathFlags; }
    // TrPathFlags; 0x20 marks a path no player train runs (Open Rails).
    void setPathFlags(unsigned int value);
    void setDisplayName(const QString &value);
    void setStartName(const QString &value);
    void setEndName(const QString &value);
    // Writes the file; false when it could not be written.
    bool save();
    // The file's text.
    QString toText();

private:
    bool modified = false;
    // Times saved: a state restored from before a save is not on disk.
    int saves = 0;
    bool isinit1 = false;
    bool isinit2 = false;
    bool hasPathFlags = false;
    unsigned int changeCount = 0;
    int serial = -1;
    TrackItemObj* pointer3d[4] = {NULL, NULL, NULL, NULL};
    QVector<OglObj*> lines;
    QVector<float> linesX;
    QVector<float> linesZ;
    QMap<float, PathObject*> pathObjectsMap;
    QMap<int, int> junctionDirections;
    int startDirection = 0;
    // The nodes of the main path, and the passing paths beside it: the
    // nodes between main node from and main node to.
    QVector<PathTrack::Node> mainNodes;
    QVector<PathSnapshot::Passing> passing;
    QVector<PathTrack::Leg> mainLegs;
    // The track database the nodes were resolved on, and its revision.
    TDB *resolvedOn = NULL;
    unsigned int resolvedRevision = 0;
    bool fileRead = false;

    // Reads the nodes from the file's PDPs and nodes, on first use.
    void readNodes();
    // Finds the nodes on the current track database: after reading, and
    // again after the track database changed.
    void resolve();
    void rebuildDerived();
    void clearDerived();
};

#endif	/* PATH_H */

