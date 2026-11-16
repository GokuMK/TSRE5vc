/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tests/TrackCantTestSuite.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>
#include <tsre/Game.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/shape/SFileDocument.h>
#include <tsre/shape/ShapeLib.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TrackCant.h>
#include <tsre/trains/EngLib.h>
#include <tsre/world/Route.h>

namespace {

struct Timing {
    double median = 0;
    double max = 0;
};

Timing timed(int runs, const std::function<void()> &work) {
    std::vector<double> ms;
    for (int i = 0; i < runs; ++i) {
        QElapsedTimer timer;
        timer.start();
        work();
        ms.push_back(double(timer.nsecsElapsed()) / 1e6);
    }
    std::sort(ms.begin(), ms.end());
    return {ms[ms.size() / 2], ms.back()};
}

void report(const char *what, const Timing &t) {
    std::cout << "[tests:track-cant-benchmark] " << what << ": median " << t.median << " ms, max " << t.max
              << " ms\n";
}

}

// Task tracks 16: what the superelevation costs the editor on a real route
// (--game-root, --route): a whole computation, and the update an edit
// makes each frame (Route::refreshTrackCant).
int TsreTests::runTrackCantBenchmark(bool verbose) {
    Q_UNUSED(verbose);
    if (!Game::checkRoot(Game::root) || !Game::checkRoute(Game::route)) {
        std::cout << "[tests:track-cant-benchmark] needs --game-root and --route\n";
        return 1;
    }
    Game::gui = false;
    Game::consoleOutput = true;
    Game::currentShapeLib = new ShapeLib();
    Game::currentEngLib = new EngLib();
    Route *route = new Route();
    route->load();
    if (!route->loaded || Game::trackDB == nullptr) {
        std::cout << "[tests:track-cant-benchmark] route failed to load\n";
        return 1;
    }
    TDB &tdb = *Game::trackDB;
    const TrackCant::Route &cant = route->trackCant;

    int nodes = 0, sections = 0, posts = 0;
    std::vector<int> postItems;
    int curvedNode = -1;
    for (const auto &entry : tdb.trackNodes) {
        const TRnode *node = entry.second;
        if (node == nullptr || node->typ != 1 || entry.first <= 0)
            continue;
        ++nodes;
        sections += node->iTrv;
        for (int i = 0; i < node->iTri; ++i) {
            const auto item = tdb.trackItems.find(node->trItemRef[i]);
            if (item != tdb.trackItems.end() && item->second != nullptr && item->second->type == "speedpostitem") {
                ++posts;
                postItems.push_back(node->trItemRef[i]);
            }
        }
        const auto tables = tdb.cant.nodes.find(entry.first);
        if (curvedNode < 0 && tables != tdb.cant.nodes.end())
            for (const TrackCant::Section &s : tables->second)
                if (s.points > 0)
                    curvedNode = entry.first;
    }
    std::cout << "[tests:track-cant-benchmark] route=" << Game::route.toStdString() << " nodes=" << nodes
              << " sections=" << sections << " speed post items=" << posts << " curves=" << tdb.cant.curves
              << "\n";

    report("whole computation (computeAll)", timed(5, [&]() { TrackCant::computeAll(tdb, cant); }));
    // A dragged speed post object reports its items every frame, unchanged.
    if (!postItems.empty()) {
        const std::set<int> dragged = {postItems.front()};
        bool changed = false;
        report("a speed post object dragged, each frame (postsChanged)",
               timed(200, [&]() { changed = changed || TrackCant::postsChanged(tdb, dragged); }));
        if (changed)
            std::cout << "[tests:track-cant-benchmark] FAIL: an unchanged speed post read as changed\n";
    }
    report("update, nothing changed", timed(20, [&]() { TrackCant::update(tdb, cant, {}); }));
    report("update, one curved node changed (a track edit)",
           timed(20, [&]() { TrackCant::update(tdb, cant, {curvedNode}); }));
    report("  of which the speed post scan (SpeedScan)", timed(20, [&]() { TrackCant::SpeedScan scan(tdb); }));
    report("  of which the owner index (indexOwners)", timed(20, [&]() { tdb.cant.indexOwners(tdb); }));
    tdb.cant.rebuild.clear();

    // A speed post's speed changed: the first whose change moves some cant.
    for (int id : postItems) {
        TRitem *item = tdb.trackItems[id];
        if (item->speedpostTrItemData == nullptr || item->getSpeedpostType() != TRitem::SIGN)
            continue;
        const float was = item->getSpeedpostSpeed();
        item->setSpeedpostSpeed(was + 40);
        QElapsedTimer timer;
        timer.start();
        const std::set<int> computed = TrackCant::update(tdb, cant, {});
        const double ms = double(timer.nsecsElapsed()) / 1e6;
        const size_t rebuilt = tdb.cant.rebuild.size();
        item->setSpeedpostSpeed(was);
        TrackCant::update(tdb, cant, {});
        tdb.cant.rebuild.clear();
        if (rebuilt == 0)
            continue;
        std::cout << "[tests:track-cant-benchmark] a speed post's speed changed (item " << id << "): " << ms
                  << " ms, " << computed.size() << " nodes computed again, " << rebuilt << " objects to rebuild\n";
        break;
    }

    // A track profile's guess reads only a shape file's image list
    // (decision 14), against parsing all its blocks (a load then also
    // extracts its meshes, uploads them and loads its textures). Each file
    // read once before, so both come from the page cache.
    const QDir shapes(Game::root + "/GLOBAL/SHAPES");
    const QStringList files = shapes.entryList({"A1t*.s", "A2t*.s"}, QDir::Files);
    double listMs = 0, wholeMs = 0;
    qint64 bytes = 0;
    for (const QString &name : files) {
        const QString path = shapes.filePath(name);
        QFile warm(path);
        if (warm.open(QIODevice::ReadOnly))
            bytes += warm.readAll().size();
        QElapsedTimer timer;
        timer.start();
        SFileComplex::readImageNames(path);
        listMs += double(timer.nsecsElapsed()) / 1e6;
        timer.restart();
        SFileDetail::Document whole;
        whole.read(path);
        wholeMs += double(timer.nsecsElapsed()) / 1e6;
    }
    if (!files.isEmpty())
        std::cout << "[tests:track-cant-benchmark] " << files.size() << " track shapes (" << bytes / 1024
                  << " KiB): image list " << listMs << " ms, all blocks parsed " << wholeMs << " ms\n";
    return 0;
}
