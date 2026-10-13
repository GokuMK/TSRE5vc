/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef PATHTRACK_H
#define PATHTRACK_H

#include <QMap>
#include <QVector>

class TDB;

// Activity paths on the track database (task editor 06): the nodes of a
// path as its file lists them, the track between them, and the edits of
// the path tool. A path has a node at every junction it passes, facing or
// trailing; the track it takes there is given by the next node. Between two
// junctions joined by two tracks, a halfway node on the track taken tells
// them apart.
namespace PathTrack {

enum class NodeType : unsigned char { Start, End, Junction, Reverse, Wait, Halfway };

// A path node: a junction (or track end) node of the track database, or a
// point on a vector node.
struct Node {
    NodeType type = NodeType::Junction;
    // The junction or end node (Junction), or the vector node (the others).
    int track = -1;
    // Points: the distance along the vector node, and the direction the
    // path leaves the point in (true: towards the node's pin 1, the
    // distance growing); the last node, the direction it arrives in.
    float distance = 0.0f;
    bool forward = true;
    // Seconds, for Wait nodes.
    int waitTime = 0;
    // Where the node is, in the editor's tile convention (z south).
    int tileX = 0;
    int tileZ = 0;
    float position[3] = {0.0f, 0.0f, 0.0f};
    // The node and PDP flags the file gave; -1 for nodes the editor made,
    // which get the flags of their type when saved.
    long long fileFlags = -1;
    int pdpFlags[2] = {-1, -1};
    // Not found on the track database.
    bool broken = false;

    bool isPoint() const { return type != NodeType::Junction; }
};

// The track between two consecutive nodes: one vector node between two
// distances along it, in the order the path runs.
struct Leg {
    int track = -1;
    float from = 0.0f;
    float to = 0.0f;
    bool broken = true;
    bool forward() const { return to >= from; }
};

// A position on the track: a vector node and the distance along it.
struct TrackPoint {
    int track = -1;
    float distance = 0.0f;
};

// Why an edit could not be made, or what it changed besides what was asked.
enum class Note : unsigned char {
    None,
    // The edit is not possible.
    NoTrack,
    NoPath,
    NotOnPath,
    NoRoute,
    NotFacingJunction,
    NoReverse,
    NoWait,
    // The edit was made.
    Rejoined,
    RestReplaced,
    StoppedAtLoop,
    StoppedAtLimit,
    StoppedAtBrokenTrack,
    // Repair: the path is routed again where its nodes could not be
    // followed, or it ends where no route was found.
    Rerouted,
    RepairStopped
};

struct Result {
    bool ok = false;
    QVector<Node> nodes;
    Note note = Note::None;
};

// Completion follows the track at most this far (metres).
constexpr float CompletionLimit = 200000.0f;
// A path started this close to a track end runs away from it.
constexpr float StartAwayFromEnd = 50.0f;
// An end placed where completion meets a loop stays this far before the
// junction that closes it.
constexpr float EndBeforeLoop = 5.0f;
// Take Other Direction rejoins the path within this many junctions.
constexpr int RejoinJunctions = 10;
// Two nodes or a point and the path closer than this are the same place.
constexpr float SamePlace = 0.5f;

// The legs between consecutive nodes (one fewer than the nodes).
QVector<Leg> legs(TDB *tdb, const QVector<Node> &nodes);
// Sets the points' directions from the legs.
void setDirections(QVector<Node> &nodes, const QVector<Leg> &legs);
// Sets a node's position from its place on the track database.
void place(TDB *tdb, Node &node);
// The length of a vector node.
float trackLength(TDB *tdb, int track);

// Where the path runs over a track point: the index of the first (or last)
// leg through it, or -1.
int legAt(const QVector<Leg> &legs, const TrackPoint &point, bool last);
// The exit Take Other Direction would take at junction node index: the
// junction's pin, or -1 when the node is not a facing junction of the path.
int otherExitPin(TDB *tdb, const QVector<Node> &nodes, const QVector<Leg> &legs, int index);

// Take Other Direction, Edit Wait Time and Remove Wait Point act on a node
// this close to the track point nearest the pointer (metres along the
// track).
constexpr float PointReach = 50.0f;
// The node Take Other Direction acts on for the track point nearest the
// pointer: the path's start when it is on that track, or a junction at
// either end of the track that the path passes facing (its last pass),
// within PointReach; of these, the nearest. -1 for none.
int otherDirectionNode(TDB *tdb, const QVector<Node> &nodes, const QVector<Leg> &legs,
                       const TrackPoint &point);
// The wait point on the track of the point, within PointReach, nearest
// it; -1 for none.
int waitNode(const QVector<Node> &nodes, const TrackPoint &point);
// The seconds Add Wait Point offers first.
constexpr int DefaultWaitTime = 60;
// The longest wait a path file holds (seconds).
constexpr int MaxWaitTime = 0xffff;

// The path tool's edits. Each returns the path's new nodes; ok is false
// when the edit is not possible, and note says why.
Result startAt(TDB *tdb, const TrackPoint &point);
Result turnStart(TDB *tdb, const QVector<Node> &nodes);
Result endAt(TDB *tdb, const QVector<Node> &nodes, const TrackPoint &point);
Result reverseAt(TDB *tdb, const QVector<Node> &nodes, const TrackPoint &point);
Result otherExit(TDB *tdb, const QVector<Node> &nodes, int index);
// The last reversal and everything after it go; the path is completed from
// where the reversal was, on in the direction it came.
Result removeLastReverse(TDB *tdb, const QVector<Node> &nodes);
// A wait point inserted on the path's last pass over the point; the route
// stays as it is.
Result waitAt(TDB *tdb, const QVector<Node> &nodes, const TrackPoint &point, int seconds);
Result setWaitTime(TDB *tdb, const QVector<Node> &nodes, int index, int seconds);
Result removeWait(TDB *tdb, const QVector<Node> &nodes, int index);
// A path built with the edits above that runs as the given nodes do: from
// their start, taking their other exits, reversals, waits and end (the
// path-roundtrip suite rebuilds the MSTS paths so). Without repair, ok is
// false where the nodes cannot be followed. With repair (Repair Path),
// nodes off the track database are left out, and where the nodes cannot be
// followed the path is routed to their next reversal, wait or end
// (Note::Rerouted); where no route is found it ends there
// (Note::RepairStopped).
Result rebuild(TDB *tdb, const QVector<Node> &nodes, bool repair);

// The output each facing junction of the path is left by, as Ruch takes
// it: 0 for the junction's first output pin, 1 for the next.
QMap<int, int> junctionChoices(TDB *tdb, const QVector<Node> &nodes, const QVector<Leg> &legs);
// Removes halfway nodes that are not between two junction nodes.
void dropStrayHalfways(QVector<Node> &nodes);
// The number of vector nodes joining two junctions.
int tracksBetween(TDB *tdb, int junctionA, int junctionB);

}

#endif
