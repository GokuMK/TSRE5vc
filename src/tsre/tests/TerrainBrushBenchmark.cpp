#include <tsre/tests/TerrainBrushBenchmark.h>
#include <tsre/tests/TerrainBrushTestSupport.h>
#include <tsre/world/TerrainHeightBrush.h>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/texture/Brush.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainInfo.h>
#include <tsre/world/TerrainLibQt.h>
#include <tsre/world/QuadTree.h>
#include <tsre/world/Trk.h>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QOpenGLContext>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
// Use the real QuadTree registry and unmodified painting implementation.
// Test support records per-tile notifications and cleans up the fixture;
// terrain lookup and the original painting implementation are not overridden.
void report(const QString &position, const QString &phase, QVector<double> times,
            int affected) {
    const double average = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    std::sort(times.begin(), times.end());
    const int n = times.size();
    const double median = (times[(n - 1) / 2] + times[n / 2]) / 2;
    qInfo().noquote() << QString("[benchmark:terrain-brush] position=%1 phase=%2 runs=%3 "
                                "tiles=%4 mean_ms=%5 median_ms=%6 p95_ms=%7 min_ms=%8 max_ms=%9")
        .arg(position, phase).arg(n).arg(affected)
        .arg(average, 0, 'f', 3).arg(median, 0, 'f', 3)
        .arg(times[int(std::ceil(n * 0.95)) - 1], 0, 'f', 3)
        .arg(times.first(), 0, 'f', 3).arg(times.last(), 0, 'f', 3);
}
}

int TsreTests::runTerrainBrushBenchmark(const TestRunOptions &opts) {
    const QDir routeDirectory(QDir::cleanPath(opts.casesFile));
    const auto trkFiles = routeDirectory.entryList({"*.trk"}, QDir::Files);
    if (opts.casesFile.isEmpty() || trkFiles.size() != 1
            || QOpenGLContext::currentContext() != nullptr || Undo::IsStateOpen()) {
        qWarning() << "[benchmark:terrain-brush] requires --test-cases <route directory>"
                      " with one TRK, no GL context and no active undo state";
        return 2;
    }
    QDir rootDirectory(routeDirectory);
    rootDirectory.cdUp();
    rootDirectory.cdUp();
    QScopedValueRollback<QString> root(Game::root, rootDirectory.absolutePath());
    QScopedValueRollback<QString> route(Game::route, routeDirectory.dirName());
    QScopedValueRollback<bool> write(Game::writeEnabled, false);
    QScopedValueRollback<bool> gui(Game::gui, false);
    QScopedValueRollback<bool> undoEnabled(Undo::UndoEnabled, true);
    const bool debugEnabled = QLoggingCategory::defaultCategory()->isDebugEnabled();
    QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, false);
    const auto debugGuard = qScopeGuard([&] {
        QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, debugEnabled);
    });
    BrushTestLibrary library;
    QScopedValueRollback<TerrainLib*> terrainLibrary(Game::terrainLib, &library);
    Trk trk;
    trk.load(routeDirectory.absoluteFilePath(trkFiles.first()));
    const int worldX = trk.startTileX, worldZ = -trk.startTileZ;
    library.loadQuadTree();
    QVector<Terrain*> tiles;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            Terrain *tile = library.getTerrainByXY(worldX + dx, worldZ + dz, true);
            if (tile == nullptr || !tile->loaded || !tile->isEditable()
                    || tile->getGridLayout().terrainWorldSize != 2048
                    || tile->getSampleCount() != 2048 || tile->getSampleSize() != 1) {
                qWarning() << "[benchmark:terrain-brush] expected editable 2048/1 m tile at"
                           << worldX + dx << worldZ + dz;
                return 2;
            }
            tiles.append(tile);
            qInfo().noquote() << QString("[benchmark:terrain-brush] tile=%1 world=%2,%3 N=%4 S=%5 P=%6")
                .arg(tile->name).arg(int(tile->mojex)).arg(int(tile->mojez))
                .arg(tile->getSampleCount()).arg(tile->getSampleSize())
                .arg(tile->getGridLayout().patchesPerSide);
        }
    }
    // Read/prewarm native edges and CPU bounds outside timing. Never call any
    // render/init entry point: Terrain's mesh backend stays null. Consequently
    // refreshModified still updates CPU bounds, but cannot generate/upload VBOs.
    for (Terrain *tile : tiles) library.fillCachedRaw(*tile);
    for (Terrain *tile : tiles) tile->refreshModified();
    qInfo().noquote() << QString("[benchmark:terrain-brush] route=%1 start_world=%2,%3 "
                    "brush_size=50 radius_m=400 hType=0 alpha=1 direction=+1 "
                    "scope=real-paintHeightMap+undo+dirty-notification+CPU-bounds "
                    "excluded=disk-load+GPU-mesh+normals+upload+route-objects+network "
                    "route_write_enabled=false debug_logging=false warmups=5")
                .arg(routeDirectory.absolutePath()).arg(worldX).arg(worldZ);

    Brush brush;
    brush.size = 50;
    brush.hType = 0;
    brush.alpha = 1;
    brush.direction = 1;
    bool valid = true;
    std::vector<std::vector<float>> initial;
    for (Terrain *tile : tiles) initial.push_back(brushHeights(*tile));
    for (int location = 0; location < 2; ++location) {
        float p[3] = {location ? 1024.0f : 0.0f, 0, location ? 1024.0f : 0.0f};
        const QString position = location ? "corner_1024_1024" : "centre_0_0";
        std::vector<std::vector<float>> expected;
        QVector<QByteArray> expectedDescriptors;
        QHash<Terrain*, QRect> expectedDirty;
        QSet<Terrain*> expectedUpdates;
        for (int method = 0; method < 3; ++method) {
            const QString methodName = method == 0 ? "old" : method == 1 ? "direct" : "buffer";
            for (int i = 0; i < tiles.size(); ++i) restoreBrushHeights(*tiles[i], initial[i]);
            library.clearDiagnostics();
            int affected = 0;
            TerrainHeightBrush::Timings totals;
            int timedCalls = 0;
            auto measure = [&] {
                QElapsedTimer timer;
                timer.start();
                TerrainHeightBrush::Timings timing;
                const QSet<Terrain*> changed = method == 0
                        ? library.paintHeightMapLegacy(&brush, worldX, worldZ, p)
                        : TerrainHeightBrush::paint(library, brush, worldX, worldZ, p,
                            method == 1 ? TerrainHeightBrush::Method::DirectSlices
                                        : TerrainHeightBrush::Method::HeightBuffer, false, &timing);
                const double ms = timer.nsecsElapsed() / 1000000.0;
                totals.prepareMs += timing.prepareMs; totals.gatherMs += timing.gatherMs;
                totals.editMs += timing.editMs; totals.scatterMs += timing.scatterMs;
                totals.commitMs += timing.commitMs; totals.bufferBytes = timing.bufferBytes;
                ++timedCalls;
                affected = changed.size();
                valid &= affected == (location ? 4 : 1) && !timing.fallback;
                return ms;
            };
            const auto undoGuard = qScopeGuard([] { Undo::StateCancel(); });
            Undo::StateBegin();
            const double firstEvent = measure();
            report(position, methodName + "_first_event_with_snapshot", {firstEvent}, affected);
            for (int i = 0; i < 5; ++i) measure();
            totals = {}; timedCalls = 0;
            QVector<double> repeated;
            for (int i = 0; i < 30; ++i) repeated.append(measure());
            report(position, methodName + "_same_stroke", repeated, affected);
            if (method != 0)
                qInfo().noquote() << QString("[benchmark:terrain-brush] position=%1 method=%2 "
                    "prepare_ms=%3 gather_ms=%4 edit_ms=%5 scatter_ms=%6 commit_ms=%7 buffer_bytes=%8")
                    .arg(position, methodName).arg(totals.prepareMs / timedCalls, 0, 'f', 3)
                    .arg(totals.gatherMs / timedCalls, 0, 'f', 3).arg(totals.editMs / timedCalls, 0, 'f', 3)
                    .arg(totals.scatterMs / timedCalls, 0, 'f', 3).arg(totals.commitMs / timedCalls, 0, 'f', 3)
                    .arg(qulonglong(totals.bufferBytes));
            Undo::StateCancel();
            repeated.clear();
            for (int i = 0; i < 10; ++i) {
                Undo::StateBegin();
                repeated.append(measure());
                Undo::StateCancel(); // discard snapshot, not an undo/refresh operation
            }
            report(position, methodName + "_fresh_stroke_with_snapshot", repeated, affected);
            repeated.clear();
            for (int i = 0; i < 30; ++i) repeated.append(measure());
            report(position, methodName + "_no_undo_state", repeated, affected);
            if (method == 0) {
                for (Terrain *t : tiles) { expected.push_back(brushHeights(*t)); expectedDescriptors.append(brushDescriptor(*t)); }
                expectedDirty = library.dirty; expectedUpdates = library.updates;
            } else {
                bool parity = library.dirty == expectedDirty && library.updates == expectedUpdates;
                for (int i = 0; i < tiles.size(); ++i)
                    parity &= sameBrushHeights(*tiles[i], expected[i])
                            && brushDescriptor(*tiles[i]) == expectedDescriptors[i];
                valid &= parity;
                qInfo() << "[benchmark:terrain-brush]" << position << methodName << "exact-parity=" << parity;
            }
        }
    }
    qInfo() << "[benchmark:terrain-brush] affected-tile-count-check=" << valid
            << "route data changed only in memory; no filesystem saves";
    return valid ? 0 : 1;
}
