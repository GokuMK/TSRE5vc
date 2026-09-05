#include <tsre/tests/TerrainNormalTestSuite.h>
#include <tsre/tests/TestRunner.h>
#include <tsre/tests/TerrainBrushTestSupport.h>
#include <tsre/world/TerrainNormals.h>
#include <tsre/world/Trk.h>
#include <tsre/Game.h>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QOpenGLContext>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <vector>

namespace {
struct Grid {
    int n;
    std::vector<float> data;
    std::vector<float*> rows;
    explicit Grid(int samples) : n(samples), data(std::size_t(n + 1) * (n + 1)), rows(n + 1) {
        for (int z = 0; z <= n; ++z) rows[z] = data.data() + std::size_t(z) * (n + 1);
    }
    void fill(int pattern, float spacing) {
        uint32_t rng = 0x92837;
        for (int z = 0; z <= n; ++z) for (int x = 0; x <= n; ++x) {
            rng = rng * 1664525u + 1013904223u;
            float h = 731.25f;
            if (pattern == 1) h += x * spacing * 0.25f + z * spacing * 0.5f;
            if (pattern == 2) h = (float(rng >> 8) / 16777216.0f - 0.5f) * 16000;
            if (pattern == 3) h = x < n / 2 ? -2000.0f : 3000.0f;
            if (pattern == 4) h = float((x + z) % 2) * 10000.0f;
            if (pattern == 5) h = (x == n / 2 && z == n / 2) ? 10000 : 0;
            if (pattern == 6) h = 1000000.0f + float(int(rng % 9) - 4) * 0.0625f;
            rows[z][x] = h;
        }
    }
};
// Independent test oracle: enumerate the mesh triangles touching the sample.
// This is test geometry, not a selectable runtime normal implementation.
TerrainNormals::Normal meshNormal(float *const *h, int n, float spacing, int x, int z) {
    float sum[3] = {};
    for (int cz = std::max(0, z - 1); cz <= std::min(n - 1, z); ++cz)
        for (int cx = std::max(0, x - 1); cx <= std::min(n - 1, x); ++cx) {
            const int vx[] = {cx, cx + 1, cx, cx + 1};
            const int vz[] = {cz, cz, cz + 1, cz + 1};
            const int triangles[][3] = {{0, 2, 1}, {3, 1, 2}};
            for (const auto &triangle : triangles) {
                bool contains = false;
                for (int v : triangle) contains |= vx[v] == x && vz[v] == z;
                if (!contains) continue;
                float p[3][3];
                for (int i = 0; i < 3; ++i) {
                    const int v = triangle[i];
                    p[i][0] = vx[v] * spacing; p[i][1] = h[vz[v]][vx[v]]; p[i][2] = vz[v] * spacing;
                }
                float a[3], b[3];
                for (int i = 0; i < 3; ++i) { a[i] = p[0][i] - p[1][i]; b[i] = p[0][i] - p[2][i]; }
                sum[0] += a[1] * b[2] - a[2] * b[1];
                sum[1] += a[2] * b[0] - a[0] * b[2];
                sum[2] += a[0] * b[1] - a[1] * b[0];
            }
        }
    const float length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
    return length > 0 ? TerrainNormals::Normal{sum[0] / length, sum[1] / length, sum[2] / length}
                      : TerrainNormals::Normal{0, 1, 0};
}
uint32_t expectedPack(float x, float y, float z, bool gap) {
    auto component = [](float v) {
        return uint32_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 511.0f)) & 0x3ffu;
    };
    // Match the defined NaN clamp convention before applying the standard oracle.
    if (std::isnan(x)) x = 1;
    if (std::isnan(y)) y = 1;
    if (std::isnan(z)) z = 1;
    return component(x) | (component(y) << 10) | (component(z) << 20) | (gap ? 1u << 30 : 0u);
}
uint32_t packed(const TerrainNormals::Normal &v, bool gap, bool actual) {
    return actual ? TerrainNormals::packNormal(v.x, v.y, v.z, gap)
                  : expectedPack(v.x, v.y, v.z, gap);
}
}

int TsreTests::runTerrainNormalSuite(bool verbose) {
    int passed = 0, failed = 0;
    quint64 checked = 0;
    auto check = [&](bool ok, const QString &message) {
        if (ok) ++passed; else ++failed;
        if (!ok || verbose) qInfo() << "[tests:terrain-normals]" << (ok ? "PASS" : "FAIL") << message;
    };
    auto compare = [&](Grid &grid, float spacing, int pattern) {
        grid.fill(pattern, spacing);
        const bool uniform = TerrainNormals::uniformCoordinates(grid.n, spacing);
        bool equal = true, packedEqual = true;
        double largest = 0;
        for (int z = 0; z <= grid.n; ++z) for (int x = 0; x <= grid.n; ++x) {
            const auto a = meshNormal(grid.rows.data(), grid.n, spacing, x, z);
            const auto b = TerrainNormals::calculate(grid.rows.data(), grid.n, spacing, x, z, uniform);
            equal &= a.x == b.x && a.y == b.y && a.z == b.z;
            packedEqual &= packed(a, false, false) == packed(b, false, true)
                    && packed(a, true, false) == packed(b, true, true);
            largest = std::max({largest, double(std::abs(a.x - b.x)), double(std::abs(a.y - b.y)),
                                double(std::abs(a.z - b.z))});
            ++checked;
        }
        check(equal && packedEqual, QString("N=%1 S=%2 pattern=%3 float_exact=%4 packed_exact=%5 max_error=%6")
              .arg(grid.n).arg(spacing).arg(pattern).arg(equal).arg(packedEqual).arg(largest));
    };
    Grid small(32);
    for (float spacing : {0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 256.0f, 1024.0f, 65536.0f})
        for (int pattern = 0; pattern < 7; ++pattern) compare(small, spacing, pattern);
    for (float spacing : {1.3f, 3.0f, 7.0f, 24.0f, 257.0f})
        for (int pattern = 0; pattern < 7; ++pattern) compare(small, spacing, pattern);
    for (int n : {1, 2, 16, 128, 256, 512, 1024, 2048}) {
        Grid grid(n);
        for (int pattern : {1, 2, 6}) compare(grid, 2048.0f / n, pattern);
    }
    check(!TerrainNormals::uniformCoordinates(256, 24) && !TerrainNormals::uniformCoordinates(0, 1)
          && !TerrainNormals::uniformCoordinates(4096, 1)
          && TerrainNormals::uniformCoordinates(2048, 1), "nonuniform geometry uses coordinate-aware formulas");
    bool packingEqual = true;
    auto comparePacking = [&](float v) {
        for (bool gap : {false, true})
            packingEqual &= expectedPack(v, -v, v, gap)
                    == TerrainNormals::packNormal(v, -v, v, gap);
    };
    for (int i = -511; i < 511; ++i) {
        const float tie = (float(i) + 0.5f) / 511.0f;
        comparePacking(tie);
        comparePacking(std::nextafter(tie, -std::numeric_limits<float>::infinity()));
        comparePacking(std::nextafter(tie, std::numeric_limits<float>::infinity()));
    }
    check(packingEqual, "packing half ties and adjacent floats are exact");
    uint32_t rng = 1;
    for (int i = 0; i < 1000000; ++i) {
        rng = rng * 1664525u + 1013904223u;
        float v; std::memcpy(&v, &rng, sizeof(v));
        comparePacking(v);
    }
    for (float v : {0.0f, -0.0f, 1.0f, -1.0f, std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        comparePacking(v);
    check(packingEqual, "packing million float bit patterns, clamps and non-finite values");
    qInfo() << "[tests:terrain-normals] cases=" << passed + failed << "passed=" << passed
            << "failed=" << failed << "compared_vertices=" << checked;
    return failed ? 1 : 0;
}

int TsreTests::runTerrainNormalBenchmark(const TestRunOptions &opts) {
    if (QOpenGLContext::currentContext()) return 2;
    QScopedValueRollback<QString> root(Game::root), route(Game::route);
    QScopedValueRollback<bool> write(Game::writeEnabled, false), gui(Game::gui, false);
    const bool debug = QLoggingCategory::defaultCategory()->isDebugEnabled();
    QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, false);
    const auto loggingGuard = qScopeGuard([&] { QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, debug); });
    BrushTestLibrary library;
    QScopedValueRollback<TerrainLib*> active(Game::terrainLib, &library);
    Grid grid(2048);
    grid.fill(2, 1);
    float **rows = grid.rows.data();
    QString source = "synthetic_2048_rough";
    if (!opts.casesFile.isEmpty()) {
        QDir directory(QDir::cleanPath(opts.casesFile));
        const auto trks = directory.entryList({"*.trk"}, QDir::Files);
        if (trks.size() != 1) return 2;
        QDir rootDir(directory); rootDir.cdUp(); rootDir.cdUp();
        Game::root = rootDir.absolutePath(); Game::route = directory.dirName();
        Trk trk; trk.load(directory.absoluteFilePath(trks.first()));
        library.loadQuadTree();
        Terrain *t = library.getTerrainByXY(trk.startTileX, -trk.startTileZ, true);
        if (!t || !t->loaded || t->getSampleCount() != 2048 || t->getSampleSize() != 1
                || t->getGridLayout().patchesPerSide != 32) return 2;
        rows = t->terrainData;
        source = directory.dirName() + "/" + t->name;
    }
    bool valid = true;
    // Same 16x16-patch work footprint as the user's larger clicks; includes
    // duplicate patch-edge vertices. Also exercise whole-tile outer boundaries.
    for (int patchesAcross : {16, 32}) {
        const int firstPatch = patchesAcross == 16 ? 8 : 0;
        const int count = patchesAcross * patchesAcross * 65 * 65;
        std::vector<uint32_t> output(count), expected;
        auto run = [&](bool oracle) {
            int i = 0;
            for (int pz = firstPatch; pz < firstPatch + patchesAcross; ++pz)
                for (int px = firstPatch; px < firstPatch + patchesAcross; ++px)
                    for (int z = 0; z <= 64; ++z) for (int x = 0; x <= 64; ++x) {
                        const auto normal = oracle ? meshNormal(rows, 2048, 1, px * 64 + x, pz * 64 + z)
                            : TerrainNormals::calculate(rows, 2048, 1, px * 64 + x, pz * 64 + z, true);
                        output[i++] = packed(normal, false, !oracle);
                    }
        };
        run(true); expected = output; // independent oracle, not timed
        std::vector<double> times;
        for (int iteration = -3; iteration < 20; ++iteration) {
            QElapsedTimer timer; timer.start();
            run(false);
            const double ms = timer.nsecsElapsed() / 1e6;
            if (iteration >= 0) times.push_back(ms);
            valid &= output == expected; // outside timing, consumes every result
        }
        const double mean = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
        std::sort(times.begin(), times.end());
        qInfo().noquote() << QString("[benchmark:terrain-normals] source=%1 patches=%2 vertices=%3 method=current "
            "runs=20 mean_ms=%4 median_ms=%5 p95_ms=%6 min_ms=%7 max_ms=%8 exact_packed_parity=%9 scope=normal+pack+output no_gl=true")
            .arg(source).arg(patchesAcross * patchesAcross).arg(count).arg(mean, 0, 'f', 3)
            .arg((times[9] + times[10]) * 0.5, 0, 'f', 3).arg(times[18], 0, 'f', 3)
            .arg(times.front(), 0, 'f', 3).arg(times.back(), 0, 'f', 3).arg(valid);
    }
    return valid ? 0 : 1;
}
