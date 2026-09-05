#ifndef TERRAINBRUSHPROFILER_H
#define TERRAINBRUSHPROFILER_H

#include <QElapsedTimer>
#include <QString>
#include <array>

// Opt-in CPU wall-time diagnostics: TSRE_TERRAIN_BRUSH_PROFILE=1.
// Nested timings overlap; no GPU fences, glFinish or per-vertex timers.
namespace TerrainBrushProfiler {
enum Metric { Paint, Undo, Bounds, Mesh, Edges, Build, Vertex, Normals, Upload, Objects, MetricCount };
enum Counter { Snapshots, Patches, Vertices, UploadCalls, UploadBytes,
               Deferred, LegacyRefresh, CounterCount };
struct Stats {
    std::array<qint64, MetricCount> ns{};
    std::array<quint64, CounterCount> counts{};
};
bool enabled();
Stats *active();
void add(Counter counter, quint64 value = 1);

class Scope {
    Stats *stats;
    Metric metric;
    QElapsedTimer timer;
public:
    explicit Scope(Metric value);
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

// Nested events contribute to their outer event. Standalone render-time mesh
// refreshes get separate records, associated with the latest brush ID as a hint.
class Event {
    Stats stats;
    QElapsedTimer timer;
    QString kind, context;
    quint64 id = 0, brushId = 0;
public:
    Event(const QString &kind, const QString &context, bool hasWork = true);
    ~Event();
    Event(const Event &) = delete;
    Event &operator=(const Event &) = delete;
};
}
#endif
