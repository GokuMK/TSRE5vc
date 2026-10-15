/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "PathTrack.h"
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tdb/TrackShape.h>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <queue>
#include <vector>

namespace PathTrack {

namespace {

TRnode *trackNode(TDB *tdb, int id) {
    if (tdb == nullptr || id <= 0)
        return nullptr;
    auto it = tdb->trackNodes.find(id);
    return it == tdb->trackNodes.end() ? nullptr : it->second;
}

bool isVector(const TRnode *n) { return n != nullptr && n->typ == 1; }
bool isJunction(const TRnode *n) { return n != nullptr && n->typ == 2; }
bool isEnd(const TRnode *n) { return n != nullptr && n->typ == 0; }

int pinCount(const TRnode *n) {
    return std::min(3, n->inputPinCount + n->outputPinCount);
}

// The end of a vector node (0: its pin 0, distance 0; 1: its pin 1, the far
// end) at junction or end node `at`; `prefer` when it joins `at` at both
// ends; -1 when it does not join it.
int endAt(const TRnode *vector, int at, int prefer = 0) {
    const bool first = vector->pins[0].link == at;
    const bool second = vector->pins[1].link == at;
    if (first && second)
        return prefer;
    return first ? 0 : second ? 1 : -1;
}

// The pin of a junction or end node that links vector node `track` at the
// vector's end `end`. A vector joining the node at both ends has its end 0
// at the first such pin and its end 1 at the last.
int pinOf(const TRnode *node, int track, int end) {
    int found = -1;
    for (int p = 0; p < pinCount(node); ++p)
        if (node->pins[p].link == track) {
            if (end == 0)
                return p;
            found = p;
        }
    return found;
}

// The end of the vector node at a junction's pin.
int endOfPin(const TRnode *junction, int junctionId, int pin, const TRnode *vector) {
    const int end = endAt(vector, junctionId);
    if (end < 0 || vector->pins[0].link != vector->pins[1].link)
        return end;
    return pinOf(junction, junction->pins[pin].link, 0) == pin ? 0 : 1;
}

// Whether a train passes a junction from pin a to pin b: always between an
// input and an output pin.
bool passes(const TRnode *junction, int a, int b) {
    const int in = junction->inputPinCount;
    return a >= 0 && b >= 0 && a != b && ((a < in) != (b < in));
}

// The output pin of a junction's main route (the track shape's MainRoute in
// tsection.dat), as Open Rails TrackViewer and MSTS take it.
int mainPin(TDB *tdb, const TRnode *junction) {
    const int in = junction->inputPinCount;
    int route = 0;
    if (tdb->tsection != nullptr) {
        auto it = tdb->tsection->shape.find(int(junction->junction.shapeIndex));
        if (it != tdb->tsection->shape.end() && it->second != nullptr)
            route = std::max(0, it->second->mainroute);
    }
    const int pin = in + route;
    return pin < pinCount(junction) ? pin : in;
}

// The track database during one edit, with the vector lengths it needed.
struct Track {
    TDB *tdb;
    QHash<int, float> lengths;

    float length(int track) {
        auto it = lengths.find(track);
        if (it != lengths.end())
            return *it;
        const float value = trackLength(tdb, track);
        lengths.insert(track, value);
        return value;
    }

    Node point(NodeType type, int track, float distance, bool forward) {
        Node node;
        node.type = type;
        node.track = track;
        node.distance = std::clamp(distance, 0.0f, length(track));
        node.forward = forward;
        place(tdb, node);
        return node;
    }

    Node junction(int id) {
        Node node;
        node.type = NodeType::Junction;
        node.track = id;
        place(tdb, node);
        return node;
    }

    // Appends a junction node reached over vector node `via`, with a halfway
    // node first when the last node is a junction joined to it by more than
    // one track.
    void appendJunction(QVector<Node> &nodes, int id, int via) {
        if (!nodes.isEmpty() && nodes.back().type == NodeType::Junction && !nodes.back().broken
                && tracksBetween(tdb, nodes.back().track, id) > 1) {
            const TRnode *vector = trackNode(tdb, via);
            const int end = vector != nullptr ? endAt(vector, nodes.back().track) : 0;
            nodes.push_back(point(NodeType::Halfway, via, 0.5f * length(via), end == 0));
        }
        nodes.push_back(junction(id));
    }
};

// Where an end goes before the junction ahead: EndBeforeLoop short of it,
// or halfway from where the path came onto the track.
float beforeJunction(float from, float length, bool forward) {
    if (forward) {
        const float d = length - EndBeforeLoop;
        return d > from ? d : 0.5f * (from + length);
    }
    return EndBeforeLoop < from ? EndBeforeLoop : 0.5f * from;
}

// Follows the track from distance `from` along vector `track` in direction
// `forward`, appending a node at every junction (the main route at facing
// ones), until a track end, where an End goes. It stops earlier, with an
// End, before a track it already followed in the same direction (a loop),
// past CompletionLimit, or where the track database is broken.
Note complete(Track &t, QVector<Node> &nodes, int track, bool forward, float from) {
    QSet<qint64> used;
    auto key = [](int v, bool f) { return (qint64(v) << 1) | (f ? 1 : 0); };
    used.insert(key(track, forward));
    float travelled = 0.0f;
    float start = from;
    for (;;) {
        const TRnode *vector = trackNode(t.tdb, track);
        const float length = t.length(track);
        travelled += forward ? length - start : start;
        const int far = forward ? 1 : 0;
        const int atId = vector->pins[far].link;
        const TRnode *at = trackNode(t.tdb, atId);
        auto end = [&](float distance, Note note) {
            nodes.push_back(t.point(NodeType::End, track, distance, forward));
            return note;
        };
        if (isEnd(at))
            return end(forward ? length : 0.0f, Note::None);
        if (!isJunction(at))
            return end(forward ? length : 0.0f, Note::StoppedAtBrokenTrack);
        if (travelled > CompletionLimit)
            return end(beforeJunction(start, length, forward), Note::StoppedAtLimit);
        const int entry = pinOf(at, track, far);
        const int exit = entry < at->inputPinCount ? mainPin(t.tdb, at) : 0;
        const int next = at->pins[exit].link;
        const TRnode *nextVector = trackNode(t.tdb, next);
        if (entry < 0 || exit == entry || !isVector(nextVector))
            return end(beforeJunction(start, length, forward), Note::StoppedAtBrokenTrack);
        const bool nextForward = endOfPin(at, atId, exit, nextVector) == 0;
        if (used.contains(key(next, nextForward)))
            return end(beforeJunction(start, length, forward), Note::StoppedAtLoop);
        used.insert(key(next, nextForward));
        t.appendJunction(nodes, atId, track);
        track = next;
        forward = nextForward;
        start = forward ? 0.0f : t.length(next);
    }
}

// A leg between nodes k and k + 1, given the legs before it.
Leg legBetween(Track &t, const QVector<Node> &nodes, const QVector<Leg> &before, int k) {
    const Node &a = nodes[k];
    const Node &b = nodes[k + 1];
    Leg leg;
    if (a.broken || b.broken)
        return leg;
    if (a.isPoint() && b.isPoint()) {
        if (a.track != b.track || !isVector(trackNode(t.tdb, a.track)))
            return leg;
        return Leg{a.track, a.distance, b.distance, false};
    }
    if (a.isPoint()) {
        const TRnode *vector = trackNode(t.tdb, a.track);
        if (!isVector(vector))
            return leg;
        const int end = endAt(vector, b.track, a.forward ? 1 : 0);
        if (end < 0)
            return leg;
        return Leg{a.track, a.distance, end == 1 ? t.length(a.track) : 0.0f, false};
    }
    if (b.isPoint()) {
        const TRnode *vector = trackNode(t.tdb, b.track);
        if (!isVector(vector))
            return leg;
        const int end = endAt(vector, a.track, b.forward ? 0 : 1);
        if (end < 0)
            return leg;
        return Leg{b.track, end == 1 ? t.length(b.track) : 0.0f, b.distance, false};
    }
    // Two junctions: the track joining them.
    const TRnode *ja = trackNode(t.tdb, a.track);
    const TRnode *jb = trackNode(t.tdb, b.track);
    if (ja == nullptr || jb == nullptr)
        return leg;
    struct Candidate { int track; int pinA; int pinB; };
    std::vector<Candidate> candidates;
    for (int p = 0; p < pinCount(ja); ++p) {
        const int id = ja->pins[p].link;
        const TRnode *vector = trackNode(t.tdb, id);
        if (!isVector(vector))
            continue;
        const int endA = endOfPin(ja, a.track, p, vector);
        if (endA < 0 || vector->pins[1 - endA].link != b.track)
            continue;
        if (a.track == b.track && endA == 1)
            continue;
        candidates.push_back({id, p, pinOf(jb, id, 1 - endA)});
    }
    if (candidates.empty())
        return leg;
    if (candidates.size() > 1 && k > 0 && !before[k - 1].broken) {
        // Only tracks a train can take from the track it came over.
        const Leg &in = before[k - 1];
        const int entry = pinOf(ja, in.track, in.forward() ? 1 : 0);
        std::vector<Candidate> usable;
        for (const Candidate &c : candidates)
            if (passes(ja, entry, c.pinA))
                usable.push_back(c);
        if (!usable.empty())
            candidates = usable;
    }
    Candidate chosen = candidates.front();
    if (candidates.size() > 1) {
        // No halfway node tells them apart: the main route of the junction
        // where they part.
        auto allOutputs = [&](const TRnode *j, bool atA) {
            for (const Candidate &c : candidates)
                if ((atA ? c.pinA : c.pinB) < j->inputPinCount)
                    return false;
            return true;
        };
        const TRnode *diverging = allOutputs(ja, true) ? ja : allOutputs(jb, false) ? jb : nullptr;
        if (diverging != nullptr) {
            const int main = mainPin(t.tdb, diverging);
            for (const Candidate &c : candidates)
                if ((diverging == ja ? c.pinA : c.pinB) == main)
                    chosen = c;
        }
    }
    const TRnode *vector = trackNode(t.tdb, chosen.track);
    const int endA = endOfPin(ja, a.track, chosen.pinA, vector);
    const float length = t.length(chosen.track);
    return Leg{chosen.track, endA == 0 ? 0.0f : length, endA == 0 ? length : 0.0f, false};
}

QVector<Leg> legsOf(Track &t, const QVector<Node> &nodes) {
    QVector<Leg> result;
    for (int k = 0; k + 1 < nodes.size(); ++k)
        result.push_back(legBetween(t, nodes, result, k));
    return result;
}

// The pin a junction node is entered by, and the pin it is left by.
int entryPin(TDB *tdb, const QVector<Node> &nodes, const QVector<Leg> &legs, int index) {
    if (index <= 0 || index > legs.size() || legs[index - 1].broken)
        return -1;
    const TRnode *junction = trackNode(tdb, nodes[index].track);
    if (junction == nullptr)
        return -1;
    const Leg &in = legs[index - 1];
    return pinOf(junction, in.track, in.forward() ? 1 : 0);
}

int exitPin(TDB *tdb, const QVector<Node> &nodes, const QVector<Leg> &legs, int index) {
    if (index < 0 || index >= legs.size() || legs[index].broken)
        return -1;
    const TRnode *junction = trackNode(tdb, nodes[index].track);
    if (junction == nullptr)
        return -1;
    const Leg &out = legs[index];
    return pinOf(junction, out.track, out.forward() ? 0 : 1);
}

Result fail(Note note) {
    Result result;
    result.note = note;
    return result;
}

Result done(QVector<Node> nodes, Note note) {
    dropStrayHalfways(nodes);
    Result result;
    result.ok = true;
    result.nodes = std::move(nodes);
    result.note = note;
    return result;
}

bool validPoint(Track &t, const TrackPoint &point) {
    return isVector(trackNode(t.tdb, point.track));
}

// The index of the last reversal, or 0: where End Here routes from.
int segmentStart(const QVector<Node> &nodes) {
    for (int k = nodes.size() - 1; k > 0; --k)
        if (nodes[k].type == NodeType::Reverse)
            return k;
    return 0;
}

// End Here off the path: the shortest route a train can drive, without
// reversing, to the point from a node of the path after its last reversal:
// that point (or the start), a junction node, or the end, which extends the
// path. The path is kept up to the node the route leaves from; of routes as
// long, the one from the later node.
Result routeTo(Track &t, const QVector<Node> &nodes, const QVector<Leg> &legs,
               const TrackPoint &point) {
    const int count = nodes.size();
    const int first = segmentStart(nodes);
    const float target = std::clamp(point.distance, 0.0f, t.length(point.track));
    struct Visit {
        int junction;
        int pin;
        float travel;
        float cost;
        int seed;
        int previous;
        int via;
    };
    struct Arrival {
        float cost = 0.0f;
        int visit = -1;
        int seed = -1;
        int track = -1;
        bool forward = true;
        bool found = false;
    };
    std::vector<Visit> visits;
    QHash<qint64, float> bestAt;
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    Arrival best;
    auto offer = [&](float cost, int visit, int seed, int track, bool forward) {
        if (!best.found || cost < best.cost)
            best = Arrival{cost, visit, seed, track, forward, true};
    };
    auto push = [&](const Visit &visit) {
        const qint64 key = (qint64(visit.junction) << 2) | visit.pin;
        auto it = bestAt.find(key);
        if (it != bestAt.end() && *it <= visit.cost)
            return;
        bestAt.insert(key, visit.cost);
        visits.push_back(visit);
        queue.push({visit.cost, int(visits.size()) - 1});
    };
    // Equally long routes: the one from the later node.
    auto seedCost = [&](int seed) { return 0.001f * float(count - 1 - seed); };

    // From a point of the path along its track: the segment's first point,
    // and the end.
    const int last = count - 1;
    auto fromPoint = [&](int index) {
        const Node &from = nodes[index];
        if (!from.isPoint() || from.broken || !isVector(trackNode(t.tdb, from.track)))
            return;
        const float base = seedCost(index);
        const float length = t.length(from.track);
        if (point.track == from.track
                && (from.forward ? target >= from.distance - SamePlace
                                 : target <= from.distance + SamePlace))
            offer(base + std::fabs(target - from.distance), -1, index, from.track, from.forward);
        const TRnode *vector = trackNode(t.tdb, from.track);
        const int far = from.forward ? 1 : 0;
        const int at = vector->pins[far].link;
        const TRnode *junction = trackNode(t.tdb, at);
        if (isJunction(junction)) {
            const float travel = from.forward ? length - from.distance : from.distance;
            push(Visit{at, pinOf(junction, from.track, far), travel, base + travel, index, -1,
                       from.track});
        }
    };
    fromPoint(first);
    if (last > first && nodes[last].type == NodeType::End)
        fromPoint(last);
    // From each junction node of the segment, where the path came in.
    for (int k = first; k < last; ++k) {
        if (nodes[k].type != NodeType::Junction || nodes[k].broken)
            continue;
        const int pin = entryPin(t.tdb, nodes, legs, k);
        if (pin >= 0)
            push(Visit{nodes[k].track, pin, 0.0f, seedCost(k), k, -1, -1});
    }

    int pops = 0;
    while (!queue.empty() && pops++ < 200000) {
        const auto [cost, index] = queue.top();
        queue.pop();
        const Visit visit = visits[index];
        if (cost > bestAt.value((qint64(visit.junction) << 2) | visit.pin))
            continue;
        if (best.found && cost >= best.cost)
            break;
        const TRnode *junction = trackNode(t.tdb, visit.junction);
        for (int exit = 0; exit < pinCount(junction); ++exit) {
            if (!passes(junction, visit.pin, exit))
                continue;
            const int track = junction->pins[exit].link;
            const TRnode *vector = trackNode(t.tdb, track);
            if (!isVector(vector))
                continue;
            const int end = endOfPin(junction, visit.junction, exit, vector);
            if (end < 0)
                continue;
            const bool forward = end == 0;
            const float length = t.length(track);
            if (track == point.track)
                offer(cost + (forward ? target : length - target), index, visit.seed, track, forward);
            const int nextId = vector->pins[1 - end].link;
            const TRnode *next = trackNode(t.tdb, nextId);
            if (!isJunction(next) || visit.travel + length > CompletionLimit)
                continue;
            push(Visit{nextId, pinOf(next, track, 1 - end), visit.travel + length, cost + length,
                       visit.seed, index, track});
        }
    }
    if (!best.found)
        return fail(Note::NoRoute);

    // The visits from the seed to the arrival.
    std::vector<int> chain;
    for (int v = best.visit; v >= 0; v = visits[v].previous)
        chain.push_back(v);
    std::reverse(chain.begin(), chain.end());
    // A route from the end replaces it.
    const bool fromEnd = best.seed == last && nodes[last].type == NodeType::End;
    QVector<Node> result = nodes.mid(0, fromEnd ? best.seed : best.seed + 1);
    for (int v : chain)
        if (visits[v].via >= 0)
            t.appendJunction(result, visits[v].junction, visits[v].via);
    result.push_back(t.point(NodeType::End, best.track, target, best.forward));
    return done(result, Note::None);
}

}

float trackLength(TDB *tdb, int track) {
    if (!isVector(trackNode(tdb, track)))
        return 0.0f;
    return tdb->getVectorSectionLength(track);
}

void place(TDB *tdb, Node &node) {
    const TRnode *n = trackNode(tdb, node.track);
    if (n == nullptr)
        return;
    if (node.isPoint()) {
        if (!isVector(n))
            return;
        float out[7];
        const float distance = std::clamp(node.distance, 0.0f, trackLength(tdb, node.track));
        if (!tdb->getDrawPositionOnTrNode(out, node.track, distance))
            return;
        node.tileX = int(out[5]);
        node.tileZ = -int(out[6]);
        node.position[0] = out[0];
        node.position[1] = out[1];
        node.position[2] = -out[2];
        return;
    }
    node.tileX = n->uid.tileX;
    node.tileZ = -n->uid.tileZ;
    node.position[0] = n->uid.x;
    node.position[1] = n->uid.y;
    node.position[2] = -n->uid.z;
}

int tracksBetween(TDB *tdb, int junctionA, int junctionB) {
    const TRnode *a = trackNode(tdb, junctionA);
    if (a == nullptr)
        return 0;
    QSet<int> found;
    for (int p = 0; p < pinCount(a); ++p) {
        const int id = a->pins[p].link;
        const TRnode *vector = trackNode(tdb, id);
        if (!isVector(vector))
            continue;
        const int end = endAt(vector, junctionA);
        if (end >= 0 && vector->pins[1 - end].link == junctionB)
            found.insert(id);
    }
    return found.size();
}

QVector<Leg> legs(TDB *tdb, const QVector<Node> &nodes) {
    Track t{tdb, {}};
    return legsOf(t, nodes);
}

void setDirections(QVector<Node> &nodes, const QVector<Leg> &legs) {
    for (int k = 0; k < nodes.size(); ++k) {
        Node &node = nodes[k];
        if (!node.isPoint())
            continue;
        if (k < legs.size() && !legs[k].broken && legs[k].from != legs[k].to)
            node.forward = legs[k].forward();
        else if (k > 0 && !legs[k - 1].broken && legs[k - 1].from != legs[k - 1].to)
            node.forward = legs[k - 1].forward();
    }
}

int legAt(const QVector<Leg> &legs, const TrackPoint &point, bool last) {
    int found = -1;
    for (int k = 0; k < legs.size(); ++k) {
        const Leg &leg = legs[k];
        if (leg.broken || leg.track != point.track)
            continue;
        const float low = std::min(leg.from, leg.to) - SamePlace;
        const float high = std::max(leg.from, leg.to) + SamePlace;
        if (point.distance < low || point.distance > high)
            continue;
        found = k;
        if (!last)
            break;
    }
    return found;
}

int otherExitPin(TDB *tdb, const QVector<Node> &nodes, const QVector<Leg> &legs, int index) {
    if (index <= 0 || index >= nodes.size() - 1 || nodes[index].type != NodeType::Junction
            || nodes[index].broken)
        return -1;
    const TRnode *junction = trackNode(tdb, nodes[index].track);
    if (!isJunction(junction))
        return -1;
    const int entry = entryPin(tdb, nodes, legs, index);
    const int exit = exitPin(tdb, nodes, legs, index);
    if (entry < 0 || exit < 0 || entry >= junction->inputPinCount)
        return -1;
    for (int pin = junction->inputPinCount; pin < pinCount(junction); ++pin)
        if (pin != exit && passes(junction, entry, pin)
                && isVector(trackNode(tdb, junction->pins[pin].link)))
            return pin;
    return -1;
}

int otherDirectionNode(TDB *tdb, const QVector<Node> &nodes, const QVector<Leg> &legs,
                       const TrackPoint &point) {
    const TRnode *vector = trackNode(tdb, point.track);
    if (!isVector(vector) || nodes.isEmpty())
        return -1;
    const float length = trackLength(tdb, point.track);
    float best = PointReach;
    int found = -1;
    const Node &start = nodes[0];
    if (start.type == NodeType::Start && !start.broken && start.track == point.track
            && std::fabs(point.distance - start.distance) <= best) {
        best = std::fabs(point.distance - start.distance);
        found = 0;
    }
    for (int end = 0; end < 2; ++end) {
        const float along = end == 0 ? point.distance : length - point.distance;
        if (along > best)
            continue;
        for (int k = nodes.size() - 2; k > 0; --k)
            if (nodes[k].type == NodeType::Junction && nodes[k].track == vector->pins[end].link
                    && otherExitPin(tdb, nodes, legs, k) >= 0) {
                best = along;
                found = k;
                break;
            }
    }
    return found;
}

Result startAt(TDB *tdb, const TrackPoint &point) {
    Track t{tdb, {}};
    if (!validPoint(t, point))
        return fail(Note::NoTrack);
    const TRnode *vector = trackNode(tdb, point.track);
    const float length = t.length(point.track);
    const float distance = std::clamp(point.distance, 0.0f, length);
    // Away from a track end close ahead.
    const bool forward = !(isEnd(trackNode(tdb, vector->pins[1].link))
                           && length - distance < StartAwayFromEnd);
    QVector<Node> nodes{t.point(NodeType::Start, point.track, distance, forward)};
    const Note note = complete(t, nodes, point.track, forward, distance);
    return done(nodes, note);
}

Result turnStart(TDB *tdb, const QVector<Node> &nodes) {
    Track t{tdb, {}};
    if (nodes.isEmpty() || nodes[0].type != NodeType::Start || nodes[0].broken
            || !isVector(trackNode(tdb, nodes[0].track)))
        return fail(Note::NoPath);
    Node start = nodes[0];
    start.forward = !start.forward;
    start.fileFlags = -1;
    QVector<Node> result{start};
    const Note note = complete(t, result, start.track, start.forward, start.distance);
    return done(result, note);
}

Result endAt(TDB *tdb, const QVector<Node> &nodes, const TrackPoint &point) {
    Track t{tdb, {}};
    if (nodes.isEmpty())
        return fail(Note::NoPath);
    if (!validPoint(t, point))
        return fail(Note::NoTrack);
    const QVector<Leg> path = legsOf(t, nodes);
    const int k = legAt(path, point, true);
    if (k >= 0) {
        QVector<Node> result = nodes.mid(0, k + 1);
        result.push_back(t.point(NodeType::End, point.track, point.distance, path[k].forward()));
        return done(result, Note::None);
    }
    return routeTo(t, nodes, path, point);
}

Result reverseAt(TDB *tdb, const QVector<Node> &nodes, const TrackPoint &point) {
    Track t{tdb, {}};
    if (nodes.isEmpty())
        return fail(Note::NoPath);
    if (!validPoint(t, point))
        return fail(Note::NoTrack);
    // Where the path passes the point more than once, its last pass: the
    // part being built (as End Here, and as Open Rails TrackViewer takes it).
    const QVector<Leg> path = legsOf(t, nodes);
    const int k = legAt(path, point, true);
    if (k < 0)
        return fail(Note::NotOnPath);
    const bool back = !path[k].forward();
    QVector<Node> result = nodes.mid(0, k + 1);
    const Node reverse = t.point(NodeType::Reverse, point.track, point.distance, back);
    result.push_back(reverse);
    const Note note = complete(t, result, point.track, back, reverse.distance);
    return done(result, note);
}

Result otherExit(TDB *tdb, const QVector<Node> &nodes, int index) {
    Track t{tdb, {}};
    const QVector<Leg> path = legsOf(t, nodes);
    const int pin = otherExitPin(tdb, nodes, path, index);
    if (pin < 0)
        return fail(Note::NotFacingJunction);
    const int junctionId = nodes[index].track;
    const TRnode *junction = trackNode(tdb, junctionId);
    const int firstTrack = junction->pins[pin].link;
    const bool firstForward = endOfPin(junction, junctionId, pin, trackNode(tdb, firstTrack)) == 0;

    // Rejoin the path ahead, before its next reversal or end, within
    // RejoinJunctions junctions along main routes.
    int limit = nodes.size() - 1;
    for (int k = index + 1; k < nodes.size(); ++k)
        if (nodes[k].type == NodeType::Reverse || nodes[k].type == NodeType::End) {
            limit = k;
            break;
        }
    struct Step { int junction; int via; };
    std::vector<Step> steps;
    int track = firstTrack;
    bool forward = firstForward;
    for (int step = 0; step < RejoinJunctions; ++step) {
        const TRnode *vector = trackNode(tdb, track);
        const int far = forward ? 1 : 0;
        const int atId = vector->pins[far].link;
        const TRnode *at = trackNode(tdb, atId);
        if (!isJunction(at))
            break;
        const int entry = pinOf(at, track, far);
        for (int r = index + 1; r < limit; ++r) {
            if (nodes[r].type != NodeType::Junction || nodes[r].track != atId || nodes[r].broken)
                continue;
            const int leave = exitPin(tdb, nodes, path, r);
            if (!passes(at, entry, leave))
                continue;
            QVector<Node> result = nodes.mid(0, index + 1);
            for (const Step &s : steps)
                t.appendJunction(result, s.junction, s.via);
            // The rejoined node, after a halfway node if one is needed.
            t.appendJunction(result, atId, track);
            result.pop_back();
            result += nodes.mid(r);
            return done(result, Note::Rejoined);
        }
        const int exit = entry < at->inputPinCount ? mainPin(tdb, at) : 0;
        const int next = at->pins[exit].link;
        const TRnode *nextVector = trackNode(tdb, next);
        if (entry < 0 || exit == entry || !isVector(nextVector))
            break;
        steps.push_back({atId, track});
        forward = endOfPin(at, atId, exit, nextVector) == 0;
        track = next;
    }

    QVector<Node> result = nodes.mid(0, index + 1);
    const Note note = complete(t, result, firstTrack, firstForward,
                               firstForward ? 0.0f : t.length(firstTrack));
    return done(result, note == Note::None ? Note::RestReplaced : note);
}

QVector<Leg> follow(TDB *tdb, int track, float from, bool forward, float length, bool *ended) {
    QVector<Leg> legs;
    if (ended != nullptr)
        *ended = false;
    Track t{tdb, {}};
    QSet<qint64> used;
    auto key = [](int v, bool f) { return (qint64(v) << 1) | (f ? 1 : 0); };
    float left = length;
    float start = from;
    for (;;) {
        const TRnode *vector = trackNode(tdb, track);
        if (!isVector(vector)) {
            if (ended != nullptr)
                *ended = true;
            return legs;
        }
        used.insert(key(track, forward));
        const float trackLength = t.length(track);
        start = std::clamp(start, 0.0f, trackLength);
        const float room = forward ? trackLength - start : start;
        Leg leg;
        leg.track = track;
        leg.from = start;
        leg.broken = false;
        if (room >= left) {
            leg.to = forward ? start + left : start - left;
            legs.push_back(leg);
            return legs;
        }
        leg.to = forward ? trackLength : 0.0f;
        legs.push_back(leg);
        left -= room;
        const int far = forward ? 1 : 0;
        const int atId = vector->pins[far].link;
        const TRnode *at = trackNode(tdb, atId);
        const int entry = isJunction(at) ? pinOf(at, track, far) : -1;
        const int exit = entry < 0 ? -1 : entry < at->inputPinCount ? mainPin(tdb, at) : 0;
        const int next = exit >= 0 ? at->pins[exit].link : -1;
        const TRnode *nextVector = trackNode(tdb, next);
        if (entry < 0 || exit == entry || !isVector(nextVector)) {
            if (ended != nullptr)
                *ended = true;
            return legs;
        }
        const bool nextForward = endOfPin(at, atId, exit, nextVector) == 0;
        if (used.contains(key(next, nextForward)))
            return legs;
        track = next;
        forward = nextForward;
        start = forward ? 0.0f : t.length(next);
    }
}

QMap<int, int> junctionChoices(TDB *tdb, const QVector<Node> &nodes, const QVector<Leg> &legs) {
    QMap<int, int> choices;
    for (int k = 1; k + 1 < nodes.size(); ++k) {
        if (nodes[k].type != NodeType::Junction || nodes[k].broken)
            continue;
        const TRnode *junction = trackNode(tdb, nodes[k].track);
        if (!isJunction(junction))
            continue;
        const int entry = entryPin(tdb, nodes, legs, k);
        const int exit = exitPin(tdb, nodes, legs, k);
        if (entry >= 0 && entry < junction->inputPinCount && exit >= junction->inputPinCount)
            choices[nodes[k].track] = exit - junction->inputPinCount;
    }
    return choices;
}

int waitNode(const QVector<Node> &nodes, const TrackPoint &point) {
    float best = PointReach;
    int found = -1;
    for (int k = 0; k < nodes.size(); ++k) {
        const Node &node = nodes[k];
        if (node.type != NodeType::Wait || node.broken || node.track != point.track)
            continue;
        const float d = std::fabs(node.distance - point.distance);
        if (d <= best) {
            best = d;
            found = k;
        }
    }
    return found;
}

Result removeLastReverse(TDB *tdb, const QVector<Node> &nodes) {
    Track t{tdb, {}};
    int last = -1;
    for (int k = nodes.size() - 1; k > 0 && last < 0; --k)
        if (nodes[k].type == NodeType::Reverse)
            last = k;
    if (last < 0)
        return fail(Note::NoReverse);
    const Node &reverse = nodes[last];
    if (reverse.broken || !isVector(trackNode(tdb, reverse.track)))
        return fail(Note::NoTrack);
    // A reversal leaves in its forward direction; the path came the other way.
    const bool came = !reverse.forward;
    QVector<Node> result = nodes.mid(0, last);
    const Note note = complete(t, result, reverse.track, came, reverse.distance);
    return done(result, note);
}

Result waitAt(TDB *tdb, const QVector<Node> &nodes, const TrackPoint &point, int seconds) {
    Track t{tdb, {}};
    if (nodes.isEmpty())
        return fail(Note::NoPath);
    if (!validPoint(t, point))
        return fail(Note::NoTrack);
    const QVector<Leg> path = legsOf(t, nodes);
    const int k = legAt(path, point, true);
    if (k < 0)
        return fail(Note::NotOnPath);
    Node wait = t.point(NodeType::Wait, point.track, point.distance, path[k].forward());
    wait.waitTime = std::clamp(seconds, 0, MaxWaitTime);
    QVector<Node> result = nodes;
    result.insert(k + 1, wait);
    return done(result, Note::None);
}

Result setWaitTime(TDB *, const QVector<Node> &nodes, int index, int seconds) {
    if (index < 0 || index >= nodes.size() || nodes[index].type != NodeType::Wait)
        return fail(Note::NoWait);
    QVector<Node> result = nodes;
    result[index].waitTime = std::clamp(seconds, 0, MaxWaitTime);
    result[index].fileFlags = -1;
    return done(result, Note::None);
}

Result removeWait(TDB *tdb, const QVector<Node> &nodes, int index) {
    Track t{tdb, {}};
    if (index < 0 || index >= nodes.size() || nodes[index].type != NodeType::Wait)
        return fail(Note::NoWait);
    const Node wait = nodes[index];
    QVector<Node> result = nodes;
    result.removeAt(index);
    // The wait told two tracks between two junctions apart: a halfway node
    // does instead.
    if (index > 0 && index < result.size() && result[index - 1].type == NodeType::Junction
            && result[index].type == NodeType::Junction && !result[index - 1].broken
            && !result[index].broken && !wait.broken
            && tracksBetween(tdb, result[index - 1].track, result[index].track) > 1)
        result.insert(index, t.point(NodeType::Halfway, wait.track, 0.5f * t.length(wait.track),
                                     wait.forward));
    return done(result, Note::None);
}

namespace {

// The nodes other than halfway nodes, and where each is in the full list.
// Files leave out some halfway nodes between two junctions joined twice and
// keep some strays; the edits add and drop them by their rule.
QVector<Node> withoutHalfways(const QVector<Node> &nodes, QVector<int> *where = nullptr) {
    QVector<Node> kept;
    for (int k = 0; k < nodes.size(); ++k)
        if (nodes[k].type != NodeType::Halfway) {
            kept << nodes[k];
            if (where != nullptr)
                *where << k;
        }
    return kept;
}

// The tracks from each node other than a halfway node to the next one.
QVector<QVector<int>> tracksBetweenNodes(TDB *tdb, const QVector<Node> &nodes) {
    const QVector<Leg> path = legs(tdb, nodes);
    QVector<QVector<int>> result;
    for (int k = 0; k < nodes.size(); ++k) {
        if (nodes[k].type != NodeType::Halfway)
            result << QVector<int>();
        if (k < path.size() && (result.back().isEmpty() || result.back().back() != path[k].track))
            result.back() << path[k].track;
    }
    return result;
}

bool sameNode(const Node &a, const Node &b) {
    if (a.type != b.type || a.track != b.track)
        return false;
    if (a.type == NodeType::Wait && a.waitTime != b.waitTime)
        return false;
    return !a.isPoint() || std::fabs(a.distance - b.distance) <= 1.0f;
}

// Where two paths part, halfway nodes left out: the first node that
// differs, or that the paths reach over other tracks; -1 when the same.
int firstDifference(TDB *tdb, const QVector<Node> &ours, const QVector<Node> &target) {
    const QVector<Node> a = withoutHalfways(ours);
    const QVector<Node> b = withoutHalfways(target);
    const QVector<QVector<int>> tracksA = tracksBetweenNodes(tdb, ours);
    const QVector<QVector<int>> tracksB = tracksBetweenNodes(tdb, target);
    const int count = std::min(a.size(), b.size());
    for (int k = 0; k < count; ++k) {
        if (!sameNode(a[k], b[k]))
            return k;
        if (k > 0 && tracksA[k - 1] != tracksB[k - 1])
            return k;
    }
    return a.size() == b.size() ? -1 : count;
}

bool isAnchor(const Node &node) {
    return node.type == NodeType::Reverse || node.type == NodeType::Wait
            || node.type == NodeType::End;
}

// The edit that places a reversal, wait or end at a node's place.
Result atAnchor(TDB *tdb, const QVector<Node> &nodes, const Node &anchor) {
    const TrackPoint point{anchor.track, anchor.distance};
    switch (anchor.type) {
    case NodeType::End: return endAt(tdb, nodes, point);
    case NodeType::Reverse: return reverseAt(tdb, nodes, point);
    case NodeType::Wait: return waitAt(tdb, nodes, point, anchor.waitTime);
    default: return Result();
    }
}

}

// One rebuild, the start turned from the nodes' direction when asked.
static Result rebuildFrom(TDB *tdb, QVector<Node> target, bool repair, bool turned) {
    Track t{tdb, {}};
    Result built = startAt(tdb, {target[0].track, target[0].distance});
    if (built.ok && (built.nodes[0].forward != target[0].forward) != turned)
        built = turnStart(tdb, built.nodes);
    if (!built.ok)
        return built;
    bool rerouted = false;
    bool stopped = false;
    for (int step = 0; step < 4000; ++step) {
        const int k = firstDifference(tdb, built.nodes, target);
        if (k < 0)
            break;
        // The edit a user would make there: the point's own edit, Take
        // Other Direction at the junction before it, or both; End Here at
        // the end (past a loop completion stopped at). The first that gets
        // further.
        QVector<int> where, whereTarget;
        withoutHalfways(built.nodes, &where);
        const QVector<Node> wanted = withoutHalfways(target, &whereTarget);
        QVector<Result> tries;
        if (k < wanted.size() && isAnchor(wanted[k]))
            tries << atAnchor(tdb, built.nodes, wanted[k]);
        if (k > 0 && k - 1 < where.size()) {
            const Result other = otherExit(tdb, built.nodes, where[k - 1]);
            tries << other;
            if (other.ok && k < wanted.size() && isAnchor(wanted[k]))
                tries << atAnchor(tdb, other.nodes, wanted[k]);
        }
        if (wanted.back().type == NodeType::End)
            tries << endAt(tdb, built.nodes, {wanted.back().track, wanted.back().distance});
        bool progressed = false;
        for (const Result &attempt : tries) {
            if (!attempt.ok)
                continue;
            const int next = firstDifference(tdb, attempt.nodes, target);
            if (next < 0 || next > k) {
                built = attempt;
                progressed = true;
                break;
            }
        }
        if (progressed)
            continue;
        if (!repair) {
            built.ok = false;
            built.note = Note::NoRoute;
            return built;
        }
        // Repair: route to the next reversal, wait or end, and follow the
        // nodes on from there.
        int anchor = -1;
        for (int a = k; a < wanted.size() && anchor < 0; ++a)
            if (isAnchor(wanted[a]))
                anchor = a;
        if (anchor < 0) {
            stopped = true;
            break;
        }
        const Node &to = wanted[anchor];
        Result routed = endAt(tdb, built.nodes, {to.track, to.distance});
        if (routed.ok && to.type != NodeType::End) {
            // The end the route made becomes the reversal or wait.
            Node &end = routed.nodes.back();
            end.type = to.type;
            end.waitTime = to.waitTime;
            end.fileFlags = -1;
            if (to.type == NodeType::Reverse)
                end.forward = !end.forward;
            complete(t, routed.nodes, end.track, end.forward, end.distance);
            dropStrayHalfways(routed.nodes);
        }
        if (!routed.ok) {
            built.note = Note::RepairStopped;
            return built;
        }
        built = routed;
        rerouted = true;
        // From here on, the nodes after the anchor.
        int reached = -1;
        for (int b = built.nodes.size() - 1; b >= 0 && reached < 0; --b)
            if (sameNode(built.nodes[b], to))
                reached = b;
        if (reached < 0)
            break;
        target = built.nodes.mid(0, reached + 1) + target.mid(whereTarget[anchor] + 1);
    }
    built.ok = true;
    built.note = stopped ? Note::RepairStopped : rerouted ? Note::Rerouted : Note::None;
    return built;
}

Result rebuild(TDB *tdb, const QVector<Node> &nodes, bool repair) {
    QVector<Node> target;
    for (const Node &node : nodes) {
        if (!node.broken)
            target << node;
        else if (!repair)
            return fail(Note::NoTrack);
    }
    if (target.isEmpty() || target[0].type != NodeType::Start)
        return fail(Note::NoPath);
    Result built = rebuildFrom(tdb, target, repair, false);
    // A start whose track to the next node broke may face the wrong way:
    // the other way, when it gets further.
    if (repair && built.ok && built.note == Note::RepairStopped) {
        Result turned = rebuildFrom(tdb, target, repair, true);
        if (turned.ok && (turned.note != Note::RepairStopped
                          || turned.nodes.size() > built.nodes.size()))
            built = turned;
    }
    return built;
}

void dropStrayHalfways(QVector<Node> &nodes) {
    for (int k = nodes.size() - 1; k >= 0; --k) {
        if (nodes[k].type != NodeType::Halfway)
            continue;
        const bool between = k > 0 && k + 1 < nodes.size()
                && nodes[k - 1].type == NodeType::Junction
                && nodes[k + 1].type == NodeType::Junction;
        if (!between)
            nodes.removeAt(k);
    }
}

}
