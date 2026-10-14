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

    // A pass of the path over a platform, as MSTS counts a stop's
    // SkipCount (task editor 07, Reference): each pass over both of a
    // platform's items between reversals (a switchback platform twice), the
    // platform the path starts or ends in, platforms passed together once.
    // A stop's SkipCount is its pass's place in pathObjects.
    struct PathObject {
        QString name;
        // The platform item the pass leaves the platform at, and the
        // distance down the path to it.
        int trItemId = -1;
        float distanceDownPath = 0;
        // The platform: the lower id of its two items.
        int platform = -1;
        // Each platform item met in the pass, with its distance.
        QVector<QPair<int, float>> items;
        // Whether the pass meets the item; its distance there.
        bool meets(int itemId, float *distance = NULL) const;
    };

    QVector<PathObject*> pathObjects;
    // The passes before the last change of the path (Path::apply, restore):
    // services find their stops' passes in them.
    const QVector<PathObject> &previousPasses() const { return previousObjects; }
    // Passing paths the last edit dropped.
    int droppedByLastEdit() const { return lastDroppedPassing; }

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
    // How the path's line is drawn: green (a selected path, the player's),
    // or as an activity's AI train's, blue, or cyan and wider when it is
    // the train chosen.
    enum LineStyle { LinePlain = 0, LineAITrain = 1, LineChosenTrain = 2 };
    void pushRenderItems(RenderQueue &queue, float * playerT, quint32 selectionId, int style = LinePlain);

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
    // Passing paths in the file that do not come back to the main path:
    // not read, so not written either.
    int droppedPassingPaths() const { return unjoinedPassing; }
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
    int unjoinedPassing = 0;
    unsigned int changeCount = 0;
    int serial = -1;
    TrackItemObj* pointer3d[4] = {NULL, NULL, NULL, NULL};
    QVector<OglObj*> lines;
    QVector<float> linesX;
    QVector<float> linesZ;
    // The lines in the AI train styles, made when first drawn so.
    QVector<OglObj*> styleLines[2];
    QVector<float> styleX[2];
    QVector<float> styleZ[2];
    void appendLines(QVector<OglObj*> &out, QVector<float> &outX, QVector<float> &outZ,
                     const float *colour, float width);
    QVector<PathObject> previousObjects;
    int lastDroppedPassing = 0;
    // The passes over platforms (PathObject), from the resolved legs.
    void findPlatformPasses(TDB *tdb);
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

