#include <tsre/world/TerrainBrushProfiler.h>
#include <QDebug>

namespace {
thread_local TerrainBrushProfiler::Stats *current = nullptr;
thread_local quint64 sequence = 0, latestBrush = 0;
}
bool TerrainBrushProfiler::enabled() {
    static const bool value = qEnvironmentVariableIntValue("TSRE_TERRAIN_BRUSH_PROFILE") != 0;
    return value;
}
TerrainBrushProfiler::Stats *TerrainBrushProfiler::active() { return current; }
void TerrainBrushProfiler::add(Counter counter, quint64 value) {
    if (current) current->counts[counter] += value;
}
TerrainBrushProfiler::Scope::Scope(Metric value) : stats(current), metric(value) {
    if (stats) timer.start();
}
TerrainBrushProfiler::Scope::~Scope() {
    if (stats) stats->ns[metric] += timer.nsecsElapsed();
}
TerrainBrushProfiler::Event::Event(const QString &eventKind, const QString &eventContext, bool hasWork) {
    if (!hasWork || !enabled() || current) return;
    kind = eventKind; context = eventContext;
    id = ++sequence;
    if (kind == "brush") latestBrush = id;
    brushId = latestBrush;
    current = &stats;
    timer.start();
}
TerrainBrushProfiler::Event::~Event() {
    if (!id) return;
    const double totalMs = timer.nsecsElapsed() / 1e6;
    current = nullptr; // formatting/file logging is outside the measured event
    QString line = QString("[terrain-brush-profile] id=%1 kind=%2 after_brush=%3 %4 total_ms=%5")
            .arg(id).arg(kind).arg(brushId).arg(context).arg(totalMs, 0, 'f', 3);
    static const char *names[] = {"paint", "undo", "bounds", "mesh", "edges", "build", "vertex", "normals", "upload", "objects"};
    for (int i = 0; i < MetricCount; ++i)
        line += QString(" %1_ms=%2").arg(names[i]).arg(stats.ns[i] / 1e6, 0, 'f', 3);
    static const char *counts[] = {"snapshots", "patches", "vertices", "upload_calls", "upload_bytes", "deferred", "legacy_refresh"};
    for (int i = 0; i < CounterCount; ++i)
        line += QString(" %1=%2").arg(counts[i]).arg(stats.counts[i]);
    qInfo().noquote() << line;
}
