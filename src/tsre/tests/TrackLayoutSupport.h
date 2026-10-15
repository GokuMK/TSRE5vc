#pragma once
// A small track database built by hand for tests (path editing, the
// simulation): end nodes, junctions with their main route, vector nodes of
// 10 m straight sections laid along x, platforms.
#include <QString>
#include <QVector>
#include <algorithm>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tdb/TrackShape.h>

namespace TestTrack {

// Ids for the test's own section and track shapes, clear of the global
// tsection.dat's.
constexpr int SectionId = 900000;
constexpr int ShapeMainFirst = 900000;
constexpr int ShapeMainSecond = 900001;

// A small track database built by hand: end nodes, junctions with their
// main route, and vector nodes of 10 m sections laid along x.
struct Layout {
    TSectionDAT sections{false, false};
    TSection piece{SectionId, 0, 10.0f, 0.0f};
    TrackShape mainFirst{ShapeMainFirst};
    TrackShape mainSecond{ShapeMainSecond};
    TDB db{&sections, false};
    float nextX = 0.0f;

    Layout() {
        sections.sekcja[SectionId] = &piece;
        mainFirst.mainroute = 0;
        mainSecond.mainroute = 1;
        sections.shape[ShapeMainFirst] = &mainFirst;
        sections.shape[ShapeMainSecond] = &mainSecond;
        db.loaded = true;
    }
    ~Layout() {
        sections.sekcja.erase(SectionId);
        sections.shape.erase(ShapeMainFirst);
        sections.shape.erase(ShapeMainSecond);
    }

    TRnode *add(int id, int type, int in, int out) {
        TRnode *node = new TRnode();
        node->typ = type;
        node->inputPinCount = in;
        node->outputPinCount = out;
        db.trackNodes[id] = node;
        db.iTRnodes = std::max(db.iTRnodes, id);
        return node;
    }
    void end(int id, int track) { add(id, 0, 1, 0)->pins[0].link = track; }
    // A junction: its input pin and two outputs, and which output is main.
    void junction(int id, int in, int first, int second, bool secondIsMain = false) {
        TRnode *node = add(id, 2, 1, 2);
        node->pins[0].link = in;
        node->pins[1].link = first;
        node->pins[2].link = second;
        node->junction.shapeIndex = secondIsMain ? ShapeMainSecond : ShapeMainFirst;
    }
    // A vector node from node `from` (its distance 0) to node `to`.
    void track(int id, int from, int to, int pieces) {
        TRnode *node = add(id, 1, 1, 1);
        node->pins[0].link = from;
        node->pins[1].link = to;
        node->iTrv = pieces;
        node->trVectorSection = new TrackVectorSection[pieces];
        for (int i = 0; i < pieces; ++i) {
            node->trVectorSection[i].sectionIndex = SectionId;
            node->trVectorSection[i].x = nextX;
            nextX += 10.0f;
        }
    }
};

// The passing loop: track end 1, track 10 to junction 2, its main output
// track 11 and its other output track 12 (laid the other way) to junction 3,
// track 13 to track end 4.
inline void passingLoop(Layout &l) {
    l.end(1, 10);
    l.track(10, 1, 2, 10);
    l.junction(2, 10, 11, 12);
    l.track(11, 2, 3, 20);
    l.track(12, 3, 2, 22);
    l.junction(3, 13, 11, 12);
    l.track(13, 3, 4, 10);
    l.end(4, 13);
}

// A platform's two items on the track, `from` and `to` metres along it.
inline void platform(Layout &l, int track, int first, float from, float to, const QString &name) {
    TRnode *node = l.db.trackNodes[track];
    QVector<int> refs;
    for (int i = 0; i < node->iTri; ++i)
        refs.push_back(node->trItemRef[i]);
    for (int k = 0; k < 2; ++k) {
        TRitem *item = TRitem::newPlatformItem(first + k, k == 0 ? from : to);
        item->platformTrItemData[1] = first + 1 - k;
        item->platformName = name;
        item->stationName = name;
        l.db.trackItems[first + k] = item;
        refs.push_back(first + k);
    }
    delete[] node->trItemRef;
    node->iTri = refs.size();
    node->trItemRef = new int[refs.size()];
    for (int i = 0; i < refs.size(); ++i)
        node->trItemRef[i] = refs[i];
}

}
