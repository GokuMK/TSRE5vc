/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tests/TerrainRawBenchmark.h>

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>

#include <tsre/Game.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainGridLayout.h>
#include <tsre/world/TerrainInfo.h>
#include <tsre/world/TFile.h>

namespace {

#pragma pack(push, 1)
struct TerrainVertex8RawBenchmark {
    quint16 rawHeight;
    quint8 localSampleX;
    quint8 localSampleZ;
    quint32 packedNormal;
};
#pragma pack(pop)

static_assert(sizeof(TerrainVertex8RawBenchmark) == 8,
              "Stage 2 raw-height candidate must remain exactly 8 bytes");

struct PatchHeightEncoding {
    float floor;
    float scale;
};

struct TimingSummary {
    double firstMs = 0.0;
    double averageMs = 0.0;
    double medianMs = 0.0;
    double minimumMs = 0.0;
    double p95Ms = 0.0;
    double maximumMs = 0.0;
    quint64 checksum = 0;
};

struct GameTerrainPathGuard {
    QString root = Game::root;
    QString route = Game::route;
    Game::TerrainMeshMode meshMode = Game::terrainMeshMode;

    ~GameTerrainPathGuard() {
        Game::root = root;
        Game::route = route;
        Game::terrainMeshMode = meshMode;
    }
};

#if defined(_MSC_VER)
#define TSRE_BENCHMARK_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define TSRE_BENCHMARK_NOINLINE __attribute__((noinline))
#else
#define TSRE_BENCHMARK_NOINLINE
#endif

static QString find1024Descriptor(const QString &inputPath) {
    const QFileInfo inputInfo(inputPath);
    QStringList candidates;
    if (inputInfo.isFile()) {
        candidates.append(inputInfo.absoluteFilePath());
    } else if (inputInfo.isDir()) {
        QDirIterator iterator(inputInfo.absoluteFilePath(),
                              QStringList() << "*.t", QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = QDir::cleanPath(iterator.next());
            const QString directory = QFileInfo(path).dir().dirName();
            if (directory.compare("tiles", Qt::CaseInsensitive) == 0
                    || directory.compare("lo_tiles", Qt::CaseInsensitive) == 0)
                candidates.append(path);
        }
        candidates.sort(Qt::CaseInsensitive);
    }

    for (const QString &candidate : candidates) {
        TFile descriptor;
        if (descriptor.readT(candidate)
                && descriptor.nsamples != nullptr
                && *descriptor.nsamples == 1024)
            return candidate;
    }
    return QString();
}

static bool configureTerrainPath(const QString &descriptorPath,
                                 TerrainInfo &info, QString &error) {
    const QFileInfo descriptorInfo(descriptorPath);
    QDir tileDirectory = descriptorInfo.dir();
    const QString tileDirectoryName = tileDirectory.dirName();
    info.low = tileDirectoryName.compare("lo_tiles", Qt::CaseInsensitive) == 0;
    info.name = descriptorInfo.completeBaseName();

    if (!tileDirectory.cdUp()) {
        error = "terrain descriptor has no route parent directory";
        return false;
    }
    Game::route = tileDirectory.dirName();
    if (!tileDirectory.cdUp() || !tileDirectory.cdUp()) {
        error = "terrain descriptor is not below a routes directory";
        return false;
    }
    Game::root = QDir::cleanPath(tileDirectory.absolutePath());
    Game::terrainMeshMode = Game::TERRAIN_MESH_PAGED;
    return true;
}

static TSRE_BENCHMARK_NOINLINE void generatePatchLocalRaw(
        float **terrainData, const TerrainGridLayout &layout,
        QVector<PatchHeightEncoding> &encodings,
        QVector<TerrainVertex8RawBenchmark> &vertices) {
    const int resolution = layout.patchResolution;
    const int verticesPerPatch = (resolution + 1) * (resolution + 1);
    TerrainVertex8RawBenchmark *output = vertices.data();
    PatchHeightEncoding *encoding = encodings.data();
    for (int patchId = 0; patchId < layout.patchRecordCount(); ++patchId) {
        const int firstX = layout.patchColumn(patchId) * resolution;
        const int firstZ = layout.patchRow(patchId) * resolution;
        float minimum = std::numeric_limits<float>::infinity();
        float maximum = -std::numeric_limits<float>::infinity();
        for (int localZ = 0; localZ <= resolution; ++localZ) {
            const float *row = terrainData[firstZ + localZ] + firstX;
            for (int localX = 0; localX <= resolution; ++localX) {
                minimum = std::min(minimum, row[localX]);
                maximum = std::max(maximum, row[localX]);
            }
        }

        const float range = maximum - minimum;
        const float scale = range > 0.0f ? range / 65535.0f : 0.0f;
        const float inverseScale = scale > 0.0f ? 1.0f / scale : 0.0f;
        encoding[patchId] = {minimum, scale};

        TerrainVertex8RawBenchmark *patchOutput =
                output + patchId * verticesPerPatch;
        int outputIndex = 0;
        for (int localZ = 0; localZ <= resolution; ++localZ) {
            const float *row = terrainData[firstZ + localZ] + firstX;
            for (int localX = 0; localX <= resolution; ++localX) {
                const float converted = (row[localX] - minimum) * inverseScale;
                const int rawHeight = std::clamp(
                        static_cast<int>(converted + 0.5f), 0, 65535);
                TerrainVertex8RawBenchmark &vertex = patchOutput[outputIndex++];
                vertex.rawHeight = static_cast<quint16>(rawHeight);
                vertex.localSampleX = static_cast<quint8>(localX);
                vertex.localSampleZ = static_cast<quint8>(localZ);
            }
        }
    }
}

static double timedGeneration(
        float **terrainData, const TerrainGridLayout &layout,
        QVector<PatchHeightEncoding> &encodings,
        QVector<TerrainVertex8RawBenchmark> &vertices,
        quint64 &checksum) {
    QElapsedTimer timer;
    timer.start();
    generatePatchLocalRaw(terrainData, layout, encodings, vertices);
    const double elapsedMs = timer.nsecsElapsed() / 1000000.0;
    const TerrainVertex8RawBenchmark &first = vertices.front();
    const TerrainVertex8RawBenchmark &middle = vertices[vertices.size() / 2];
    const TerrainVertex8RawBenchmark &last = vertices.back();
    checksum = static_cast<quint64>(first.rawHeight)
            + middle.rawHeight + last.rawHeight
            + first.localSampleX + middle.localSampleZ + last.localSampleX;
    return elapsedMs;
}

static TimingSummary benchmarkCase(
        float **terrainData, const TerrainGridLayout &layout,
        QVector<PatchHeightEncoding> &encodings,
        QVector<TerrainVertex8RawBenchmark> &vertices) {
    constexpr int WarmupRuns = 100;
    constexpr int MeasuredRuns = 100;
    TimingSummary summary;
    summary.firstMs = timedGeneration(
            terrainData, layout, encodings, vertices, summary.checksum);
    for (int i = 0; i < WarmupRuns; ++i)
        timedGeneration(terrainData, layout, encodings, vertices,
                        summary.checksum);

    QVector<double> timings;
    timings.reserve(MeasuredRuns);
    double total = 0.0;
    for (int i = 0; i < MeasuredRuns; ++i) {
        const double elapsed = timedGeneration(
                terrainData, layout, encodings, vertices, summary.checksum);
        timings.append(elapsed);
        total += elapsed;
    }
    std::sort(timings.begin(), timings.end());
    summary.averageMs = total / MeasuredRuns;
    summary.minimumMs = timings.front();
    summary.medianMs = (timings[MeasuredRuns / 2 - 1]
            + timings[MeasuredRuns / 2]) * 0.5;
    summary.p95Ms = timings[94];
    summary.maximumMs = timings.back();
    return summary;
}

static float verifyEncoding(
        float **terrainData, const TerrainGridLayout &layout,
        const QVector<PatchHeightEncoding> &encodings,
        const QVector<TerrainVertex8RawBenchmark> &vertices) {
    const int resolution = layout.patchResolution;
    const int verticesPerPatch = (resolution + 1) * (resolution + 1);
    float maximumError = 0.0f;
    for (int patchId = 0; patchId < layout.patchRecordCount(); ++patchId) {
        const int firstX = layout.patchColumn(patchId) * resolution;
        const int firstZ = layout.patchRow(patchId) * resolution;
        const PatchHeightEncoding encoding = encodings[patchId];
        const TerrainVertex8RawBenchmark *patchVertices =
                vertices.constData() + patchId * verticesPerPatch;
        int vertexIndex = 0;
        for (int localZ = 0; localZ <= resolution; ++localZ) {
            for (int localX = 0; localX <= resolution; ++localX) {
                const TerrainVertex8RawBenchmark &vertex =
                        patchVertices[vertexIndex++];
                const float decoded = encoding.floor
                        + vertex.rawHeight * encoding.scale;
                maximumError = std::max(maximumError, std::fabs(
                        decoded - terrainData[firstZ + localZ][firstX + localX]));
            }
        }
    }
    return maximumError;
}

static void addEditedRelief(float **terrainData,
                            const TerrainGridLayout &layout) {
    for (int z = 0; z <= layout.sampleCount; ++z) {
        for (int x = 0; x <= layout.sampleCount; ++x) {
            terrainData[z][x] += 0.025f * x - 0.0125f * z
                    + 18.0f * std::sin(x * 0.017f)
                    * std::cos(z * 0.013f);
        }
    }
}

static void printSummary(const QString &caseName,
                         const TerrainGridLayout &layout,
                         const TimingSummary &summary,
                         float maximumDecodeError) {
    const qint64 vertexCount = static_cast<qint64>(layout.patchRecordCount())
            * (layout.patchResolution + 1) * (layout.patchResolution + 1);
    const double throughput = vertexCount / (summary.averageMs * 1000.0);
    qInfo().noquote() << QString(
            "[benchmark:terrain-raw] case=%1 first_ms=%2 average_ms=%3 "
            "median_ms=%4 min_ms=%5 p95_ms=%6 max_ms=%7 "
            "throughput_mvertices_s=%8 max_decode_error=%9 checksum=%10")
            .arg(caseName)
            .arg(summary.firstMs, 0, 'f', 3)
            .arg(summary.averageMs, 0, 'f', 3)
            .arg(summary.medianMs, 0, 'f', 3)
            .arg(summary.minimumMs, 0, 'f', 3)
            .arg(summary.p95Ms, 0, 'f', 3)
            .arg(summary.maximumMs, 0, 'f', 3)
            .arg(throughput, 0, 'f', 1)
            .arg(maximumDecodeError, 0, 'g', 6)
            .arg(summary.checksum);
}

} // namespace

int TsreTests::runTerrainRawBenchmark(const TestRunOptions &opts) {
    const QString inputPath = QDir::cleanPath(opts.casesFile);
    if (inputPath.isEmpty() || !QFileInfo::exists(inputPath)) {
        qWarning() << "[benchmark:terrain-raw] --test-cases must name a terrain"
                   << "descriptor or route directory:" << opts.casesFile;
        return 2;
    }

    const QString descriptorPath = find1024Descriptor(inputPath);
    if (descriptorPath.isEmpty()) {
        qWarning() << "[benchmark:terrain-raw] no 1024-sample terrain descriptor"
                   << "found below" << inputPath;
        return 2;
    }

    GameTerrainPathGuard guard;
    TerrainInfo info;
    QString pathError;
    if (!configureTerrainPath(descriptorPath, info, pathError)) {
        qWarning() << "[benchmark:terrain-raw]" << pathError
                   << descriptorPath;
        return 2;
    }

    Terrain terrain(&info);
    if (!terrain.loaded || terrain.terrainData == nullptr) {
        qWarning() << "[benchmark:terrain-raw] failed to load" << descriptorPath;
        return 1;
    }
    const TerrainGridLayout &layout = terrain.getGridLayout();
    if (layout.sampleCount != 1024 || layout.patchResolution > 255) {
        qWarning() << "[benchmark:terrain-raw] unsuitable layout"
                   << "samples=" << layout.sampleCount
                   << "patches=" << layout.patchesPerSide
                   << "patch_resolution=" << layout.patchResolution;
        return 1;
    }

    const int verticesPerPatch =
            (layout.patchResolution + 1) * (layout.patchResolution + 1);
    QVector<PatchHeightEncoding> encodings(layout.patchRecordCount());
    QVector<TerrainVertex8RawBenchmark> vertices(
            layout.patchRecordCount() * verticesPerPatch);
    qInfo().noquote() << QString(
            "[benchmark:terrain-raw] descriptor=%1 samples=%2 patches=%3 "
            "patch_resolution=%4 output_vertices=%5 output_bytes=%6 "
            "warmup_runs=100 measured_runs=100 "
            "scope=patch-range-scan+raw-quantization+local-XZ-writes "
            "excluded=normal-generation+GPU-upload")
            .arg(QDir::toNativeSeparators(descriptorPath))
            .arg(layout.sampleCount)
            .arg(layout.patchesPerSide)
            .arg(layout.patchResolution)
            .arg(vertices.size())
            .arg(static_cast<qint64>(vertices.size())
                 * sizeof(TerrainVertex8RawBenchmark));

    const TimingSummary loadedSummary = benchmarkCase(
            terrain.terrainData, layout, encodings, vertices);
    const float loadedError = verifyEncoding(
            terrain.terrainData, layout, encodings, vertices);
    printSummary("loaded", layout, loadedSummary, loadedError);

    addEditedRelief(terrain.terrainData, layout);
    const TimingSummary editedSummary = benchmarkCase(
            terrain.terrainData, layout, encodings, vertices);
    const float editedError = verifyEncoding(
            terrain.terrainData, layout, encodings, vertices);
    printSummary("edited-relief", layout, editedSummary, editedError);
    return 0;
}
