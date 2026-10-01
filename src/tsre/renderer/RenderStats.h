/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef RENDERSTATS_H
#define RENDERSTATS_H

#include <QtGlobal>
#include <QJsonObject>

// Per-frame renderer measurements used to compare the legacy and gather
// pipelines. GPU phase counters come from GL queries and work for both
// pipelines; queue counters describe the gather renderer only.
namespace RenderStats {

// Phases that run in the same order in both pipelines.
enum Phase {
    PhaseShadow = 0,
    PhaseSky,
    PhaseDistant,
    PhaseScene,
    PhaseUi,
    // Legacy draws the scene in sequential sections; it records these instead
    // of PhaseScene. sceneTotal() sums them for comparison with gather.
    PhaseSceneTerrain,
    PhaseSceneWorld,
    PhaseSceneWater,
    PhaseCount
};

// Producer groups for gathered items.
enum Category {
    CategoryOther = 0,
    CategoryTerrain,
    CategoryWorld,
    CategoryOverlay,
    CategoryCount
};

struct PhaseCounters {
    quint64 primitives = 0;
    quint64 samples = 0;
};

struct CategoryCounters {
    quint64 items = 0;
    quint64 draws = 0;
    quint64 primitives = 0;
};

struct FrameStats {
    int pipeline = -1;
    quint64 frameIndex = 0;
    double cpuMs = 0.0;
    bool gpuQueriesValid = false;
    PhaseCounters phases[PhaseCount];
    CategoryCounters categories[CategoryCount];

    // Heap churn during the frame.
    quint64 renderItemsCreated = 0;
    quint64 matrixClones = 0;

    // Gather queue shape.
    quint64 queuedItems = 0;
    quint64 groupedPackets = 0;
    quint64 groupedInstances = 0;
    quint64 textureGroups = 0;
    quint64 drawCalls = 0;
    quint64 flushes = 0;
    // Draw calls per Renderer::RenderPass.
    static constexpr int PassSlots = 8;
    quint64 passDraws[PassSlots] = {};
};

bool enabled();
void setEnabled(bool enabled);

// Frames are recorded per pipeline; a validation frame records both.
void beginFrame(int pipeline);
void endFrame();
bool inFrame();

void beginPhase(Phase phase);
void endPhase(Phase phase);

void setCategory(Category category);
Category category();

FrameStats &current();
FrameStats lastFrame(int pipeline);

// Cheap process-wide allocation counters, always active.
void countRenderItem();
void countMatrixClone();

void countDraw(Category category, unsigned int glPrimitive, unsigned int vertexCount);
void countPassDraw(int pass);

PhaseCounters sceneTotal(const FrameStats &stats);
const char *phaseName(Phase phase);
const char *categoryName(Category category);
QJsonObject toJson(const FrameStats &stats);

// Scoped helpers keep begin/end pairs balanced across early returns.
class ScopedFrame {
public:
    ScopedFrame(int pipeline, bool record) : active(record && enabled() && !inFrame()) {
        if (active)
            beginFrame(pipeline);
    }
    ~ScopedFrame() {
        if (active)
            endFrame();
    }
private:
    bool active;
};

class ScopedPhase {
public:
    explicit ScopedPhase(Phase phase) : phase(phase) { beginPhase(phase); }
    ~ScopedPhase() { endPhase(phase); }
private:
    Phase phase;
};

class ScopedCategory {
public:
    explicit ScopedCategory(Category next) : previous(category()) { setCategory(next); }
    ~ScopedCategory() { setCategory(previous); }
private:
    Category previous;
};

}

#endif /* RENDERSTATS_H */
