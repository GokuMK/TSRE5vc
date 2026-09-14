/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/tests/ProceduralProfileBenchmark.h>

#include <tsre/tests/TestRunner.h>

#include <algorithm>
#include <cmath>

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSet>
#include <QVector>

#include <tsre/Game.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/procedural/OrtsTrackProfileRenderer.h>
#include <tsre/procedural/ProceduralShape.h>
#include <tsre/procedural/ShapeTemplates.h>
#include <tsre/tdb/TSection.h>

namespace {

constexpr int BenchmarkRuns = 12;
constexpr int StressRuns = 400;

void discardShape(QVector<OglObj*> &shape) {
    for(OglObj *object : shape){
        if(object == nullptr)
            continue;
        object->deleteVBO();
        delete object;
    }
    shape.clear();
}

void silentMessageHandler(QtMsgType, const QMessageLogContext &, const QString &) {
}

struct TimingSummary {
    double coldMs = 0;
    double minimumMs = 0;
    double medianMs = 0;
    double averageMs = 0;
    int objects = 0;
    int vertices = -1;
};

TimingSummary summarize(const QVector<double> &samples, double coldMs,
                        int objects, int vertices = -1) {
    TimingSummary result;
    result.coldMs = coldMs;
    result.objects = objects;
    result.vertices = vertices;
    if(samples.isEmpty())
        return result;

    QVector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    result.minimumMs = sorted.first();
    result.medianMs = sorted[sorted.size() / 2];
    for(double sample : samples)
        result.averageMs += sample;
    result.averageMs /= samples.size();
    return result;
}

void printSummary(const QString &backend, const QString &name,
                  const TimingSummary &summary) {
    QString geometry = QString("objects=%1").arg(summary.objects);
    if(summary.vertices >= 0)
        geometry += QString(" vertices=%1").arg(summary.vertices);
    qInfo().noquote() << QString(
            "[benchmark:procedural-profile] backend=%1 profile=%2 "
            "cold_ms=%3 min_ms=%4 median_ms=%5 average_ms=%6 runs=%7 %8")
            .arg(backend, name)
            .arg(summary.coldMs, 0, 'f', 3)
            .arg(summary.minimumMs, 0, 'f', 3)
            .arg(summary.medianMs, 0, 'f', 3)
            .arg(summary.averageMs, 0, 'f', 3)
            .arg(BenchmarkRuns)
            .arg(geometry);
}

void printPhase(const QString &name, const QString &phase,
                const QVector<double> &samples, int objects, int vertices) {
    const TimingSummary summary = summarize(samples, 0, objects, vertices);
    QVector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    const double p95 = sorted.isEmpty()
            ? 0 : sorted[(sorted.size() - 1) * 95 / 100];
    const double p99 = sorted.isEmpty()
            ? 0 : sorted[(sorted.size() - 1) * 99 / 100];
    const double maximum = sorted.isEmpty() ? 0 : sorted.last();
    int above50 = 0;
    int above100 = 0;
    int above200 = 0;
    for(double sample : samples){
        if(sample >= 50)
            above50++;
        if(sample >= 100)
            above100++;
        if(sample >= 200)
            above200++;
    }
    qInfo().noquote() << QString(
            "[benchmark:procedural-profile-phase] profile=%1 phase=%2 "
            "min_ms=%3 median_ms=%4 average_ms=%5 p95_ms=%6 p99_ms=%7 "
            "max_ms=%8 runs=%9 objects=%10 vertices=%11 "
            "over50=%12 over100=%13 over200=%14")
            .arg(name, phase)
            .arg(summary.minimumMs, 0, 'f', 3)
            .arg(summary.medianMs, 0, 'f', 3)
            .arg(summary.averageMs, 0, 'f', 3)
            .arg(p95, 0, 'f', 3)
            .arg(p99, 0, 'f', 3)
            .arg(maximum, 0, 'f', 3)
            .arg(samples.size())
            .arg(objects)
            .arg(vertices)
            .arg(above50)
            .arg(above100)
            .arg(above200);
}

void discardProceduralCache() {
    QSet<OglObj*> objects;
    for(const QVector<OglObj*> &shape : ProceduralShape::Shapes)
        for(OglObj *object : shape)
            objects.insert(object);
    ProceduralShape::Shapes.clear();
    QVector<OglObj*> owned = objects.values().toVector();
    discardShape(owned);
}

} // namespace

int TsreTests::runProceduralProfileBenchmark(const TestRunOptions &opts) {
    const QString routePath = QDir::cleanPath(opts.casesFile);
    if(routePath.isEmpty() || !QDir(routePath).exists()){
        qWarning() << "[benchmark:procedural-profile] --test-cases must name"
                      " an existing route directory";
        return 2;
    }

    QDir rootDirectory(routePath);
    const QString routeName = rootDirectory.dirName();
    if(!rootDirectory.cdUp() || !rootDirectory.cdUp()){
        qWarning() << "[benchmark:procedural-profile] cannot derive content root from"
                   << routePath;
        return 2;
    }

    const QString originalRoot = Game::root;
    const QString originalRoute = Game::route;
    Game::root = QDir::cleanPath(rootDirectory.absolutePath());
    Game::route = routeName;

    QOpenGLContext context;
    if(!context.create()){
        Game::root = originalRoot;
        Game::route = originalRoute;
        qWarning() << "[benchmark:procedural-profile] cannot create OpenGL context";
        return 3;
    }
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if(!surface.isValid() || !context.makeCurrent(&surface)){
        Game::root = originalRoot;
        Game::route = originalRoute;
        qWarning() << "[benchmark:procedural-profile] cannot make OpenGL context current";
        return 3;
    }

    ProceduralShape::Load();
    OrtsTrackProfileCatalog::load(routePath, true);

    QVector<TSection> sections;
    sections.append(TSection(0, 0, 100.0f, 0));
    sections.append(TSection(0, 1, 10.0f * (float)M_PI / 180.0f, 1000.0f));

    qInfo().noquote() << QString(
            "[benchmark:procedural-profile] route=%1 sections=straight:100m,curve:1000m/10deg")
            .arg(routePath);

    const QStringList nativeNames = {"DefaultTrack", "DefaultTrack3"};
    const QStringList ortsNames = {"TrProfile_DB1", "TrProfile_SR_w"};
    bool complete = true;

    for(const QString &name : nativeNames){
        if(ProceduralShape::ShapeTemplateFile == nullptr
                || !ProceduralShape::ShapeTemplateFile->templates.contains(name)){
            qWarning() << "[benchmark:procedural-profile] native template missing:"
                       << name;
            complete = false;
            continue;
        }
        if(ProceduralShape::ShapeTemplateFile->templates.value(name)->type
                != ShapeTemplate::TRACK){
            qWarning() << "[benchmark:procedural-profile] native template has"
                          " unexpected Type:"
                       << name;
            complete = false;
            continue;
        }

        QVector<OglObj*> warmShape;
        ProceduralShape::Shapes.clear();
        QtMessageHandler previousHandler =
                qInstallMessageHandler(silentMessageHandler);
        QElapsedTimer coldTimer;
        coldTimer.start();
        ProceduralShape::GetShape(name, warmShape, sections);
        context.functions()->glFinish();
        const double coldMs = coldTimer.nsecsElapsed() / 1000000.0;
        qInstallMessageHandler(previousHandler);
        const int objectCount = warmShape.size();
        ProceduralShape::Shapes.clear();
        discardShape(warmShape);

        QVector<double> samples;
        samples.reserve(BenchmarkRuns);
        previousHandler = qInstallMessageHandler(silentMessageHandler);
        for(int run = 0; run < BenchmarkRuns; run++){
            QVector<OglObj*> shape;
            ProceduralShape::Shapes.clear();
            QElapsedTimer timer;
            timer.start();
            ProceduralShape::GetShape(name, shape, sections);
            context.functions()->glFinish();
            const double elapsedMs = timer.nsecsElapsed() / 1000000.0;
            samples.append(elapsedMs);
            ProceduralShape::Shapes.clear();
            discardShape(shape);
        }
        qInstallMessageHandler(previousHandler);
        printSummary("tsre", name,
                     summarize(samples, coldMs, objectCount));

        QVector<OglObj*> cachedShape;
        ProceduralShape::GetShape(name, cachedShape, sections);
        context.functions()->glFinish();
        QVector<double> cachedSamples;
        cachedSamples.reserve(BenchmarkRuns);
        previousHandler = qInstallMessageHandler(silentMessageHandler);
        for(int run = 0; run < BenchmarkRuns; run++){
            QVector<OglObj*> lookup;
            QElapsedTimer timer;
            timer.start();
            ProceduralShape::GetShape(name, lookup, sections);
            cachedSamples.append(timer.nsecsElapsed() / 1000000.0);
        }
        qInstallMessageHandler(previousHandler);
        const TimingSummary cached = summarize(cachedSamples,
                                               cachedSamples.first(),
                                               objectCount);
        printSummary("tsre-cache", name, cached);
        ProceduralShape::Shapes.clear();
        discardShape(cachedShape);
    }

    auto testNativeWheelCache = [&](float heightStep,
                                    const QString &label) {
        constexpr int updates = 25;
        const float pathLength = 100.0f
                + 1000.0f * 10.0f * (float)M_PI / 180.0f;
        discardProceduralCache();
        int cacheHits = 0;
        QtMessageHandler previousHandler =
                qInstallMessageHandler(silentMessageHandler);
        for(int update = 0; update < updates; update++){
            const float height = update * heightStep;
            const float extraPlanarLength =
                    std::hypot(pathLength, height) - pathLength;
            QVector<TSection> elevatedSections = sections;
            elevatedSections[0] = TSection(
                    0, 0, 100.0f + extraPlanarLength, 0);
            const int before = ProceduralShape::Shapes.size();
            QVector<OglObj*> shape;
            ProceduralShape::GetShape(
                    "DefaultTrack", shape, elevatedSections);
            if(ProceduralShape::Shapes.size() == before)
                cacheHits++;
        }
        context.functions()->glFinish();
        qInstallMessageHandler(previousHandler);
        qInfo().noquote() << QString(
                "[benchmark:procedural-profile-wheel-cache] step=%1 "
                "updates=%2 unique_meshes=%3 cache_hits=%4")
                .arg(label)
                .arg(updates)
                .arg(ProceduralShape::Shapes.size())
                .arg(cacheHits);
        discardProceduralCache();
    };
    testNativeWheelCache(1.2f, "1.2m");
    testNativeWheelCache(0.12f, "0.12m");

    for(const QString &name : ortsNames){
        const QSharedPointer<const OrtsTrackProfile> profile =
                OrtsTrackProfileCatalog::find(name);
        if(profile == nullptr){
            qWarning() << "[benchmark:procedural-profile] ORTS profile missing:"
                       << name;
            complete = false;
            continue;
        }

        QVector<OrtsGeneratedProfileMesh> meshInfo;
        OrtsTrackProfileRenderer::buildMeshes(*profile, sections, meshInfo);
        int vertexCount = 0;
        for(const OrtsGeneratedProfileMesh &mesh : meshInfo)
            vertexCount += mesh.vertices.size() / 9;

        QVector<double> cpuSamples;
        cpuSamples.reserve(BenchmarkRuns);
        for(int run = 0; run < BenchmarkRuns; run++){
            QElapsedTimer timer;
            timer.start();
            {
                QVector<OrtsGeneratedProfileMesh> meshes;
                OrtsTrackProfileRenderer::buildMeshes(
                        *profile, sections, meshes);
            }
            cpuSamples.append(timer.nsecsElapsed() / 1000000.0);
        }
        printPhase(profile->id, "cpu-build", cpuSamples,
                   meshInfo.size(), vertexCount);

        QVector<OglObj*> warmShape;
        QElapsedTimer coldTimer;
        coldTimer.start();
        OrtsTrackProfileRenderer::generate(
                *profile, sections, warmShape, routePath);
        context.functions()->glFinish();
        const double coldMs = coldTimer.nsecsElapsed() / 1000000.0;
        const int objectCount = warmShape.size();
        discardShape(warmShape);

        QVector<double> samples;
        samples.reserve(BenchmarkRuns);
        for(int run = 0; run < BenchmarkRuns; run++){
            QVector<OglObj*> shape;
            QElapsedTimer timer;
            timer.start();
            OrtsTrackProfileRenderer::generate(
                    *profile, sections, shape, routePath);
            context.functions()->glFinish();
            const double elapsedMs = timer.nsecsElapsed() / 1000000.0;
            samples.append(elapsedMs);
            discardShape(shape);
        }
        printSummary("orts", profile->id,
                     summarize(samples, coldMs, objectCount, vertexCount));

        QVector<double> destroySamples;
        destroySamples.reserve(BenchmarkRuns);
        for(int run = 0; run < BenchmarkRuns; run++){
            QVector<OglObj*> shape;
            OrtsTrackProfileRenderer::generate(
                    *profile, sections, shape, routePath);
            context.functions()->glFinish();
            QElapsedTimer timer;
            timer.start();
            discardShape(shape);
            context.functions()->glFinish();
            destroySamples.append(timer.nsecsElapsed() / 1000000.0);
        }
        printPhase(profile->id, "destroy-after-finish", destroySamples,
                   objectCount, vertexCount);

        QVector<OglObj*> replacementShape;
        OrtsTrackProfileRenderer::generate(
                *profile, sections, replacementShape, routePath);
        context.functions()->glFinish();
        QVector<double> replacementSamples;
        replacementSamples.reserve(BenchmarkRuns);
        for(int run = 0; run < BenchmarkRuns; run++){
            QElapsedTimer timer;
            timer.start();
            discardShape(replacementShape);
            OrtsTrackProfileRenderer::generate(
                    *profile, sections, replacementShape, routePath);
            context.functions()->glFinish();
            replacementSamples.append(timer.nsecsElapsed() / 1000000.0);
        }
        printPhase(profile->id, "replace-one", replacementSamples,
                   objectCount, vertexCount);
        discardShape(replacementShape);

        QVector<QVector<OglObj*>> companionShapes(3);
        for(QVector<OglObj*> &shape : companionShapes)
            OrtsTrackProfileRenderer::generate(
                    *profile, sections, shape, routePath);
        context.functions()->glFinish();
        QVector<double> companionSamples;
        companionSamples.reserve(BenchmarkRuns);
        for(int run = 0; run < BenchmarkRuns; run++){
            QElapsedTimer timer;
            timer.start();
            for(QVector<OglObj*> &shape : companionShapes){
                discardShape(shape);
                OrtsTrackProfileRenderer::generate(
                        *profile, sections, shape, routePath);
            }
            context.functions()->glFinish();
            companionSamples.append(timer.nsecsElapsed() / 1000000.0);
        }
        printPhase(profile->id, "replace-three", companionSamples,
                   objectCount * 3, vertexCount * 3);
        for(QVector<OglObj*> &shape : companionShapes)
            discardShape(shape);

        if(name.compare("TrProfile_DB1", Qt::CaseInsensitive) == 0){
            QVector<QVector<OglObj*>> stressShapes(3);
            for(QVector<OglObj*> &shape : stressShapes)
                OrtsTrackProfileRenderer::generate(
                        *profile, sections, shape, routePath);
            context.functions()->glFlush();
            QVector<double> stressSamples;
            stressSamples.reserve(StressRuns);
            for(int run = 0; run < StressRuns; run++){
                QElapsedTimer timer;
                timer.start();
                for(QVector<OglObj*> &shape : stressShapes){
                    discardShape(shape);
                    OrtsTrackProfileRenderer::generate(
                            *profile, sections, shape, routePath);
                }
                context.functions()->glFlush();
                stressSamples.append(timer.nsecsElapsed() / 1000000.0);
            }
            context.functions()->glFinish();
            printPhase(profile->id, "replace-three-stress", stressSamples,
                       objectCount * 3, vertexCount * 3);
            for(QVector<OglObj*> &shape : stressShapes)
                discardShape(shape);
        }
    }

    context.doneCurrent();
    Game::root = originalRoot;
    Game::route = originalRoute;
    return complete ? 0 : 1;
}
