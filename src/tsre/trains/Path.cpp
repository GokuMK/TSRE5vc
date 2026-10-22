/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/trains/Path.h>
#include <tsre/trains/ActLib.h>
#include <tsre/tdb/TRnode.h>
#include <QString>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QSet>
#include <QTextStream>
#include <tsre/Game.h>
#include <tsre/map/MapFeatures.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/tdb/Ruch.h>
#include <tsre/math3d/Vector3f.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/ogl/TrackItemObj.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/tdb/TRitem.h>
#include <algorithm>
#include <cmath>

using PathTrack::Node;
using PathTrack::NodeType;

namespace {

// "No next node" in a TrPathNode.
constexpr unsigned int NoNext = 4294967295u;
// A file node within this distance of a junction (metres, across the
// ground) is that junction; a point within this distance of a track is on
// it. Open Rails' TrackViewer calls a junction node broken beyond 2.5 m
// (its AI paths take the tile's nearest junction at any distance); PRUDNIK
// has paths 1.5 m off.
constexpr float JunctionSnap = 2.5f;
constexpr float PointSnap = 2.0f;

qint64 tileKey(int x, int z) {
    return (qint64(x) << 32) ^ qint64(quint32(z));
}

float acrossGround(int tileXA, int tileZA, const float *a, int tileXB, int tileZB, const float *b) {
    const float dx = float(tileXA - tileXB) * 2048.0f + a[0] - b[0];
    const float dz = float(tileZA - tileZB) * 2048.0f + a[2] - b[2];
    return std::sqrt(dx * dx + dz * dz);
}

// The nearest place on vector node `track` to a node's position, across the
// ground: its distance from the node and its distance along the track.
bool nearestOn(TDB *tdb, int track, const Node &node, float &across, float &along) {
    float *buffer = nullptr;
    int length = 0;
    tdb->getVectorSectionLine(buffer, length, node.tileX, node.tileZ, track, true);
    if (buffer == nullptr)
        return false;
    across = 1e9f;
    for (int i = 0; i + 12 <= length; i += 12) {
        const float *a = buffer + i;
        const float *b = buffer + i + 6;
        const float sx = b[0] - a[0], sz = b[2] - a[2];
        const float squared = sx * sx + sz * sz;
        float t = squared > 0.0f
                ? ((node.position[0] - a[0]) * sx + (node.position[2] - a[2]) * sz) / squared : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float dx = a[0] + sx * t - node.position[0];
        const float dz = a[2] + sz * t - node.position[2];
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d < across) {
            across = d;
            along = a[5] + (b[5] - a[5]) * t;
        }
    }
    delete[] buffer;
    return across < 1e9f;
}

// The track database's nodes by tile, for finding path nodes on it.
struct TrackIndex {
    QHash<qint64, QVector<int>> junctions;
    QHash<qint64, QVector<int>> vectors;

    explicit TrackIndex(TDB *tdb) {
        for (const auto &entry : tdb->trackNodes) {
            const TRnode *n = entry.second;
            if (n == nullptr)
                continue;
            if (n->typ == 0 || n->typ == 2) {
                junctions[tileKey(n->uid.tileX, -n->uid.tileZ)].push_back(entry.first);
            } else if (n->typ == 1) {
                QSet<qint64> tiles;
                for (int i = 0; i < n->iTrv; ++i)
                    tiles.insert(tileKey(n->trVectorSection[i].tileX, -n->trVectorSection[i].tileZ));
                for (qint64 tile : tiles)
                    vectors[tile].push_back(entry.first);
            }
        }
    }

    template<typename F>
    static void around(const QHash<qint64, QVector<int>> &index, const Node &node, F visit) {
        QSet<int> seen;
        for (int x = node.tileX - 1; x <= node.tileX + 1; ++x)
            for (int z = node.tileZ - 1; z <= node.tileZ + 1; ++z) {
                auto it = index.find(tileKey(x, z));
                if (it == index.end())
                    continue;
                for (int id : *it)
                    if (!seen.contains(id)) {
                        seen.insert(id);
                        visit(id);
                    }
            }
    }
};

void resolveJunction(TDB *tdb, const TrackIndex &index, Node &node) {
    float best = JunctionSnap;
    node.broken = true;
    TrackIndex::around(index.junctions, node, [&](int id) {
        const TRnode *n = tdb->trackNodes[id];
        const float uid[3] = {n->uid.x, n->uid.y, -n->uid.z};
        const float d = acrossGround(n->uid.tileX, -n->uid.tileZ, uid, node.tileX, node.tileZ,
                                     node.position);
        if (d <= best) {
            best = d;
            node.track = id;
            node.broken = false;
        }
    });
}

// A point node on the track nearest it; the tracks joining the junction
// nodes next to it first, as they tell apart tracks close together.
void resolvePoint(TDB *tdb, const TrackIndex &index, Node &node, const Node *previous,
                  const Node *next) {
    float best = PointSnap;
    node.broken = true;
    auto consider = [&](int id) {
        float across = 0.0f, along = 0.0f;
        if (nearestOn(tdb, id, node, across, along) && across <= best) {
            best = across;
            node.track = id;
            node.distance = along;
            node.broken = false;
        }
    };
    for (const Node *neighbour : {previous, next}) {
        if (neighbour == nullptr || neighbour->isPoint() || neighbour->broken)
            continue;
        const TRnode *junction = tdb->trackNodes[neighbour->track];
        for (int p = 0; p < junction->inputPinCount + junction->outputPinCount && p < 3; ++p) {
            auto it = tdb->trackNodes.find(junction->pins[p].link);
            if (it != tdb->trackNodes.end() && it->second != nullptr && it->second->typ == 1)
                consider(junction->pins[p].link);
        }
    }
    if (!node.broken)
        return;
    TrackIndex::around(index.vectors, node, consider);
}

void resolveNodes(TDB *tdb, const TrackIndex &index, QVector<Node> &nodes) {
    for (Node &node : nodes)
        if (!node.isPoint())
            resolveJunction(tdb, index, node);
    for (int k = 0; k < nodes.size(); ++k)
        if (nodes[k].isPoint())
            resolvePoint(tdb, index, nodes[k], k > 0 ? &nodes[k - 1] : nullptr,
                         k + 1 < nodes.size() ? &nodes[k + 1] : nullptr);
}

QString number(float value) {
    if (std::fabs(value) < 1e-4f)
        value = 0.0f;
    return QString::number(value, 'g', 6);
}

QString hex8(long long value) {
    return QString("%1").arg(qulonglong(value) & 0xffffffffull, 8, 16, QChar('0'));
}

// The node flags MSTS gives a node of each type: BBBB 1 a reversal, 2 a
// wait (AAAA its seconds), 4 a point between junctions.
long long nodeFlags(const Node &node) {
    if (node.fileFlags >= 0)
        return node.fileFlags;
    switch (node.type) {
    case NodeType::Reverse: return 1;
    case NodeType::Wait: return 2 | (qint64(std::clamp(node.waitTime, 0, 0xffff)) << 16);
    case NodeType::Halfway: return 4;
    default: return 0;
    }
}

bool sameNode(const Node &a, const Node &b) {
    return a.type == b.type && a.track == b.track && a.broken == b.broken
            && std::fabs(a.distance - b.distance) < 0.01f && a.waitTime == b.waitTime;
}

// A straight line between two nodes, relative to a tile: where the path's
// track is not on the track database.
void brokenLine(std::vector<float> &segments, const Node &a, const Node &b, int tileX, int tileZ) {
    for (const Node *node : {&a, &b}) {
        segments.push_back((node->tileX - tileX) * 2048 + node->position[0]);
        segments.push_back(node->position[1]);
        segments.push_back((node->tileZ - tileZ) * 2048 + node->position[2]);
    }
}

// What the 3D view and the map draw a node as: 0 start, 1 end, 2 reversal
// or wait, 3 a node off the track database; -1 not drawn.
int markerKind(const Node &node) {
    if (node.broken)
        return 3;
    switch (node.type) {
    case NodeType::Start: return 0;
    case NodeType::End: return 1;
    case NodeType::Reverse:
    case NodeType::Wait: return 2;
    default: return -1;
    }
}

}

Path::Path() {
    typeObj = activitypath;
}

Path::Path(QString p, QString n, bool nowe) {
    typeObj = activitypath;
    pathid = p + "/" + n;
    pathid = ContentPath::normalize(pathid);
    hashid = ContentPath::key(pathid);
    path = p;
    routeHashid = ContentPath::key(ContentPath::parentDirectory(path));
    name = n;
    nameId = n.section(".", 0, -2);
    if(!nowe){
        loaded = -1;
        load();
        if(loaded == 1)
            initRoute();
    } else {
        loaded = 1;
        modified = true;
        fileRead = true;
        trPathName = nameId;
    }
}

bool Path::isModified(){
    return modified;
}

void Path::load(){
    QString sh;
    pathid = ContentPath::normalize(pathid);
    hashid = ContentPath::key(pathid);
    qDebug() << pathid;
    QFile *file = new QFile(pathid);
    if (!file->open(QIODevice::ReadOnly)) {
        qDebug() << pathid << "not exist";
        return;
    }

    FileBuffer* data = ReadFile::read(file);
    file->close();
    data->toUtf16();
    data->skipBOM();

    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        //qDebug() << sh;
        if (sh == ("simisa@@@@@@@@@@jinx0p0t______")) {
            continue;
        }
        if (sh == ("serial")) {
            serial = ParserX::GetNumber(data);
            ParserX::SkipToken(data);
            continue;
        }
        if (sh == ("trackpdps")) {
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                if (sh == ("trackpdp")) {
                    trackPdp.push_back(new float[7]);
                    for(int i = 0; i < 7; i++)
                        trackPdp.back()[i] = ParserX::GetNumber(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                qDebug() << "#TrackPDPs - undefined token: " << sh;
                ParserX::SkipToken(data);
            }
            ParserX::SkipToken(data);
            continue;
        }
        // Names may be empty: "TrPathStart ( )" (GetStringInside).
        if (sh == ("trackpath")) {
            while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                if (sh == ("trpathname")) {
                    trPathName = ParserX::GetStringInside(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("name")) {
                    displayName = ParserX::GetStringInside(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("trpathstart")) {
                    trPathStart = ParserX::GetStringInside(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("trpathend")) {
                    trPathEnd = ParserX::GetStringInside(data);
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("trpathflags")) {
                    trPathFlags = ParserX::GetHex(data);
                    hasPathFlags = true;
                    ParserX::SkipToken(data);
                    continue;
                }
                if (sh == ("trpathnodes")) {
                    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
                        if (sh == ("trpathnode")) {
                            trPathNode.push_back(new unsigned int[4]);
                            trPathNode.back()[0] = ParserX::GetHex(data);
                            trPathNode.back()[1] = ParserX::GetUInt(data);
                            trPathNode.back()[2] = ParserX::GetUInt(data);
                            trPathNode.back()[3] = ParserX::GetUInt(data);
                            ParserX::SkipToken(data);
                            continue;
                        }
                        qDebug() << "#TrPathNodes - undefined token: " << sh;
                        ParserX::SkipToken(data);
                    }
                    ParserX::SkipToken(data);
                    continue;
                }
                qDebug() << "#TrackPath - undefined token: " << sh;
                ParserX::SkipToken(data);
            }
            ParserX::SkipToken(data);
            continue;
        }
        qDebug() << "#PAT - undefined token: " << sh;
        ParserX::SkipToken(data);
    }

    loaded = 1;
}

void Path::initRoute(){
    readNodes();
}

void Path::readNodes() {
    if (fileRead)
        return;
    fileRead = true;
    mainNodes.clear();
    passing.clear();
    const int count = trPathNode.size();
    auto nodeOf = [&](int index, bool first, bool last) {
        Node node;
        const unsigned int *raw = trPathNode[index];
        const unsigned int pdpIndex = raw[3];
        node.fileFlags = raw[0];
        if (pdpIndex >= unsigned(trackPdp.size())) {
            node.broken = true;
            node.type = NodeType::Halfway;
            return node;
        }
        const float *pdp = trackPdp[int(pdpIndex)];
        node.pdpFlags[0] = int(pdp[5]);
        node.pdpFlags[1] = int(pdp[6]);
        node.tileX = int(pdp[0]);
        node.tileZ = -int(pdp[1]);
        node.position[0] = pdp[2];
        node.position[1] = pdp[3];
        node.position[2] = -pdp[4];
        const unsigned int kind = raw[0] & 0xffffu;
        if (node.pdpFlags[0] == 2)
            node.type = NodeType::Junction;
        else if (first)
            node.type = NodeType::Start;
        else if (last)
            node.type = NodeType::End;
        else if (kind & 1u)
            node.type = NodeType::Reverse;
        else if (kind & 2u) {
            node.type = NodeType::Wait;
            node.waitTime = int(raw[0] >> 16);
        } else
            node.type = NodeType::Halfway;
        return node;
    };

    // The main path: from the first node along the main links.
    QVector<int> chain;
    QHash<int, int> mainIndex;
    for (int k = 0; k >= 0 && k < count && !mainIndex.contains(k);) {
        mainIndex.insert(k, chain.size());
        chain.push_back(k);
        const unsigned int next = trPathNode[k][1];
        k = next == NoNext || next >= unsigned(count) ? -1 : int(next);
    }
    for (int m = 0; m < chain.size(); ++m)
        mainNodes.push_back(nodeOf(chain[m], m == 0, m == chain.size() - 1));
    // Passing paths: from a main node along the passing links back to the
    // main path.
    for (int m = 0; m < chain.size(); ++m) {
        unsigned int next = trPathNode[chain[m]][2];
        if (next == NoNext)
            continue;
        PathSnapshot::Passing side;
        side.from = m;
        QSet<int> seen;
        while (next != NoNext && next < unsigned(count) && !mainIndex.contains(int(next))
               && !seen.contains(int(next))) {
            seen.insert(int(next));
            side.nodes.push_back(nodeOf(int(next), false, false));
            const unsigned int *raw = trPathNode[int(next)];
            next = raw[2] != NoNext ? raw[2] : raw[1];
        }
        if (next != NoNext && mainIndex.contains(int(next))) {
            side.to = mainIndex.value(int(next));
            passing.push_back(side);
        } else {
            ++unjoinedPassing;
        }
    }
    resolvedOn = NULL;
}

void Path::resolve() {
    readNodes();
    TDB *tdb = Game::trackDB;
    if (tdb == NULL || !tdb->loaded)
        return;
    if (resolvedOn == tdb && resolvedRevision == tdb->revision)
        return;
    const TrackIndex index(tdb);
    resolveNodes(tdb, index, mainNodes);
    for (PathSnapshot::Passing &side : passing)
        resolveNodes(tdb, index, side.nodes);
    resolvedOn = tdb;
    resolvedRevision = tdb->revision;
    rebuildDerived();
}

void Path::clearDerived() {
    for (OglObj *line : lines)
        delete line;
    lines.clear();
    linesX.clear();
    linesZ.clear();
    for (int style = 0; style < 2; ++style) {
        qDeleteAll(styleLines[style]);
        styleLines[style].clear();
        styleX[style].clear();
        styleZ[style].clear();
    }
    isinit1 = false;
    isinit2 = false;
}

void Path::rebuildDerived() {
    ++changeCount;
    clearDerived();
    TDB *tdb = resolvedOn;
    if (tdb == NULL)
        return;
    mainLegs = PathTrack::legs(tdb, mainNodes);
    PathTrack::setDirections(mainNodes, mainLegs);
    junctionDirections = PathTrack::junctionChoices(tdb, mainNodes, mainLegs);
    startDirection = !mainNodes.isEmpty() && mainNodes[0].isPoint() && !mainNodes[0].forward ? 1 : 0;

    findPlatformPasses(tdb);
    isinit1 = true;
}

bool Path::PathObject::meets(int itemId, float *distance) const {
    for (const QPair<int, float> &item : items)
        if (item.first == itemId) {
            if (distance != NULL)
                *distance = item.second;
            return true;
        }
    return false;
}

void Path::findPlatformPasses(TDB *tdb) {
    previousObjects.clear();
    for (const PathObject *object : pathObjects)
        previousObjects.push_back(*object);
    qDeleteAll(pathObjects);
    pathObjects.clear();
    const auto platformItem = [tdb](int id) -> TRitem* {
        auto item = tdb->trackItems.find(id);
        return item != tdb->trackItems.end() && item->second != NULL && item->second->type == "platformitem"
                && item->second->platformTrItemData != NULL ? item->second : NULL;
    };
    const auto platformItems = [&](int track) {
        QVector<TRitem*> items;
        auto it = tdb->trackNodes.find(track);
        const TRnode *vector = it != tdb->trackNodes.end() ? it->second : NULL;
        for (int i = 0; vector != NULL && i < vector->iTri; ++i)
            if (TRitem *item = platformItem(vector->trItemRef[i]))
                items.push_back(item);
        return items;
    };
    const auto within = [](const PathTrack::Leg &leg, float at) {
        return at >= std::min(leg.from, leg.to) - PathTrack::SamePlace
            && at <= std::max(leg.from, leg.to) + PathTrack::SamePlace;
    };
    QVector<float> legStart(mainLegs.size());
    float down = 0.0f;
    for (int i = 0; i < mainLegs.size(); ++i) {
        legStart[i] = down;
        down += std::fabs(mainLegs[i].to - mainLegs[i].from);
    }
    // The legs between reversals: the train meets each item at most once
    // in a run.
    QVector<QPair<int, int>> runs;
    int runStart = 0;
    for (int i = 1; i <= mainLegs.size(); ++i) {
        const bool reversal = i < mainLegs.size() && mainLegs[i].track == mainLegs[i - 1].track
                && std::fabs(mainLegs[i].from - mainLegs[i - 1].to) < PathTrack::SamePlace
                && mainLegs[i].forward() != mainLegs[i - 1].forward();
        if (i == mainLegs.size() || reversal) {
            runs.push_back({runStart, i});
            runStart = i;
        }
    }
    for (const QPair<int, int> &run : runs) {
        // The items of each platform met in the run.
        QMap<int, PathObject> met;
        for (int l = run.first; l < run.second; ++l) {
            const PathTrack::Leg &leg = mainLegs[l];
            if (leg.broken)
                continue;
            for (TRitem *item : platformItems(leg.track)) {
                const float at = item->getTrackPosition();
                if (!within(leg, at))
                    continue;
                const int id = int(item->trItemId);
                const int pair = std::min(id, int(item->platformTrItemData[1]));
                PathObject &object = met[pair];
                object.platform = pair;
                object.items.push_back({id, legStart[l] + std::fabs(at - leg.from)});
            }
        }
        QVector<PathObject> passes;
        for (auto it = met.begin(); it != met.end(); ++it) {
            PathObject &object = it.value();
            QSet<int> ids;
            for (const QPair<int, float> &item : object.items)
                ids.insert(item.first);
            bool counts = ids.size() >= 2;
            // One item met: the path starts or ends inside the platform
            // (its other item on the first or last leg's track, beyond it).
            if (!counts && !mainLegs.isEmpty()) {
                TRitem *item = platformItem(object.items.first().first);
                const int partnerId = item != NULL ? int(item->platformTrItemData[1]) : -1;
                TRitem *partner = platformItem(partnerId);
                const PathTrack::Leg &first = mainLegs.first(), &last = mainLegs.last();
                if (partner != NULL) {
                    const float at = partner->getTrackPosition();
                    bool onFirst = false, onLast = false;
                    for (TRitem *t : platformItems(first.track))
                        onFirst = onFirst || int(t->trItemId) == partnerId;
                    for (TRitem *t : platformItems(last.track))
                        onLast = onLast || int(t->trItemId) == partnerId;
                    counts = (run.first == 0 && onFirst && (first.forward() ? at < first.from : at > first.from))
                          || (run.second == mainLegs.size() && onLast && (last.forward() ? at > last.to : at < last.to));
                }
            }
            if (!counts)
                continue;
            std::sort(object.items.begin(), object.items.end(),
                      [](const QPair<int, float> &a, const QPair<int, float> &b) { return a.second < b.second; });
            object.trItemId = object.items.last().first;
            object.distanceDownPath = object.items.last().second;
            passes.push_back(object);
        }
        // In the order the train meets them; platforms it is on at once
        // (overlapping) as one.
        std::sort(passes.begin(), passes.end(), [](const PathObject &a, const PathObject &b) {
            return a.items.first().second < b.items.first().second;
        });
        const int firstOfRun = pathObjects.size();
        for (const PathObject &pass : passes) {
            PathObject *last = pathObjects.size() > firstOfRun ? pathObjects.last() : NULL;
            if (last != NULL && pass.items.first().second <= last->distanceDownPath + PathTrack::SamePlace) {
                last->items += pass.items;
                if (pass.distanceDownPath > last->distanceDownPath) {
                    last->distanceDownPath = pass.distanceDownPath;
                    last->trItemId = pass.trItemId;
                }
                continue;
            }
            pathObjects.push_back(new PathObject(pass));
        }
    }
    for (PathObject *object : pathObjects) {
        TRitem *item = platformItem(object->trItemId);
        object->name = item != NULL ? item->stationName : QString();
    }
}

const QVector<Node> &Path::nodes() {
    resolve();
    return mainNodes;
}

const QVector<PathSnapshot::Passing> &Path::passingPaths() {
    resolve();
    return passing;
}

const QVector<PathTrack::Leg> &Path::legs() {
    resolve();
    return mainLegs;
}

bool Path::isBroken() {
    resolve();
    for (const Node &node : mainNodes)
        if (node.broken)
            return true;
    for (const PathTrack::Leg &leg : mainLegs)
        if (leg.broken)
            return true;
    return false;
}

void Path::apply(const QVector<Node> &nodes) {
    resolve();
    int same = 0;
    while (same < nodes.size() && same < mainNodes.size() && sameNode(nodes[same], mainNodes[same]))
        ++same;
    // The nodes the same at the end, apart from those the same at the start.
    int tail = 0;
    while (tail < nodes.size() - same && tail < mainNodes.size() - same
           && sameNode(nodes[nodes.size() - 1 - tail], mainNodes[mainNodes.size() - 1 - tail]))
        ++tail;
    // Passing paths before the change stay, those after it move with the
    // nodes; those through it go.
    const int shift = nodes.size() - mainNodes.size();
    const int firstOfTail = mainNodes.size() - tail;
    lastDroppedPassing = 0;
    for (int i = passing.size() - 1; i >= 0; --i) {
        if (passing[i].to < same)
            continue;
        if (passing[i].from >= firstOfTail) {
            passing[i].from += shift;
            passing[i].to += shift;
            continue;
        }
        passing.removeAt(i);
        ++lastDroppedPassing;
    }
    mainNodes = nodes;
    modified = true;
    rebuildDerived();
    ActLib::PathChanged(this);
}

std::shared_ptr<PathSnapshot> Path::snapshot() {
    resolve();
    auto copy = std::make_shared<PathSnapshot>();
    copy->nodes = mainNodes;
    copy->passing = passing;
    copy->modified = modified;
    copy->saves = saves;
    return copy;
}

void Path::restore(const PathSnapshot &snapshot) {
    mainNodes = snapshot.nodes;
    passing = snapshot.passing;
    modified = snapshot.modified || snapshot.saves != saves;
    lastDroppedPassing = 0;
    rebuildDerived();
    ActLib::PathChanged(this, false);
}

void Path::copyFrom(Path &source) {
    const QVector<Node> copied = source.nodes();
    source.resolve();
    passing = source.passing;
    mainNodes = copied;
    displayName = source.displayName;
    trPathStart = source.trPathStart;
    trPathEnd = source.trPathEnd;
    trPathFlags = source.trPathFlags;
    hasPathFlags = source.hasPathFlags;
    fileRead = true;
    modified = true;
    resolvedOn = source.resolvedOn;
    resolvedRevision = source.resolvedRevision;
    rebuildDerived();
}

void Path::setDisplayName(const QString &value) {
    if (displayName == value)
        return;
    displayName = value;
    modified = true;
    ++changeCount;
}

void Path::setStartName(const QString &value) {
    if (trPathStart == value)
        return;
    trPathStart = value;
    modified = true;
    ++changeCount;
}

void Path::setEndName(const QString &value) {
    if (trPathEnd == value)
        return;
    trPathEnd = value;
    modified = true;
    ++changeCount;
}

void Path::setPathFlags(unsigned int value) {
    if (hasPathFlags && trPathFlags == value)
        return;
    trPathFlags = value;
    hasPathFlags = true;
    modified = true;
    ++changeCount;
}

QString Path::toText() {
    readNodes();
    // Output order: each main node, then the nodes of a passing path
    // leaving it (as MSTS and Open Rails TrackViewer write them).
    QVector<int> outIndex(mainNodes.size());
    QHash<int, int> passingFrom;
    for (int i = 0; i < passing.size(); ++i)
        passingFrom.insert(passing[i].from, i);
    int next = 0;
    for (int m = 0; m < mainNodes.size(); ++m) {
        outIndex[m] = next++;
        if (passingFrom.contains(m))
            next += passing[passingFrom.value(m)].nodes.size();
    }
    struct Entry { const Node *node; unsigned int main; unsigned int side; };
    QVector<Entry> entries;
    for (int m = 0; m < mainNodes.size(); ++m) {
        const unsigned int main = m + 1 < mainNodes.size() ? unsigned(outIndex[m + 1]) : NoNext;
        if (!passingFrom.contains(m)) {
            entries.push_back({&mainNodes[m], main, NoNext});
            continue;
        }
        const PathSnapshot::Passing &side = passing[passingFrom.value(m)];
        const unsigned int rejoin = unsigned(outIndex[side.to]);
        entries.push_back({&mainNodes[m], main,
                           side.nodes.isEmpty() ? rejoin : unsigned(outIndex[m] + 1)});
        for (int s = 0; s < side.nodes.size(); ++s)
            entries.push_back({&side.nodes[s], NoNext,
                               s + 1 < side.nodes.size() ? unsigned(outIndex[m] + 2 + s) : rejoin});
    }

    // PDPs: one for each junction, shared, and one for each point.
    QStringList pdps;
    QVector<int> pdpOf;
    QHash<int, int> junctionPdp;
    for (const Entry &entry : entries) {
        const Node &node = *entry.node;
        if (!node.isPoint() && !node.broken && junctionPdp.contains(node.track)) {
            pdpOf.push_back(junctionPdp.value(node.track));
            continue;
        }
        const int flag1 = node.pdpFlags[0] >= 0 ? node.pdpFlags[0] : (node.isPoint() ? 1 : 2);
        const int flag2 = node.pdpFlags[0] >= 0 ? node.pdpFlags[1] : (node.isPoint() ? 1 : 0);
        pdps.push_back(QString("\tTrackPDP ( %1 %2 %3 %4 %5 %6 %7 )")
                       .arg(node.tileX).arg(-node.tileZ)
                       .arg(number(node.position[0]), number(node.position[1]),
                            number(-node.position[2]))
                       .arg(flag1).arg(flag2));
        pdpOf.push_back(pdps.size() - 1);
        if (!node.isPoint() && !node.broken)
            junctionPdp.insert(node.track, pdps.size() - 1);
    }

    QString text;
    QTextStream out(&text);
    out << "SIMISA@@@@@@@@@@JINX0P0t______\n\n";
    out << "Serial ( " << (serial > 0 ? serial : 1) << " )\n";
    out << "TrackPDPs (\n";
    for (const QString &line : pdps)
        out << line << "\n";
    out << ")\n";
    out << "TrackPath (\n";
    out << "\tTrPathName ( " << ParserX::AddComIfReq(trPathName) << " )\n";
    if (hasPathFlags)
        out << "\tTrPathFlags ( " << hex8(trPathFlags) << " )\n";
    out << "\tName ( " << ParserX::AddComIfReq(displayName) << " )\n";
    out << "\tTrPathStart ( " << ParserX::AddComIfReq(trPathStart) << " )\n";
    out << "\tTrPathEnd ( " << ParserX::AddComIfReq(trPathEnd) << " )\n";
    out << "\tTrPathNodes ( " << entries.size() << "\n";
    for (int i = 0; i < entries.size(); ++i)
        out << "\t\tTrPathNode ( " << hex8(nodeFlags(*entries[i].node)) << " " << entries[i].main
            << " " << entries[i].side << " " << pdpOf[i] << " )\n";
    out << "\t)\n";
    out << ")\n";
    out.flush();
    return text;
}

bool Path::save() {
    // A new path without a start yet: nothing a simulator could read.
    readNodes();
    if (mainNodes.isEmpty()) {
        qWarning() << "Path without nodes not saved" << pathid;
        return false;
    }
    const QString text = toText();
    QFile file(pathid);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "Cannot write path file" << pathid;
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << text;
    out.flush();
    file.close();
    if (out.status() != QTextStream::Ok)
        return false;
    modified = false;
    ++saves;
    loaded = 1;
    return true;
}

bool Path::isNamed(const QString &pathId) const {
    return nameId.compare(pathId, Qt::CaseInsensitive) == 0
            || trPathName.compare(pathId, Qt::CaseInsensitive) == 0;
}

QMap<int, int>* Path::getJunctionDirections(){
    resolve();
    return &junctionDirections;
}

int Path::getStartDirection(){
    resolve();
    return startDirection;
}

float* Path::getStartPositionTXZ(float* out){
    resolve();
    if(out == NULL)
        out = new float[4];
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (mainNodes.isEmpty())
        return out;
    // In the file's convention: tile x and z, x and z.
    const Node &start = mainNodes[0];
    out[0] = start.tileX;
    out[1] = -start.tileZ;
    out[2] = start.position[0];
    out[3] = -start.position[2];
    return out;
}

void Path::init3dShapes(bool initShapes){
    resolve();
    if (isinit2 || !initShapes || resolvedOn == NULL)
        return;
    static const float green[3] = {0.0f, 1.0f, 0.0f};
    appendLines(lines, linesX, linesZ, green, 2);
    isinit2 = true;
}

// The track of each leg as lines in the colour (a broken leg's straight
// line magenta), one object per leg in its first node's tile.
void Path::appendLines(QVector<OglObj*> &out, QVector<float> &outX, QVector<float> &outZ,
                       const float *colour, float width){
    TDB *tdb = resolvedOn;
    for (int k = 0; k < mainLegs.size(); ++k) {
        const PathTrack::Leg &leg = mainLegs[k];
        std::vector<float> segments;
        const int tileX = mainNodes[k].tileX;
        const int tileZ = mainNodes[k].tileZ;
        if (leg.broken)
            brokenLine(segments, mainNodes[k], mainNodes[k + 1], tileX, tileZ);
        else
            tdb->getTrackSegments(segments, leg.track, leg.from, leg.to, tileX, tileZ);
        if (segments.empty())
            continue;
        OglObj *line = new OglObj();
        line->init(segments.data(), int(segments.size()), RenderItem::V, GL_LINES);
        if (leg.broken)
            line->setMaterial(0.9, 0.0, 0.63);
        else
            line->setMaterial(colour[0], colour[1], colour[2]);
        line->setLineWidth(width);
        out.push_back(line);
        outX.push_back(tileX);
        outZ.push_back(tileZ);
    }
}

void Path::pushRenderItems(RenderQueue &queue, float *playerT, quint32 selectionId, int style) {
    static const float colours[4][3] = {{0.0f, 0.8f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.5f, 0.0f},
                                        {0.9f, 0.0f, 0.63f}};
    for (int kind = 0; kind < 4; ++kind)
        if (pointer3d[kind] == NULL) {
            pointer3d[kind] = new TrackItemObj(1);
            pointer3d[kind]->setMaterial(colours[kind][0], colours[kind][1], colours[kind][2]);
        }

    init3dShapes();

    if (!Game::viewInteractives)
        return;

    for (const Node &node : mainNodes) {
        const int kind = markerKind(node);
        if (kind < 0)
            continue;
        queue.pushTransform();
        Mat4::translate(queue.transform(), queue.transform(),
                        node.position[0] + 2048 * (node.tileX - playerT[0]), node.position[1] + 2.2,
                        node.position[2] + 2048 * (node.tileZ - playerT[1]));
        pointer3d[kind]->pushRenderItem(queue);
        queue.popTransform();
    }

    QVector<OglObj*> *shown = &lines;
    QVector<float> *shownX = &linesX, *shownZ = &linesZ;
    if ((style == LineAITrain || style == LineChosenTrain) && resolvedOn != NULL) {
        static const float colours[2][3] = {{0.2f, 0.45f, 1.0f}, {0.0f, 0.9f, 1.0f}};
        const int k = style - 1;
        if (styleLines[k].isEmpty())
            appendLines(styleLines[k], styleX[k], styleZ[k], colours[k], style == LineChosenTrain ? 4 : 2);
        shown = &styleLines[k];
        shownX = &styleX[k];
        shownZ = &styleZ[k];
    }
    for (int i = 0; i < shown->size(); i++) {
        queue.pushTransform();
        Mat4::translate(queue.transform(), queue.transform(), 2048 * ((*shownX)[i] - playerT[0]), 2.2, 2048 * ((*shownZ)[i] - playerT[1]));
        (*shown)[i]->pushRenderItem(queue);
        queue.popTransform();
    }
}

void Path::getMapFeatures(MapFeatures &features, int tileX, int tileZ) {
    resolve();
    if (resolvedOn == NULL)
        return;
    for (int k = 0; k < mainLegs.size(); ++k) {
        const PathTrack::Leg &leg = mainLegs[k];
        if (leg.broken)
            brokenLine(features.brokenLines, mainNodes[k], mainNodes[k + 1], tileX, tileZ);
        else
            resolvedOn->getTrackSegments(features.lines, leg.track, leg.from, leg.to, tileX, tileZ);
    }
    for (const Node &node : mainNodes) {
        const int kind = markerKind(node);
        if (kind < 0)
            continue;
        std::vector<float> &into = kind == 0 ? features.starts : kind == 1 ? features.ends
                                 : kind == 3 ? features.brokenPoints : features.points;
        into.push_back((node.tileX - tileX) * 2048 + node.position[0]);
        into.push_back(node.position[1]);
        into.push_back((node.tileZ - tileZ) * 2048 + node.position[2]);
    }
}

Path::Path(const Path& orig) {
}

Path::~Path() {
}

void Path::CreatePaths(TDB * tdb){
    QString path;
    path = Game::root + "/ROUTES/" + Game::route + "/PATHS";
    QDir dir(path);
    qDebug() << path;
    dir.setNameFilters(QStringList() << "*.pat");
    dir.setFilter(QDir::Files);
    foreach(QString dirFile, dir.entryList())
    {
        qDebug() << dirFile;
        if(!QDir(dirFile).exists())
            dir.remove(dirFile);
    }
    
    QFile file;
    QTextStream out;
    QString filepath;
    
    Ruch ruch;
    Vector3f* pos;
    for (int i = 1; i <= tdb->iTRnodes; i++) {
        if (tdb->trackNodes[i] == NULL) continue;
        if (tdb->trackNodes[i]->typ == 0) {
            //tdb->trackNodes[i]->pins[0].link;
            ruch.set(i, 0, 0);

            filepath = path+"/"+QString::number(i,10)+".pat";
            file.setFileName(filepath);
            //qDebug() << filepath;
            if(!file.open(QIODevice::WriteOnly | QIODevice::Text)){
                qDebug() << "Cannot create path file: " << filepath;
                continue;
            }
            out.setDevice(&file);
            out.setRealNumberPrecision(8);
            out.setEncoding(QStringConverter::Utf16);
            out.setGenerateByteOrderMark(true);

            out << "SIMISA@@@@@@@@@@JINX0P0t______" << "\n\n";
            out << "Serial ( 1 )" << "\n";
            out << "TrackPDPs (" << "\n";
            //ruch.toNext(1);
            //pos = ruch.getPosition();
            float *dPos = ruch.getCurrentPosition();
            Game::check_coords(dPos[5],dPos[6],dPos[0],dPos[2]);
            qDebug() << dPos[0]<<" "<<dPos[1]<<" "<<dPos[2];
            out << "	TrackPDP ( "<<dPos[5]<<" "<<dPos[6]<<" "<<dPos[0]<<" "<<dPos[1]<<" "<<dPos[2]<<" 1 1 )" << "\n";
            ruch.next(10);
            dPos = ruch.getCurrentPosition();
            Game::check_coords(dPos[5],dPos[6],dPos[0],dPos[2]);
            qDebug() << dPos[0]<<" "<<dPos[1]<<" "<<dPos[2];
            out << "	TrackPDP ( "<<dPos[5]<<" "<<dPos[6]<<" "<<dPos[0]<<" "<<dPos[1]<<" "<<dPos[2]<<" 1 1 )" << "\n";
            out << ")" << "\n";
            out << "TrackPath (" << "\n";
            out << "	TrPathName ( "<<i<<" )" << "\n";
            out << "	Name ( endNode_"<<i<<" )" << "\n";
            out << "	TrPathStart ( endNode_"<<i<<" )" << "\n";
            out << "	TrPathEnd ( endNode_"<<i<<" )" << "\n";
            out << "	TrPathNodes ( 2" << "\n";
            out << "		TrPathNode ( 00000000 1 4294967295 0 )" << "\n";
            out << "		TrPathNode ( 00000000 4294967295 4294967295 1 )" << "\n";
            out << "	)" << "\n";
            out << ")" << "\n";
            
            out.flush();
            file.close();
        }
        
    }
}
