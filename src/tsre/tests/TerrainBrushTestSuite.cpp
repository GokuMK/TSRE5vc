#include <tsre/tests/TerrainBrushTestSuite.h>
#include <tsre/tests/TerrainBrushTestSupport.h>
#include <tsre/world/TerrainHeightBrush.h>
#include <tsre/world/TerrainHeightArea.h>
#include <tsre/world/TerrainBrushProfiler.h>
#include <tsre/world/TerrainMeshBackend.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/texture/Brush.h>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <QTemporaryDir>
#include <QDir>
#include <QDebug>
#include <QLoggingCategory>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <memory>
#include <cmath>
#include <limits>

namespace {
class FixtureTerrain : public Terrain {
public:
    using Terrain::Terrain;
    void makeReadOnly() { editable = false; }
    bool attachPagedMesh() {
        meshBackend = new TerrainMeshPaged(*this);
        return meshBackend->ensureInitialized();
    }
    QByteArray firstVertexPage() {
        RenderItem item;
        meshBackend->configureRenderItem(item, 0, false, false);
        if (!item.VBO || !item.VBO->bind()) return {};
        QByteArray bytes(item.VBO->size(), '\0');
        const bool read = item.VBO->read(0, bytes.data(), bytes.size());
        item.VBO->release();
        return read ? bytes : QByteArray();
    }
};
class FixtureLibrary : public TsreTests::BrushTestLibrary {
public:
    std::vector<std::unique_ptr<Terrain>> owned;
    int lookups = 0;
    FixtureLibrary() { quadTree = new QuadTree; quadTreeLo = new QuadTree(true); setDetailedAsCurrent(); }
    Terrain *getTerrainByXY(int x, int z, bool = false) override {
        ++lookups;
        for (auto &t : owned) {
            const auto b = TerrainPhysicalBounds::of(*t);
            if (qint64(x) * 2048 >= b.minX && qint64(x) * 2048 < b.maxX
                    && qint64(z) * 2048 >= b.minZ && qint64(z) * 2048 < b.maxZ) return t.get();
        }
        return nullptr;
    }
    bool add(int x, int z, int samples, int spacing, int patches) {
        const QString name = "brush" + QString::number(owned.size());
        if (!Terrain::SaveEmpty(name, samples, spacing, patches, false, true)) return false;
        TerrainInfo info; info.cx = x; info.cy = -z; info.name = name;
        auto t = std::make_unique<FixtureTerrain>(&info);
        if (!t->loaded) return false;
        for (int row = 0; row <= samples; ++row)
            for (int col = 0; col <= samples; ++col)
                t->terrainData[row][col] = float((row * 17 + col * 23) % 193) / 7 + x * 3;
        owned.push_back(std::move(t));
        return true;
    }
};
}

int TsreTests::runTerrainBrushSuite(bool verbose) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const QString &label) {
        if (ok) ++passed; else ++failed;
        if (!ok || verbose) qInfo() << "[tests:terrain-brush]" << (ok ? "PASS" : "FAIL") << label;
    };
    QTemporaryDir directory;
    if (!directory.isValid() || Undo::IsStateOpen()) return 2;
    QScopedValueRollback<QString> root(Game::root, directory.path());
    QScopedValueRollback<QString> route(Game::route, "brush-tests");
    QScopedValueRollback<bool> write(Game::writeEnabled, true);
    QScopedValueRollback<bool> undo(Undo::UndoEnabled, true);
    auto *logging = QLoggingCategory::defaultCategory();
    const bool debug = logging->isDebugEnabled(), warning = logging->isWarningEnabled();
    logging->setEnabled(QtDebugMsg, false);
    logging->setEnabled(QtWarningMsg, false); // legacy mixed-seam warnings repeat per column
    const auto loggingGuard = qScopeGuard([&] {
        logging->setEnabled(QtDebugMsg, debug); logging->setEnabled(QtWarningMsg, warning);
    });
    QDir().mkpath(directory.path() + "/routes/brush-tests/tiles");
    FixtureLibrary library;
    QScopedValueRollback<TerrainLib*> active(Game::terrainLib, &library);
    for (int z = -1; z <= 1; ++z) for (int x = -1; x <= 1; ++x)
        if (!library.add(x, z, 256, 8, (x + z) % 2 ? 16 : 32)) return 2;

    auto compare = [&](Brush brush, int wx, int wz, float px, float pz, bool mixed,
                       bool compareOld = true, bool expectFallback = false) {
        std::vector<std::vector<float>> initial, expected;
        QVector<QByteArray> descriptors;
        QVector<bool> modified;
        QHash<Terrain*, QRect> dirty;
        QSet<Terrain*> updates, affected;
        for (auto &t : library.owned) initial.push_back(brushHeights(*t));
        bool ok = true;
        for (int method = compareOld ? 0 : 1; method < (mixed ? 3 : 4); ++method) {
            for (int i = 0; i < int(library.owned.size()); ++i)
                restoreBrushHeights(*library.owned[i], initial[i]);
            library.clearDiagnostics();
            library.lookups = 0;
            float p[3] = {px, 0, pz};
            Undo::StateBegin();
            TerrainHeightBrush::Timings timing;
            const auto changed = method == 0 ? library.paintHeightMapLegacy(&brush, wx, wz, p)
                    : method == 3 ? library.paintHeightMap(&brush, wx, wz, p)
                    : TerrainHeightBrush::paint(library, brush, wx, wz, p,
                        method == 1 ? TerrainHeightBrush::Method::DirectSlices
                                    : TerrainHeightBrush::Method::HeightBuffer, mixed, &timing);
            const int lookups = library.lookups;
            if (mixed && !compareOld) ok &= changed.size() == 2;
            if (method == (compareOld ? 0 : 1)) {
                for (auto &t : library.owned) {
                    expected.push_back(brushHeights(*t)); descriptors.append(brushDescriptor(*t));
                    modified.append(t->isModified());
                }
                dirty = library.dirty; updates = library.updates; affected = changed;
            } else {
                ok &= dirty == library.dirty && updates == library.updates && affected == changed;
                for (int i = 0; i < int(library.owned.size()); ++i) {
                    const bool heightOk = sameBrushHeights(*library.owned[i], expected[i]);
                    const bool descriptorOk = brushDescriptor(*library.owned[i]) == descriptors[i];
                    if (!heightOk || !descriptorOk)
                        qInfo() << "[tests:terrain-brush] mismatch" << method << i << "height" << heightOk << "descriptor" << descriptorOk;
                    ok &= heightOk && descriptorOk && modified[i] == library.owned[i]->isModified();
                }
            }
            if (method == 1 || method == 2) ok &= timing.fallback == expectFallback
                    && (expectFallback || lookups < 100);
            Undo::StateEnd();
            Undo::UndoLast();
            for (int i = 0; i < int(library.owned.size()); ++i)
                ok &= sameBrushHeights(*library.owned[i], initial[i]);
        }
        check(ok, QString("mode=%1 direction=%2 size=%3 alpha=%4 p=%5,%6 mixed=%7 old=%8")
              .arg(brush.hType).arg(brush.direction).arg(brush.size).arg(brush.alpha)
              .arg(px).arg(pz).arg(mixed).arg(compareOld));
    };
    for (int mode = 0; mode < 4; ++mode)
        for (float direction : {-1.0f, 1.0f})
            for (int size : {0, 1, 50, 100})
                for (float alpha : {0.0f, 0.37f})
                    for (const auto &p : {QPointF(0, 0), QPointF(1024, 1024),
                                          QPointF(1024, 0), QPointF(-1024, -1024),
                                          QPointF(1023.5, -11.7)}) {
                        Brush brush; brush.hType = mode; brush.direction = direction;
                        brush.size = size; brush.alpha = alpha; brush.hFixed = 15;
                        compare(brush, 0, 0, p.x(), p.y(), false);
                    }
    // Four-km terrain next to two two-km tiles; same spacing, different P.
    library.owned.clear();
    if (!library.add(0, 0, 512, 8, 16) || !library.add(2, -1, 256, 8, 32)
            || !library.add(2, 0, 256, 8, 16)) return 2;
    for (int mode = 0; mode < 4; ++mode) {
        Brush brush; brush.hType = mode; brush.size = 50;
        compare(brush, 1, -1, 1024, 1024, false);
    }
    // Native 4m/8m neighbours: preserve old skip policy, then explicitly enable
    // mixed editing for both experiments and compare native output, not old.
    library.owned.clear();
    if (!library.add(0, 0, 512, 4, 16) || !library.add(1, 0, 256, 8, 32)) return 2;
    for (auto &t : library.owned) library.fillCachedRaw(*t);
    for (int mode = 0; mode < 4; ++mode) {
        Brush brush; brush.hType = mode; brush.size = 50;
        compare(brush, 0, 0, 1016, 0, false);
        compare(brush, 0, 0, 1016, 0, true, false);
    }
    library.owned.back()->loaded = false;
    Brush brush; brush.size = 50;
    compare(brush, 0, 0, 1016, 0, false);
    library.owned.back()->loaded = true;
    static_cast<FixtureTerrain*>(library.owned.back().get())->makeReadOnly();
    compare(brush, 0, 0, 1016, 0, true);
    for (int spacing : {16, 32}) {
        library.owned.clear();
        if (!library.add(0, 0, 2048 / spacing, spacing, spacing == 16 ? 8 : 4)) return 2;
        for (int mode = 0; mode < 4; ++mode) {
            brush.hType = mode;
            compare(brush, 0, 0, 1023.5, -11.7, false);
        }
    }
    library.owned.clear();
    if (!library.add(0, 0, 256, 24, 16)) return 2;
    brush.hType = 0;
    compare(brush, 0, 0, 1024, 0, false, true, true);

    // Public get/edit/set contract, without any brush-specific preparation.
    library.owned.clear();
    if (!library.add(0, 0, 256, 8, 16)) return 2;
    auto *tile = library.owned.front().get();
    const auto original = brushHeights(*tile);
    restoreBrushHeights(*tile, original);
    library.clearDiagnostics();
    const auto descriptor = brushDescriptor(*tile);
    auto area = TerrainHeightArea::getArea(library, -15, -7, 15, 7);
    check(area.supported && area.originX == -16 && area.originZ == -8
          && area.width == 5 && area.height == 3 && area.spacing == 8
          && area.slices.size() == 1 && sameBrushHeights(*tile, original)
          && brushDescriptor(*tile) == descriptor && library.dirty.empty()
          && !tile->isModified() && !Undo::IsStateOpen(), "getArea rectangular read-only gather");
    check(TerrainHeightArea::setArea(library, area).empty() && !tile->isModified()
          && library.dirty.empty(), "unchanged setArea has no side effects");
    area = TerrainHeightArea::getArea(library, -16, -8, 16, 8);
    for (float &h : area.heights) h += 3;
    Undo::StateBegin();
    const auto changed = TerrainHeightArea::setArea(library, area);
    bool raised = changed.size() == 1 && library.updates.contains(tile);
    for (int z = 127; z <= 129; ++z) for (int x = 126; x <= 130; ++x)
        raised &= tile->terrainData[z][x] == original[std::size_t(z) * 257 + x] + 3;
    check(raised && library.dirty.value(tile) == QRect(126, 127, 5, 3),
          "setArea native scatter and one exact dirty rectangle");
    for (float &h : area.heights) h += 100;
    check(TerrainHeightArea::setArea(library, area).empty()
          && tile->terrainData[128][128] == original[128 * 257 + 128] + 3,
          "setArea cannot commit twice");
    Undo::StateEnd(); Undo::UndoLast();
    check(sameBrushHeights(*tile, original), "setArea real undo restores all heights");
    restoreBrushHeights(*tile, original); library.clearDiagnostics();
    area = TerrainHeightArea::getArea(library, 0, 0, 8, 8);
    area.heights[0] = std::numeric_limits<float>::quiet_NaN();
    area.heights[1] = std::numeric_limits<float>::infinity();
    check(TerrainHeightArea::setArea(library, area).empty()
          && sameBrushHeights(*tile, original), "setArea ignores non-finite values");
    area = TerrainHeightArea::getArea(library, 0, 0, 8, 8);
    for (float &h : area.heights) h += 10;
    static_cast<FixtureTerrain*>(tile)->makeReadOnly();
    check(TerrainHeightArea::setArea(library, area).empty()
          && sameBrushHeights(*tile, original), "setArea respects newly read-only terrain");
    check(!TerrainHeightArea::getArea(library, 0, 0, 8, 8).supported,
          "getArea excludes read-only terrain");

    library.owned.clear();
    if (!library.add(0, 0, 512, 4, 16) || !library.add(1, 0, 256, 8, 16)) return 2;
    for (auto &t : library.owned) library.fillCachedRaw(*t);
    auto mixedArea = TerrainHeightArea::getArea(library, 1016, 0, 1040, 8);
    check(mixedArea.supported && mixedArea.spacing == 4 && mixedArea.width == 7
          && mixedArea.height == 3 && mixedArea.slices.size() == 2,
          "getArea automatically chooses finest mixed-grid spacing");
    auto roundedEdge = TerrainHeightArea::getArea(library, 1021, 0, 1023, 0);
    check(roundedEdge.supported && roundedEdge.slices.size() == 2
          && std::isfinite(roundedEdge.heights.back()),
          "getArea discovers owner at outward-rounded tile boundary");
    auto *coarse = library.owned.back().get();
    const float expectedInterpolation = (coarse->terrainData[128][0] + coarse->terrainData[128][1]) * 0.5f;
    check(std::abs(mixedArea.heights[3] - expectedInterpolation) < 0.00001f,
          "getArea interpolates coarse heights between native samples");
    const auto fineOriginal = brushHeights(*library.owned.front());
    const auto coarseOriginal = brushHeights(*coarse);
    mixedArea.heights[3] += 20; // x=1028, no native 8m sample here
    check(TerrainHeightArea::setArea(library, mixedArea).empty()
          && sameBrushHeights(*coarse, coarseOriginal)
          && sameBrushHeights(*library.owned.front(), fineOriginal),
          "setArea samples native positions; extra fine points do not blur coarse terrain");
    auto filtered = TerrainHeightArea::getArea(library, 1016, 0, 1040, 8, 4);
    check(filtered.supported && filtered.slices.size() == 1 && std::isnan(filtered.heights[2]),
          "spacing filter leaves skipped terrain unavailable");
    mixedArea = TerrainHeightArea::getArea(library, 1016, 0, 1040, 8);
    for (float &h : mixedArea.heights) h += 2;
    Undo::StateBegin();
    check(TerrainHeightArea::setArea(library, mixedArea).size() == 2,
          "setArea scatters a mixed rectangle to both native grids");
    Undo::StateEnd(); Undo::UndoLast();
    check(sameBrushHeights(*coarse, coarseOriginal)
          && sameBrushHeights(*library.owned.front(), fineOriginal), "mixed area undo");
    check(!TerrainHeightArea::getArea(library, 10, 0, 0, 8).supported,
          "getArea rejects reversed rectangle");
    library.owned.clear();
    if (!library.add(0, 0, 256, 24, 16)) return 2;
    check(!TerrainHeightArea::getArea(library, 0, 0, 8, 8).supported,
          "getArea reports unsupported spacing without mutation");
    // Opt-in instrumentation smoke check with a real GL context, but no scene
    // rendering. These numbers are NOT interactive performance measurements.
    if (TerrainBrushProfiler::enabled()) {
        library.owned.clear();
        QOpenGLContext context;
        QSurfaceFormat format;
        format.setVersion(3, 3); format.setProfile(QSurfaceFormat::CoreProfile);
        context.setFormat(format);
        QOffscreenSurface surface;
        const bool created = context.create();
        surface.setFormat(context.format()); surface.create();
        const bool current = created && surface.isValid() && context.makeCurrent(&surface);
        check(current, "profiling GL context");
        if (current) {
            if (!library.add(0, 0, 256, 8, 16)) return 2;
            auto *t = static_cast<FixtureTerrain*>(library.owned.front().get());
            check(t->attachPagedMesh(), "profiling paged backend initialization");
            // Initial build used the ordinary interleaved loop. Force an
            // unchanged rebuild with the split diagnostic loop, then compare
            // actual uploaded bytes. Readback is outside the profiling event.
            const QByteArray ordinaryVertices = t->firstVertexPage();
            {
                TerrainBrushProfiler::Event profile("mesh", "smoke_split_parity=true");
                t->invalidateAll(TerrainDirtyHeight | TerrainDirtyNormals);
                t->refreshModified();
            }
            check(!ordinaryVertices.isEmpty() && ordinaryVertices == t->firstVertexPage(),
                  "split profiler and ordinary builder produce identical vertex bytes");
            float position[3] = {0, 0, 0};
            brush.hType = 0; brush.size = 2; brush.alpha = 1;
            Undo::StateBegin();
            for (int event = 0; event < 2; ++event) {
                TerrainBrushProfiler::Event profile("brush", "smoke=true");
                {
                    TerrainBrushProfiler::Scope timer(TerrainBrushProfiler::Paint);
                    library.paintHeightMap(&brush, 0, 0, position);
                }
                const auto &s = *TerrainBrushProfiler::active();
                check(s.ns[TerrainBrushProfiler::Build] > 0
                      && s.ns[TerrainBrushProfiler::Vertex] > 0
                      && s.ns[TerrainBrushProfiler::Normals] > 0
                      && s.ns[TerrainBrushProfiler::Build]
                         >= s.ns[TerrainBrushProfiler::Vertex] + s.ns[TerrainBrushProfiler::Normals]
                      && s.ns[TerrainBrushProfiler::Upload] > 0
                      && s.ns[TerrainBrushProfiler::Mesh] > 0
                      && s.counts[TerrainBrushProfiler::Patches] > 0
                      && s.counts[TerrainBrushProfiler::UploadBytes]
                         == s.counts[TerrainBrushProfiler::Vertices] * sizeof(TerrainVertex8Derived)
                      && s.counts[TerrainBrushProfiler::Snapshots] == (event == 0 ? 1u : 0u),
                      "profiling build/upload counts and fresh versus existing undo");
            }
            Undo::StateCancel();
            context.doneCurrent();
            {
                TerrainBrushProfiler::Event profile("brush", "smoke_deferred=true");
                library.paintHeightMap(&brush, 0, 0, position);
                check(TerrainBrushProfiler::active()->counts[TerrainBrushProfiler::Deferred] > 0
                      && TerrainBrushProfiler::active()->ns[TerrainBrushProfiler::Build] == 0,
                      "profiling detects deferred no-context refresh");
            }
            context.makeCurrent(&surface);
            t->refreshModified(); // emits a standalone kind=mesh record
            check(!TerrainBrushProfiler::active(), "standalone mesh event releases profiler scope");
            library.owned.clear(); // destroy GPU objects while context is current
            context.doneCurrent();
        }
    }
    qInfo() << "[tests:terrain-brush] cases=" << passed + failed << "passed=" << passed << "failed=" << failed;
    return failed ? 1 : 0;
}
