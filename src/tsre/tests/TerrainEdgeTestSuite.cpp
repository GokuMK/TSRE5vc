#include <tsre/tests/TerrainEdgeTestSuite.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainInfo.h>
#include <tsre/world/TerrainLibQt.h>
#include <tsre/world/QuadTree.h>
#include <tsre/world/TerrainMeshBackend.h>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <QTemporaryDir>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QDataStream>
#include <QScopedValueRollback>
#include <memory>
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace {
// Exercise the production domain guard and resolver with deterministic loaded
// terrain bounds, without disk I/O during lookup or an OpenGL context.
class EdgeLibrary : public TerrainLibQt {
public:
    using TerrainLib::prepareTerrainLod;
    QVector<Terrain*> tiles;
    QSet<Terrain*> loadable;
    int lookups = 0;
    int loads = 0;
    EdgeLibrary() { quadTree = new QuadTree; quadTreeLo = new QuadTree(true); setDetailedAsCurrent(); }
    ~EdgeLibrary() { delete quadTree; delete quadTreeLo; }
    Terrain *getTerrainByXY(int x, int z, bool load = false) override {
        ++lookups;
        const bool low = currentQt == &terrainQtLo;
        const qint64 px = qint64(x) * 2048, pz = qint64(z) * 2048;
        for (auto *t : tiles) {
            const auto b = TerrainPhysicalBounds::of(*t);
            if (t->lowTile == low && px >= b.minX && px < b.maxX
                    && pz >= b.minZ && pz < b.maxZ) {
                if (!t->loaded && load && loadable.contains(t)) {
                    t->loaded = true;
                    ++loads;
                    terrainAvailabilityChanged(t);
                }
                return t;
            }
        }
        return nullptr;
    }
    bool detailedCurrent() const { return currentQt == &terrainQt; }
};

float plane(qint64 x, qint64 z) { return float(x / 32.0 + z / 16.0); }
void setPlane(Terrain &t) {
    const auto b = TerrainPhysicalBounds::of(t);
    const auto &g = t.getGridLayout();
    for (int z = 0; z < g.sampleCount; ++z)
        for (int x = 0; x < g.sampleCount; ++x)
            t.terrainData[z][x] = plane(b.minX + x * g.sampleSpacing,
                                       b.minZ + z * g.sampleSpacing);
}
}

int TsreTests::runTerrainEdgeSuite(bool verbose) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) {
        if (ok) ++passed; else ++failed;
        if (!ok || verbose)
            qInfo() << "[tests:terrain-edges]" << (ok ? "PASS" : "FAIL") << name;
    };
    TerrainAdjacentEdge edge;
    edge.ownerLengthM = 32;
    TerrainEdgeSection section;
    section.points = {{0, 0}, {8, 16}, {16, 32}};
    edge.sections = {section};
    edge.finish();
    float h = -99;
    check(edge.status == TerrainAdjacentEdge::Status::Partial
          && edge.sampleHeight(2, h) && h == 4
          && edge.sampleHeight(16, h) && h == 32
          && !edge.sampleHeight(17, h), "linear-interpolation-and-missing-interval");
    section.points = {{16, 32}, {24, 48}, {32, 64}};
    edge.sections.push_back(section);
    edge.finish();
    TerrainAdjacentEdge::Cursor cursor;
    bool monotonic = true;
    for (int i = 0; i <= 32; ++i)
        monotonic &= edge.sampleHeight(i, h, cursor) && h == i * 2;
    check(monotonic && edge.status == TerrainAdjacentEdge::Status::Complete,
          "monotonic-scan-shared-endpoint");
    edge.sections[1].points[0].height = 33;
    edge.finish();
    check(edge.status == TerrainAdjacentEdge::Status::Conflict
          && !edge.sampleHeight(16, h) && edge.sampleHeight(8, h),
          "conflicting-endpoint-keeps-unaffected-region");
    edge.sections[1].points = {{8, 1}, {16, 2}, {24, 3}};
    edge.finish();
    check(!edge.sampleHeight(12, h), "overlap-rejected-for-sampling");
    edge.sections[1].points = {{16, 1}, {20, 2}, {32, 3}};
    edge.finish();
    check(edge.sections.size() == 1, "irregular-section-uses-fallback");

    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    QScopedValueRollback<QString> root(Game::root, directory.path());
    QScopedValueRollback<QString> route(Game::route, "edge-tests");
    QScopedValueRollback<bool> write(Game::writeEnabled, true);
    EdgeLibrary library;
    QScopedValueRollback<TerrainLib*> terrainLibrary(Game::terrainLib, &library);
    QDir().mkpath(Game::root + "/routes/edge-tests/tiles");
    QDir().mkpath(Game::root + "/routes/edge-tests/lo_tiles");
    std::vector<std::unique_ptr<Terrain>> owned;
    auto make = [&](int wx, int wz, int n, int s, int p, bool low = false) -> Terrain* {
        const QString name = "test" + QString::number(owned.size());
        if (!Terrain::SaveEmpty(name, n, s, p, low)) return nullptr;
        TerrainInfo info;
        info.cx = wx; info.cy = -wz; info.name = name; info.low = low;
        auto t = std::make_unique<Terrain>(&info);
        if (!t->loaded) return nullptr;
        setPlane(*t);
        Terrain *result = t.get();
        owned.push_back(std::move(t));
        library.tiles.push_back(result);
        return result;
    };
    Terrain *owner = make(0, 0, 1024, 2, 32);
    Terrain *right = make(1, 0, 256, 8, 16);
    Terrain *left = make(-1, 0, 512, 4, 8);
    Terrain *up = make(0, 1, 512, 4, 16);
    Terrain *down = make(0, -1, 256, 8, 4);
    Terrain *diagonal = make(1, 1, 256, 8, 16);
    if (!owner || !right || !left || !up || !down || !diagonal) {
        check(false, "fixtures-load"); return 1;
    }
    const auto b = TerrainPhysicalBounds::of(*owner);
    library.prepareTerrainLod(library.tiles, {{2, 1000}}, 0, 0);
    const auto nativeStates = *library.preparedPatchLod(owner);
    check((nativeStates[32 * 10].edgeMask & TerrainLod::LocalX0)
          && (nativeStates[32 * 10 + 31].edgeMask & TerrainLod::LocalXMax)
          && (nativeStates[10].edgeMask & TerrainLod::LocalZ0)
          && (nativeStates[31 * 32 + 10].edgeMask & TerrainLod::LocalZMax),
          "lod-native-2-to-4-and-2-to-8-stitches-all-four-sides");
    check(((*library.preparedPatchLod(left))[8 * 3 + 7].edgeMask & TerrainLod::LocalXMax) == 0,
          "lod-coarser-native-side-does-not-stitch-to-finer");
    auto sameStates = [](const QVector<TerrainPatchLodState> &a,
                         const QVector<TerrainPatchLodState> &b) {
        if (a.size() != b.size()) return false;
        for (int i = 0; i < a.size(); ++i)
            if (a[i].sourceStep != b[i].sourceStep
                    || a[i].effectiveSampleSpacing != b[i].effectiveSampleSpacing
                    || a[i].edgeMask != b[i].edgeMask) return false;
        return true;
    };
    QVector<Terrain*> reversed = library.tiles;
    std::reverse(reversed.begin(), reversed.end());
    library.prepareTerrainLod(reversed, {{2, 1000}}, 0, 0);
    check(sameStates(nativeStates, *library.preparedPatchLod(owner)),
          "lod-native-selection-independent-of-tile-traversal-order");
    const auto *cachedPoints = owner->adjacentEdges[0].sections[0].points.constData();
    library.prepareTerrainLod(library.tiles, {{8, 1000}}, 10000, 10000);
    const auto reducedStates = *library.preparedPatchLod(owner);
    bool equalEdges = true;
    for (const auto &state : reducedStates)
        equalEdges &= state.effectiveSampleSpacing == 8 && state.edgeMask == 0;
    check(equalEdges, "lod-equal-effective-spacing-across-different-native-grids");
    check(cachedPoints == owner->adjacentEdges[0].sections[0].points.constData(),
          "lod-camera-change-keeps-native-edge-vectors");
    library.prepareTerrainLod(library.tiles, TerrainLod::defaultProfile(), 500, 500);
    const auto distanceStates = *library.preparedPatchLod(owner);
    library.prepareTerrainLod(reversed, TerrainLod::defaultProfile(), 500, 500);
    check(sameStates(distanceStates, *library.preparedPatchLod(owner)),
          "lod-distance-and-refinement-independent-of-submission-order");
    right->loaded = false;
    library.prepareTerrainLod(library.tiles, {{2, 1000}}, 0, 0);
    check(((*library.preparedPatchLod(owner))[32 * 10 + 31].edgeMask & TerrainLod::LocalXMax) == 0,
          "lod-missing-neighbour-has-no-external-transition");
    right->loaded = true;
    library.terrainAvailabilityChanged(right);
    library.clearPreparedTerrainLod();
    for (auto side : {TerrainEdgeSide::LocalX0, TerrainEdgeSide::LocalXMax,
                      TerrainEdgeSide::LocalZ0, TerrainEdgeSide::LocalZMax}) {
        const auto &e = library.resolveAdjacentEdge(*owner, side, TerrainEdgeDiscovery::LoadedOnly);
        const bool x = side == TerrainEdgeSide::LocalX0 || side == TerrainEdgeSide::LocalXMax;
        const bool positive = side == TerrainEdgeSide::LocalXMax || side == TerrainEdgeSide::LocalZMax;
        const float expected = plane(x ? (positive ? b.maxX : b.minX) : b.minX + 128,
                                      x ? b.minZ + 128 : (positive ? b.maxZ : b.minZ));
        check(e.sections.size() == 1 && e.sampleHeight(128, h) && h == expected,
              "orientation-and-canonical-height");
    }
    library.fillCachedRaw(*owner);
    bool matches = true;
    // Interior edge samples match the plane even between coarser source points.
    for (int i = 0; i < 1000; ++i) {
        matches &= owner->terrainData[i][1024] == plane(b.maxX, b.minZ + i * 2);
        matches &= owner->terrainData[1024][i] == plane(b.minX + i * 2, b.maxZ);
    }
    check(matches && !owner->isModified(), "mixed-resolution-filled-without-modified-state");
    check(owner->terrainData[1024][1024] == plane(b.maxX, b.maxZ),
          "already-loaded-diagonal-endpoint-used");
    const auto &rightEdge = owner->adjacentEdges[size_t(TerrainEdgeSide::LocalXMax)];
    check(rightEdge.sections[0].nativeSpacing() == 8
          && rightEdge.sections[0].sourcePatchAt(*owner, *right, 128) == 16,
          "native-spacing-and-source-patch-arithmetic");
    library.lookups = 0;
    library.fillCachedRaw(*owner);
    check(library.lookups < 12, "warm-fill-lookup-count-independent-of-samples");

    auto &zeroCache = right->adjacentEdges[size_t(TerrainEdgeSide::LocalX0)];
    library.resolveAdjacentEdge(*right, TerrainEdgeSide::LocalX0, TerrainEdgeDiscovery::LoadedOnly);
    right->terrainData[16][0] = 777;
    right->invalidateSamples(0, 16, 0, 16, TerrainDirtyHeight | TerrainDirtyNormals);
    check(zeroCache.dirty && owner->adjacentEdges[1].dirty,
          "zero-side-edit-dirties-own-and-dependent-cache");
    library.fillCachedRaw(*owner);
    check(owner->terrainData[64][1024] == 777, "boundary-edit-reaches-neighbour");
    {
        QScopedValueRollback<bool> undoEnabled(Undo::UndoEnabled, true);
        Undo::StateBegin();
        Undo::PushTerrainHeightMap(1, 0, right->terrainData, 256);
        right->setFixedHeight(53);
        Undo::StateEnd();
        library.fillCachedRaw(*owner);
        check(owner->terrainData[64][1024] == 53, "reset-height-updates-cached-edge");
        Undo::UndoLast();
        library.fillCachedRaw(*owner);
        check(owner->terrainData[64][1024] == 777, "undo-restores-cached-edge-height");
    }
    right->invalidateSamples(12, 12, 12, 12, TerrainDirtyHeight);
    check(!owner->adjacentEdges[1].dirty, "interior-edit-does-not-dirty-edge");

    // No notification: cache detects absence on the next actual refresh.
    right->loaded = false;
    library.fillCachedRaw(*owner);
    check(owner->terrainData[64][1024] == owner->terrainData[64][1023],
          "unnotified-unload-uses-owner-fallback");
    owner->terrainData[64][1023] = 456;
    library.fillCachedRaw(*owner);
    check(owner->terrainData[64][1024] == 456, "fallback-uses-current-owner-height");
    library.loadable.insert(right);
    library.resolveAdjacentEdge(*owner, TerrainEdgeSide::LocalXMax, TerrainEdgeDiscovery::LoadedOnly);
    const int loadsBefore = library.loads;
    library.fillCachedRaw(*owner);
    check(library.loads == loadsBefore + 1 && owner->terrainData[64][1024] == 777,
          "loaded-only-miss-can-upgrade-to-direct-load");
    library.loadable.clear();
    diagonal->loaded = false;
    for (auto &e : owner->adjacentEdges) e.dirty = true;
    library.loadable.insert(diagonal);
    const int beforeCorner = library.loads;
    library.fillCachedRaw(*owner);
    check(library.loads == beforeCorner && !diagonal->loaded
          && owner->terrainData[1024][1024] == owner->terrainData[1023][1023],
          "diagonal-not-loaded-and-corner-fallback-initialized");
    library.loadable.clear();
    diagonal->loaded = true;
    library.terrainAvailabilityChanged(diagonal);
    check(owner->adjacentEdges[1].dirty, "availability-dirties-existing-corner-consumer");
    library.fillCachedRaw(*owner);

    Terrain *low = make(0, 0, 256, 8, 4, true);
    Terrain *lowRight = make(1, 0, 256, 8, 4, true);
    if (low && lowRight) {
        lowRight->terrainData[1][0] = -888;
        library.fillCachedRaw(*low);
        check(low->terrainData[1][256] == -888 && library.detailedCurrent(),
              "distant-domain-isolated-and-current-domain-restored");
    } else check(false, "distant-fixtures");

    Terrain *large = make(4, 0, 512, 8, 16);
    Terrain *smallA = make(6, -1, 1024, 2, 32);
    Terrain *smallB = make(6, 0, 256, 8, 16);
    if (large && smallA && smallB) {
        smallA->loaded = false;
        smallB->loaded = false;
        library.loadable.insert(smallA);
        library.loadable.insert(smallB);
        library.fillCachedRaw(*large);
        library.loadable.clear();
        const auto &e = large->adjacentEdges[1];
        check(e.sections.size() == 2 && e.sections[0].nativeSpacing() == 2
              && e.sections[1].nativeSpacing() == 8
              && e.sections[0].lastAlongM() == 2048
              && e.sections[1].firstAlongM() == 2048,
              "large-edge-has-two-native-resolution-sections");
        const auto lb = TerrainPhysicalBounds::of(*large);
        check(large->terrainData[256][512] == plane(lb.maxX, lb.minZ + 2048),
              "section-junction-canonical-owner-on-first-cold-fill");
        library.prepareTerrainLod({large, smallA, smallB}, {{4, 1000}}, 0, 0);
        bool splitLod = true;
        for (int row = 0; row < 32; ++row)
            splitLod &= ((*library.preparedPatchLod(smallA))[row * 32].edgeMask
                         & TerrainLod::LocalX0) != 0;
        for (int row = 0; row < 16; ++row)
            splitLod &= ((*library.preparedPatchLod(smallB))[row * 16].edgeMask
                         & TerrainLod::LocalX0) == 0;
        check(splitLod, "lod-four-km-edge-walks-two-two-km-sections-and-patch-sizes");
        library.clearPreparedTerrainLod();
        library.tiles.removeAll(smallB);
        large->adjacentEdges[1].dirty = true;
        library.fillCachedRaw(*large);
        check(large->adjacentEdges[1].status == TerrainAdjacentEdge::Status::Partial
              && large->terrainData[400][512] == large->terrainData[400][511],
              "partial-neighbour-coverage-falls-back");
    } else check(false, "mixed-size-fixtures");

    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    owner->saveRAWfileToStreamFloat(out);
    check(bytes.size() == 1024 * 1024 * int(sizeof(float)),
          "synthesized-borders-excluded-from-raw");

    // Complete RAW replacement is the network path; it must notify consumers.
    QByteArray replacement;
    QDataStream rawStream(&replacement, QIODevice::WriteOnly);
    rawStream.setByteOrder(QDataStream::LittleEndian);
    rawStream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for (int i = 0; i < 256 * 256; ++i) rawStream << 91.0f;
    FileBuffer raw;
    raw.length = replacement.size();
    raw.data = new unsigned char[raw.length];
    std::memcpy(raw.data, replacement.constData(), size_t(raw.length));
    right->loadRAWFile(&raw);
    check(owner->adjacentEdges[1].dirty, "raw-replacement-notifies-cached-neighbour");
    library.fillCachedRaw(*owner);
    check(owner->terrainData[64][1024] == 91, "raw-replacement-reaches-synthesized-border");

    // Same identity, different live object: no cache keeps a Terrain pointer.
    Terrain *replacementTerrain = make(1, 0, 512, 4, 16);
    if (replacementTerrain) {
        replacementTerrain->name = right->name;
        library.tiles.removeAll(right);
        library.terrainAvailabilityChanged(replacementTerrain);
        library.fillCachedRaw(*owner);
        check(owner->adjacentEdges[1].sections[0].nativeSpacing() == 4,
              "same-name-replacement-rebuilds-native-grid");
    } else check(false, "replacement-fixture");

    Terrain *extreme = make(10, 0, 2048, 1, 16);
    Terrain *extremeRight = make(11, 0, 256, 8, 16);
    if (extreme && extremeRight) {
        library.fillCachedRaw(*extreme);
        const auto eb = TerrainPhysicalBounds::of(*extreme);
        check(extreme->terrainData[3][2048] == plane(eb.maxX, eb.minZ + 3),
              "2048-one-metre-non-midpoint-interpolation");
    } else check(false, "extreme-fixtures");

    // The reported 4m/8m crack: a fine-only border vertex must remain editable
    // in RAW but disappear from the transition's referenced boundary vertices.
    Terrain *fine = make(40, 0, 512, 4, 16);
    QVector<Terrain*> stitchTiles = {fine, make(39, 0, 256, 8, 16),
                                   make(41, 0, 256, 8, 32),
                                   make(40, -1, 256, 8, 8),
                                   make(40, 1, 256, 8, 4)};
    if (std::all_of(stitchTiles.begin(), stitchTiles.end(), [](Terrain *t) { return t != nullptr; })) {
        fine->terrainData[5][0] = -999;
        fine->terrainData[0][5] = -999;
        fine->invalidateSamples(0, 0, 5, 5, TerrainDirtyHeight);
        for (Terrain *t : stitchTiles) library.fillCachedRaw(*t);
        library.prepareTerrainLod(stitchTiles, {{4, 1000}}, 40.0 * 2048, 0);
        const auto &states = *library.preparedPatchLod(fine);
        bool boundaryTopology = true;
        for (int side = 0; side < 4; ++side) {
            const int id = side == 0 ? 16 * 5 : side == 1 ? 16 * 5 + 15
                                    : side == 2 ? 5 : 15 * 16 + 5;
            const auto &state = states[id];
            boundaryTopology &= state.sourceStep == 1 && (state.edgeMask & (1 << side));
            const auto indices = TerrainMeshPaged::buildLodIndices(32, state.sourceStep, state.edgeMask);
            for (quint16 vertex : indices) {
                const int x = vertex % 33, z = vertex / 33;
                if ((side == 0 && x == 0) || (side == 1 && x == 32))
                    boundaryTopology &= z % 2 == 0;
                if ((side == 2 && z == 0) || (side == 3 && z == 32))
                    boundaryTopology &= x % 2 == 0;
            }
        }
        check(boundaryTopology && fine->terrainData[5][0] == -999
              && fine->terrainData[0][5] == -999,
              "lod-four-to-eight-metre-all-sides-omit-fine-midpoints-without-changing-raw");
        // Check exact shared endpoint geometry (away from the documented
        // diagonal-corner fallback), not just the transition mask.
        bool sharedHeights = true;
        for (int k = 2; k < 250; ++k) {
            sharedHeights &= fine->terrainData[k * 2][0] == stitchTiles[1]->terrainData[k][256];
            sharedHeights &= fine->terrainData[k * 2][512] == stitchTiles[2]->terrainData[k][0];
            sharedHeights &= fine->terrainData[0][k * 2] == stitchTiles[3]->terrainData[256][k];
            sharedHeights &= fine->terrainData[512][k * 2] == stitchTiles[4]->terrainData[0][k];
        }
        check(sharedHeights, "lod-two-to-one-shared-endpoint-heights-match-both-ownership-directions");
        fine->setModified(true);
        fine->save();
        TerrainInfo info;
        info.cx = 40; info.cy = 0; info.name = fine->name;
        auto reloadedFine = std::make_unique<Terrain>(&info);
        library.tiles.removeAll(fine);
        library.tiles.append(reloadedFine.get());
        library.terrainAvailabilityChanged(reloadedFine.get());
        stitchTiles[0] = reloadedFine.get();
        library.prepareTerrainLod(stitchTiles, {{4, 1000}}, 40.0 * 2048, 0);
        const auto *reloadedStates = library.preparedPatchLod(reloadedFine.get());
        check(reloadedFine->loaded && reloadedStates
              && ((*reloadedStates)[0].edgeMask & TerrainLod::LocalX0)
              && reloadedFine->terrainData[5][0] < -998,
              "lod-edit-save-reload-keeps-raw-dip-and-restores-transition");
        library.clearPreparedTerrainLod();
        library.tiles.removeAll(reloadedFine.get());
    } else check(false, "lod-stitch-fixtures-load");

    // Independent comparison with the existing world-space query on valid spans.
    bool parity = true;
    for (int i = 0; i < 1000; ++i) {
        float oldHeight = 0;
        parity &= library.tryGetHeight(0, 0, 1024.0f, i * 2.0f - 1024.0f,
                                        oldHeight, false, false)
                && oldHeight == owner->terrainData[i][1024];
    }
    check(parity, "existing-try-get-height-parity");

    // Production QuadTree registry and disk load/reload, without a GL context.
    {
        TerrainLibQt real;
        QScopedValueRollback<TerrainLib*> actual(Game::terrainLib, &real);
        real.loadQuadTree();
        QDir().mkpath(Game::root + "/routes/edge-tests/td");
        auto *qt = real.getQuadTreeDetailed();
        qt->addTile(20, 0);
        qt->addTile(21, 0);
        qt->addTile(20, -1); // populated, deliberately no payload
        const QString aName = qt->getMyName(20, 0);
        const QString bName = qt->getMyName(21, 0);
        bool created = Terrain::SaveEmpty(aName, 512, 4, 16)
                && Terrain::SaveEmpty(bName, 256, 8, 16);
        Terrain *a = created ? real.getTerrainByXY(20, 0, true) : nullptr;
        if (a && a->loaded) {
            const auto &missing = real.resolveAdjacentEdge(*a, TerrainEdgeSide::LocalXMax,
                                                           TerrainEdgeDiscovery::LoadedOnly);
            check(missing.sections.isEmpty() && real.getTerrainByXY(21, 0, false) == nullptr,
                  "production-loaded-only-does-not-load");
            real.fillRaw(a, 20, 0);
            Terrain *bTile = real.getTerrainByXY(21, 0, false);
            check(bTile && bTile->loaded && a->adjacentEdges[1].sections.size() == 1,
                  "production-direct-neighbour-load");
            check(a->adjacentEdges[3].status == TerrainAdjacentEdge::Status::Missing
                  && std::isfinite(a->terrainData[512][0]),
                  "production-populated-without-payload-fallback");
            const bool reloaded = real.reload(21, 0);
            check(reloaded && a->adjacentEdges[1].dirty,
                  "production-reload-notifies-existing-cache");
            real.fillRaw(a, 20, 0);
            qt->addTile(20, -1);
            Terrain::SaveEmpty(qt->getMyName(20, -1), 256, 8, 16);
            const bool appeared = real.reload(20, 1);
            check(appeared && a->adjacentEdges[3].dirty,
                  "production-missing-payload-becomes-available");
            real.fillRaw(a, 20, 0);
            check(a->adjacentEdges[3].sections.size() == 1,
                  "production-available-neighbour-fills-border");
            Terrain *bCurrent = real.getTerrainByXY(21, 0, false);
            bCurrent->setFixedHeight(73);
            bCurrent->save();
            real.reload(21, 0);
            real.fillRaw(a, 20, 0);
            const Terrain *reloadedB = real.getTerrainByXY(21, 0, false);
            // Disk RAW truncates to uint16; compare the cache exactly to the
            // decoded canonical sample, allowing quantization versus input 73.
            check(reloadedB && a->terrainData[10][512] == reloadedB->terrainData[5][0]
                  && std::abs(a->terrainData[10][512] - 73.0f) < 0.001f
                  && !a->isModified(),
                  "production-save-reload-preserves-canonical-height");
        } else check(false, "production-fixtures-load");
    }

    QElapsedTimer timer;
    timer.start();
    library.lookups = 0;
    for (int repeat = 0; repeat < 100; ++repeat) library.fillCachedRaw(*owner);
    qInfo() << "[tests:terrain-edges] warm 1024 fill average us="
            << timer.nsecsElapsed() / 100000.0 << "lookups=" << library.lookups;
    library.prepareTerrainLod({owner, left, up, down, replacementTerrain},
                              TerrainLod::defaultProfile(), 0, 0);
    timer.restart();
    const int loadsBeforeLod = library.loads;
    for (int repeat = 0; repeat < 100; ++repeat)
        library.prepareTerrainLod({owner, left, up, down, replacementTerrain},
                                  TerrainLod::defaultProfile(), repeat, repeat);
    qInfo() << "[tests:terrain-edges] warm five-tile LOD preparation average us="
            << timer.nsecsElapsed() / 100000.0;
    check(library.loads == loadsBeforeLod, "lod-neighbour-walk-never-loads-terrain");
    library.clearPreparedTerrainLod();
    qInfo() << "[tests:terrain-edges] cases=" << passed + failed
            << "passed=" << passed << "failed=" << failed;
    return failed ? 1 : 0;
}
