#include <tsre/tests/TdbEditingTestSuite.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/Undo.h>
#include <tsre/Game.h>
#include <tsre/world/Route.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <QDebug>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace {
class Database : public TDB {
public:
    using TDB::TDB;
    using TDB::addItemToTrNode;
    using TDB::deleteItemFromTrNode;
    using TDB::road;
    std::vector<int> itemUpdates;
    void updateTrItem(int id) override { itemUpdates.push_back(id); }
};

QString snapshot(TDB &db, const std::vector<int> &ids) {
    QString text;
    QTextStream stream(&text);
    stream.setRealNumberPrecision(9);
    for(int id : ids) {
        auto it = db.trackNodes.find(id);
        if(it != db.trackNodes.end() && it->second)
            it->second->saveToStream(stream, id);
    }
    return text;
}

std::vector<int> liveIds(TDB &db) {
    std::vector<int> ids;
    for(const auto &entry : db.trackNodes)
        if(entry.second) ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool validLinks(TDB &db) {
    for(const auto &entry : db.trackNodes) {
        const auto *node = entry.second;
        if(!node) continue;
        if(node->typ == 1 && (node->iTrv <= 0 || !node->trVectorSection))
            return false;
        for(int p = 0; p < node->inputPinCount + node->outputPinCount; ++p) {
            int target = node->pins[p].link;
            if(!target) continue; // Vacant junction branch.
            auto it = db.trackNodes.find(target);
            if(it == db.trackNodes.end() || !it->second) return false;
            bool reciprocal = false;
            for(int q = 0; q < it->second->inputPinCount + it->second->outputPinCount; ++q)
                reciprocal |= it->second->pins[q].link == entry.first;
            if(!reciprocal) return false;
        }
    }
    return true;
}

struct Fixture {
    TSectionDAT definitions{false, false};
    TSection straight{10, 0, 10.0f, 0.0f};
    Database db;
    std::vector<int> sentinelIds;
    QString sentinelBefore;

    explicit Fixture(bool road) : db(&definitions, road) {
        definitions.sekcja[10] = &straight;
        int sentinel = path(3, 1000, 500.0f, 800.0f);
        item(sentinel, 100, 15);
        sentinelIds = {db.trackNodes[sentinel]->pins[0].link, sentinel,
                       db.trackNodes[sentinel]->pins[1].link};
        sentinelBefore = snapshot(db, sentinelIds);
    }

    int path(int count, int uid, float x = 0, float z = 0) {
        float position[3] = {x, 0, z};
        float frame[3] = {0, 0, 0};
        int ends[2] = {0, 1};
        int last = db.newTrack(0, 0, position, frame, ends, 20, 10, uid);
        int vector = db.trackNodes[last]->pins[0].link;
        for(int i = 1; i < count; ++i)
            last = db.appendTrack(last, ends, 20, 10, uid + i);
        return vector;
    }

    void item(int vector, int id, float distance) {
        db.trackItems[id] = TRitem::newPickupItem(id, distance);
        db.iTRitems = std::max(db.iTRitems, id + 1);
        db.addItemToTrNode(vector, id);
    }

    bool sentinelUnchanged() {
        return snapshot(db, sentinelIds) == sentinelBefore
            && db.trackItems[100]->type == "pickupitem"
            && db.trackItems[100]->getTrackPosition() == 15;
    }
};

bool reloadEquivalent(Fixture &f) {
    QString text;
    QTextStream out(&text);
    out.setRealNumberPrecision(6);
    f.db.saveToStream(out);
    auto bytes = new unsigned char[text.size() * 2];
    for(int i = 0; i < text.size(); ++i) {
        bytes[i*2] = text[i].unicode() & 255;
        bytes[i*2+1] = text[i].unicode() >> 8;
    }
    FileBuffer data(bytes, text.size() * 2);
    ParserX::NextTokenInside(&data);
    Database reloaded(&f.definitions, f.db.road);
    reloaded.loadUtf16Data(&data);
    if(liveIds(reloaded) != liveIds(f.db) || !validLinks(reloaded)) return false;
    for(int id : liveIds(f.db)) {
        auto *a = f.db.trackNodes[id];
        auto *b = reloaded.trackNodes[id];
        if(a->typ != b->typ || a->iTrv != b->iTrv || a->iTri != b->iTri) return false;
        for(int i = 0; i < a->iTrv; ++i)
            if(a->trVectorSection[i].worldObjectId != b->trVectorSection[i].worldObjectId
                    || a->trVectorSection[i].sectionIndex != b->trVectorSection[i].sectionIndex) return false;
        for(int i = 0; i < a->iTri; ++i) {
            if(a->trItemRef[i] != b->trItemRef[i]) return false;
            int item = a->trItemRef[i];
            if(!reloaded.trackItems[item] || f.db.trackItems[item]->getTrackPosition()
                    != reloaded.trackItems[item]->getTrackPosition()) return false;
        }
        for(int i = 0; i < a->inputPinCount + a->outputPinCount; ++i)
            if(a->pins[i].link != b->pins[i].link || a->pins[i].direction != b->pins[i].direction) return false;
    }
    return true;
}

bool itemAt(TDB &db, int id, int node, float distance) {
    return db.findTrItemNodeId(id) == node
            && std::fabs(db.trackItems[id]->getTrackPosition() - distance) < 1e-5f;
}

int sectionsWithOwner(TDB &db, int uid) {
    int count = 0;
    for(const auto &entry : db.trackNodes) {
        auto *n = entry.second;
        if(n && n->typ == 1)
            for(int s = 0; s < n->iTrv; ++s)
                count += n->trVectorSection[s].worldObjectId == uid;
    }
    return count;
}
}

int TsreTests::runTdbEditingSuite() {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name, bool road) {
        if(ok) ++passed;
        else { ++failed; qWarning() << "[tests:tdb-editing] FAIL" << name << "road" << road; }
    };
    for(bool road : {false, true}) {
        // Every orientation uses the same physical 0..40 m path. Items must
        // remain at their world positions even when a join reverses a side.
        for(int orientation = 0; orientation < 4; ++orientation) {
            Fixture f(road);
            int a = f.path(2, 100);
            int b = f.path(2, 200, 0, -20);
            f.item(a, 0, 5);
            f.item(b, 1, 15);
            if(orientation & 1) f.db.rotate(a);
            if(orientation & 2) f.db.rotate(b);
            bool ok = f.db.joinVectorSections(a, b) == 0;
            int survivor = f.db.trackNodes[a] ? a : b;
            auto *node = f.db.trackNodes[survivor];
            float first[7] = {}, second[7] = {};
            ok = ok && node && node->iTrv == 4 && node->iTri == 2
                    && std::fabs(f.db.getVectorSectionLength(survivor) - 40) < 1e-5f
                    && f.db.getDrawPositionOnTrNode(first, survivor, f.db.trackItems[0]->getTrackPosition())
                    && f.db.getDrawPositionOnTrNode(second, survivor, f.db.trackItems[1]->getTrackPosition())
                    && std::fabs(first[2] - 5) < 0.001f
                    && std::fabs(second[2] - 35) < 0.001f;
            check(ok && validLinks(f.db) && f.sentinelUnchanged(), "join orientation and item positions", road);
        }
        for(int cut : {1, 2}) {
            Fixture f(road);
            int original = f.path(3, 100);
            f.item(original, 0, 5);
            f.item(original, 1, cut * 10);
            f.item(original, 2, 29);
            int right = f.db.splitVectorSection(original, cut);
            check(f.db.trackNodes[original]->iTrv == cut
                    && f.db.trackNodes[right]->iTrv == 3 - cut
                    && itemAt(f.db, 0, original, 5)
                    && itemAt(f.db, 1, right, 0)
                    && itemAt(f.db, 2, right, 29 - cut * 10)
                    && validLinks(f.db) && f.sentinelUnchanged(), "split boundary ownership", road);
            f.db.joinVectorSections(original, right);
            check(f.db.trackNodes[original]->iTrv == 3
                    && itemAt(f.db, 1, original, cut * 10)
                    && itemAt(f.db, 2, original, 29)
                    && validLinks(f.db) && f.sentinelUnchanged(), "split then rejoin", road);
        }
        for(bool loop : {false, true}) {
            Fixture f(road);
            int vector = f.path(1, 100);
            auto *node = f.db.trackNodes[vector];
            const int left = node->pins[0].link;
            const int right = node->pins[1].link;
            auto *junction = f.db.trackNodes[left];
            junction->typ = 2;
            junction->outputPinCount = 2;
            if(loop) {
                delete f.db.trackNodes[right];
                f.db.trackNodes[right] = nullptr;
                node->pins[1].link = left;
                junction->pins[1] = {vector, 0};
            }
            f.db.deleteVectorSection(vector);
            check(f.db.trackNodes[left] == junction && junction->typ == 2
                && !junction->isLikedTo(vector) && !f.db.trackNodes[vector]
                && validLinks(f.db) && f.sentinelUnchanged(),
                "delete vector attached to junction, including loop", road);
        }
        {
            Fixture f(road);
            int vector = f.path(1, 100);
            const int left = f.db.trackNodes[vector]->pins[0].link;
            int ends[2] = {0, 1};
            f.db.appendTrack(left, ends, 20, 10, 99);
            auto *node = f.db.trackNodes[vector];
            check(node->iTrv == 2 && node->trVectorSection[0].worldObjectId == 99
                && node->trVectorSection[1].worldObjectId == 100
                && std::fabs(f.db.getVectorSectionLength(vector) - 20) < 0.001f
                && validLinks(f.db) && f.sentinelUnchanged(), "prepend section", road);
        }
        for(int part : {0, 1, 2}) {
            Fixture f(road);
            int vector = f.path(3, 100);
            f.item(vector, 0, 5);
            f.item(vector, 1, 15);
            f.item(vector, 2, 25);
            bool removed = f.db.removeTrackFromTDB(0, 0, 100 + part);
            bool othersRemain = true;
            for(int other = 0; other < 3; ++other)
                othersRemain &= sectionsWithOwner(f.db, 100 + other) == (other == part ? 0 : 1);
            bool updatesValid = std::all_of(f.db.itemUpdates.begin(), f.db.itemUpdates.end(),
                                          [](int id) { return id >= 0 && id <= 2; });
            check(removed && othersRemain && updatesValid
                    && f.db.trackItems[part]->type == "emptyitem"
                    && validLinks(f.db) && f.sentinelUnchanged(), "delete first/middle/last subsection", road);
            check(reloadEquivalent(f), "edited graph save and reload", road);
        }
        {
            Fixture f(road);
            int vector = f.path(5, 100);
            auto *node = f.db.trackNodes[vector];
            node->trVectorSection[1].worldObjectId = 100;
            node->trVectorSection[3].worldObjectId = 100;
            check(f.db.removeTrackFromTDB(0, 0, 100)
                && sectionsWithOwner(f.db, 100) == 0
                && sectionsWithOwner(f.db, 102) == 1 && sectionsWithOwner(f.db, 104) == 1
                && validLinks(f.db) && f.sentinelUnchanged() && reloadEquivalent(f),
                "remove consecutive and separated sections with selected owner", road);
        }
        {
            Fixture f(road);
            int vector = f.path(1, 100);
            int left = f.db.trackNodes[vector]->pins[0].link;
            int right = f.db.trackNodes[vector]->pins[1].link;
            check(f.db.removeTrackFromTDB(0, 0, 100)
                    && !f.db.trackNodes[vector] && !f.db.trackNodes[left] && !f.db.trackNodes[right]
                    && validLinks(f.db) && f.sentinelUnchanged(), "delete only subsection", road);
        }
        {
            Fixture f(road);
            int vector = f.path(2, 100);
            f.item(vector, 0, 1);
            f.item(vector, 1, 2);
            f.db.deleteFromVectorSection(vector, 0);
            check(f.db.trackNodes[vector]->iTri == 0
                    && f.db.itemUpdates == std::vector<int>({0, 1})
                    && f.sentinelUnchanged(), "delete every item in first subsection", road);
        }
        {
            Fixture f(road);
            int vector = f.path(2, 100);
            f.item(vector, 0, 1);
            f.db.addItemToTrNode(vector, 0);
            f.item(vector, 1, 15);
            f.db.deleteTrItem(0);
            check(f.db.trackNodes[vector]->iTri == 1
                    && f.db.trackNodes[vector]->trItemRef[0] == 1
                    && itemAt(f.db, 1, vector, 15) && f.sentinelUnchanged(), "duplicate item removal", road);
            f.db.deleteItemFromTrNode(vector, 999);
            check(f.db.trackNodes[vector]->iTri == 1
                    && f.db.trackNodes[vector]->trItemRef[0] == 1, "absent item removal is a no-op", road);
        }
        {
            Fixture f(road);
            int vector = f.path(3, 100);
            f.item(vector, 0, 5);
            const auto ids = liveIds(f.db);
            const auto before = snapshot(f.db, ids);
            Route route;
            auto *oldRoute = Game::currentRoute;
            auto *oldRail = Game::trackDB;
            auto *oldRoad = Game::roadDB;
            Game::currentRoute = &route;
            // Exercise the production snapshot and restore hooks, without
            // loading route content or handing ownership of the fixture away.
            Undo::StateBegin();
            Undo::PushTrackDB(&f.db, road);
            f.db.deleteFromVectorSection(vector, 1);
            Undo::StateEnd();
            Undo::UndoLast();
            TDB *undo = road ? Game::roadDB : Game::trackDB;
            check(undo && snapshot(*undo, ids) == before
                    && itemAt(*undo, 0, vector, 5)
                    && undo->trackNodes[vector] != f.db.trackNodes[vector]
                    && validLinks(*undo) && f.sentinelUnchanged(), "undo snapshot survives editing", road);
            Undo::Clear();
            delete undo;
            Game::currentRoute = oldRoute;
            Game::trackDB = oldRail;
            Game::roadDB = oldRoad;
        }
        {
            Fixture f(road);
            int vector = f.path(3, 100);
            f.item(vector, 0, 5);
            f.db.rotate(vector);
            f.db.rotate(vector);
            // Angles may use an equivalent turn; sample geometry separately.
            float p[7] = {};
            check(f.db.getDrawPositionOnTrNode(p, vector, 25) && std::fabs(p[2] - 25) < 0.001f
                    && itemAt(f.db, 0, vector, 5) && validLinks(f.db)
                    && sectionsWithOwner(f.db, 100) == 1 && f.sentinelUnchanged(), "reverse twice", road);
        }
    }
    qInfo() << "[tests:tdb-editing] passed=" << passed << "failed=" << failed;
    return failed ? 1 : 0;
}
