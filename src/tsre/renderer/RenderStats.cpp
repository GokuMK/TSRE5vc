/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/renderer/RenderStats.h>
#include <tsre/renderer/Renderer.h>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QVector>

#include <atomic>

#ifndef GL_PRIMITIVES_GENERATED
#define GL_PRIMITIVES_GENERATED 0x8C87
#endif
#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif

namespace RenderStats {
namespace {

constexpr int PipelineSlots = 4;

struct PendingQuery {
    Phase phase;
    GLuint primitives;
    GLuint samples;
};

struct State {
    bool enabled = false;
    bool inFrame = false;
    Category category = CategoryOther;
    int activePhase = -1;
    FrameStats current;
    FrameStats last[PipelineSlots];
    quint64 frameCounter[PipelineSlots] = {};
    quint64 renderItemsAtStart = 0;
    quint64 matrixClonesAtStart = 0;
    QElapsedTimer timer;

    // Query objects are reused across frames and belong to one GL context.
    QOpenGLContext *queryContext = nullptr;
    QVector<GLuint> queryPool;
    int queryPoolUsed = 0;
    QVector<PendingQuery> pending;
    bool queriesFailed = false;
};

State &state() {
    static State s;
    return s;
}

std::atomic<quint64> renderItemCounter{0};
std::atomic<quint64> matrixCloneCounter{0};

QOpenGLExtraFunctions *queryFunctions() {
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if (context == nullptr)
        return nullptr;
    State &s = state();
    if (s.queryContext != context) {
        // Queries from a destroyed context cannot be released here.
        s.queryContext = context;
        s.queryPool.clear();
        s.queryPoolUsed = 0;
        s.pending.clear();
    }
    return context->extraFunctions();
}

GLuint takeQuery(QOpenGLExtraFunctions *f) {
    State &s = state();
    if (s.queryPoolUsed == s.queryPool.size()) {
        GLuint id = 0;
        f->glGenQueries(1, &id);
        s.queryPool.push_back(id);
    }
    return s.queryPool[s.queryPoolUsed++];
}

}

bool enabled() {
    return state().enabled;
}

void setEnabled(bool enabled) {
    state().enabled = enabled;
}

void beginFrame(int pipeline) {
    State &s = state();
    if (!s.enabled)
        return;
    if (pipeline < 0 || pipeline >= PipelineSlots)
        return;
    s.current = FrameStats();
    s.current.pipeline = pipeline;
    s.current.frameIndex = ++s.frameCounter[pipeline];
    s.renderItemsAtStart = renderItemCounter.load(std::memory_order_relaxed);
    s.matrixClonesAtStart = matrixCloneCounter.load(std::memory_order_relaxed);
    s.activePhase = -1;
    s.queryPoolUsed = 0;
    s.pending.clear();
    s.queriesFailed = false;
    s.inFrame = true;
    s.timer.start();
}

void endFrame() {
    State &s = state();
    if (!s.inFrame)
        return;
    if (s.activePhase >= 0)
        endPhase(static_cast<Phase>(s.activePhase));
    s.inFrame = false;

    FrameStats &frame = s.current;
    frame.renderItemsCreated = renderItemCounter.load(std::memory_order_relaxed)
            - s.renderItemsAtStart;
    frame.matrixClones = matrixCloneCounter.load(std::memory_order_relaxed)
            - s.matrixClonesAtStart;

    QOpenGLExtraFunctions *f = queryFunctions();
    frame.gpuQueriesValid = f != nullptr && !s.queriesFailed;
    if (frame.gpuQueriesValid) {
        for (const PendingQuery &query : s.pending) {
            GLuint primitives = 0;
            GLuint samples = 0;
            f->glGetQueryObjectuiv(query.primitives, GL_QUERY_RESULT, &primitives);
            f->glGetQueryObjectuiv(query.samples, GL_QUERY_RESULT, &samples);
            frame.phases[query.phase].primitives += primitives;
            frame.phases[query.phase].samples += samples;
        }
    }
    s.pending.clear();
    frame.cpuMs = s.timer.nsecsElapsed() / 1000000.0;
    s.last[frame.pipeline] = frame;
}

bool inFrame() {
    return state().inFrame;
}

void beginPhase(Phase phase) {
    State &s = state();
    if (!s.inFrame || phase < 0 || phase >= PhaseCount)
        return;
    // GL does not allow nested queries of one target; keep the outer phase.
    if (s.activePhase >= 0)
        return;
    QOpenGLExtraFunctions *f = queryFunctions();
    if (f == nullptr) {
        s.queriesFailed = true;
        return;
    }
    PendingQuery query;
    query.phase = phase;
    query.primitives = takeQuery(f);
    query.samples = takeQuery(f);
    f->glBeginQuery(GL_PRIMITIVES_GENERATED, query.primitives);
    f->glBeginQuery(GL_SAMPLES_PASSED, query.samples);
    s.pending.push_back(query);
    s.activePhase = phase;
}

void endPhase(Phase phase) {
    State &s = state();
    if (!s.inFrame || s.activePhase != phase)
        return;
    QOpenGLExtraFunctions *f = queryFunctions();
    if (f == nullptr) {
        s.queriesFailed = true;
    } else {
        f->glEndQuery(GL_SAMPLES_PASSED);
        f->glEndQuery(GL_PRIMITIVES_GENERATED);
    }
    s.activePhase = -1;
}

void setCategory(Category category) {
    state().category = category;
}

Category category() {
    return state().category;
}

FrameStats &current() {
    return state().current;
}

FrameStats lastFrame(int pipeline) {
    if (pipeline < 0 || pipeline >= PipelineSlots)
        return FrameStats();
    return state().last[pipeline];
}

void countRenderItem() {
    renderItemCounter.fetch_add(1, std::memory_order_relaxed);
}

void countMatrixClone() {
    matrixCloneCounter.fetch_add(1, std::memory_order_relaxed);
}

void countDraw(Category category, unsigned int glPrimitive, unsigned int vertexCount) {
    State &s = state();
    if (!s.inFrame)
        return;
    if (category < 0 || category >= CategoryCount)
        category = CategoryOther;
    quint64 primitives = vertexCount;
    if (glPrimitive == GL_TRIANGLES)
        primitives = vertexCount / 3;
    else if (glPrimitive == GL_LINES)
        primitives = vertexCount / 2;
    else if (glPrimitive == GL_LINE_STRIP)
        primitives = vertexCount > 0 ? vertexCount - 1 : 0;
    else if (glPrimitive == GL_TRIANGLE_STRIP || glPrimitive == GL_TRIANGLE_FAN)
        primitives = vertexCount > 2 ? vertexCount - 2 : 0;
    s.current.drawCalls++;
    s.current.categories[category].draws++;
    s.current.categories[category].primitives += primitives;
}

PhaseCounters sceneTotal(const FrameStats &stats) {
    PhaseCounters total;
    for (Phase phase : {PhaseScene, PhaseSceneTerrain, PhaseSceneWorld, PhaseSceneWater}) {
        total.primitives += stats.phases[phase].primitives;
        total.samples += stats.phases[phase].samples;
    }
    return total;
}

void countPassDraw(int pass) {
    State &s = state();
    if (!s.inFrame || pass < 0 || pass >= FrameStats::PassSlots)
        return;
    s.current.passDraws[pass]++;
}

const char *phaseName(Phase phase) {
    switch (phase) {
    case PhaseShadow: return "shadow";
    case PhaseSky: return "sky";
    case PhaseDistant: return "distant";
    case PhaseScene: return "scene";
    case PhaseUi: return "ui";
    case PhaseSceneTerrain: return "sceneTerrain";
    case PhaseSceneWorld: return "sceneWorld";
    case PhaseSceneWater: return "sceneWater";
    default: return "unknown";
    }
}

const char *categoryName(Category category) {
    switch (category) {
    case CategoryOther: return "other";
    case CategoryTerrain: return "terrain";
    case CategoryWorld: return "world";
    case CategoryOverlay: return "overlay";
    default: return "unknown";
    }
}

QJsonObject toJson(const FrameStats &stats) {
    QJsonObject phases;
    for (int i = 0; i < PhaseCount; ++i) {
        QJsonObject phase;
        phase["primitives"] = double(stats.phases[i].primitives);
        phase["samples"] = double(stats.phases[i].samples);
        phases[phaseName(static_cast<Phase>(i))] = phase;
    }
    QJsonObject categories;
    for (int i = 0; i < CategoryCount; ++i) {
        QJsonObject category;
        category["items"] = double(stats.categories[i].items);
        category["draws"] = double(stats.categories[i].draws);
        category["primitives"] = double(stats.categories[i].primitives);
        categories[categoryName(static_cast<Category>(i))] = category;
    }
    QJsonObject json;
    json["pipeline"] = stats.pipeline;
    json["frameIndex"] = double(stats.frameIndex);
    json["cpuMs"] = stats.cpuMs;
    json["gpuQueriesValid"] = stats.gpuQueriesValid;
    const PhaseCounters scene = sceneTotal(stats);
    phases["sceneTotal"] = QJsonObject{{"primitives", double(scene.primitives)},
                                       {"samples", double(scene.samples)}};
    json["phases"] = phases;
    json["categories"] = categories;
    json["renderItemsCreated"] = double(stats.renderItemsCreated);
    json["matrixClones"] = double(stats.matrixClones);
    json["queuedItems"] = double(stats.queuedItems);
    json["groupedPackets"] = double(stats.groupedPackets);
    json["groupedInstances"] = double(stats.groupedInstances);
    json["textureGroups"] = double(stats.textureGroups);
    json["drawCalls"] = double(stats.drawCalls);
    json["flushes"] = double(stats.flushes);
    static_assert(Renderer::PASS_COUNT <= FrameStats::PassSlots, "pass slots");
    // Shadow cascades follow the render passes.
    static const char *passNames[FrameStats::PassSlots] = {
        "sky", "distant", "terrain", "opaque", "alphaTest", "blended",
        "overlay", "water", "ui", "shadow0", "shadow1", "shadow2"};
    static_assert(Renderer::PASS_COUNT == 9, "pass names");
    QJsonObject passDraws;
    for (int i = 0; i < FrameStats::PassSlots; ++i)
        passDraws[passNames[i]] = double(stats.passDraws[i]);
    json["passDraws"] = passDraws;
    return json;
}

}
